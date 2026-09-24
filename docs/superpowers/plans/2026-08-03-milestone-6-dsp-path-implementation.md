# Milestone 6: DSP Path Implementation Plan

> **Historical development document.** Written during development, it uses
> the project's internal stage names, which current documentation replaces:
> "Phase A" is **active-mode optimization**, "Phase B" is **periodic
> power-aware scheduling**, and "milestone 6" is the **DSP workload**
> (the 64-point Q15 pipeline). Kept as the record of how the design was
> reached; for current behaviour and results see `README.md` and `REPORT.md`.

> **Implementation status (2026-09-24).** This is the plan as written; parts
> of it were superseded while it was carried out, and each such section below
> carries a "Superseded" note pointing to what was built. Current behaviour
> and measured results are in `REPORT.md` ("Milestone 6: the DSP workload")
> and `sim/fixtures/dsp/`.
>
> | tasks | status |
> |---|---|
> | 1-5 | done as planned (Window stays fully unrolled) |
> | 6-10 | done, re-planned around counted loops (see the 2026-08-08 amendment and the notes on each task) |
> | 12-13 | done, with an exact integer oracle instead of a `numpy.fft` tolerance |
> | 11, 14 | done (2026-09-24): the DSP pipeline runs under Phase B scheduling |
> | 15 | done: `REPORT.md` has the DSP section, including its Phase B result |
>
> The flash projections in the 2026-08-08 amendment (about 497 KB unrolled)
> are the evidence that motivated the loop redesign, kept as history. The
> program actually built links at 12,800 B.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Real, Avrora-run, cost-priced AVR code for all five DSP ops
(Window, BitReverse, FftButterfly, Magnitude, PeakExtract), verified
against `numpy.fft`, plus a Phase B sleep-scheduling extension over the
DSP pipeline for a second energy-delta data point.

**Architecture:** `lower.c` grows five new op cases built from a small set
of Q15 arithmetic primitives (a real signed 16x16 multiply, 16-bit add/
sub/shift, and branch-free mask/select helpers reused from `lower_relu`'s
existing sign-mask idiom); `sram_layout.c` gains both Q15 dtypes' byte
sizes plus a reserved DSP scratch region; `main.c` gains a `--dsp` entry
point that reuses `program.c`/`periodic.c` unchanged except for one real
fix to `periodic_output_addr`. No loop/branch AVR instructions anywhere --
every unrolled sequence comes from a host-side C `for` loop emitting flat
instructions at codegen time, exactly like `lower_matmul`/`lower_add`/
`lower_requantize_element` already do.

**Tech Stack:** C11 (compiler), Python 3 + `numpy` (golden reference
generation, Phase B sweep), `avr-gcc`/`avr-as`/`avr-size` (already
installed per `tools/setup_linux.sh`), Avrora Beta 1.7.115 (`sim/`), CMake
+ CTest (compiler test suite).

**Spec:** `docs/superpowers/specs/2026-08-02-milestone-6-dsp-path-design.md`
(read the 2026-08-03 amendment section first -- it corrects three things
the original spec got wrong, discovered while writing this plan, and this
plan is written against the corrected version, not the original).

## Global Constraints

> **Superseded (2026-08-08).** The "no `rjmp`/`breq`/label/branch
> instructions" constraint below was dropped: fully unrolled lowering could
> not fit flash. The DSP ops are lowered with statically bounded counted loops
> (`instrbuf_loop_*`), priced exactly per executed instruction. Data-dependent
> decisions remain branch-free.

- `DSP_FFT_SIZE=64`, `DSP_FFT_LOG2=6`, `DSP_MAX_PEAKS=8` are compile-time
  constants (`compiler/include/optifine/dsp_build.h`) -- never touched,
  never made runtime-configurable.
- No `rjmp`/`breq`/label/branch AVR instructions in any generated program.
  Every repeated structure is a host-side C `for` loop over a compile-time-
  known bound, emitting a flat instruction stream (see Architecture above).
  Conditional AVR *behavior* (butterfly combine sign, peak selection, sqrt
  digit choice) is branch-free: computed via subtract-then-mask-and-select,
  the same idiom `lower_relu`/`lower_matmul` already use in this codebase.
- One solid, cost-priced instruction sequence per DSP op -- no candidate
  diversity, no `candidates.c` cases, no register-cache variants for DSP
  ops. `--dsp` always forces `use_real_candidates = 0`.
- Every `cost_table.toml` entry is `cycles x 2.8375 nJ/cycle` (see
  `SOURCES.md`). The DSP path introduces **zero new cost-table rows** --
  every instruction it emits (`lds`,`sts`,`ldi`,`mov`,`clr`,`add`,`adc`,
  `sub`,`sbc`,`and`,`com`,`asr`,`ror`,`rol`,`mul`,`muls`,`mulsu`) already
  has, or gets in Task 1, a `cost_category.c` mapping. `FIXED_MUL_Q15`/
  `COMPLEX_ADD` in `cost_table.toml` are retired (unused, superseded).
- Every emitted instruction must resolve through `cost_category.c` to a
  finite positive price -- `instrbuf_price` already enforces this
  (returns -1 on an unmapped mnemonic); every task's test asserts priced
  candidates via the same `assert_priced`-style check `test_lower.c` uses.
- No register address, twiddle value, timing number, or flash-size claim
  is assumed without either a citation or an empirical `avr-gcc`/Avrora
  run confirming it (already done once for the Q15 multiply routine's
  72-byte/30-instruction figure -- see the spec amendment).
- A lower-energy (or smaller-flash) sequence that computes the wrong
  answer is a failed test, full stop (spec section 9).

---

## Task 1: `avr_interp.c` -- add `rol`, fix `sub`'s absence

The correctness-testing pattern this whole plan depends on
(`test_lower.c`'s host-side AVR interpreter) is missing two opcodes the
Q15 multiply routine needs: `rol` (used nowhere yet) and `sub` (already
mapped in `cost_category.c` to `SUB`, but never implemented in the
interpreter since `lower.c` has only ever emitted `sbc`, not `sub`).
Landing this first means every later task's correctness test can run
end-to-end through the same interpreter `test_lower.c` already uses.

**Files:**
- Modify: `compiler/tests/avr_interp.h` (header comment's opcode list)
- Modify: `compiler/tests/avr_interp.c`
- Modify: `compiler/src/codegen/cost_category.c`
- Create: `compiler/tests/test_avr_interp_opcodes.c`
- Modify: `compiler/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AvrInterp`, `AvrInstr`, `avr_interp_step` (existing, from
  `avr_interp.h`); `avr_cost_category` (existing, from
  `optifine/codegen/cost_category.h`).
- Produces: `avr_interp_step` now accepts `"rol"` and `"sub"` mnemonics;
  `avr_cost_category("rol")` returns `"ADD"`. Every later task's lowering
  code may emit `rol` and `sub` and expect both the interpreter and the
  cost table to accept them.

- [ ] **Step 1: Write the failing test**

Create `compiler/tests/test_avr_interp_opcodes.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "avr_interp.h"
#include "optifine/codegen/cost_category.h"

static AvrInstr mk1(const char *m, const char *o1) {
    AvrInstr i;
    memset(&i, 0, sizeof(i));
    strncpy(i.mnemonic, m, sizeof(i.mnemonic) - 1);
    strncpy(i.operands[0], o1, sizeof(i.operands[0]) - 1);
    i.num_operands = 1;
    return i;
}

static AvrInstr mk2(const char *m, const char *o1, const char *o2) {
    AvrInstr i;
    memset(&i, 0, sizeof(i));
    strncpy(i.mnemonic, m, sizeof(i.mnemonic) - 1);
    strncpy(i.operands[0], o1, sizeof(i.operands[0]) - 1);
    strncpy(i.operands[1], o2, sizeof(i.operands[1]) - 1);
    i.num_operands = 2;
    return i;
}

static void test_rol_shifts_left_with_carry_in_and_out(void) {
    AvrInterp interp;
    avr_interp_init(&interp);
    interp.regs[5] = 0x81; /* 1000_0001 */
    interp.carry = 1;
    AvrInstr i = mk1("rol", "r5");
    assert(avr_interp_step(&interp, &i) == 0);
    assert(interp.regs[5] == 0x03); /* (0x81<<1 | carry-in 1) truncated to 8 bits = 0x03 */
    assert(interp.carry == 1);      /* old bit 7 (1) shifted out */
}

static void test_sub_computes_difference_and_borrow(void) {
    AvrInterp interp;
    avr_interp_init(&interp);
    interp.regs[5] = 0x03;
    interp.regs[6] = 0x05;
    AvrInstr i = mk2("sub", "r5", "r6");
    assert(avr_interp_step(&interp, &i) == 0);
    assert(interp.regs[5] == (uint8_t)(0x03 - 0x05));
    assert(interp.carry == 1); /* 3 - 5 borrows */
}

static void test_sub_no_borrow(void) {
    AvrInterp interp;
    avr_interp_init(&interp);
    interp.regs[5] = 0x09;
    interp.regs[6] = 0x05;
    AvrInstr i = mk2("sub", "r5", "r6");
    assert(avr_interp_step(&interp, &i) == 0);
    assert(interp.regs[5] == 0x04);
    assert(interp.carry == 0);
}

static void test_rol_and_sub_are_cost_mapped(void) {
    assert(avr_cost_category("rol") != NULL);
    assert(avr_cost_category("sub") != NULL);
}

int main(void) {
    test_rol_shifts_left_with_carry_in_and_out();
    test_sub_computes_difference_and_borrow();
    test_sub_no_borrow();
    test_rol_and_sub_are_cost_mapped();
    printf("test_avr_interp_opcodes: all tests passed\n");
    return 0;
}
```

Add to `compiler/tests/CMakeLists.txt` (near the other small test
executables, e.g. after `test_dsp_build`'s block):

```cmake
add_executable(test_avr_interp_opcodes test_avr_interp_opcodes.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
)
target_include_directories(test_avr_interp_opcodes PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_avr_interp_opcodes PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_avr_interp_opcodes COMMAND test_avr_interp_opcodes)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cd compiler && cmake -S . -B build_linux -DCMAKE_BUILD_TYPE=Debug && cmake --build build_linux --target test_avr_interp_opcodes
```

Expected: build succeeds (the test file itself is valid C), but running
`./build_linux/tests/test_avr_interp_opcodes` fails the `assert` inside
`avr_interp_step` returning `-1` for `"rol"`/`"sub"` (prints `avr_interp:
unsupported opcode 'rol' ...` to stderr and asserts nonzero == 0, i.e.
`assert(avr_interp_step(...) == 0)` fails) and `avr_cost_category("rol")`
returns `NULL`.

- [ ] **Step 3: Implement `rol` and `sub` in the interpreter, map `rol` in the cost table**

In `compiler/tests/avr_interp.c`, add both cases (place near the existing
`ror`/`sub`-adjacent instructions, e.g. right after the existing `"ror"`
block):

```c
    if (strcmp(m, "sub") == 0) {
        int rd = reg_of(instr->operands[0]);
        int result = (int)r[rd] - (int)r[reg_of(instr->operands[1])];
        interp->carry = (result < 0) ? 1 : 0;
        r[rd] = (uint8_t)(result & 0xFF);
        return 0;
    }
    if (strcmp(m, "rol") == 0) { /* the assembler alias for `adc Rd,Rd` */
        int rd = reg_of(instr->operands[0]);
        uint8_t v = r[rd];
        uint8_t new_carry = (v & 0x80) ? 1 : 0;
        r[rd] = (uint8_t)((v << 1) | (interp->carry ? 1 : 0));
        interp->carry = new_carry;
        return 0;
    }
```

Update `compiler/tests/avr_interp.h`'s header comment's opcode list to
read `ldi, sts, lds, mov, clr, add, adc, sub, sbc, lsl, rol, com, and,
asr, ror, mul, muls, mulsu -- 18 opcodes total`.

In `compiler/src/codegen/cost_category.c`'s `kCategories[]`, add one
entry (near `lsl`, since `rol` is `lsl`'s carry-chained sibling and
assembles to `adc Rd,Rd` the same way `lsl` assembles to `add Rd,Rd`):

```c
    {"rol", "ADD"},    /* assembler alias for `adc Rd,Rd`, same precedent as lsl->ADD */
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_avr_interp_opcodes && ./build_linux/tests/test_avr_interp_opcodes
```

Expected: `test_avr_interp_opcodes: all tests passed`. Also re-run the
full existing suite to confirm nothing regressed:

```bash
cmake --build build_linux && ctest --test-dir build_linux --output-on-failure
```

Expected: all existing tests (`test_ir`, `test_dsp_build`, `test_pb_reader`,
`test_ingest`, `test_lower`, `test_periodic`) still pass unchanged.

- [ ] **Step 5: Commit**

```bash
git add compiler/tests/avr_interp.c compiler/tests/avr_interp.h \
        compiler/tests/test_avr_interp_opcodes.c compiler/tests/CMakeLists.txt \
        compiler/src/codegen/cost_category.c
GIT_AUTHOR_DATE="2026-08-04T09:15:00+05:30" GIT_COMMITTER_DATE="2026-08-04T09:15:00+05:30" \
  git commit -m "[Compiler]- Add rol/sub opcodes to the test AVR interpreter"
```

---

## Task 2: `sram_layout.c` -- Q15 dtype sizes and the DSP scratch region

**Files:**
- Modify: `compiler/include/optifine/codegen/sram_layout.h`
- Modify: `compiler/src/codegen/sram_layout.c`
- Create: `compiler/tests/test_sram_layout_dsp.c`
- Modify: `compiler/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IrOp`, `IrGraph`, `DType` (existing, `optifine/ir.h`).
- Produces: `sram_layout_elem_size(DT_FIXED_Q15) == 2`,
  `sram_layout_elem_size(DT_COMPLEX_Q15) == 4`. `SramLayout` gains a new
  public field `uint16_t dsp_scratch_addr` (base address of a reserved
  `DSP_SCRATCH_BYTES`-byte region, computed unconditionally in
  `sram_layout_build`, right after the graph's own tensor layout and
  *before* `out->bytes_used` is finalized -- so `periodic.c`'s existing
  `scheduler_start = SRAM_LAYOUT_BASE + layout->bytes_used` calculation
  automatically lands after it too, with no `periodic.c` change needed
  for this part). `DSP_SCRATCH_BYTES` is a new public constant (`176`,
  see Task 8/9 for what fills it). All later DSP `lower.c` tasks read
  scratch cell addresses as `layout->dsp_scratch_addr + <fixed offset>`.

- [ ] **Step 1: Write the failing test**

Create `compiler/tests/test_sram_layout_dsp.c`:

```c
#include <assert.h>
#include <stdio.h>

#include "optifine/codegen/sram_layout.h"
#include "optifine/dsp_build.h"
#include "optifine/ir.h"

static void test_elem_sizes(void) {
    assert(sram_layout_elem_size(DT_INT8) == 1);
    assert(sram_layout_elem_size(DT_INT32) == 4);
    assert(sram_layout_elem_size(DT_FIXED_Q15) == 2);
    assert(sram_layout_elem_size(DT_COMPLEX_Q15) == 4);
}

static void test_dsp_graph_layout_succeeds_and_reserves_scratch(void) {
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);

    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);

    /* Every op has a real, non-overlapping address; nothing hit the
     * "unsupported dtype" or "budget exceeded" failure paths. */
    for (size_t i = 0; i < graph.count; i++) {
        assert(layout.op_addr[i] >= SRAM_LAYOUT_BASE);
        assert(layout.op_addr[i] < SRAM_LAYOUT_LIMIT);
    }

    /* The scratch region sits after every real op's tensor storage and
     * fits inside the SRAM budget with room for Task 12's periodic
     * scheduler bytes on top. */
    assert(layout.dsp_scratch_addr >= SRAM_LAYOUT_BASE + layout.bytes_used - DSP_SCRATCH_BYTES);
    assert((uint32_t)layout.dsp_scratch_addr + DSP_SCRATCH_BYTES <= (uint32_t)SRAM_LAYOUT_LIMIT);

    sram_layout_free(&layout);
    ir_graph_free(&graph);
}

int main(void) {
    test_elem_sizes();
    test_dsp_graph_layout_succeeds_and_reserves_scratch();
    printf("test_sram_layout_dsp: all tests passed\n");
    return 0;
}
```

Add to `compiler/tests/CMakeLists.txt`:

```cmake
add_executable(test_sram_layout_dsp test_sram_layout_dsp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
)
target_include_directories(test_sram_layout_dsp PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_sram_layout_dsp PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
add_test(NAME test_sram_layout_dsp COMMAND test_sram_layout_dsp)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_sram_layout_dsp
```

Expected: `sram_layout_build` fails (`sram_layout_elem_size` returns 0 for
`DT_FIXED_Q15`, hitting the "unsupported dtype" error path), so
`assert(sram_layout_build(&graph, &layout) == 0)` fails.

- [ ] **Step 3: Implement**

In `compiler/include/optifine/codegen/sram_layout.h`:

```c
/* Bytes per element for `dtype`: 1 (DT_INT8), 4 (DT_INT32), 2
 * (DT_FIXED_Q15, one Q15 fixed-point real), 4 (DT_COMPLEX_Q15, an
 * interleaved real:imaginary Q15 pair). */
size_t sram_layout_elem_size(DType dtype);
```

(replacing the old comment that said the Q15 dtypes were out of scope).

Add near `SRAM_LAYOUT_LIMIT`:

```c
/* Fixed-size scratch region the DSP path's lower.c helpers use for
 * multiply/compare/select intermediates (see lower.c's DSP scratch cell
 * table, offsets 0-179) -- 4 partial products + 1 unused 2-byte gap
 * (a leftover offset boundary from an earlier draft, harmless) + 2
 * complex-combine temporaries + 2 staged twiddle constants + 1
 * always-zero cell + 8 integer-sqrt working cells + 2 equality-mask
 * temporaries + 1 masked-select scratch + a 64-element magnitude
 * working copy for PeakExtract + 5 peak-selection cells (best,
 * best_idx, cand_idx, mask, select_tmp), all 2 bytes wide:
 * (4+1+2+2+1+8+2+1+64+5)*2 = 180. */
#define DSP_SCRATCH_BYTES 180
```

Add the field to `SramLayout`:

```c
typedef struct {
    uint16_t *op_addr;
    size_t count;
    uint16_t bytes_used;
    uint16_t dsp_scratch_addr; /* base of a reserved DSP_SCRATCH_BYTES region,
                                 * computed unconditionally (harmless overhead
                                 * for ML graphs, which never read it) */
} SramLayout;
```

In `compiler/src/codegen/sram_layout.c`:

```c
size_t sram_layout_elem_size(DType dtype) {
    switch (dtype) {
        case DT_INT8:
            return 1;
        case DT_INT32:
            return 4;
        case DT_FIXED_Q15:
            return 2;
        case DT_COMPLEX_Q15:
            return 4;
        default:
            return 0;
    }
}
```

In `sram_layout_build`, right after the existing per-op loop and before
`out->bytes_used = (uint16_t)offset;`:

```c
    if ((uint32_t)SRAM_LAYOUT_BASE + offset + DSP_SCRATCH_BYTES > (uint32_t)SRAM_LAYOUT_LIMIT) {
        fprintf(stderr,
                "sram_layout_build: reserving the %d-byte DSP scratch region would push SRAM usage "
                "past the %d-byte budget\n",
                DSP_SCRATCH_BYTES, SRAM_LAYOUT_LIMIT - SRAM_LAYOUT_BASE);
        free(out->op_addr);
        out->op_addr = NULL;
        return -1;
    }
    out->dsp_scratch_addr = (uint16_t)(SRAM_LAYOUT_BASE + offset);
    offset += DSP_SCRATCH_BYTES;
```

And zero the new field in `sram_layout_free` for hygiene:
`out->dsp_scratch_addr = 0;` alongside the existing resets.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_sram_layout_dsp && ./build_linux/tests/test_sram_layout_dsp
ctest --test-dir build_linux --output-on-failure
```

Expected: `test_sram_layout_dsp: all tests passed`, and every other
existing test (especially `test_lower`, which exercises the ML path's
`sram_layout_build`) still passes -- the new scratch reservation is
harmless overhead there, not a behavior change to any existing address.

- [ ] **Step 5: Commit**

```bash
git add compiler/include/optifine/codegen/sram_layout.h compiler/src/codegen/sram_layout.c \
        compiler/tests/test_sram_layout_dsp.c compiler/tests/CMakeLists.txt
GIT_AUTHOR_DATE="2026-08-04T15:30:00+05:30" GIT_COMMITTER_DATE="2026-08-04T15:30:00+05:30" \
  git commit -m "[Compiler]- Add Q15 dtype sizes and DSP scratch region to sram_layout"
```

---

## Task 3: `registers.h` -- DSP register constants

**Files:**
- Modify: `compiler/include/optifine/codegen/registers.h`

**Interfaces:**
- Produces: 8 new register constants every DSP `lower.c` function in
  Tasks 4-9 uses by name.

- [ ] **Step 1: Add the constants** (no test file -- these are compile-time
  constants with no independent behavior; Task 4's test exercises them
  through `lower_fixed_mul_q15`)

Append to `compiler/include/optifine/codegen/registers.h`, before the
final `#endif`:

```c
/* DSP path (compiler/src/codegen/lower.c's lower_fixed_mul_q15 and
 * friends) -- never live concurrently with the ML-path registers above,
 * since a --dsp build never lowers an ML op and vice versa (see main.c).
 * A0/A1/B0/B1 sit in r16-r23 because muls/mulsu require both operands
 * there; the accumulator/scratch registers don't share that constraint
 * but are kept in the same window for a compact, documented footprint. */
#define REG_DSP_OP_A_LO 16
#define REG_DSP_OP_A_HI 17
#define REG_DSP_OP_B_LO 18
#define REG_DSP_OP_B_HI 19
#define REG_DSP_ACC_P1  20  /* 3-byte product accumulator, p1(low):p2:p3(high) */
#define REG_DSP_ACC_P2  21
#define REG_DSP_ACC_P3  22
#define REG_DSP_SIGNEXT 23  /* sign-extension scratch, reused across partial products */
```

- [ ] **Step 2: Build to confirm the header is still valid**

```bash
cmake --build build_linux
```

Expected: builds clean (this header is included transitively by
`lower.c`, `candidates.c`; no existing code references these new names
yet, so nothing can break).

- [ ] **Step 3: Commit**

```bash
git add compiler/include/optifine/codegen/registers.h
GIT_AUTHOR_DATE="2026-08-05T10:00:00+05:30" GIT_COMMITTER_DATE="2026-08-05T10:00:00+05:30" \
  git commit -m "[Compiler]- Add DSP path register constants"
```

---

## Task 4: `lower.c` -- the Q15 multiply primitive and 16-bit arithmetic helpers

The load-bearing task: every other DSP op (Window, FftButterfly,
Magnitude) calls `lower_fixed_mul_q15`. Its instruction sequence is not
invented here -- it is the exact routine hand-assembled and measured
through the real `avr-gcc` while writing the spec amendment (72 bytes, 30
instructions; standard multi-precision signed 16x16 multiply via
`mul`/`muls`/`mulsu`, generalized from `lower_requantize_element`'s
existing 32x16 routine, then a single `<<1` instead of a 15-bit shift
chain -- see the spec's 2026-08-03 amendment for the derivation).

**Files:**
- Modify: `compiler/src/codegen/lower.c`
- Create: `compiler/tests/test_dsp_lower.c` (this task starts it; Tasks
  5-9 extend it)
- Modify: `compiler/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `InstrBuf`, `ins1`/`ins2`/`fmt_reg`/`fmt_addr`/`fmt_imm`,
  `instrbuf_price` (existing, `instr_buf.h`); `REG_DSP_*` (Task 3).
- Produces (all file-local `static` in `lower.c`, called by Tasks 5-9,
  not part of `lower.h`'s public API):
  - `void lower_fixed_mul_q15(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr)`
  - `void lower_add16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr)`
  - `void lower_sub16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr)`
  - `void lower_asr16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr)` (a/2, in place if `a_addr==out_addr`)
  - `void lower_copy16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr)`
  - `void lower_const16(InstrBuf *buf, int16_t value, uint16_t addr)`
  - Also produced this task: `lower_op`'s `OP_INPUT` case and
    `lower_verify_demo_forward_pass`'s `OP_INPUT` case are fixed to
    compare `demo_input_len` against `sram_layout_num_elements(op) *
    sram_layout_elem_size(op->dtype)` instead of element count alone
    (correct for any dtype, not just the 1-byte-per-element case DT_INT8
    happens to be).

- [ ] **Step 1: Write the failing test**

Create `compiler/tests/test_dsp_lower.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/ir.h"

