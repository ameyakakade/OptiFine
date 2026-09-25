# OptiFine Architecture

How the compiler is built, as implemented. For results and their method see
[`REPORT.md`](../REPORT.md); for commands see
[`REPRODUCIBILITY.md`](REPRODUCIBILITY.md).

```
ONNX classifier --(ingest.c)------\
                                   >-- typed IR --> lowering / candidates --> selection --> emission --> AVR assembly
DSP pipeline ---(dsp_build.c)-----/     (ir.c)      (lower.c, candidates.c)   (select.c)   (emit.c, program.c,
                                                                                            periodic.c)
                     assembly --> avr-gcc --> ELF --> Avrora 1.7.115 (-monitors=energy, ATmega128)
```

The compiler is C11 (`compiler/`, CMake, no dependencies beyond libc/libm).
One IR, one lowering and emission backend and one cost model serve both
workloads.

## Graph construction

- **IR** (`compiler/include/optifine/ir.h`, `src/ir.c`): a typed op graph.
  Each op has a kind, an element type (INT8, INT32, FIXED_Q15, ...), a shape
  and its input op ids. Ops run once each, in op-id order.
- **ML ingestion** (`src/pb_reader.c`, `src/ingest.c`): a hand-written
  protobuf reader for exactly the ONNX subset the demo classifier uses --
  Input, Const, MatMul, Add, Relu, Requantize, Output. Anything else is
  rejected; general ONNX coverage is a non-goal. The model is produced by
  `export/export_model.py`, which writes the quantized graph directly rather
  than going through PyTorch's quantized export.
- **DSP builder** (`src/dsp_build.c`): there is no standard graph format for a
  fixed audio pipeline, so the graph is constructed directly: Input, Const
  (Hamming coefficients, computed on the host at compile time), Window,
  BitReverse, six FftButterfly stages, Magnitude, PeakExtract, Output.
  `DSP_FFT_SIZE` is fixed at 64.

Inputs are compile-time constants: `--input` (int8 values for the classifier,
64 int16 Q15 samples for `--dsp`) is embedded in the program.

## Lowering

`src/codegen/lower.c` turns each op into exactly one correct instruction
sequence, a *candidate* (`AvrInstr` list plus its price). For the ML path this
is the **naive baseline**: every value is stored to its SRAM slot and reloaded
on each use.

- **SRAM layout** (`sram_layout.c`): one contiguous slot per op tensor from
  `0x0200` upwards, then the 181-byte DSP scratch arena (reserved in every
  layout, though only DSP ops use it), then, in periodic programs, the four
  scheduler bytes.
- **Register roles** (`codegen/registers.h`): r0/r1 multiply result, r2 a
  permanent zero, r3-r8 Requantize's 48-bit product, r16/r17 MAC operands,
  r18-r21 the 32-bit accumulator, r22-r25 scratch.
- **Requantize** uses a 16-bit fixed-point multiplier derived with `frexp`.
  Because MinMax per-tensor quantization cannot bound arbitrary inputs, the
  compiler evaluates the real forward pass for the embedded input at compile
  time (`lower_verify_demo_forward_pass`) and refuses to compile rather than
  saturate on overflow.
- **Weights** are loaded with synthesized `ldi`/`sts` (programs are assembled
  with `-nostartfiles`, so there is no crt0 to initialize `.data`).

## InstrBuf and EmitUnit

- **InstrBuf** (`codegen/instr_buf.h`) builds instruction sequences and prices
  them. It also records **counted loops**: `LoopRegion`s of `[first, last]`
  instructions executed `trip` times, which may nest.
- **EmitUnit** (`emit.h`, `emit.c`) is one emitted assembly file's label
  namespace. Each candidate's local labels are renumbered into the unit, and
  its global labels are checked for duplicates, so several looping ops can
  share one program.

## Exact cost model

`cost_table.toml` prices instruction categories; `SOURCES.md` cites each
entry. No available source supports different energy for different
instruction *types* within Active mode, so every entry is
`cycles x 2.8375 nJ`, a per-cycle constant calibrated to Avrora's own
ATmega128 model (3.0 V, 7.5667 mA, 8 MHz).

Pricing is per **executed** instruction. Straight-line code costs each
instruction once. Inside a counted loop an instruction costs its cycles times
the product of the enclosing trip counts, and the loop's closing branch is
priced exactly: taken `trip - 1` times, falling through once. Loops close as
`dec / brne head` when the body is within BRNE's reach, or
`dec / breq exit / rjmp head` beyond it. The trip counts are compile-time
constants and no data-dependent branch exists (DSP decisions are masks), so
the predicted cycle count equals Avrora's for every retained program. The one
unpriced instruction is the final `break`, which halts the simulator and
accounts for the fixed +1 cycle `sim/smoke.s` calibrates.

