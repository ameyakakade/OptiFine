# Project Build Spec: Energy-Aware Code Generation for Edge DSP and ML Inference

> **Historical development document.** Written during development, it uses
> the project's internal stage names, which current documentation replaces:
> "Phase A" is **active-mode optimization**, "Phase B" is **periodic
> power-aware scheduling**, and "milestone 6" is the **DSP workload**
> (the 64-point Q15 pipeline). Kept as the record of how the design was
> reached; for current behaviour and results see `README.md` and `REPORT.md`.

**Status:** Ready for implementation (Phase 1)
**Author:** Partha
**Type:** Individual compiler-design project (AVR back end, simulation-validated) —
also the empirical core of a planned submission to *Journal of Systems
Architecture* (JSA).

This document is written to be handed directly to a coding agent (Claude Code
or similar) to implement Phase 1. It specifies scope, architecture, concrete
data formats, and an explicit non-goals list so the agent doesn't wander into
out-of-scope work. Where a decision was made for you (language, target ISA,
validation method, FFT scope), the reasoning is given so it can be challenged
and changed before work starts.

This supersedes the earlier ML-only draft of this spec. The two changes that
matter most: (1) a second, structurally different workload — audio/DSP — is
now first-class, not a stretch goal; (2) physical hardware is reframed from a
permanent non-goal into deferred Phase 2 work, because the eventual paper's
central claim (simulated energy numbers are trustworthy) depends on it.

---

## 1. Goal

Build a compiler backend that takes IR from two different sources — an
INT8-quantized neural network (via ONNX) and a fixed-size audio/DSP pipeline
(hand-built, no standard interchange format exists for this) — and compiles
both to AVR machine code, choosing between multiple valid instruction
sequences at each step based on **estimated energy cost**, not cycle count.
Prove the effect by compiling each workload two ways — speed-optimized and
energy-aware — and comparing both through a cycle-accurate simulator.

**This is not a claim of beating any production ML or DSP compiler.** It is a
demonstration that a single compiler backend can treat energy as a
first-class cost function during code generation, across workload classes
with genuinely different instruction-access patterns (batched matrix ops vs.
streaming butterfly/MAC operations), validated end-to-end without physical
hardware in Phase 1.

The research framing this spec supports: does optimizing for energy produce
different code than optimizing for cycles, how often, and by how much, across
two structurally different workload classes on the same backend. That
divergence — not the existence of an energy-aware compiler alone — is the
paper's actual contribution. See Section 11 for how this maps to the JSA
submission.

## 2. Two-Phase Structure

