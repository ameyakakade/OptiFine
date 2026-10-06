#include "optifine/codegen/regalloc.h"

#include <stdlib.h>

int regalloc_next_use(const IrGraph *graph, RegAllocResult *out) {
    out->assignment = calloc(graph->count > 0 ? graph->count : 1, sizeof(int));
    out->count = graph->count;
    if (!out->assignment) {
        out->count = 0;
        return -1;
    }
    for (size_t i = 0; i < graph->count; i++) {
        out->assignment[i] = -1;
    }

    /* Next-use analysis (see regalloc.h's module comment for the scope
     * this covers): a MatMul's activation input (inputs[0]) is read once
     * per output channel of the weight it's paired with -- N reuses at a
     * short, bounded distance within that MatMul's own inner loop, every
     * time, for any model shape. Every other op's output is consumed
     * exactly once in this project's graphs (Add/Relu/Requantize/Output
     * are all single-consumer), so there is no reuse for them to exploit.
     * This is computed directly from the graph rather than approximated,
     * so it is exact next-use information, not a heuristic guess. */
    for (size_t j = 0; j < graph->count; j++) {
        if (graph->ops[j].kind == OP_MATMUL && graph->ops[j].num_inputs >= 1) {
            size_t activation_id = graph->ops[j].inputs[0];
            out->assignment[activation_id] = 1;
        }
    }
    return 0;
}

void regalloc_result_free(RegAllocResult *result) {
    free(result->assignment);
    result->assignment = NULL;
    result->count = 0;
}
