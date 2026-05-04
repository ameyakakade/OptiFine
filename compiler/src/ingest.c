#include "optifine/ingest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/pb_reader.h"

/* ONNX protobuf field numbers used below, verified empirically against
 * onnx==1.22.0's message descriptors (see export/export_model.py's header
 * comment for how tiny_classifier.onnx is produced and why). Scope: only
 * the fields this project's own authored graphs actually use -- e.g.
 * TensorProto.dims is never read here because the matching Const node's
 * own `shape` attribute is the authoritative source (see meta_node in
 * export_model.py), not general ONNX operator/field coverage. */
#define ONNX_MODEL_GRAPH 7
#define ONNX_GRAPH_NODE 1
#define ONNX_GRAPH_INITIALIZER 5
#define ONNX_NODE_INPUT 1
#define ONNX_NODE_OUTPUT 2
#define ONNX_NODE_OP_TYPE 4
#define ONNX_NODE_ATTRIBUTE 5
#define ONNX_ATTR_NAME 1
#define ONNX_ATTR_F 2
#define ONNX_ATTR_I 3
#define ONNX_ATTR_INTS 8
#define ONNX_TENSOR_NAME 8
#define ONNX_TENSOR_RAW_DATA 9

#define MAX_NODE_INPUTS 2
#define MAX_SHAPE_DIMS 4
#define MAX_NODES 32
#define MAX_INITIALIZERS 8

typedef struct {
    const uint8_t *ptr;
    size_t len;
} Slice;

static int slice_eq(Slice s, const char *cstr) {
    size_t clen = strlen(cstr);
    return s.len == clen && memcmp(s.ptr, cstr, clen) == 0;
}

static int slice_eq_slice(Slice a, Slice b) {
    return a.len == b.len && memcmp(a.ptr, b.ptr, a.len) == 0;
}

typedef struct {
    Slice name;
    Slice raw_data;
} OnnxInitializer;

typedef struct {
    Slice op_type;
    Slice inputs[MAX_NODE_INPUTS];
    size_t num_inputs;
    Slice output;
    float scale;
    int64_t zero_point;
    int64_t dtype;
    int64_t shape[MAX_SHAPE_DIMS];
    size_t shape_len;
} OnnxNode;

typedef struct {
    Slice name;
    size_t op_id;
} NameEntry;

/* Parses one embedded AttributeProto message. Every node in our authored
 * graphs carries exactly four attributes (scale/zero_point/dtype/shape,
 * see export_model.py's meta_node) -- this fills in whichever one `r`
 * names, ignoring AttributeProto fields we don't use (e.g. `type`). */
static int parse_attribute(PbReader r, OnnxNode *node) {
    Slice attr_name = {0};
    int has_f = 0, has_i = 0;
    float f_val = 0;
    int64_t i_val = 0;
    int64_t ints_val[MAX_SHAPE_DIMS];
    size_t ints_len = 0;

    while (!pb_reader_done(&r)) {
        PbField field;
        if (pb_read_field(&r, &field) != 0) {
            return -1;
        }
        switch (field.field_number) {
            case ONNX_ATTR_NAME:
                if (field.wire_type != PB_WIRE_LEN) return -1;
                attr_name.ptr = field.bytes;
                attr_name.len = field.bytes_len;
                break;
            case ONNX_ATTR_F:
                if (field.wire_type != PB_WIRE_FIXED32) return -1;
                f_val = pb_decode_float32(&field);
                has_f = 1;
                break;
            case ONNX_ATTR_I:
                if (field.wire_type != PB_WIRE_VARINT) return -1;
                /* AttributeProto.i is int64 with plain (non-zigzag) varint
                 * encoding; every value this project writes is a small
                 * non-negative int (dtype/zero_point ordinals), so a
                 * direct unsigned-to-signed cast is safe here -- no
                 * general negative-varint decoding is implemented. */
                i_val = (int64_t)field.varint;
                has_i = 1;
                break;
            case ONNX_ATTR_INTS:
                /* Despite being a repeated scalar field, ONNX serializes
                 * `ints` unpacked -- each element is its own separate
                 * (field 8, varint) occurrence, not one length-delimited
                 * field containing concatenated varints. Confirmed against
                 * the real generated file's raw bytes, not assumed from
                 * the proto3-packed-by-default convention (which doesn't
                 * hold here). This case fires once per element. */
                if (field.wire_type != PB_WIRE_VARINT) return -1;
                if (ints_len < MAX_SHAPE_DIMS) {
                    ints_val[ints_len++] = (int64_t)field.varint;
                }
                break;
            default:
                break;
        }
    }

    if (slice_eq(attr_name, "scale") && has_f) {
        node->scale = f_val;
    } else if (slice_eq(attr_name, "zero_point") && has_i) {
        node->zero_point = i_val;
    } else if (slice_eq(attr_name, "dtype") && has_i) {
        node->dtype = i_val;
    } else if (slice_eq(attr_name, "shape")) {
        memcpy(node->shape, ints_val, ints_len * sizeof(int64_t));
        node->shape_len = ints_len;
    }
    return 0;
}