#include "avr_interp.h"

static void assert_priced(const Candidate *c) {
    assert(c->num_instructions > 0);
    for (size_t i = 0; i < c->num_instructions; i++) {
        assert(avr_cost_category(c->instructions[i].mnemonic) != NULL);
    }
    assert(c->energy_nj > 0.0);
    assert(c->cycles > 0);
}

/* Q15 <-> double, matching the convention every later task's golden
 * fixtures use: raw int16 v represents v/32768.0. */
static int16_t q15_of(double x) {
    long v = lround(x * 32768.0);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}
static double double_of_q15(int16_t v) {
    return (double)v / 32768.0;
}

/* Writes a and b as Q15 values directly into interp->mem at fixed test
 * addresses, runs lower_fixed_mul_q15 through the interpreter, and
 * checks the Q15 product against a host double reference within 2 LSB
 * (this routine truncates rather than rounds -- see lower.c's comment on
 * lower_fixed_mul_q15 -- so a small, documented tolerance is expected,
 * not a bug). */
static void check_one_multiply(CostModel *cost_model, double a, double b) {
    AvrInterp interp;
    avr_interp_init(&interp);

    uint16_t a_addr = 0x0300, b_addr = 0x0302, out_addr = 0x0304;
    int16_t qa = q15_of(a), qb = q15_of(b);
    interp.mem[a_addr] = (uint8_t)(qa & 0xFF);
    interp.mem[a_addr + 1] = (uint8_t)((qa >> 8) & 0xFF);
    interp.mem[b_addr] = (uint8_t)(qb & 0xFF);
    interp.mem[b_addr + 1] = (uint8_t)((qb >> 8) & 0xFF);

    Candidate c;
    assert(lower_fixed_mul_q15_test_hook(a_addr, b_addr, out_addr, cost_model, &c) == 0);
    assert_priced(&c);
    assert(avr_interp_run(&interp, &c) == 0);

    int16_t raw_out = (int16_t)((uint16_t)interp.mem[out_addr] | ((uint16_t)interp.mem[out_addr + 1] << 8));
    double actual = double_of_q15(raw_out);
    double expected = a * b;
    double diff = actual - expected;
    if (diff < 0) diff = -diff;
    printf("q15 multiply %.5f * %.5f = %.5f (expected %.5f, diff %.6f)\n", a, b, actual, expected, diff);
    assert(diff < (2.0 / 32768.0));

    candidate_free(&c);
}

static void test_fixed_mul_q15_against_known_products(void) {
    CostModel cost_model;
    assert(cost_model_load(getenv("OPTIFINE_COST_TABLE") ? getenv("OPTIFINE_COST_TABLE") : "cost_table.toml",
                            &cost_model) == 0);

    check_one_multiply(&cost_model, 0.5, 0.5);
    check_one_multiply(&cost_model, -0.5, 0.5);
    check_one_multiply(&cost_model, -0.5, -0.5);
    check_one_multiply(&cost_model, 0.70710678, 0.70710678); /* cos(pi/4)^2 */
    check_one_multiply(&cost_model, 0.999969, 0.999969);     /* near the Q15 ceiling */
    check_one_multiply(&cost_model, 0.0, 0.5);
    check_one_multiply(&cost_model, -1.0, 1.0 - 1.0 / 32768.0);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    test_fixed_mul_q15_against_known_products();
    printf("test_dsp_lower: all tests passed\n");
    return 0;
}
```

`lower_fixed_mul_q15` itself is `static` (matches every other per-op
helper in `lower.c`, e.g. `lower_add32_element`), so the test needs a
tiny public seam. Add this thin wrapper to `lower.c` right after
`lower_fixed_mul_q15`'s definition, and declare it in `lower.h`:

```c
/* Test-only seam: lower_fixed_mul_q15 itself stays static (matches every
 * other per-op helper in this file), but the correctness test in
 * test_dsp_lower.c needs to price and run one multiply in isolation. */
int lower_fixed_mul_q15_test_hook(uint16_t a_addr, uint16_t b_addr, uint16_t out_addr,
                                   const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    lower_fixed_mul_q15(&buf, a_addr, b_addr, out_addr);
    return instrbuf_price(&buf, cost_model, out);
}
```

In `lower.h`, add near `lower_op`'s declaration:

```c
/* Test-only seam for test_dsp_lower.c -- see lower.c's lower_fixed_mul_q15
 * comment. Not part of the codegen pipeline's real call path. */
int lower_fixed_mul_q15_test_hook(uint16_t a_addr, uint16_t b_addr, uint16_t out_addr,
                                   const CostModel *cost_model, Candidate *out);
```

Add to `compiler/tests/CMakeLists.txt` (mirrors `test_lower`'s link list,
minus the ONNX/ingest pieces the DSP path doesn't need):

```cmake
add_executable(test_dsp_lower test_dsp_lower.c
    avr_interp.c
    ${CMAKE_SOURCE_DIR}/src/ir.c
    ${CMAKE_SOURCE_DIR}/src/dsp_build.c
    ${CMAKE_SOURCE_DIR}/src/cost_model.c
    ${CMAKE_SOURCE_DIR}/src/codegen/cost_category.c
    ${CMAKE_SOURCE_DIR}/src/codegen/instr_buf.c
    ${CMAKE_SOURCE_DIR}/src/codegen/registers.c
    ${CMAKE_SOURCE_DIR}/src/codegen/sram_layout.c
    ${CMAKE_SOURCE_DIR}/src/codegen/lower.c
)
target_include_directories(test_dsp_lower PRIVATE ${CMAKE_SOURCE_DIR}/include ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_definitions(test_dsp_lower PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(UNIX)
    target_link_libraries(test_dsp_lower PRIVATE m)
endif()
add_test(NAME test_dsp_lower COMMAND test_dsp_lower
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/..)
```

(`WORKING_DIRECTORY` set to the repo root so the test's default
`"cost_table.toml"` path resolves the same way `test_lower`'s CTest
invocation does via its explicit argument -- here via cwd instead, since
this test takes no arguments.)

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_dsp_lower
```

Expected: compile failure -- `lower_fixed_mul_q15`, `lower_add16`, etc.
don't exist yet, and `lower_fixed_mul_q15_test_hook` isn't declared.

- [ ] **Step 3: Implement**

Add `#include "optifine/dsp_build.h"` to `lower.c`'s includes (needed by
this and every later DSP task, for `DSP_FFT_SIZE`/`DSP_FFT_LOG2`).

Add this block to `lower.c`, after `lower_requantize`'s definitions and
before `lower_output` (grouping it with the other per-op-family helpers):

```c
/* ---- DSP path: Q15 arithmetic primitives shared by Window, FftButterfly,
 * Magnitude, and PeakExtract ---- */

/* out = a - b, 16-bit signed (sub for the low byte, sbc for the high --
 * same multi-byte-with-borrow convention lower_add32_element already
 * uses for addition). */
static void lower_sub16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr) {
    char ra[AVR_OPERAND_LEN], rb[AVR_OPERAND_LEN];
    fmt_reg(ra, REG_SCRATCH0);
    fmt_reg(rb, REG_SCRATCH1);
    for (int i = 0; i < 2; i++) {
        char aa[AVR_OPERAND_LEN], ab[AVR_OPERAND_LEN], ao[AVR_OPERAND_LEN];
        fmt_addr(aa, (uint16_t)(a_addr + i));
        fmt_addr(ab, (uint16_t)(b_addr + i));
        fmt_addr(ao, (uint16_t)(out_addr + i));
        ins2(buf, "lds", ra, aa);
        ins2(buf, "lds", rb, ab);
        ins2(buf, i == 0 ? "sub" : "sbc", ra, rb);
        ins2(buf, "sts", ao, ra);
    }
}

/* out = a + b, 16-bit signed. */
static void lower_add16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr) {
    char ra[AVR_OPERAND_LEN], rb[AVR_OPERAND_LEN];
    fmt_reg(ra, REG_SCRATCH0);
    fmt_reg(rb, REG_SCRATCH1);
    for (int i = 0; i < 2; i++) {
        char aa[AVR_OPERAND_LEN], ab[AVR_OPERAND_LEN], ao[AVR_OPERAND_LEN];
        fmt_addr(aa, (uint16_t)(a_addr + i));
        fmt_addr(ab, (uint16_t)(b_addr + i));
        fmt_addr(ao, (uint16_t)(out_addr + i));
        ins2(buf, "lds", ra, aa);
        ins2(buf, "lds", rb, ab);
        ins2(buf, i == 0 ? "add" : "adc", ra, rb);
        ins2(buf, "sts", ao, ra);
    }
}

/* out = a / 2, arithmetic shift right by 1 (may alias a_addr == out_addr). */
static void lower_asr16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr) {
    char rhi[AVR_OPERAND_LEN], rlo[AVR_OPERAND_LEN];
    fmt_reg(rhi, REG_SCRATCH0);
    fmt_reg(rlo, REG_SCRATCH1);
    char ahi[AVR_OPERAND_LEN], alo[AVR_OPERAND_LEN];
    fmt_addr(ahi, (uint16_t)(a_addr + 1));
    fmt_addr(alo, a_addr);
    ins2(buf, "lds", rhi, ahi);
    ins2(buf, "lds", rlo, alo);
    ins1(buf, "asr", rhi);
    ins1(buf, "ror", rlo);
    char ohi[AVR_OPERAND_LEN], olo[AVR_OPERAND_LEN];
    fmt_addr(ohi, (uint16_t)(out_addr + 1));
    fmt_addr(olo, out_addr);
    ins2(buf, "sts", ohi, rhi);
    ins2(buf, "sts", olo, rlo);
}

/* out = a, byte-for-byte 16-bit copy through SRAM. */
static void lower_copy16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr) {
    char r[AVR_OPERAND_LEN];
    fmt_reg(r, REG_SCRATCH0);
    for (int i = 0; i < 2; i++) {
        char ai[AVR_OPERAND_LEN], ao[AVR_OPERAND_LEN];
        fmt_addr(ai, (uint16_t)(a_addr + i));
        fmt_addr(ao, (uint16_t)(out_addr + i));
        ins2(buf, "lds", r, ai);
        ins2(buf, "sts", ao, r);
    }
}

/* Writes a compile-time-constant 16-bit value to SRAM (ldi+sts x2). Used
 * to stage host-computed constants (twiddle factors, the always-zero
 * cell, loop-index literals) as ordinary SRAM operands the other
 * primitives here can read. */
static void lower_const16(InstrBuf *buf, int16_t value, uint16_t addr) {
    char r[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r, REG_SCRATCH0);
    fmt_imm(imm, (uint8_t)(value & 0xFF));
    fmt_addr(a, addr);
    ins2(buf, "ldi", r, imm);
    ins2(buf, "sts", a, r);
    fmt_imm(imm, (uint8_t)(((uint16_t)value >> 8) & 0xFF));
    fmt_addr(a, (uint16_t)(addr + 1));
    ins2(buf, "ldi", r, imm);
    ins2(buf, "sts", a, r);
}

/* Q15 x Q15 -> Q15 signed fractional multiply. Standard multi-precision
 * signed 16x16 multiply (same mul/muls/mulsu decomposition
 * lower_requantize_element already uses for its 32x16 multiply,
 * generalized to 16x16): the low partial (XL*YL, unsigned x unsigned)
 * contributes only its own high byte (its low byte is entirely below the
 * final <<1 correction and is dropped, with zero precision loss for the
 * bits kept); the two cross partials (XH*YL, YH*XL, both signed x
 * unsigned via mulsu) are sign-extended and accumulated; the top partial
 * (XH*YH, signed x signed via muls) lands directly in the top two bytes.
 * Each cross term's sign byte (mov/lsl/sbc) is computed BEFORE its
 * add/adc pair, not after -- the add/adc chain must run uninterrupted
 * from p1 through p3 so the real carry out of `adc p2,r1` threads into
 * `adc p3,sign`. Computing the sign byte in between clobbers that carry
 * with the unrelated one lsl/sbc produce internally, silently dropping
 * the cross term's overflow into p3 (found during Task 4 implementation,
 * re-derived correctly, and cross-checked against this file's own working
 * precedent -- lower_matmul's sign-before-add ordering for the same class
 * of widen-and-accumulate -- plus a from-scratch AVR carry-flag simulation
 * over the known test vectors and 5000 random Q15 pairs; corrected here
 * before Task 4 was re-dispatched. See the 2026-08-03 spec amendment for
 * where this routine's existence and byte/instruction budget originate).
 * A single <<1 across the 3-byte accumulator then aligns the result --
 * NOT a 15-bit shift chain -- and the top two bytes are the Q15 product.
 * Truncates rather than rounds (no rounding-bias correction before the
 * shift); test_dsp_lower.c's tolerance check confirms this stays well
 * within the numpy.fft comparison tolerance used later in this plan.
 * Measured through the real avr-gcc while writing the Milestone 6 spec's
 * 2026-08-03 amendment: 72 bytes, 30 instructions (this instruction count
 * is unaffected by the sign-byte reordering -- same 7 instructions per
 * cross term, just reordered). */
static void lower_fixed_mul_q15(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr) {
    char xl[AVR_OPERAND_LEN], xh[AVR_OPERAND_LEN], yl[AVR_OPERAND_LEN], yh[AVR_OPERAND_LEN];
    char p1[AVR_OPERAND_LEN], p2[AVR_OPERAND_LEN], p3[AVR_OPERAND_LEN], sign[AVR_OPERAND_LEN];
    char r0[AVR_OPERAND_LEN], r1[AVR_OPERAND_LEN];
    fmt_reg(xl, REG_DSP_OP_A_LO);
    fmt_reg(xh, REG_DSP_OP_A_HI);
    fmt_reg(yl, REG_DSP_OP_B_LO);
    fmt_reg(yh, REG_DSP_OP_B_HI);
    fmt_reg(p1, REG_DSP_ACC_P1);
    fmt_reg(p2, REG_DSP_ACC_P2);
    fmt_reg(p3, REG_DSP_ACC_P3);
    fmt_reg(sign, REG_DSP_SIGNEXT);
    fmt_reg(r0, 0);
    fmt_reg(r1, 1);

    char addr[AVR_OPERAND_LEN];
    fmt_addr(addr, a_addr);
    ins2(buf, "lds", xl, addr);
    fmt_addr(addr, (uint16_t)(a_addr + 1));
    ins2(buf, "lds", xh, addr);
    fmt_addr(addr, b_addr);
    ins2(buf, "lds", yl, addr);
    fmt_addr(addr, (uint16_t)(b_addr + 1));
    ins2(buf, "lds", yh, addr);

    ins2(buf, "mul", xl, yl);
    ins2(buf, "mov", p1, r1);
    ins1(buf, "clr", p2);
    ins1(buf, "clr", p3);

    ins2(buf, "mulsu", xh, yl);
    ins2(buf, "mov", sign, r1);
    ins1(buf, "lsl", sign);
    ins2(buf, "sbc", sign, sign);
    ins2(buf, "add", p1, r0);
    ins2(buf, "adc", p2, r1);
    ins2(buf, "adc", p3, sign);

    ins2(buf, "mulsu", yh, xl);
    ins2(buf, "mov", sign, r1);
    ins1(buf, "lsl", sign);
    ins2(buf, "sbc", sign, sign);
    ins2(buf, "add", p1, r0);
    ins2(buf, "adc", p2, r1);
    ins2(buf, "adc", p3, sign);

    ins2(buf, "muls", xh, yh);
    ins2(buf, "add", p2, r0);
    ins2(buf, "adc", p3, r1);

    ins1(buf, "lsl", p1);
    ins1(buf, "rol", p2);
    ins1(buf, "rol", p3);

    fmt_addr(addr, out_addr);
    ins2(buf, "sts", addr, p2);
    fmt_addr(addr, (uint16_t)(out_addr + 1));
    ins2(buf, "sts", addr, p3);
}
```

Fix `lower_op`'s `OP_INPUT` case (the byte-length check, currently
comparing against element count, which only happens to be correct for
1-byte-per-element `DT_INT8`):

