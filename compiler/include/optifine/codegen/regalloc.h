/* Module 6: next-use based register allocation -- which values stay
 * resident in AVR's limited register file vs. get reloaded from SRAM.
 * This is where most of the real engineering effort belongs. */
#ifndef OPTIFINE_CODEGEN_REGALLOC_H
#define OPTIFINE_CODEGEN_REGALLOC_H

#include <stddef.h>

#include "optifine/ir.h"

#define AVR_NUM_GP_REGISTERS 32

typedef struct {
    /* ops[i].id -> assigned register, or -1 if spilled to SRAM */
    int *assignment;
    size_t count;
} RegAllocResult;

int regalloc_next_use(const IrGraph *graph, RegAllocResult *out);
void regalloc_result_free(RegAllocResult *result);

#endif /* OPTIFINE_CODEGEN_REGALLOC_H */
