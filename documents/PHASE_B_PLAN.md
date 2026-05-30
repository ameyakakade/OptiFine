# Phase B Duty-Cycle Scheduling Implementation Plan


**Goal:** Build and evaluate matched active-wait and Power-save periodic AVR classifier programs, sweep four duty cycles, and document whether sleep scheduling adds energy savings beyond Phase A.

**Architecture:** Split the existing emitter into reusable initialization and inference regions. A periodic wrapper owns interrupt vectors, asynchronous Timer0, waiting policy, invocation counting, overrun detection, and termination; both policies embed an identical classifier body. Python tooling validates matched Avrora reports and generates paper tables.

**Tech Stack:** C11, CMake/CTest, AVR assembly for ATmega128, avr-gcc, Avrora Beta 1.7.115 on JDK 8, Python 3 standard library.

**Spec:** `documents/PHASE_B_DESIGN.md`

## Global Constraints

- Target ATmega128; asynchronous periodic wake uses Timer0 through `ASSR.AS0`.
- The active and Power-save variants must use identical model, input, classifier body, timer setup, ISR, invocation count, and stop condition.
- Reject output mismatch, unequal invocation counts, overrun, nontermination, and missing expected CPU-state cycles.
- Preserve generated assembly, raw Avrora reports, commands, and metadata.
- Do not create Git commits unless the user explicitly requests one.

## File Map

- Create `compiler/include/optifine/codegen/periodic.h` for schedule types and the public periodic-emission API.
- Create `compiler/src/codegen/periodic.c` for vectors, Timer0 setup, wait loops, ISR, and repeated classifier invocation.
- Create `compiler/tests/test_periodic.c` for validation and emitted-assembly tests.
- Create `sim/run_phase_b.py` for generation and Avrora orchestration.
- Create `sim/compare_phase_b.py` and `sim/tests/test_compare_phase_b.py` for strict matched-pair analysis.
- Create `sim/fixtures/phase_b/manifest.json`, generated `.S` programs, raw reports, CSV, and Markdown tables.
- Modify `program.h/.c`, `main.c`, CMake files, `parse_report.py`, `REPORT.md`, the living spec, and `documents/PHASE_B_NOTES.md`.

---

### Task 1: Split classifier emission into reusable regions

**Files:**
- Modify: `compiler/include/optifine/codegen/program.h`
- Modify: `compiler/src/codegen/program.c`
- Modify: `compiler/tests/test_lower.c`

**Interfaces:**
- Produces `ProgramRegionCost`, `codegen_emit_initialization(...)`, and `codegen_emit_inference_body(...)`.
- Preserves `codegen_emit_program(...)` and current standalone output.

- [ ] **Step 1: Write failing tests.** Emit each region to a temporary stream. Assert initialization contains constant `sts` operations, inference contains `muls`, both costs are positive, and neither region contains `_start:` or `break`.

```c
ProgramRegionCost init = {0}, body = {0};
assert(codegen_emit_initialization(&g, &layout, &ra, &model,
    input, input_len, 1, init_out, &init) == 0);
assert(codegen_emit_inference_body(&g, &layout, &ra, &model,
    input, input_len, 1, body_out, &body) == 0);
assert(init.cycles > 0 && body.cycles > 0);
```

- [ ] **Step 2: Verify failure.** Run `cmake --build compiler/build` and `ctest --test-dir compiler/build -R test_lower --output-on-failure`. Expect missing region symbols.
- [ ] **Step 3: Implement the interface.**

```c
typedef struct { double energy_nj; uint32_t cycles; } ProgramRegionCost;
int codegen_emit_initialization(const IrGraph *, const SramLayout *,
    const RegAllocResult *, const CostModel *, const int8_t *, size_t,
    int, FILE *, ProgramRegionCost *);
int codegen_emit_inference_body(const IrGraph *, const SramLayout *,
    const RegAllocResult *, const CostModel *, const int8_t *, size_t,
    int, FILE *, ProgramRegionCost *);
```

Move zero-register initialization plus `OP_INPUT`/`OP_CONST` into the first function and all remaining ops into the second. Keep candidate selection private and shared.

- [ ] **Step 4: Rebuild `codegen_emit_program` by composition.** Emit the existing wrapper, call both region functions, transfer their costs, and emit `break`. Compare regenerated standalone assembly with current fixtures; instruction sequences must not change.
- [ ] **Step 5: Run all compiler tests.** `ctest --test-dir compiler/build --output-on-failure` must pass, including golden output `[0,-3,18,27]`.

---

### Task 2: Define and validate periodic scheduling

**Files:**
- Create: `compiler/include/optifine/codegen/periodic.h`
- Create: `compiler/src/codegen/periodic.c`
- Create: `compiler/tests/test_periodic.c`
- Modify: `compiler/CMakeLists.txt`
- Modify: `compiler/tests/CMakeLists.txt`

**Interfaces:**
- Consumes the Task 1 region emitters.
- Produces `PeriodicOptions`, `PeriodicProgramCost`, `periodic_options_validate()`, and `codegen_emit_periodic_program()`.

