# OptiFine: Energy-Aware Code Generation for Resource-Constrained AVR Targets

Technical report, OptiFine 0.1.0.

Status: research prototype. This report covers the compiler's two evaluated
workloads -- an INT8 classifier ingested from ONNX and a 64-point Q15 DSP
pipeline -- and its two energy mechanisms: active-mode optimization (choosing
instruction sequences, including next-use register caching) and periodic
power-aware scheduling (sleeping between periodic runs instead of
busy-waiting). Every result comes from the compiler's own output simulated in
Avrora; none is a physical measurement. Every figure is regenerated from the
retained raw Avrora output by `python3 sim/report_results.py`, not
transcribed by hand.

## Abstract

On an AVR ATmega128 model, energy-aware instruction selection reduces
exactly to cycle reduction: every instruction's energy is its cycle count
times one constant, and the optimizer's 1.83% simulated saving on an INT8
classifier is a pure cycle saving. Latency stops being a proxy for energy
once the compiler schedules sleep: with periodic Power-save scheduling, an
idle cycle costs 61.2 times less than an Active one, the classifier saves
up to 96.33% per period against busy-waiting, and the same 112-cycle
active-mode saving is worth 312.606 nJ per period under sleep scheduling
and exactly zero under busy-wait. A second, structurally different workload
-- a 64-point Q15 FFT pipeline compiled to 12,800 bytes, whose 148,335
simulated cycles the compiler predicts exactly -- saves 42.97% under the
same scheduling, because its body fills 56% of the period. All results are
simulated; hardware validation is future work.

## Motivation

In 2021, Heim, Biri, Qu and Thiele measured neural-network inference across
a range of ARM Cortex-M microcontrollers (STM32 L4/F4/F7) with an external
energy monitor and reported a near-perfect linear correlation between
latency and energy — r = 0.9946 across whole-network optimizations,
r = 0.9995 at the individual-layer level. They concluded that "the
inference latency is a perfect proxy for the energy consumption of the
investigated MCUs." If that holds generally, energy-aware compilation is a
solved problem: optimize for speed, and energy follows for free.

OptiFine is an energy-aware compiler backend built to locate the boundary
of that equivalence. The finding is that it holds precisely as long as the
processor never leaves Active mode — and fails the moment it does.

The project does not dispute their measurement. Its active-mode
optimization corroborates it: an energy-cost model driving instruction
selection and next-use register allocation reduces exactly to cycles times a
constant, and its 1.83% simulated saving is a pure cycle reduction — the
only lever their regime predicts is available. The divergence appears with
periodic power-aware scheduling, where the compiler itself schedules
sleep-state entry around a periodic workload. On a calibrated ATmega128
model, an Active cycle costs 2.8375 nJ and a Power-save cycle 0.0464 nJ — a
ratio of 61.2x. Latency and energy decouple: idle duration is unbounded while
its energy cost is bounded by sleep current, yielding 96.3% savings at a ~98%
idle fraction. The two mechanisms also interact — the 112-cycle active-mode
saving is worth 312.6 nJ per inference under sleep scheduling and exactly
zero under busy-wait, making the sleep-aware compiler the precondition for
the instruction-level optimization being observable at all.

The same backend targets two structurally different workload classes —
INT8 neural-network inference and a fixed-point streaming DSP pipeline
(64-point FFT, Q15) — so the result can be tested against computational
shape rather than a single benchmark.

This is a structural argument about the generality of a claim, not a
competing measurement: it is established in a cycle-accurate simulator,
whereas the result it contests was taken on physical hardware. Closing that
gap — on both AVR silicon and an STM32 target of the same architectural
family Heim et al. measured — is future work. The full related-work
positioning is in `docs/LITERATURE_SURVEY.md`.

## Architecture

```
ONNX classifier --(ingest.c)----\
                                 >-- typed IR --> lowering / candidates --> selection --> AVR assembly --> avr-gcc --> Avrora
DSP pipeline ---(dsp_build.c)---/
```

The ML path ingests a hand-authored quantized ONNX graph
(`export/export_model.py`) through a hand-rolled protobuf parser
(`compiler/src/ingest.c`); the DSP path constructs its fixed graph directly
(`compiler/src/dsp_build.c`). Both share one typed IR, one lowering
(`compiler/src/codegen/lower.c`), candidate generation and cost-based
selection (`candidates.c`, `select.c`, minimizing predicted energy), and
emission (`emit.c`, `program.c`, and `periodic.c` for the periodic wrapper).
The emitted assembly is assembled by `avr-gcc` and simulated by Avrora Beta
1.7.115 (`sim/run_avrora.sh`, `-monitors=energy`, ATmega128). The component
design is described in `docs/ARCHITECTURE.md`.

