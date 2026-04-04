/* Module 4: typed op graph. No lexer/parser -- ingest.c builds this
 * directly from the ONNX protobuf. */
#ifndef OPTIFINE_IR_H
#define OPTIFINE_IR_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DT_INT8,
    DT_INT32,
} DType;

typedef struct {
    float scale;
    int32_t zero_point;
} QuantParams;

typedef enum {
    OP_INPUT,
    OP_CONST,
    OP_MATMUL,
    OP_ADD,
    OP_RELU,
    OP_REQUANTIZE,
    OP_OUTPUT,
} OpKind;

typedef struct {
    size_t id;
    OpKind kind;
    size_t *inputs;
    size_t num_inputs;
    size_t *output_shape;
    size_t output_shape_len;
    DType dtype;
    int has_quant;
    QuantParams quant;
} IrOp;

typedef struct {
    IrOp *ops;
    size_t count;
    size_t capacity;
} IrGraph;

void ir_graph_init(IrGraph *graph);
void ir_graph_free(IrGraph *graph);

/* Appends an op, taking ownership of `inputs` and `output_shape` (must be
 * heap-allocated with malloc, or NULL if the respective length is 0).
 * Returns the new op's id. */
size_t ir_graph_push(IrGraph *graph, OpKind kind,
                      size_t *inputs, size_t num_inputs,
                      size_t *output_shape, size_t output_shape_len,
                      DType dtype, const QuantParams *quant);

#endif /* OPTIFINE_IR_H */
