# OptiFine: Energy-Aware Code Generation for Resource-Constrained AVR Targets -- Technical Report

Status: research prototype. This report covers the compiler's two
evaluated workloads -- an INT8 classifier ingested from ONNX and a 64-point
Q15 DSP pipeline -- and its two energy mechanisms: active-mode optimization
(choosing instruction sequences, including next-use register caching) and
periodic power-aware scheduling (sleeping between periodic runs instead of
busy-waiting). Every result comes from the compiler's own output simulated
in Avrora; none is a physical measurement.

The DSP pipeline compiles to one AVR program that Avrora runs in exactly
the 148,335 cycles the compiler predicts, and whose every intermediate
buffer matches an independent integer host reference; its results, including
under periodic scheduling, are in "DSP Workload" below.

Every figure in this report is regenerated from raw Avrora output by
`python3 sim/report_results.py`, not transcribed by hand.

`documents/PHASE_B_NOTES.md` has the exploratory research trail that
preceded periodic scheduling's implementation (which sleep modes Avrora can
and can't simulate, and why); `documents/PHASE_B_PLAN.md` has its historical
task breakdown; `documents/LITERATURE_SURVEY.md` has the full related-work
positioning summarized in this report's periodic-scheduling section below.
(`PHASE_B` in file and directory names is the project's internal name for
periodic scheduling.)

## Abstract

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
optimization corroborates it: an energy-cost model driving instruction selection and
next-use register allocation reduces exactly to cycles times a constant,
and its 1.83% simulated saving is a pure cycle reduction — the only lever
their regime predicts is available. The divergence appears with periodic
power-aware scheduling, where the compiler itself schedules sleep-state
entry around a periodic workload. On a calibrated ATmega128 model, an Active cycle costs
2.8375 nJ and a Power-save cycle 0.0464 nJ — a ratio of 61.2x. Latency and
energy decouple: idle duration is unbounded while its energy cost is
bounded by sleep current, yielding 96.3% savings at a ~98% idle fraction.
The two mechanisms also interact — the 112-cycle active-mode saving is
worth 312.6 nJ per inference under sleep scheduling and exactly zero under
busy-wait, making the sleep-aware compiler the precondition for the
instruction-level optimization being observable at all.

The same backend targets two structurally different workload classes —
INT8 neural-network inference and a fixed-point streaming DSP pipeline
(64-point FFT, Q15) — so the result can be tested against computational
shape rather than a single benchmark. The complete DSP pipeline compiles
to a 12,800-byte ATmega128 program whose simulated cycle count the
compiler predicts exactly. Under the same sleep scheduling it saves 43%
per period where the ML classifier saves 96%, at the same period: the
DSP body keeps the processor busy for 56% of it, the classifier for about
2%, which is exactly the duty-cycle dependence the abstract describes.

This is a structural argument about the generality of a claim, not a
competing measurement: it is established in a cycle-accurate simulator,
whereas the result it contests was taken on physical hardware. Closing
that gap — on both AVR silicon and an STM32 target of the same
architectural family Heim et al. measured — is future work.

## Methodology

Pipeline: ONNX (hand-authored quantized graph, `export/export_model.py`) ->
IR (`compiler/src/ingest.c`, a hand-rolled protobuf parser) -> per-op
candidate generation (`compiler/src/codegen/candidates.c`) -> cost-based
selection (`compiler/src/codegen/select.c`, minimizes `energy_nj`) -> AVR
assembly (`compiler/src/emit.c`) -> `avr-gcc` assembly -> Avrora Beta
1.7.115 simulation (`sim/run_avrora.sh`, `-monitors=energy`, ATmega128).

Every energy figure in this project is either a cited published number or
an Avrora simulation output -- never a physical measurement (spec section
3). `cost_table.toml`'s per-cycle constant (2.8375 nJ/cycle) is calibrated
to Avrora's own built-in ATmega128 power model (3.0V, 7.5667mA active
current, decompiled from `avrora.jar`'s `avrora.sim.mcu.ATMega128.class`;
see `SOURCES.md` and spec v2 section 13) rather than the ATmega128
datasheet's 5V/17mA row an earlier version used -- the first
end-to-end run showed the datasheet-derived constant overstated Avrora's
real simulated energy by ~3.75x, even though predicted and real cycle
counts matched almost exactly. No source available to this project
supports differentiating energy by instruction *type* within CPU active
mode (spec v2 section 12) -- every `cost_table.toml` entry reduces to
`cycles x this constant`.