## Compiler Backend

**Pricing.** Every emitted instruction is priced from `cost_table.toml` as
`cycles x 2.8375 nJ` (see "Experimental Methodology" for why a single
constant). Pricing is per executed instruction: straight-line code once, code
inside a statically bounded counted loop times the product of its enclosing
trip counts, including the closing branch's one fall-through. The trip counts
are compile-time constants and no data-dependent branch is emitted, so the
predicted cycle count is exact; for every retained program it equals
Avrora's.

**Registers.** Candidate generation and register allocation are
deliberately scoped, not a fully general "any value in any of AVR's 32
registers across any op boundary" allocator. AVR's register file is
committed as follows for the per-op arithmetic (see
`compiler/include/optifine/codegen/registers.h`):

| registers | role | notes |
|---|---|---|
| r0, r1 | multiply destination | hardwired by `mul`/`muls`/`mulsu`, not optional |
| r2 | permanent zero | carry-propagation source for Requantize's wide multiply, cleared once at program start |
| r3-r8 | Requantize's 48-bit product buffer | live only during a Requantize op |
| r16, r17 | MAC operands | `muls`/`mulsu` require r16-r23/r16-r31 |
| r18-r21 | 32-bit MAC accumulator | live across one MatMul output channel |
| r22, r23 | sign-extension / ReLU mask scratch | |
| r24, r25 | general scratch | Const/Input byte loading, Requantize temporaries |

That leaves r9-r15 and r26-r31 (13 registers) idle at all times, plus two
more pools available only when their "owning" op isn't running: r24/r25
(idle during MatMul) and r3-r8 (idle during MatMul, since Requantize and
MatMul never execute concurrently in these straight-line programs).

**Correctness.** Generated programs run in a test-only AVR interpreter
(`compiler/tests/avr_interp.c`) and are compared exactly with independent
references: the classifier's output against `export/gen_golden.py`'s numpy
reference, the DSP pipeline's every intermediate buffer against an integer
host implementation. The interpreter also counts executed cycles, which the
tests compare with the compiler's prediction. A lower-energy sequence that
computes a different answer is a failed test.

## Active-Mode Optimization

The naive baseline lowers each op once and reloads every value from SRAM on
each use. With `--optimized`, the compiler generates alternative candidates
and keeps the cheapest. `regalloc_next_use`
(`compiler/src/codegen/regalloc.c`) performs next-use analysis: it marks a
value as worth caching when it is a MatMul's activation input, which is read
once per output channel (a bounded-distance reuse pattern), and leaves every
other op's output alone, since every other op in these graphs consumes its
inputs exactly once. MatMul then gets a second candidate that loads its input
into registers once. Wider cross-op register residency was considered and not
attempted, because it would require either shrinking the intra-op scratch
budget above (risking the golden-value-validated arithmetic) or accepting
collisions between a cached cross-op value and a later op's scratch use.

### Register cache-pool gap and its closure

The first implementation reserved a 15-register cache pool (r9-r15,
r24-r25, r26-r31) for MatMul's cached-input candidate. This covers fc2's
MatMul (K=8, the contraction dimension of its activation input) fully,
but fc1's MatMul (K=16) exceeded the pool by exactly one register: byte
15 of the 16-byte input still fell back to a per-use `lds` (2 cycles)
instead of the cached `mov` (1 cycle) the other 15 bytes got -- fc1's MatMul
candidate priced at 5,101.825 nJ instead of its achievable minimum.

