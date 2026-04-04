/* Module 6: candidate instruction sequences. For each IrOp, codegen must
 * produce at least two provably-equivalent candidates that differ in
 * register/memory access pattern, addressing mode, or operand order. */
#ifndef OPTIFINE_CODEGEN_CANDIDATES_H
#define OPTIFINE_CODEGEN_CANDIDATES_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/ir.h"

#define AVR_MAX_OPERANDS 3
#define AVR_MNEMONIC_LEN 8
#define AVR_OPERAND_LEN 16

typedef struct {
    char mnemonic[AVR_MNEMONIC_LEN];
    char operands[AVR_MAX_OPERANDS][AVR_OPERAND_LEN];
    int num_operands;
} AvrInstr;

typedef struct {
    AvrInstr *instructions;
    size_t num_instructions;
    uint32_t cycles;    /* for reference/comparison, not the selection criterion */
    double energy_nj;   /* computed from the cost table */
} Candidate;

void candidate_free(Candidate *candidate);

/* Generates the candidate instruction sequences for `op`, writing them into
 * `out_candidates` (caller-allocated, capacity `max_candidates`). Returns
 * the number of candidates written. */
size_t candidates_generate(const IrOp *op, Candidate *out_candidates, size_t max_candidates);

#endif /* OPTIFINE_CODEGEN_CANDIDATES_H */
