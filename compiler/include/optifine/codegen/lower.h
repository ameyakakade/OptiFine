/* Phase A: naive, single-candidate AVR lowering for the ML path (Input,
 * Const, MatMul, Add, Relu, Requantize, Output). This is deliberately
 * separate from candidates.c (the milestone-5 candidate-diversity module,
 * still a stub) -- lower_op always produces exactly one real, correct
 * candidate per op, using regalloc's always-spill assignment. Milestone 5
 * will later add real register-resident variants alongside this one. */
#ifndef OPTIFINE_CODEGEN_LOWER_H
#define OPTIFINE_CODEGEN_LOWER_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/codegen/candidates.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/ir.h"

/* One-time, program-global initialization: `clr r2`. r2 is used as an
 * always-zero register for carry propagation in Requantize's wide
 * multiply (see lower.c) -- mul/muls/mulsu only ever write r0:r1, so r2
 * stays zero for the whole program once cleared here. Must be emitted
 * exactly once, before any op's lowering. */
int lower_init_zero_reg(const CostModel *cost_model, Candidate *out);

/* Pre-pass: evaluates the real forward pass for `demo_input` (the one
 * fixed, compile-time-known input this Phase A program actually runs) in
 * host int64 arithmetic, using the same fixed-point Requantize logic
 * lower_op's generated AVR code uses. Must be called once, before any
 * lower_op call, so a Requantize overflow is caught before any codegen is
 * emitted. Returns 0 if every Requantize stage stays within int8 range for
 * this input, non-zero (with an error already printed to stderr)
 * otherwise -- this project does not saturate at runtime, it refuses to
 * compile instead (see lower.c for why a static worst-case-over-all-inputs
 * bound is not used: it is mathematically infeasible to satisfy for
 * MinMax-calibrated per-tensor quantization on unbounded-support inputs). */
int lower_verify_demo_forward_pass(const IrGraph *graph, const int8_t *demo_input, size_t demo_input_len);

/* Lowers a single IrOp (graph->ops[op_id]) into exactly one naive, correct
 * Candidate. `regalloc` is consulted (Phase A always takes the SRAM-spill
 * branch, regalloc->assignment[op_id] == -1 -- asserted, not silently
 * assumed). `demo_input`/`demo_input_len` are only read when
 * graph->ops[op_id].kind == OP_INPUT.
 *
 * Returns 0 on success. Returns non-zero (with an error already printed to
 * stderr) for: any DSP-path OpKind (out of scope for Phase A), or a
 * Requantize op whose compile-time-verified worst-case output would
 * overflow int8 range given the model's real weight/bias magnitudes (see
 * lower.c's compute_fixed_multiplier -- this project does not saturate at
 * runtime, it refuses to compile instead). */
int lower_op(const IrGraph *graph, size_t op_id,
             const SramLayout *layout, const RegAllocResult *regalloc,
             const CostModel *cost_model,
             const int8_t *demo_input, size_t demo_input_len,
             Candidate *out);

#endif /* OPTIFINE_CODEGEN_LOWER_H */