The fix: r3-r8 (Requantize's 48-bit product buffer, 6 registers) are also
idle during any MatMul's execution -- they are used elsewhere in the same
program (by every Requantize op), but never concurrently with a MatMul,
since this is a straight-line program with no interleaving: by the time any
Requantize op runs, every MatMul that used the cache pool has finished and
stored its result to SRAM. Extending `MATMUL_CACHE_POOL_SIZE` from 15 to 21
(adding r3-r8) let fc1's MatMul cache all 16 input bytes, re-verified
against the golden-value test (bit-exact classifier output unchanged) before
and after the change.

| | before (15-register pool) | after (21-register pool) |
|---|---:|---:|
| Optimized cycles | 6,024 | 6,018 |
| Optimized energy | 17,093.175300 nJ | 17,076.150225 nJ |
| Delta vs. naive | -106 cycles / -300.776 nJ (-1.73%) | -112 cycles / -317.80 nJ (-1.83%) |

AVR's register budget is the binding constraint on how much a next-use
allocator can achieve here, and small increases to that budget -- justified
by an explicit non-overlap argument -- translate directly into measurable
energy reductions.

## Periodic Power-Aware Scheduling

Active-mode optimization works *within* Active mode -- every instruction it
chooses between still costs `cycles x 2.8375 nJ`. Periodic scheduling adds a
second, independent axis: whether the MCU sleeps between inferences at all.
`compiler/src/codegen/periodic.c` emits two matched program variants around
the *same* compute body:

- **Active (busy-wait) policy**: the MCU polls a tick flag in a tight
  8-cycle loop between inferences, never leaving Active mode.
- **Power-save policy**: `ASSR.AS0` clocks Timer0 asynchronously so it
  keeps ticking while the CPU is asleep; the MCU enters Power-save mode
  (`MCUCR` SM2:0=011) via `cli` -> test tick -> arm `MCUCR` -> `sei` ->
  `sleep`, with nothing emitted between `sei` and `sleep` -- the
  standard AVR idiom that closes the flag-test/interrupt race (the
  instruction immediately after `sei` is guaranteed to execute before
  any pending interrupt is serviced).

Both variants share one code path for everything between `; BODY BEGIN`
and `; BODY END` -- `codegen_emit_inference_body()`, the same function
the non-periodic program uses -- so a policy can only change *how the
MCU waits*, never *what it computes*. `test_periodic.c` enforces this
with a `memcmp` over the two variants' BODY regions. A shared
Timer0-overflow ISR (saves/restores r16 and SREG, sets an `overrun` flag
if a tick arrives while an inference is still `running`) drives both
policies identically. The period is `256 x prescaler` cycles for the four
ATmega128 Timer0 divisors 8, 32, 128 and 1024 (2,048 / 8,192 / 32,768 /
262,144 cycles).

For the DSP pipeline the wrapper copies the output tensor at its real size
(16 bytes, `FIXED_Q15[8]`, where the classifier's is 4) and emits the
twiddle table after its interrupt handler, where control cannot reach it.
`test_dsp_pipeline` checks that the periodic build's initialization, body
and table are the ordinary `--dsp` program's, instruction for instruction,
that its wrapper writes no register but r16 (saved by the interrupt
handler), and that in the interpreter the body run five times after one
initialization reproduces every buffer and all 16 output bytes each time.

## DSP Workload

The second workload is a fixed audio-style pipeline built directly as IR
(`compiler/src/dsp_build.c`) rather than ingested: 64 Q15 samples, a Hamming
window, a 64-point FFT, per-bin magnitude and peak selection, compiled by
`optifine --dsp`.

### Why the DSP lowering uses loops

Lowering every op fully unrolled, as the ML path does, was checked first. A
flash model -- checked against real `avr-size` output for the Window op
(4,608 B predicted, 4,608 B linked) -- projected the unrolled pipeline at
about 497 KB, roughly 379% of the ATmega128's 128 KiB flash. That was a
code-representation problem, not a workload problem, so the workload was
kept and the representation changed: each repeated structure is emitted once
inside a statically bounded counted loop, priced exactly as described under
"Compiler Backend". The 497 KB figure is that projection, not a measurement
of anything built.

Supporting backend work, each validated against Avrora before use:

- labels and counted-loop closure in two forms -- `dec / brne` when the body
  is within BRNE's 64-word reach, and `dec / breq exit / rjmp head` beyond
  it -- with the form recorded per loop and priced exactly;
- the FFT's 32 unique Q15 twiddles as a 128-byte program-memory table read
  with `lpm Z+`, placed after the program's only `break` (below 0x10000, so
  no ELPM/RAMPZ);
- a DSP scratch arena used as a lifetime overlay: ops run once each in
  sequence, so every op may reuse any cell provided it writes before it
  reads, which a test checks over every op's code;
- one label namespace per emitted assembly file, so several looping ops
  can share a program.

### Pipeline and semantics

`Input -> Const (Hamming coefficients) -> Window -> BitReverse -> FFT x6 ->
Magnitude -> PeakExtract -> Output`.

- **Window**: per-sample Q15 product `floor(x * c / 32768)` with the
  coefficients baked in at compile time.
- **BitReverse**: the 6-bit bit-reversal permutation into a complex buffer
  (imaginary parts zero), ahead of the stages.
- **FFT**: N = 64, radix-2 decimation-in-time, forward transform,
  `W^k = exp(-2*pi*i*k/64)`. Each stage halves its operands before
  combining them (`p/2 +/- t/2`, arithmetic shift), so scaling is 1/2 per
  stage and 1/64 overall and no intermediate overflows int16. Stage 0 is one
  loop of 32 butterflies; stages 1-5 are a block loop around a butterfly
  loop. 4,230 B of code.
- **Magnitude**: exact `floor(sqrt(re^2 + im^2))` per bin, the squares and
  their sum in 32 bits (the sum reaches 2^31, so it is unsigned), and a
  fixed 16-iteration bit-by-bit integer square root with masks in place of
  data-dependent branches. The result is an unsigned 16-bit value in Q15
  units stored in the op's `FIXED_Q15[64]` tensor; over arbitrary complex
  Q15 input it reaches 46,340. Behind the Hamming window it provably stays
  below 17,474 (sum of coefficients / 64, plus at most 15 of rounding over
  six stages). 246 B of code.
- **PeakExtract**: the 8 largest magnitudes, largest first, compared
  unsigned. Output is the eight values only, not bin positions (the IR
  output is `FIXED_Q15[8]`). Each pass excludes earlier winners through an
  explicit `selected[64]` table the op zeroes itself, and ties go to the
  lower bin index, so every value comes from a distinct bin even when
  values repeat or are all zero. Decisions are masks, not branches. 128 B
  of code.

### Correctness

The complete program was run in the test interpreter on 18 inputs (zeros,
impulses, two DC levels, tones at bins 1, 5, 23 and 31, alternating int16
extremes, a mixed ramp, a negative-heavy signal, Q15 boundary values, four
fixed-seed random signals and the demo input). Every graph buffer -- window
output, bit-reversed buffer, all six FFT stages, magnitudes, peaks and the 16
output bytes -- equals an independent host reference exactly (integer Q15
arithmetic, a textbook-indexed FFT, integer square root, a sort for the
peaks). Each run is repeated under different SRAM poison, and op by op with
the scratch arena re-poisoned between ops, with identical results and no
write outside the graph tensors and scratch arena (`test_dsp_pipeline`). The
interpreted instruction stream is checked to be exactly the one
`optifine --dsp` writes.

## Experimental Methodology

**Simulator and energy model.** Every energy figure in this project is
either a cited published number or an Avrora simulation output -- never a
physical measurement. Programs run in Avrora Beta 1.7.115's ATmega128 model
at 8 MHz with the energy monitor. `cost_table.toml`'s per-cycle constant
(2.8375 nJ/cycle) is calibrated to Avrora's own built-in ATmega128 power
model (3.0 V, 7.5667 mA active current, decompiled from `avrora.jar`'s
`avrora.sim.mcu.ATMega128.class`; see `SOURCES.md`) rather than the
ATmega128 datasheet's 5 V/17 mA row an earlier version used -- the first
end-to-end run showed the datasheet-derived constant overstated Avrora's
simulated energy by ~3.75x, even though predicted and simulated cycle counts
matched almost exactly. No source available to this project supports
differentiating energy by instruction *type* within CPU active mode, and
Avrora itself simulates equal-cycle sequences to bit-identical energy, so
every `cost_table.toml` entry reduces to `cycles x this constant`. Avrora's
Power-save cost, 0.0463875 nJ/cycle, is solved from its own reports (below).

**Harness calibration.** The hand-written `sim/smoke.s` (7 one-cycle
instructions and a `break`) reports 8 cycles and 22.7001 nJ: the final
`break` adds one fixed cycle to every simulated program.

**Steady-state measurement.** Every periodic figure is
`E(inference_count=5) - E(inference_count=4)`, not a single raw run. This
cancels the one-time initialization cost (stack/Timer0/ISR setup) and
isolates the per-period marginal cost, including the fact that the busy-wait
and Power-save variants have different fixed wrapper lengths (in the raw
counts, the naive build's prescaler-32 count-4 run totals 39,011 busy-wait
cycles against 40,006 for Power-save, a 995-cycle wrapper-length difference the differential
method makes non-confounding; `sim/tests/test_compare_periodic.py` locks this
in).

**Validity gates**, checked before any pair's numbers are reported: the
count-4 and count-5 programs of a variant differ only in the count literal;
the busy-wait/Power-save quartet shares one `body_sha256`, one set of input
hashes and one toolchain; the declared output is the golden value
(`[0,-3,18,27]` for the classifier); and the steady-state increments agree.
A pair failing any gate is marked `rejected` with its reason, never dropped.

**Static deadline check.** Before comparing energy, a prescaler is rejected
when the compiler's predicted body cycles are not below its period. For the
classifier this rejects prescaler 8 (2,048-cycle period against a
5,456 naive / 5,344 optimized cycle body); for the DSP pipeline it rejects
prescalers 8, 32 and 128 (147,565-cycle body). Rejected prescalers are still
simulated and kept visible in the tables. Their Avrora runs corroborate the
prediction: count-4 and count-5 runs are identical, as happens when the
program hits `overrun` and halts on its first tick.

**DSP busy-wait tolerance.** One comparison rule differs for the DSP sweep
only. The busy-wait loop polls the tick flag in an 8-cycle loop, so each wake
is detected up to 8 cycles late, and with the DSP body's length that
detection phase does not settle: Avrora's busy-wait increments are 262,141
and 262,149 cycles around the exact 262,144 of every Power-save increment
(the classifier's body happens to hit a fixed point, where the increments
match exactly, as the ML comparator requires). The DSP sweep accepts a pair
whose Power-save increment equals the Timer0 period exactly and whose
busy-wait increment is within the loop's 8 cycles of it, after checking that
the emitted loop is exactly that 8-cycle loop and the busy-wait runs are all
Active cycles, and scales the busy-wait energy to the exact period: a factor
of 262,144 / 262,141 = 1.0000114, from a raw busy-wait increment of
743,828.36 nJ to 743,836.88 nJ. The raw increments and the factor are
recorded in the manifest.

**Evidence limit.** Output, completion count and overrun are compiler/run
declarations; no simulated SRAM is read back from Avrora. That the periodic
programs compute correctly is established in the test interpreter.

## Evaluation

### Active-mode optimization: ML classifier

Avrora simulation of the compiled `tiny_classifier.onnx` (16->8->4 INT8
two-layer classifier), naive (single candidate per op, every value reloaded
from SRAM per use) vs. optimized (candidate diversity for MatMul + next-use
input caching):

| | naive | optimized | delta |
|---|---:|---:|---:|
| Cycles (Avrora) | 6,130 | 6,018 | -112 (-1.83%) |
| Energy (Avrora) | 17,393.951625 nJ | 17,076.150225 nJ | -317.80 nJ (-1.83%) |
| Energy (compiler predicted) | 17,391.038 nJ | 17,073.238 nJ | -317.80 nJ |
| Golden-value correctness | bit-exact | bit-exact | both correct |

Predicted and simulated numbers agree to within the fixed +1-cycle `break`
on both builds. The implied Active cost is 2.8375125 nJ/cycle whether derived
from the naive run, the optimized run, or their difference: under Avrora's
Active model, energy selection reduces exactly to cycle selection.

The improvement is concentrated where the mechanism predicts -- the two
MatMul ops, the only ops whose activation input is reused across a loop:

| op | kind | naive energy (nJ) | optimized candidates | optimized energy (nJ) | saved |
|---:|---|---:|---:|---:|---:|
| 3 | MatMul (fc1) | 5,357.200 | 2 | 5,084.800 | 272.400 |
| 9 | MatMul (fc2) | 1,407.400 | 2 | 1,362.000 | 45.400 |
| all others | -- | (unchanged) | 1 | (unchanged) | 0 |

### Periodic scheduling: ML classifier

**Periodic scheduling is what makes the active-mode saving observable.**
Under the Power-save policy, the naive-vs-optimized energy delta is
**312.606 nJ per period, exactly, at every accepted prescaler**:

| prescaler | naive Power-save (nJ) | optimized Power-save (nJ) | delta |
|---:|---:|---:|---:|
| 32 | 15,814.9276 | 15,502.3217 | 312.6060 |
| 128 | 16,954.9468 | 16,642.3409 | 312.6060 |
| 1024 | 27,595.1260 | 27,282.5200 | 312.6060 |

This is the active-mode optimizer's 112-cycle saving (in the steady-state
increments: naive 5,530 Active cycles per period vs. optimized 5,418) times
the simulator's Active/Power-save per-cycle energy gap. Both per-cycle rates
are re-derived from the raw reports: Active = 23,244.9024 nJ / 8,192 cycles
= 2.8375125 nJ/cycle; Power-save = 0.0463875 nJ/cycle, solved from the same
period's Active/Power-save energy and cycle split.
`112 x (2.8375125 - 0.0463875) = 112 x 2.7911250 = 312.6060 nJ`.

