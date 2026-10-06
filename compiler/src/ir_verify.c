#include "optifine/ir_verify.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>

#include "optifine/dsp_build.h"

static const char *kind_name(OpKind kind) {
    switch (kind) {
        case OP_INPUT: return "Input";
        case OP_CONST: return "Const";
        case OP_MATMUL: return "MatMul";
        case OP_ADD: return "Add";
        case OP_RELU: return "Relu";
        case OP_REQUANTIZE: return "Requantize";
        case OP_OUTPUT: return "Output";
        case OP_WINDOW: return "Window";
        case OP_BIT_REVERSE: return "BitReverse";
        case OP_FFT_BUTTERFLY: return "FftButterfly";
        case OP_MAGNITUDE: return "Magnitude";
        case OP_PEAK_EXTRACT: return "PeakExtract";
    }
    return NULL;
}

typedef struct {
    char *message;
    size_t message_len;
} Diag;

static int reject(Diag *d, size_t op_id, OpKind kind, const char *format, ...) {
    if (d->message_len == 0) return -1;
    const char *name = kind_name(kind);
    int n = snprintf(d->message, d->message_len, "op %zu (%s): ", op_id, name ? name : "?");
    if (n >= 0 && (size_t)n < d->message_len) {
        va_list args;
        va_start(args, format);
        vsnprintf(d->message + n, d->message_len - (size_t)n, format, args);
        va_end(args);
    }
    return -1;
}

static int arity(OpKind kind) {
    switch (kind) {
        case OP_INPUT:
        case OP_CONST:
            return 0;
        case OP_MATMUL:
        case OP_ADD:
        case OP_WINDOW:
            return 2;
        default:
            return 1;
    }
}

typedef enum { FAMILY_SHARED, FAMILY_ML, FAMILY_DSP } Family;

static Family family(OpKind kind) {
    switch (kind) {
        case OP_INPUT:
        case OP_CONST:
        case OP_OUTPUT:
            return FAMILY_SHARED;
        case OP_MATMUL:
        case OP_ADD:
        case OP_RELU:
        case OP_REQUANTIZE:
            return FAMILY_ML;
        default:
            return FAMILY_DSP;
    }
}

static size_t elements_of(const IrOp *op) {
    size_t n = 0;
    ir_op_tensor_size(op, &n, NULL); /* already checked for every earlier op */
    return n;
}

static int positive_scale(const IrOp *op) {
    return op->has_quant && isfinite(op->quant.scale) && op->quant.scale > 0.0f;
}

/* The operator-specific contract of `op`, whose inputs are already known to
 * be valid earlier ops. */
