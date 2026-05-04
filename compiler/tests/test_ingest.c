#include <assert.h>
#include <stdio.h>
#include <stdint.h>

#include "optifine/ingest.h"
#include "optifine/ir.h"

static void assert_int8_range(const IrOp *op) {
    const int8_t *data = (const int8_t *)op->data;
    for (size_t i = 0; i < op->data_len; i++) {
        assert(data[i] >= -127 && data[i] <= 127);
    }
}

static void dump(const IrGraph *g) {
    static const char *kind_names[] = {
        "Input", "Const", "MatMul", "Add", "Relu", "Requantize", "Output",
        "Window", "BitReverse", "FftButterfly", "Magnitude", "PeakExtract",
    };
    for (size_t i = 0; i < g->count; i++) {
        const IrOp *op = &g->ops[i];
        printf("[%zu] %s dtype=%d shape=[", i, kind_names[op->kind], op->dtype);
        for (size_t j = 0; j < op->output_shape_len; j++) {
            printf("%zu%s", op->output_shape[j], j + 1 < op->output_shape_len ? "," : "");
        }
        printf("] scale=%.8f zero_point=%d data_len=%zu inputs=[", op->quant.scale,
               op->quant.zero_point, op->data_len);
        for (size_t j = 0; j < op->num_inputs; j++) {
            printf("%zu%s", op->inputs[j], j + 1 < op->num_inputs ? "," : "");
        }
        printf("]\n");
    }
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <model.onnx>\n", argv[0]);
        return 2;
    }

    IrGraph g;
    int rc = ingest_load_onnx(argv[1], &g);
    assert(rc == 0);

    dump(&g);

    assert(g.count == 13);

    /* fc1: Input, Const(W), Const(B), MatMul, Add, Relu, Requantize */
    assert(g.ops[0].kind == OP_INPUT);
    assert(g.ops[0].dtype == DT_INT8);
    assert(g.ops[0].output_shape_len == 2 && g.ops[0].output_shape[0] == 1 && g.ops[0].output_shape[1] == 16);
    assert(g.ops[0].has_quant && g.ops[0].quant.zero_point == 0 && g.ops[0].quant.scale > 0);
    assert(g.ops[0].num_inputs == 0);

    assert(g.ops[1].kind == OP_CONST);
    assert(g.ops[1].dtype == DT_INT8);
    assert(g.ops[1].output_shape_len == 2 && g.ops[1].output_shape[0] == 8 && g.ops[1].output_shape[1] == 16);
    assert(g.ops[1].data != NULL && g.ops[1].data_len == 8 * 16);
    assert_int8_range(&g.ops[1]);

    assert(g.ops[2].kind == OP_CONST);
    assert(g.ops[2].dtype == DT_INT32);
    assert(g.ops[2].output_shape_len == 1 && g.ops[2].output_shape[0] == 8);
    assert(g.ops[2].data != NULL && g.ops[2].data_len == 8 * 4);

    assert(g.ops[3].kind == OP_MATMUL);
    assert(g.ops[3].dtype == DT_INT32);
    assert(g.ops[3].num_inputs == 2 && g.ops[3].inputs[0] == 0 && g.ops[3].inputs[1] == 1);

    assert(g.ops[4].kind == OP_ADD);
    assert(g.ops[4].num_inputs == 2 && g.ops[4].inputs[0] == 3 && g.ops[4].inputs[1] == 2);
    assert(g.ops[4].quant.scale == g.ops[3].quant.scale); /* Add preserves the accumulator scale */

    assert(g.ops[5].kind == OP_RELU);
    assert(g.ops[5].num_inputs == 1 && g.ops[5].inputs[0] == 4);
    assert(g.ops[5].quant.scale == g.ops[3].quant.scale); /* Relu preserves it too */

    assert(g.ops[6].kind == OP_REQUANTIZE);
    assert(g.ops[6].dtype == DT_INT8);
    assert(g.ops[6].num_inputs == 1 && g.ops[6].inputs[0] == 5);
    assert(g.ops[6].quant.scale != g.ops[5].quant.scale); /* real rescale, not a passthrough */

    /* fc2: Const(W), Const(B), MatMul, Add, Requantize (no Relu), Output */
    assert(g.ops[7].kind == OP_CONST);
    assert(g.ops[7].dtype == DT_INT8);
    assert(g.ops[7].output_shape_len == 2 && g.ops[7].output_shape[0] == 4 && g.ops[7].output_shape[1] == 8);
    assert(g.ops[7].data != NULL && g.ops[7].data_len == 4 * 8);
    assert_int8_range(&g.ops[7]);

    assert(g.ops[8].kind == OP_CONST);
    assert(g.ops[8].dtype == DT_INT32);
    assert(g.ops[8].data != NULL && g.ops[8].data_len == 4 * 4);

    assert(g.ops[9].kind == OP_MATMUL);
    assert(g.ops[9].num_inputs == 2 && g.ops[9].inputs[0] == 6 && g.ops[9].inputs[1] == 7);

    assert(g.ops[10].kind == OP_ADD);
    assert(g.ops[10].num_inputs == 2 && g.ops[10].inputs[0] == 9 && g.ops[10].inputs[1] == 8);
    assert(g.ops[10].quant.scale == g.ops[9].quant.scale);

    assert(g.ops[11].kind == OP_REQUANTIZE);
    assert(g.ops[11].dtype == DT_INT8);
    assert(g.ops[11].num_inputs == 1 && g.ops[11].inputs[0] == 10);

    assert(g.ops[12].kind == OP_OUTPUT);
    assert(g.ops[12].dtype == DT_INT8);
    assert(g.ops[12].num_inputs == 1 && g.ops[12].inputs[0] == 11);
    assert(g.ops[12].quant.scale == g.ops[11].quant.scale);

    ir_graph_free(&g);

    /* A path that doesn't exist should fail cleanly, not crash. */
    IrGraph missing;
    assert(ingest_load_onnx("this/path/does/not/exist.onnx", &missing) != 0);

    printf("test_ingest: all tests passed\n");
    return 0;
}
