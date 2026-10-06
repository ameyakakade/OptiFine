# OptiFine Architecture

How the compiler is built, as implemented. For results and their method see
[`REPORT.md`](../REPORT.md); for commands see
[`REPRODUCIBILITY.md`](REPRODUCIBILITY.md).

```
ONNX classifier ---(ingest.c)----\
                                  >--> workload HIR --> ir_verify --> hir_to_mir --> MIR --> mir_verify --> AVR backend --> EmitUnit --> .s
fixed DSP pipeline --(dsp_build.c)/    (IrGraph)                     |  kernels from                    (avr_mir.c)     (emit.c)
                                                                     |  lower*.c enter as
                                                                     |  MIR_TARGET regions
                               program wrappers: program.c (one-shot programs), periodic.c (timer-driven periodic programs)

.s --> avr-gcc --> ELF --> Avrora 1.7.115 (-monitors=energy, ATmega128)

Planned, not implemented:
EDG IL --> EDG-to-MIR adapter --> MIR --> mir_verify --> AVR backend
```

The compiler is ISO C11 with extensions off (`CMAKE_C_EXTENSIONS OFF`, so
`-std=c11`), built with `-Wall -Wextra -Wpedantic`, under `compiler/` with no
dependencies beyond libc and libm. Everything except the command-line driver
(`src/main.c`) is the `optifine_core` library, which the tests link as well.

## 1. Frontends

- **ML ingestion** (`src/pb_reader.c`, `src/ingest.c`): a hand-written
  protobuf reader for exactly the ONNX subset the demo classifier uses --
  Input, Const, MatMul, Add, Relu, Requantize, Output. Anything else is
  rejected; general ONNX coverage is a non-goal. The model is produced by
  `export/export_model.py`, which writes the quantized graph directly.
- **DSP builder** (`src/dsp_build.c`): there is no standard graph format for a
  fixed audio pipeline, so the graph is constructed directly: Input, Const
  (Hamming coefficients, computed on the host at compile time), Window,
  BitReverse, six FftButterfly stages, Magnitude, PeakExtract, Output.
  `DSP_FFT_SIZE` is fixed at 64.

Inputs are compile-time constants: `--input` (int8 values for the classifier,
64 int16 Q15 samples for `--dsp`) is read, range-checked and embedded in the
program.

## 2. Workload HIR

`IrGraph` (`include/optifine/ir.h`, `src/ir.c`) is the **workload HIR**: a
typed graph of workload operators. Each op has a kind, an element type
(INT8, INT32, FIXED_Q15, COMPLEX_Q15), a shape and its input op ids, and the
ops run once each, in op-id order. It has no control flow, no scalar values
and no general memory; its operators (MatMul, FftButterfly, ...) are workload
kernels. It is not the compiler's general IR, and arbitrary imperative code
must not be forced into it (see "Planned adapter boundary").

## 3. Verification boundaries

- **`ir_verify`** (`include/optifine/ir_verify.h`) checks every contract the
  lowering relies on, after the frontend and before anything else: valid
  kinds and types, arity, inputs naming earlier ops, non-null shapes with
  nonzero dimensions, overflow-checked element and byte sizes, a Const
  initializer of exactly its tensor's byte size, one Input, one Output at the
  end, no mixing of ML and DSP operators, and each operator's shape and type
  contract (for example MatMul `INT8[K] x INT8[N,K] -> INT32[N]`, PeakExtract
  `FIXED_Q15[n] -> FIXED_Q15[k]`, `1 <= k <= n <= 255`). The driver calls it on
  both frontends' graphs. `test_ir_verify` rejects 35 malformed variants.
- **`mir_verify`** (`include/optifine/mir.h`) checks a MIR module: block and
  branch targets, exactly one terminator per block, value ids and types per
  opcode, immediates that fit, memory-object ids, in-bounds object accesses,
  no stores to constants, stack objects used only by their owner, and that
  every use of a value is preceded by an assignment on every path from the
  entry. `hir_to_mir` verifies every module it builds; `test_mir` rejects 20
  malformed variants.

Malformed input is reported with a diagnostic and a nonzero exit. Conditions
only a compiler bug can produce are internal invariants (`invariant.h`,
`OPTIFINE_INVARIANT`), checked in every build type -- unlike `assert`, which
`NDEBUG` removes -- because several guard against wrong assembly: operand
truncation, a loop trip count the 8-bit counter cannot hold, a branch beyond
RJMP's reach, a register-pair alignment.

## 4. Analyses and candidates

