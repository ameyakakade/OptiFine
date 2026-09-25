# Reproducing OptiFine's Results

Every figure in [`README.md`](../README.md) and [`REPORT.md`](../REPORT.md) is
derived by script from raw simulator output retained under `sim/fixtures/`.
This page lists the commands, from a fresh clone.

## Tool setup

| Tool | Version used for the retained evidence | Needed for |
|---|---|---|
| CMake, C11 compiler (gcc/clang) | CMake >= 3.16 (4.4 used) | building the compiler and its tests |
| Python | 3.9+ (3.13 used), with `pytest` | result generation, reproduction scripts and their tests |
| AVR toolchain | avr-gcc 16.1.0, avr-binutils, avr-libc | assembling emitted programs |
| JDK | Temurin 8u504-b01 | running Avrora (Avrora 1.7.115 fails on JDK 9+) |
| Avrora | Beta 1.7.115, SHA-256 `016021f4...eb` | simulation and energy reports |

On Linux, `tools/setup_linux.sh` downloads the JDK and `avrora.jar` into
`tools/` (gitignored, never committed), checks the jar's SHA-256, and tells you
how to install the AVR toolchain from your distribution:

```bash
./tools/setup_linux.sh
export JAVA_HOME=$PWD/tools/jdk8u504-b01     # used by sim/run_avrora.sh
```

The Python scripts find the JDK under `tools/jdk8*`, `avrora.jar` under
`tools/` and `avr-gcc` on the `PATH`; `AVR_GCC`, `JAVA8_BIN` and `AVRORA_JAR`
override them.

## Build and tests

```bash
cmake -S compiler -B compiler/build
cmake --build compiler/build
ctest --test-dir compiler/build      # compiler, interpreter, pipeline and CLI tests
python3 -m pytest sim/tests          # evidence checks and result-generation tests
```

Neither suite starts Avrora. The Python tests revalidate the retained evidence
(hashes, re-derived tables and the canonical figures) and tamper-test the
checks.

## Retained evidence

| Directory | Experiment | Script |
|---|---|---|
| `sim/fixtures/active_ml/` | ML classifier, naive and optimized, plus the bring-up calibration program `sim/smoke.s` | `sim/run_active_ml.py` |
| `sim/fixtures/dsp/` | the complete DSP program: assembly, compiler output, Avrora report, `avr-size`, per-op prediction | `sim/run_dsp.py` |
| `sim/fixtures/periodic_ml/` | periodic scheduling of the ML classifier: 4 prescalers x naive/optimized x busy-wait/Power-save x counts 4 and 5 (32 runs) | `sim/run_periodic_ml.py` |
| `sim/fixtures/periodic_dsp/` | periodic scheduling of the DSP pipeline: 4 prescalers x busy-wait/Power-save x counts 4 and 5 (16 runs) | `sim/run_periodic_dsp.py` |

Each directory holds the generated assembly, the raw tool output and a
`manifest.json` recording:

- the source commit the capture ran from (`git.commit`, and whether the
  compiler, model or cost-table sources differed from it),
- the exact commands, with repository-relative paths,
- the SHA-256 of every recorded input (model, inputs, golden outputs,
  `cost_table.toml`) and every raw artifact,
- the tools used: the compiler binary's SHA-256, avr-gcc and Java version
  output, the `avrora.jar` SHA-256,
- the evidence limit: Avrora-model simulation, no hardware measurement, and no
  simulated SRAM read back.

## Verify the retained evidence (no tool runs)

```bash
python3 sim/run_active_ml.py check
python3 sim/run_dsp.py check
python3 sim/run_periodic_ml.py reproduce-retained
python3 sim/run_periodic_dsp.py reproduce-retained
```

Each checks every recorded hash, re-parses the raw Avrora reports, and
re-derives the experiment's tables byte for byte. The periodic scripts re-run
the pair evaluation (body identity, count-only differences between the count-4
and count-5 programs, the static deadline check, and the steady-state
comparison) and require the same outcome. None starts the compiler, the
assembler or Avrora, and none writes a file.

## Regenerate the result tables

```bash
python3 sim/report_results.py                      # Markdown to stdout
python3 sim/report_results.py --format csv
python3 sim/report_results.py --out-dir build/results
```

`report_results.py` reads only the raw files under `sim/fixtures/` and does
arithmetic; every number in the README and the report comes from it.

## Re-run the experiments

Fresh runs use your local toolchain, write into a new directory and refuse
one that already holds a manifest, so retained evidence is never overwritten.

```bash
export JAVA_HOME=$PWD/tools/jdk8u504-b01

# Active-mode ML: naive vs optimized, plus the calibration program
python3 sim/run_active_ml.py capture --output-dir build/active_ml

# Ordinary DSP program
python3 sim/run_dsp.py capture --output-dir build/dsp
python3 sim/run_dsp.py compare sim/fixtures/dsp build/dsp

# Periodic scheduling, ML and DSP
python3 sim/run_periodic_ml.py run-new --output-dir build/periodic_ml
python3 sim/run_periodic_dsp.py run-new --output-dir build/periodic_dsp
```

A fresh capture can then be checked like the retained one, e.g.
`python3 sim/run_periodic_ml.py reproduce-retained --output-dir build/periodic_ml`,
and its raw Avrora reports compared with the retained ones. The ELF files a
run leaves beside the assembly are regenerable and gitignored.

## Single programs

```bash
compiler/build/optifine models/tiny_classifier.onnx --cost-table cost_table.toml \
  --input models/tiny_classifier_golden_input.txt --optimized --out build/optimized.s
bash sim/run_avrora.sh build/optimized.s          # Simulated time: 6018 cycles

compiler/build/optifine --dsp --cost-table cost_table.toml --out build/dsp.s
bash sim/run_avrora.sh build/dsp.s                # Simulated time: 148335 cycles
```

## What reproduces, and what does not

Given the same compiler sources, inputs and `avrora.jar`, the emitted assembly
and Avrora's cycle and energy reports are deterministic: the retained evidence
was recaptured on a different operating system, assembler and JDK build than
the project's original runs, and every figure was identical. What no script
here can reproduce is a physical measurement; there is none.
