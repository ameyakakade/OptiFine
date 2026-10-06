/* Typed op graph shared by both workload paths. No lexer/parser --
 * ingest.c builds the ML side from the ONNX protobuf, the DSP side is
 * constructed directly via a builder API (dsp_build.h). */
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

/* Appends an op and stores its id in *out_id. Takes ownership of `inputs`
 * and `output_shape` (heap-allocated with malloc, or NULL when the length is
 * 0) whether or not it succeeds: on failure -- the op array cannot grow --
 * both are freed, the graph is unchanged and -1 is returned. */
int ir_graph_push(IrGraph *graph, OpKind kind,
                  size_t *inputs, size_t num_inputs,
                  size_t *output_shape, size_t output_shape_len,
                  DType dtype, const QuantParams *quant, size_t *out_id);

/* Attaches compile-time-constant data to an already-pushed op, taking
 * ownership of `data` (must be heap-allocated with malloc). */
void ir_op_set_data(IrGraph *graph, size_t op_id, void *data, size_t data_len);

/* Bytes per element of `dtype`: 1 (DT_INT8), 4 (DT_INT32), 2 (DT_FIXED_Q15),
 * 4 (DT_COMPLEX_Q15, a real:imaginary Q15 pair). 0 for a value outside the
 * enum. */
size_t ir_dtype_size(DType dtype);

/* a * b into *out, or -1 when the product does not fit size_t. */
int ir_mul_size(size_t a, size_t b, size_t *out);

/* Element count (the product of output_shape, 1 for a scalar) and byte size
 * of `op`'s output tensor, computed with overflow checks. Returns -1 when
 * either product overflows, the shape pointer is missing, or the dtype is
 * unknown; ir_verify rejects such ops, so code downstream of it may use the
 * unchecked sram_layout_num_elements. Either out pointer may be NULL. */
int ir_op_tensor_size(const IrOp *op, size_t *out_elements, size_t *out_bytes);

#endif /* OPTIFINE_IR_H */