- [ ] **Step 1: Write failing validation tests.** Accept prescalers `8`, `32`, `128`, `1024` and counts `1..255`; reject every other divisor and count zero.
- [ ] **Step 2: Verify failure.** Build and run `ctest --test-dir compiler/build -R test_periodic --output-on-failure`; expect missing target or symbols.
- [ ] **Step 3: Implement public types.**

```c
typedef enum { WAIT_ACTIVE = 0, WAIT_POWER_SAVE = 1 } WaitPolicy;
typedef struct {
    WaitPolicy policy;
    uint16_t timer_prescaler;
    uint8_t inference_count;
    int use_real_candidates;
} PeriodicOptions;
typedef struct {
    ProgramRegionCost initialization;
    ProgramRegionCost inference;
    uint16_t tick_addr, running_addr, overrun_addr, completed_addr;
} PeriodicProgramCost;
```

Reserve four scheduler bytes immediately after the tensor layout and reject addresses above `0x10FF`.

- [ ] **Step 4: Implement exact Timer0 encoding.** Map divisors `8,32,128,1024` to `CS02:0` values `2,3,5,7`; return an error for anything else.
- [ ] **Step 5: Run focused and full compiler tests.** All must pass.

---

### Task 3: Emit matched periodic programs

**Files:**
- Modify: `compiler/src/codegen/periodic.c`
- Modify: `compiler/tests/test_periodic.c`

**Interfaces:**
- Produces complete `.S` from `codegen_emit_periodic_program(...)`.

- [ ] **Step 1: Write failing structure tests.** Both outputs must contain the Timer0 vector, common ISR, one `break`, count check, and overrun flag. Active output must not contain `sleep`; Power-save must. Text between `; BODY BEGIN` and `; BODY END` must be identical.
- [ ] **Step 2: Verify failure.** Run the focused CTest and expect missing emission behavior.
- [ ] **Step 3: Emit common vectors and setup.** Include `<avr/io.h>`, reset/vector jumps, stack initialization, four scheduler bytes, `ASSR.AS0`, selected prescaler, busy-flag wait, `TIMSK.TOIE0`, and `sei`.

```asm
.org 0x0000
    jmp reset
.org (TIMER0_OVF_vect_num * 4)
    jmp timer0_ovf_isr
```

- [ ] **Step 4: Emit a register-safe ISR.** Save and restore `r16` and SREG. Set `overrun=1` when `running!=0`, then set `tick=1` and return.

```asm
timer0_ovf_isr:
    push r16
    in r16, _SFR_IO_ADDR(SREG)
    push r16
    lds r16, RUNNING_ADDR
    tst r16
    breq no_overrun
    ldi r16, 1
    sts OVERRUN_ADDR, r16
no_overrun:
    ldi r16, 1
    sts TICK_ADDR, r16
    pop r16
    out _SFR_IO_ADDR(SREG), r16
    pop r16
    reti
```

- [ ] **Step 5: Emit policy-specific waits.** Active repeatedly tests `tick`. Power-save uses atomic `cli`/flag-test/sleep-setup/`sei`/`sleep` so a tick cannot be lost between testing and sleeping. Both clear tick, set running, execute the identical body, clear running, increment completed count, and repeat.
- [ ] **Step 6: Emit terminal state.** Store completed count, overrun status, and four output bytes at documented SRAM addresses before `break`, including on overrun.
- [ ] **Step 7: Verify.** Run all compiler tests and diff both BODY regions; the diff must be empty.

---

### Task 4: Add periodic CLI support

**Files:**
- Modify: `compiler/src/main.c`
- Modify: `compiler/tests/test_periodic.c`

**Interfaces:**
- Adds `--periodic-count N --wait-policy active|powersave --timer-prescaler N`.

- [ ] **Step 1: Add failing CLI tests.** A valid invocation must produce vectors; invalid policy, divisor `7`, and count `0` must exit nonzero.
- [ ] **Step 2: Verify failure.** Run the focused CTest; expect unknown arguments.
- [ ] **Step 3: Parse flags.** Preserve standalone output when `--periodic-count` is absent. In periodic mode require all three flags and route to `codegen_emit_periodic_program()`.
- [ ] **Step 4: Print metadata.** Report policy, divisor, count, initialization predicted cost, and per-inference predicted compute cost to stderr.
- [ ] **Step 5: Verify compatibility.** Run all CTests, assemble one standalone optimized program, and assemble both periodic policies with `avr-gcc -mmcu=atmega128 -nostartfiles`.

---

### Task 5: Validate matched Avrora reports

**Files:**
- Modify: `sim/parse_report.py`
- Create: `sim/compare_phase_b.py`
- Create: `sim/tests/test_compare_phase_b.py`

**Interfaces:**
- Consumes raw reports plus metadata.
- Produces validated rows for Markdown and CSV.

