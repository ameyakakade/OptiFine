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

Simulation needs the AVR toolchain, JDK 8 and Avrora; see
[docs/REPRODUCIBILITY.md](docs/REPRODUCIBILITY.md) (`tools/setup_linux.sh`
fetches the last two on Linux).

## Expectations

- **Correctness first.** A cheaper instruction sequence that computes a
  different result is a failed change. Pair new compiler behaviour with a test
  under `compiler/tests/` that checks results exactly, and keep the compiler's
  cycle prediction equal to what the test interpreter executes.
- **Retained evidence is not edited.** Files under `sim/fixtures/` are
  hash-pinned simulator output. A new experiment goes into a fresh directory
  through the `capture`/`run-new` mode of the matching `sim/run_*.py` script;
  the `check`/`reproduce-retained` modes verify existing ones. A change that
  alters generated programs needs the affected evidence recaptured, not
  patched.
- **Say what a number is.** Energy figures here are Avrora simulations. Label
  them as simulated and state their source and assumptions; every
  `cost_table.toml` entry needs a citation in `SOURCES.md`.
- **No machine-specific paths** in committed files, manifests included: record
  repository-relative paths.
- **Keep generated output out of commits:** build trees, ELF files, emitted
  assembly and simulator reports, unless deliberately added as evidence.

## Commits

- One logical change per commit; stage deliberately rather than `git add .`.
- Message format: `[Area]- Imperative summary`, no trailing period, about 72
  characters at most, with an optional body explaining why. For example
  `[Compiler]- Price the closing branch of long counted loops`.
- Areas: `[Compiler]` (`compiler/`), `[Sim]` (`sim/`), `[Export]` (`export/`,
  `models/`), `[Cost]` (`cost_table.toml`, `SOURCES.md`), `[Build]` (CMake,
  toolchain setup, repository configuration), `[Test]` (test-only changes),
  `[Docs]` (documentation). Split changes that span areas where practical.
- A `cost_table.toml` change carries its citation in the same commit.
