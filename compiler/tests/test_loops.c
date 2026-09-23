/* Contract tests for the DSP path's counted-loop machinery.
 *
 * The point of loops here is flash size, and the risk they introduce is that
 * emitted code and executed cost stop being the same thing. These tests pin
 * both halves: the interpreter must really execute the body `trip` times
 * following branches, and instrbuf_price must report what executes, not what
 * is emitted -- checked against a hand-derived hardware cycle count rather
 * than against the implementation's own arithmetic. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/registers.h"
#include "optifine/cost_model.h"

#include "avr_interp.h"

#define CYCLE_NJ 2.8375
#define COUNTER 20

static void load_costs(CostModel *cm) {
    const char *path = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(path ? path : "cost_table.toml", cm) == 0);
}

/* Body used by several tests: mem[addr] += 1, four instructions. */
static void emit_increment(InstrBuf *b, uint16_t addr) {
    char rv[AVR_OPERAND_LEN], rone[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(rv, REG_SCRATCH0);
    fmt_reg(rone, REG_SCRATCH1);
    fmt_addr(a, addr);
    fmt_imm(imm, 1);
    ins2(b, "lds", rv, a);
    ins2(b, "ldi", rone, imm);
    ins2(b, "add", rv, rone);
    ins2(b, "sts", a, rv);
}

static void test_loop_executes_body_trip_times(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    LoopCtx loop;
    instrbuf_loop_begin(&b, &loop, 64, COUNTER);
    emit_increment(&b, 0x0300);
    instrbuf_loop_end(&b, &loop);

    Candidate c;
    assert(instrbuf_price(&b, &cm, &c) == 0);
    AvrInterp in; avr_interp_init(&in);
    assert(avr_interp_run(&in, &c) == 0);
    assert(in.mem[0x0300] == 64);
    candidate_free(&c);
}

static void test_loop_emits_far_less_than_unrolling(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf u; instrbuf_init(&u);
    for (int i = 0; i < 64; i++) emit_increment(&u, 0x0300);
    Candidate cu; assert(instrbuf_price(&u, &cm, &cu) == 0);

    InstrBuf l; instrbuf_init(&l);
    LoopCtx loop;
    instrbuf_loop_begin(&l, &loop, 64, COUNTER);
    emit_increment(&l, 0x0300);
    instrbuf_loop_end(&l, &loop);
    Candidate cl; assert(instrbuf_price(&l, &cm, &cl) == 0);

    printf("unrolled: %zu emitted, %u cycles | looped: %zu emitted, %u cycles\n",
           cu.num_instructions, cu.cycles, cl.num_instructions, cl.cycles);
    assert(cu.num_instructions == 256);
    /* ldi + label + 4 body + dec + brne. The label is one array entry but
     * occupies no flash and costs no cycles; at eight entries against 256 it
     * is not worth excluding from the count. */
    assert(cl.num_instructions == 8);
    assert(cl.num_instructions * 8 < cu.num_instructions);

    /* Both compute the same thing, so both must leave the same memory. */
    AvrInterp a, b2;
    avr_interp_init(&a); avr_interp_init(&b2);
    assert(avr_interp_run(&a, &cu) == 0);
    assert(avr_interp_run(&b2, &cl) == 0);
    assert(memcmp(a.mem, b2.mem, AVR_INTERP_MEM_SIZE) == 0);
    candidate_free(&cu); candidate_free(&cl);
}

static void test_loop_cycles_match_hand_derived_hardware_count(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    LoopCtx loop;
    instrbuf_loop_begin(&b, &loop, 64, COUNTER);
    emit_increment(&b, 0x0300);
    instrbuf_loop_end(&b, &loop);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);

    /* Derived from the AVR Instruction Set Manual, independently of the
     * pricing code: lds 2 + ldi 1 + add 1 + sts 2 = 6 cycles of body.
     *   ldi counter          1
     *   64 x (body + dec)    64 * (6 + 1)
     *   brne taken 63 times  63 * 2
     *   brne falls through    1
     * A loop whose brne was priced at the taken cost throughout would report
     * exactly one cycle more than this. */
    const uint32_t expected = 1 + 64 * (6 + 1) + 63 * 2 + 1;
    printf("looped cycles: reported %u, hand-derived %u\n", c.cycles, expected);
    assert(c.cycles == expected);
    candidate_free(&c);
}