**Under busy-wait the same 112-cycle saving is worth exactly 0 nJ.** The
busy-wait energy of the naive and optimized builds is identical at a given
prescaler (23,244.9024 nJ at prescaler 32) because it is
`period x 2.8375 nJ/cycle`, fixed by the period alone -- finishing the body
112 cycles sooner only buys 112 more polling cycles. The instruction-level
saving is real but invisible without sleep scheduling.

**Scheduling saving**, comparing Power-save against a busy-wait baseline at
the same period (a scheduling result, not a compiler-selection result):

| prescaler | period (cycles) | busy-wait (nJ) | Power-save (nJ) | saved (nJ) | saving | status |
|---:|---:|---:|---:|---:|---:|---|
| 8 | 2,048 | -- | -- | -- | -- | rejected: body exceeds period |
| 32 | 8,192 | 23,244.90 | 15,502.32 | 7,742.58 | 33.31% | accepted |
| 128 | 32,768 | 92,979.61 | 16,642.34 | 76,337.27 | 82.10% | accepted |
| 1024 | 262,144 | 743,836.88 | 27,282.52 | 716,554.36 | 96.33% | accepted |

(Power-save figures are for the optimized build. The full 4x2x2 matrix of
runs is in `sim/fixtures/periodic_ml/two_by_two.md`.) The saving depends on
the duty cycle -- how long the MCU sleeps between wake events -- and
approaches 100% as the period grows; it is not a fixed compiler saving, and
it is kept separate from the 312.606 nJ compiler-attributable result above.

