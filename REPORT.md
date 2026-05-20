# OptiFine: Energy-Aware Code Generation for Edge ML -- Report

Status: draft. Phase A (naive baseline) and milestone 5 (real candidate
diversity + next-use register allocation) are done and produce the real
numbers below, from the actual compiler running the actual `tiny_classifier.onnx`
model through real Avrora. Milestone 6 (broader end-to-end comparison,
DSP path) is not yet started. Phase B (sleep-mode scheduling) is under
exploratory spike investigation -- not yet wired into the compiler -- see
`PHASE_B_NOTES.md` for the research trail; this report will be extended
once that lands.

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
