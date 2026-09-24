# OptiFine

OptiFine is a research prototype of an energy-aware compiler backend for
resource-constrained AVR microcontrollers. It compiles two structurally
different workloads -- a small INT8 neural-network classifier (from ONNX) and a
fixed 64-point Q15 DSP pipeline -- to ATmega128 assembly, prices every emitted
instruction with a sourced energy model, and evaluates the result in the Avrora
cycle-accurate simulator.

**Status: research prototype.** Both workloads compile end to end and are
evaluated in simulation, with the compiler's cycle prediction matching Avrora
exactly for every retained program. All energy figures are **simulated**
(Avrora's ATmega128 power model); no physical board has been measured.
Hardware validation is future work.

## What OptiFine does

- **Compiles an ONNX classifier and a Q15 DSP pipeline to AVR** through one
  typed IR, one lowering and emission backend, and one cost model.
- **Active-mode optimization:** chooses between alternative instruction
  sequences by predicted energy, with next-use-informed register caching.
- **Periodic power-aware scheduling:** wraps a compute body in a timer-driven
  periodic program that spends the rest of each period either busy-waiting or
  in AVR Power-save mode.
- **Predicts cycles exactly:** straight-line code and statically bounded loops
  are priced per executed instruction; for every retained program the
  prediction equals Avrora's cycle count.
- **Keeps its evidence:** raw simulator output is retained in the repository,
  and every reported figure is regenerated from it by script.

## Key results

All figures are Avrora simulations of an ATmega128 at 8 MHz. They are derived
from the retained raw output by `python3 sim/report_results.py`; the full
tables and their method are in [REPORT.md](REPORT.md).

| Result | Value |
|---|---|
| ML classifier, cycles (naive -> optimized) | 6,130 -> 6,018 (-1.83%) |
| ML classifier, simulated energy (naive -> optimized) | 17,393.95 -> 17,076.15 nJ |
| DSP pipeline, cycles (predicted = Avrora) | 148,335 (18.542 ms at 8 MHz) |
| DSP pipeline, simulated active energy | 420,902.42 nJ (420.90 µJ) |
| DSP pipeline, linked flash / SRAM | 12,800 B / 2,517 B |
| Periodic scheduling, ML, Power-save vs busy-wait, per period | 33.31% / 82.10% / 96.33% saved at prescaler 32 / 128 / 1024 |
| Periodic scheduling, DSP, prescaler 1024, per period | 743,836.88 nJ busy-wait (period-normalized) vs 424,239.11 nJ Power-save: 42.97% saved |

The ML and DSP rows are different computations; they show what the backend can
handle, not a contest between the workloads. The periodic-scheduling savings
depend on how much of each period is idle (the ML body fills about 2% of the
1024-prescaler period, the DSP body about 56%), so they are separate,
workload-dependent results and are not averaged. The DSP busy-wait figure is
normalized from the simulated 262,141-cycle increment to the exact
262,144-cycle period (see [Periodic power-aware scheduling](#periodic-power-aware-scheduling)).

## Architecture

```
ONNX classifier --(ingest.c)--\
                               >-- typed IR --> lowering / candidates --> cost-based --> AVR assembly --> avr-gcc --> Avrora
DSP pipeline ---(dsp_build.c)-/                 (lower.c, candidates.c)    selection        (emit.c)                 (energy
                                                                           (select.c)                                monitor)
```

- **IR** (`compiler/src/ir.c`): a typed op graph shared by both workloads.
- **Ingestion** (`compiler/src/ingest.c`): a hand-written protobuf reader for
  the ONNX operators the classifier uses (MatMul, Add, Relu, Requantize).
- **DSP builder** (`compiler/src/dsp_build.c`): constructs the fixed DSP graph
  directly.
- **Lowering** (`compiler/src/codegen/lower.c`): one correct instruction
  sequence per op; DSP ops use statically bounded counted loops.
- **Candidates and selection** (`candidates.c`, `select.c`): a second,
  register-cached sequence for MatMul, chosen by predicted energy.
- **Pricing** (`instr_buf.c`, `cost_table.toml`): every instruction's cost is
  cycles times a per-cycle constant calibrated to Avrora's own ATmega128 model
  (see [SOURCES.md](SOURCES.md)).
- **Emission** (`emit.c`, `program.c`, `periodic.c`): whole programs, one label
  namespace per assembly file, optionally inside the periodic wrapper.

`compiler/src/locality.c` is a no-op placeholder; no result depends on it.

## Supported workloads

### ML classifier

A 16 -> 8 -> 4 INT8 classifier (`models/tiny_classifier.onnx`, exported by
`export/export_model.py`), compiled naive (one sequence per op) or optimized
(candidate diversity for MatMul plus next-use input caching). Its output is
checked bit-exactly against an independent golden reference.

### 64-point Q15 DSP pipeline

```
Input -> Hamming window -> bit reversal -> 64-point radix-2 FFT -> exact magnitude -> top-8 selection -> Output
```

- **FFT:** six decimation-in-time stages, forward transform, Q15 arithmetic,
  1/2 scaling per stage (1/64 overall), twiddles from a 32-entry program-memory
  table.
- **Magnitude:** exact `floor(sqrt(re^2 + im^2))` per bin, via a 32-bit
  unsigned square sum and a fixed 16-iteration integer square root.
- **Top-8 selection:** the eight largest magnitudes, largest first, compared
  unsigned. Only the values are output, not bin positions.

The whole compiled program matches an independent integer host reference
exactly at every intermediate buffer, over 18 test signals.

## Active-mode optimization

With the processor always active, energy-optimal and cycle-optimal code
coincide under Avrora's power model: no available source supports pricing AVR
instructions differently by type within Active mode, and equal-cycle sequences
simulate to identical energy. The optimizer therefore saves exactly what it
saves in cycles -- 112 cycles, 1.83%, on the classifier -- and that equivalence
is one of the project's findings. There is deliberately no separate
speed-versus-energy switch.

## Periodic power-aware scheduling

A periodic program runs the compute body once per Timer0 period and spends the
rest of the period either **busy-waiting** (polling in Active mode) or in
**Power-save** mode, woken by the timer. The body is identical in both builds;
only the waiting differs. Figures are steady-state differences between 5 and 4
periods, which cancel one-time setup.

Because an Active cycle costs about 61 times a Power-save cycle in Avrora's
model, the saving tracks the idle fraction of the period, not the compute code:

- **ML classifier:** its short body leaves most of each period idle, reaching
  96.33% saved at prescaler 1024.
- **DSP pipeline:** the 147,565-cycle body fits only the 262,144-cycle period
  of prescaler 1024 (prescalers 8, 32 and 128 are compute-bound, with no idle
  window). There it fills about 56% of the period, and Power-save saves
  42.97%.

**Busy-wait normalization (DSP only).** The busy-wait loop polls in an 8-cycle
loop, so each wake is detected up to 8 cycles late; with the DSP body the
detection phase does not settle, and the raw busy-wait increment Avrora reports
is 262,141 cycles against the exact 262,144 of the Power-save increment. The DSP
busy-wait energy is scaled by 262,144 / 262,141 to one exact period (raw:
743,828.36 nJ; normalized: 743,836.88 nJ). The normalized value equals the ML
sweep's directly simulated busy-wait energy at the same period, as it should:
busy-waiting spends the whole period in Active mode whatever the workload.

## Build

Requirements:

| Requirement | Used for |
|---|---|
| CMake >= 3.16 and a C11 compiler | the compiler and its tests (Linux, macOS, Windows) |
| Python 3 | result generation and reproduction scripts; `pytest` for their tests |
| AVR toolchain (`avr-gcc`, `avr-binutils`, `avr-libc`) | assembling emitted programs |
| JDK 8 and Avrora Beta 1.7.115 | simulation |
| PyTorch + ONNX | only to re-export the demo model |

```bash
cmake -S compiler -B compiler/build
cmake --build compiler/build
ctest --test-dir compiler/build
```

On Linux, `./tools/setup_linux.sh` downloads a pinned JDK 8 and
`avrora-beta-1.7.115.jar` into `tools/` (neither is committed). Avrora 1.7.115
does not run on newer JDKs, so the scripts look for `tools/jdk8*` first; when
calling `sim/run_avrora.sh` directly, point `JAVA_HOME` at it. The AVR toolchain
comes from the system package manager:

```bash
sudo pacman -S avr-gcc avr-binutils avr-libc      # Arch
sudo apt install gcc-avr avr-libc binutils-avr     # Debian/Ubuntu
sudo dnf install avr-gcc avr-libc avr-binutils     # Fedora
```

## Usage

### ML classifier

```bash
compiler/build/optifine models/tiny_classifier.onnx --cost-table cost_table.toml \
  --input models/tiny_classifier_golden_input.txt --out build/naive.s
compiler/build/optifine models/tiny_classifier.onnx --cost-table cost_table.toml \
  --input models/tiny_classifier_golden_input.txt --optimized --out build/optimized.s
```

### DSP pipeline

```bash
compiler/build/optifine --dsp --cost-table cost_table.toml --out build/dsp.s                       # default input
compiler/build/optifine --dsp --cost-table cost_table.toml --input samples.txt --out build/dsp.s   # your samples
```

The input file holds 64 decimal int16 (Q15) samples separated by whitespace;
`#` starts a comment. The default, `models/dsp_demo_input.txt`, is two tones on
bins 5 and 12. Samples are embedded in the program at compile time. `--dsp`
takes no model path and does not accept `--optimized` (each DSP op has one
lowering).

### Periodic scheduling

Either workload can be wrapped:

```bash
compiler/build/optifine --dsp --cost-table cost_table.toml --out build/dsp_ps.S \
  --periodic-count 4 --wait-policy powersave --timer-prescaler 1024
```

`--periodic-count` sets how many periods run, `--wait-policy` is `active`
(busy-wait) or `powersave`, and `--timer-prescaler` (8, 32, 128 or 1024) sets
the period, 256 x prescaler cycles.

### Simulate

```bash
JAVA_HOME=$PWD/tools/jdk8u504-b01 bash sim/run_avrora.sh build/dsp.s
# Simulated time: 148335 cycles
```

`sim/run_avrora.sh <in.s> [mcu] [avrora.jar]` assembles and runs a program;
the MCU defaults to `atmega128`, the only calibrated target (Avrora 1.7.115 has
no ATmega328P or ATmega2560 model).

### Compiler options

| Flag | Description |
|---|---|
| `--cost-table` | path to `cost_table.toml` |
| `--out` | output assembly file |
| `--input` | compile-time input (ML: int8 values; `--dsp`: 64 int16 samples) |
| `--optimized` | ML only: candidate diversity and next-use input caching |
| `--dsp` | compile the DSP pipeline instead of an ONNX model |
| `--periodic-count`, `--wait-policy`, `--timer-prescaler` | periodic wrapper, all three together |

## Reproducing the results

The retained simulator evidence lives under `sim/fixtures/`:

| Directory | Contents |
|---|---|
| `sim/fixtures/classifier_*` | active-mode ML runs (naive and optimized) |
| `sim/fixtures/phase_b/` | periodic-scheduling sweep over the ML classifier |
| `sim/fixtures/dsp/` | the complete DSP program |
| `sim/fixtures/phase_b_dsp/` | periodic-scheduling sweep over the DSP pipeline |

Each experiment directory keeps the generated assembly, the raw Avrora reports
and a manifest of hashes, commands and tool versions. (`phase_b` in a path is
the repository's internal name for the periodic-scheduling experiments.)

```bash
ctest --test-dir compiler/build                  # compiler, interpreter and pipeline tests
python3 -m pytest sim/tests                      # reproduction and result-generation tests
python3 sim/report_results.py                    # regenerate every result table from raw output
python3 sim/run_dsp.py check                     # verify the retained DSP program; runs no tool
python3 sim/run_phase_b.py reproduce-retained    # verify the ML periodic-scheduling sweep; runs no tool
python3 sim/run_phase_b_dsp.py                   # verify the DSP periodic-scheduling sweep; runs no tool
```

Fresh runs with your own toolchain write to a new directory and never
overwrite retained evidence:

```bash
JAVA_HOME=$PWD/tools/jdk8u504-b01 python3 sim/run_dsp.py capture --output-dir build/dsp_new
python3 sim/run_dsp.py compare sim/fixtures/dsp build/dsp_new
python3 sim/run_phase_b.py run-new --output-dir build/ml_periodic_new
python3 sim/run_phase_b_dsp.py run-new --output-dir build/dsp_periodic_new
```

## Repository structure

```
compiler/           C11 compiler (CMake)
  include/optifine/   public headers (IR, cost model, codegen)
  src/                ingestion, DSP builder, CLI (main.c)
  src/codegen/        lowering, candidates, selection, pricing, emission, periodic wrapper
  tests/              CTest suite and a test-only AVR interpreter
sim/                simulation wrapper, result generation, reproduction scripts and their tests
  fixtures/           retained raw simulator evidence
models/             demo classifier, its golden input/output, DSP demo input/output
export/             PyTorch -> ONNX export of the demo classifier
tools/              toolchain setup script (downloads JDK 8 and Avrora; not committed)
docs/, documents/   design notes, literature survey and historical development plans
cost_table.toml     per-instruction energy model; SOURCES.md cites every entry
REPORT.md           method, results and limitations
```

## Validation

- **Correctness:** compiled programs run in a test-only AVR interpreter and are
  compared exactly with independent references -- the classifier against its
  golden output, the DSP pipeline against an integer host implementation at
  every intermediate buffer. A lower-energy sequence that computes a different
  answer is a failed test.
- **Cycle model:** predicted cycles equal the interpreter's count in the test
  suite and Avrora's count for every retained program.
- **Evidence:** retained raw output is hash-pinned by manifests, and the
  reproduction scripts re-derive the published tables from it.

## Limitations

- **Simulation only.** All energy figures come from Avrora's ATmega128 power
  model; no physical board has been measured.
- **Uniform Active-mode cost model.** Energy is cycles times one constant,
  because no available source supports per-instruction-type pricing.
- **Fixed, compile-time inputs.** Each program embeds one input; runtime input
  from a sensor or host is not implemented.
- **Narrow workload scope.** One classifier and one fixed DSP pipeline:
  arbitrary ONNX graphs, arbitrary FFT lengths or DSP graphs, peak-position
  output, and a single program mixing ML and DSP are not supported.
- **Scoped register allocation.** Register caching covers MatMul's activation
  input only; there is no general cross-op allocator.
- **One accepted DSP periodic-scheduling point**, with the busy-wait
  normalization described above.

## Research status and citation

OptiFine is a research prototype, not production software. Its interfaces may
change. If you use it, please cite the repository; a citation file will be
added with the first public release. The design rationale and related work are
in [REPORT.md](REPORT.md) and
[documents/LITERATURE_SURVEY.md](documents/LITERATURE_SURVEY.md).

Contributions: see [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT, see [LICENSE](LICENSE).
