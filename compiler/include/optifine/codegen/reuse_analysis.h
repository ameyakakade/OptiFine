/* Value-reuse analysis over the workload graph.
 *
 * This is NOT a register allocator. It answers one question per op: is this
 * op's output read repeatedly by a single consumer, so that the consumer
 * could profitably keep it in registers instead of reloading it from SRAM on
 * every use? In this project's graphs exactly one pattern qualifies: a
 * MatMul's activation input (inputs[0]) is read once per output channel, N
 * times at a short, bounded distance. Every other output is consumed exactly
 * once.
 *
 * The only reader is codegen/candidates.c's cached-input MatMul candidate,
 * which holds that activation in a fixed pool of registers
 * (kMatmulCacheRegs, registers.h) for the duration of one MatMul. The naive
 * lowering (lower_op) ignores the result. No value is kept in a register
 * across an op boundary, and every register use inside an op follows the
 * fixed contracts in registers.h; a general allocator (live ranges,
 * interference, spilling) does not exist yet. It belongs below the generic
 * MIR (docs/ARCHITECTURE.md), not here. */
#ifndef OPTIFINE_CODEGEN_REUSE_ANALYSIS_H
#define OPTIFINE_CODEGEN_REUSE_ANALYSIS_H

#include <stddef.h>

#include "optifine/ir.h"

typedef struct {
    /* Indexed by op id: 1 if that op's output is reused by one consumer
     * (worth caching in registers there), 0 if it is read once. */
    int *reused;
    size_t count;
} ReuseAnalysis;

/* Computes the analysis for a verified graph. Returns 0, or -1 when the
 * result cannot be allocated. */
int reuse_analyze(const IrGraph *graph, ReuseAnalysis *out);
void reuse_analysis_free(ReuseAnalysis *result);

#endif /* OPTIFINE_CODEGEN_REUSE_ANALYSIS_H */