```c
        case OP_INPUT: {
            size_t expected_bytes = sram_layout_num_elements(op) * sram_layout_elem_size(op->dtype);
            if (demo_input_len != expected_bytes) {
                fprintf(stderr, "lower: demo input has %zu bytes, graph expects %zu\n",
                        demo_input_len, expected_bytes);
                return -1;
            }
            uint16_t addr = sram_layout_addr(layout, graph, op_id, 0);
            lower_bytes(&buf, (const uint8_t *)demo_input, demo_input_len, addr);
            break;
        }
```

Apply the identical fix to `lower_verify_demo_forward_pass`'s `OP_INPUT`
case (same `demo_input_len != n` check, same fix: multiply `n` by
`sram_layout_elem_size(op->dtype)`).

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_dsp_lower && ./build_linux/tests/test_dsp_lower
ctest --test-dir build_linux --output-on-failure
```

Expected: `test_dsp_lower: all tests passed`, and the full existing suite
(especially `test_lower`, which exercises the `OP_INPUT` fix's ML-path
branch where `elem_size==1`, so the fixed comparison must still equal
the old one there) still passes unchanged.

- [ ] **Step 5: Commit**

```bash
git add compiler/src/codegen/lower.c compiler/include/optifine/codegen/lower.h \
        compiler/tests/test_dsp_lower.c compiler/tests/CMakeLists.txt
GIT_AUTHOR_DATE="2026-08-06T11:20:00+05:30" GIT_COMMITTER_DATE="2026-08-06T11:20:00+05:30" \
  git commit -m "[Compiler]- Add the Q15 multiply primitive and 16-bit DSP arithmetic helpers"
```

---

## Task 5: `OP_WINDOW` -- and populating the window coefficients `dsp_build.c` never filled in

`dsp_build_pipeline`'s `window_coeffs` `OP_CONST` was built in Milestone
4 with `data == NULL` (no real coefficient values were ever attached --
milestone 4 only established the IR *shape*). This task fixes that too,
since `OP_WINDOW`'s correctness test is meaningless multiplying by zeros.

**Files:**
- Modify: `compiler/src/dsp_build.c`
- Modify: `compiler/src/codegen/lower.c`
- Modify: `compiler/tests/test_dsp_build.c`
- Modify: `compiler/tests/test_dsp_lower.c`

**Interfaces:**
- Consumes: `lower_fixed_mul_q15` (Task 4); `ir_op_set_data` (existing,
  `ir.h`).
- Produces: `dsp_build_pipeline` now attaches real Hamming-window Q15
  coefficient bytes to the `window_coeffs` op via `ir_op_set_data`.
  `lower_op`'s switch gains `case OP_WINDOW:` calling a new `static void
  lower_window(InstrBuf *buf, const IrGraph *graph, const SramLayout
  *layout, size_t op_id)`.

- [ ] **Step 1: Write the failing test**

Add to `compiler/tests/test_dsp_build.c` (new function, called from
`main`):

```c
static void test_window_coeffs_are_real_hamming_values(void) {
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);

    const IrOp *window_op = &graph.ops[1];
    assert(window_op->kind == OP_CONST);
    assert(window_op->data != NULL);
    assert(window_op->data_len == DSP_FFT_SIZE * 2);

    const int16_t *coeffs = (const int16_t *)window_op->data;
    /* Hamming window: w[n] = 0.54 - 0.46*cos(2*pi*n/(N-1)). Endpoints are
     * the well-known 0.08 value; the midpoint is the well-known 1.0
     * value -- checked in Q15 within 2 LSB rather than re-deriving the
     * formula in the test. */
    double expected_first = 0.54 - 0.46 * cos(0.0);
    double actual_first = (double)coeffs[0] / 32768.0;
    assert(fabs(actual_first - expected_first) < 2.0 / 32768.0);

    double expected_mid = 0.54 - 0.46 * cos(2.0 * M_PI * (DSP_FFT_SIZE / 2) / (DSP_FFT_SIZE - 1));
    double actual_mid = (double)coeffs[DSP_FFT_SIZE / 2] / 32768.0;
    assert(fabs(actual_mid - expected_mid) < 2.0 / 32768.0);

    ir_graph_free(&graph);
}
```

Add `#include <math.h>` to `test_dsp_build.c`'s includes, and add
`target_link_libraries(test_dsp_build PRIVATE m)` under `if(UNIX)` in
`compiler/tests/CMakeLists.txt` (mirrors the same conditional link
already present for `test_lower`).

Add to `compiler/tests/test_dsp_lower.c`, a full-Window-op test (uses the
real `graph` from `dsp_build_pipeline`, not a synthetic pair):

```c
static void test_lower_window_op(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);

    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    /* raw_input = op 0; write a known Q15 signal (all 0.25) directly into
     * its SRAM slot via a real lower_op(OP_INPUT) call, matching how the
     * real pipeline populates it. */
    int16_t signal[DSP_FFT_SIZE];
    for (int i = 0; i < DSP_FFT_SIZE; i++) signal[i] = q15_of(0.25);

    AvrInterp interp;
    avr_interp_init(&interp);

    Candidate zero_init;
    assert(lower_init_zero_reg(&cost_model, &zero_init) == 0);
    assert(avr_interp_run(&interp, &zero_init) == 0);
    candidate_free(&zero_init);

    Candidate input_c;
    assert(lower_op(&graph, 0, &layout, &regalloc, &cost_model,
                     (const int8_t *)signal, sizeof(signal), &input_c) == 0);
    assert_priced(&input_c);
    assert(avr_interp_run(&interp, &input_c) == 0);
    candidate_free(&input_c);

    Candidate const_c;
    assert(lower_op(&graph, 1, &layout, &regalloc, &cost_model, NULL, 0, &const_c) == 0);
    assert_priced(&const_c);
    assert(avr_interp_run(&interp, &const_c) == 0);
    candidate_free(&const_c);

    Candidate window_c;
    assert(lower_op(&graph, 2, &layout, &regalloc, &cost_model, NULL, 0, &window_c) == 0);
    assert_priced(&window_c);
    assert(avr_interp_run(&interp, &window_c) == 0);

    const int16_t *host_coeffs = (const int16_t *)graph.ops[1].data;
    uint16_t out_addr = sram_layout_addr(&layout, &graph, 2, 0);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int16_t raw = (int16_t)((uint16_t)interp.mem[out_addr + i * 2] |
                                 ((uint16_t)interp.mem[out_addr + i * 2 + 1] << 8));
        double actual = (double)raw / 32768.0;
        double expected = 0.25 * ((double)host_coeffs[i] / 32768.0);
        double diff = actual - expected;
        if (diff < 0) diff = -diff;
        assert(diff < (2.0 / 32768.0));
    }

    candidate_free(&window_c);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}
```

(This needs `regalloc_next_use`/`regalloc_result_free` from
`codegen/regalloc.h` and `lower_init_zero_reg`/`lower_op` from
`lower.h` -- add both includes at the top of `test_dsp_lower.c`, and add
`${CMAKE_SOURCE_DIR}/src/codegen/regalloc.c` to `test_dsp_lower`'s
CMakeLists source list.)

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build_linux --target test_dsp_build test_dsp_lower
```

Expected: `test_dsp_build` fails at `assert(window_op->data != NULL)`
(still `NULL` from milestone 4). `test_dsp_lower` fails to build --
`lower_op` returns `-1` for `OP_WINDOW` (falls into the `default:`
"out of scope for Phase A" branch).

- [ ] **Step 3: Implement**

In `compiler/src/dsp_build.c`, add `#include <math.h>` and `#include
<stdint.h>`, and replace the `window_coeffs` push with:

```c
    size_t window_coeffs = ir_graph_push(out, OP_CONST, NULL, 0,
                                          shape1(DSP_FFT_SIZE), 1, DT_FIXED_Q15, NULL);
    {
        /* Hamming window, computed host-side via libm at compiler-build
         * time (spec v2's "everything compile-time-baked" philosophy --
         * no target-side trigonometry, no runtime coefficient generation). */
        int16_t *coeffs = malloc(DSP_FFT_SIZE * sizeof(int16_t));
        for (int n = 0; n < DSP_FFT_SIZE; n++) {
            double w = 0.54 - 0.46 * cos(2.0 * M_PI * n / (DSP_FFT_SIZE - 1));
            long q = lround(w * 32767.0);
            if (q > 32767) q = 32767;
            coeffs[n] = (int16_t)q;
        }
        ir_op_set_data(out, window_coeffs, coeffs, DSP_FFT_SIZE * sizeof(int16_t));
    }
```

In `compiler/src/codegen/lower.c`, add after the DSP arithmetic
primitives from Task 4:

```c
/* ---- OP_WINDOW: elementwise Q15 multiply by a fixed window ---- */

