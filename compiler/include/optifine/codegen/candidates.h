/* Module 6: candidate instruction sequences. For each IrOp, codegen must
 * produce at least two provably-equivalent candidates that differ in
 * register/memory access pattern, addressing mode, or operand order.
 *
 * candidates_generate's signature mirrors lower_op's (codegen/lower.h) --
 * generating a real, priced alternative needs the same context lower_op
 * needs (SRAM addresses, the regalloc decision, the cost model), not just
 * the single IrOp being lowered. This is a deliberate extension beyond
 * this header's original stub signature, matching the same precedent
 * lower.h and ir.h already established. */
#ifndef OPTIFINE_CODEGEN_CANDIDATES_H
#define OPTIFINE_CODEGEN_CANDIDATES_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/ir.h"

#define AVR_MAX_OPERANDS 3
/* `mnemonic` holds the literal AVR opcode as emitted to .s (e.g. "muls",
 * "adc"), not a cost_table.toml key -- see codegen/cost_category.h for the
 * mapping from opcode to cost-table category. Longest current opcode is
 * "mulsu" (5 chars). */
#define AVR_MNEMONIC_LEN 16
/* Widened from 16 for the DSP path's symbolic program-memory operands:
 * "lo8(.Ltw+124)" does not fit 16 bytes, and snprintf truncates silently,
 * which produced a wrong-but-plausible address rather than an error. The
 * formatters now detect truncation outright; this keeps ordinary operands
 * comfortably inside the buffer. */
#define AVR_OPERAND_LEN 24

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

/* Generates the candidate instruction sequences for graph->ops[op_id],
 * writing them into `out_candidates` (caller-allocated, capacity
 * `max_candidates`). Every candidate is already priced (Candidate::cycles/
 * energy_nj populated). Returns the number of candidates written, or 0 on
 * error (with a message already printed to stderr) -- 0 is never a valid
 * successful count, every real IrOp lowers to at least one candidate. */
size_t candidates_generate(const IrGraph *graph, size_t op_id,
                            const SramLayout *layout, const RegAllocResult *regalloc,
                            const CostModel *cost_model,
                            const int8_t *demo_input, size_t demo_input_len,
                            Candidate *out_candidates, size_t max_candidates);

#endif /* OPTIFINE_CODEGEN_CANDIDATES_H */
