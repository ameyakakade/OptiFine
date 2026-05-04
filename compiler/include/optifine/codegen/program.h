/* Orchestrates per-op lowering for the whole graph into one flat AVR .s
 * program. Each op's candidate(s) are routed through select_min_energy
 * (a real cost-based pick when candidates_generate returns more than one,
 * a pass-through otherwise) and locality_optimize (still a no-op). See
 * codegen_emit_program's `use_real_candidates` parameter for the choice
 * between Phase A's naive baseline and milestone 5's real candidate
 * diversity. */
#ifndef OPTIFINE_CODEGEN_PROGRAM_H
#define OPTIFINE_CODEGEN_PROGRAM_H

#include <stdint.h>
#include <stdio.h>

#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/ir.h"

typedef struct {
    double prologue_energy_nj; /* OP_INPUT + OP_CONST loading, plus the one-time zero-register init */
    uint32_t prologue_cycles;
    double body_energy_nj;     /* everything else -- the actual inference computation */
    uint32_t body_cycles;
} ProgramCost;

/* Writes a complete AVR .s program for `graph` to `out`, wrapped with the
 * .arch/.section/_start:/break boilerplate. `demo_input`/`demo_input_len`
 * feed OP_INPUT (see codegen/lower.h). `use_real_candidates` selects the
 * per-op lowering strategy: 0 calls lower_op directly (Phase A's naive
 * baseline, one candidate per op, byte-for-byte the original behavior);
 * 1 calls codegen/candidates.h's candidates_generate instead, letting
 * select_min_energy pick the cheapest of however many real candidates it
 * returns (milestone 5). Returns 0 on success, non-zero (with an error
 * already printed to stderr) if any op fails to lower. */
int codegen_emit_program(const IrGraph *graph, const SramLayout *layout,
                          const RegAllocResult *regalloc, const CostModel *cost_model,
                          const int8_t *demo_input, size_t demo_input_len,
                          int use_real_candidates,
                          FILE *out, ProgramCost *out_cost);

#endif /* OPTIFINE_CODEGEN_PROGRAM_H */
