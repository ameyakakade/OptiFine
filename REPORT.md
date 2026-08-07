# OptiFine: Energy-Aware Code Generation for Edge ML -- Report

Status: draft. Phase A (naive baseline), milestone 5 (real candidate
diversity + next-use register allocation), and Phase B (compiler-emitted
sleep scheduling) are done and produce the real numbers below, from the
actual compiler running the actual `tiny_classifier.onnx` model through
real Avrora. Milestone 6 (broader end-to-end comparison, DSP path) is not
yet started. `documents/PHASE_B_NOTES.md` has the exploratory spike
research trail that preceded Phase B's implementation (which sleep modes
Avrora can and can't simulate, and why); `documents/PHASE_B_PLAN.md` has
the implementation task breakdown; `documents/LITERATURE_SURVEY.md` has
the full related-work positioning summarized in this report's Phase B
section below.

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

The project does not dispute their measurement. Its first phase
corroborates it: an energy-cost model driving instruction selection and
next-use register allocation reduces exactly to cycles times a constant,
and its 1.83% simulated saving is a pure cycle reduction — the only lever
their regime predicts is available. The divergence appears in the second
phase, where the compiler itself schedules sleep-state entry around a
periodic workload. On a calibrated ATmega128 model, an Active cycle costs
2.8375 nJ and a Power-save cycle 0.0464 nJ — a ratio of 61.2x. Latency and
energy decouple: idle duration is unbounded while its energy cost is
bounded by sleep current, yielding 96.3% savings at a ~98% idle fraction.
The two phases also interact — the 112-cycle saving from the first is
worth 312.6 nJ per inference under sleep scheduling and exactly zero under
busy-wait, making the sleep-aware compiler the precondition for the
instruction-level optimization being observable at all.

The same backend targets two structurally different workload classes —
INT8 neural-network inference and a fixed-point streaming DSP pipeline
(64-point FFT, Q15) — so the result can be tested against computational
shape rather than a single benchmark. The DSP path is currently in
implementation; all figures reported above are from the ML workload.

This is a structural argument about the generality of a claim, not a
competing measurement: it is established in a cycle-accurate simulator,
whereas the result it contests was taken on physical hardware. Closing
that gap — on both AVR silicon and an STM32 target of the same
architectural family Heim et al. measured — is deferred Phase 2 work
(spec v2 section 2).

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
datasheet's 5V/17mA row an earlier version used -- Phase A's first
end-to-end run showed the datasheet-derived constant overstated Avrora's
real simulated energy by ~3.75x, even though predicted and real cycle
counts matched almost exactly. No source available to this project
supports differentiating energy by instruction *type* within CPU active
mode (spec v2 section 12) -- every `cost_table.toml` entry reduces to
`cycles x this constant`.

Candidate generation and register allocation (milestone 5) are
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
validate Phase A's naive baseline, not assumed correct because it "should"
be.

### Phase B: compiler-emitted sleep scheduling

Phase A and milestone 5 both optimize *within* Active mode -- every
instruction they choose between still costs `cycles x 2.8375 nJ`. Phase B
adds a second, independent axis: whether the MCU is put to sleep between
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
Phase A/milestone 5 already used -- so a policy can only change *how the
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
Phase A's own golden value), and identical `body_sha256` across all four
variants of a divisor. A pair failing any of these is marked `rejected`
with its reason, never silently dropped (`sim/fixtures/phase_b/primary.md`,
`two_by_two.md`, `manifest.json`).

## Results

Real Avrora simulation of the compiled `tiny_classifier.onnx` (16->8->4
INT8 two-layer classifier), naive (Phase A, single candidate per op,
every value reloaded from SRAM per use) vs. optimized (milestone 5, real
candidate diversity for MatMul + next-use-informed input caching):

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

### Phase B results

**Headline finding: Phase B is what makes Phase A's saving observable at
all.** Under the Power-save policy, the naive-vs-optimized energy delta
is **312.606 nJ per inference, exactly, at every accepted prescaler**:

| prescaler | naive Power-save (nJ) | optimized Power-save (nJ) | delta |
|---:|---:|---:|---:|
| 32 | 15,814.9276 | 15,502.3217 | 312.6060 |
| 128 | 16,954.9468 | 16,642.3409 | 312.6060 |
| 1024 | 27,595.1260 | 27,282.5200 | 312.6060 |

This is not a coincidence -- it is Phase A's 112-cycle saving (confirmed
from the steady-state increments: naive 5,530 active cycles/inference vs.
optimized 5,418, a 112-cycle difference, matching milestone 5's own
Avrora-simulated saving in the Results section above) times the
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
cycles at the same total energy. **Phase A's instruction-selection
saving is real but invisible without Phase B's sleep scheduling; Phase B
is the precondition for Phase A being measurable, not a separate,
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
Active mode*, and Phase A/milestone 5 agree with it: this project's own
energy model there reduces to `cycles x 2.8375 nJ`, so Phase A's ~1.83%
energy saving is mathematically identical to a cycle-count saving --
exactly what Heim et al.'s regime predicts, and this project does not
contest that.

Phase B is where the claim breaks, and the 312.606 nJ / 0 nJ contrast
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

## Limitations

- **Demo input is fixed and compile-time-baked**, not live sensor data
  (`models/tiny_classifier_golden_input.txt`). A real deployment would
  read this from ADC/UART/host at runtime. This is a deliberate Phase A
  scope cut, stated here rather than glossed over.
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
- **Milestone 5's register allocation is scoped to MatMul's activation
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
- **Milestone 5's cost-category-to-cost_table.toml mapping for the ~9
  opcodes beyond the original 7** (`muls`, `mulsu`, `adc`, `sbc`, `lsl`,
  `clr`, `com`, `and`, `asr`, `ror`) borrows same-cycle-count categories
  rather than having their own cited AVR Instruction Set Manual section
  numbers -- numerically inert (see `SOURCES.md`) but a traceability gap
  for a fully rigorous write-up, deferred to milestone 7's sourcing pass.
- **Phase B has no physical-hardware correlation.** All Power-save/Timer0
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
  (non-periodic) trigger patterns -- Phase B's saving formula
  (`saving ~= 0.984 x idle_fraction`, `documents/LITERATURE_SURVEY.md`
  section 1.3) is specific to this project's calibrated
  active/Power-save current ratio.
- **`sim/run_phase_b.py`'s reproduction path hard-gates on a recorded
  `avr-gcc` binary SHA-256**, which will legitimately differ on any
  machine other than the one that generated `manifest.json` (confirmed:
  two of this project's own Python regression tests fail on a from-scratch
  Linux checkout with no bundled `avr-gcc`, for exactly this reason, not
  a code defect). This is a real reproducibility gap in the *automated*
  reproduction script for an artifact-evaluation reviewer on a different
  machine, not yet fixed -- distinct from whether the numbers themselves
  replicate cross-platform, which they do (see "Cross-platform
  reproduction" under Phase B results above).
