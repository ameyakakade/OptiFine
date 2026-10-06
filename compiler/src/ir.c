#include "optifine/ir.h"

#include <stdint.h>
#include <stdlib.h>

#include "optifine/invariant.h"

void ir_graph_init(IrGraph *graph) {
    graph->ops = NULL;
    graph->count = 0;
    graph->capacity = 0;
}

void ir_graph_free(IrGraph *graph) {
    for (size_t i = 0; i < graph->count; i++) {
        free(graph->ops[i].inputs);
        free(graph->ops[i].output_shape);
        free(graph->ops[i].data);
    }
    free(graph->ops);
    graph->ops = NULL;
    graph->count = 0;
    graph->capacity = 0;
}

int ir_graph_push(IrGraph *graph, OpKind kind,
                  size_t *inputs, size_t num_inputs,
                  size_t *output_shape, size_t output_shape_len,
                  DType dtype, const QuantParams *quant, size_t *out_id) {
    if (graph->count == graph->capacity) {
        size_t new_capacity = graph->capacity == 0 ? 8 : graph->capacity * 2;
        IrOp *grown = NULL;
        if (new_capacity > graph->capacity && new_capacity <= SIZE_MAX / sizeof(IrOp)) {
            grown = realloc(graph->ops, new_capacity * sizeof(IrOp));
        }
        if (!grown) {
            free(inputs);
            free(output_shape);
            return -1;
        }
        graph->ops = grown;
        graph->capacity = new_capacity;
    }

    size_t id = graph->count;
    IrOp *op = &graph->ops[id];
    op->id = id;
    op->kind = kind;
    op->inputs = inputs;
    op->num_inputs = num_inputs;
    op->output_shape = output_shape;
    op->output_shape_len = output_shape_len;
    op->dtype = dtype;
    op->has_quant = quant != NULL;
    if (quant) {
        op->quant = *quant;
    } else {
        op->quant.scale = 0.0f;
        op->quant.zero_point = 0;
    }
    op->data = NULL;
    op->data_len = 0;

    graph->count++;
    *out_id = id;
    return 0;
}

void ir_op_set_data(IrGraph *graph, size_t op_id, void *data, size_t data_len) {
    OPTIFINE_INVARIANT(op_id < graph->count);
    free(graph->ops[op_id].data);
    graph->ops[op_id].data = data;
    graph->ops[op_id].data_len = data_len;
}

size_t ir_dtype_size(DType dtype) {
    switch (dtype) {
        case DT_INT8:
            return 1;
        case DT_INT32:
            return 4;
        case DT_FIXED_Q15:
            return 2;
        case DT_COMPLEX_Q15:
            return 4;
        default:
            return 0;
    }
}

int ir_mul_size(size_t a, size_t b, size_t *out) {
    if (a != 0 && b > SIZE_MAX / a) {
        return -1;
    }
    *out = a * b;
    return 0;
}

int ir_op_tensor_size(const IrOp *op, size_t *out_elements, size_t *out_bytes) {
    size_t elements = 1;
    if (op->output_shape_len > 0 && op->output_shape == NULL) {
        return -1;
    }
    for (size_t i = 0; i < op->output_shape_len; i++) {
        if (ir_mul_size(elements, op->output_shape[i], &elements) != 0) {
            return -1;
        }
    }
    size_t elem_size = ir_dtype_size(op->dtype);
    size_t bytes;
    if (elem_size == 0 || ir_mul_size(elements, elem_size, &bytes) != 0) {
        return -1;
    }
    if (out_elements) *out_elements = elements;
    if (out_bytes) *out_bytes = bytes;
    return 0;
}
