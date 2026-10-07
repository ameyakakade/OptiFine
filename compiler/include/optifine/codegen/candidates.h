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

#include "optifine/codegen/avr_instr.h"
#include "optifine/codegen/reuse_analysis.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/ir.h"

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