- **SRAM layout** (`codegen/sram_layout.c`): one contiguous slot per op
  tensor from `0x0200` upwards, then a 181-byte DSP scratch arena (reserved in
  every layout, though only DSP ops use it), then, in periodic programs, the
  four scheduler bytes. This is the backend's address-assignment strategy for
  workload tensors.
- **Reuse analysis** (`codegen/reuse_analysis.c`): marks an op output that a
  single consumer reads repeatedly -- in these graphs exactly a MatMul's
  activation input, read once per output channel. It is not a register
  allocator. Its one reader is the cached-input MatMul candidate.
- **Candidates** (`codegen/candidates.c`, `select.c`): with `--optimized`,
  MatMul gets a second, equivalent candidate that loads its activation into a
  fixed pool of 21 registers once instead of reloading it for every output
  channel; `select_min_energy` keeps the cheaper. Every other op, ML or DSP,
  has exactly one candidate. There is no locality or scheduling pass.

## 5. Lowering

`codegen/hir_to_mir.c` translates the verified HIR into a MIR module:

- every op's output tensor becomes a `MIR_MEM_TENSOR` object, the scratch
  arena a `MIR_MEM_SCRATCH` object, and the FFT twiddle table a
  `MIR_MEM_CONST` object labelled `.Ltw`;
- function `initialize` stores the embedded input and every constant as MIR
  byte stores; function `infer` copies the Output tensor with MIR loads and
  stores;
- every other op is a **workload kernel**, a hand-scheduled AVR sequence
  produced by the lowering files and entered into MIR as a `MIR_TARGET`
  region that declares the objects it reads and writes:

  | file | ops |
  |---|---|
  | `lower_ml.c` | MatMul, Add, Relu |
  | `lower_requantize.c` | Requantize, and `lower_verify_demo_forward_pass` |
  | `lower_dsp.c` | Q15 and pointer primitives, Window, BitReverse |
  | `lower_fft.c` | FFT stages, the twiddle values |
  | `lower_magnitude.c`, `lower_peak.c` | Magnitude, PeakExtract |
  | `lower.c` | `lower_op` dispatch; Input/Const/Output delegate to the MIR path |

  `lower_internal.h` is their private interface; `lower.h` is the public one.

Register use inside a kernel follows the fixed contracts in
`codegen/registers.h`: r0/r1 multiply result, r2 a permanent zero, r3-r8
Requantize's 48-bit product, r16/r17 MAC operands, r18-r21 the 32-bit
accumulator, r22-r25 scratch, and the DSP layout of r3-r31 documented there.
No value stays in a register across an op boundary.

Requantize uses a 16-bit fixed-point multiplier derived with `frexp`. MinMax
per-tensor quantization cannot bound arbitrary inputs, so instead of a
worst-case proof the compiler evaluates the real forward pass for the one
embedded input (`lower_verify_demo_forward_pass`) and refuses to compile if a
Requantize output would leave int8; it never saturates at run time.

## 6. MIR

`include/optifine/mir.h` is the generic, target-independent representation:

- a **module** of memory objects and functions;
- a **function** of typed virtual registers (I8, I16, I32, PTR), parameters,
  a return type and basic blocks, block 0 the entry;
- a **block** of instructions -- const, copy, add, sub, mul, and, or, xor,
  compare (eq, ne, ult, slt), zext, sext, trunc, load, store, addr, ptr_add,
  target -- ending in exactly one terminator: `br`, `cbr` or `ret`;
- **memory objects** with a kind -- global, stack (owned by a function),
  scratch, constant (with an initializer), tensor -- and a size, but no
  address.

Values are virtual registers, not SSA: a value may be assigned in several
places (a loop counter is), and `mir_verify` checks every use is defined on
every path. SSA, phis and dominance can be added by the work that first needs
them; nothing in the representation prevents it. Workload operators are
absent: there is no MatMul or FFT opcode.

## 7. AVR machine representation and backend

`AvrInstr` / `InstrBuf` (`codegen/instr_buf.h`) is the AVR machine
representation: opcodes with symbolic operands, labels, program-memory data
words, and counted-loop regions (`LoopRegion`) that exist for exact pricing.
A `Candidate` is a priced machine sequence. There is no other AVR layer.

The MIR backend (`codegen/avr_mir.c`) takes any verified module:

- **layout** assigns SRAM addresses to objects and value slots, or keeps
  addresses a caller fixed -- the workload path fixes tensors at their
  `sram_layout` addresses and refuses any value that would need more SRAM;
