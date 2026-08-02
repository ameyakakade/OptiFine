# Milestone 6: DSP path instruction selection, comparison, and Phase B extension

## Context

Spec v2 section 8 lists seven build milestones. Milestones 1-4 are done:
toolchain bring-up, the minimal hand-written candidate-pair proof (both
workload classes), ONNX ingestion for the ML path, and the DSP IR builder
(`dsp_build.c`: `Window -> BitReverse -> FftButterfly x6 -> Magnitude ->
PeakExtract`, fixed at `DSP_FFT_SIZE=64`). Milestone 5 (full instruction
selection) and the amendment's Phase B (sleep-mode scheduling) are also
done, but *only for the ML path* -- `candidates.c`/`lower.c` have zero case
handling for the five DSP op kinds, and `main.c` has no DSP entry point at
all. `dsp_build.c`'s own comment names this gap explicitly: "candidate
generation for DSP ops doesn't exist until milestone 5."

This spec closes that gap: real DSP instruction selection (milestone 5's
scope, DSP side), a real naive-vs-optimized comparison run through Avrora
with correctness verified against `numpy.fft` (milestone 6, matching spec
section 9's testing plan), and an extension of Phase B's sleep-scheduling
harness to the DSP pipeline -- producing a second, structurally-independent
data point for the paper's central sleep-mode-scheduling finding (the
312.606 nJ/inference result Phase A/B established for the ML classifier).

Two scoping decisions were made in brainstorming and are load-bearing for
everything below:

1. **One solid, cost-priced instruction sequence per DSP op, not a
   competing-candidates registry.** Spec section 12's amendment already
   established, for the ML path, that under Phase 1's uniform
   per-cycle-constant energy model, energy-optimal *is* cycle-optimal --
   model-independent of op type. Rebuilding milestone 5's full
   candidate-diversity/register-cache machinery for the DSP ops would
   almost certainly just re-confirm that, not produce new evidence. Effort
   goes instead into the Phase B DSP extension, which is where the actual
   new evidence lives.
2. **No loop/branch codegen.** `candidates.c`/`lower.c`/`emit.c` only ever
   emit straight-line instruction sequences -- there is no `rjmp`/`breq`/
   label machinery anywhere in the candidate-selection path. This spec
   does not add one. Every DSP op is fully unrolled at the fixed
   `DSP_FFT_SIZE=64`, `DSP_FFT_LOG2=6` compile-time constants, matching the
   project's existing "everything compile-time-baked" philosophy (Phase
   A's fixed demo input, milestone 4's fixed transform size).

## Goals

- Real, Avrora-run, cost-model-priced AVR code for all five DSP ops.
- Numerical correctness verified against `numpy.fft` for a known test
  signal, within Q15 rounding tolerance (spec section 9).
- A real, cost-priced DSP comparison report, same reporting shape as Phase
  A's ML comparison -- honestly stating whether it is naive-vs-optimized or
  a single converged build (see scoping decision 1 and the End-to-end
  comparison section below).
- The DSP pipeline wrapped in the existing Phase B periodic/Power-save
  harness, unmodified, producing a second energy-delta data point.
- `REPORT.md` updated with a Milestone 6 section covering all of the
  above.

## Non-goals

- Runtime-configurable FFT size (spec section 10, already a Phase 1
  non-goal; `DSP_FFT_SIZE` stays a compile-time constant).
- Loop/branch codegen as a general compiler capability.
- Candidate diversity / register-cache allocation for DSP ops (see scoping
  decision 1 above).
- Flash-resident (`.rodata`+`lpm`) constant loading for window
  coefficients or twiddle factors -- stays `ldi`+`sts`, consistent with
  the ML path's already-documented limitation. Not revisited here.
- General-purpose peak-picking (arbitrary peak count, adaptive threshold)
  -- `DSP_MAX_PEAKS=8`, fixed, per milestone 4's existing IR shape.

## Architecture

