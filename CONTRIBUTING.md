# Contributing to OptiFine

OptiFine is a research prototype. Changes are welcome where they keep its
results correct and reproducible.

## Build and test

```sh
cmake -S compiler -B compiler/build
cmake --build compiler/build
ctest --test-dir compiler/build
python3 -m pytest sim/tests
```

Simulation needs the AVR toolchain, JDK 8 and Avrora (see `README.md`,
"Build"; `tools/setup_linux.sh` fetches the last two on Linux).

## Expectations

- **Correctness first.** A cheaper instruction sequence that computes a
  different result is a failed change. Pair new compiler behaviour with a test
  under `compiler/tests/` that checks results exactly, and keep the compiler's
  cycle prediction equal to what the test interpreter executes.
- **Retained evidence is not edited.** Files under `sim/fixtures/` are
  hash-pinned simulator output. New experiments go into a fresh directory via
  the `capture`/`run-new` modes of the `sim/` scripts; existing ones are
  checked with `python3 sim/run_dsp.py check`,
  `python3 sim/run_phase_b.py reproduce-retained` and
  `python3 sim/run_phase_b_dsp.py`.
- **Say what a number is.** Energy figures here are Avrora simulations. Label
  them as simulated, and state their source and assumptions; every
  `cost_table.toml` entry needs a citation in `SOURCES.md`.
- **Keep generated output out of commits:** build trees, ELF files, emitted
  assembly and simulator reports, unless deliberately added as evidence.
- **Commit messages** follow `documents/COMMIT_GUIDELINES.md`.
