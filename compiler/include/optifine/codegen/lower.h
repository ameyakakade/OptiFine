/* Single-candidate AVR lowering for every op of both paths: the ML path
 * (Input, Const, MatMul, Add, Relu, Requantize, Output -- the naive
 * baseline) and the DSP path (Window, BitReverse, FftButterfly, Magnitude,
 * PeakExtract, with counted loops). lower_op always produces exactly one real,
 * correct candidate per op, using regalloc's always-spill assignment;
 * candidates.c adds its register-resident alternatives for ML ops. */
#ifndef OPTIFINE_CODEGEN_LOWER_H
#define OPTIFINE_CODEGEN_LOWER_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/codegen/candidates.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/ir.h"

/* One-time, program-global initialization: `clr r2`. r2 is used as an
 * always-zero register for carry propagation in Requantize's wide
 * multiply (see lower_requantize.c) -- mul/muls/mulsu only ever write r0:r1, so r2
 * stays zero for the whole program once cleared here. Must be emitted
 * exactly once, before any op's lowering. */
int lower_init_zero_reg(const CostModel *cost_model, Candidate *out);

/* Pre-pass: evaluates the real forward pass for `demo_input` (the one
 * fixed, compile-time-known input this ML program actually runs) in
 * host int64 arithmetic, using the same fixed-point Requantize logic
 * lower_op's generated AVR code uses. Must be called once, before any
 * lower_op call, so a Requantize overflow is caught before any codegen is
 * emitted. Returns 0 if every Requantize stage stays within int8 range for
 * this input, non-zero (with an error already printed to stderr)
 * otherwise -- this project does not saturate at runtime, it refuses to
 * compile instead (see lower_requantize.c for why a static worst-case-over-all-inputs
 * bound is not used: it is mathematically infeasible to satisfy for
 * MinMax-calibrated per-tensor quantization on unbounded-support inputs). */
int lower_verify_demo_forward_pass(const IrGraph *graph, const int8_t *demo_input, size_t demo_input_len);

/* Lowers a single IrOp (graph->ops[op_id]) into exactly one naive, correct
 * Candidate. `regalloc` is consulted (the naive path always takes the SRAM-spill
 * branch, regalloc->assignment[op_id] == -1 -- asserted, not silently
 * assumed). `demo_input`/`demo_input_len` are only read when
 * graph->ops[op_id].kind == OP_INPUT.
 *
 * Returns 0 on success. Returns non-zero (with an error already printed to
 * stderr) for: an OpKind with no lowering, a DSP op whose tensor shapes or
 * dtypes are not the ones its lowering handles, or a Requantize op whose compile-time-verified worst-case output would
 * overflow int8 range given the model's real weight/bias magnitudes (see
 * lower_requantize.c's compute_fixed_multiplier -- this project does not saturate at
 * runtime, it refuses to compile instead). */
int lower_op(const IrGraph *graph, size_t op_id,
             const SramLayout *layout, const RegAllocResult *regalloc,
             const CostModel *cost_model,
             const int8_t *demo_input, size_t demo_input_len,
             Candidate *out);

/* Test-only seam for test_dsp_lower.c -- see lower_dsp.c's lower_fixed_mul_q15
 * comment. Not part of the codegen pipeline's real call path. */
int lower_fixed_mul_q15_test_hook(uint16_t a_addr, uint16_t b_addr, uint16_t out_addr,
                                   const CostModel *cost_model, Candidate *out);

/* Canonical Q15 twiddle, shared by the table emitter and the tests' oracle. */
void dsp_twiddle_q15(int k, int16_t *wr, int16_t *wi);
/* Emits the 32-entry canonical twiddle table as program-memory data. Must be
 * placed where control flow cannot reach it. */
void dsp_emit_twiddle_table(InstrBuf *buf);

int lower_fft_stage0_test_hook(const IrGraph *graph, size_t op_id,
                                const SramLayout *layout, const CostModel *cost_model,
                                Candidate *out);

/* The end of a DSP program: the terminating `break`, priced like any other
 * instruction (1 cycle), then -- only if the graph has FFT stages -- the one
 * canonical twiddle table they read through lpm. The table must follow the
 * break so control flow can never reach it, and must be emitted exactly once
 * per assembly unit (EmitUnit refuses a second .Ltw). */
int lower_dsp_program_end(const IrGraph *graph, const CostModel *cost_model, Candidate *out);

/* Just the program-memory constant data lower_dsp_program_end places after its
 * break -- the twiddle table if the graph has FFT stages, otherwise an empty
 * candidate -- for a program whose terminating code is written elsewhere (the
 * periodic scheduling wrapper). The caller must place it where control flow
 * cannot reach it. */
int lower_dsp_constant_data(const IrGraph *graph, const CostModel *cost_model, Candidate *out);

/* FFT stages first_stage..last_stage (0-based, inclusive) followed by break
 * and the twiddle table, as one self-contained program. */
int lower_fft_stages_test_hook(const IrGraph *graph, int first_stage, int last_stage,
                               const SramLayout *layout, const CostModel *cost_model,
                               Candidate *out);

/* Test seam for OP_BIT_REVERSE's permutation (see lower_dsp.c). */
size_t dsp_bit_reverse_index_test_hook(size_t i, int bits);

#endif /* OPTIFINE_CODEGEN_LOWER_H */