static int parse_node(PbReader r, OnnxNode *node) {
    memset(node, 0, sizeof(*node));
    while (!pb_reader_done(&r)) {
        PbField field;
        if (pb_read_field(&r, &field) != 0) {
            return -1;
        }
        switch (field.field_number) {
            case ONNX_NODE_INPUT:
                if (field.wire_type != PB_WIRE_LEN) return -1;
                if (node->num_inputs < MAX_NODE_INPUTS) {
                    node->inputs[node->num_inputs].ptr = field.bytes;
                    node->inputs[node->num_inputs].len = field.bytes_len;
                    node->num_inputs++;
                }
                break;
            case ONNX_NODE_OUTPUT:
                if (field.wire_type != PB_WIRE_LEN) return -1;
                node->output.ptr = field.bytes;
                node->output.len = field.bytes_len;
                break;
            case ONNX_NODE_OP_TYPE:
                if (field.wire_type != PB_WIRE_LEN) return -1;
                node->op_type.ptr = field.bytes;
                node->op_type.len = field.bytes_len;
                break;
            case ONNX_NODE_ATTRIBUTE: {
                if (field.wire_type != PB_WIRE_LEN) return -1;
                PbReader ar;
                pb_reader_from_field(&ar, &field);
                if (parse_attribute(ar, node) != 0) return -1;
                break;
            }
            default:
                break;
        }
    }
    return 0;
}

static int parse_initializer(PbReader r, OnnxInitializer *init) {
    memset(init, 0, sizeof(*init));
    while (!pb_reader_done(&r)) {
        PbField field;
        if (pb_read_field(&r, &field) != 0) {
            return -1;
        }
        switch (field.field_number) {
            case ONNX_TENSOR_NAME:
                if (field.wire_type != PB_WIRE_LEN) return -1;
                init->name.ptr = field.bytes;
                init->name.len = field.bytes_len;
                break;
            case ONNX_TENSOR_RAW_DATA:
                if (field.wire_type != PB_WIRE_LEN) return -1;
                init->raw_data.ptr = field.bytes;
                init->raw_data.len = field.bytes_len;
                break;
            default:
                break;
        }
    }
    return 0;
}

static OpKind op_kind_from_type(Slice op_type, int *ok) {
    *ok = 1;
    if (slice_eq(op_type, "Input")) return OP_INPUT;
    if (slice_eq(op_type, "Const")) return OP_CONST;
    if (slice_eq(op_type, "MatMul")) return OP_MATMUL;
    if (slice_eq(op_type, "Add")) return OP_ADD;
    if (slice_eq(op_type, "Relu")) return OP_RELU;
    if (slice_eq(op_type, "Requantize")) return OP_REQUANTIZE;
    if (slice_eq(op_type, "Output")) return OP_OUTPUT;
    *ok = 0;
    return OP_INPUT; /* unused; caller checks *ok */
}

