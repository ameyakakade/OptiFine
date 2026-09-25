# OptiFine

**Energy-Aware Code Generation for AVR**

OptiFine is a research compiler backend that generates energy-aware code for
the AVR ATmega128. It compiles an INT8 neural-network classifier (from ONNX)
and a 64-point Q15 DSP pipeline to AVR assembly, prices every emitted
instruction with a sourced energy model, and evaluates the programs in the
Avrora cycle-accurate simulator.

## Status

Research prototype, version 0.1.0. Both workloads compile end to end, and for
every retained program the compiler's cycle prediction equals Avrora's count.
**All energy figures are simulated** with Avrora's ATmega128 power model; no
physical board has been measured. Interfaces may change.

## What it does

- Compiles an ONNX classifier and a Q15 DSP pipeline to AVR through one typed
  IR, one lowering and emission backend and one cost model.
- **Active-mode optimization:** picks between alternative instruction
  sequences by predicted energy, with next-use register caching.
- **Periodic power-aware scheduling:** wraps a compute body in a timer-driven
  program that spends each period's idle time busy-waiting or in Power-save.
- **Exact cycle prediction:** straight-line code and statically bounded loops
  are priced per executed instruction.
- **Retained evidence:** raw simulator output is kept in the repository with
  hash manifests, and every reported figure is regenerated from it by script.

## Key results

Avrora simulations of an ATmega128 at 8 MHz, derived from `sim/fixtures/` by
`python3 sim/report_results.py`. Method and full tables: [REPORT.md](REPORT.md).

| Result | Value |
|---|---|
| ML classifier, cycles (naive -> optimized) | 6,130 -> 6,018 (-1.83%) |
| ML classifier, simulated energy (naive -> optimized) | 17,393.95 -> 17,076.15 nJ |
| DSP pipeline, cycles (predicted = simulated) | 148,335 (18.542 ms at 8 MHz) |
| DSP pipeline, simulated active energy | 420,902.42 nJ (420.90 µJ) |
| DSP pipeline, flash / SRAM | 12,800 B / 2,517 B |
| ML periodic scheduling, Power-save vs busy-wait | 33.31% / 82.10% / 96.33% saved at prescaler 32 / 128 / 1024 |
| DSP periodic scheduling, prescaler 1024 | 42.97% saved (424,239.11 vs 743,836.88 nJ per period) |

