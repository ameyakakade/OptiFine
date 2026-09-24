/* Module 4: typed op graph shared by both workload paths (spec v2 section
 * 6.1). No lexer/parser -- ingest.c builds the ML side from the ONNX
 * protobuf, the DSP side is constructed directly via a builder API
 * (spec v2 section 6.2). */
#ifndef OPTIFINE_IR_H
#define OPTIFINE_IR_H

#include <stddef.h>
#include <stdint.h>

typedef enum {
    DT_INT8,        /* ML path -- activations */
    DT_INT32,       /* ML path -- accumulators */
    DT_FIXED_Q15,   /* DSP path -- Q15 fixed-point real */
    DT_COMPLEX_Q15, /* DSP path -- Q15 fixed-point complex (re, im pair) */
} DType;

typedef struct {
    float scale;
    int32_t zero_point;
} QuantParams;

typedef enum {
    /* ML path */
    OP_INPUT,
    OP_CONST,
    OP_MATMUL,
    OP_ADD,
    OP_RELU,
    OP_REQUANTIZE,
    OP_OUTPUT,
    /* DSP path */
    OP_WINDOW,       /* elementwise multiply by a fixed window (e.g. Hamming) */
    OP_BIT_REVERSE,  /* bit-reversal permutation ahead of the butterfly stages */
    OP_FFT_BUTTERFLY, /* one radix-2 DIT butterfly stage */
    OP_MAGNITUDE,    /* complex magnitude from FFT output */
    OP_PEAK_EXTRACT, /* the DSP_MAX_PEAKS largest magnitude VALUES, largest first
                      * (unsigned; not local maxima, and no bin indices -- the
                      * FIXED_Q15[k] output has no room for them) */
} OpKind;

typedef struct {
    size_t id;
    OpKind kind;
    size_t *inputs;
    size_t num_inputs;
    size_t *output_shape;
    size_t output_shape_len;
    DType dtype;
    int has_quant; /* quant is ML-path only; always 0 on the DSP path */
    QuantParams quant;
    void *data;     /* NULL unless this op carries compile-time-constant
                      * data (e.g. OP_CONST weights); owned, set via
                      * ir_op_set_data, freed by ir_graph_free */
    size_t data_len; /* bytes in `data` */
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

/* Attaches compile-time-constant data to an already-pushed op, taking
 * ownership of `data` (must be heap-allocated with malloc). */
void ir_op_set_data(IrGraph *graph, size_t op_id, void *data, size_t data_len);

#endif /* OPTIFINE_IR_H */