No new orchestration layer. `program.c`'s `codegen_emit_program`/
`codegen_emit_initialization`/`codegen_emit_inference_body` and
`periodic.c`'s `codegen_emit_periodic_program` already iterate
`graph->ops[i]` generically through `lower_op()` -- nothing ML-specific is
hardcoded there. Concretely:

1. **`lower.c` grows five new cases** in its existing switch statement:
   `OP_WINDOW`, `OP_BIT_REVERSE`, `OP_FFT_BUTTERFLY`, `OP_MAGNITUDE`,
   `OP_PEAK_EXTRACT`. Same file, same `instr_buf.h` helpers
   (`instrbuf_init`/`ins1`/`ins2`/`instrbuf_price`) milestone 5 already
   uses for the ML ops.
2. **`sram_layout.c` extended** to lay out `DT_COMPLEX_Q15` (interleaved
   real/imaginary `int16` pairs) alongside the existing `DT_FIXED_Q15`
   handling. The full working buffer is 64 samples x 4 bytes = 256 bytes
   -- trivial against ATmega128's 4KB SRAM, no budget concern.
3. **`main.c` gains a `--dsp` flag/entry point** that calls
   `dsp_build_pipeline()` instead of `ingest.c`'s ONNX parser, then feeds
   the resulting `IrGraph` through the *same* `codegen_emit_program` /
   `codegen_emit_periodic_program` calls the ML path already uses. Phase
   B's sleep-scheduling wrapper requires no code changes -- it operates on
   whatever `IrGraph` it's handed.

## Per-op instruction strategy

Because `DSP_FFT_SIZE`/`DSP_FFT_LOG2` are compile-time constants, the
entire butterfly network -- which SRAM slot pair each of the 6x32=192
butterflies touches, and which twiddle-factor value each one needs -- is
knowable at *compiler-build* time (i.e. computed by the C program that
generates AVR assembly), not target-runtime. This is the key move that
avoids needing any runtime addressing or loop machinery:

- **Window** (64x, unrolled): one `fmuls` per sample (real Q15 sample x
  real Q15 window coefficient). AVR's `fmuls` does a signed fractional
  multiply with the Q15 left-shift built in -- exactly the right
  primitive, already costed as `FIXED_MUL_Q15` in `cost_table.toml`.
  Coefficients (Hamming window) are computed host-side via libm at
  compiler-build time, quantized to Q15, and loaded via the existing
  `ldi`+`sts` constant pattern.
- **BitReverse** (64x, unrolled): a fixed, compile-time-computed index
  permutation for N=64. Pure `lds`/`sts` data movement -- no arithmetic,
  no new cost category.
- **FftButterfly** (6 stages x 32 butterflies = 192x, unrolled): twiddle
  factors computed host-side via libm `cos`/`sin`, quantized to Q15, and
  `ldi`-loaded as per-butterfly constants -- no target-side trigonometry,
  no runtime table indexing. Each butterfly is a Q15 complex
  multiply-accumulate: `fmuls`-based complex multiply (real x real, real x
  imag, etc.) combined via `COMPLEX_ADD`'s existing 2x `add`/`sub`
  pattern. Both cost categories (`FIXED_MUL_Q15`, `COMPLEX_ADD`) already
  exist in `cost_table.toml`; this task wires them to actual mnemonics in
  `cost_category.c` (currently unmapped).
- **Magnitude** (64x, unrolled): real integer Q15 square root --
  `sqrt(re^2 + im^2)`, computed via a small fixed-length unrolled
  digit-by-digit or Newton-iteration integer sqrt routine (a handful of
  new 1-cycle-bucket cost-category entries for whatever compare/shift
  instructions the routine needs, following the existing precedent of
  bucketing same-cycle-count instructions together, e.g. `asr`/`ror`
  already share `SUB`'s bucket). Chosen over an approximation (alpha-max-
  beta-min) or magnitude-squared specifically so the correctness test
  compares directly against `numpy.fft`'s true magnitude spectrum, no
  re-derivation of the reference needed.
