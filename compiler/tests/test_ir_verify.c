/* ir_verify: the real ML and DSP graphs pass, and each malformed variant --
 * one defect injected into an otherwise valid graph -- is rejected with a
 * diagnostic naming the problem, before any layout or lowering could act on
 * it.
 *
 * argv: <tiny_classifier.onnx> */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/dsp_build.h"
#include "optifine/ingest.h"
#include "optifine/ir_verify.h"

static const char *g_model;

/* Op ids in tiny_classifier.onnx, as ingested. */
enum { ML_INPUT = 0, ML_W1 = 1, ML_B1 = 2, ML_MATMUL1 = 3, ML_ADD1 = 4, ML_RELU = 5, ML_REQ1 = 6,
       ML_OUTPUT = 12 };
/* Op ids in dsp_build_pipeline's graph. */
enum { DSP_INPUT = 0, DSP_COEFFS = 1, DSP_WINDOW = 2, DSP_BITREV = 3, DSP_STAGE0 = 4, DSP_MAG = 10,
       DSP_PEAKS = 11 };

static void load_ml(IrGraph *g) {
    assert(ingest_load_onnx(g_model, g) == 0);
    assert(g->count == 13);
}
static void load_dsp(IrGraph *g) {
    assert(dsp_build_pipeline(g) == 0);
}

/* Verifies `g`, expects rejection with `expect` in the message, frees `g`. */
static void expect_reject(IrGraph *g, const char *expect, const char *what) {
    char message[256];
    int rc = ir_verify(g, message, sizeof(message));
    if (rc == 0 || strstr(message, expect) == NULL) {
        fprintf(stderr, "%s: rc=%d message='%s', expected '%s'\n", what, rc, message, expect);
        abort();
    }
    printf("  rejected %-40s %s\n", what, message);
    ir_graph_free(g);
}