static void test_nested_loops_multiply_trip_counts(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    LoopCtx outer, inner;
    instrbuf_loop_begin(&b, &outer, 8, COUNTER);
    instrbuf_loop_begin(&b, &inner, 4, COUNTER + 1);
    emit_increment(&b, 0x0300);
    instrbuf_loop_end(&b, &inner);
    instrbuf_loop_end(&b, &outer);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);

    AvrInterp in; avr_interp_init(&in);
    assert(avr_interp_run(&in, &c) == 0);
    printf("nested 8x4: mem=%u (expect 32), emitted=%zu\n", in.mem[0x0300], c.num_instructions);
    assert(in.mem[0x0300] == 32);
    candidate_free(&c);
}

static void test_pointer_post_increment_walks_an_array(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    char zl[AVR_OPERAND_LEN], zh[AVR_OPERAND_LEN], xl[AVR_OPERAND_LEN], xh[AVR_OPERAND_LEN];
    char v[AVR_OPERAND_LEN], zp[AVR_OPERAND_LEN], xp[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(zl, 30); fmt_reg(zh, 31); fmt_reg(xl, 26); fmt_reg(xh, 27);
    fmt_reg(v, REG_SCRATCH0);
    fmt_ptr(zp, 'Z', 1); fmt_ptr(xp, 'X', 1);

    fmt_lo8(imm, 0x0400); ins2(&b, "ldi", zl, imm);
    fmt_hi8(imm, 0x0400); ins2(&b, "ldi", zh, imm);
    fmt_lo8(imm, 0x0500); ins2(&b, "ldi", xl, imm);
    fmt_hi8(imm, 0x0500); ins2(&b, "ldi", xh, imm);

    LoopCtx loop;
    instrbuf_loop_begin(&b, &loop, 16, COUNTER);
    ins2(&b, "ld", v, zp);
    ins2(&b, "st", xp, v);
    instrbuf_loop_end(&b, &loop);

    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    AvrInterp in; avr_interp_init(&in);
    for (int i = 0; i < 16; i++) in.mem[0x0400 + i] = (uint8_t)(i * 3 + 1);
    assert(avr_interp_run(&in, &c) == 0);
    for (int i = 0; i < 16; i++) assert(in.mem[0x0500 + i] == (uint8_t)(i * 3 + 1));
    /* pointers ended up past the last element they touched */
    assert(((uint16_t)in.regs[30] | ((uint16_t)in.regs[31] << 8)) == 0x0410);
    candidate_free(&c);
}

static void test_displacement_addressing_reads_within_an_element(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    char zl[AVR_OPERAND_LEN], zh[AVR_OPERAND_LEN], v[AVR_OPERAND_LEN];
    char d0[AVR_OPERAND_LEN], d2[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(zl, 30); fmt_reg(zh, 31); fmt_reg(v, REG_SCRATCH0);
    fmt_ptr_disp(d0, 'Z', 0); fmt_ptr_disp(d2, 'Z', 2);
    fmt_lo8(imm, 0x0400); ins2(&b, "ldi", zl, imm);
    fmt_hi8(imm, 0x0400); ins2(&b, "ldi", zh, imm);
    ins2(&b, "ldd", v, d2);
    fmt_addr(a, 0x0600); ins2(&b, "sts", a, v);
    ins2(&b, "ldd", v, d0);
    fmt_addr(a, 0x0601); ins2(&b, "sts", a, v);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    AvrInterp in; avr_interp_init(&in);
    in.mem[0x0400] = 0xAA; in.mem[0x0402] = 0xBB;
    assert(avr_interp_run(&in, &c) == 0);
    assert(in.mem[0x0600] == 0xBB);
    assert(in.mem[0x0601] == 0xAA);
    /* ldd must not advance the pointer */
    assert(((uint16_t)in.regs[30] | ((uint16_t)in.regs[31] << 8)) == 0x0400);
    candidate_free(&c);
}

static void test_label_is_free_and_every_new_opcode_is_priced(void) {
    static const char *opcodes[] = {"dec", "brne", "ld", "ldd", "st", "std",
                                    "adiw", "sbiw", "movw"};
    CostModel cm; load_costs(&cm);
    for (size_t i = 0; i < sizeof(opcodes) / sizeof(opcodes[0]); i++) {
        const char *cat = avr_cost_category(opcodes[i]);
        assert(cat != NULL);
        assert(cost_model_lookup(&cm, cat) != NULL);
    }
    /* A label adds an emitted entry but no cost. */
    InstrBuf b; instrbuf_init(&b);
    char r[AVR_OPERAND_LEN]; fmt_reg(r, REG_SCRATCH0);
    ins1(&b, "dec", r);
    ins1(&b, AVR_LABEL_MNEMONIC, ".Lfree");
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    assert(c.num_instructions == 2);
    assert(c.cycles == 1);
    candidate_free(&c);
}

/* --- Program-memory (lpm) microfixture ---
 *
 * The DSP path bakes its canonical 32-entry twiddle table into flash and reads
 * it with lpm. Three things have to hold and none may be assumed: lo8/hi8 must
 * give the BYTE address lpm wants (pm_lo8/pm_hi8 give the word address and
 * would read the wrong half), the four bytes per entry must reconstruct
 * little-endian into two int16s, and the table must sit where control flow
 * cannot execute it. */

#define TW_ENTRIES 32
#define TW_LABEL ".Ltw"

static void twiddle_q15(int k, int16_t *wr, int16_t *wi) {
    double a = -2.0 * 3.14159265358979323846 * (double)k / 64.0;
    long r = lround(cos(a) * 32767.0), i = lround(sin(a) * 32767.0);
    *wr = (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
    *wi = (int16_t)(i > 32767 ? 32767 : (i < -32768 ? -32768 : i));
}

/* Emits: load entry k into r16..r19, halt, then the table. */
static void build_twiddle_probe_padded(InstrBuf *b, int k, int pad_words) {
    char zl[AVR_OPERAND_LEN], zh[AVR_OPERAND_LEN], zp[AVR_OPERAND_LEN], reg[AVR_OPERAND_LEN];
    char sym[AVR_OPERAND_LEN];
    fmt_reg(zl, 30); fmt_reg(zh, 31); fmt_ptr(zp, 'Z', 1);
    fmt_reg(reg, REG_SCRATCH1);
    for (int i = 0; i < pad_words; i++) ins2(b, "mov", reg, reg); /* pushes the table up in flash */
    fmt_lo8_sym(sym, TW_LABEL, k * 4); ins2(b, "ldi", zl, sym);
    fmt_hi8_sym(sym, TW_LABEL, k * 4); ins2(b, "ldi", zh, sym);
    for (int i = 0; i < 4; i++) { fmt_reg(reg, 16 + i); ins2(b, "lpm", reg, zp); }
    /* store them so the test can read them back out of SRAM */
    for (int i = 0; i < 4; i++) {
        char a[AVR_OPERAND_LEN]; fmt_addr(a, (uint16_t)(0x0300 + i));
        fmt_reg(reg, 16 + i); ins2(b, "sts", a, reg);
    }
    ins0(b, "break");
    /* table AFTER break: unreachable by fall-through */
    ins1(b, AVR_LABEL_MNEMONIC, TW_LABEL);
    for (int e = 0; e < TW_ENTRIES; e++) {
        int16_t wr, wi; twiddle_q15(e, &wr, &wi);
        ins_data_word(b, (uint16_t)wr);
        ins_data_word(b, (uint16_t)wi);
    }
}

static void build_twiddle_probe(InstrBuf *b, int k) { build_twiddle_probe_padded(b, k, 0); }

static void test_lpm_reads_canonical_twiddles(void) {
    CostModel cm; load_costs(&cm);
    const int probes[] = {0, 1, 16, 31};
    for (size_t t = 0; t < sizeof(probes) / sizeof(probes[0]); t++) {
        int k = probes[t];
        InstrBuf b; instrbuf_init(&b);
        build_twiddle_probe(&b, k);
        Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
        AvrInterp in; avr_interp_init(&in);
        assert(avr_interp_run(&in, &c) == 0);

        int16_t wr_exp, wi_exp; twiddle_q15(k, &wr_exp, &wi_exp);
        int16_t wr = (int16_t)((uint16_t)in.mem[0x0300] | ((uint16_t)in.mem[0x0301] << 8));
        int16_t wi = (int16_t)((uint16_t)in.mem[0x0302] | ((uint16_t)in.mem[0x0303] << 8));
        printf("  lpm k=%2d -> wr=%6d wi=%6d (expect %6d %6d)\n", k, wr, wi, wr_exp, wi_exp);
        assert(wr == wr_exp);
        assert(wi == wi_exp);
        candidate_free(&c);
    }
}

static void test_twiddle_table_is_below_the_lpm_64k_boundary(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    build_twiddle_probe(&b, 0);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    /* Recompute the table's flash byte address the same way the image is laid
     * out. Without ELPM/RAMPZ, Z can only reach below 0x10000, so this is a
     * hard precondition of the whole approach, not a nicety. */
    size_t addr = 0, table = (size_t)-1;
    for (size_t i = 0; i < c.num_instructions; i++) {
        const AvrInstr *ins = &c.instructions[i];
        if (avr_instr_is_label(ins) && strcmp(ins->operands[0], TW_LABEL) == 0) { table = addr; break; }
        if (avr_instr_is_data_word(ins)) addr += 2;
        else if (strcmp(ins->mnemonic, "lds") == 0 || strcmp(ins->mnemonic, "sts") == 0) addr += 4;
        else if (!avr_instr_is_label(ins)) addr += 2;
    }
    printf("  twiddle table at flash byte 0x%04X (limit 0x10000)\n", (unsigned)table);
    assert(table != (size_t)-1);
    assert(table < 0x10000u);
    candidate_free(&c);
}

static void test_table_cannot_be_reached_by_fall_through(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    build_twiddle_probe(&b, 0);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    /* Two structural guarantees, checked rather than argued: the table label
     * comes after the halting `break`, and no branch anywhere targets it. */
    size_t brk = (size_t)-1, label = (size_t)-1;
    for (size_t i = 0; i < c.num_instructions; i++) {
        const AvrInstr *ins = &c.instructions[i];
        if (strcmp(ins->mnemonic, "break") == 0 && brk == (size_t)-1) brk = i;
        if (avr_instr_is_label(ins) && strcmp(ins->operands[0], TW_LABEL) == 0) label = i;
        if (strcmp(ins->mnemonic, "brne") == 0) assert(strcmp(ins->operands[0], TW_LABEL) != 0);
    }
    assert(brk != (size_t)-1 && label != (size_t)-1 && label > brk);
    /* And the interpreter refuses to execute a data word if it ever happened. */
    candidate_free(&c);
}

static void test_lpm_is_priced_at_three_cycles(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    char zp[AVR_OPERAND_LEN], reg[AVR_OPERAND_LEN];
    fmt_ptr(zp, 'Z', 1); fmt_reg(reg, 16);
    ins2(&b, "lpm", reg, zp);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    /* Confirmed against Avrora's own trace: lpmpi runs cycle 2 -> 5. */
    assert(c.cycles == 3);
    candidate_free(&c);
}

/* Regression: the interpreter's program image used to be sized like SRAM
 * (0x1100 bytes) and silently skipped any .dw beyond it, so the six-stage
 * FFT -- over 4 KB of code before its table -- read the wrong bytes. Here the
 * table sits past 0x1100 and must still read back exactly. */
static void test_lpm_reads_a_table_beyond_0x1100(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    build_twiddle_probe_padded(&b, 31, 0x1200 / 2);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    AvrInterp *in = malloc(sizeof(AvrInterp));
    avr_interp_init(in);
    assert(avr_interp_run(in, &c) == 0);
    int16_t wr_exp, wi_exp; twiddle_q15(31, &wr_exp, &wi_exp);
    int16_t wr = (int16_t)((uint16_t)in->mem[0x0300] | ((uint16_t)in->mem[0x0301] << 8));
    int16_t wi = (int16_t)((uint16_t)in->mem[0x0302] | ((uint16_t)in->mem[0x0303] << 8));
    printf("  lpm past 0x1100 (image 0x%zX B): k=31 -> %d %d (expect %d %d)\n",
           in->progmem_size, wr, wi, wr_exp, wi_exp);
    assert(in->progmem_size > 0x1100);
    assert(wr == wr_exp && wi == wi_exp);
    free(in);
    candidate_free(&c);
}

/* An SRAM-held counter, as the FFT's block loop uses: 8 outer x 4 inner with
 * the inner counter in a register. Cycles derived by hand from the AVR
 * Instruction Set Manual:
 *   outer init        ldi 1 + sts 2                               3
 *   per outer iter    inner (ldi 1 + 4x(6+1) + 3x2 + 1 = 36)
 *                     + lds 2 + dec 1 + sts 2                    41
 *   outer brne        taken 7 x 2, falls through once x 1        15
 * total 3 + 8*41 + 15 = 346. */
static void test_sram_counter_nested_loop(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    LoopCtx outer, inner;
    instrbuf_loop_begin_sram(&b, &outer, 8, 0x0400);
    instrbuf_loop_begin(&b, &inner, 4, COUNTER);
    emit_increment(&b, 0x0300);
    instrbuf_loop_end(&b, &inner);
    instrbuf_loop_end(&b, &outer);
    assert(!b.loops[outer.region].is_long && !b.loops[inner.region].is_long);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    AvrInterp in; avr_interp_init(&in);
    assert(avr_interp_run(&in, &c) == 0);
    printf("  sram-counter 8x4: mem=%u (expect 32), counter cell=%u, cycles %u predicted, %lu run, 346 by hand\n",
           in.mem[0x0300], in.mem[0x0400], c.cycles, in.cycles);
    assert(in.mem[0x0300] == 32);
    assert(in.mem[0x0400] == 0);
    assert(c.cycles == 346 && in.cycles == 346);
    candidate_free(&c);
}

/* Closure forms are recorded per loop: here the outer loop is long (padded
 * past BRNE's reach) while the inner one stays short. By hand:
 *   outer init 3; per outer iter 36 (inner) + 70 (pad) + 5 (lds/dec/sts) = 111;
 *   outer close: breq not taken + rjmp (1 + 2) twice, breq taken (2) once.
 * total 3 + 3*111 + 2*3 + 2 = 344. */
static void test_long_outer_around_short_inner(void) {
    CostModel cm; load_costs(&cm);
    InstrBuf b; instrbuf_init(&b);
    LoopCtx outer, inner;
    char pad[AVR_OPERAND_LEN]; fmt_reg(pad, REG_SCRATCH1);
    instrbuf_loop_begin_sram(&b, &outer, 3, 0x0400);
    instrbuf_loop_begin(&b, &inner, 4, COUNTER);
    emit_increment(&b, 0x0300);
    instrbuf_loop_end(&b, &inner);
    for (int i = 0; i < 70; i++) ins2(&b, "mov", pad, pad);
    instrbuf_loop_end(&b, &outer);
    assert(b.loops[outer.region].is_long && !b.loops[inner.region].is_long);
    Candidate c; assert(instrbuf_price(&b, &cm, &c) == 0);
    AvrInterp in; avr_interp_init(&in);
    assert(avr_interp_run(&in, &c) == 0);
    printf("  long outer / short inner 3x4: mem=%u, cycles %u predicted, %lu run, 344 by hand\n",
           in.mem[0x0300], c.cycles, in.cycles);
    assert(in.mem[0x0300] == 12);
    assert(c.cycles == 344 && in.cycles == 344);
    candidate_free(&c);
}

int main(void) {
    test_loop_executes_body_trip_times();
    test_loop_emits_far_less_than_unrolling();
    test_loop_cycles_match_hand_derived_hardware_count();
    test_nested_loops_multiply_trip_counts();
    test_pointer_post_increment_walks_an_array();
    test_displacement_addressing_reads_within_an_element();
    test_label_is_free_and_every_new_opcode_is_priced();
    test_lpm_reads_canonical_twiddles();
    test_twiddle_table_is_below_the_lpm_64k_boundary();
    test_table_cannot_be_reached_by_fall_through();
    test_lpm_is_priced_at_three_cycles();
    test_lpm_reads_a_table_beyond_0x1100();
    test_sram_counter_nested_loop();
    test_long_outer_around_short_inner();
    printf("test_loops: all tests passed\n");
    return 0;
}