static int verify_contract(const IrGraph *g, const IrOp *op, Family graph_family, size_t *butterflies,
                           size_t *last_butterfly, Diag *d) {
    const IrOp *a = op->num_inputs >= 1 ? &g->ops[op->inputs[0]] : NULL;
    const IrOp *b = op->num_inputs >= 2 ? &g->ops[op->inputs[1]] : NULL;
    size_t n = elements_of(op);
    size_t id = op->id;

    switch (op->kind) {
        case OP_INPUT:
            if (graph_family == FAMILY_DSP ? op->dtype != DT_FIXED_Q15 : op->dtype != DT_INT8) {
                return reject(d, id, op->kind, "input must be %s",
                              graph_family == FAMILY_DSP ? "FIXED_Q15" : "INT8");
            }
            return 0;
        case OP_CONST:
            if (graph_family == FAMILY_DSP ? op->dtype != DT_FIXED_Q15
                                           : (op->dtype != DT_INT8 && op->dtype != DT_INT32)) {
                return reject(d, id, op->kind, "constant must be %s",
                              graph_family == FAMILY_DSP ? "FIXED_Q15" : "INT8 or INT32");
            }
            return 0;
        case OP_MATMUL: {
            if (a->dtype != DT_INT8 || b->dtype != DT_INT8 || op->dtype != DT_INT32) {
                return reject(d, id, op->kind, "expects INT8 x INT8 -> INT32");
            }
            if (a->output_shape_len == 0 || b->output_shape_len != 2) {
                return reject(d, id, op->kind, "expects an activation with a last dimension K and a [N,K] weight");
            }
            size_t k = a->output_shape[a->output_shape_len - 1];
            if (elements_of(a) != k || b->output_shape[1] != k || n != b->output_shape[0]) {
                return reject(d, id, op->kind, "shapes are not INT8[K] x INT8[N,K] -> INT32[N]");
            }
            return 0;
        }
        case OP_ADD:
            if (a->dtype != DT_INT32 || b->dtype != DT_INT32 || op->dtype != DT_INT32) {
                return reject(d, id, op->kind, "expects INT32 + INT32 -> INT32");
            }
            if (elements_of(a) != n || elements_of(b) != n) {
                return reject(d, id, op->kind, "operand and result element counts differ");
            }
            return 0;
        case OP_RELU:
            if (a->dtype != DT_INT32 || op->dtype != DT_INT32 || elements_of(a) != n) {
                return reject(d, id, op->kind, "expects INT32[n] -> INT32[n]");
            }
            return 0;
        case OP_REQUANTIZE:
            if (a->dtype != DT_INT32 || op->dtype != DT_INT8 || elements_of(a) != n) {
                return reject(d, id, op->kind, "expects INT32[n] -> INT8[n]");
            }
            if (!positive_scale(a) || !positive_scale(op)) {
                return reject(d, id, op->kind, "input and output scales must be positive and finite");
            }
            return 0;
        case OP_OUTPUT:
            if (a->dtype != op->dtype || elements_of(a) != n) {
                return reject(d, id, op->kind, "dtype and element count must match its producer");
            }
            return 0;
        case OP_WINDOW:
            if (a->dtype != DT_FIXED_Q15 || b->dtype != DT_FIXED_Q15 || op->dtype != DT_FIXED_Q15 ||
                elements_of(a) != n || elements_of(b) != n) {
                return reject(d, id, op->kind, "expects FIXED_Q15[n] x FIXED_Q15[n] -> FIXED_Q15[n]");
            }
            return 0;
        case OP_BIT_REVERSE:
            if (a->dtype != DT_FIXED_Q15 || op->dtype != DT_COMPLEX_Q15 || elements_of(a) != DSP_FFT_SIZE ||
                n != DSP_FFT_SIZE) {
                return reject(d, id, op->kind, "expects FIXED_Q15[%d] -> COMPLEX_Q15[%d]", DSP_FFT_SIZE,
                              DSP_FFT_SIZE);
            }
            return 0;
        case OP_FFT_BUTTERFLY:
            if (a->dtype != DT_COMPLEX_Q15 || op->dtype != DT_COMPLEX_Q15 || elements_of(a) != DSP_FFT_SIZE ||
                n != DSP_FFT_SIZE) {
                return reject(d, id, op->kind, "expects COMPLEX_Q15[%d] -> COMPLEX_Q15[%d]", DSP_FFT_SIZE,
                              DSP_FFT_SIZE);
            }
            /* Lowering derives a stage's geometry from its position among the
             * butterflies, so the stages must form one chain from BitReverse. */
            if (*butterflies == DSP_FFT_LOG2) {
                return reject(d, id, op->kind, "more than %d stages for a %d-point transform", DSP_FFT_LOG2,
                              DSP_FFT_SIZE);
            }
            if (*butterflies == 0 ? a->kind != OP_BIT_REVERSE : op->inputs[0] != *last_butterfly) {
                return reject(d, id, op->kind, "stage %zu must read %s", *butterflies,
                              *butterflies == 0 ? "the BitReverse output" : "the previous stage");
            }
            (*butterflies)++;
            *last_butterfly = id;
            return 0;
        case OP_MAGNITUDE:
            if (a->dtype != DT_COMPLEX_Q15 || op->dtype != DT_FIXED_Q15 || elements_of(a) != n || n > 256) {
                return reject(d, id, op->kind, "expects COMPLEX_Q15[n] -> FIXED_Q15[n], 1 <= n <= 256");
            }
            return 0;
        case OP_PEAK_EXTRACT: {
            size_t in_n = elements_of(a);
            if (a->dtype != DT_FIXED_Q15 || op->dtype != DT_FIXED_Q15 || in_n > 255 || n > in_n) {
                return reject(d, id, op->kind, "expects FIXED_Q15[n] -> FIXED_Q15[k], 1 <= k <= n <= 255");
            }
            return 0;
        }
    }
    return reject(d, id, op->kind, "unknown operator");
}