## Test interpreter

`compiler/tests/avr_interp.c` is a test-only AVR interpreter covering every
opcode the backend emits, including `lpm` from program memory. The test suite
runs generated programs in it and compares results exactly: the classifier
against `models/tiny_classifier_golden_output.txt` (from
`export/gen_golden.py`'s independent numpy reference), the DSP pipeline
against an integer host reference at every intermediate buffer. It also counts
executed cycles, which the tests compare with the compiler's prediction.

## Active-mode optimization

With `--optimized`, `src/codegen/candidates.c` generates more than one
candidate where it can and `select.c` keeps the cheapest by predicted energy:

- **MatMul** gets a second candidate that loads its activation input into a
  register cache once, instead of reloading it for every output channel.
  `regalloc_next_use` (`regalloc.c`) marks exactly those values, the only ones
  in these graphs read more than once. The cache pool is 21 registers (r9-r15,
  r24-r31, and r3-r8, which only Requantize uses and never concurrently with a
  MatMul), enough for the classifier's 16-byte input.
- Every other op keeps its single naive candidate.

Because every price is cycles times one constant, minimizing energy is
minimizing cycles under this model. `locality.c` is a no-op placeholder.

## Periodic scheduling wrapper

`src/codegen/periodic.c` wraps a compute body in a timer-driven periodic
program (`--periodic-count N --wait-policy active|powersave
--timer-prescaler 8|32|128|1024`):

- Timer0 runs from the asynchronous clock (`ASSR.AS0`), so it keeps counting
  in Power-save; its overflow interrupt defines the period,
  `256 x prescaler` cycles.
- **Busy-wait** (`active`) polls a tick flag in an 8-cycle loop, staying in
  Active mode. **Power-save** enters sleep with `cli`, a tick test, `MCUCR`
  set up, `sei`, `sleep` -- nothing between `sei` and `sleep`, which closes the
  flag-test/interrupt race.
- The body between `; BODY BEGIN` and `; BODY END` is emitted by the same
  function as the ordinary program, so the two policies differ only in how
  they wait (`test_periodic` compares the body bytes).
- The interrupt handler saves r16 and SREG, sets the tick and an `overrun`
  flag if a tick arrives while a body is still running. Four scheduler bytes
  (tick, running, overrun, completed count) follow the tensor layout; `; META`
  lines in the assembly name their addresses.
- The compiler prints the predicted initialization and per-body cycles, which
  the experiment scripts use for a static deadline check: a prescaler whose
  period is not longer than the body is rejected before any energy is
  compared.

For `--dsp`, the wrapper copies the output tensor at its real size (16 bytes,
`FIXED_Q15[8]`) and places the twiddle table after the interrupt handler,
where control cannot reach it.

## Bounded DSP control flow

Fully unrolled, the DSP pipeline would need about 497 KB against the
ATmega128's 128 KiB of flash, so its repeated structures are counted loops:

- **Window:** 64 unrolled Q15 products with compile-time coefficients.
- **BitReverse:** the 6-bit permutation into a complex buffer.
- **FFT:** radix-2 decimation-in-time, forward, 1/2 scaling per stage. Stage 0
  is one loop of 32 butterflies; stages 1-5 are a block loop around a
  butterfly loop.
- **Magnitude:** exact `floor(sqrt(re^2 + im^2))` per bin, a 32-bit unsigned
  square sum and a fixed 16-iteration bit-by-bit integer square root.
- **PeakExtract:** eight passes selecting the largest remaining magnitude
  (unsigned; ties go to the lower bin), excluding earlier winners through an
  op-local `selected[64]` table. Output is the eight values.

Only loop control branches; every data decision is a mask, so the cycle count
does not depend on the input samples.

## Scratch lifetime model

The DSP ops share a 181-byte scratch arena after the graph tensors. It is an
**overlay**: ops run once each, in sequence, so no two op lifetimes overlap
and any op may use any cell provided it writes a cell before reading it.
Results cross op boundaries only through graph tensors. `test_asm_unit`
checks the write-before-read rule over every DSP op's accesses, and the
pipeline tests repeat every run under different SRAM poison (and op by op
with the arena re-poisoned between ops). The arena covers the largest single
op's extent -- 64 B for PeakExtract's `selected[]` -- not the sum.

## Program-memory twiddle table

The FFT's 32 unique Q15 twiddle factors (cos and sin, 128 bytes) are a table
in program memory, read with `lpm r, Z+`. It is emitted after the program's
only `break` (or, in the periodic build, after the interrupt handler), below
address 0x10000, so plain `lpm` suffices and no `ELPM`/`RAMPZ` is needed.
