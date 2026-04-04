#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include "optifine/codegen/select.h"
#include "optifine/ir.h"

static void test_ir_graph_push(void) {
    IrGraph graph;
    ir_graph_init(&graph);

    size_t id0 = ir_graph_push(&graph, OP_INPUT, NULL, 0, NULL, 0, DT_INT8, NULL);
    assert(id0 == 0);
    assert(graph.count == 1);
    assert(graph.ops[0].kind == OP_INPUT);

    size_t *inputs = malloc(sizeof(size_t));
    inputs[0] = id0;
    size_t id1 = ir_graph_push(&graph, OP_RELU, inputs, 1, NULL, 0, DT_INT8, NULL);
    assert(id1 == 1);
    assert(graph.ops[1].num_inputs == 1);
    assert(graph.ops[1].inputs[0] == id0);

    ir_graph_free(&graph);
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
    test_select_min_energy();
    printf("test_ir: all tests passed\n");
    return 0;
}
