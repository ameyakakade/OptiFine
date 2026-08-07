# OptiFine

![GitHub last commit](https://img.shields.io/github/last-commit/rugbedbugg/OptiFine?style=for-the-badge&labelColor=000000)
![GitHub repo size](https://img.shields.io/github/repo-size/rugbedbugg/OptiFine?style=for-the-badge&labelColor=000000)
![Stars](https://img.shields.io/github/stars/rugbedbugg/OptiFine?style=for-the-badge&labelColor=000000)

Compiler backend that takes IR from two sources - an INT8-quantized neural network (PyTorch → ONNX) and a fixed-size audio/DSP pipeline (64-point FFT, Q15 fixed-point) - and compiles both to AVR machine code, selecting between candidate instruction sequences by **estimated energy cost** instead of cycle count. Validated end-to-end via Avrora (cycle-accurate AVR simulator with built-in energy monitor). No physical hardware in Phase 1 (sim-only); sim-to-hardware correlation is deferred Phase 2 work.

## Status

**ML path: working end-to-end.** The compiler ingests the demo ONNX model,
lowers it to AVR, selects candidates by cost, allocates registers with
next-use information, and emits assembly that `avr-gcc` assembles and Avrora
simulates. Phase A (Active-mode code generation) and Phase B (compiler-emitted
sleep scheduling) both produce verified results; see `REPORT.md`, or regenerate
every figure from raw simulator output with `python3 sim/report_results.py`.

**DSP path: partial.** The 64-point Q15 pipeline's IR is complete and 4 of its
13 graph ops lower to AVR (`Input`, `Const`, `Window`, `Output`). `BitReverse`,
the six `FftButterfly` stages, `Magnitude` and `PeakExtract` are **not yet
lowered**, so the DSP pipeline does not compile end-to-end and no DSP energy
figure is claimed anywhere in this repository.

See `energy_aware_compiler_spec_v2.md` for the build spec and milestone order.

## Features

| Feature | Description |
|---------|-------------|
| Custom typed IR | Ingestion parses ONNX protobuf directly, no lexer/parser |
| Candidate generation | ≥2 equivalent AVR instruction sequences per IR op |
| Energy-cost selection | Via `cost_table.toml` (cited sources required) |
| Register allocation | Next-use information for AVR's limited register file |
| AVR assembly emission | `avr-gcc`/`avr-as` → ELF → Avrora simulation |
| Sleep scheduling | Periodic wrapper with busy-wait or Power-save waiting (Phase B) |
| End-to-end comparison | Same model compiled naive vs. optimized, both simulated |

`compiler/src/locality.c` exists but is a **no-op stub**; the memory-locality
pass described in the spec is not implemented, and no result in this repository
depends on it.

## Tech Stack

| Component | Details |
|-----------|---------|
| Model Export (`export/`) | Python + PyTorch + `torch.onnx.export` (existing tooling) |
| Compiler Core (`compiler/`) | C (C11, CMake) - hand-built pipeline: IR, ingestion, codegen (candidates, regalloc, select), cost model, locality, emit; unit tests via CMake/CTest |
| Simulation (`sim/`) | Avrora (Java) + `avr-gcc`/`avr-as`/`avr-objcopy` (AVR toolchain) |
| Cost Table (`cost_table.toml`) | Per-instruction energy costs, every entry sourced |

## Pipeline

```
Model Export        Custom IR         AVR Target      Energy-Cost         Emit AVR      Avrora         Comparison
(ONNX/TFLite)  -->  (typed op    -->  Selection   -->  Instruction   -->  Assembly  -->  Simulation --> & Report
 [existing]           graph)           [this proj]      Selection          [this proj]    [validation]
                       [this proj]                        [this proj,
                                                           centerpiece]
```

### Stages

1. **Export** (`export/export_model.py`): PyTorch `TinyClassifier` (MatMul, Add, Relu, Requantize) → INT8-quantized ONNX
2. **Ingest** (`compiler/src/ingest.c`): ONNX protobuf → custom IR (OpKind: Input, Const, MatMul, Add, Relu, Requantize, Output)
3. **Codegen** (`compiler/src/codegen/`):
   - `candidates.c`: Generate ≥2 equivalent AVR instruction sequences per IR op
   - `regalloc.c`: Next-use register allocation
   - `select.c`: Pick candidate with minimum `energy_nj` from cost table
4. **Locality** (`compiler/src/locality.c`): Reorder/restructure to maximize register reuse, reduce SRAM loads
5. **Emit** (`compiler/src/emit.c`): Output AVR assembly (`.s`)
6. **Simulate** (`sim/run_avrora.sh`): Assemble → ELF → Avrora energy monitor → parse report
7. **Compare**: Run both speed-optimized and energy-aware builds, diff energy numbers

## Install

### Prerequisites

| Requirement | Details |
|-------------|---------|
| CMake | ≥ 3.16 |
| C11 compiler | clang/MSVC/GCC |
| Python 3 + PyTorch | For `export/export_model.py` |
| AVR toolchain | `avr-gcc`, `avr-as`, `avr-objcopy` |
| Avrora | `avrora.jar` (requires JVM) |
| ONNX protobuf | `protoc` + `onnx.proto` (for ingestion) |

The compiler itself (`compiler/`) is host C11 with no AVR dependency --
`cmake`/a C compiler is all it needs to build and pass its CTest suite on
Linux, macOS, or Windows. The AVR toolchain and Avrora are only needed to
actually simulate emitted assembly (`sim/run_avrora.sh`,
`sim/run_phase_b.py`).

#### Linux toolchain setup

```bash
./tools/setup_linux.sh
```

Downloads a pinned JDK 8 (Avrora 1.7.115 crashes on JDK 9+, see
`sim/run_avrora.sh`) and the exact `avrora-beta-1.7.115.jar` this
project's energy constants are calibrated against (`SOURCES.md`) into
`tools/` -- no root needed for either. `avr-gcc`/`avr-libc`/`avr-binutils`
still need a real system package (Arch's prebuilt `avr-gcc` hardcodes its
linker lookup to `/usr/bin/avr-ld` in a way that ignores relocated
copies -- confirmed empirically, not assumed):

```bash
sudo pacman -S avr-gcc avr-binutils avr-libc      # Arch/Manjaro
sudo apt install gcc-avr avr-libc binutils-avr     # Debian/Ubuntu
sudo dnf install avr-gcc avr-libc avr-binutils     # Fedora
```

`sim/run_avrora.sh` and `sim/run_phase_b.py` both auto-discover `avr-gcc`
(PATH, or `$AVR_GCC`), the vendored JDK 8 (or `$JAVA8_BIN`/`$JAVA_HOME`),
and the vendored `avrora.jar` (or `$AVRORA_JAR`) in that order -- set the
env vars to override any of them.

### Build Compiler

```bash
cmake -S compiler -B compiler/build
cmake --build compiler/build
ctest --test-dir compiler/build
```

Output: `compiler/build/optifine.exe` (or `optifine` on Linux).

### Export Demo Model

```bash
cd export
python export_model.py --out ../models/tiny_classifier.onnx
```

### Compile Model (naive vs optimized)

```bash
# Naive baseline: one candidate per op, every value reloaded from SRAM
./compiler/build/optifine models/tiny_classifier.onnx \
  --cost-table cost_table.toml \
  --input models/tiny_classifier_golden_input.txt \
  --out build/naive.s

# Optimized: real candidate diversity + next-use input caching
./compiler/build/optifine models/tiny_classifier.onnx \
  --cost-table cost_table.toml \
  --input models/tiny_classifier_golden_input.txt \
  --optimized \
  --out build/optimized.s
```

There is deliberately no `--strategy speed|energy` flag. An earlier design
anticipated one, but the project's own sourcing pass found that no available
source supports differentiating AVR energy by instruction *type* within Active
mode, so every cost-table entry reduces to `cycles x a single constant` and a
"speed" strategy would select exactly what an "energy" strategy selects. That
collapse is a **result**, documented in `REPORT.md` and `SOURCES.md`, not an
unimplemented feature.

### Simulate & Compare

The simulation target is **ATmega128**. Avrora Beta 1.7.115 ships no
`atmega328p` or `atmega2560` MCU class, so ATmega128 is the closest supported
device and every energy constant in this project is calibrated to it.

```bash
./sim/run_avrora.sh build/naive.s atmega128
./sim/run_avrora.sh build/optimized.s atmega128
```

### Regenerate the result tables

Every figure in `REPORT.md` is derived from raw Avrora output rather than
transcribed. To recompute them:

```bash
python3 sim/report_results.py                 # Markdown to stdout
python3 sim/report_results.py --format csv
python3 sim/report_results.py --out-dir build/results
```

### Phase B: periodic sleep scheduling

```bash
# Revalidate the canonical retained experiment. Starts no tool, writes no file.
python3 sim/run_phase_b.py reproduce-retained

# Run a fresh experiment with the current toolchain, into a new directory.
python3 sim/run_phase_b.py run-new --output-dir build/phase_b_new

# Extend an existing count-4 experiment with the count-5 sweep.
python3 sim/run_phase_b.py supplemental --output-dir build/phase_b_new
```

`reproduce-retained` validates the retained assembly and Avrora reports
byte-for-byte and re-derives the result tables from them. It reports whether
this machine's toolchain matches the one that produced the canonical run, but
does not require it to, because it never invokes the toolchain. `run-new`
refuses a directory that already holds a manifest, and `supplemental` refuses
the canonical fixture directory, so neither can overwrite retained evidence.

## Commands

### Compiler (`optifine`)

```bash
optifine <model.onnx> --cost-table <cost_table.toml> --out <out.s> [--input <vec.txt>]
         [--optimized] [--periodic-count <n> --wait-policy active|powersave
          --timer-prescaler 8|32|128|1024]
```

| Flag | Description |
|------|-------------|
| `--cost-table` | Path to `cost_table.toml` (required) |
| `--out` | Output assembly file (default: `out.s`) |
| `--input` | Demo input vector, compile-time baked into the program |
| `--optimized` | Use real candidate diversity + next-use input caching (default: naive) |
| `--periodic-count` | Emit a periodic wrapper running the body N times (Phase B) |
| `--wait-policy` | `active` (busy-wait) or `powersave` (sleep between inferences) |
| `--timer-prescaler` | Timer0 divisor setting the wake period |

### Export Model

```bash
python export/export_model.py [--out <path>] [--in-features <n>]
```

| Flag | Default | Description |
|------|---------|-------------|
| `--out` | `../models/tiny_classifier.onnx` | Output ONNX path |
| `--in-features` | `16` | Input dimension |

### Simulate

```bash
./sim/run_avrora.sh <in.s> <platform> [avrora.jar path]
```

| Arg | Description |
|-----|-------------|
| `<in.s>` | Emitted AVR assembly |
| `<platform>` | `atmega328p` or `atmega2560` |
| `[avrora.jar]` | Path to Avrora JAR (default: `avrora.jar` in CWD) |

## Options / Configuration

### Cost Table (`cost_table.toml`)

Every entry has a cited source; **no `PLACEHOLDER` entries remain**. All 9
entries are recorded in `SOURCES.md` with their derivation.

```toml
[instructions]
ADD      = { energy_nj = 2.8375, source = "Avrora ATmega128 power model x 1 cycle" }
MUL      = { energy_nj = 5.675,  source = "Avrora ATmega128 power model x 2 cycles" }
LD_SRAM  = { energy_nj = 5.675,  source = "... x 2 cycles (LDS)" }
```

**The important finding, not a shortcut:** no source available to this project
(AVR datasheets, the instruction-level energy literature, or Avrora's own
monitor) supports differentiating energy by instruction *type* within Active
mode. Equal-cycle sequences were confirmed empirically to produce bit-identical
simulated energy. Every entry is therefore
`cycles(instruction) x per_cycle_energy_nj`, where the constant (2.8375125
nJ/cycle) is calibrated to Avrora's own ATmega128 model (3.0 V, 7.5667 mA
Active), **not** to a datasheet operating point. A datasheet-derived constant
used earlier overstated Avrora's simulated energy by ~3.75x. Full derivation
and citations in `SOURCES.md`.

### Sources (`SOURCES.md`)

`SOURCES.md` carries the derivation and citation behind every cost-table
constant, including how the per-cycle figure was recovered from Avrora's own
compiled power model and why the datasheet-derived alternative was rejected.
Relevant prior work and this project's positioning against it are in
`documents/LITERATURE_SURVEY.md`.

## Project Structure

```
OptiFine/
├── export/
│   └── export_model.py        # PyTorch -> ONNX (thin, existing tooling)
├── compiler/                  # C - the actual project
│   ├── include/optifine/
│   │   ├── cost_model.h
│   │   ├── emit.h
│   │   ├── ingest.h
│   │   ├── ir.h
│   │   ├── locality.h
│   │   └── codegen/
│   │       ├── candidates.h
│   │       ├── regalloc.h
│   │       └── select.h
│   ├── src/
│   │   ├── main.c
│   │   ├── cost_model.c
│   │   ├── dsp_build.c        # DSP path: hand-built IR (fixed 64-pt FFT)
│   │   ├── emit.c
│   │   ├── ingest.c           # ML path: ONNX protobuf -> IR
│   │   ├── ir.c
│   │   ├── locality.c
│   │   └── codegen/
│   │       ├── candidates.c
│   │       ├── regalloc.c
│   │       └── select.c
│   ├── tests/
│   │   ├── CMakeLists.txt
│   │   └── test_ir.c
│   └── CMakeLists.txt
├── sim/
│   ├── run_avrora.sh          # Assemble + invoke Avrora
│   └── parse_report.py        # Parse Avrora energy output
├── models/
│   └── tiny_classifier.onnx   # Exported demo model
├── cost_table.toml            # Per-instruction energy costs (sourced)
├── SOURCES.md                 # Cited source for every cost entry
├── energy_aware_compiler_spec_v2.md  # Full build spec
├── REPORT.md                  # Methodology, results, limitations
└── README.md
```

## Testing

```bash
cmake -S compiler -B compiler/build
cmake --build compiler/build
ctest --test-dir compiler/build
```

Covers: IR construction, cost table loading, candidate generation correctness (via Avrora), end-to-end output equivalence (both variants produce identical numeric results).

**Correctness principle**: A lower-energy sequence that computes the wrong answer is a failed test, full stop. Every candidate is validated against expected numeric output via Avrora.

## Milestones

| # | Milestone | Status |
|---|-----------|--------|
| 1 | Toolchain bring-up (avr-gcc + Avrora) | ✅ |
| 2 | Minimal end-to-end, both workloads (ML `Add` + DSP `Window`, 2 candidates each) | ✅ |
| 3 | ONNX ingestion (MatMul, Add, Relu, Requantize) | ✅ |
| 4 | DSP builder + fixed 64-point FFT pipeline (IR only; lowering is milestone 6) | ✅ |
| 5 | Full instruction selection + regalloc + cost-table select (locality pass not implemented) | ✅ |
| 6 | DSP lowering + end-to-end DSP comparison | 🚧 4 of 13 ops lowered |
| 7 | Sourcing (cost table) + write REPORT.md | ✅ cost table sourced; REPORT.md drafted |
| — | Phase B: compiler-emitted sleep scheduling (added after the original plan) | ✅ |

See `energy_aware_compiler_spec_v2.md` §8 for details.

## Non-Goals (Phase 1)

| Non-Goal | Reason |
|----------|--------|
| Multi-ISA support (ARM Cortex-M, RISC-V) | Phase 2 |
| Physical hardware measurement | Phase 2 (deferred, not eliminated); sim-only in Phase 1 |
| Arbitrary-N FFT | Fixed-size only: one hardcoded length (64 points), radix-2 DIT |
| Fingerprint hash generation | Lightweight post-processing, not the energy-relevant kernel |
| General ONNX operator coverage | Demo model only |
| ML/DSP compiler backend dependencies | TVM, Glow, IREE, TFLite Micro, CMSIS-DSP excluded |
| Polished CLI/UX | Proof-of-concept pipeline only |

## License

MIT, see [LICENSE](LICENSE).

## Links

- **Repo:** https://github.com/rugbedbugg/OptiFine
- **Spec:** `energy_aware_compiler_spec_v2.md`
- **Commit guidelines:** `documents/COMMIT_GUIDELINES.md`
- **Issues:** https://github.com/rugbedbugg/OptiFine/issues