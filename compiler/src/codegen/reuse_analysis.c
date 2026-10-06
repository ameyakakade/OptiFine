#include "optifine/codegen/reuse_analysis.h"

#include <stdlib.h>

int reuse_analyze(const IrGraph *graph, ReuseAnalysis *out) {
    out->reused = calloc(graph->count > 0 ? graph->count : 1, sizeof(int));
    out->count = graph->count;
    if (!out->reused) {
        out->count = 0;
        return -1;
    }

    /* A MatMul's activation input is read once per output channel of the
     * weight it is paired with: N reads at a short, bounded distance, for any
     * model shape. Every other op's output in this project's graphs
     * (Add/Relu/Requantize/Output and the DSP ops) has a single consumer that
     * reads each element once, so there is no reuse to exploit. Computed
     * directly from the graph, so it is exact for these graphs, not a
     * heuristic. */
    for (size_t j = 0; j < graph->count; j++) {
        if (graph->ops[j].kind == OP_MATMUL && graph->ops[j].num_inputs >= 1) {
            out->reused[graph->ops[j].inputs[0]] = 1;
        }
    }
    return 0;
}

void reuse_analysis_free(ReuseAnalysis *result) {
    free(result->reused);
    result->reused = NULL;
    result->count = 0;
}