**Relation to "latency is a perfect proxy for energy".** Heim et al.'s claim
holds within Active mode, and the active-mode results agree with it: the
energy model there is `cycles x 2.8375 nJ`, so the 1.83% energy saving is
identical to a cycle saving. The 312.606 nJ / 0 nJ contrast is where it
breaks: under busy-wait the naive and optimized builds take the same time to
the next tick and cost the same energy, while under Power-save they differ by
312.606 nJ. Once a sleep state exists below Active, a Power-save cycle and an
Active cycle cost 2.7911 nJ apart (a ~61.2x ratio, from Avrora's
`ATMega128.class`/`Energy.class`, see `SOURCES.md`), and elapsed latency
alone cannot tell which kind of cycle was spent. This is a scope objection,
not a numbers dispute: their Cortex-M benchmark never left Active mode. The
full comparison is in `docs/LITERATURE_SURVEY.md`, section 1.

### DSP workload

Avrora simulation of the `optifine --dsp` program on the default demo input
(`models/dsp_demo_input.txt`, ATmega128, 8 MHz):

| metric | DSP pipeline |
|---|---:|
| Cycles (Avrora) | 148,335 |
| Cycles (compiler prediction) | 148,335 |
| Cycle-prediction error | 0 |
| Time @ 8 MHz | 18.542 ms |
| Simulated active energy (Avrora) | 420,902.42 nJ (420.90 µJ) |
| Linked flash (.text; .data and .bss are 0) | 12,800 B (9.8% of 128 KiB) |
| SRAM (graph tensors + scratch arena) | 2,517 B (61.5% of 4,096 B; 1,579 B free) |