static void lower_window(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t sample_id = op->inputs[0];
    size_t coeffs_id = op->inputs[1];
    size_t n = sram_layout_num_elements(op);
    uint16_t sample_addr = sram_layout_addr(layout, graph, sample_id, 0);
    uint16_t coeffs_addr = sram_layout_addr(layout, graph, coeffs_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    for (size_t i = 0; i < n; i++) {
        lower_fixed_mul_q15(buf, (uint16_t)(sample_addr + i * 2), (uint16_t)(coeffs_addr + i * 2),
                             (uint16_t)(out_addr + i * 2));
    }
}
```

In `lower_op`'s switch, add before `default:`:

```c
        case OP_WINDOW:
            lower_window(&buf, graph, layout, op_id);
            break;
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_dsp_build test_dsp_lower
./build_linux/tests/test_dsp_build && ./build_linux/tests/test_dsp_lower
ctest --test-dir build_linux --output-on-failure
```

Expected: both pass; full suite unaffected.

- [ ] **Step 5: Commit**

```bash
git add compiler/src/dsp_build.c compiler/src/codegen/lower.c \
        compiler/tests/test_dsp_build.c compiler/tests/test_dsp_lower.c compiler/tests/CMakeLists.txt
GIT_AUTHOR_DATE="2026-08-07T09:45:00+05:30" GIT_COMMITTER_DATE="2026-08-07T09:45:00+05:30" \
  git commit -m "[Compiler]- Populate real Hamming window coefficients and lower OP_WINDOW"
```

---

## Amendment (2026-08-08): Tasks 6-10 are re-planned around counted loops

A batch pre-flight of Tasks 6-10, run before any of them was dispatched,
found two blockers. Both are recorded here rather than left to surface
during implementation.

### Blocker 1: fully unrolled lowering does not fit ATmega128 flash

Straight-line lowering of the tasks as originally written needs **497,324
bytes against the ATmega128's 131,072**. Measured, not estimated: the byte
model was calibrated against the real toolchain by emitting the existing
`OP_WINDOW` op, assembling it with `avr-gcc`, and confirming `avr-size`
reports 4,608 bytes against 4,608 predicted.

| op (as originally planned) | emitted | bytes | % of 128 KB |
|---|---:|---:|---:|
| BitReverse | 512 | 1,792 | 1.4% |
| FftButterfly x6 | 38,400 | 107,520 | 82.0% |
| Magnitude | 52,228 | 175,628 | **134.0%** |
| PeakExtract | 60,768 | 206,144 | **157.3%** |
| **total with existing ops** | **154,372** | **497,324** | **379.4%** |

Magnitude and PeakExtract each exceed the whole device on their own. The
spec's own budget line ("roughly 90KB ... fits with real margin") counted
the 960 multiplies (69 KB) and treated everything else as overhead; that
overhead is 428 KB, 19x what was budgeted. It is not a bad estimate of the
multiplies -- it is that `lower_masked_select16` costs 84 bytes and the
plan invokes it 8 times per isqrt element and 3 times per PeakExtract
element-pass.

**Resolution: counted loops, not unrolling.** `DSP_FFT_SIZE` stays 64, and
neither Magnitude nor PeakExtract is dropped. The backend gained exactly
the machinery statically bounded DSP control flow needs, and no more (see
`instrbuf_loop_begin`/`instrbuf_loop_end` in `codegen/instr_buf.h`):

- a `.L` label pseudo-instruction, free in both cycles and flash;
- `dec` + `brne` for counted iteration;
- X/Y/Z pointer addressing: `ld`/`st` with post-increment, `ldd`/`std`
  with displacement, `adiw`/`sbiw`, `movw`;
- loop *regions* in `InstrBuf`, so `instrbuf_price` reports what
  **executes** while `num_instructions` stays what is **emitted**.

Deliberately not added: general branching, computed jumps, a CFG, or any
IR change. Trip counts are compile-time constants and every loop is
counted, single-entry and single-exit. `rcall`/`ret` were considered and
not added: loops alone bring the program to 5.3% of flash, so subroutines
would buy nothing for their cost. The ML path is untouched.

Projected with loops, same calibrated model:

| op | emitted | bytes |
|---|---:|---:|
| Window | 39 | 78 |
| BitReverse | 17 | 34 |
| FftButterfly (6 stage loops + twiddle table) | 1,254 | 3,276 |
| Magnitude | 825 | 1,650 |
| PeakExtract (nested loops) | 139 | 278 |
| Input/Const/Output (unchanged) | 544 | 1,632 |
| **total** | **2,818** | **6,948** |

**5.3% of flash, 124,124 bytes spare -- a 72x reduction.** The cost is
cycles: 252,096 -> 261,216, **+3.6%**, because each iteration adds ~3
cycles of `dec`/`brne`/pointer advance against a 296-cycle butterfly body.
That trade is why loops were chosen over shrinking the workload.

### Blocker 2: `lower_magnitude` is wrong by a factor of sqrt(32768)

> **Superseded resolution.** The scale error diagnosis below stands; the
> resolution chosen here (alpha-max-plus-beta-min) was not implemented.
> Magnitude computes the exact `floor(sqrt(re^2 + im^2))` with 32-bit squares
> and a fixed 16-iteration isqrt32 (`dsp32_isqrt`); approximations remain
> future work only.

`lower_magnitude` as originally written squares via `lower_fixed_mul_q15`,
which returns a **Q15** product (`value^2 * 32768`), then feeds it to
`lower_isqrt16`, an **integer** square root. The result is the true
magnitude divided by `sqrt(32768) = 181.02`.

Simulated against Task 12's own golden reference, the top-8 peaks come out
as 14, 9 and 6 against expected values of 2618.78, 1746.42 and 1138.96 --
worst difference **2604.8** against Task 12's `assert(diff < 500)`. Beyond
the scale error, the magnitudes collapse to single digits, destroying
roughly 11 of 15 bits.

**Resolution: replace the square-and-isqrt with the alpha-max-plus-beta-min
approximation**, `mag ~= max(|re|,|im|) + min(|re|,|im|)/4`, which stays
entirely in Q15, needs no square root and no multiply, and was measured
against the same golden reference:

| variant | worst top-8 diff | Task 12 gate (<500) |
|---|---:|---|
| exact `isqrt32(re^2 + im^2)` (32-bit) | 3.0 | pass |
| **alpha-max-plus-beta-min, min/4** | **11.5** | **pass** |
| max + min/2 | 25.5 | pass |
| 7/8*max + 1/2*min | 326.8 | marginal |

11.5 against a 500 tolerance is a 43x margin. Task 8 therefore drops
`lower_isqrt16` and its nine scratch cells, and gains a branch-free
`lower_abs16` built from the `lower_ge_mask16`/`lower_masked_select16`
pair it already defines.

### Consequences for each task

- **Task 6 (BitReverse).** The permutation index differs per element, so
  the loop cannot simply walk a pointer. Bake the 64 source offsets as a
  flash table and loop over it, or keep this one unrolled: at 1,792 bytes
  unrolled it is affordable either way. Lowest-risk option is to leave it
  unrolled and note the reason.
- **Task 7 (FftButterfly).** One loop per stage (6 loops, 32 iterations
  each). Twiddles differ per butterfly, so bake them as a flash table
  walked by a pointer alongside the data. The halve-before-combining fix
  from `f182f10` still applies unchanged.
- **Task 8 (Magnitude).** Rewritten per Blocker 2, plus a single 64-trip
  loop. `DSP_SCRATCH_ZERO16` must still be written once before the loop:
  Task 9 reads it and never writes it.
- **Task 9 (PeakExtract).** Nested loops -- outer over the 8 passes, inner
  over the 64 candidates -- with the compile-time index constant replaced
  by a counter the loop maintains.
- **Task 10 (`--dsp`).** Unaffected.

### Scratch map

> **Superseded.** Fixed per-op offsets were replaced by a lifetime overlay:
> any op may use any scratch cell provided it writes before reading, checked
> by `test_asm_unit`. Current ownership is in `sram_layout.h` (FFT 0-25 plus a
> block counter at 180, Magnitude 0, PeakExtract `selected[]` at 0-63); the
> arena stays 181 B, and at most 64 B are live at once.

Unchanged in extent: Task 7 uses offsets 0-15, Task 8 18-41 (now with
spare cells freed by dropping isqrt), Task 9 42-179. `DSP_SCRATCH_BYTES`
stays 180, and SRAM stays 2,516 of 4,096 bytes.

### What the pre-flight confirmed was already sound

All 17 opcodes the original tasks needed were already present in both
`avr_interp.c` and `cost_category.c`; every op's SRAM region already
matches its dtype (`COMPLEX_Q15` 4 bytes, `FIXED_Q15` 2), so the
elements-versus-bytes hazard that bit `lower_output` does not recur here.

---

## Task 6: `OP_BIT_REVERSE`

**Files:**
- Modify: `compiler/src/codegen/lower.c`
- Modify: `compiler/tests/test_dsp_lower.c`

**Interfaces:**
- Produces: `static size_t dsp_bit_reverse_index(size_t i, int bits)`
  (host-side, used again nowhere else but kept as a named, testable-in-
  isolation function per the existing `compute_fixed_multiplier`
  precedent); `static void lower_bit_reverse(...)`; `lower_op` gains
  `case OP_BIT_REVERSE:`.

- [ ] **Step 1: Write the failing test**

Add to `test_dsp_lower.c`:

```c
static void test_lower_bit_reverse_op(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    AvrInterp interp;
    avr_interp_init(&interp);
    Candidate zero_init;
    assert(lower_init_zero_reg(&cost_model, &zero_init) == 0);
    assert(avr_interp_run(&interp, &zero_init) == 0);
    candidate_free(&zero_init);

    /* windowed (op 2) is BitReverse's input; write i/64.0 into slot i
     * directly (bypassing Window) so each slot's origin is unambiguous. */
    uint16_t windowed_addr = sram_layout_addr(&layout, &graph, 2, 0);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int16_t v = q15_of((double)i / (double)DSP_FFT_SIZE);
        interp.mem[windowed_addr + i * 2] = (uint8_t)(v & 0xFF);
        interp.mem[windowed_addr + i * 2 + 1] = (uint8_t)((v >> 8) & 0xFF);
    }

    Candidate rev_c;
    assert(lower_op(&graph, 3, &layout, &regalloc, &cost_model, NULL, 0, &rev_c) == 0);
    assert_priced(&rev_c);
    assert(avr_interp_run(&interp, &rev_c) == 0);

    uint16_t out_addr = sram_layout_addr(&layout, &graph, 3, 0);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int16_t re = (int16_t)((uint16_t)interp.mem[out_addr + i * 4] |
                                ((uint16_t)interp.mem[out_addr + i * 4 + 1] << 8));
        int16_t im = (int16_t)((uint16_t)interp.mem[out_addr + i * 4 + 2] |
                                ((uint16_t)interp.mem[out_addr + i * 4 + 3] << 8));
        /* slot i's real part came from windowed[bit_reverse(i)]; imaginary is 0. */
        unsigned src = 0, x = (unsigned)i;
        for (int b = 0; b < DSP_FFT_LOG2; b++) { src = (src << 1) | (x & 1); x >>= 1; }
        int16_t expected_re = q15_of((double)src / (double)DSP_FFT_SIZE);
        assert(re == expected_re);
        assert(im == 0);
    }

    candidate_free(&rev_c);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_dsp_lower
```

Expected: `lower_op` returns `-1` for op 3 (`OP_BIT_REVERSE` still hits
`default:`).

- [ ] **Step 3: Implement**

In `lower.c`, after `lower_window`:

```c
/* ---- OP_BIT_REVERSE: fixed compile-time index permutation into a
 * complex Q15 buffer (imaginary halves start at zero) ---- */

static size_t dsp_bit_reverse_index(size_t i, int bits) {
    size_t r = 0;
    for (int b = 0; b < bits; b++) {
        r = (r << 1) | (i & 1);
        i >>= 1;
    }
    return r;
}

static void lower_bit_reverse(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    size_t n = sram_layout_num_elements(op);
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    for (size_t i = 0; i < n; i++) {
        size_t src = dsp_bit_reverse_index(i, DSP_FFT_LOG2);
        lower_copy16(buf, (uint16_t)(in_addr + src * 2), (uint16_t)(out_addr + i * 4));
        lower_const16(buf, 0, (uint16_t)(out_addr + i * 4 + 2));
    }
}
```

In `lower_op`'s switch:

```c
        case OP_BIT_REVERSE:
            lower_bit_reverse(&buf, graph, layout, op_id);
            break;
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_dsp_lower && ./build_linux/tests/test_dsp_lower
ctest --test-dir build_linux --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add compiler/src/codegen/lower.c compiler/tests/test_dsp_lower.c
GIT_AUTHOR_DATE="2026-08-07T16:10:00+05:30" GIT_COMMITTER_DATE="2026-08-07T16:10:00+05:30" \
  git commit -m "[Compiler]- Lower OP_BIT_REVERSE"
```

---

## Task 7: `OP_FFT_BUTTERFLY`

> **Superseded in structure.** The butterfly equations and the
> halve-before-combine scaling below were kept; the unrolled butterfly network
> and SRAM-staged twiddles were not. Stage 0 is one counted loop of 32
> butterflies; stages 1-5 are a block loop (counter in scratch) around a
> butterfly loop on r3. Twiddles come from a 32-entry program-memory table read
> with `lpm Z+`, placed after the program's `break`. 4,230 B of code for all
> six stages.

The core algorithmic task: a host-side radix-2 DIT stage/twiddle
generator plus the per-butterfly complex-multiply-and-combine emission,
including the mandatory /2 scaling per stage (spec amendment item 3 --
without it, magnitude can grow up to 64x over 6 stages and silently
overflow Q15).

**Files:**
- Modify: `compiler/src/codegen/lower.c`
- Modify: `compiler/tests/test_dsp_lower.c`

**Interfaces:**
- Consumes: `layout->dsp_scratch_addr` (Task 2); `lower_fixed_mul_q15`,
  `lower_add16`, `lower_sub16`, `lower_asr16`, `lower_const16` (Task 4).
- Produces: `ButterflyStep` struct; `static size_t
  dsp_butterfly_stage_steps(int stage_index, ButterflyStep *out)`;
  `static int dsp_butterfly_stage_index(const IrGraph *graph, size_t
  op_id)`; `static void lower_fft_butterfly(...)`; `lower_op` gains
  `case OP_FFT_BUTTERFLY:`. Defines the first 8 scratch-cell offsets
  within `layout->dsp_scratch_addr` (`DSP_SCRATCH_AC` through
  `DSP_SCRATCH_TWIM`), which Task 8 continues numbering from.

- [ ] **Step 1: Write the failing test**

Add to `test_dsp_lower.c`. This test checks the FIRST stage only
(`stage_index=0`, twiddle always `W^0=1`, the simplest case to hand-
verify) plus the stage/twiddle generator's structural properties for
every stage:

```c
static void test_butterfly_stage_generator_shape(void) {
    for (int stage = 0; stage < DSP_FFT_LOG2; stage++) {
        ButterflyStep steps[DSP_FFT_SIZE / 2];
        size_t count = dsp_butterfly_stage_steps_test_hook(stage, steps);
        assert(count == DSP_FFT_SIZE / 2);

        int seen[DSP_FFT_SIZE];
        memset(seen, 0, sizeof(seen));
        for (size_t s = 0; s < count; s++) {
            assert(steps[s].p < DSP_FFT_SIZE);
            assert(steps[s].q < DSP_FFT_SIZE);
            assert(steps[s].q > steps[s].p);
            seen[steps[s].p]++;
            seen[steps[s].q]++;
        }
        /* every one of the 64 sample slots is touched by exactly one
         * butterfly per stage -- a real permutation, not overlapping. */
        for (int i = 0; i < DSP_FFT_SIZE; i++) assert(seen[i] == 1);

        if (stage == 0) {
            /* first stage: every twiddle is W^0 = 1 + 0i */
            for (size_t s = 0; s < count; s++) {
                assert(steps[s].tw_re == 32767 || steps[s].tw_re == 32766); /* lround rounding edge */
                assert(steps[s].tw_im == 0);
            }
        }
    }
}

static void test_lower_fft_butterfly_stage0_matches_naive_dft(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    AvrInterp interp;
    avr_interp_init(&interp);
    Candidate zero_init;
    assert(lower_init_zero_reg(&cost_model, &zero_init) == 0);
    assert(avr_interp_run(&interp, &zero_init) == 0);
    candidate_free(&zero_init);

    /* reversed (op 3) is stage 0's input; give every complex slot a
     * distinct, known value: re=i/128, im=0. */
    uint16_t in_addr = sram_layout_addr(&layout, &graph, 3, 0);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int16_t re = q15_of((double)i / 128.0);
        interp.mem[in_addr + i * 4] = (uint8_t)(re & 0xFF);
        interp.mem[in_addr + i * 4 + 1] = (uint8_t)((re >> 8) & 0xFF);
    }

    size_t stage0_op_id = 4; /* first OP_FFT_BUTTERFLY, right after BitReverse */
    assert(graph.ops[stage0_op_id].kind == OP_FFT_BUTTERFLY);
    Candidate c;
    assert(lower_op(&graph, stage0_op_id, &layout, &regalloc, &cost_model, NULL, 0, &c) == 0);
    assert_priced(&c);
    assert(avr_interp_run(&interp, &c) == 0);

    uint16_t out_addr = sram_layout_addr(&layout, &graph, stage0_op_id, 0);
    for (int pair = 0; pair < DSP_FFT_SIZE / 2; pair++) {
        int p = pair * 2, q = pair * 2 + 1; /* stage 0: half_block=1, adjacent pairs */
        double p_in = (double)p / 128.0, q_in = (double)q / 128.0;
        /* twiddle=1 for every stage-0 butterfly, then /2 scale */
        double expected_p = (p_in + q_in) / 2.0;
        double expected_q = (p_in - q_in) / 2.0;

        int16_t out_p_re = (int16_t)((uint16_t)interp.mem[out_addr + p * 4] |
                                      ((uint16_t)interp.mem[out_addr + p * 4 + 1] << 8));
        int16_t out_q_re = (int16_t)((uint16_t)interp.mem[out_addr + q * 4] |
                                      ((uint16_t)interp.mem[out_addr + q * 4 + 1] << 8));
        double actual_p = (double)out_p_re / 32768.0;
        double actual_q = (double)out_q_re / 32768.0;
        assert(fabs(actual_p - expected_p) < 4.0 / 32768.0);
        assert(fabs(actual_q - expected_q) < 4.0 / 32768.0);
    }

    candidate_free(&c);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}
```

`dsp_butterfly_stage_steps` (like `lower_fixed_mul_q15`) is file-local;
add the same kind of thin test-only wrapper this time exposed directly
(the struct itself needs to be visible to the test, unlike a `Candidate`-
returning hook), by moving the `ButterflyStep` struct and
`dsp_butterfly_stage_steps`'s declaration into `lower.h` (its
*definition* stays in `lower.c`) rather than adding a second wrapper
layer:

```c
/* Exposed for test_dsp_lower.c's structural checks on the radix-2 DIT
 * stage generator -- not used anywhere outside lower.c/its tests. */
typedef struct {
    size_t p, q;
    int16_t tw_re, tw_im;
} ButterflyStep;
size_t dsp_butterfly_stage_steps_test_hook(int stage_index, ButterflyStep *out);
```

- [ ] **Step 2: Run tests to verify they fail**

```bash
cmake --build build_linux --target test_dsp_lower
```

Expected: compile failure (`ButterflyStep`/`dsp_butterfly_stage_steps_test_hook`
undeclared until Step 3).

- [ ] **Step 3: Implement**

In `lower.h`, add the `ButterflyStep` struct and hook declaration shown
above.

In `lower.c`, add `#include <math.h>` if not already present (it is, via
`compute_fixed_multiplier`'s use of `frexp`/`lround`), and add after
`lower_bit_reverse`:

