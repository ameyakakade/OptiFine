#include "optifine/ir.h"

#include <stdlib.h>

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

size_t ir_graph_push(IrGraph *graph, OpKind kind,
                      size_t *inputs, size_t num_inputs,
                      size_t *output_shape, size_t output_shape_len,
                      DType dtype, const QuantParams *quant) {
    if (graph->count == graph->capacity) {
        size_t new_capacity = graph->capacity == 0 ? 8 : graph->capacity * 2;
        graph->ops = realloc(graph->ops, new_capacity * sizeof(IrOp));
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
    }
    op->data = NULL;
    op->data_len = 0;

    graph->count++;
    return id;
}

void ir_op_set_data(IrGraph *graph, size_t op_id, void *data, size_t data_len) {
    free(graph->ops[op_id].data);
    graph->ops[op_id].data = data;
    graph->ops[op_id].data_len = data_len;
}