static void set_shape(IrOp *op, size_t len, const size_t *dims) {
    free(op->output_shape);
    op->output_shape = malloc(len * sizeof(size_t));
    assert(op->output_shape);
    memcpy(op->output_shape, dims, len * sizeof(size_t));
    op->output_shape_len = len;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    g_model = argv[1];
    IrGraph g;
    char message[256];

    load_ml(&g);
    assert(ir_verify(&g, message, sizeof(message)) == 0);
    ir_graph_free(&g);
    load_dsp(&g);
    assert(ir_verify(&g, message, sizeof(message)) == 0);
    ir_graph_free(&g);
    printf("  the real ML and DSP graphs verify\n");

    IrGraph empty;
    ir_graph_init(&empty);
    expect_reject(&empty, "empty", "empty graph");

    /* --- structure --- */
    load_ml(&g); g.ops[ML_RELU].kind = (OpKind)99;            expect_reject(&g, "not an operator", "unknown OpKind");
    load_ml(&g); g.ops[ML_RELU].dtype = (DType)99;            expect_reject(&g, "not a type", "unknown DType");
    load_ml(&g); g.ops[ML_RELU].id = 7;                       expect_reject(&g, "does not match", "id out of place");
    load_ml(&g); g.ops[ML_MATMUL1].num_inputs = 1;            expect_reject(&g, "expects 2", "MatMul with one input");
    load_ml(&g); g.ops[ML_ADD1].inputs[0] = ML_REQ1;          expect_reject(&g, "does not precede", "forward reference");
    load_ml(&g); g.ops[ML_ADD1].inputs[0] = ML_ADD1;          expect_reject(&g, "does not precede", "self reference");
    load_ml(&g); g.ops[ML_ADD1].inputs[1] = 4000;             expect_reject(&g, "does not precede", "input id out of range");
    load_ml(&g);
    free(g.ops[ML_RELU].output_shape);
    g.ops[ML_RELU].output_shape = NULL;                       expect_reject(&g, "no shape array", "shape length without shape");
    load_ml(&g); g.ops[ML_RELU].output_shape[1] = 0;          expect_reject(&g, "is zero", "zero dimension");
    load_ml(&g);
    {
        size_t dims[3] = {SIZE_MAX / 2, 4, 2};
        set_shape(&g.ops[ML_RELU], 3, dims);
    }
    expect_reject(&g, "overflows", "element count overflow");

    /* --- constants: the initializer must be exactly the tensor --- */
    load_ml(&g); g.ops[ML_W1].data_len -= 1;                  expect_reject(&g, "constant data is 127 bytes", "short initializer");
    load_ml(&g); g.ops[ML_W1].data_len += 1;                  expect_reject(&g, "constant data is 129 bytes", "long initializer");
    load_ml(&g);
    free(g.ops[ML_W1].data);
    g.ops[ML_W1].data = NULL;                                 expect_reject(&g, "has no data", "constant without data");
    load_ml(&g);
    g.ops[ML_RELU].data = malloc(4);
    g.ops[ML_RELU].data_len = 4;                              expect_reject(&g, "only a constant", "data on a non-constant");

    /* --- graph-level --- */
    load_ml(&g); g.ops[ML_OUTPUT].kind = OP_RELU;             expect_reject(&g, "Output", "no Output");
    load_ml(&g);
    g.ops[ML_W1].kind = OP_INPUT;
    free(g.ops[ML_W1].data);
    g.ops[ML_W1].data = NULL;
    g.ops[ML_W1].data_len = 0;                                expect_reject(&g, "exactly 1", "two Inputs");
    load_ml(&g); g.ops[ML_RELU].kind = OP_MAGNITUDE;          expect_reject(&g, "cannot share", "ML and DSP mixed");

    /* --- ML operator contracts --- */
    load_ml(&g); g.ops[ML_INPUT].dtype = DT_INT32;            expect_reject(&g, "input must be INT8", "INT32 ML input");
    load_ml(&g);
    {
        size_t dims[2] = {2, 16};
        set_shape(&g.ops[ML_INPUT], 2, dims);
    }
    expect_reject(&g, "INT8[K] x INT8[N,K]", "MatMul activation not [K]");
    load_ml(&g);
    {
        size_t dims[2] = {1, 7};
        set_shape(&g.ops[ML_MATMUL1], 2, dims);
    }
    expect_reject(&g, "INT8[K] x INT8[N,K]", "MatMul result not [N]");
    load_ml(&g); g.ops[ML_MATMUL1].dtype = DT_INT8;           expect_reject(&g, "INT8 x INT8 -> INT32", "MatMul INT8 result");
    load_ml(&g);
    {
        size_t dims[2] = {1, 4};
        set_shape(&g.ops[ML_ADD1], 2, dims);
    }
    expect_reject(&g, "element counts differ", "Add count mismatch");
    load_ml(&g); g.ops[ML_RELU].dtype = DT_INT8;              expect_reject(&g, "INT32[n] -> INT32[n]", "Relu INT8 result");
    load_ml(&g); g.ops[ML_REQ1].quant.scale = 0.0f;           expect_reject(&g, "positive and finite", "zero requantize scale");
    load_ml(&g); g.ops[ML_RELU].quant.scale = -1.0f;          expect_reject(&g, "positive and finite", "negative producer scale");
    load_ml(&g); g.ops[ML_REQ1].quant.zero_point = 3;         expect_reject(&g, "symmetric", "nonzero zero point");
    load_ml(&g); g.ops[ML_OUTPUT].dtype = DT_INT32;           expect_reject(&g, "match its producer", "Output dtype mismatch");

    /* --- DSP operator contracts --- */
    load_dsp(&g); g.ops[DSP_INPUT].dtype = DT_INT8;           expect_reject(&g, "input must be FIXED_Q15", "INT8 DSP input");
    load_dsp(&g); g.ops[DSP_WINDOW].dtype = DT_COMPLEX_Q15;   expect_reject(&g, "FIXED_Q15[n] x FIXED_Q15[n]", "Window result type");
    load_dsp(&g);
    {
        size_t dims[1] = {32};
        set_shape(&g.ops[DSP_BITREV], 1, dims);
    }
    expect_reject(&g, "COMPLEX_Q15[64]", "BitReverse of 32 points");
    load_dsp(&g); g.ops[DSP_STAGE0 + 2].inputs[0] = DSP_BITREV; expect_reject(&g, "previous stage", "FFT stage out of chain");
    load_dsp(&g); g.ops[DSP_STAGE0].inputs[0] = DSP_WINDOW;   expect_reject(&g, "COMPLEX_Q15", "FFT stage reading real data");
    load_dsp(&g); g.ops[DSP_MAG].dtype = DT_COMPLEX_Q15;      expect_reject(&g, "COMPLEX_Q15[n] -> FIXED_Q15[n]", "Magnitude result type");
    load_dsp(&g);
    {
        size_t dims[1] = {65};
        set_shape(&g.ops[DSP_PEAKS], 1, dims);
    }
    expect_reject(&g, "1 <= k <= n", "PeakExtract k > n");

    /* A zero-capacity diagnostic buffer still gets a rejection. */
    load_ml(&g);
    g.ops[ML_W1].data_len = 1;
    assert(ir_verify(&g, NULL, 0) != 0);
    ir_graph_free(&g);

    printf("test_ir_verify: all tests passed\n");
    return 0;
}