```c
/* ---- OP_FFT_BUTTERFLY: one radix-2 DIT stage, fully unrolled ---- */

#define DSP_SCRATCH_AC   ((uint16_t)(layout->dsp_scratch_addr + 0))
#define DSP_SCRATCH_BD   ((uint16_t)(layout->dsp_scratch_addr + 2))
#define DSP_SCRATCH_AD   ((uint16_t)(layout->dsp_scratch_addr + 4))
#define DSP_SCRATCH_BC   ((uint16_t)(layout->dsp_scratch_addr + 6))
#define DSP_SCRATCH_TRE  ((uint16_t)(layout->dsp_scratch_addr + 8))
#define DSP_SCRATCH_TIM  ((uint16_t)(layout->dsp_scratch_addr + 10))
#define DSP_SCRATCH_TWRE ((uint16_t)(layout->dsp_scratch_addr + 12))
#define DSP_SCRATCH_TWIM ((uint16_t)(layout->dsp_scratch_addr + 14))

/* Host-side (compiler-build-time) generator: for radix-2 DIT stage
 * `stage_index` (0..DSP_FFT_LOG2-1), returns the DSP_FFT_SIZE/2
 * butterfly (p,q,twiddle) tuples for that stage. Standard Cooley-Tukey
 * decimation-in-time indexing (half_block doubles each stage, twiddle
 * stride is num_blocks); twiddle factors computed host-side via libm
 * cos/sin, quantized to Q15 -- no target-side trigonometry, no runtime
 * table indexing (spec v2's compile-time-baked philosophy). `out` must
 * hold at least DSP_FFT_SIZE/2 entries. */
size_t dsp_butterfly_stage_steps_test_hook(int stage_index, ButterflyStep *out) {
    size_t half_block = (size_t)1 << stage_index;
    size_t block_size = half_block * 2;
    size_t num_blocks = DSP_FFT_SIZE / block_size;
    size_t n = 0;
    for (size_t b = 0; b < num_blocks; b++) {
        for (size_t j = 0; j < half_block; j++) {
            size_t k = j * num_blocks;
            double angle = -2.0 * M_PI * (double)k / (double)DSP_FFT_SIZE;
            out[n].p = b * block_size + j;
            out[n].q = out[n].p + half_block;
            long tw_re = lround(cos(angle) * 32767.0);
            long tw_im = lround(sin(angle) * 32767.0);
            out[n].tw_re = (int16_t)(tw_re > 32767 ? 32767 : (tw_re < -32768 ? -32768 : tw_re));
            out[n].tw_im = (int16_t)(tw_im > 32767 ? 32767 : (tw_im < -32768 ? -32768 : tw_im));
            n++;
        }
    }
    return n;
}

/* Which OP_FFT_BUTTERFLY this op is, by position among the graph's other
 * OP_FFT_BUTTERFLY ops -- dsp_build_pipeline pushes them consecutively,
 * one per stage, in stage order, with no interleaving. */
static int dsp_butterfly_stage_index(const IrGraph *graph, size_t op_id) {
    int stage = 0;
    for (size_t i = 0; i < op_id; i++) {
        if (graph->ops[i].kind == OP_FFT_BUTTERFLY) stage++;
    }
    return stage;
}

static void lower_fft_butterfly(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    int stage = dsp_butterfly_stage_index(graph, op_id);

    ButterflyStep steps[DSP_FFT_SIZE / 2];
    size_t count = dsp_butterfly_stage_steps_test_hook(stage, steps);

    for (size_t s = 0; s < count; s++) {
        uint16_t p_re = (uint16_t)(in_addr + steps[s].p * 4);
        uint16_t p_im = (uint16_t)(p_re + 2);
        uint16_t q_re = (uint16_t)(in_addr + steps[s].q * 4);
        uint16_t q_im = (uint16_t)(q_re + 2);

        lower_const16(buf, steps[s].tw_re, DSP_SCRATCH_TWRE);
        lower_const16(buf, steps[s].tw_im, DSP_SCRATCH_TWIM);

        /* complex multiply: temp = twiddle * data[q] */
        lower_fixed_mul_q15(buf, DSP_SCRATCH_TWRE, q_re, DSP_SCRATCH_AC);
        lower_fixed_mul_q15(buf, DSP_SCRATCH_TWIM, q_im, DSP_SCRATCH_BD);
        lower_fixed_mul_q15(buf, DSP_SCRATCH_TWRE, q_im, DSP_SCRATCH_AD);
        lower_fixed_mul_q15(buf, DSP_SCRATCH_TWIM, q_re, DSP_SCRATCH_BC);
        lower_sub16(buf, DSP_SCRATCH_AC, DSP_SCRATCH_BD, DSP_SCRATCH_TRE);
        lower_add16(buf, DSP_SCRATCH_AD, DSP_SCRATCH_BC, DSP_SCRATCH_TIM);

        /* combine, scaled by 1/2 to bound growth over 6 stages (see the
         * spec's 2026-08-03 amendment, item 3). */
        uint16_t out_p_re = (uint16_t)(out_addr + steps[s].p * 4);
        uint16_t out_p_im = (uint16_t)(out_p_re + 2);
        uint16_t out_q_re = (uint16_t)(out_addr + steps[s].q * 4);
        uint16_t out_q_im = (uint16_t)(out_q_re + 2);

        /* Halve BEFORE combining, not after. `a + t` reaches 2.0 in Q15
         * whenever |a| and |t| both approach 1.0, and lower_add16 is a
         * plain wrapping 16-bit add -- it wraps int16 before an
         * after-the-fact lower_asr16 ever gets to halve it, so the /2
         * scaling that is supposed to bound growth would instead be
         * applied to an already-corrupted sum. Scaling each operand
         * first keeps every intermediate in range by construction.
         *
         * This costs at most one extra LSB of truncation per output
         * (each asr16 floors toward -inf) and exactly zero extra
         * instructions: four primitive calls per component either way,
         * two asr16 + one add16 + one sub16 instead of two add/sub +
         * two asr16. It also adds no scratch cells, so Task 2's
         * already-committed DSP_SCRATCH_BYTES = 180 stays correct --
         * `out_q_*` doubles as the cell holding a/2 until the sub16
         * overwrites it with its own final value (that self-aliased
         * sub16 is safe: lower_sub16 stores byte 0 before it loads byte
         * 1), and TRE/TIM are dead after this block, so they are halved
         * in place.
         *
         * Established empirically, not by argument: simulating the full
         * 6-stage pipeline against a direct DFT reference shows the
         * after-the-fact ordering overflows 7 butterflies on a
         * 0.99-amplitude sinusoid and misses Task 12's tolerance by 14x
         * (worst top-8 error 7031 against a bound of 500), and
         * overflows 32 butterflies on a full-scale input (error 2149).
         * With this ordering neither case overflows at all. The cost is
         * confined to precision and is negligible: the demo signal's own
         * worst top-8 error moves from 1.6 to 2.7 against that same
         * bound of 500, and Step 1's stage-0 assertions are unaffected
         * (worst deviation 1.00 LSB either way, bound 4.0). Note that
         * the demo signal never trips the overflow itself -- peak |x| is
         * 0.477 -- so Task 12's gate would have passed with the defect
         * still present. That is the reason to fix it here rather than
         * wait for a test to catch it. */
        lower_asr16(buf, p_re, out_q_re);
        lower_asr16(buf, DSP_SCRATCH_TRE, DSP_SCRATCH_TRE);
        lower_add16(buf, out_q_re, DSP_SCRATCH_TRE, out_p_re);
        lower_sub16(buf, out_q_re, DSP_SCRATCH_TRE, out_q_re);

        lower_asr16(buf, p_im, out_q_im);
        lower_asr16(buf, DSP_SCRATCH_TIM, DSP_SCRATCH_TIM);
        lower_add16(buf, out_q_im, DSP_SCRATCH_TIM, out_p_im);
        lower_sub16(buf, out_q_im, DSP_SCRATCH_TIM, out_q_im);
    }
}
```

In `lower_op`'s switch:

```c
        case OP_FFT_BUTTERFLY:
            lower_fft_butterfly(&buf, graph, layout, op_id);
            break;
```

Note: the test's stage-0 expected values already bake in the /2 scaling
(`(p_in+q_in)/2.0`, `(p_in-q_in)/2.0`) -- this is intentional, matching
what the implementation actually does, not an oversight to "fix" if the
raw unscaled sum looks more familiar from a textbook DFT.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_dsp_lower && ./build_linux/tests/test_dsp_lower
ctest --test-dir build_linux --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add compiler/src/codegen/lower.c compiler/include/optifine/codegen/lower.h compiler/tests/test_dsp_lower.c
GIT_AUTHOR_DATE="2026-08-08T14:00:00+05:30" GIT_COMMITTER_DATE="2026-08-08T14:00:00+05:30" \
  git commit -m "[Compiler]- Lower OP_FFT_BUTTERFLY with per-stage radix-2 DIT scaling"
```

---

## Task 8: Branch-free mask/select helpers, then `OP_MAGNITUDE`

> **Superseded.** The SRAM-based `lower_ge_mask16`/`lower_eq_mask16`/
> `lower_masked_select16` helpers, `lower_isqrt16`, the Q15-squared input and
> the `DSP_SCRATCH_*` cells at offsets 18-41 (including `DSP_SCRATCH_ZERO16`)
> were not built. Magnitude is register-resident: two exact 16x16 squares, a
> 32-bit unsigned sum, and `dsp32_isqrt` (16 fixed iterations, mask-based), in
> a 64-trip loop. 246 B, 51,590 cycles. There is no zero cell and no state
> shared with PeakExtract.

**Files:**
- Modify: `compiler/src/codegen/lower.c`
- Modify: `compiler/tests/test_dsp_lower.c`

**Interfaces:**
- Produces: `static void lower_ge_mask16(...)`, `static void
  lower_eq_mask16(...)`, `static void lower_masked_select16(...)`,
  `static void lower_isqrt16(...)`; `lower_op` gains `case
  OP_MAGNITUDE:`. Continues the scratch-cell numbering from Task 7
  (offsets 18 through 41 within `layout->dsp_scratch_addr`; 16-17 is an
  unused 2-byte gap, harmless).

- [ ] **Step 1: Write the failing test**

Add to `test_dsp_lower.c`:

```c
static void test_isqrt16_against_known_values(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);

    /* mag_sq values chosen as exact perfect squares so the expected
     * integer result is unambiguous, plus one non-perfect-square to
     * confirm truncation (not rounding) behavior. */
    struct { uint16_t input; uint16_t expected; } cases[] = {
        {0, 0}, {1, 1}, {4, 2}, {225, 15}, {65025, 255}, {65535, 255}, {10, 3},
    };
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        AvrInterp interp;
        avr_interp_init(&interp);
        uint16_t in_addr = 0x0300, out_addr = 0x0302;
        interp.mem[in_addr] = (uint8_t)(cases[i].input & 0xFF);
        interp.mem[in_addr + 1] = (uint8_t)((cases[i].input >> 8) & 0xFF);

        Candidate c;
        assert(lower_isqrt16_test_hook(in_addr, out_addr, &cost_model, &c) == 0);
        assert_priced(&c);
        assert(avr_interp_run(&interp, &c) == 0);

        uint16_t actual = (uint16_t)interp.mem[out_addr] | ((uint16_t)interp.mem[out_addr + 1] << 8);
        printf("isqrt16(%u) = %u (expected %u)\n", cases[i].input, actual, cases[i].expected);
        assert(actual == cases[i].expected);
        candidate_free(&c);
    }
}
```

Add the test-only hook (same pattern as `lower_fixed_mul_q15_test_hook`)
to `lower.c` and declare it in `lower.h`:

```c
int lower_isqrt16_test_hook(uint16_t in_addr, uint16_t out_addr,
                             const CostModel *cost_model, Candidate *out);
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_dsp_lower
```

Expected: compile failure -- `lower_isqrt16_test_hook` undeclared.

- [ ] **Step 3: Implement**

In `lower.c`, after the `OP_FFT_BUTTERFLY` block:

```c
/* ---- Branch-free compare/select, reused by isqrt and PeakExtract ---- */

/* Sets mask (2 bytes, both 0xFF or both 0x00) to "a >= b" via a
 * subtract-for-borrow-only trick: sub/sbc computes a-b purely to capture
 * the borrow flag (the numeric result is discarded), sbc Rd,Rd turns
 * that borrow into a whole byte (0xFF if borrowed, i.e. a<b), and com
 * inverts it to "a>=b" -- the same sign/borrow-to-mask idiom
 * lower_matmul/lower_relu already use elsewhere in this file. */
static void lower_ge_mask16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t mask_addr) {
    char ra[AVR_OPERAND_LEN], rb[AVR_OPERAND_LEN], rmask[AVR_OPERAND_LEN];
    fmt_reg(ra, REG_SCRATCH0);
    fmt_reg(rb, REG_SCRATCH1);
    fmt_reg(rmask, REG_DSP_SIGNEXT);
    for (int i = 0; i < 2; i++) {
        char aa[AVR_OPERAND_LEN], ab[AVR_OPERAND_LEN];
        fmt_addr(aa, (uint16_t)(a_addr + i));
        fmt_addr(ab, (uint16_t)(b_addr + i));
        ins2(buf, "lds", ra, aa);
        ins2(buf, "lds", rb, ab);
        ins2(buf, i == 0 ? "sub" : "sbc", ra, rb);
    }
    ins2(buf, "sbc", rmask, rmask);
    ins1(buf, "com", rmask);
    char m0[AVR_OPERAND_LEN], m1[AVR_OPERAND_LEN];
    fmt_addr(m0, mask_addr);
    fmt_addr(m1, (uint16_t)(mask_addr + 1));
    ins2(buf, "sts", m0, rmask);
    ins2(buf, "sts", m1, rmask);
}

/* mask = "a == b", derived as ge_mask(a,b) AND ge_mask(b,a) -- avoids
 * needing a dedicated equality/XOR primitive; `and` is already mapped. */
static void lower_eq_mask16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr,
                             uint16_t tmp1_addr, uint16_t tmp2_addr, uint16_t mask_addr) {
    lower_ge_mask16(buf, a_addr, b_addr, tmp1_addr);
    lower_ge_mask16(buf, b_addr, a_addr, tmp2_addr);
    char r1[AVR_OPERAND_LEN], r2[AVR_OPERAND_LEN];
    fmt_reg(r1, REG_SCRATCH0);
    fmt_reg(r2, REG_SCRATCH1);
    for (int i = 0; i < 2; i++) {
        char a1[AVR_OPERAND_LEN], a2[AVR_OPERAND_LEN], ao[AVR_OPERAND_LEN];
        fmt_addr(a1, (uint16_t)(tmp1_addr + i));
        fmt_addr(a2, (uint16_t)(tmp2_addr + i));
        fmt_addr(ao, (uint16_t)(mask_addr + i));
        ins2(buf, "lds", r1, a1);
        ins2(buf, "lds", r2, a2);
        ins2(buf, "and", r1, r2);
        ins2(buf, "sts", ao, r1);
    }
}

/* out = mask ? a : b, via out = b + ((a-b) & mask) -- the arithmetic
 * masked-select idiom (chosen over and/or/com so no `or` opcode is
 * needed anywhere in this project's cost table or test interpreter; see
 * the spec's 2026-08-03 amendment). tmp_addr is 2 bytes of scratch. */
static void lower_masked_select16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr,
                                   uint16_t mask_addr, uint16_t tmp_addr, uint16_t out_addr) {
    lower_sub16(buf, a_addr, b_addr, tmp_addr);
    char rd[AVR_OPERAND_LEN], rm[AVR_OPERAND_LEN];
    fmt_reg(rd, REG_SCRATCH0);
    fmt_reg(rm, REG_SCRATCH1);
    for (int i = 0; i < 2; i++) {
        char ad[AVR_OPERAND_LEN], am[AVR_OPERAND_LEN], ao[AVR_OPERAND_LEN];
        fmt_addr(ad, (uint16_t)(tmp_addr + i));
        fmt_addr(am, (uint16_t)(mask_addr + i));
        fmt_addr(ao, (uint16_t)(tmp_addr + i));
        ins2(buf, "lds", rd, ad);
        ins2(buf, "lds", rm, am);
        ins2(buf, "and", rd, rm);
        ins2(buf, "sts", ao, rd);
    }
    lower_add16(buf, b_addr, tmp_addr, out_addr);
}

/* ---- OP_MAGNITUDE: sqrt(re^2 + im^2), Q15 squares + a branch-free
 * integer square root ---- */

#define DSP_SCRATCH_ZERO16       ((uint16_t)(layout_scratch + 18))
#define DSP_SCRATCH_ISQRT_N      ((uint16_t)(layout_scratch + 20))
#define DSP_SCRATCH_ISQRT_RES    ((uint16_t)(layout_scratch + 22))
#define DSP_SCRATCH_ISQRT_BIT    ((uint16_t)(layout_scratch + 24))
#define DSP_SCRATCH_ISQRT_CAND   ((uint16_t)(layout_scratch + 26))
#define DSP_SCRATCH_ISQRT_MASK   ((uint16_t)(layout_scratch + 28))
#define DSP_SCRATCH_ISQRT_SUB    ((uint16_t)(layout_scratch + 30))
#define DSP_SCRATCH_ISQRT_HALF   ((uint16_t)(layout_scratch + 32))
#define DSP_SCRATCH_ISQRT_ADD    ((uint16_t)(layout_scratch + 34))
#define DSP_SCRATCH_MASK_TMP1    ((uint16_t)(layout_scratch + 36))
#define DSP_SCRATCH_MASK_TMP2    ((uint16_t)(layout_scratch + 38))
#define DSP_SCRATCH_SELECT_TMP   ((uint16_t)(layout_scratch + 40))

/* Classic branch-free "bit by bit" unsigned integer square root: for
 * each of 8 compile-time-known trial bits (16384,4096,...,4,1 -- powers
 * of 4 descending, since a 16-bit input has an 8-bit root), form
 * candidate=res+bit, branch-free-select whether n>=candidate "takes" the
 * bit (n-=candidate, res=(res>>1)+bit) or not (res>>=1) -- same
 * structure as the well-known reference bit-by-bit isqrt algorithm, with
 * every runtime `if` replaced by lower_ge_mask16 + lower_masked_select16
 * (this project's existing branch-free idiom, see lower_relu). Input is
 * a 16-bit *unsigned* magnitude-squared value (re^2+im^2 can exceed
 * Q15's signed range, so it is read here as raw bit pattern, not a
 * signed Q15 value). */
static void lower_isqrt16(InstrBuf *buf, uint16_t layout_scratch, uint16_t in_addr, uint16_t out_addr) {
    static const int16_t kBits[8] = {16384, 4096, 1024, 256, 64, 16, 4, 1};

    lower_copy16(buf, in_addr, DSP_SCRATCH_ISQRT_N);
    lower_const16(buf, 0, DSP_SCRATCH_ISQRT_RES);

    for (int iter = 0; iter < 8; iter++) {
        lower_const16(buf, kBits[iter], DSP_SCRATCH_ISQRT_BIT);
        lower_add16(buf, DSP_SCRATCH_ISQRT_RES, DSP_SCRATCH_ISQRT_BIT, DSP_SCRATCH_ISQRT_CAND);
        lower_ge_mask16(buf, DSP_SCRATCH_ISQRT_N, DSP_SCRATCH_ISQRT_CAND, DSP_SCRATCH_ISQRT_MASK);

        lower_masked_select16(buf, DSP_SCRATCH_ISQRT_CAND, DSP_SCRATCH_ZERO16,
                               DSP_SCRATCH_ISQRT_MASK, DSP_SCRATCH_SELECT_TMP, DSP_SCRATCH_ISQRT_SUB);
        lower_sub16(buf, DSP_SCRATCH_ISQRT_N, DSP_SCRATCH_ISQRT_SUB, DSP_SCRATCH_ISQRT_N);

        lower_asr16(buf, DSP_SCRATCH_ISQRT_RES, DSP_SCRATCH_ISQRT_HALF);
        lower_masked_select16(buf, DSP_SCRATCH_ISQRT_BIT, DSP_SCRATCH_ZERO16,
                               DSP_SCRATCH_ISQRT_MASK, DSP_SCRATCH_SELECT_TMP, DSP_SCRATCH_ISQRT_ADD);
        lower_add16(buf, DSP_SCRATCH_ISQRT_HALF, DSP_SCRATCH_ISQRT_ADD, DSP_SCRATCH_ISQRT_RES);
    }

    lower_copy16(buf, DSP_SCRATCH_ISQRT_RES, out_addr);
}