- [ ] **Step 1: Write failing `unittest` cases.** Test Active/Power-save parsing, saving percentage, energy per inference, and rejection of unequal divisor, count, body hash, output, overrun, or missing Power-save cycles.
- [ ] **Step 2: Verify failure.** Run `python -m unittest discover -s sim/tests -p "test_*.py" -v`.
- [ ] **Step 3: Extend `EnergyReport`.** Parse `Simulated time:` while preserving current component/state fields and CLI output.
- [ ] **Step 4: Implement strict comparison.** Metadata fields are `policy`, `prescaler`, `inference_count`, `body_sha256`, `completed_count`, `overrun`, and `output`. Validate first, then calculate `saved_nj`, `saving_pct`, and `nj_per_inference`.
- [ ] **Step 5: Emit these columns:** `prescaler,period_cycles,inferences,active_nj,powersave_nj,saved_nj,saving_pct,nj_per_inference,active_cycles,powersave_cycles,deadline_status,output_status`.
- [ ] **Step 6: Run Python and compiler regressions.** Both suites must pass.

---

### Task 6: Run the four-point, four-variant sweep

**Files:**
- Create: `sim/run_phase_b.py`
- Generate: `sim/fixtures/phase_b/manifest.json`
- Generate: `sim/fixtures/phase_b/*.S`
- Generate: `sim/fixtures/phase_b/*.avrora.txt`
- Rename the legacy Timer2-named artifact to `sim/fixtures/spike_powersave_timer0.S`
- Rename: matching `.avrora.txt`

**Interfaces:**
- Produces 16 runs: four divisors x naive/optimized x active/Power-save.

- [ ] **Step 1: Test dry-run enumeration.** `--dry-run` must print exactly 16 unique commands without running subprocesses.
- [ ] **Step 2: Implement orchestration.** Use `subprocess.run` argument arrays with a 120-second timeout. Hash text between BODY markers and refuse to run a matched pair with different hashes.
- [ ] **Step 3: Write the manifest.** Record command, timestamp, tool versions, model/input hashes, body hash, divisor, expected count `4`, policy, compute path, paths, compiler stderr, and parsed report fields.
- [ ] **Step 4: Correct the spike name and references.** Rename Timer2 artifacts to Timer0 and update `documents/PHASE_B_NOTES.md`; verify the obsolete filename has no matches.
- [ ] **Step 5: Run a prescaler-32 smoke pair.** Require termination, matching body hashes, positive Power-save cycles only in the sleep variant, correct output, equal count, and no overrun.
- [ ] **Step 6: Run divisors `8,32,128,1024`.** Retain divisor 8 as a rejected observation if it overruns; do not replace it. Divisors 32, 128, and 1024 must produce valid pairs or be debugged before analysis.
- [ ] **Step 7: Generate primary and 2x2 tables.** Keep rejected rows with explicit reasons.

---

### Task 7: Verify the dataset

**Files:**
- Modify generated manifest/results only when regeneration corrects a demonstrated tooling error.

- [ ] **Step 1: Re-run golden output.** Naive and optimized tests must remain bit-exact at `[0,-3,18,27]`.
- [ ] **Step 2: Disassemble every ELF.** Use `avr-objdump -d`; matched policy variants must have equivalent classifier-body address ranges.
- [ ] **Step 3: Check invariants.** Equal count, divisor, body hash, and output; zero overrun; expected CPU states; comparable observation window.
- [ ] **Step 4: Investigate contradictions.** If valid positive-slack Power-save is not lower-energy, inspect state cycles, interrupts, waits, and disassembly. Do not discard an unfavorable result.
- [ ] **Step 5: Run the full gate.** Build, CTest, Python unittest, and comparison generation must all pass.

---

### Task 8: Close the paper gaps

**Files:**
- Modify: `REPORT.md`
- Modify: `energy_aware_compiler_spec_v2.md`
- Modify: `documents/PHASE_B_NOTES.md`
- Modify: `SOURCES.md` only for authoritative Timer0/Power-save sources actually cited.

- [ ] **Step 1: Update the spec.** Record validated async Timer0 + Power-save, the matched active baseline, sweep, deadline rule, and 2x2 experiment.
- [ ] **Step 2: Add methodology.** State hypotheses, controls, modes, count, divisors, formulas, output/overrun checks, initialization caveat, and tool versions.
- [ ] **Step 3: Add generated results.** Keep Phase A's 1.83% compute saving distinct from Phase B, then report their combination.
- [ ] **Step 4: Add threats to validity.** Include Avrora's state model, absent watchdog, no physical measurement, one MCU/model/input, periodic-workload assumption, timer fidelity, and initialization interpretation.
- [ ] **Step 5: Add reproduction commands and artifact links.** Every paper row must trace to manifest metadata and a raw report.
- [ ] **Step 6: Check consistency.** Remove stale Timer2 and implemented-watchdog wording, stale phase status, unsupported hardware claims, and numbers that differ from generated CSV.

## Final Acceptance Gate

- [ ] C and Python tests pass.
- [ ] Matched bodies are hash-identical and disassembly-equivalent.
- [ ] All accepted variants output `[0,-3,18,27]` with equal counts and no overrun.
- [ ] Divisors 32, 128, and 1024 yield valid pairs; divisor 8 remains visible whether accepted or rejected.
- [ ] Naive/optimized x active/Power-save data exists for every valid divisor.
- [ ] Raw reports, manifest, formulas, limitations, and reproduction commands support every claim.
- [ ] No Git commit is made without explicit user authorization.
