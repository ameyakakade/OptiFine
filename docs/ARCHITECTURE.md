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
EDG IL --> EDG-to-MIR adapter --> MIR --> AVR backend (avr_mir_build_program: verifies, lays out, selects) --> .s
```

The compiler is ISO C11 with extensions off (`CMAKE_C_EXTENSIONS OFF`, so
`-std=c11`), built with `-Wall -Wextra -Wpedantic`, under `compiler/` with no
dependencies beyond libc and libm. It builds as two libraries, which the
tests link as well:

- **`optifine_backend`**: MIR and `mir_verify`, AVR selection from MIR, the
  AVR instruction and pricing layer, the cost model and assembly emission. It
  knows nothing of the workload graph: no backend header includes `ir.h` or
  any workload header, and no backend object needs a workload symbol.
  `test_backend_link`, `test_mir`, `test_avr_mir`, `test_avr_mir_branches`,
  `test_mir_standalone` and `test_emit_unit` link it alone, so a dependency
  creeping back in fails the build.
- **`optifine_core`**: the frontends, the workload graph and its lowering,
  `hir_to_mir` and the program wrappers, on top of the backend.

The command-line driver (`src/main.c`) links `optifine_core`.

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
  entry. Every instruction and terminator must be in canonical form: `mir.h`
  tables which fields each opcode and terminator kind uses, and every other
  field must be empty, so a stray id in an unused field is rejected rather
  than read. The definedness dataflow reads only the fields an opcode uses
  and bounds-checks every id it indexes with, so malformed input gets a
  diagnostic, never an out-of-bounds access. `test_mir` rejects 45 malformed
  variants, including the four that once made the verifier itself read or
  write out of bounds.
- **The backend verifies for itself.** Every `avr_mir` entry point that takes
  a module -- layout, selection, constants, `avr_mir_build_program` -- runs
  `mir_verify` first and refuses invalid MIR, so skipping the caller's own
  verification cannot get malformed MIR selected. Nothing records "already
  verified": a module changed between calls is verified again (verification
  is linear, and a workload program verifies its small modules a few times).
  Selection also refuses a layout made for a different or since-changed
  module, or one that left an object or value without an address.
  `hir_to_mir` additionally verifies every module it builds, to report
  translation errors where they arise.

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

`AvrInstr` and `Candidate` (`codegen/avr_instr.h`, a priced machine
sequence) and `InstrBuf` (`codegen/instr_buf.h`) are the AVR machine
representation: opcodes with symbolic operands, labels, program-memory data
words, and counted-loop regions (`LoopRegion`) that exist for exact pricing.
There is no other AVR layer. The workload candidate machinery
(`candidates.h`) uses these types; it does not own them.

The MIR backend (`codegen/avr_mir.c`) takes any verified module:

- **layout** assigns SRAM addresses to objects and value slots, or keeps
  addresses a caller fixed -- the workload path fixes tensors at their
  `sram_layout` addresses and refuses any value that would need more SRAM;
- **selection** turns each instruction into AVR code with fixed scratch
  registers: unfolded values live in SRAM slots, and a load consumed only by
  the next store is folded into an `lds`/`sts` pair. Blocks get labels
  `.Lf<fn>b<blk>`; `cbr` is a compare against r2 and a `breq` that hops over
  the taken edge's jump, so no conditional branch needs long reach; `ret` of
  an entry function falls through (there is no call or calling convention
  yet). A `MIR_TARGET` region for "avr" is emitted as its Candidate. Code is
  grouped into segments by the HIR op it came from, which keeps per-op cost
  accounting;
- **branch reach**: each block jump is an `rjmp` (+/-2K words) unless the
  function's layout in flash words -- `lds`, `sts` and `jmp` are two words,
  labels none -- shows it cannot reach, in which case it becomes a `jmp`
  (two words, 3 cycles). Selection repeats until no jump changes, so jumps
  that push each other out of reach are handled, and the assembler never sees
  an out-of-range branch. `test_avr_mir_branches` checks short and long
  forward, backward and conditional branches, and interacting ones, against
  an independent reach check and by execution; the workloads never emit a
  `jmp`;
- **constants** are emitted as labels and `.dw` words after the code.

Labels a function's code defines (`.Lf<fn>b<k>`, hops, the exit) span several
segments, so the assembly unit (`EmitUnit`, `emit.h`) records them by name to
refuse a second definition; the record grows with the program (it was a fixed
256 entries, which a 201-block function exhausted). Candidate-local loop
labels (`.Ldsp<n>`, `.Lex<n>`) are renumbered per candidate instead.
`test_emit_unit` covers both.

**Standalone programs and the entry convention.** A frontend compiles MIR
with `avr_mir_build_program` (verify, lay out from 0x0200, select one entry
function with the `clr r2` prologue, price, collect constants) and
`avr_mir_emit_program`. The program follows a research entry convention for
single-function programs, pending real call support -- it is not an AVR C
ABI: `_start` runs the entry function inlined and halts on `break`;
parameter i lives at the absolute SRAM address of symbol `optifine_param_<i>`
and the return value at `optifine_return`, little-endian in their MIR widths;
whoever runs the program writes the parameters first (SRAM is not cleared).
There are no calls, no recursion, no stack frame (stack objects are placed
statically) and no initialized or zeroed globals at reset. `avr_mir.h` has
the exact contract; `test_mir_standalone` drives a C-shaped function through
it.

Not supported yet, and refused with a diagnostic: multiplication wider than
16 bits, and the address of a program-memory constant (a pointer into flash
needs an address-space-qualified pointer type). `test_avr_mir` runs every
generic test program -- arithmetic, 8- and 16-bit multiply, compares, a
diamond, loads and stores including program memory, a pointer-walking counted
loop -- on the AVR test interpreter and compares it with the reference MIR
interpreter (`tests/mir_interp.c`). `test_mir_standalone` compiles

```c
int f(int x) { int y = x + 3; if (y > 10) y = y - 2; else y = y + 4;
               int sum = 0; for (int i = 0; i < 5; ++i) sum += y; return sum; }