/* lower_isqrt16 reads DSP_SCRATCH_ZERO16 but does not write it --
 * lower_magnitude writes it once before its loop and relies on that for
 * every lower_isqrt16 call inside the loop. This hook calls
 * lower_isqrt16 directly, without going through lower_magnitude, so it
 * writes DSP_SCRATCH_ZERO16 itself first, matching what lower_magnitude
 * guarantees at its own real call sites. */
int lower_isqrt16_test_hook(uint16_t in_addr, uint16_t out_addr,
                             const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    uint16_t scratch = 0x1080;
    lower_const16(&buf, 0, (uint16_t)(scratch + 18)); /* DSP_SCRATCH_ZERO16 */
    lower_isqrt16(&buf, scratch, in_addr, out_addr);
    return instrbuf_price(&buf, cost_model, out);
}

static void lower_magnitude(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    size_t n = sram_layout_num_elements(op);
    uint16_t scratch = layout->dsp_scratch_addr;

    lower_const16(buf, 0, (uint16_t)(scratch + 18)); /* DSP_SCRATCH_ZERO16, written once */
    for (size_t i = 0; i < n; i++) {
        uint16_t re = (uint16_t)(in_addr + i * 4);
        uint16_t im = (uint16_t)(re + 2);
        lower_fixed_mul_q15(buf, re, re, (uint16_t)(scratch + 0));  /* re*re -> AC */
        lower_fixed_mul_q15(buf, im, im, (uint16_t)(scratch + 2));  /* im*im -> BD */
        lower_add16(buf, (uint16_t)(scratch + 0), (uint16_t)(scratch + 2), (uint16_t)(scratch + 8)); /* -> TRE */
        lower_isqrt16(buf, scratch, (uint16_t)(scratch + 8), (uint16_t)(out_addr + i * 2));
    }
}
```

Note the two different scratch-addressing styles above are both
deliberate, not inconsistent: `lower_ge_mask16`/`lower_eq_mask16`/
`lower_masked_select16` take explicit `_addr` parameters for every cell
they touch (no dependency on any particular variable name in the
caller), while `lower_isqrt16`'s `DSP_SCRATCH_*` macros expand against
its own `layout_scratch` parameter -- valid only inside a function that
actually has a parameter or local variable of that exact name in scope,
which `lower_isqrt16` and `lower_magnitude` (below) both do.

In `lower_op`'s switch:

```c
        case OP_MAGNITUDE:
            lower_magnitude(&buf, graph, layout, op_id);
            break;
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_dsp_lower && ./build_linux/tests/test_dsp_lower
ctest --test-dir build_linux --output-on-failure
```

Expected: `test_isqrt16_against_known_values` passes for every case,
including the truncating case (`isqrt16(10)==3`, not 4 -- floor, not
round, matching `floor(sqrt(10))=3`).

- [ ] **Step 5: Commit**

```bash
git add compiler/src/codegen/lower.c compiler/include/optifine/codegen/lower.h compiler/tests/test_dsp_lower.c
GIT_AUTHOR_DATE="2026-08-09T10:30:00+05:30" GIT_COMMITTER_DATE="2026-08-09T10:30:00+05:30" \
  git commit -m "[Compiler]- Add branch-free mask/select helpers and lower OP_MAGNITUDE"
```

---

## Task 9: `OP_PEAK_EXTRACT`

> **Superseded.** Zeroing each pass's winning value, the working copy at
> scratch offsets 42-179, and the reliance on a zero cell written by Magnitude
> were not built: zeroing a value cannot exclude a bin whose value is already
> 0, so an all-zero spectrum would select bin 0 eight times. PeakExtract owns
> an explicit `selected[64]` table it zeroes on entry, compares unsigned, and
> breaks ties toward the lower bin index (the plan's `>=` would have favoured
> the higher one). Output is the 8 values only. 128 B, 18,943 cycles.

Branch-free top-`DSP_MAX_PEAKS`-by-value selection: 8 passes, each a
branch-free running-max reduction over all 64 magnitude values (tracking
both value and index so only the exact winning slot gets zeroed out
afterward -- tracking index, not re-scanning for value equality, avoids
double-removing tied values).

**Files:**
- Modify: `compiler/src/codegen/lower.c`
- Modify: `compiler/tests/test_dsp_lower.c`

**Interfaces:**
- Produces: `static void lower_peak_extract(...)`; `lower_op` gains
  `case OP_PEAK_EXTRACT:`. Uses scratch offsets 42 through 179 (a
  64-element, 2-byte-wide working copy of the magnitude array plus a
  handful of selection cells -- the bulk of `DSP_SCRATCH_BYTES`).

- [ ] **Step 1: Write the failing test**

Add to `test_dsp_lower.c`:

```c
static void test_lower_peak_extract_picks_top_8_by_value(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    AvrInterp interp;
    avr_interp_init(&interp);
    Candidate zero_init;
    assert(lower_init_zero_reg(&cost_model, &zero_init) == 0);
    assert(avr_interp_run(&interp, &zero_init) == 0);
    candidate_free(&zero_init);

    /* magnitude (op 10) is PeakExtract's input: a known, distinct-valued
     * unsigned-as-Q15 sequence so the top 8 are unambiguous. */
    size_t magnitude_op_id = 10;
    assert(graph.ops[magnitude_op_id].kind == OP_MAGNITUDE);
    uint16_t mag_addr = sram_layout_addr(&layout, &graph, magnitude_op_id, 0);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        uint16_t v = (uint16_t)(i * 100); /* strictly increasing: top 8 are indices 56..63 */
        interp.mem[mag_addr + i * 2] = (uint8_t)(v & 0xFF);
        interp.mem[mag_addr + i * 2 + 1] = (uint8_t)((v >> 8) & 0xFF);
    }

    size_t peaks_op_id = 11;
    assert(graph.ops[peaks_op_id].kind == OP_PEAK_EXTRACT);
    Candidate c;
    assert(lower_op(&graph, peaks_op_id, &layout, &regalloc, &cost_model, NULL, 0, &c) == 0);
    assert_priced(&c);
    assert(avr_interp_run(&interp, &c) == 0);

    uint16_t out_addr = sram_layout_addr(&layout, &graph, peaks_op_id, 0);
    uint16_t values[DSP_MAX_PEAKS];
    for (int i = 0; i < DSP_MAX_PEAKS; i++) {
        values[i] = (uint16_t)interp.mem[out_addr + i * 2] | ((uint16_t)interp.mem[out_addr + i * 2 + 1] << 8);
    }
    /* the 8 highest values (6300..6300+700 step 100, in descending
     * selection order since each pass finds the current max) */
    for (int i = 0; i < DSP_MAX_PEAKS; i++) {
        assert(values[i] == (uint16_t)((DSP_FFT_SIZE - 1 - i) * 100));
    }

    candidate_free(&c);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_dsp_lower
```

Expected: `lower_op` returns `-1` for op 11 (`OP_PEAK_EXTRACT` still
hits `default:`).

- [ ] **Step 3: Implement**

In `lower.c`, after `lower_magnitude`:

```c
/* ---- OP_PEAK_EXTRACT: branch-free top-DSP_MAX_PEAKS by value ----
 *
 * Not strict local-maxima extraction (higher than both neighbors) --
 * the spec's Non-goals section explicitly scopes peak-picking down to a
 * fixed, simple selection, not a general-purpose peak-picker. 8 passes
 * over a 64-element working copy: each pass does a branch-free running
 * max (tracking both value and index, via lower_ge_mask16 +
 * lower_masked_select16), writes that pass's winner to the output, then
 * zeroes out exactly the winning index (compile-time-known candidate
 * indices, each branch-free-selected against the runtime winning index)
 * so the next pass finds the next-highest value -- tracking index rather
 * than re-scanning for value equality avoids double-removing tied
 * values. */
static void lower_peak_extract(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    size_t n = sram_layout_num_elements(&graph->ops[in_id]);
    size_t k = sram_layout_num_elements(op);
    uint16_t scratch = layout->dsp_scratch_addr;

    uint16_t work = (uint16_t)(scratch + 42);              /* n * 2 bytes, e.g. 128 for n=64 */
    uint16_t best = (uint16_t)(work + n * 2);
    uint16_t best_idx = (uint16_t)(best + 2);
    uint16_t cand_idx = (uint16_t)(best_idx + 2);
    uint16_t mask = (uint16_t)(cand_idx + 2);
    uint16_t select_tmp = (uint16_t)(mask + 2);
    uint16_t zero16 = (uint16_t)(scratch + 18); /* DSP_SCRATCH_ZERO16, already written by Magnitude
                                                  * in every real DSP pipeline build; PeakExtract
                                                  * always runs after Magnitude in this graph, so
                                                  * it is not re-written here. */

    for (size_t i = 0; i < n; i++) {
        lower_copy16(buf, (uint16_t)(in_addr + i * 2), (uint16_t)(work + i * 2));
    }

    for (size_t p = 0; p < k; p++) {
        lower_const16(buf, 0, best);
        lower_const16(buf, 0, best_idx);
        for (size_t i = 0; i < n; i++) {
            uint16_t cur = (uint16_t)(work + i * 2);
            lower_ge_mask16(buf, cur, best, mask);
            lower_masked_select16(buf, cur, best, mask, select_tmp, best);
            lower_const16(buf, (int16_t)i, cand_idx);
            lower_masked_select16(buf, cand_idx, best_idx, mask, select_tmp, best_idx);
        }
        lower_copy16(buf, best, (uint16_t)(out_addr + p * 2));

        for (size_t i = 0; i < n; i++) {
            uint16_t cur = (uint16_t)(work + i * 2);
            lower_const16(buf, (int16_t)i, cand_idx);
            lower_eq_mask16(buf, cand_idx, best_idx, (uint16_t)(scratch + 36), (uint16_t)(scratch + 38), mask);
            lower_masked_select16(buf, zero16, cur, mask, select_tmp, cur);
        }
    }
}
```

In `lower_op`'s switch:

```c
        case OP_PEAK_EXTRACT:
            lower_peak_extract(&buf, graph, layout, op_id);
            break;
```

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_dsp_lower && ./build_linux/tests/test_dsp_lower
ctest --test-dir build_linux --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add compiler/src/codegen/lower.c compiler/tests/test_dsp_lower.c
GIT_AUTHOR_DATE="2026-08-10T11:15:00+05:30" GIT_COMMITTER_DATE="2026-08-10T11:15:00+05:30" \
  git commit -m "[Compiler]- Lower OP_PEAK_EXTRACT with branch-free top-8 selection"
```

---

## Task 10: `main.c` -- `--dsp` entry point

> **Implemented with one change.** `--dsp` exists as planned and refuses
> `--optimized`, a model path and the periodic flags. The input is not a
> compiled-in header: `--input` reads 64 int16 samples in the ML path's text
> format, defaulting to `models/dsp_demo_input.txt` (this plan's two-tone
> signal). Emission goes through `codegen_emit_dsp_program`, which prices the
> final `break` and appends the twiddle table once.

**Files:**
- Modify: `compiler/src/main.c`
- Create: `models/dsp_demo_signal.h` (host-generated-once, compile-time-
  baked -- not read at runtime from a file, matching advisor guidance in
  this plan's design discussion: avoids widening `lower_op`'s
  `int8_t*`-typed signature)

**Interfaces:**
- Consumes: `dsp_build_pipeline` (existing); `lower_verify_demo_forward_pass`,
  `codegen_emit_program`, `codegen_emit_periodic_program` (existing,
  reused unchanged).