The two workloads compute different things, so their rows show the backend's
range, not a comparison. Periodic savings depend on the idle fraction of each
period (the ML body fills about 2% of the prescaler-1024 period, the DSP body
about 56%). The DSP busy-wait figure is normalized from Avrora's raw
262,141-cycle increment (743,828.36 nJ) to the exact 262,144-cycle period; see
[Periodic power-aware scheduling](#periodic-power-aware-scheduling).

## Architecture

```
ONNX classifier --(ingest.c)----\
                                 >-- typed IR --> lowering / candidates --> selection --> AVR assembly --> avr-gcc --> Avrora
DSP pipeline ---(dsp_build.c)---/
```

Lowering produces one correct sequence per op; DSP ops use statically bounded
counted loops. Every instruction is priced as cycles times one per-cycle
constant calibrated to Avrora's ATmega128 model ([SOURCES.md](SOURCES.md)).
Details: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Workloads

- **ML classifier:** a 16 -> 8 -> 4 INT8 network (`models/tiny_classifier.onnx`,
  exported by `export/export_model.py`), checked bit-exactly against an
  independent numpy reference.
- **DSP pipeline:** Hamming window -> bit reversal -> 64-point radix-2 FFT
  (Q15, 1/2 scaling per stage) -> exact magnitude -> top-8 magnitudes. The
  compiled program matches an integer host reference at every intermediate
  buffer over 18 test signals.

## Active-mode optimization

`--optimized` adds a register-cached MatMul candidate and keeps whichever
candidate is cheaper. No available source supports pricing AVR instructions
differently by type within Active mode, and Avrora simulates equal-cycle
sequences to identical energy, so energy-optimal and cycle-optimal code
coincide: the optimizer saves exactly its 112 cycles (1.83%).

## Periodic power-aware scheduling

`--periodic-count N --wait-policy active|powersave --timer-prescaler P` runs
the body once per Timer0 period (256 x P cycles) and spends the rest of the
period **busy-waiting** or in **Power-save**. The body is identical under both
policies. Figures are steady-state `E(5) - E(4)` differences, which cancel
one-time setup.

An Active cycle costs about 61 times a Power-save cycle in Avrora's model, so
the saving follows the idle fraction. The DSP body (147,565 cycles) fits only
the prescaler-1024 period; prescalers 8, 32 and 128 are rejected as
compute-bound. Under sleep scheduling the classifier's 112-cycle active-mode
saving is worth 312.606 nJ per period; under busy-wait it is worth nothing.

**Busy-wait normalization (DSP only).** The busy-wait loop polls every 8
cycles, so each wake is detected up to 8 cycles late, and with the DSP body the
phase does not settle: Avrora's busy-wait increment is 262,141 cycles against
the exact 262,144 of the Power-save increment. The DSP busy-wait energy is
therefore scaled by 262,144 / 262,141 to one exact period: 743,828.36 nJ raw,
743,836.88 nJ normalized. The normalized value equals the ML experiment's
busy-wait energy at the same period, as it must.

## Build

Requirements: CMake >= 3.16 and a C11 compiler; Python 3.9+ with `pytest`; for
simulation, the AVR toolchain (`avr-gcc`, `avr-binutils`, `avr-libc`), JDK 8
and Avrora Beta 1.7.115.

```bash
cmake -S compiler -B compiler/build
cmake --build compiler/build
ctest --test-dir compiler/build

./tools/setup_linux.sh                       # Linux: JDK 8 + Avrora into tools/ (not committed)
export JAVA_HOME=$PWD/tools/jdk8u504-b01     # Avrora 1.7.115 needs JDK 8
```

Install the AVR toolchain from your package manager (`avr-gcc avr-binutils
avr-libc` on Arch and Fedora, `gcc-avr binutils-avr avr-libc` on Debian/Ubuntu).

## Usage

```bash
mkdir -p build

# ML classifier, naive and optimized
compiler/build/optifine models/tiny_classifier.onnx --cost-table cost_table.toml \
  --input models/tiny_classifier_golden_input.txt --out build/naive.s
compiler/build/optifine models/tiny_classifier.onnx --cost-table cost_table.toml \
  --input models/tiny_classifier_golden_input.txt --optimized --out build/optimized.s

# DSP pipeline (default input: models/dsp_demo_input.txt, two tones)
compiler/build/optifine --dsp --cost-table cost_table.toml --out build/dsp.s

# Either workload in the periodic wrapper
compiler/build/optifine --dsp --cost-table cost_table.toml --out build/dsp_ps.S \
  --periodic-count 4 --wait-policy powersave --timer-prescaler 1024

# Assemble and simulate
bash sim/run_avrora.sh build/dsp.s           # Simulated time: 148335 cycles
```

| Flag | Meaning |
|---|---|
| `--cost-table` | path to `cost_table.toml` |
| `--out` | output assembly file |
| `--input` | compile-time input (ML: int8 values; `--dsp`: 64 int16 Q15 samples, `#` comments) |
| `--optimized` | ML only: active-mode optimization |
| `--dsp` | compile the DSP pipeline instead of an ONNX model |
| `--periodic-count`, `--wait-policy`, `--timer-prescaler` | periodic wrapper; all three together |

## Reproducing the results

```bash
python3 -m pytest sim/tests                          # verifies all retained evidence
python3 sim/report_results.py                        # regenerates every result table
python3 sim/run_active_ml.py check                   # each of these runs no tool
python3 sim/run_dsp.py check
python3 sim/run_periodic_ml.py reproduce-retained
python3 sim/run_periodic_dsp.py reproduce-retained
```

Raw evidence lives in `sim/fixtures/{active_ml,dsp,periodic_ml,periodic_dsp}/`,
each with a manifest of source commit, commands, tool versions and SHA-256
hashes. Re-running the experiments with your own toolchain is described in
[docs/REPRODUCIBILITY.md](docs/REPRODUCIBILITY.md).

## Validation

- **Correctness:** generated programs run in a test-only AVR interpreter and
  are compared exactly with independent references.
- **Cycle model:** predicted cycles equal the interpreter's count in the tests
  and Avrora's count for every retained program.
- **Evidence:** manifests pin every raw file; the scripts re-derive every
  published table from them.

## Limitations

- **Simulation only:** no physical hardware measurement.
- **Uniform Active-mode cost:** cycles times one constant.
- **Fixed, compile-time inputs;** no runtime sensor or host input.
- **Narrow scope:** one classifier and one fixed DSP pipeline on one MCU; no
  general ONNX coverage, other FFT sizes or peak positions.
- **Scoped register allocation:** caching covers MatMul's activation input
  only.
- **One accepted DSP periodic point,** with the busy-wait normalization above.

More in [REPORT.md](REPORT.md#limitations).

## Citation

If you use OptiFine, please cite it using [CITATION.cff](CITATION.cff):

> Partha Pratim Gogoi. *OptiFine: Energy-Aware Code Generation for AVR*,
> version 0.1.0, 2026.

Related work is surveyed in [docs/LITERATURE_SURVEY.md](docs/LITERATURE_SURVEY.md).
Contributions: [CONTRIBUTING.md](CONTRIBUTING.md).

## License

MIT; see [LICENSE](LICENSE).