int ir_verify(const IrGraph *graph, char *message, size_t message_len) {
    Diag diag = {message, message_len};
    Diag *d = &diag;
    if (message_len > 0) message[0] = '\0';

    if (graph == NULL || graph->count == 0 || graph->ops == NULL) {
        if (message_len > 0) snprintf(message, message_len, "graph is empty");
        return -1;
    }

    /* Structure of every op, and which operator family the graph uses. */
    Family graph_family = FAMILY_SHARED;
    size_t inputs = 0, outputs = 0;
    for (size_t i = 0; i < graph->count; i++) {
        const IrOp *op = &graph->ops[i];
        if (kind_name(op->kind) == NULL) {
            return reject(d, i, op->kind, "OpKind %d is not an operator", (int)op->kind);
        }
        if (op->id != i) return reject(d, i, op->kind, "id %zu does not match its position", op->id);
        if (ir_dtype_size(op->dtype) == 0) {
            return reject(d, i, op->kind, "DType %d is not a type", (int)op->dtype);
        }
        if (op->num_inputs != (size_t)arity(op->kind)) {
            return reject(d, i, op->kind, "has %zu inputs, expects %d", op->num_inputs, arity(op->kind));
        }
        if (op->num_inputs > 0 && op->inputs == NULL) return reject(d, i, op->kind, "input list is missing");
        for (size_t j = 0; j < op->num_inputs; j++) {
            if (op->inputs[j] >= i) {
                return reject(d, i, op->kind, "input %zu names op %zu, which does not precede it", j,
                              op->inputs[j]);
            }
        }
        if (op->output_shape_len > 0 && op->output_shape == NULL) {
            return reject(d, i, op->kind, "shape has %zu dimensions but no shape array", op->output_shape_len);
        }
        for (size_t j = 0; j < op->output_shape_len; j++) {
            if (op->output_shape[j] == 0) return reject(d, i, op->kind, "dimension %zu is zero", j);
        }
        size_t bytes;
        if (ir_op_tensor_size(op, NULL, &bytes) != 0) {
            return reject(d, i, op->kind, "tensor size overflows");
        }
        if (op->kind == OP_CONST) {
            if (op->data == NULL) return reject(d, i, op->kind, "constant has no data");
            if (op->data_len != bytes) {
                return reject(d, i, op->kind, "constant data is %zu bytes, the tensor is %zu", op->data_len,
                              bytes);
            }
        } else if (op->data != NULL || op->data_len != 0) {
            return reject(d, i, op->kind, "only a constant may carry data");
        }

        Family f = family(op->kind);
        if (f != FAMILY_SHARED) {
            if (graph_family != FAMILY_SHARED && graph_family != f) {
                return reject(d, i, op->kind, "ML and DSP operators cannot share one graph");
            }
            graph_family = f;
        }
        if (op->kind == OP_INPUT) inputs++;
        if (op->kind == OP_OUTPUT) outputs++;
    }
    if (inputs != 1) {
        if (message_len > 0) snprintf(message, message_len, "graph has %zu Input ops, expects exactly 1", inputs);
        return -1;
    }
    if (outputs != 1 || graph->ops[graph->count - 1].kind != OP_OUTPUT) {
        if (message_len > 0) snprintf(message, message_len, "graph must end in its one Output op");
        return -1;
    }

    /* Operator contracts. The ML lowering ignores zero points, so the model
     * must be symmetrically quantized. */
    size_t butterflies = 0, last_butterfly = 0;
    for (size_t i = 0; i < graph->count; i++) {
        const IrOp *op = &graph->ops[i];
        if (graph_family == FAMILY_ML && op->has_quant && op->quant.zero_point != 0) {
            return reject(d, i, op->kind, "zero point %d: only symmetric quantization is supported",
                          (int)op->quant.zero_point);
        }
        if (verify_contract(graph, op, graph_family, &butterflies, &last_butterfly, d) != 0) return -1;
    }
    return 0;
}

int ir_verify_or_report(const IrGraph *graph, const char *what) {
    char message[256];
    if (ir_verify(graph, message, sizeof(message)) != 0) {
        fprintf(stderr, "%s: invalid IR: %s\n", what, message);
        return -1;
    }
    return 0;
}