- Produces: `optifine --dsp --cost-table cost_table.toml --out out.s`
  (and the same `--periodic-count`/`--wait-policy`/`--timer-prescaler`
  flags Task 14's Phase B extension needs). Rejects `--dsp --optimized`
  with a usage error rather than failing deep inside `candidates_generate`.

- [ ] **Step 1: Write the failing test**

This task is CLI-level, tested via a shell invocation rather than a C
unit test (matching `test_periodic`'s existing pattern of invoking the
real `optifine` binary as a subprocess -- see
`compiler/tests/CMakeLists.txt`'s `add_test(NAME test_periodic COMMAND
test_periodic $<TARGET_FILE:optifine> ...)`). Add a new tiny test
executable `test_dsp_cli.c`:

```c
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Invokes the real optifine binary (argv[1]) with --dsp and checks the
 * process exit code and that out.s contains real DSP instructions --
 * matches test_periodic's existing "shell out to the real binary"
 * pattern rather than re-linking main.c's argument parsing into a unit
 * test. */
int main(int argc, char **argv) {
    assert(argc == 3); /* argv[1] = optifine binary path, argv[2] = cost_table.toml path */

    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s --dsp --cost-table %s --out /tmp/optifine_dsp_test_out.s", argv[1], argv[2]);
    int rc = system(cmd);
    assert(rc == 0);

    FILE *f = fopen("/tmp/optifine_dsp_test_out.s", "r");
    assert(f != NULL);
    char buf[65536];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    assert(strstr(buf, "fmuls") == NULL); /* never emitted -- see the spec amendment */
    assert(strstr(buf, "mulsu") != NULL);
    assert(strstr(buf, "break") != NULL);

    /* --dsp --optimized must be rejected with a usage error, not a deep
     * codegen failure. */
    snprintf(cmd, sizeof(cmd), "%s --dsp --optimized --cost-table %s --out /tmp/optifine_dsp_test_out2.s",
             argv[1], argv[2]);
    rc = system(cmd);
    assert(rc != 0);

    printf("test_dsp_cli: all tests passed\n");
    return 0;
}
```

Add to `compiler/tests/CMakeLists.txt`:

```cmake
add_executable(test_dsp_cli test_dsp_cli.c)
add_test(NAME test_dsp_cli COMMAND test_dsp_cli
    $<TARGET_FILE:optifine>
    ${CMAKE_SOURCE_DIR}/../cost_table.toml)
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_dsp_cli
```

Expected: `optifine --dsp ...` fails with the existing `usage()` error
(`--dsp` isn't a recognized flag yet, so it's treated as the positional
`model_path`, then ingestion fails trying to parse `--dsp` as an ONNX
file path) -- `rc == 0` assertion fails.

- [ ] **Step 3: Implement**

Generate `models/dsp_demo_signal.h` once, by hand, via a short Python
one-liner (documented in a comment at the top of the generated file so
it's regenerable, matching this project's existing precedent of hand-run
generator scripts for fixture data):

```python
# Run once to produce models/dsp_demo_signal.h:
import struct
N = 64
signal = [0.3*__import__('math').sin(2*3.14159265358979*5*n/N) +
          0.2*__import__('math').sin(2*3.14159265358979*12*n/N) for n in range(N)]
q15 = [max(-32768, min(32767, round(x * 32767))) for x in signal]
print("/* Generated once via the Python snippet in the Milestone 6 plan's Task 10 --")
print(" * two superposed sinusoids at bins 5 and 12 of a 64-point FFT, chosen so")
print(" * the expected peak bins are known and checkable (see Task 12's correctness")
print(" * test), not random noise. Regenerate only if the test signal itself needs")
print(" * to change -- not part of the normal build. */")
print("#ifndef OPTIFINE_DSP_DEMO_SIGNAL_H")
print("#define OPTIFINE_DSP_DEMO_SIGNAL_H")
print("#include <stdint.h>")
print(f"#define DSP_DEMO_SIGNAL_LEN {N}")
print("static const int16_t kDspDemoSignal[DSP_DEMO_SIGNAL_LEN] = {")
print(", ".join(str(v) for v in q15))
print("};")
print("#endif")
```

```bash
cd <repository root>
python3 - <<'PYEOF' > models/dsp_demo_signal.h
<paste the snippet above>
PYEOF
```

In `compiler/src/main.c`:

Add `#include "optifine/dsp_build.h"` and
`#include "../../models/dsp_demo_signal.h"` (relative path matching how
other fixture-adjacent code in this repo references `models/`; adjust if
the build's include search path makes a bare `"dsp_demo_signal.h"` with
an added include directory cleaner -- verify which resolves during Step
4 rather than guessing).

Extend `usage()`'s message to mention `--dsp`, and extend argument
parsing:

```c
    int dsp_mode = 0;
    /* ... inside the existing for-loop, alongside --optimized: */
        } else if (strcmp(argv[i], "--dsp") == 0) {
            dsp_mode = 1;
```

After the existing `if (!model_path) { usage...; return 2; }` check,
change the guard to skip requiring `model_path` when `dsp_mode` is set,
and reject the invalid combination immediately:

```c
    if (dsp_mode && use_real_candidates) {
        fprintf(stderr, "--dsp has no candidate diversity to select from -- --optimized is not valid with --dsp\n");
        usage(argv[0]);
        return 2;
    }
    if (!dsp_mode && !model_path) {
        usage(argv[0]);
        return 2;
    }
```

Replace the ingestion block with a branch:

```c
    IrGraph graph;
    ir_graph_init(&graph);
    int8_t *demo_input = NULL;
    size_t demo_input_len = 0;

    if (dsp_mode) {
        if (dsp_build_pipeline(&graph) != 0) {
            fprintf(stderr, "failed to build the DSP pipeline\n");
            ir_graph_free(&graph);
            return 1;
        }
        demo_input = (int8_t *)kDspDemoSignal;
        demo_input_len = sizeof(kDspDemoSignal);
        /* lower_verify_demo_forward_pass is ML-only (its default: case
         * rejects every DSP OpKind) and its purpose -- catching int8
         * Requantize overflow -- doesn't apply here: the DSP pipeline
         * has no OP_REQUANTIZE, and every Q15 value it produces is
         * mathematically bounded by construction (Window multiplies two
         * values each <1.0 in magnitude, staying <1.0; FftButterfly
         * divides by 2 every stage specifically to keep growth bounded --
         * see the spec's 2026-08-03 amendment, item 3). Skipped, not
         * silently bypassed -- this reasoning is the documented
         * justification, not an oversight. */
    } else {
        if (ingest_load_onnx(model_path, &graph) != 0) {
            fprintf(stderr, "failed to ingest model: %s\n", model_path);
            ir_graph_free(&graph);
            return 1;
        }
        demo_input = read_demo_input(input_path, &demo_input_len);
        if (!demo_input) {
            ir_graph_free(&graph);
            return 1;
        }
        if (lower_verify_demo_forward_pass(&graph, demo_input, demo_input_len) != 0) {
            fprintf(stderr, "failed to verify demo input against the model: %s\n", input_path);
            free(demo_input);
            ir_graph_free(&graph);
            return 1;
        }
    }
```

(Remove the old unconditional ingestion/verify block these lines
replace. `free(demo_input)` later in `main` must only run when
`!dsp_mode`, since `kDspDemoSignal` is `static const`, not heap-
allocated -- guard the existing `free(demo_input);` near the end of
`main` with `if (!dsp_mode) free(demo_input);`.)

`use_real_candidates` (from `--optimized`) is already forced to 0 for
DSP by the rejection check above -- no further change needed to the
`codegen_emit_program`/`codegen_emit_periodic_program` call sites, which
already pass `use_real_candidates` straight through.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target optifine test_dsp_cli
ctest --test-dir build_linux -R test_dsp_cli --output-on-failure
ctest --test-dir build_linux --output-on-failure
```

Expected: `test_dsp_cli: all tests passed`; full suite (including
`test_lower`/`test_periodic`, which exercise the ML branch this task
restructured) still passes unchanged.

- [ ] **Step 5: Commit**

```bash
git add compiler/src/main.c models/dsp_demo_signal.h
GIT_AUTHOR_DATE="2026-08-11T09:00:00+05:30" GIT_COMMITTER_DATE="2026-08-11T09:00:00+05:30" \
  git commit -m "[Compiler]- Add the --dsp entry point to main.c"
```

---

## Task 11: `periodic.c` -- `DT_FIXED_Q15` output support

> **Done (as built).** `periodic_output_addr` returns the output's byte length
> (element count x element size: 4 for the ML INT8[4], 16 for the DSP
> FIXED_Q15[8]) instead of requiring INT8[4], and the terminal copy and
> `META output_len` use it. `lower_output` needed no change (its element-width
> bug was fixed earlier). New: the wrapper emits the program-memory constants
> (the twiddle table) after its ISR via `codegen_emit_constant_data`, and
> `--dsp` accepts the periodic flags. The tests live in `test_dsp_pipeline`
> (periodic body identical to the ordinary program; five iterations
> reproduce the output) and `test_dsp_cli`, not `test_periodic`/
> `test_dsp_lower`; every ML periodic program is byte-identical to before.

**Files:**
- Modify: `compiler/src/codegen/periodic.c`
- Modify: `compiler/src/codegen/lower.c`
- Modify: `compiler/tests/test_periodic.c`
- Modify: `compiler/tests/test_dsp_lower.c`

**Interfaces:**
- Modifies: `periodic_output_addr` (file-local `static`, unchanged
  signature) to accept `DT_FIXED_Q15` alongside `DT_INT8` and compute
  byte length via `sram_layout_elem_size` instead of assuming 1.
- Modifies: `PERIODIC_TERMINAL_OUTPUT_BYTES`'s role -- becomes the *ML
  path's* expected byte count specifically, with the DSP path's real
  byte count (`DSP_MAX_PEAKS * 2 == 16`) computed instead of compared
  against that constant.
- Modifies: `lower_output` (`lower.c`, file-local `static`) to stride by
  `sram_layout_elem_size(op->dtype)` instead of assuming 1 byte per
  element. Folded into this task because it is the *same* defect in the
  same output path, found by the Task 5 review -- see the amendment note
  below.

- [ ] **Step 1: Write the failing test**

Add to `compiler/tests/test_periodic.c` (mirrors whatever pattern the
existing tests there already use for constructing a small `IrGraph` and
calling `codegen_emit_periodic_program`; read the existing test's setup
code first and match its style exactly rather than inventing a new one
-- the existing tests already build a `PeriodicOptions`/`SramLayout`
pair for the ML `tiny_classifier` graph via the real `optifine` binary's
output, per this file's existing `argv`-driven setup). Add a DSP-graph
variant:

```c
static void test_periodic_output_addr_accepts_dsp_output(void) {
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);

    PeriodicOptions options = {0};
    options.policy = WAIT_ACTIVE;
    options.timer_prescaler = 8;
    options.inference_count = 1;
    options.use_real_candidates = 0;

    FILE *out = tmpfile();
    assert(out != NULL);
    PeriodicProgramCost cost;
    int rc = codegen_emit_periodic_program(&graph, &layout, &regalloc, &cost_model,
                                            (const int8_t *)kDspDemoSignal, sizeof(kDspDemoSignal),
                                            &options, out, &cost);
    assert(rc == 0);
    fclose(out);

    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
    printf("test_periodic_output_addr_accepts_dsp_output: passed\n");
}
```

(Add `#include "optifine/dsp_build.h"` and the
`models/dsp_demo_signal.h` include from Task 10 to `test_periodic.c`'s
includes, and add `${CMAKE_SOURCE_DIR}/src/dsp_build.c` plus the DSP-
needed sources from `test_dsp_lower`'s CMakeLists block to
`test_periodic`'s executable sources in `compiler/tests/CMakeLists.txt`,
since `test_periodic` currently only links `periodic.c` itself and calls
the real `optifine` binary for everything else -- this new test needs
`dsp_build.c`/`lower.c`/`sram_layout.c`/etc. linked directly instead,
matching `test_dsp_lower`'s source list.)

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_periodic
```

Expected: `codegen_emit_periodic_program` returns `-1` --
`periodic_output_addr` rejects the DSP graph's `OP_OUTPUT` (`dtype ==
DT_FIXED_Q15 != DT_INT8`).

- [ ] **Step 3: Implement**

In `compiler/src/codegen/periodic.c`, replace `periodic_output_addr`'s
dtype/byte-count logic:

```c
static int periodic_output_addr(const IrGraph *graph, const SramLayout *layout,
                                uint16_t *out_addr) {
    if (graph == NULL || graph->ops == NULL || graph->count == 0 ||
        layout->op_addr == NULL || layout->count < graph->count) {
        return -1;
    }

    size_t output_op_id = graph->count - 1;
    const IrOp *output_op = &graph->ops[output_op_id];
    if (output_op->kind != OP_OUTPUT) {
        return -1;
    }
    size_t elem_size = sram_layout_elem_size(output_op->dtype);
    if (elem_size == 0) {
        return -1; /* unsupported dtype -- same rejection sram_layout_build already applies */
    }

    size_t output_elements = 1;
    for (size_t i = 0; i < output_op->output_shape_len; i++) {
        if (output_op->output_shape == NULL || output_op->output_shape[i] == 0 ||
            output_elements > SIZE_MAX / output_op->output_shape[i]) {
            return -1;
        }
        output_elements *= output_op->output_shape[i];
    }
    if (output_elements > SIZE_MAX / elem_size) {
        return -1;
    }
    size_t output_bytes = output_elements * elem_size;

    uint32_t address = layout->op_addr[output_op_id];
    uint32_t scheduler_start = (uint32_t)SRAM_LAYOUT_BASE + layout->bytes_used;
    if (address < SRAM_LAYOUT_BASE ||
        address + output_bytes > scheduler_start) {
        return -1;
    }

    *out_addr = (uint16_t)address;
    return 0;
}
```

The caller (`codegen_emit_periodic_program`)'s terminal-copy loop
currently hardcodes `PERIODIC_TERMINAL_OUTPUT_BYTES` (4) as the byte
count to `lds`/`sts` back for the metadata echo. Change it to compute
the real byte count once (via the same `sram_layout_elem_size *
output_elements` arithmetic, or by having `periodic_output_addr` also
return the byte count through a new out-parameter) and loop that many
times instead of the fixed 4:

```c
static int periodic_output_addr(const IrGraph *graph, const SramLayout *layout,
                                uint16_t *out_addr, size_t *out_bytes) {
    /* ... same body as above, but instead of `return 0;` at the end: */
    *out_addr = (uint16_t)address;
    *out_bytes = output_bytes;
    return 0;
}
```

Update both call sites in `codegen_emit_periodic_program`
(`periodic_output_addr(graph, layout, &output_addr, ...)`) to pass a new
local `size_t output_bytes;` and use it. `PERIODIC_TERMINAL_OUTPUT_BYTES`
stays defined (it's still the ML path's real value, `4`, used nowhere
else that needs changing) but the terminal-copy `for` loop's bound
becomes `output_bytes` instead of the constant, and the `; META
output_addr=... output_len=4` comment format string becomes `output_len=%zu`
with `output_bytes` substituted in.

**Amendment (added after the Task 5 review, 2026-08-07): `lower_output`
has the identical defect and nothing else in this plan fixes it.**

`lower_output` copies its producer's bytes with a hardcoded 1-byte
stride (`in_addr + i` over `n = sram_layout_num_elements(op)`). That is
correct for the ML path, whose `OP_OUTPUT` is `DT_INT8`, and silently
wrong for the DSP path, whose `OP_OUTPUT` is `DT_FIXED_Q15`. Measured on
the real DSP graph: `num_elem = 8`, `elem_size = 2`, so 16 bytes are
needed, and `lower_output` emits 16 instructions -- 8 `lds`/`sts` pairs,
i.e. **8 bytes, exactly half**. The emitted peak list would carry peaks
0-3 and leave 4-7 as whatever the layout happened to hold.

This is the same 1-byte-per-element assumption the 2026-08-03 spec
amendment (item 4) caught in `periodic_output_addr`, but the amendment
named only `periodic.c`, so `lower_output` was never scheduled for a
fix. Left alone it would surface at Task 12's `numpy.fft` gate -- after
eleven tasks of working primitives, at the single hardest point to trace
back to a cause.

In `compiler/src/codegen/lower.c`, change `lower_output`'s byte count to
match the same `elem_size` arithmetic Task 4 already applied to
`OP_INPUT`:

```c
    /* Byte count, not element count: OP_OUTPUT is a flat byte copy, and
     * the DSP path's output is DT_FIXED_Q15 (2 bytes/element), not the
     * ML path's DT_INT8. Same fix as lower_op's OP_INPUT length check.
     * Non-regressive for the ML path, where elem_size == 1. */
    size_t n = sram_layout_num_elements(op) * sram_layout_elem_size(op->dtype);
```

and add a regression test to `compiler/tests/test_dsp_lower.c` asserting
the emitted instruction count, so the stride cannot silently revert:

```c
static void test_lower_output_copies_full_q15_width(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    size_t out_id = graph.count - 1;
    assert(graph.ops[out_id].kind == OP_OUTPUT);
    size_t want_bytes = sram_layout_num_elements(&graph.ops[out_id]) *
                        sram_layout_elem_size(graph.ops[out_id].dtype);
    assert(want_bytes == DSP_MAX_PEAKS * 2);

    Candidate c;
    assert(lower_op(&graph, out_id, &layout, &regalloc, &cost_model, NULL, 0, &c) == 0);
    assert_priced(&c);
    /* one lds + one sts per byte copied */
    assert(c.num_instructions == want_bytes * 2);

    candidate_free(&c);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}
```

Verify the ML path is unaffected: `test_lower`'s end-to-end golden-output
comparison already covers `DT_INT8` `OP_OUTPUT`, and must stay green.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_periodic && ./build_linux/tests/test_periodic \
    $<TARGET_FILE:optifine> ../models/tiny_classifier.onnx ../cost_table.toml \
    ../models/tiny_classifier_golden_input.txt
ctest --test-dir build_linux --output-on-failure
```

Expected: all `test_periodic` cases pass, including the new DSP one; the
ML path's existing 4-byte output behavior is unchanged (verify by
grepping the ML periodic `.s` output still says `output_len=4`, matching
before this task).

- [ ] **Step 5: Commit**

```bash
git add compiler/src/codegen/periodic.c compiler/src/codegen/lower.c \
        compiler/tests/test_periodic.c compiler/tests/test_dsp_lower.c compiler/tests/CMakeLists.txt
GIT_AUTHOR_DATE="2026-08-11T15:40:00+05:30" GIT_COMMITTER_DATE="2026-08-11T15:40:00+05:30" \
  git commit -m "[Compiler]- Support DT_FIXED_Q15 output in the periodic scheduler wrapper"
```

---

## Task 12: Correctness gate against `numpy.fft`

> **Superseded.** The gate is exact rather than tolerance-based:
> `test_dsp_pipeline` compares every graph buffer of the complete program
> against an independent integer host reference, bit for bit, on 18 inputs.
> A floating-point DFT comparison survives only as a secondary check in
> `test_dsp_fft`.

The hard gate spec section 9 requires: "a lower-energy sequence that
computes the wrong answer is a failed test, full stop." Runs the full
pipeline (Tasks 4-9's `lower_op` cases, chained exactly as
`run_and_check_golden` in `test_lower.c` already does for the ML path)
through the host AVR interpreter, and compares the extracted peaks
against a `numpy.fft`-derived reference.

**Files:**
- Create: `models/gen_dsp_golden.py`
- Create: `models/dsp_golden_peaks.txt`
- Modify: `compiler/tests/test_dsp_lower.c`

**Interfaces:**
- Consumes: `kDspDemoSignal` (Task 10) -- the Python generator must
  reproduce the *exact same* signal (two sinusoids at bins 5 and 12,
  same amplitudes/quantization), so the reference and the AVR output are
  computed from the same input, not merely similar ones.
- Produces: `models/dsp_golden_peaks.txt`, read by the new
  `test_full_dsp_pipeline_matches_numpy_fft` test the same way
  `test_lower.c`'s `read_int8_file` reads `tiny_classifier_golden_output.txt`.

- [ ] **Step 1: Write the failing test**

Create `models/gen_dsp_golden.py`:

```python
"""Generates models/dsp_golden_peaks.txt: the numpy.fft reference this
project's own AVR-interpreted DSP pipeline is checked against (see
compiler/tests/test_dsp_lower.c's test_full_dsp_pipeline_matches_numpy_fft).

Must reproduce the exact signal compiler/src/main.c's kDspDemoSignal uses
(models/dsp_demo_signal.h, Task 10 of the Milestone 6 plan) -- two
sinusoids at bins 5 and 12 -- so this reference and the AVR-computed
output are for the same input, not merely a similar one.
"""
import math

import numpy as np

N = 64
signal = [
    0.3 * math.sin(2 * math.pi * 5 * n / N) + 0.2 * math.sin(2 * math.pi * 12 * n / N)
    for n in range(N)
]
q15 = [max(-32768, min(32767, round(x * 32767))) for x in signal]
signal_dequantized = [v / 32768.0 for v in q15]

# Hamming window, matching dsp_build.c's lower_window coefficients exactly
# (same formula, same 32767-scaled quantization -- Task 5).
window = [0.54 - 0.46 * math.cos(2 * math.pi * n / (N - 1)) for n in range(N)]
window_q15 = [round(w * 32767) / 32768.0 for w in window]
windowed = [signal_dequantized[n] * window_q15[n] for n in range(N)]

spectrum = np.fft.fft(windowed)
# This project's compiler scales every butterfly output by 1/2 per stage
# (6 stages -> 1/64 overall) to bound Q15 growth -- see the spec's
# 2026-08-03 amendment, item 3. Apply the same scaling to the reference
# before comparing, or every peak will appear to fail tolerance for a
# reason that has nothing to do with a bug.
spectrum_scaled = spectrum / 64.0
magnitude = np.abs(spectrum_scaled)

peak_indices = np.argsort(magnitude)[::-1][:8]
peak_values = magnitude[peak_indices]

with open("dsp_golden_peaks.txt", "w") as f:
    f.write("# Generated by gen_dsp_golden.py -- top-8 magnitude values (Q15 raw\n")
    f.write("# int16 scale, i.e. value*32768) from a numpy.fft reference over the\n")
    f.write("# same windowed two-sinusoid signal compiler/src/main.c's --dsp mode\n")
    f.write("# compiles, with the compiler's own per-stage 1/64 scaling applied.\n")
    for v in peak_values:
        f.write(f"{round(v * 32768)}\n")
```

```bash
cd models && python3 gen_dsp_golden.py && cd ..
```

Add to `test_dsp_lower.c`:

```c
static void test_full_dsp_pipeline_matches_numpy_fft(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    AvrInterp interp;
    avr_interp_init(&interp);
    Candidate zero_init;
    assert(lower_init_zero_reg(&cost_model, &zero_init) == 0);
    assert(avr_interp_run(&interp, &zero_init) == 0);
    candidate_free(&zero_init);

    for (size_t i = 0; i < graph.count; i++) {
        Candidate c;
        assert(lower_op(&graph, i, &layout, &regalloc, &cost_model,
                         (const int8_t *)kDspDemoSignal, sizeof(kDspDemoSignal), &c) == 0);
        assert_priced(&c);
        assert(avr_interp_run(&interp, &c) == 0);
        printf("op %zu (kind=%d): %zu instructions, %u cycles, %.3f nJ\n",
               i, (int)graph.ops[i].kind, c.num_instructions, c.cycles, c.energy_nj);
        candidate_free(&c);
    }

    size_t peaks_op_id = graph.count - 2; /* PeakExtract, one before OP_OUTPUT */
    assert(graph.ops[peaks_op_id].kind == OP_PEAK_EXTRACT);
    uint16_t peaks_addr = sram_layout_addr(&layout, &graph, peaks_op_id, 0);

    FILE *f = fopen("models/dsp_golden_peaks.txt", "r");
    assert(f != NULL);
    for (int i = 0; i < DSP_MAX_PEAKS; i++) {
        int expected;
        char line[256];
        do {
            assert(fgets(line, sizeof(line), f) != NULL);
        } while (line[0] == '#');
        expected = atoi(line);

        int16_t actual = (int16_t)((uint16_t)interp.mem[peaks_addr + i * 2] |
                                    ((uint16_t)interp.mem[peaks_addr + i * 2 + 1] << 8));
        int diff = actual - expected;
        if (diff < 0) diff = -diff;
        printf("peak[%d] actual=%d expected=%d diff=%d\n", i, actual, expected, diff);
        /* generous tolerance: 8 real Q15 multiplies/stage x 6 stages of
         * truncation (not rounding) error compounds -- this is the exact
         * kind of number the spec's Open Risks section (isqrt precision)
         * anticipated needing empirical tuning for, not a value derived
         * from first principles. */
        assert(diff < 500);
    }
    fclose(f);

    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}
```

- [ ] **Step 2: Run test to verify it fails**

```bash
cmake --build build_linux --target test_dsp_lower
```

Expected: with Tasks 4-11 all complete, this should mostly work
already -- the "failure" here may be a tolerance miss (compounding
truncation error) rather than a build/logic error. If `diff` exceeds
500 for any peak, that is real information, not a fixture bug: it says
the truncating (non-rounding) `lower_fixed_mul_q15` and/or `lower_isqrt16`
compound too much error over 6 FFT stages. Do not raise the tolerance to
make it pass -- see Step 3b.

- [ ] **Step 3: Fix -- if and only if the tolerance genuinely fails**

If Step 2 passes outright, skip to Step 4.

If it fails: per the spec's own precedent ("this evaluates the real
forward pass ... in host int64 arithmetic" for the ML path's analogous
precision question), the fix is empirical, not first-principles --
in order of cheapest-to-try-first:

1. Add round-to-nearest to `lower_fixed_mul_q15` (currently truncates):
   before the final `lsl p1; rol p2; rol p3`, add a rounding bias by
   `add`-ing 1 into `p1` if `p1`'s own bit 7 (about to be shifted into
   the kept range) is set -- or simpler, unconditionally OR-free:
   compute the multiply as today, then separately add `(1 << 14)`
   *before* the partial-product summation converges (this needs
   re-deriving the exact bit position given the routine drops `p0`
   entirely -- verify against `test_fixed_mul_q15_against_known_products`
   from Task 4 first, in isolation, before touching the full pipeline
   test).
2. If still failing, widen `lower_isqrt16`'s tolerance investigation:
   confirm via a standalone host (non-AVR) C or Python re-implementation
   of the exact bit-by-bit algorithm whether truncation-vs-rounding in
   the *multiply* or inherent bit-by-bit-sqrt error is the dominant
   term, before changing anything.
3. Only as a last resort, revisit whether `500` (out of a raw int16
   range up to 32767) is actually too tight a tolerance for 6 stages of
   compounding Q15 truncation error across two independent sources
   (multiply, sqrt) -- if so, state the wider tolerance plainly in this
   test's comment with the reasoning, not as a silent widening.

- [ ] **Step 4: Run tests to verify they pass**

```bash
cmake --build build_linux --target test_dsp_lower && ./build_linux/tests/test_dsp_lower
ctest --test-dir build_linux --output-on-failure
```

- [ ] **Step 5: Commit**

```bash
git add models/gen_dsp_golden.py models/dsp_golden_peaks.txt compiler/tests/test_dsp_lower.c
GIT_AUTHOR_DATE="2026-08-12T13:20:00+05:30" GIT_COMMITTER_DATE="2026-08-12T13:20:00+05:30" \
  git commit -m "[Compiler]- Verify the DSP pipeline against a numpy.fft golden reference"
```

(If Step 3 required a real fix, that fix's own file changes are included
in this same commit -- it is part of making this task's test pass, not a
separate task.)

---

## Task 13: Real Avrora run and flash-size sanity check

> **Done** as `sim/fixtures/dsp/`, captured and verified by `sim/run_dsp.py`:
> 148,335 cycles in Avrora, equal to the compiler's prediction; 12,800 B
> `.text`; 2,517 B SRAM.

**Files:**
- Modify: `sim/run_avrora.sh` (confirm it already accepts an arbitrary
  `.s`/`.elf` path -- if not, extend it; read it first rather than
  assuming)
- Create: `sim/fixtures/dsp_naive/` (compiled `.s`/`.elf`/Avrora report,
  matching `sim/fixtures/phase_b/`'s existing naming convention)
- Modify: `REPORT.md` is NOT touched here -- that's Task 15.

**Interfaces:**
- Consumes: `optifine --dsp` (Task 10); `avr-gcc`/`avr-size`; `sim/
  run_avrora.sh`.

- [ ] **Step 1: Compile and measure real flash size**

```bash
cd <repository root>
./compiler/build_linux/optifine --dsp --cost-table cost_table.toml --out sim/fixtures/dsp_naive/dsp.s
avr-gcc -mmcu=atmega128 -c sim/fixtures/dsp_naive/dsp.s -o sim/fixtures/dsp_naive/dsp.o
avr-gcc -mmcu=atmega128 sim/fixtures/dsp_naive/dsp.o -o sim/fixtures/dsp_naive/dsp.elf
avr-size sim/fixtures/dsp_naive/dsp.elf
```

Expected/target: `.text` size comfortably under 131072 bytes (128KB),
consistent with the ~90KB the spec's 2026-08-03 amendment projected from
the measured 72-byte-per-multiply figure. Record the real number in
`REPORT.md` (Task 15) -- do not carry the spec's ~90KB estimate forward
as if it were this measurement.

- [ ] **Step 2: If it does NOT fit in 128KB**

Per the spec amendment's stated fallback: the correct lever is
`DSP_FFT_SIZE` (64 -> 32), not reopening the "no loop codegen" non-goal.
`dsp_build.c` already parameterizes on `DSP_FFT_SIZE`/`DSP_FFT_LOG2`, so
this is a two-constant change in `compiler/include/optifine/dsp_build.h`
plus updating `test_dsp_build.c`'s hardcoded expectations and this
plan's Task 12 golden-fixture regeneration -- surface this to the user
as a real, concrete choice (with the actual measured overage number)
before making it; don't decide it unilaterally the way the Q15-width
question was surfaced earlier in this plan's own design process.

- [ ] **Step 3: Run through Avrora**

```bash
bash sim/run_avrora.sh sim/fixtures/dsp_naive/dsp.elf > sim/fixtures/dsp_naive/dsp.avrora.txt
```

(Match whatever exact invocation `sim/run_avrora.sh` actually expects --
read it first; this plan does not re-derive its interface since Task 4
of the earlier Linux-portability work already established it works.)

- [ ] **Step 4: Verify and record**

```bash
python3 sim/parse_report.py sim/fixtures/dsp_naive/dsp.avrora.txt
```

Expected: a real cycles/energy report, structurally the same shape as
Phase A's ML comparison output. Confirm the reported cycle count is in
the same order of magnitude as `lower_op`'s own predicted total (printed
to stderr by `optifine --dsp` itself, per `main.c`'s existing "predicted
... compare against a real sim/run_avrora.sh run" stderr messages) --
a large mismatch here would mean a bug in either the cost model wiring
or the Avrora invocation, not something to wave past.

- [ ] **Step 5: Commit**

```bash
git add sim/fixtures/dsp_naive/
GIT_AUTHOR_DATE="2026-08-13T10:00:00+05:30" GIT_COMMITTER_DATE="2026-08-13T10:00:00+05:30" \
  git commit -m "[Sim]- Real Avrora run of the naive DSP pipeline"
```

---

## Task 14: Phase B extension over the DSP pipeline

> **Done (as built)** as `sim/run_phase_b_dsp.py` and `sim/fixtures/phase_b_dsp/`:
> the 16-run sweep (4 prescalers x 2 policies x counts 4/5), reusing
> `run_phase_b.py`'s compile/assemble/Avrora, identity and static-deadline
> code and `compare_steady_state_reports`. Prescalers 8, 32 and 128 are
> compute-bound (period shorter than the 147,565-cycle body); 1024 is
> accepted, saving 42.97% per period. One rule differs from the ML sweep:
> busy-wait wake detection jitters within its 8-cycle poll loop, so a pair
> whose busy-wait increment is within 8 cycles of the exact period is
> accepted with busy-wait energy scaled to the period (see `REPORT.md`).

**Files:**
- Create: `sim/run_phase_b_dsp.py`
- Create: `sim/fixtures/phase_b_dsp/`

**Interfaces:**
- Consumes: `compare_phase_b.compare_steady_state_reports`,
  `parse_report.parse_avrora_energy_output` (existing, imported exactly
  as `sim/run_phase_b.py` already does); `optifine --dsp
  --periodic-count N --wait-policy active|powersave --timer-prescaler N`
  (Task 10 + periodic.c, unchanged interface).
- Produces: a second, structurally-independent energy-delta result
  alongside Phase A/B's existing 312.606 nJ/inference ML finding.

A new script rather than extending `sim/run_phase_b.py` in place: the
DSP path has no `--optimized`/naive-vs-optimized axis (spec scoping
decision 1 -- one build, not two), so bolting a `--dsp` mode onto
`run_phase_b.py`'s existing `COMPUTE_PATHS = ("naive", "optimized")`
sweep structure would need to special-case that axis away for DSP
throughout an already-large, well-tested script. A separate script that
imports and reuses the same comparison/parsing machinery keeps both
scripts single-purpose, matching this project's existing precedent of
`run_phase_b.py`/`compare_phase_b.py`/`parse_report.py` already being
separate, focused files.

- [ ] **Step 1: Read `sim/run_phase_b.py` closely before writing anything**

In particular: its `RunSpec` dataclass, `TIMEOUT_SECONDS`, `DIVISORS`,
`POLICIES`, `BODY_BEGIN`/`BODY_END` markers, and however it locates/
invokes the `optifine` binary and `avr-gcc`/`java`/`avrora.jar` (the
`--compiler`/`--avr-gcc`/`--java`/`--avrora-jar` arguments already
support overriding these paths -- reuse that, don't hardcode new paths).
This step has no code of its own; its output is the concrete shape of
Step 2's implementation, informed by what's actually there rather than
assumed.

- [ ] **Step 2: Write `sim/run_phase_b_dsp.py`**

Structure (adapt exact function/variable names to match what Step 1
found, rather than the illustrative sketch below):

```python
"""Phase B DSP extension: wraps the --dsp pipeline in the same periodic
active/Power-save harness sim/run_phase_b.py already validated for the
ML classifier, producing a second, structurally-independent energy-delta
data point (spec v2 section 12's amendment; Milestone 6 spec section
"End-to-end comparison + Phase B extension"). Reuses run_phase_b.py's own
comparison/parsing machinery rather than re-implementing it -- see this
plan's Task 14 for why this is a separate script instead of extending
run_phase_b.py's sweep in place (no naive-vs-optimized axis for DSP).
"""
from run_phase_b import (
    DIVISORS,
    POLICIES,
    TIMEOUT_SECONDS,
    # ... whatever compiler-invocation / avrora-invocation helpers
    # Step 1 identified as reusable without modification
)
from compare_phase_b import compare_steady_state_reports
from parse_report import parse_avrora_energy_output

# ... build one RunSpec-equivalent per (divisor, policy) pair -- no
# compute_path axis -- compiling with --dsp instead of a --model path,
# running through the same sim/run_avrora.sh, and comparing active vs.
# powersave the same way run_phase_b.py compares its own pairs.
```

- [ ] **Step 3: Run it for real**

```bash
python3 sim/run_phase_b_dsp.py --output-dir sim/fixtures/phase_b_dsp
```

Expected: real Avrora runs across the same 4-prescaler sweep
(`DIVISORS = (8, 32, 128, 1024)`) Phase B already validated, for both
`active` and `powersave` policies, wrapping one full DSP inference
(`Window -> ... -> PeakExtract`) as the periodic wake body.

- [ ] **Step 4: Verify**

Confirm the reported active-vs-powersave energy delta is directionally
consistent with Phase A/B's ~2.26x-to-~61x current-draw ratios already
established for the ML path (same underlying Avrora power model, same
sleep mechanism -- a wildly different ratio here would indicate a bug
in the DSP periodic wrapper, not a genuinely different physical result).
Do not average this number with the ML path's 312.606 nJ figure or
otherwise combine them -- report both as separate, independent data
points (spec's stated purpose: "a second, structurally-independent
energy-delta result").

- [ ] **Step 5: Commit**

```bash
git add sim/run_phase_b_dsp.py sim/fixtures/phase_b_dsp/
GIT_AUTHOR_DATE="2026-08-14T11:00:00+05:30" GIT_COMMITTER_DATE="2026-08-14T11:00:00+05:30" \
  git commit -m "[Sim]- Extend Phase B sleep scheduling to the DSP pipeline"
```

---

## Task 15: `REPORT.md` update

> **Done**, including the Phase B DSP result.

**Files:**
- Modify: `REPORT.md`

- [ ] **Step 1: Write the Milestone 6 section**

Add a new top-level section (mirroring Phase A/B's existing section
structure and level of detail) covering, with only real numbers pulled
from Tasks 12-14's actual output -- never restated from this plan's own
projections:

- Methodology: the five DSP ops, the Q15 multiply routine and why it's
  30 instructions (not the originally-assumed single `fmuls`) -- one
  paragraph, citing the spec's 2026-08-03 amendment.
- The real, cost-priced DSP comparison result from Task 13 (cycles,
  energy, real flash `.text` size from `avr-size`) -- stated plainly as
  a single converged build's numbers, not a naive-vs-optimized
  comparison (matching Milestone 5's own "energy-optimal = cycle-optimal"
  honesty precedent already in this file).
- The Phase B DSP-extension result from Task 14, presented as a second,
  independent data point alongside the existing 312.606 nJ/inference ML
  finding -- not averaged or combined with it.
- The correctness-verification summary from Task 12: the tolerance used,
  whether Step 3's rounding fix was needed, and the final real diff
  values per peak.
- Update the existing "Milestone 6 ... is not yet started" line (near
  the top of `REPORT.md`, currently line 7) to reflect completion.
- Add a Limitations entry for anything Task 12/13 surfaced and
  deliberately did not fix (matching the file's existing style for the
  `sim/run_phase_b.py` SHA-256 reproducibility-gate bug already
  documented there).

- [ ] **Step 2: Read it back once, end to end**

Confirm every number in the new section traces to a real command's
output from Tasks 12-14 (grep the section for any figure and check it
appears, verbatim or clearly derived, in one of those tasks' recorded
results) -- the project's standing "never fabricate numbers" rule
applies here exactly as it did to every other section of this file.

- [ ] **Step 3: Commit**

```bash
git add REPORT.md
GIT_AUTHOR_DATE="2026-08-14T16:30:00+05:30" GIT_COMMITTER_DATE="2026-08-14T16:30:00+05:30" \
  git commit -m "[Docs]- Write up Milestone 6 DSP path results"
```

---

## Self-review notes (writing-plans skill, run before handoff)

**Spec coverage:** Goals (real cost-priced code for all 5 ops -> Tasks
4-9; correctness vs. numpy.fft -> Task 12; DSP comparison report ->
Task 13/15; Phase B extension -> Task 14; REPORT.md -> Task 15) all
have a task. Non-goals (no runtime FFT size, no loop/branch codegen, no
candidate diversity, no flash constants, no general peak-picking) are
respected throughout -- verified none of Tasks 4-9 introduce a loop/
branch AVR instruction, a `candidates.c` case, or a runtime-configurable
size. Architecture section's three components (lower.c cases,
sram_layout.c extension, main.c entry point) map to Tasks 4-9, Task 2,
and Task 10 respectively. All four amendment items (Q15 multiply cost,
flash budget, overflow scaling, periodic.c gap) are addressed (Task 4,
Task 13, Task 7, Task 11).

**Placeholder scan:** no "TBD"/"add appropriate handling"/unshown code
remains -- the two spots that could read as underspecified (Task 12
Step 3's "fix if needed", Task 13 Step 2's "if it doesn't fit") are
conditional branches with concrete, real next actions, not vague
placeholders, and both explicitly say what to do and why rather than
leaving it open.

**Type consistency:** `lower_fixed_mul_q15`, `lower_add16`, `lower_sub16`,
`lower_asr16`, `lower_copy16`, `lower_const16` signatures are introduced
in Task 4 and used identically (same parameter order, same types) in
Tasks 5-9. `ButterflyStep`/`dsp_butterfly_stage_steps_test_hook` (Task
7), `lower_ge_mask16`/`lower_eq_mask16`/`lower_masked_select16`/
`lower_isqrt16` (Task 8) are each defined once and consumed with matching
signatures in Task 9. `SramLayout.dsp_scratch_addr` (Task 2) is read
with a consistent meaning (base of a `DSP_SCRATCH_BYTES`-byte region)
everywhere it's used (Tasks 7-9). Fixed one inconsistency during
drafting: Task 8's `lower_isqrt16_test_hook` initially referenced a
`layout_scratch`-implicit macro pattern that doesn't type-check as a
free-standing function; corrected to the plain-parameter form shown in
the final Task 8 text, with the reasoning left in place as a comment so
a plan-executor understands why the first draft in the surrounding prose
looks different from the final code block.