Time is the cycle count divided by the 8 MHz clock, not a wall-clock
measurement. The energy is Avrora's own report, 2.8375125 nJ per Active cycle
exactly as for the ML path. The compiler's printed estimate, 420,900.56 nJ,
prices the same cycles at `cost_table.toml`'s rounded 2.8375 nJ/cycle. The
cycle count does not depend on the input samples: every data decision is a
mask, and only loop control branches.

Where the cycles and the flash go (compiler prediction per op, which sums
to the Avrora total):

| part | cycles | % | code bytes |
|---|---:|---:|---:|
| Setup: `clr r2`, embedded input, window coefficients | 769 | 0.52% | 1,538 |
| Window | 2,560 | 1.73% | 4,608 |
| BitReverse | 896 | 0.60% | 1,792 |
| FFT, six stages | 73,512 | 49.56% | 4,230 |
| Magnitude | 51,590 | 34.78% | 246 |
| PeakExtract | 18,943 | 12.77% | 128 |
| Output | 64 | 0.04% | 128 |
| `break` + twiddle table | 1 | 0.00% | 2 + 128 |
| **total** | **148,335** | | **12,800** |

The setup row is part of the program, not a harness: like the ML build's
demo input, the samples and coefficients are embedded at compile time.
(Isolated FFT tests report 73,513 cycles because they include their own
`break`; inside the complete program the six stages cost 73,512.) FFT and
Magnitude are 84% of the cycles; Window, still fully unrolled, is 36% of the
flash.