- **Phase 1 (this spec's scope, course deliverable).** AVR + Avrora
  simulation only. Both workloads. No physical hardware. This is sufficient
  to satisfy the course project and produce the first full set of results.
- **Phase 2 (not built by this spec, stubbed for later).** Sim-to-hardware
  correlation: run the same AVR binaries Avrora simulates on a real
  ATmega328P/2560 with an INA219/INA226 current-sense breakout, to validate
  that Avrora's simulated energy numbers track real measurement. Optionally,
  a cross-architecture generalization check on an ARM Cortex-M55 target
  (STM32N657X0, e.g. the NUCLEO-N657X0-Q board) to show the optimization
  approach isn't AVR-specific.

**Phase 1 alone is a complete, defensible course project.** It is not, on its
own, sufficient evidence for the JSA submission's central energy-trustworthiness
claim — a reviewer's first question will be "how do you know the simulated
numbers mean anything," and Phase 1 has no answer to that. Phase 2 is what
closes that gap. Do not conflate the two: build and ship Phase 1 completely
first, then treat Phase 2 as a separate follow-on effort once board access
and timeline are settled.

## 3. Hard Constraints — Phase 1 (do not violate these)

- **No physical hardware in Phase 1.** No target board, no power meter.
  Every energy number in Phase 1 comes from either a cited published source
  (for the cost table) or from Avrora's simulated energy monitor. Nothing in
  the code, comments, or report may imply a Phase 1 number was physically
  measured.
- **AVR only.** Target ATmega328P/ATmega2560-class AVR microcontrollers for
  codegen. AVR is the one ISA with a free, open-source, cycle-accurate
  simulator (Avrora) with built-in per-component energy monitoring. Do not
  generalize to ARM Cortex-M or RISC-V in Phase 1 — that's Phase 2.
- **Two workload classes, both first-class.**
  - **ML:** INT8-quantized neural network, ingested from ONNX.
  - **DSP:** fixed-size audio pipeline (windowing through peak extraction),
    ingested via a hand-built IR construction API — see Section 6.2 for why
    there's no file-based frontend for this path.
- **FFT is fixed-size, not general-purpose.** Support exactly one hardcoded
  transform length at a time (64 or 128 points, power-of-2, radix-2
  decimation-in-time). No arbitrary-N support, no runtime-configurable size.
  This is a deliberate, stated scope limit — treat it the same way the ML
  path's limited `OpKind` set is scoped to the demo model's needs, not
  general coverage.
- **No ML-compiler-framework or DSP-framework dependency.** Do not use TVM,
  Glow, IREE, TFLite Micro, CMSIS-DSP, or similar as an implementation
  dependency. Reference in documentation only, never import or wrap.
- **Every energy cost table entry needs a cited source.** A datasheet
  section, a specific paper, or an explicit `# PLACEHOLDER — needs sourcing`
  marker. Never a bare, unattributed number presented as real.

## 4. Recommended Stack

- **ML model export (`export/`):** Python + PyTorch + `torch.onnx.export`.
  Existing tooling, out of scope for the "built by hand" part of the
  project.
- **DSP pipeline construction:** no export step — built directly as IR via
  a Rust builder API (Section 6.2). No external DSP framework.
- **Compiler core (`compiler/`):** Rust. Matches prior systems-level work;
  AVR code generation benefits from Rust's control over memory layout and
  lack of a runtime. Swappable if a different language is preferred —
  nothing else in this spec depends on the choice.
  **Amendment (2026-08-26):** implemented in C11 + CMake instead; the
  scaffolding already existed in C and the spec's language recommendation is
  explicitly swappable. All module boundaries below are language-agnostic.
- **Simulation (`sim/`):** Avrora (Java, invoked via subprocess) +
  `avr-gcc`/`avr-as`/`avr-objcopy` to assemble emitted `.s` files into an
  ELF Avrora can load.

## 5. Architecture

```
                ONNX Export --> ONNX Ingest ----\
                (existing)      (this proj)      \
                                                   >-- Custom IR --> AVR Target        Energy-Cost         Emit AVR      Avrora         Comparison
                DSP Builder API ------------------/    (typed op       Selection  -->  Instruction   -->  Assembly  -->  Simulation --> & Report
                (hand-built,                            graph)        [this proj]      Selection          [this proj]    [validation]
                 this proj, no                          [this proj]                    [this proj,
                 file format)                                                          centerpiece]
```

Both ingestion paths feed the same downstream pipeline (IR → target
selection → energy-cost instruction selection → emit → simulate). That
shared backend, exercised identically across two structurally different
workload classes, is the point.

## 6. Component Specs

### 6.1 Custom IR

A typed op graph, not source text.

```rust
struct IrOp {
    id: usize,
    kind: OpKind,
    inputs: Vec<usize>,       // producer op ids
    output_shape: Vec<usize>,
    dtype: DType,
    quant: Option<QuantParams>, // scale, zero_point — ML path only
}

enum OpKind {
    // ML path
    Input, Const, MatMul, Add, Relu, Requantize, Output,
    // DSP path
    Window,        // elementwise multiply by a fixed window (e.g. Hamming)
    BitReverse,    // bit-reversal permutation ahead of the butterfly stages
    FftButterfly,  // one radix-2 DIT butterfly stage
    Magnitude,     // complex magnitude from FFT output
    PeakExtract,   // top-8 magnitude values, largest first (as implemented: values only)
}

enum DType {
    Int8, Int32,      // ML path (activations / accumulators)
    FixedQ15,         // DSP path — Q15 fixed-point real
    ComplexQ15,       // DSP path — Q15 fixed-point complex (re, im pair)
}

struct QuantParams { scale: f32, zero_point: i32 }
```

Scope note: fingerprint hash generation (the final stage in a real
audio-fingerprinting pipeline) is **out of scope**. It's lightweight
post-processing, not the energy-relevant compute kernel this project is
about. The DSP pipeline ends at `PeakExtract`.

Scope the supported `OpKind` set to exactly what each demo workload needs.
Don't build general ONNX operator coverage or a general DSP operator
library.

### 6.2 DSP Ingestion — Builder API, Not a Parser

Unlike the ML path, there is no standard portable graph format for a
fixed-size audio/DSP pipeline to parse from. Construct the IR directly:

```rust
// dsp/build.rs
fn build_dsp_pipeline(fft_size: usize) -> IrGraph {
    // fft_size fixed at IR-construction time (64 or 128 — pick one and
    // hardcode it; do not build runtime size selection).
    // Window -> BitReverse -> log2(fft_size) FftButterfly stages
    //   -> Magnitude -> PeakExtract
}
```

This is a deliberate asymmetry with the ML path's ONNX ingestion, not an
oversight — flag it as such in the report rather than trying to paper over
it with a fake file format.

### 6.3 AVR Code Generation (the centerpiece)

Unchanged mechanism from the original spec, now exercised across both
workload classes:

```rust
struct Candidate {
    instructions: Vec<AvrInstr>,
    cycles: u32,       // reference only, not the selection criterion
    energy_nj: f64,    // computed from the cost table below
}

fn select(candidates: &[Candidate]) -> &Candidate {
    candidates.iter().min_by(|a, b| a.energy_nj.total_cmp(&b.energy_nj)).unwrap()
}
```

For each `IrOp` — ML or DSP — generate **at least two** candidate
instruction sequences, provably equivalent, differing in register/memory
access pattern, addressing mode, or operand order.

`FftButterfly` candidates specifically should vary in: operand order for
the fixed-point complex multiply, whether intermediate products stay
register-resident vs. spill to SRAM, and instruction sequencing around the
Q15 rounding step. `BitReverse` is a natural place for the memory-locality
pass (6.6) to show a concrete, explainable energy delta, since its access
pattern is non-sequential by construction.

Register allocation uses next-use information (Module 6) — this is where
most of the real engineering effort belongs, for both workload classes.

### 6.4 Energy Cost Table

```toml
# cost_table.toml — every entry needs a `source` before it's treated as real.
[instructions]
MUL          = { energy_nj = 0.0, source = "PLACEHOLDER — cite ATmega328P datasheet section or paper" }
ADD          = { energy_nj = 0.0, source = "PLACEHOLDER" }
LD_SRAM      = { energy_nj = 0.0, source = "PLACEHOLDER" }
ST_SRAM      = { energy_nj = 0.0, source = "PLACEHOLDER" }
FIXED_MUL_Q15 = { energy_nj = 0.0, source = "PLACEHOLDER — DSP path" }
COMPLEX_ADD  = { energy_nj = 0.0, source = "PLACEHOLDER — DSP path" }
```

First real task before any numbers go in: find and cite actual published
per-instruction or per-cycle energy figures for AVR. Log every source in
`SOURCES.md` alongside the number it justifies. This file doubles as part
of the JSA research-data disclosure — see Section 11.

### 6.5 Basic-Block Cost Comparison (Module 5)

Unchanged: apply `select()` at the basic-block level, comparator swapped
from cycle count to `energy_nj`. Applies identically to ML and DSP basic
blocks.

### 6.6 Memory Locality Pass (Module 7)

SRAM access costs meaningfully more energy than register arithmetic on
AVR-class hardware. Add a pass reordering instructions or restructuring
access within a basic block to maximize register/data reuse. `BitReverse`
in the DSP path and the matmul accumulation loop in the ML path are both
good targets to report before/after numbers for.

### 6.7 Avrora Integration

- Emit AVR assembly text (`emit.rs`), for both workload pipelines.
- Assemble via `avr-gcc`/`avr-as` → ELF.
- Invoke Avrora with the energy monitor enabled:
  `java -jar avrora.jar -monitors=energy -platform=<target> program.elf`
- Parse per-component energy output (CPU, memory) from Avrora's report.
- Run speed-optimized and energy-aware builds of **both** workloads through
  this same path and diff the reported numbers.

## 7. Repository Layout

```
energy-aware-avr-compiler/
├── export/
│   └── export_model.py         # PyTorch model + ONNX export (ML path)
├── compiler/                   # Rust — the actual project
│   ├── src/
│   │   ├── ir.rs                # Module 4 — shared IR, both paths
│   │   ├── ingest/
│   │   │   ├── onnx.rs           # ONNX -> IR (ML path)
│   │   │   └── dsp_build.rs      # hand-built IR construction (DSP path)
│   │   ├── codegen/
│   │   │   ├── candidates.rs     # Module 6
│   │   │   ├── regalloc.rs       # Module 6
│   │   │   └── select.rs         # Module 5
│   │   ├── cost_model.rs         # cost_table.toml loader
│   │   ├── locality.rs           # Module 7
│   │   └── emit.rs
│   └── Cargo.toml
├── sim/
│   ├── run_avrora.sh            # assemble + invoke Avrora
│   └── parse_report.py
├── models/
│   └── tiny_classifier.onnx     # ML demo model
├── cost_table.toml
├── SOURCES.md                   # cited source for every cost table entry
└── REPORT.md                    # methodology, results, stated limitations
```

## 8. Milestones (build in this order)

1. **Toolchain bring-up.** Confirm `avr-gcc` and `avrora.jar` run locally.
   Hand-write a trivial AVR asm program, run it through Avrora, print the
   energy monitor output.
2. **Minimal end-to-end path, both workload classes.** Hardcode a
   single-op IR for each path — one `Add` (ML) and one `Window` (DSP) —
   emit two candidate instruction sequences by hand for each, run both
   through Avrora, confirm energy numbers differ. Proving the shared
   backend mechanism works across *both* op families from the start
   matters more here than depth on either one.
3. **ONNX ingestion.** Export the demo model, parse into IR, supporting
   `MatMul`, `Add`, `Relu`, `Requantize`.
4. **DSP builder + fixed-size FFT.** Construct the `Window -> BitReverse ->
   FftButterfly (xN) -> Magnitude -> PeakExtract` pipeline for the chosen
   fixed size (64 or 128).
5. **Full instruction selection.** Candidate generation + register
   allocation, cost-table-driven selection, for both op families.
6. **End-to-end comparison.** Compile both workloads two ways, run through
   Avrora, generate the comparison report.
7. **Sourcing and write-up.** Replace every placeholder cost-table entry
   with a cited real figure. Write `REPORT.md`.

## 9. Testing Plan

- Unit tests on IR construction for both paths (ONNX ingestion and DSP
  builder each produce the expected graph for a small hand-crafted input).
- **FFT correctness specifically:** compare the fixed-size FFT's output
  against a reference implementation (e.g. `numpy.fft` computed offline)
  for a known test input, within Q15 rounding tolerance.
- **Correctness tests on generated code, not just energy comparisons:**
  run each candidate instruction sequence through Avrora and check the
  resulting register/memory state matches the expected numeric output. A
  lower-energy sequence that computes the wrong answer is a failed test,
  full stop — this applies equally to both workload paths.
- End-to-end test: both compiled variants of each workload produce
  identical outputs; only the energy numbers differ.

## 10. Non-Goals — Phase 1 (explicitly do not build)

- Multi-ISA support (ARM Cortex-M, RISC-V) — Phase 2.
- Physical hardware measurement or any claim of measured (vs. simulated)
  results — Phase 2, not eliminated.
- General arbitrary-N FFT. Fixed-size only.
- Fingerprint hash generation (final Shazam pipeline stage).
- General ONNX operator coverage beyond the demo model's needs.
- Reuse of or dependency on existing ML- or DSP-compiler backends.
- A polished CLI/UX layer.

## 11. Final Deliverable

**Phase 1:** a working repository producing, from one command: both
workloads (ML and DSP) each compiled two ways, both run through Avrora, and
a report with the energy comparison and its sourcing — explicitly and
consistently labeled as simulation-derived throughout.

This is both the course project deliverable and the results section for a
first submission draft. Before submitting to JSA, note two additional
requirements from their author guidelines that aren't part of this spec's
build scope but do affect the repo:

- **Research data deposit.** JSA requires code/models/algorithms used in
  the study to be deposited in a citable repository (their "Option C"
  policy). Archive the repo on Zenodo near submission time — it mints a
  free DOI for a GitHub repo — and cite that DOI in the paper.
- **Phase 2 is what the paper's central claim depends on**, not Phase 1
  alone. Don't treat Phase 1 completion as "ready to submit" — it's ready
  to write up as the software contribution; the energy-trustworthiness
  argument still needs the sim-to-hardware correlation work.

## 12. Amendment (2026-08-26): instruction-level energy finding + sleep-mode scheduling

After milestones 1–4 were complete (real Avrora bring-up, real hand-written
candidate-pair energy deltas, real ONNX ingestion, real DSP builder), an
attempt to source real per-instruction energy figures for `cost_table.toml`
(originally scoped as milestone 7) turned up a finding significant enough
to record here rather than only in `SOURCES.md`.

**Finding:** no source available to this project — not the official
Microchip/Atmel AVR datasheets (which publish only aggregate Active/Idle/
Power-down current vs. clock frequency, never a per-instruction table), not
JouleTrack (a well-cited instruction-level energy paper, which found
per-instruction current variance small relative to baseline active current
on the processors it studied), and not Avrora's own energy monitor
(confirmed empirically: two instruction sequences with equal cycle counts
but different opcodes — `nop`×4 vs. `ldi`/`add`/`mov` — produced
bit-identical simulated energy) — supports differentiating energy by
*instruction type* within CPU active mode. Every source agrees active-mode
energy is, in effect, `cycles × a per-cycle constant`.

**Consequence:** `cost_table.toml`, sourced honestly from the datasheet's
Active Mode Supply Current figures, will be a uniform per-cycle constant
in Phase 1, not a differentiated per-instruction table. Under that model,
`select()`'s energy-minimizing choice is mathematically equivalent to
cycle-minimization — which is not vacuous (AVR's `lds`/`sts` already cost 2
cycles vs. 1 for register ops, so cycle-aware selection still captures
real, physical SRAM-avoidance behavior, as already demonstrated in
milestone 2's hand-written candidate pairs) but does mean a
"speed-optimized" and "energy-aware" build of the same op are expected to
converge, not diverge, at this measurement granularity. State this plainly
in `REPORT.md` rather than presenting it as a limitation to paper over —
it's a legitimate, evidenced answer to this spec's own research question
(section 1: "does optimizing for energy produce different code than
optimizing for cycles").

This sharpens *why* Phase 2 (sim-to-hardware correlation) matters: it is
now an open, testable question whether real silicon exhibits
instruction-level current variation that Avrora's power-state model simply
doesn't capture, not just whether Avrora's aggregate numbers are roughly
right.

**Addition — sleep-mode scheduling as a second, genuinely-divergent energy
axis.** AVR's documented power-down/sleep modes differ from Active mode by
roughly 100–1000x in current draw (real, datasheet-cited numbers, unlike
per-instruction variance) — this is also the reason Avrora's own authors
built its event-queue architecture around sleep (their paper reports
typical sensor programs sleep 96–99% of the time). A compiler that
schedules explicit low-power sleep between compute bursts is doing
genuine, evidenced energy-aware optimization, via a different mechanism
than per-op candidate selection: program *structure* (when to sleep, how
long, triggered by what) rather than instruction *choice* for a fixed
computation.

Scope for Phase 1: model a periodic trigger (timer-driven wake, run one
inference pass over the ML or DSP pipeline, re-enter sleep) and compare a
sleep-aware build (real AVR `sleep` instruction, sleep mode configured via
the MCU control register) against a naive busy-wait baseline, over a
simulated duration spanning multiple trigger cycles — not a single
straight-line compute-then-`break` program like the existing milestone 1–2
fixtures. This is additive to, not a replacement for, the per-op candidate
generation work in milestone 5; both axes ship in Phase 1.

**2026-08-27 update:** this scope's specific wake mechanism (watchdog-
triggered wake from Power-down) does not hold -- Avrora has no watchdog
timer model and no general external-interrupt injection, confirmed by a
standalone spike hanging indefinitely rather than completing (see
`documents/PHASE_B_NOTES.md`). The comparison target above (sleep-aware vs.
busy-wait, over multiple trigger cycles) still stands as the goal, but the
*mechanism* is being re-derived spike-first against what Avrora actually
simulates rather than assumed from general AVR knowledge -- Power-save
mode + asynchronous Timer0 is the current viable candidate, itself found
by spiking rather than planned in advance. Treat this phase's remaining
work as exploratory until the mechanism is nailed down, not as executing
a predetermined plan.

## 13. Amendment (2026-08-27): cost_table.toml recalibrated to Avrora's own power model

Phase A (real naive AVR codegen for `models/tiny_classifier.onnx`, the
first end-to-end run from ONNX through real emitted assembly through a
real `sim/run_avrora.sh` invocation) surfaced a second, independent
finding beyond section 12's.

**Finding:** `cost_table.toml`'s predicted cycle count for the compiled
classifier matched Avrora's real simulated cycle count almost exactly
(6129 predicted vs. 6130 simulated — the +1 is known fixed harness
overhead, already present in `bringup_smoke.avrora.txt`), but predicted
and simulated *energy* diverged by a factor of ~3.75x (65,120.625 nJ
predicted vs. 17,393.951625 nJ simulated). Decompiling `avrora.jar`'s
`avrora.sim.mcu.ATMega128.class` and `avrora.sim.energy.Energy.class`
(via `jar xf` + `javap -v`, both part of the JDK 8 toolchain already
required to run Avrora) found the discrepancy's exact source: Avrora's
own built-in ATmega128 power model assumes 3.0V and 7.5667mA active
current (2.8375 nJ/cycle), not the 5V/17mA datasheet row (10.625
nJ/cycle) `cost_table.toml` cited at the time. `3.0V × 0.0075667A /
8,000,000 Hz = 2.8375125 nJ/cycle` matches Avrora's real simulated energy
divided by real simulated cycles to full floating-point precision, on two
independently-generated programs.

**Consequence:** `cost_table.toml` is recalibrated to Avrora's own
constant (see `SOURCES.md`'s "Per-cycle energy constant" section for the
full derivation, citation, and reproduction steps). This is a deliberate
choice to prioritize internal consistency with this project's own
ground-truth benchmark over datasheet fidelity to an operating point (5V)
Avrora's simulator doesn't actually model — both figures are real,
correctly-cited numbers describing different supply voltages, and the
5V/17mA figure remains documented in `SOURCES.md` as a superseded value,
not deleted. This does not affect section 12's finding (no source
supports per-instruction-*type* differentiation within active mode) —
that conclusion holds regardless of which per-cycle constant is used,
since it concerns relative differentiation between opcodes at equal cycle
counts, not the constant's absolute value.
