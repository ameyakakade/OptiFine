#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "optifine/codegen/select.h"
#include "optifine/ir.h"

static void test_ir_graph_push(void) {
    IrGraph graph;
    ir_graph_init(&graph);

    size_t id0 = 99, id1 = 99;
    assert(ir_graph_push(&graph, OP_INPUT, NULL, 0, NULL, 0, DT_INT8, NULL, &id0) == 0);
    assert(id0 == 0);
    assert(graph.count == 1);
    assert(graph.ops[0].kind == OP_INPUT);

    size_t *inputs = malloc(sizeof(size_t));
    inputs[0] = id0;
    assert(ir_graph_push(&graph, OP_RELU, inputs, 1, NULL, 0, DT_INT8, NULL, &id1) == 0);
    assert(id1 == 1);
    assert(graph.ops[1].num_inputs == 1);
    assert(graph.ops[1].inputs[0] == id0);

    ir_graph_free(&graph);
}

/* Tensor sizes are products of untrusted dimensions; every product is
 * checked rather than left to wrap. */
static void test_tensor_size_overflow(void) {
    size_t out = 7;
    assert(ir_mul_size(0, SIZE_MAX, &out) == 0 && out == 0);
    assert(ir_mul_size(SIZE_MAX, 1, &out) == 0 && out == SIZE_MAX);
    assert(ir_mul_size(SIZE_MAX / 2 + 1, 2, &out) != 0);

    size_t shape[2] = {4, 16};
    IrOp op = {0};
    op.output_shape = shape;
    op.output_shape_len = 2;
    op.dtype = DT_INT32;
    size_t elements = 0, bytes = 0;
    assert(ir_op_tensor_size(&op, &elements, &bytes) == 0 && elements == 64 && bytes == 256);

    shape[0] = SIZE_MAX / 4; /* elements fit, bytes (x4) do not */
    shape[1] = 2;
    assert(ir_op_tensor_size(&op, &elements, &bytes) != 0);
    shape[1] = SIZE_MAX; /* the element product itself overflows */
    assert(ir_op_tensor_size(&op, NULL, NULL) != 0);

    op.output_shape = NULL; /* a length without a shape */
    assert(ir_op_tensor_size(&op, NULL, NULL) != 0);
    op.output_shape_len = 0; /* a scalar */
    assert(ir_op_tensor_size(&op, &elements, &bytes) == 0 && elements == 1 && bytes == 4);
    op.dtype = (DType)42;
    assert(ir_op_tensor_size(&op, NULL, NULL) != 0);
}

static void test_select_min_energy(void) {
    Candidate candidates[2] = {
        { .instructions = NULL, .num_instructions = 0, .cycles = 3, .energy_nj = 5.0 },
        { .instructions = NULL, .num_instructions = 0, .cycles = 5, .energy_nj = 2.0 },
    };

    const Candidate *best = select_min_energy(candidates, 2);
    assert(best == &candidates[1]);
    assert(select_min_energy(candidates, 0) == NULL);
}

int main(void) {
    test_ir_graph_push();
    test_tensor_size_overflow();
    test_select_min_energy();
    printf("test_ir: all tests passed\n");
    return 0;
}
