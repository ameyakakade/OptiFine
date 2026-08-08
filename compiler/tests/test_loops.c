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

int main(void) {
    test_loop_executes_body_trip_times();
    test_loop_emits_far_less_than_unrolling();
    test_loop_cycles_match_hand_derived_hardware_count();
    test_nested_loops_multiply_trip_counts();
    test_pointer_post_increment_walks_an_array();
    test_displacement_addressing_reads_within_an_element();
    test_label_is_free_and_every_new_opcode_is_priced();
    printf("test_loops: all tests passed\n");
    return 0;
}