Candidate generation and register allocation are
deliberately scoped, not a fully general "any value in any of AVR's 32
registers across any op boundary" allocator. AVR's register file is
committed as follows for this project's per-op arithmetic (see
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

That leaves r9-r15 and r26-r31 (13 registers) genuinely idle at all times,
plus two more pools available only when their "owning" op isn't running:
r24/r25 (idle during MatMul) and r3-r8 (idle during MatMul, since
Requantize and MatMul never execute concurrently in this project's
straight-line, no-branches programs). `regalloc_next_use`
(`compiler/src/codegen/regalloc.c`) performs real next-use analysis: it
marks a value as worth caching when it is a MatMul's activation input,
which is read once per output channel (a genuine, bounded-distance reuse
pattern), and leaves every other op's output alone, since every other op
in this project's graphs consumes its inputs exactly once. Wider,
general-purpose cross-op register residency was considered and explicitly
not attempted, because it would require either shrinking the intra-op
scratch budget above (risking the already golden-value-validated
arithmetic) or accepting collisions between a cached cross-op value and a
later op's own scratch use.

### Register cache-pool gap and its closure

The first implementation reserved a 15-register cache pool (r9-r15,
r24-r25, r26-r31) for MatMul's cached-input candidate. This covers fc2's
MatMul (K=8, the contraction dimension of its activation input) fully,
but fc1's MatMul (K=16) exceeded the pool by exactly one register: byte
15 of the 16-byte input still fell back to a per-use `lds` (2 cycles)
instead of the cached `mov` (1 cycle) the other 15 bytes got. This was a
real, measurable gap, not a rounding error -- fc1's MatMul candidate
initially priced at 5,101.825 nJ instead of its true achievable minimum.

The fix: r3-r8 (Requantize's 48-bit product buffer, 6 registers) are also
idle during any MatMul's own execution, for a different reason than the
other pools -- they *are* used elsewhere in the same program (by every
Requantize op), but never concurrently with a MatMul, since this is a
straight-line program with no interleaving: by the time any Requantize op
runs, every MatMul that used the cache pool has already finished and
stored its result to SRAM. Extending `MATMUL_CACHE_POOL_SIZE` from 15 to
21 (adding r3-r8) let fc1's MatMul cache all 16 input bytes. This was
re-verified against the golden-value test (bit-exact classifier output
unchanged) before and after the change -- a correctness-preserving,
purely energy-improving fix.

Impact of closing this specific gap, on the real Avrora simulation of the
compiled classifier:

| | before (15-register pool) | after (21-register pool) |
|---|---:|---:|
| Optimized cycles | 6,024 | 6,018 |
| Optimized energy | 17,093.175300 nJ | 17,076.150225 nJ |
| Delta vs. naive | -106 cycles / -300.776 nJ (-1.73%) | -112 cycles / -317.80 nJ (-1.83%) |

This is offered as a worked example of the broader point: AVR's register
budget is the binding constraint on how much a next-use allocator can
achieve here, and small, principled (not ad hoc) increases to that budget
-- justified by an explicit non-overlap argument, not by guesswork --
directly translate into measurable energy reductions. It also demonstrates
the project's verification discipline: every register-allocation change
was re-checked against the same bit-exact golden-value test used to
validate the naive baseline, not assumed correct because it "should"
be.

### Periodic power-aware scheduling

Active-mode optimization works *within* Active mode -- every instruction it
chooses between still costs `cycles x 2.8375 nJ`. Periodic scheduling adds a
second, independent axis: whether the MCU is put to sleep between
inferences at all. `compiler/src/codegen/periodic.c` emits two matched
program variants around the *same* classifier body:

- **Active (busy-wait) policy**: the MCU polls a tick flag in a tight
  loop between inferences, never leaving Active mode.
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
with a `memcmp` over the two variants' BODY regions, not just an
assertion in prose. A shared Timer0-overflow ISR (saves/restores r16 and
SREG, sets an `overrun` flag if a tick arrives while an inference is
still `running`) drives both policies identically.

**Timer0 prescaler sweep.** Four ATmega128 Timer0 CS02:0 divisors (8, 32,
128, 1024) were swept, each defining a different period between wake
events (2,048 / 8,192 / 32,768 / 262,144 cycles at the simulated 1 MHz
clock). Before running any pair through Avrora, `sim/run_phase_b.py`
statically rejects a divisor if the compiler's own predicted BODY cycle
count exceeds that period -- divisor 8's period (2,048 cycles) is shorter
than either variant's predicted body (5,456 naive / 5,344 optimized
cycles), so it is **excluded from the primary analysis on a static
deadline check before its numbers are trusted**, not silently folded in.
Divisor 8 *was* still simulated -- its four raw Avrora runs are retained
in `sim/fixtures/phase_b/two_by_two.md` as a rejected observation, per
the project's own rule of keeping rejected rows visible with their reason
rather than discarding them -- and those runs corroborate the static
prediction rather than merely repeating it: the count-4 and count-5
Avrora runs for every divisor-8 variant are byte-identical (same cycle
count, same energy), which is exactly what happens when the program hits
`overrun` and halts on its first tick rather than completing a 5th
inference.