```

(with 16-bit `int`), built directly with the MIR API in seven blocks -- once
with locals as values, once as stack objects -- through
`avr_mir_build_program`, and checks it on both arms and through the loop
against MIR's wrapping 16-bit semantics for every input, and against host C
only for inputs whose computation stays in the int16 range (signed overflow
is undefined in C, so a wrapped result is no claim about C).

## 8. Cost and timing model

Cycles and energy are accumulated separately (`instrbuf_price`):

- **cycles** come from the AVR timing table `kAvrCycles`
  (`codegen/cost_category.c`, AVR Instruction Set Manual), a property of the
  target;
- **energy** comes from `cost_table.toml` categories (`SOURCES.md` cites each
  entry), plus the active-mode per-cycle energy for the opcodes the table
  cannot express (`lpm`, `rjmp`, `jmp`, `break`).

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
is exact. For generic MIR, a single-block function executes each emitted
instruction once, so its price is its execution cost. A function with
branches gets only a **static** price -- each emitted instruction once,
whatever path runs -- which is not an execution estimate: the reference
function above prices at 134 cycles statically and executes in 328-329.
`AvrMirCode.straight_line` and `AvrMirStaticCost.exact` say which case
applies, and a caller may present the figures as predicted cycles or energy
only when they are set. What a frontend can report today is code size,
instruction count and those static figures. Path-sensitive costing is future
work, and no energy-aware choice between codings of generic MIR exists (the
candidate selection works on workload kernels, above MIR). The workload
programs are single-block functions whose loops live inside priced target
regions; `program.c` checks that invariantly, so their printed totals stay
execution costs.

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

No adapter code exists in this repository. The adapter needs only
`optifine_backend`: it builds a `MirModule`, then calls
`avr_mir_build_program` and `avr_mir_emit_program`, which verify the module
themselves.

### Supported before EDG, and not

Supported, verified and tested on the AVR interpreter (and, for the reference
function and the long-branch programs, checked once in Avrora outside CI):
one entry function with parameters and a return value; 8-, 16- and 32-bit
integer constants, add, sub, and, or, xor, and multiply up to 16 bits;
eq/ne/unsigned-less/signed-less compares; zero and sign extension and
truncation; any CFG of blocks with `br`/`cbr`/`ret`, loops and merges, at
any branch distance; stack and global objects with loads and stores;
`addr`/`ptr_add` pointers into SRAM; read-only constants in program memory
by direct access.

Not supported: everything in the next list, plus storing or comparing
pointer values and initialized globals.

### What the adapter work still needs below MIR

- **Calls.** MIR has no call instruction and the backend no calling
  convention or stack frames; the entry function is inlined, `ret` falls
  through, and parameters and the result use the research entry convention
  above.
- **Register allocation.** Unfolded values live in SRAM slots, reloaded per
  instruction. A real allocator belongs in the AVR backend.
- **Address spaces.** C `const` data in flash needs a pointer type that knows
  it points into program memory; MIR's PTR is SRAM-only, and the backend
  refuses the address of a constant object.
- **Wider arithmetic.** Multiplication above 16 bits, division, shifts.
- **Path-sensitive cost.** Code with branches gets only a static price (see
  "Cost and timing model"); costing it needs path or profile information.
- **Initialized and zeroed globals.** Programs start straight from reset with
  no `.data` copy or `.bss` clear; a frontend must store initial values
  explicitly, or the backend must gain startup code.

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