SRAM is 2,336 B of graph tensors (one buffer per op, none reused) plus a
181-byte scratch arena. The arena is an overlay across op lifetimes, so the
most any op holds at once is 64 B (PeakExtract's `selected[]`); per-op
scratch is not additive. The program uses no stack (its periodic build uses
4 B, for the timer interrupt).

The two workloads side by side (they compute different things, so this shows
the backend's breadth rather than comparing the algorithms' efficiency):

| | ML classifier (optimized) | DSP pipeline |
|---|---:|---:|
| Cycles | 6,018 | 148,335 |
| Time @ 8 MHz | 0.752 ms | 18.542 ms |
| Simulated active energy | 17,076.15 nJ | 420,902.42 nJ |
| Linked flash | 11,524 B | 12,800 B |

(ML flash is `avr-size` of the `--optimized` build.)

The DSP path has one lowering per op, so there is no naive-versus-optimized
pair as on the ML path: under a single per-cycle energy constant the two
would select the same code (the active-mode finding).

### Periodic scheduling: DSP pipeline

The DSP program as the periodic wake body (`optifine --dsp --periodic-count N
--wait-policy active|powersave --timer-prescaler P`), swept over the same four
prescalers, both wait policies and counts 4 and 5 -- 16 Avrora runs.
Figures are steady-state `E(5) - E(4)` increments, as for the ML sweep.

| prescaler | period (cycles) | DSP compute (cycles) | idle window | busy-wait (nJ/period) | Power-save (nJ/period) | saved | saving |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 2,048 | 147,565 | none | -- | -- | -- | compute-bound |
| 32 | 8,192 | 147,565 | none | -- | -- | -- | compute-bound |
| 128 | 32,768 | 147,565 | none | -- | -- | -- | compute-bound |
| 1024 | 262,144 | 147,565 | 114,579 | 743,836.88 | 424,239.11 | 319,597.77 nJ | 42.97% |

Only prescaler 1024 has a period long enough for the DSP body; at the three
shorter periods the body overruns the timer, there is no idle time to sleep
through, and the wrapper's overrun flag ends each run after one body.

At prescaler 1024 each Power-save period is 147,639 Active cycles (the body
and the wrapper) and 114,505 Power-save cycles; busy-waiting spends all
262,144 in Active mode. The saving is the idle part of the period moved from
2.8375 to 0.0464 nJ per cycle, so it tracks the duty cycle, not the DSP code:
the classifier, whose body fills about 2% of the same period, saves 96.33%
there. The two points are separate results and are not averaged.

The busy-wait figure is the normalized one described under "Experimental
Methodology" (raw 743,828.36 nJ over a 262,141-cycle increment; normalized
743,836.88 nJ over the exact period). The normalized figure, 743,836.8768 nJ,
equals the ML sweep's directly simulated busy-wait energy at the same
prescaler, as it must: busy-waiting spends the whole period in Active mode
whatever the workload.

The periodic DSP program links at 13,172 B of `.text` (the ordinary one:
12,800 B). Static SRAM is 2,521 B -- the 2,517 B of DSP tensors and scratch
plus the wrapper's 4 scheduler bytes -- and the timer interrupt, the only
stack user, needs at most 4 B (two pushes and the return address; it does
not nest, and nothing calls).

### Toolchain independence

The published evidence was captured on Linux with avr-gcc 16.1.0 and
Temurin JDK 8u504-b01. The project's original ML runs -- the two active-mode
builds, the calibration program and all 32 periodic-scheduling runs -- were
made on Windows with avr-gcc 15.2.0 and Zulu JDK 8u492; only `avrora.jar`
was the same file (SHA-256 `016021f4...eb`). Every cycle count and energy
figure in those reports is identical between the two, to the last digit
Avrora prints, as are the DSP program's and DSP sweep's reports against their
earlier captures. The results are properties of the emitted programs and
Avrora's power model, not of one machine's toolchain; they are still not
hardware measurements (see Limitations).

## Reproducibility

Raw simulator output is retained under `sim/fixtures/`: the active-mode ML
runs and calibration program (`active_ml/`), the complete DSP program
(`dsp/`), and the two periodic-scheduling sweeps (`periodic_ml/`,
`periodic_dsp/`). Each experiment has a manifest recording its source
commit, exact commands with repository-relative paths, tool versions and the
SHA-256 of every recorded input and raw artifact. `python3
sim/report_results.py` regenerates every table in this report from those
files. `sim/run_active_ml.py check`, `sim/run_dsp.py check`,
`sim/run_periodic_ml.py reproduce-retained` and
`sim/run_periodic_dsp.py reproduce-retained` revalidate the retained
experiments without running any tool; their `capture`/`run-new` modes repeat
them with a local toolchain into a fresh directory. `docs/REPRODUCIBILITY.md`
lists the commands and tool versions.