**Differential (steady-state) measurement.** Every reported number is
`E(inference_count=5) - E(inference_count=4)`, not a single raw run. This
cancels the one-time initialization cost (stack/Timer0/ISR setup) and
isolates the true per-inference marginal cost, including the fact that
the active and Power-save variants have different fixed wrapper lengths
(confirmed in the raw counts: prescaler-32 count-4 totals 39,011 active
cycles vs. 40,006 Power-save cycles, a 995-cycle wrapper-length
difference that the differential method makes non-confounding).
`sim/tests/test_compare_phase_b.py` locks this in
(`test_uses_equal_steady_state_increments_despite_995_cycle_offset`).

**Validity gates**, checked before any pair's numbers are reported: equal
invocation count between the naive/optimized-and-active/Power-save
quartet, zero overrun, bit-exact classifier output (`[0,-3,18,27]`,
the classifier's golden value), and identical `body_sha256` across all four
variants of a divisor. A pair failing any of these is marked `rejected`
with its reason, never silently dropped (`sim/fixtures/phase_b/primary.md`,
`two_by_two.md`, `manifest.json`).

## Results: ML Classifier

### Active-mode results

Real Avrora simulation of the compiled `tiny_classifier.onnx` (16->8->4
INT8 two-layer classifier), naive (single candidate per op, every value
reloaded from SRAM per use) vs. optimized (candidate diversity for MatMul +
next-use-informed input caching):

| | naive | optimized | delta |
|---|---:|---:|---:|
| Cycles (Avrora, real) | 6,130 | 6,018 | -112 (-1.83%) |
| Energy (Avrora, real) | 17,393.951625 nJ | 17,076.150225 nJ | -317.80 nJ (-1.83%) |
| Energy (compiler predicted) | 17,391.038 nJ | 17,073.238 nJ | -317.80 nJ |
| Golden-value correctness (vs. `export/gen_golden.py`'s independent numpy reference) | bit-exact | bit-exact | both correct |

Predicted and real Avrora numbers agree to within the known +1-cycle fixed
harness overhead (see `sim/fixtures/bringup_smoke.avrora.txt`) on both
builds, which is itself a useful internal-consistency check on the cost
model.

Per-op breakdown shows the improvement is concentrated exactly where the
mechanism predicts it should be -- the two `OP_MATMUL` ops (the only ops
whose activation input is reused across a loop rather than touched once):

| op | kind | naive energy (nJ) | optimized candidates | optimized energy (nJ) | saved |
|---:|---|---:|---:|---:|---:|
| 3 | MatMul (fc1) | 5,357.200 | 2 | 5,084.800 | 272.400 |
| 9 | MatMul (fc2) | 1,407.400 | 2 | 1,362.000 | 45.400 |
| all others | -- | (unchanged) | 1 | (unchanged) | 0 |

### Periodic scheduling results

**Headline finding: periodic power-aware scheduling is what makes the
active-mode saving observable at all.** Under the Power-save policy, the naive-vs-optimized energy delta
is **312.606 nJ per inference, exactly, at every accepted prescaler**:

| prescaler | naive Power-save (nJ) | optimized Power-save (nJ) | delta |
|---:|---:|---:|---:|
| 32 | 15,814.9276 | 15,502.3217 | 312.6060 |
| 128 | 16,954.9468 | 16,642.3409 | 312.6060 |
| 1024 | 27,595.1260 | 27,282.5200 | 312.6060 |

This is not a coincidence -- it is the active-mode optimizer's 112-cycle
saving (confirmed from the steady-state increments: naive 5,530 active
cycles/inference vs. optimized 5,418, a 112-cycle difference, matching the
optimizer's own Avrora-simulated saving in the Results section above) times the
simulator's active/Power-save per-cycle energy gap. Both per-cycle rates were
re-derived independently from the raw CSV for this write-up (not merely
repeated from `cost_table.toml`): active = 23,244.9024 nJ / 8,192 cycles
= 2.8375125 nJ/cycle (matches the cost table's 2.8375 rounded constant);
Power-save = 0.0463875 nJ/cycle, solved from the same period's naive
active/Power-save energy and cycle split. `112 x (2.8375125 -
0.0463875) = 112 x 2.7911250 = 312.6060 nJ`, matching the table above to
the fourth decimal place.

**Cross-platform reproduction.** All numbers above were originally
generated on Windows. The prescaler-32 matched pair (naive vs.
optimized, both policies, counts 4 and 5 -- the six raw observations
behind the 312.606 nJ headline number) was independently regenerated
from source on a second, unrelated machine (Linux) with a *different*
compiler binary (avr-gcc 16.1.0 vs. the original run's avr-gcc 15.2.0,
`ckormanyos/real-time-cpp` build) and a *different* JDK 8 vendor/build
(Temurin 8u504-b01 vs. the original run's Zulu 8.0.492) -- everything
that could plausibly vary between environments, did. Only `avrora.jar`
was held identical, and verifiably so: its SHA-256
(`016021f4...eb`) matches byte-for-byte between the original manifest
record and the independently downloaded copy used for reproduction, not
merely "the same version string."

| observation | Windows (original) | Linux (reproduced) |
|---|---:|---:|
| optimized/active, count 4 -> 5 (nJ) | 110,376.398738 -> 133,621.301138 | 110,376.398738 -> 133,621.301138 |
| optimized/Power-save, count 4 -> 5 (nJ) | 64,357.827300 -> 79,860.148950 | 64,357.827300 -> 79,860.148950 |
| naive/Power-save, count 4 -> 5 (nJ) | 65,613.446700 -> 81,428.374350 | 65,613.446700 -> 81,428.374350 |
| optimized Power-save marginal (nJ) | 15,502.32165 | 15,502.32165 |
| naive Power-save marginal (nJ) | 15,814.92765 | 15,814.92765 |
| **headline delta (nJ)** | **312.6060** | **312.6060** |

Every figure matched to the last recorded decimal place -- not "close
to," identical. The four regenerated `.S` files' `; BODY BEGIN`/`; BODY
END` regions were also confirmed byte-identical to the committed
fixtures (after normalizing line endings, since the original files are
CRLF and the freshly generated ones are LF -- a text-encoding artifact
of the two systems, not a content difference; `diff` after `tr -d
'\r'` shows zero lines changed). This is offered as direct evidence
against a specific failure mode of simulator-based results: that the
number could be an artifact of one machine's specific toolchain build
rather than a property of the emitted program and Avrora's power model.
It is not the same as hardware validation (see Limitations), but it is
stronger than a single-machine result.

**Under the active (busy-wait) policy this same 112-cycle saving is
worth exactly 0 nJ.** `active_nj` in every naive/optimized pair is
byte-identical at a given prescaler (e.g. both read 23,244.9024 nJ at
prescaler 32) because busy-wait energy is `period x 2.8375 nJ/cycle`,
fully determined by the wake period alone -- finishing the classifier
body 112 cycles sooner under busy-wait just buys 112 more polling
cycles at the same total energy. **The instruction-selection saving is
real but invisible without sleep scheduling; periodic scheduling is the
precondition for the active-mode saving being measurable, not a separate,
additive percentage.**

**Secondary: duty-cycle-driven scheduling saving**, comparing the
Power-save policy against an always-Active busy-wait strawman at the
same wake period (not a compiler-selection result -- a scheduling
result, reported separately per the distinction above):

| prescaler | period (cycles) | active_nj | powersave_nj | saved_nj | saving % | status |
|---:|---:|---:|---:|---:|---:|---|
| 8 | 2,048 | -- | -- | -- | -- | **rejected** (predicted body exceeds period; corroborated by identical count-4/count-5 Avrora runs on all 4 divisor-8 variants) |
| 32 | 8,192 | 23,244.90 | 15,502.32 (optimized) | 7,742.58 | 33.31% | accepted |
| 128 | 32,768 | 92,979.61 | 16,642.34 (optimized) | 76,337.27 | 82.10% | accepted |
| 1024 | 262,144 | 743,836.88 | 27,282.52 (optimized) | 716,554.36 | 96.33% | accepted |

(Full 4x2x2 matrix -- both compute paths, both policies, counts 4 and 5
-- in `sim/fixtures/phase_b/two_by_two.md`; raw Avrora output per run in
`sim/fixtures/phase_b/*.avrora.txt`; provenance/hashes in
`sim/fixtures/phase_b/manifest.json`.)

This saving percentage is duty-cycle-parameterized -- it is a property of
*how long the MCU chooses to sleep between wake events*, and approaches
100% as the period grows, not a fixed number this project can claim as
"the" saving. It is reported here as the scheduling-level result it is,
kept explicitly separate from the 312.606 nJ compiler-attributable result
above.

#### Relation to the TinyML "latency is a perfect proxy for energy" claim

Heim, Biri, Qu, and Thiele (arXiv:2104.10645, 2021) measured inference
energy on ARM Cortex-M boards and found near-perfect latency/energy
correlation (r = 0.9946 whole-network, r = 0.9995 per-layer), concluding
that "the inference latency is a perfect proxy for the energy
consumption of the investigated MCUs" and that latency-optimization
results "also apply to energy consumption." That claim holds *within
Active mode*, and this project's active-mode results agree with it: its
energy model there reduces to `cycles x 2.8375 nJ`, so the ~1.83%
energy saving is mathematically identical to a cycle-count saving --
exactly what Heim et al.'s regime predicts, and this project does not
contest that.

Periodic scheduling is where the claim breaks, and the 312.606 nJ / 0 nJ contrast
above is the direct evidence: two variants (naive vs. optimized under
busy-wait) share the *same* absolute latency between wake events (both
still take the full 8,192-cycle period to reach the next tick) yet
differ in energy by 0 nJ, while the same two variants under Power-save
differ in energy by 312.606 nJ despite an even smaller latency
difference once idle time is excluded. Once a sleep state exists below
Active, cycles stop being a proxy for energy, because a Power-save cycle
and an Active cycle cost 2.7911 nJ apart (a ~61.2x ratio, decompiled from
Avrora's own `ATMega128.class`/`Energy.class`, see `SOURCES.md`) --
elapsed latency alone cannot distinguish which kind of cycle was spent.
This is a scope objection, not a numbers dispute: Heim et al.'s Cortex-M
benchmark never left Active mode, so their model has no term for a
below-Active state; ATmega128, like nearly all MCUs, exposes one. The
full comparison, including why this is a fair, non-strawman contention
and how it relates to prior compiler-directed sleep-scheduling and
instruction-level-energy work, is in `documents/LITERATURE_SURVEY.md`
section 1.

## DSP Workload

The second workload is a fixed audio-style pipeline built directly as IR
(`compiler/src/dsp_build.c`) rather than ingested: 64 Q15 samples, a Hamming
window, a 64-point FFT, per-bin magnitude and peak selection.
`optifine --dsp` compiles it (README, "Compile the DSP pipeline"). The
figures in this section are derived from the retained raw run in
`sim/fixtures/dsp/` by `sim/report_results.py` (generated table:
`sim/fixtures/dsp/results.md`; verify with `python3 sim/run_dsp.py check`).

### Why the DSP lowering uses loops

The original design lowered every op fully unrolled, as the ML
path does. A pre-flight flash model -- checked against real `avr-size`
output for the Window op (4,608 B predicted, 4,608 B linked) -- projected the
unrolled pipeline at about 497 KB, roughly 379% of the ATmega128's 128 KiB
flash. That was a code-representation problem, not a workload problem, so the
workload was kept and the representation changed: each repeated structure is
emitted once inside a statically bounded counted loop. The trip counts are
compile-time constants, so pricing stays exact: the cost model records each
loop's region and trip count and charges every instruction by how often it
executes, including the one-time fall-through of the closing branch.

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

The result is the whole pipeline in 12,800 B of flash with exact semantics
and zero cycle-prediction error, against a projection that could not fit.
The 497 KB figure is the pre-redesign projection that motivated this change,
not a measurement of anything that was built.

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

### DSP results

Avrora simulation of the `optifine --dsp` program on the default demo input
(ATmega128, 8 MHz):

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
prices the same cycles at `cost_table.toml`'s rounded 2.8375 nJ/cycle; the
table is pinned by the periodic-scheduling manifest and was not changed. The cycle count
does not depend on the input samples: every data decision is a mask, and
only loop control branches.

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
(Isolated FFT fixtures elsewhere in the test suite report 73,513 cycles
because they include their own `break`; inside the complete program the
FFT's six stages cost 73,512.) FFT and Magnitude are 84% of the cycles;
Window, still fully unrolled, is 36% of the flash.

SRAM is 2,336 B of graph tensors (one buffer per op, none reused) plus a
181-byte scratch arena. The arena is an overlay across op lifetimes, so the
most any op holds at once is 64 B (PeakExtract's `selected[]`); per-op
scratch is not additive. The program uses no stack (the periodic build of
the next section uses 4 B, for its timer interrupt).

The two workloads side by side (they compute different things, so this
shows the backend's breadth rather than comparing the algorithms' efficiency):

| | ML classifier (optimized) | DSP pipeline |
|---|---:|---:|
| Cycles | 6,018 | 148,335 |
| Time @ 8 MHz | 0.752 ms | 18.542 ms |
| Simulated active energy | 17,076.15 nJ | 420,902.42 nJ |
| Linked flash | 11,524 B | 12,800 B |

(ML flash is `avr-size` of the current `--optimized` build.)

The DSP path has one lowering per op, so there is no naive-versus-optimized
pair as on the ML path: under a single per-cycle energy constant the two
would select the same code (the active-mode finding), so the effort went
into periodic scheduling instead.

### DSP periodic scheduling results

A second, structurally independent energy-delta data point: the DSP program
as the periodic wake body, in the unchanged periodic-scheduling
wrapper (`optifine --dsp --periodic-count N --wait-policy
active|powersave --timer-prescaler P`), swept over the same four Timer0
prescalers, both wait policies and counts 4 and 5 -- 16 Avrora runs,
retained in `sim/fixtures/phase_b_dsp/` and revalidated without running any
tool by `python3 sim/run_phase_b_dsp.py`. Input: `models/dsp_demo_input.txt`.
Figures are steady-state `E(5) - E(4)` increments, as for the ML sweep.

Carrying the DSP program needed one wrapper change: the terminal output
copy handled only the ML classifier's 4-byte INT8 output, and now uses the
output tensor's real size (16 bytes, `FIXED_Q15[8]`). The wrapper also emits
the twiddle table after its interrupt handler, where control cannot reach it.
Every ML program, periodic or not, that the compiler emits is
byte-identical to before this change. `test_dsp_pipeline` checks that the periodic build's
initialization, body and table are the ordinary `--dsp` program's,
instruction for instruction, and that its wrapper writes no register but
r16 (saved by the interrupt handler); in the interpreter, the body run five
times after one initialization reproduces every buffer and all 16 output
bytes each time. As in the ML experiment, no simulated SRAM is read back
from Avrora.

| prescaler | period (cycles) | DSP compute (cycles) | idle window | busy-wait (nJ/period) | Power-save (nJ/period) | saved | saving |
|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 2,048 | 147,565 | none | -- | -- | -- | compute-bound |
| 32 | 8,192 | 147,565 | none | -- | -- | -- | compute-bound |
| 128 | 32,768 | 147,565 | none | -- | -- | -- | compute-bound |
| 1024 | 262,144 | 147,565 | 114,579 | 743,836.88 | 424,239.11 | 319,597.77 nJ | 42.97% |

(Generated by `sim/report_results.py`; the per-run tables are
`sim/fixtures/phase_b_dsp/primary.md` and `two_by_two.md`.)

Only prescaler 1024 has a period long enough for the DSP body; at the three
shorter periods the body overruns the timer, which the static deadline check
the ML sweep applies to its prescaler 8 rejects here as well. That is a
property of the workload against these periods: there is no idle time to
sleep through, and the wrapper's overrun flag ends each such run after one
body, as it ends the ML sweep's prescaler-8 runs.

At prescaler 1024 each Power-save period is 147,639 Active cycles (the
body and the wrapper) and 114,505 Power-save cycles; busy-waiting spends
all 262,144 in Active mode. The saving is the idle part of the period moved
from 2.8375 to 0.0464 nJ per cycle, so it tracks the duty cycle, not the
DSP code: the ML classifier, whose body fills about 2% of the same period,
saves 96.33% there. The two points are separate results and are not
averaged.

One comparison rule differs from the ML sweep, and only for this sweep.
The busy-wait loop polls the tick flag in an 8-cycle loop, so each wake is
detected up to 8 cycles late, and with the DSP body's length that
detection phase does not settle: Avrora's busy-wait increments were
262,141 and 262,149 cycles around the exact 262,144 of every Power-save
increment (the ML body happens to hit a fixed point, where the increments
match exactly, as the ML comparator requires). The DSP sweep accepts a pair
whose Power-save increment equals the Timer0 period exactly and whose
busy-wait increment is within the loop's 8 cycles of it, and scales the
busy-wait energy (all Active cycles at constant power) to the exact period:
a factor of 262,144 / 262,141 = 1.0000114 here, from a raw Avrora busy-wait
increment of 743,828.36 nJ to the normalized 743,836.88 nJ in the table. The scaled figure, 743,836.8768 nJ, is
identical to the ML sweep's directly simulated busy-wait energy at the same
prescaler, as it must be: busy-waiting spends the whole period in Active
mode whatever the workload.

The periodic DSP program links at 13,172 B of `.text` (the ordinary one:
12,800 B). Static SRAM is 2,521 B -- the 2,517 B of DSP tensors and scratch
plus the wrapper's 4 scheduler bytes -- and the timer interrupt, the only
stack user, needs at most 4 B (two pushes and the return address; it does
not nest, and nothing calls).

## Limitations

- **Demo input is fixed and compile-time-baked**, not live sensor data
  (`models/tiny_classifier_golden_input.txt`). A real deployment would
  read this from ADC/UART/host at runtime. This is a deliberate scope cut,
  stated here rather than glossed over.
- **No runtime saturation on Requantize overflow.** This project verifies
  the actual fixed demo input's forward pass at compile time
  (`lower_verify_demo_forward_pass`) and refuses to compile rather than
  saturate -- a static worst-case-over-all-inputs bound is mathematically
  infeasible for MinMax-calibrated per-tensor quantization on
  unbounded-support inputs (confirmed empirically, not just in theory).
  A real deployment accepting arbitrary runtime inputs would need actual
  runtime saturation, not implemented here.
- **Requantize's fixed-point multiplier is 16-bit, not 32-bit** (Q15-style,
  derived via `frexp`). Chosen because the int32 accumulator only ever
  uses ~19 bits of real magnitude for these layer shapes, so a 16-bit
  multiplier's precision already exceeds what the int8 output can
  represent -- not a shortcut, a match to the actual precision ceiling.
- **Register allocation is scoped to MatMul's activation
  input**, not a fully general cross-op allocator (see Methodology). This
  is a real, honest limit of AVR's register budget given this project's
  already-committed intra-op scratch usage, not an oversight.
- **Weight constants load via synthesized `ldi`+`sts`**, not
  `.rodata`/flash-resident + `lpm`. `sim/run_avrora.sh` builds with
  `avr-gcc -nostartfiles`, so there is no crt0 to copy an initialized
  `.data` section into SRAM; flash-resident constants would need
  `lpm`/Z-register addressing and a new cost-table category, deferred.
- **Single demo model, single input vector.** No claim is made about
  behavior across model architectures, layer counts, or a broader input
  distribution -- see spec section 9's non-goals (no physical hardware,
  no claim of beating a production ML compiler, no multi-ISA support,
  limited operator coverage).
- **The cost-category-to-cost_table.toml mapping for the opcodes beyond the
  original 7** (`muls`, `mulsu`, `adc`, `sbc`, `lsl`,
  `clr`, `com`, `and`, `asr`, `ror`) borrows same-cycle-count categories
  rather than having their own cited AVR Instruction Set Manual section
  numbers -- numerically inert (see `SOURCES.md`) but a traceability gap
  for a fully rigorous write-up, left to a future sourcing pass.
- **Periodic scheduling has no physical-hardware correlation.** All Power-save/Timer0
  behavior is Avrora's simulated model; Avrora has no Watchdog Timer
  implementation at all (confirmed via `jar tf avrora.jar | grep -i
  watchdog` returning zero matches, and empirically by a Power-down +
  watchdog spike hanging indefinitely -- see `documents/PHASE_B_NOTES.md`),
  so this project cannot claim Power-down mode is simulatable here, only
  Idle and Power-save, and only Power-save is used for the reported
  numbers.
- **The deadline/overrun check is compiler-side static prediction plus
  Avrora's own reported cycle/state counts, not an in-simulation SRAM
  readback of the program's own `completed`/`overrun` bytes.** Divisor
  8's rejection is corroborated by external evidence (identical count-4
  and count-5 runs, consistent with an early halt) rather than by reading
  the emitted `overrun` flag back out of simulated memory; no claim is
  made beyond what `manifest.json`'s `evidence_limit` field states.
- **Single MCU, single model, single input, four prescalers.** No claim
  is made about behavior across other AVR chips, sleep-mode
  combinations, model architectures, or workloads with irregular
  (non-periodic) trigger patterns -- the scheduling saving formula
  (`saving ~= 0.984 x idle_fraction`, `documents/LITERATURE_SURVEY.md`
  section 1.3) is specific to this project's calibrated
  active/Power-save current ratio.
- **The DSP figures are simulated, like everything else here.** The
  generated program executes correctly and its cycle count is predicted
  exactly, but only in Avrora and the test interpreter; no ATmega128 board
  has run it.
- **One DSP input is simulated in Avrora.** The 18-input correctness sweep
  runs in the test interpreter; Avrora reproduces the cycle count, which
  does not depend on the samples, for the demo input.
- **The DSP periodic-scheduling result is one accepted point.** Only prescaler 1024 is
  long enough for the DSP body, and its busy-wait figure is scaled by
  1.0000114 to the exact period because busy-wait wake detection jitters
  within an 8-cycle poll loop (see "DSP periodic scheduling results").
- **No DSP candidate diversity:** each op has one lowering.
- **PeakExtract reports magnitudes, not bin positions.** Reporting positions
  would need a wider or second output tensor, an IR change not made here.
- **Extending the retained ML scheduling experiment requires its recorded
  toolchain.** `python3 sim/run_phase_b.py reproduce-retained` revalidates
  the retained evidence on any machine and only *reports* whether the local
  toolchain matches the recorded one; `supplemental`, which adds runs to that
  experiment, refuses unless the recorded compiler, assembler and simulator
  binaries are present. A fresh experiment (`run-new`) records whatever
  toolchain it used. The numbers themselves replicate across platforms (see
  "Cross-platform reproduction" above).

## Reproducibility

Raw simulator output is retained under `sim/fixtures/` -- the active-mode ML
runs, the ML periodic-scheduling sweep (`phase_b/`), the complete DSP program
(`dsp/`) and the DSP periodic-scheduling sweep (`phase_b_dsp/`) -- each
experiment with a manifest pinning its inputs, artifacts and tools by
SHA-256. `python3 sim/report_results.py` regenerates every table in this
report from those files; `python3 sim/run_dsp.py check`,
`python3 sim/run_phase_b.py reproduce-retained` and
`python3 sim/run_phase_b_dsp.py` revalidate the retained experiments without
running any tool, and their `capture`/`run-new` modes repeat them with a
local toolchain into a fresh directory. The README lists the commands.

## Future Work

- Measure the generated programs on physical ATmega128 hardware, and an
  STM32 target of the family Heim et al. measured.
- Per-instruction-type energy costs, if a source or measurement supports them.
- Runtime input from a sensor or host instead of compile-time inputs.
- DSP optimizations the current baseline leaves open: FFT and Magnitude are
  84% of its cycles, and the fully unrolled Window is 36% of its flash.
- Broader workloads: more ONNX operators, other FFT lengths, peak positions
  as output.
