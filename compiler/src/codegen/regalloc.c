#include "optifine/codegen/regalloc.h"

#include <stdlib.h>

int regalloc_next_use(const IrGraph *graph, RegAllocResult *out) {
    out->assignment = calloc(graph->count, sizeof(int));
    out->count = graph->count;
    for (size_t i = 0; i < graph->count; i++) {
        out->assignment[i] = -1;
    }
    /* TODO(milestone 4): next-use-distance based allocation into the 32 AVR
     * GP registers, spilling to SRAM (ST/LD) when live ranges exceed the
     * register file. */
    return 0;
}

void regalloc_result_free(RegAllocResult *result) {
    free(result->assignment);
    result->assignment = NULL;
    result->count = 0;
}