## Limitations

- **Simulation only.** All energy figures come from Avrora's ATmega128 power
  model; no ATmega128 board has run the generated programs.
- **Uniform Active-mode cost model.** Energy is cycles times one constant,
  because no available source supports per-instruction-type pricing.
- **Demo inputs are fixed and compile-time-baked**
  (`models/tiny_classifier_golden_input.txt`, `models/dsp_demo_input.txt`),
  not live sensor data. A deployment would read them at runtime.
- **No runtime saturation on Requantize overflow.** The compiler verifies the
  fixed demo input's forward pass at compile time
  (`lower_verify_demo_forward_pass`) and refuses to compile rather than
  saturate -- a static worst-case bound over all inputs is infeasible for
  MinMax-calibrated per-tensor quantization on unbounded-support inputs. A
  deployment accepting arbitrary inputs would need runtime saturation.
- **Requantize's fixed-point multiplier is 16-bit** (Q15-style, derived via
  `frexp`). The int32 accumulator only uses ~19 bits of magnitude for these
  layer shapes, so a 16-bit multiplier's precision already exceeds what the
  int8 output can represent.
- **Register allocation is scoped to MatMul's activation input**, not a
  general cross-op allocator (see "Active-Mode Optimization").
- **Weight constants load via synthesized `ldi`+`sts`**, not flash-resident
  `.rodata` + `lpm`: programs are assembled with `-nostartfiles`, so there is
  no crt0 to copy an initialized `.data` section into SRAM.
- **Narrow scope.** One classifier, one fixed DSP pipeline, one MCU, four
  prescalers. No claim is made across model architectures, other AVR chips,
  other sleep modes, broader input distributions, general ONNX operator
  coverage, multiple ISAs, or irregular (non-periodic) trigger patterns; the
  scheduling-saving relation (`saving ~= 0.984 x idle_fraction`,
  `docs/LITERATURE_SURVEY.md` section 1.3) is specific to this calibrated
  Active/Power-save ratio.
- **Cost-category traceability.** The opcodes beyond the original seven
  (`muls`, `mulsu`, `adc`, `sbc`, `lsl`, `clr`, `com`, `and`, `asr`, `ror`,
  and the DSP loop machinery) borrow same-cycle-count categories rather than
  having their own cited instruction-manual rows -- numerically inert (see
  `SOURCES.md`) but a traceability gap.
- **Only Idle and Power-save are simulatable.** Avrora has no Watchdog Timer
  implementation (`jar tf avrora.jar | grep -i watchdog` finds nothing, and a
  Power-down + watchdog program never wakes), so Power-down cannot be
  evaluated here; only Power-save is used.
- **No simulated SRAM readback.** The deadline/overrun evidence is the
  compiler's static prediction plus Avrora's cycle and state counts, not a
  read of the program's own `completed`/`overrun` bytes; manifests state this
  in their `evidence_limit` field.
- **One DSP input is simulated in Avrora.** The 18-input correctness sweep
  runs in the test interpreter; Avrora reproduces the cycle count, which does
  not depend on the samples, for the demo input.
- **One accepted DSP periodic point,** at prescaler 1024, with the busy-wait
  normalization described above.
- **No DSP candidate diversity:** each DSP op has one lowering.
- **PeakExtract reports magnitudes, not bin positions.** Positions would need
  a wider or second output tensor.

## Future Work

- Measure the generated programs on physical ATmega128 hardware, and on an
  STM32 target of the family Heim et al. measured.
- Per-instruction-type energy costs, if a source or measurement supports
  them.
- Runtime input from a sensor or host instead of compile-time inputs.
- DSP optimizations the current baseline leaves open: FFT and Magnitude are
  84% of its cycles, and the fully unrolled Window is 36% of its flash.
- Broader workloads: more ONNX operators, other FFT lengths, peak positions
  as output, and a general cross-op register allocator.

## Conclusion

OptiFine compiles two structurally different workloads for the ATmega128
with an exact cycle model: every retained program's predicted cycle count
equals Avrora's. Within Active mode, energy-aware code selection is cycle
minimization -- the classifier's 1.83% saving is exactly its 112 cycles.
Once the compiler also schedules sleep, energy and latency separate: idle
time costs 61 times less, the saving follows the duty cycle (96.33% for the
classifier, 42.97% for the DSP pipeline at the same period), and the
active-mode saving becomes visible (312.606 nJ per period) only because the
processor sleeps through the cycles it frees. These are simulated results;
the next step is to measure them on hardware.
