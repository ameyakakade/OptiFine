/* Candidate instruction sequences for one op, priced, for select_min_energy
 * to choose between (the --optimized path).
 *
 * Every op gets lower_op's naive candidate. Only OP_MATMUL gets a second,
 * equivalent one: the cached-input form, which loads the reused activation
 * into registers once (reuse_analysis.h). Every other op, ML or DSP, has
 * exactly one candidate, so for them selection is a pass-through.
 *
 * candidates_generate takes the same context as lower_op (codegen/lower.h):
 * SRAM addresses, the reuse analysis and the cost model. */
#ifndef OPTIFINE_CODEGEN_CANDIDATES_H
#define OPTIFINE_CODEGEN_CANDIDATES_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/codegen/reuse_analysis.h"
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
    uint32_t cycles;    /* executed cycles, from the AVR timing table; not the selection criterion */
    double energy_nj;   /* predicted (simulated-model) energy, from the cost table */
} Candidate;

void candidate_free(Candidate *candidate);

/* Generates the candidate instruction sequences for graph->ops[op_id],
 * writing them into `out_candidates` (caller-allocated, capacity
 * `max_candidates`). Every candidate is already priced (Candidate::cycles/
 * energy_nj populated). Returns the number of candidates written, or 0 on
 * error (with a message already printed to stderr) -- 0 is never a valid
 * successful count, every real IrOp lowers to at least one candidate. */
size_t candidates_generate(const IrGraph *graph, size_t op_id,
                            const SramLayout *layout, const ReuseAnalysis *reuse,
                            const CostModel *cost_model,
                            const int8_t *demo_input, size_t demo_input_len,
                            Candidate *out_candidates, size_t max_candidates);

#endif /* OPTIFINE_CODEGEN_CANDIDATES_H */