- **PeakExtract** (unrolled scan): fixed top-`DSP_MAX_PEAKS`(=8)
  local-maxima extraction over the 64 magnitude values, comparison-based,
  fully unrolled (no loop).

## Correctness testing

Mirrors the existing `lower_verify_demo_forward_pass` pattern in
`test_lower.c` (spec section 9's "correctness tests on generated code, not
just energy comparisons" requirement). A new equivalent -- e.g.
`dsp_verify_demo_pipeline`, or a `sim/`-side script if easier to compute
the `numpy.fft` reference in Python -- does:

1. Compute a `numpy.fft` reference spectrum for a fixed synthetic test
   signal (a small number of superposed sinusoids, chosen so the expected
   peak bins are known and checkable, not random noise).
2. Quantize the same signal to Q15, run it through the compiled program in
   Avrora, and capture the emitted peak list from SRAM.
3. Compare against the `numpy.fft`-derived reference within Q15 rounding
   tolerance -- both magnitude values and peak bin locations.

This test is a hard correctness gate, same standing as
`lower_verify_demo_forward_pass`: a lower-energy sequence that computes
the wrong answer is a failed test, full stop (spec section 9).

## End-to-end comparison + Phase B extension

Same shape as Phase A/B's existing results:

- **Comparison run.** Compile the DSP pipeline, run through Avrora,
  extract cycles/energy the same way `sim/run_phase_b.py`/
  `sim/compare_phase_b.py` already do for the ML path. Given scoping
  decision 1 (one sequence per op, not competing candidates), "naive vs.
  optimized" for DSP most likely collapses to reporting one real,
  cost-priced build's cycles/energy rather than two builds that turn out
  identical by construction -- this is itself worth stating plainly in
  `REPORT.md` (same honesty precedent as the ML path's milestone-5
  convergence finding), not glossed over as if diversity were attempted
  and happened to converge.
- **Phase B extension.** Wrap the DSP inference (one full
  `Window -> ... -> PeakExtract` pass) as the periodic wake body, reusing
  `periodic.c`'s existing active/Power-save split, shared-BODY `memcmp`
  invariant, and 4-prescaler sweep -- unchanged. This produces a second,
  structurally-independent energy-delta result to sit alongside the ML
  classifier's 312.606 nJ/inference finding.
- **`REPORT.md`** gets a new Milestone 6 section: methodology (mirroring
  Phase A/B's existing section structure), the DSP comparison result, the
  Phase B DSP-extension result, and the correctness-verification summary.

## Testing plan

- Unit test for each new `lower.c` DSP case (candidate priced, no missing
  cost-category error) -- same pattern as existing `test_lower.c` cases.
- `dsp_verify_demo_pipeline`-style correctness test against `numpy.fft`
  (see above) -- hard gate.
- Real Avrora run of the full DSP pipeline (naive/single-build), same
  validate-before-automating discipline as every other empirical claim in
  this project -- no register address, twiddle value, or timing number
  assumed without either a citation or an empirical Avrora run confirming
  it.
- Real Avrora run of the Phase B DSP extension across the existing
  4-prescaler sweep, reusing `sim/run_phase_b.py`'s existing harness
  pointed at the DSP build instead of the ML build.

## Open risks / things to confirm during implementation

- Exact integer-sqrt routine choice (digit-by-digit vs. Newton) and its
  precision/cycle-count tradeoff at Q15 -- pick during implementation,
  validate against `numpy.fft` rather than deciding from first principles.
- Whether `sram_layout.c`'s existing allocator generalizes cleanly to a
  256-byte complex working buffer or needs real extension -- verify early,
  before committing to the full unrolled butterfly network.
- Total emitted program size (192 butterflies + 64 window multiplies + 64
  sqrt routines + peak scan, fully unrolled) -- expected to fit AVR flash
  comfortably (128KB) but worth a sanity check on `.elf` size once the
  first full build compiles.