int ingest_load_onnx(const char *path, IrGraph *out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        return 1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        return 1;
    }

    uint8_t *buf = malloc((size_t)size);
    size_t nread = fread(buf, 1, (size_t)size, f);
    fclose(f);
    if (nread != (size_t)size) {
        free(buf);
        return 1;
    }

    PbReader model_r;
    pb_reader_init(&model_r, buf, (size_t)size);

    Slice graph_slice = {0};
    int found_graph = 0;
    while (!pb_reader_done(&model_r)) {
        PbField field;
        if (pb_read_field(&model_r, &field) != 0) {
            free(buf);
            return 1;
        }
        if (field.field_number == ONNX_MODEL_GRAPH && field.wire_type == PB_WIRE_LEN) {
            graph_slice.ptr = field.bytes;
            graph_slice.len = field.bytes_len;
            found_graph = 1;
        }
    }
    if (!found_graph) {
        free(buf);
        return 1;
    }

    PbReader graph_r;
    pb_reader_init(&graph_r, graph_slice.ptr, graph_slice.len);

    Slice node_slices[MAX_NODES];
    size_t num_node_slices = 0;
    OnnxInitializer initializers[MAX_INITIALIZERS];
    size_t num_initializers = 0;

    while (!pb_reader_done(&graph_r)) {
        PbField field;
        if (pb_read_field(&graph_r, &field) != 0) {
            free(buf);
            return 1;
        }
        if (field.field_number == ONNX_GRAPH_NODE && field.wire_type == PB_WIRE_LEN) {
            if (num_node_slices >= MAX_NODES) {
                free(buf);
                return 1;
            }
            node_slices[num_node_slices].ptr = field.bytes;
            node_slices[num_node_slices].len = field.bytes_len;
            num_node_slices++;
        } else if (field.field_number == ONNX_GRAPH_INITIALIZER && field.wire_type == PB_WIRE_LEN) {
            if (num_initializers >= MAX_INITIALIZERS) {
                free(buf);
                return 1;
            }
            PbReader ir;
            pb_reader_init(&ir, field.bytes, field.bytes_len);
            if (parse_initializer(ir, &initializers[num_initializers]) != 0) {
                free(buf);
                return 1;
            }
            num_initializers++;
        }
    }

    ir_graph_init(out);

    NameEntry names[MAX_NODES];
    size_t num_names = 0;

    for (size_t i = 0; i < num_node_slices; i++) {
        PbReader nr;
        pb_reader_init(&nr, node_slices[i].ptr, node_slices[i].len);
        OnnxNode node;
        if (parse_node(nr, &node) != 0) {
            ir_graph_free(out);
            free(buf);
            return 1;
        }

        int ok;
        OpKind kind = op_kind_from_type(node.op_type, &ok);
        if (!ok) {
            /* Unsupported op_type -- spec section 9 non-goal: no general
             * ONNX operator coverage beyond this project's own op set. */
            ir_graph_free(out);
            free(buf);
            return 1;
        }

        /* export_model.py's DT_INT8/DT_INT32 constants are defined to
         * equal these enum ordinals exactly, so this cast needs no lookup
         * table -- see export_model.py's module docstring. */
        DType dtype = (DType)node.dtype;

        size_t *inputs = NULL;
        if (node.num_inputs > 0) {
            inputs = malloc(node.num_inputs * sizeof(size_t));
            for (size_t j = 0; j < node.num_inputs; j++) {
                int found = 0;
                for (size_t k = 0; k < num_names; k++) {
                    if (slice_eq_slice(names[k].name, node.inputs[j])) {
                        inputs[j] = names[k].op_id;
                        found = 1;
                        break;
                    }
                }
                if (!found) {
                    /* Input tensor produced by a node we haven't seen yet
                     * -- the graph isn't topologically ordered, which this
                     * parser doesn't support (export_model.py always
                     * writes nodes in dependency order). */
                    free(inputs);
                    ir_graph_free(out);
                    free(buf);
                    return 1;
                }
            }
        }

        size_t *shape = NULL;
        if (node.shape_len > 0) {
            shape = malloc(node.shape_len * sizeof(size_t));
            for (size_t j = 0; j < node.shape_len; j++) {
                shape[j] = (size_t)node.shape[j];
            }
        }

        QuantParams quant = { node.scale, (int32_t)node.zero_point };
        size_t op_id = ir_graph_push(out, kind, inputs, node.num_inputs,
                                      shape, node.shape_len, dtype, &quant);

        if (num_names < MAX_NODES) {
            names[num_names].name = node.output;
            names[num_names].op_id = op_id;
            num_names++;
        }

        if (kind == OP_CONST) {
            int found_init = 0;
            for (size_t k = 0; k < num_initializers; k++) {
                if (slice_eq_slice(initializers[k].name, node.output)) {
                    void *data = malloc(initializers[k].raw_data.len);
                    memcpy(data, initializers[k].raw_data.ptr, initializers[k].raw_data.len);
                    ir_op_set_data(out, op_id, data, initializers[k].raw_data.len);
                    found_init = 1;
                    break;
                }
            }
            if (!found_init) {
                /* Const node with no matching initializer -- malformed
                 * graph (export_model.py always pairs the two). */
                ir_graph_free(out);
                free(buf);
                return 1;
            }
        }
    }

    free(buf);
    return 0;
}
