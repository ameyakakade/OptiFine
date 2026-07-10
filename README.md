# OptiFine

![GitHub last commit](https://img.shields.io/github/last-commit/rugbedbugg/OptiFine?style=for-the-badge&labelColor=000000)
![GitHub repo size](https://img.shields.io/github/repo-size/rugbedbugg/OptiFine?style=for-the-badge&labelColor=000000)
![Stars](https://img.shields.io/github/stars/rugbedbugg/OptiFine?style=for-the-badge&labelColor=000000)

Compiler backend that takes IR from two sources - an INT8-quantized neural network (PyTorch → ONNX) and a fixed-size audio/DSP pipeline (64-point FFT, Q15 fixed-point) - and compiles both to AVR machine code, selecting between candidate instruction sequences by **estimated energy cost** instead of cycle count. Validated end-to-end via Avrora (cycle-accurate AVR simulator with built-in energy monitor). No physical hardware in Phase 1 (sim-only); sim-to-hardware correlation is deferred Phase 2 work.

## Status

**WIP** - scaffolding stage. See `energy_aware_compiler_spec_v2.md` for the full build spec and milestone order; most modules are stubs (`TODO(milestone N)`) until their milestone is reached.

## Features

| Feature | Description |
|---------|-------------|
| Custom typed IR | Ingestion parses ONNX protobuf directly, no lexer/parser |
| Candidate generation | ≥2 equivalent AVR instruction sequences per IR op |
| Energy-cost selection | Via `cost_table.toml` (cited sources required) |
| Register allocation | Next-use information for AVR's limited register file |
| Memory locality pass | Reduces SRAM accesses (register reuse, loop tiling) |
| AVR assembly emission | `avr-gcc`/`avr-as` → ELF → Avrora simulation |
| End-to-end comparison | Same model compiled speed-optimized vs. energy-aware |

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

### Compile Model (Speed vs Energy)

> **Not yet implemented** — `--strategy` lands at milestone 5. Shown here as
> the target CLI.

```bash
# Speed-optimized (select by cycles)
./compiler/build/optifine models/tiny_classifier.onnx \
  --cost-table cost_table.toml \
  --out build/speed_opt.s \
  --strategy speed

# Energy-aware (select by energy_nj)
./compiler/build/optifine models/tiny_classifier.onnx \
  --cost-table cost_table.toml \
  --out build/energy_opt.s \
  --strategy energy
```

### Simulate & Compare

```bash
cd sim
./run_avrora.sh ../build/speed_opt.s atmega328p path/to/avrora.jar
./run_avrora.sh ../build/energy_opt.s atmega328p path/to/avrora.jar
python parse_report.py speed_opt.energy.csv energy_opt.energy.csv
```

## Commands

### Compiler (`optifine`)

```bash
optifine <model.onnx> --cost-table <cost_table.toml> --out <out.s> [--strategy speed|energy]
```

| Flag | Description |
|------|-------------|
| `--cost-table` | Path to `cost_table.toml` (required) |
| `--out` | Output assembly file (default: `out.s`) |
| `--strategy` | `speed` (min cycles) or `energy` (min energy_nj). **Planned — not yet implemented** |

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

Every entry **must** have a cited source before treated as real. Log same source in `SOURCES.md`.

```toml
[instructions]
MUL      = { energy_nj = 0.0, source = "PLACEHOLDER -- needs sourcing" }
ADD      = { energy_nj = 0.0, source = "PLACEHOLDER -- needs sourcing" }
SUB      = { energy_nj = 0.0, source = "PLACEHOLDER -- needs sourcing" }
LD_SRAM  = { energy_nj = 0.0, source = "PLACEHOLDER -- needs sourcing" }
ST_SRAM  = { energy_nj = 0.0, source = "PLACEHOLDER -- needs sourcing" }
MOV      = { energy_nj = 0.0, source = "PLACEHOLDER -- needs sourcing" }
LDI      = { energy_nj = 0.0, source = "PLACEHOLDER -- needs sourcing" }
```

**Milestone 7 task**: Replace all `PLACEHOLDER` entries with cited figures from ATmega328P/ATmega2560 datasheet current-draw tables or academic instruction-level energy characterization papers. DSP-path entries (`FIXED_MUL_Q15`, `COMPLEX_ADD`) get added at milestone 4 as placeholders.

### Sources (`SOURCES.md`)

```markdown
# Energy Cost Sources

| Instruction | Energy (nJ) | Source |
|-------------|------------:|--------|
| MUL         | X.XX        | ATmega328P DS §XX.X / [Paper Title](URL) |
| ADD         | X.XX        | ... |
```

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
| 1 | Toolchain bring-up (avr-gcc + Avrora) | ⏳ |
| 2 | Minimal end-to-end, both workloads (ML `Add` + DSP `Window`, 2 candidates each) | ⏳ |
| 3 | ONNX ingestion (MatMul, Add, Relu, Requantize) | ⏳ |
| 4 | DSP builder + fixed 64-point FFT pipeline | ⏳ |
| 5 | Full instruction selection + regalloc + locality + cost-table select | ⏳ |
| 6 | End-to-end comparison (both workloads × both strategies) | ⏳ |
| 7 | Sourcing (cost table) + write REPORT.md | ⏳ |

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