- **selection** turns each instruction into AVR code with fixed scratch
  registers: unfolded values live in SRAM slots, and a load consumed only by
  the next store is folded into an `lds`/`sts` pair. Blocks get labels
  `.Lf<fn>b<blk>`; `cbr` is a compare against r2 and a one-word `breq` hop
  over `rjmp`s, so no conditional branch needs long reach; `ret` of an entry
  function falls through (there is no call or calling convention yet). A
  `MIR_TARGET` region for "avr" is emitted as its Candidate. Code is grouped
  into segments by the HIR op it came from, which keeps per-op cost
  accounting;
- **constants** are emitted as labels and `.dw` words after the code.

Not supported yet, and refused with a diagnostic: multiplication wider than
16 bits, and the address of a program-memory constant (a pointer into flash
needs an address-space-qualified pointer type). `test_avr_mir` runs every
generic test program -- arithmetic, 8- and 16-bit multiply, compares, a
diamond, loads and stores including program memory, a pointer-walking counted
loop -- on the AVR test interpreter and compares it with the reference MIR
interpreter (`tests/mir_interp.c`).

## 8. Cost and timing model

Cycles and energy are accumulated separately (`instrbuf_price`):

- **cycles** come from the AVR timing table `kAvrCycles`
  (`codegen/cost_category.c`, AVR Instruction Set Manual), a property of the
  target;
- **energy** comes from `cost_table.toml` categories (`SOURCES.md` cites each
  entry), plus the active-mode per-cycle energy for the opcodes the table
  cannot express (`lpm`, `rjmp`, `break`).

No available source supports different energy for different instruction
types within Active mode, so every table entry is `cycles x 2.8375 nJ`, a
per-cycle constant calibrated to Avrora's ATmega128 model (3.0 V, 7.5667 mA,
8 MHz), and energy is proportional to cycles. Neither is computed from the
other: `test_pricing` prices both workloads under a deliberately
non-proportional table and checks every op's cycle count is unchanged.

Pricing is per **executed** instruction. Straight-line code costs each
instruction once. Inside a counted loop an instruction costs its cycles times
the product of the enclosing trip counts, and the closing branch is priced
exactly: taken `trip - 1` times, falling through once. Loops close as
`dec / brne head` within BRNE's reach, or `dec / breq exit / rjmp head`
beyond it. Trip counts are compile-time constants and no data-dependent
branch exists in the workloads (DSP decisions are masks), so the prediction
is exact. For generic MIR code with data-dependent branches, a segment's
price is the cost of executing each emitted instruction once, which is not a
path cost.

What each printed total covers:

- **ML programs** report initialization (`clr r2`, input, constants) plus the
  inference body. The final `break` is written by the program epilogue and
  not priced, so Avrora's count is one higher (6,130 against 6,129 naive).
- **The DSP program** prices its `break` as a termination region, so
  initialization + body + termination (769 + 147,565 + 1 = 148,335) is the
  whole executed program and equals Avrora's count.
- **Periodic programs** report the initialization and per-inference compute
  cost only. The wrapper's timer setup, wait loops, interrupt handler and
  `break` are not priced by the compiler; the experiments take whole-program
  cycles and energy from Avrora.

## 9. Emission

`EmitUnit` (`emit.h`, `emit.c`) is one assembly file's label namespace.
Candidate-local loop labels (`.Ldsp<n>`, `.Lex<n>`) are renumbered past those
already emitted; every other label (MIR block labels, `.Ltw`) is global to the
unit and refused if defined twice. Programs are assembled with
`-nostartfiles`, so there is no crt0: constants are stored by code, and
program-memory data is placed after the code where control cannot reach it.

## 10. Periodic runtime wrapper

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
- The interrupt handler saves r16 and SREG and sets the tick, plus an
  `overrun` flag if a tick arrives while a body is still running. Four
  scheduler bytes (tick, running, overrun, completed count) follow the tensor
  layout; `; META` lines in the assembly name their addresses.
- The compiler prints the predicted initialization and per-body cycles, which
  the experiment scripts use for a static deadline check: a prescaler whose
  period is not longer than the body is rejected before any energy is
  compared.

For `--dsp`, the wrapper copies the output tensor at its real size (16 bytes,
`FIXED_Q15[8]`) and places the twiddle table after the interrupt handler.

## 11. Simulation and evidence tooling

`sim/run_*.py` drive the compiler, `avr-gcc` and Avrora and capture raw
output into `sim/fixtures/`, each directory with a manifest of source commit,
commands, tool versions and SHA-256 hashes. `sim/tests` re-derive every
reported figure from those raw files without running any tool. CI builds the
compiler with GCC and Clang (warnings as errors), runs the C tests under
Debug, AddressSanitizer + UndefinedBehaviorSanitizer and Release, regenerates
the retained ML and DSP programs and compares them byte for byte, and runs
the evidence tests. Avrora is not run in CI.

