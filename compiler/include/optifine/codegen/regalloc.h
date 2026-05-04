/* Module 6: next-use based register allocation -- which values stay
 * resident in AVR's limited register file vs. get reloaded from SRAM.
 * This is where most of the real engineering effort belongs.
 *
 * Scope actually implemented: AVR's 32-register file leaves only a small,
 * disjoint pool of registers never touched by any op's own intra-op
 * scratch (see codegen/lower.c's REG_* constants) -- not enough headroom
 * for a fully general cross-op allocator without risking collisions with
 * that already-validated arithmetic. What genuinely differs by value in
 * this project's graphs is *reuse count*: a MatMul's activation input is
 * read once per output channel (N times, a real, bounded-distance reuse
 * pattern -- the classic case next-use analysis exists to catch), while
 * every other op's output is consumed exactly once (no reuse to exploit).
 * regalloc_next_use marks the former; codegen/candidates.c's cached-input
 * MatMul candidate is what actually acts on that decision. A fully general
 * "any value may live in any of the 32 registers across any op boundary"
 * allocator remains a documented future extension, not implemented here. */
#ifndef OPTIFINE_CODEGEN_REGALLOC_H
#define OPTIFINE_CODEGEN_REGALLOC_H

#include <stddef.h>

#include "optifine/ir.h"

#define AVR_NUM_GP_REGISTERS 32

typedef struct {
    /* ops[i].id -> 1 if this op's output has high next-use reuse (worth a
     * consumer keeping it register-resident instead of reloading from
     * SRAM on each use), -1 if single-use (reload-per-use, Phase A's
     * baseline behavior). Not a literal register number -- see the module
     * comment above for why. */
    int *assignment;
    size_t count;
} RegAllocResult;

int regalloc_next_use(const IrGraph *graph, RegAllocResult *out);
void regalloc_result_free(RegAllocResult *result);

#endif /* OPTIFINE_CODEGEN_REGALLOC_H */