## DSP specifics

Fully unrolled, the DSP pipeline would need about 497 KB against the
ATmega128's 128 KiB of flash, so its repeated structures are counted loops:

- **Window:** 64 unrolled Q15 products with compile-time coefficients.
- **BitReverse:** the 6-bit permutation into a complex buffer, unrolled.
- **FFT:** radix-2 decimation-in-time, forward, 1/2 scaling per stage. Stage 0
  is one loop of 32 butterflies; stages 1-5 are a block loop around a
  butterfly loop.
- **Magnitude:** exact `floor(sqrt(re^2 + im^2))` per bin, a 32-bit unsigned
  square sum and a fixed 16-iteration bit-by-bit integer square root.
- **PeakExtract:** `FIXED_Q15[64] -> FIXED_Q15[8]`: the eight largest values,
  compared unsigned, in descending order, ties going to the lower source
  index, each bin chosen at most once (through an op-local `selected[64]`
  table). The output carries values only; bin indices are internal and not
  reported.

Only loop control branches; every data decision is a mask, so the cycle count
does not depend on the input samples.

**Scratch arena.** The DSP ops share the 181-byte arena as an **overlay**:
ops run once each, in sequence, so any op may use any cell provided it writes
the cell before reading it, and results cross op boundaries only through
graph tensors. The arena's extent is 181 bytes because the FFT keeps its
block counter at offset 180 (its butterfly temporaries use 0-25). The most
any single op touches is PeakExtract's 64-byte `selected[]`; per-op use is
not additive. `test_asm_unit` checks the write-before-read rule over every DSP
op's accesses, and the pipeline tests repeat every run under different SRAM
poison.

**Twiddle table.** The FFT's 32 unique Q15 twiddle factors (128 bytes) are a
MIR constant object emitted into program memory after the program's only
`break` (or, in the periodic build, after the interrupt handler), below
address 0x10000, so plain `lpm r, Z+` reads them and no `ELPM`/`RAMPZ` is
needed.

## Planned adapter boundary

The next frontend is an EDG IL adapter for C:

```
EDG IL --> EDG-to-MIR adapter --> MIR --> mir_verify --> AVR backend --> EmitUnit
```

It belongs at the MIR boundary, not the workload HIR. `IrGraph` has no
control flow, no scalar values and no addressable memory, and its operators
are workload kernels; lowering C into it would mean inventing a "kernel" for
every statement and losing the CFG. The adapter instead builds MIR functions,
blocks and memory objects directly (`mir.h` states the contract): EDG node
and type objects stay inside the adapter, nothing below MIR may include EDG
headers, and constructs MIR does not model are rejected, not approximated.

No adapter code exists in this repository.

### What the adapter work still needs below MIR

- **Calls.** MIR has no call instruction and the backend no calling
  convention or stack frames; entry functions are inlined by the wrapper and
  `ret` falls through.
- **Register allocation.** Unfolded values live in SRAM slots, reloaded per
  instruction. A real allocator belongs in the AVR backend.
- **Address spaces.** C `const` data in flash needs a pointer type that knows
  it points into program memory; MIR's PTR is SRAM-only, and the backend
  refuses the address of a constant object.
- **Wider arithmetic.** Multiplication above 16 bits, division, shifts.
- **Long branches.** Block branches use `rjmp` (+/-2K words); larger
  functions would need `jmp`.
- **Path-sensitive cost.** Generic code is priced per emitted instruction;
  costing code with data-dependent branches needs path or profile
  information.

### Remaining coupling between the workloads and the AVR target

The backend no longer needs workload operators as its input: both workloads
reach it as MIR. What remains workload-specific sits above MIR, in
`hir_to_mir` and the kernel files:

- Of the 13 ops in each graph, Input, Const and Output (3 kinds) are generic
  MIR. The kernels -- MatMul, Add, Relu, Requantize and all five DSP
  operators -- are `MIR_TARGET` regions holding pre-selected AVR code. MIR can
  express them, but re-deriving their exact instruction sequences through the
  generic selector would change the canonical assembly, so they stay as
  target regions.
- The DSP counted loops live only inside those regions, described by
  `InstrBuf` loop regions rather than MIR blocks. MIR can express counted
  loops (`test_avr_mir`'s loop); the workload kernels do not use that yet.
- The candidate machinery (`candidates.c`, `select.c`) chooses among AVR
  sequences per HIR op, above MIR.
- Workload tensor addresses come from `sram_layout`, a workload-specific
  allocation strategy the backend accepts as fixed addresses.
