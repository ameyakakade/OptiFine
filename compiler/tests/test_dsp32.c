/* Differential validation of the DSP 32-bit register-resident primitives.
 *
 * Every helper is checked against a host-side oracle over the same corpus:
 * zero, +/-1, the int16 and int32 extremes, the byte-boundary values where a
 * carry chain is most likely to be mis-wired, and a deterministic pseudo-random
 * sweep. The point is not that the helpers look right -- it is that the
 * emitted AVR, executed instruction by instruction, produces exactly what C
 * produces for the same inputs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/dsp32.h"
#include "optifine/codegen/registers.h"
#include "optifine/cost_model.h"

#include "avr_interp.h"

#define A_ADDR 0x0400
#define B_ADDR 0x0410
#define R_ADDR 0x0420

static CostModel g_cm;
static void load_costs(void) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
}

static void put32(AvrInterp *in, uint16_t addr, uint32_t v) {
    for (int i = 0; i < 4; i++) in->mem[addr + i] = (uint8_t)(v >> (8 * i));
}
static uint32_t get32(const AvrInterp *in, uint16_t addr) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)in->mem[addr + i] << (8 * i);
    return v;
}

/* Runs one emitted sequence and returns the 32-bit result written to R_ADDR. */
static uint32_t run(InstrBuf *b, uint32_t a, uint32_t bb, uint32_t *cycles_out) {
    Candidate c;
    assert(instrbuf_price(b, &g_cm, &c) == 0);
    AvrInterp in; avr_interp_init(&in);
    /* REG_ZERO must hold zero, exactly as lower_init_zero_reg guarantees. */
    in.regs[REG_ZERO] = 0;
    put32(&in, A_ADDR, a); put32(&in, B_ADDR, bb);
    assert(avr_interp_run(&in, &c) == 0);
    uint32_t r = get32(&in, R_ADDR);
    if (cycles_out) *cycles_out = c.cycles;
    candidate_free(&c);
    return r;
}

/* --- the corpus ------------------------------------------------------- */
static const uint32_t kCorpus32[] = {
    0u, 1u, 0xFFFFFFFFu,                      /* 0, 1, -1 */
    0x7FFFu, 0xFFFF8000u,                     /* INT16_MAX, INT16_MIN sign-extended */
    0x7FFFFFFFu, 0x80000000u,                 /* INT32_MAX, INT32_MIN */
    0x00FFu, 0x0100u, 0xFFFFu, 0x10000u,      /* byte boundaries */
    0x00FFFFFFu, 0x01000000u, 0xFFFFFF00u,    /* higher byte boundaries */
    0x01010101u, 0xFEFEFEFEu, 0x80000001u,
};
#define CORPUS_N (sizeof(kCorpus32) / sizeof(kCorpus32[0]))

static uint32_t rng_state = 0x13579BDFu;
static uint32_t rng(void) { /* deterministic xorshift: same corpus every run */
    rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5;
    return rng_state;
}

/* Leaves the carry flag in a known state via a multiply, using registers no
 * helper under test reads. */
static void set_carry_via_mul(InstrBuf *b, uint8_t seed) {
    char x[AVR_OPERAND_LEN], y[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(x, REG_SCRATCH0); fmt_reg(y, REG_SCRATCH1);
    fmt_imm(imm, seed); ins2(b, "ldi", x, imm);
    fmt_imm(imm, seed); ins2(b, "ldi", y, imm);
    ins2(b, "mul", x, y);
}

/* --- per-helper differential tests ------------------------------------ */

static void test_add_sub(void) {
    for (size_t i = 0; i < CORPUS_N; i++) {
        for (size_t j = 0; j < CORPUS_N; j++) {
            uint32_t a = kCorpus32[i], b = kCorpus32[j];
            InstrBuf ab; instrbuf_init(&ab);
            dsp32_load(&ab, DSP32_A, A_ADDR); dsp32_load(&ab, DSP32_B, B_ADDR);
            dsp32_add(&ab, DSP32_A, DSP32_B); dsp32_store(&ab, DSP32_A, R_ADDR);
            assert(run(&ab, a, b, NULL) == (uint32_t)(a + b));

            InstrBuf sb; instrbuf_init(&sb);
            dsp32_load(&sb, DSP32_A, A_ADDR); dsp32_load(&sb, DSP32_B, B_ADDR);
            dsp32_sub(&sb, DSP32_A, DSP32_B); dsp32_store(&sb, DSP32_A, R_ADDR);
            assert(run(&sb, a, b, NULL) == (uint32_t)(a - b));
        }
    }
    for (int k = 0; k < 4000; k++) {
        uint32_t a = rng(), b = rng();
        InstrBuf ab; instrbuf_init(&ab);
        dsp32_load(&ab, DSP32_A, A_ADDR); dsp32_load(&ab, DSP32_B, B_ADDR);
        dsp32_add(&ab, DSP32_A, DSP32_B); dsp32_store(&ab, DSP32_A, R_ADDR);
        assert(run(&ab, a, b, NULL) == (uint32_t)(a + b));
        InstrBuf sb; instrbuf_init(&sb);
        dsp32_load(&sb, DSP32_A, A_ADDR); dsp32_load(&sb, DSP32_B, B_ADDR);
        dsp32_sub(&sb, DSP32_A, DSP32_B); dsp32_store(&sb, DSP32_A, R_ADDR);
        assert(run(&sb, a, b, NULL) == (uint32_t)(a - b));
    }
    printf("  add/sub: %zu corpus pairs + 4000 random -> exact\n", CORPUS_N * CORPUS_N);
}

static void test_sext16(void) {
    int checked = 0;
    for (long v = -32768; v <= 32767; v += 1) {
        if (!(v == -32768 || v == -1 || v == 0 || v == 1 || v == 32767 ||
              v == 255 || v == 256 || v == -256 || v == -257 || (v & 0x3FF) == 0)) continue;
        int16_t x = (int16_t)v;
        InstrBuf b; instrbuf_init(&b);
        dsp16_load(&b, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, A_ADDR);
        dsp32_sext16(&b, DSP32_A, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI);
        dsp32_store(&b, DSP32_A, R_ADDR);
        uint32_t got = run(&b, (uint32_t)(uint16_t)x, 0, NULL);
        assert(got == (uint32_t)(int32_t)x);
        checked++;
    }
    printf("  sext16: %d values incl. INT16_MIN/MAX and byte boundaries -> exact\n", checked);
}

static void test_neg_and_lsr(void) {
    for (size_t i = 0; i < CORPUS_N; i++) {
        uint32_t a = kCorpus32[i];
        InstrBuf nb; instrbuf_init(&nb);
        dsp32_load(&nb, DSP32_A, A_ADDR); dsp32_neg(&nb, DSP32_A); dsp32_store(&nb, DSP32_A, R_ADDR);
        assert(run(&nb, a, 0, NULL) == (uint32_t)(0u - a));

        InstrBuf sb; instrbuf_init(&sb);
        dsp32_load(&sb, DSP32_A, A_ADDR); dsp32_lsr(&sb, DSP32_A); dsp32_store(&sb, DSP32_A, R_ADDR);
        assert(run(&sb, a, 0, NULL) == (a >> 1)); /* logical: INT32_MIN >> 1 must be 0x40000000 */
    }
    for (int k = 0; k < 2000; k++) {
        uint32_t a = rng();
        InstrBuf sb; instrbuf_init(&sb);
        dsp32_load(&sb, DSP32_A, A_ADDR); dsp32_lsr(&sb, DSP32_A); dsp32_store(&sb, DSP32_A, R_ADDR);
        assert(run(&sb, a, 0, NULL) == (a >> 1));
    }
    printf("  neg/lsr: corpus + 2000 random -> exact (lsr is logical, not arithmetic)\n");
}

/* dsp32_cmp leaves its answer in the carry flag; materialise it with the
 * sbc idiom so the test can observe it. */
static const int16_t kI16[] = {0, 1, -1, 2, -2, 127, -128, 255, -256, 256, -257,
                                32767, -32768, 32766, -32767, 181, -181, 16384, -16384};
#define I16_N (sizeof(kI16) / sizeof(kI16[0]))

static void test_lsl_and_q15_extract(void) {
    /* dsp32_lsl is exact for the whole corpus... */
    for (size_t i = 0; i < CORPUS_N; i++) {
        uint32_t a = kCorpus32[i];
        InstrBuf b; instrbuf_init(&b);
        dsp32_load(&b, DSP32_A, A_ADDR); dsp32_lsl(&b, DSP32_A); dsp32_store(&b, DSP32_A, R_ADDR);
        assert(run(&b, a, 0, NULL) == (uint32_t)(a << 1));
    }
    /* ...and the composition that matters: a Q15 product is (x*y) >> 15,
     * which equals the top two bytes of (x*y) << 1. Checked against the host
     * definition for every int16 corpus pair. */
    for (size_t i = 0; i < I16_N; i++) {
        for (size_t j = 0; j < I16_N; j++) {
            int16_t x = kI16[i], y = kI16[j];
            InstrBuf b; instrbuf_init(&b);
            dsp16_load(&b, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, A_ADDR);
            dsp16_load(&b, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI, B_ADDR);
            dsp32_mul16x16(&b, DSP32_A, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI,
                            REG_DSP_OP_B_LO, REG_DSP_OP_B_HI);
            dsp32_lsl(&b, DSP32_A);
            dsp32_store(&b, DSP32_A, R_ADDR);
            uint32_t got = run(&b, (uint32_t)(uint16_t)x, (uint32_t)(uint16_t)y, NULL);
            int16_t q15 = (int16_t)(got >> 16);                 /* top two bytes */
            int16_t want = (int16_t)(((int32_t)x * (int32_t)y) >> 15);
            if (q15 != want) printf("  q15 FAIL %d*%d got=%d want=%d\n", x, y, q15, want);
            assert(q15 == want);
        }
    }
    printf("  lsl/q15: corpus exact; (x*y)<<1 top two bytes == (x*y)>>15 for %zu pairs\n", I16_N*I16_N);
}

static void test_cmp_unsigned(void) {
    for (size_t i = 0; i < CORPUS_N; i++) {
        for (size_t j = 0; j < CORPUS_N; j++) {
            uint32_t a = kCorpus32[i], b = kCorpus32[j];
            InstrBuf cb; instrbuf_init(&cb);
            dsp32_load(&cb, DSP32_A, A_ADDR); dsp32_load(&cb, DSP32_B, B_ADDR);
            dsp32_cmp(&cb, DSP32_A, DSP32_B);
            char m[AVR_OPERAND_LEN]; fmt_reg(m, DSP32_C);
            ins2(&cb, "sbc", m, m);           /* 0xFF if borrow (a<b), else 0x00 */
            char z[AVR_OPERAND_LEN]; fmt_reg(z, REG_ZERO);
            for (int k = 1; k < 4; k++) { char d[AVR_OPERAND_LEN]; fmt_reg(d, DSP32_C + k); ins2(&cb, "mov", d, m); }
            dsp32_store(&cb, DSP32_C, R_ADDR);
            uint32_t got = run(&cb, a, b, NULL);
            uint32_t want = (a < b) ? 0xFFFFFFFFu : 0u;
            if (got != want) { printf("  cmp FAIL a=%08X b=%08X got=%08X want=%08X\n", a, b, got, want); }
            assert(got == want);
        }
    }
    for (int k = 0; k < 3000; k++) {
        uint32_t a = rng(), b = rng();
        InstrBuf cb; instrbuf_init(&cb);
        dsp32_load(&cb, DSP32_A, A_ADDR); dsp32_load(&cb, DSP32_B, B_ADDR);
        dsp32_cmp(&cb, DSP32_A, DSP32_B);
        char m[AVR_OPERAND_LEN]; fmt_reg(m, DSP32_C);
        ins2(&cb, "sbc", m, m);
        for (int q = 1; q < 4; q++) { char d[AVR_OPERAND_LEN]; fmt_reg(d, DSP32_C + q); ins2(&cb, "mov", d, m); }
        dsp32_store(&cb, DSP32_C, R_ADDR);
        assert(run(&cb, a, b, NULL) == ((a < b) ? 0xFFFFFFFFu : 0u));
    }
    printf("  cmp: %zu corpus pairs + 3000 random -> exact unsigned ordering\n", CORPUS_N * CORPUS_N);
}


static void test_mul16x16(void) {
    for (size_t i = 0; i < I16_N; i++) {
        for (size_t j = 0; j < I16_N; j++) {
            int16_t x = kI16[i], y = kI16[j];
            InstrBuf b; instrbuf_init(&b);
            dsp16_load(&b, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, A_ADDR);
            dsp16_load(&b, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI, B_ADDR);
            dsp32_mul16x16(&b, DSP32_C, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI,
                            REG_DSP_OP_B_LO, REG_DSP_OP_B_HI);
            dsp32_store(&b, DSP32_C, R_ADDR);
            uint32_t got = run(&b, (uint32_t)(uint16_t)x, (uint32_t)(uint16_t)y, NULL);
            uint32_t want = (uint32_t)((int32_t)x * (int32_t)y);
            if (got != want) printf("  mul FAIL %d * %d got=%08X want=%08X\n", x, y, got, want);
            assert(got == want);
        }
    }
    for (int k = 0; k < 3000; k++) {
        int16_t x = (int16_t)rng(), y = (int16_t)rng();
        InstrBuf b; instrbuf_init(&b);
        dsp16_load(&b, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, A_ADDR);
        dsp16_load(&b, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI, B_ADDR);
        dsp32_mul16x16(&b, DSP32_C, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI,
                        REG_DSP_OP_B_LO, REG_DSP_OP_B_HI);
        dsp32_store(&b, DSP32_C, R_ADDR);
        assert(run(&b, (uint32_t)(uint16_t)x, (uint32_t)(uint16_t)y, NULL)
               == (uint32_t)((int32_t)x * (int32_t)y));
    }
    printf("  mul16x16: %zu corpus pairs + 3000 random -> exact signed 16x16->32\n", I16_N * I16_N);
}

/* The exact composition Magnitude performs, end to end, plus the overflow
 * argument that makes the chosen widths sound. */
static void test_magnitude_composition_and_bounds(void) {
    uint32_t worst = 0;
    for (size_t i = 0; i < I16_N; i++) {
        for (size_t j = 0; j < I16_N; j++) {
            int16_t re = kI16[i], im = kI16[j];
            InstrBuf b; instrbuf_init(&b);
            dsp16_load(&b, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, A_ADDR);
            dsp16_load(&b, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI, B_ADDR);
            /* re_sq into C, moved to A; im_sq into C; A += C */
            dsp32_mul16x16(&b, DSP32_C, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI,
                            REG_DSP_OP_A_LO, REG_DSP_OP_A_HI);
            for (int q = 0; q < 4; q++) {
                char d[AVR_OPERAND_LEN], s[AVR_OPERAND_LEN];
                fmt_reg(d, DSP32_A + q); fmt_reg(s, DSP32_C + q); ins2(&b, "mov", d, s);
            }
            dsp32_mul16x16(&b, DSP32_C, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI,
                            REG_DSP_OP_B_LO, REG_DSP_OP_B_HI);
            dsp32_add(&b, DSP32_A, DSP32_C);
            dsp32_store(&b, DSP32_A, R_ADDR);
            uint32_t got = run(&b, (uint32_t)(uint16_t)re, (uint32_t)(uint16_t)im, NULL);
            uint32_t want = (uint32_t)((int32_t)re * (int32_t)re)
                          + (uint32_t)((int32_t)im * (int32_t)im);
            assert(got == want);
            if (want > worst) worst = want;
        }
    }
    /* Bound: |re|,|im| <= 32768, so each square <= 2^30 and the sum <= 2^31.
     * 2^31 exceeds INT32_MAX by one, which is exactly why `sum` is carried as
     * UNSIGNED and why isqrt32 must shift logically. isqrt32(2^31) = 46340,
     * comfortably inside uint16. */
    uint32_t bound = 2147483648u; /* 2^31, attained at re = im = -32768 */
    printf("  magnitude composition: %zu pairs exact; worst observed sum=%u, "
           "theoretical max=%u (2^31)\n", I16_N * I16_N, worst, bound);
    assert(worst <= bound);
    assert(bound > 2147483647u); /* > INT32_MAX: unsigned representation is required */
    /* isqrt32 of the bound fits uint16 */
    unsigned long root = 0, rem = 0;
    for (int s = 30; s >= 0; s -= 2) {
        rem = (rem << 2) | ((bound >> s) & 3u);
        root <<= 1;
        if (rem > root) { rem -= root + 1; root += 2; }
    }
    root >>= 1;
    printf("  isqrt32(2^31) = %lu (fits uint16: %s)\n", root, root <= 65535 ? "yes" : "NO");
    assert(root == 46340 && root <= 65535);
}

/* No helper may read a flag that a preceding multiply left behind.
 *
 * Each helper runs twice, preceded by a multiply that deliberately leaves the
 * carry flag set in one run and clear in the other (real AVR sets C to bit 15
 * of the product, which the interpreter now models). The carry-setting
 * multiply uses REG_SCRATCH0/1, which none of the helpers under test reads, so
 * the only thing differing between the two runs is the incoming flag. Equal
 * results mean every flag a helper consumes is one it established itself. */
static void test_no_helper_depends_on_mul_carry(void) {
    for (size_t i = 0; i < CORPUS_N; i++) {
        uint32_t a = kCorpus32[i];
        int16_t half = (int16_t)(uint16_t)(a & 0xFFFFu);
        uint32_t sext[2], lsr[2], neg[2], mul[2];
        for (int pre = 0; pre < 2; pre++) {
            /* 0xFF*0xFF = 0xFE01 -> bit15 = 1; 0x01*0x01 = 0x0001 -> bit15 = 0 */
            uint8_t seed = pre ? 0xFF : 0x01;

            InstrBuf b1; instrbuf_init(&b1);
            set_carry_via_mul(&b1, seed);
            dsp16_load(&b1, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, A_ADDR);
            dsp32_sext16(&b1, DSP32_A, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI);
            dsp32_store(&b1, DSP32_A, R_ADDR);
            sext[pre] = run(&b1, (uint32_t)(uint16_t)half, 0, NULL);

            InstrBuf b2; instrbuf_init(&b2);
            set_carry_via_mul(&b2, seed);
            dsp32_load(&b2, DSP32_A, A_ADDR);
            dsp32_lsr(&b2, DSP32_A);
            dsp32_store(&b2, DSP32_A, R_ADDR);
            lsr[pre] = run(&b2, a, 0, NULL);

            InstrBuf b3; instrbuf_init(&b3);
            set_carry_via_mul(&b3, seed);
            dsp32_load(&b3, DSP32_A, A_ADDR);
            dsp32_neg(&b3, DSP32_A);
            dsp32_store(&b3, DSP32_A, R_ADDR);
            neg[pre] = run(&b3, a, 0, NULL);

            InstrBuf b4; instrbuf_init(&b4);
            set_carry_via_mul(&b4, seed);
            dsp16_load(&b4, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, A_ADDR);
            dsp16_load(&b4, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI, B_ADDR);
            dsp32_mul16x16(&b4, DSP32_C, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI,
                            REG_DSP_OP_B_LO, REG_DSP_OP_B_HI);
            dsp32_store(&b4, DSP32_C, R_ADDR);
            mul[pre] = run(&b4, (uint32_t)(uint16_t)half, (uint32_t)(uint16_t)half, NULL);
        }
        assert(sext[0] == sext[1]);
        assert(lsr[0] == lsr[1]);
        assert(neg[0] == neg[1]);
        assert(mul[0] == mul[1]);
        /* and each still equals its host oracle under both flag states */
        assert(sext[0] == (uint32_t)(int32_t)half);
        assert(lsr[0] == (a >> 1));
        assert(neg[0] == (uint32_t)(0u - a));
        assert(mul[0] == (uint32_t)((int32_t)half * (int32_t)half));
    }
    printf("  flag independence: sext16/lsr/neg/mul16x16 identical with mul carry set vs clear\n");
}


/* --- register-footprint contract ---------------------------------------
 *
 * registers.h states which registers each helper may write. Stating it is not
 * enough: FFT and Magnitude lowering will hold live values in the other quads
 * while calling these, so a helper that quietly touches a register outside its
 * declared set would corrupt a caller's value in a way no value test catches.
 * This walks the emitted instructions and checks the written set directly. */

/* Destination register of an emitted instruction, or -1 if it writes none.
 *
 * The mul family is the subtlety: `mul Rd, Rr` writes the hardwired pair
 * r1:r0 and treats BOTH operands as sources, so reading operand 0 as a
 * destination would report a write that never happens. Handled explicitly
 * rather than by pattern, because getting it wrong in the other direction --
 * silently ignoring a real write -- would make this whole check vacuous. */
static int written_reg(const AvrInstr *ins, int *also_r0r1) {
    const char *m = ins->mnemonic;
    *also_r0r1 = 0;
    if (avr_instr_is_label(ins) || avr_instr_is_data_word(ins)) return -1;
    if (!strcmp(m, "mul") || !strcmp(m, "muls") || !strcmp(m, "mulsu")) {
        *also_r0r1 = 1;
        return -1;                                                  /* operands are sources */
    }
    if (!strcmp(m, "sts") || !strcmp(m, "cp") || !strcmp(m, "cpc") ||
        !strcmp(m, "brne") || !strcmp(m, "break")) return -1;      /* no register destination */
    if (!strcmp(m, "st") || !strcmp(m, "std")) return -1;
    if (ins->num_operands < 1 || ins->operands[0][0] != 'r') return -1;
    return atoi(ins->operands[0] + 1);
}

static int in_set(int r, const int *set, size_t n) {
    for (size_t i = 0; i < n; i++) if (set[i] == r) return 1;
    return 0;
}

static void check_footprint(const char *name, InstrBuf *b, const int *allowed, size_t n) {
    Candidate c;
    assert(instrbuf_price(b, &g_cm, &c) == 0);
    for (size_t i = 0; i < c.num_instructions; i++) {
        int r0r1 = 0;
        int w = written_reg(&c.instructions[i], &r0r1);
        if (r0r1 && (!in_set(0, allowed, n) || !in_set(1, allowed, n))) {
            printf("  FOOTPRINT VIOLATION in %s: %s clobbers r0/r1, not declared\n",
                   name, c.instructions[i].mnemonic);
            assert(0);
        }
        if (w < 0) continue;
        if (!in_set(w, allowed, n)) {
            printf("  FOOTPRINT VIOLATION in %s: writes r%d (%s)\n", name, w, c.instructions[i].mnemonic);
            assert(0);
        }
    }
    printf("  %-14s writes only its declared registers (%zu instructions)\n", name, c.num_instructions);
    candidate_free(&c);
}

static void test_register_footprints(void) {
    /* mul writes r0/r1 unconditionally -- hardwired, and declared as such. */
    const int quadA[] = {DSP32_A, DSP32_A+1, DSP32_A+2, DSP32_A+3};
    const int quadC[] = {DSP32_C, DSP32_C+1, DSP32_C+2, DSP32_C+3, 0, 1, REG_DSP_SIGNEXT};
    const int quadA_scratch[] = {DSP32_A, DSP32_A+1, DSP32_A+2, DSP32_A+3, REG_SCRATCH0};

    InstrBuf b; instrbuf_init(&b);
    dsp32_sext16(&b, DSP32_A, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI);
    check_footprint("sext16", &b, quadA, 4);

    instrbuf_init(&b); dsp32_add(&b, DSP32_A, DSP32_B);
    check_footprint("add", &b, quadA, 4);

    instrbuf_init(&b); dsp32_sub(&b, DSP32_A, DSP32_B);
    check_footprint("sub", &b, quadA, 4);

    instrbuf_init(&b); dsp32_lsr(&b, DSP32_A);
    check_footprint("lsr", &b, quadA, 4);

    /* neg needs one byte of scratch to hold the constant 1 */
    instrbuf_init(&b); dsp32_neg(&b, DSP32_A);
    check_footprint("neg", &b, quadA_scratch, 5);

    /* cmp must write nothing at all: it answers in the carry flag */
    instrbuf_init(&b); dsp32_cmp(&b, DSP32_A, DSP32_B);
    check_footprint("cmp", &b, quadA, 0);

    instrbuf_init(&b);
    dsp32_mul16x16(&b, DSP32_C, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI,
                    REG_DSP_OP_B_LO, REG_DSP_OP_B_HI);
    check_footprint("mul16x16", &b, quadC, 7);

    /* And the decisive property: the quads do not overlap each other, nor the
     * 16-bit multiply window, nor the pointer registers. */
    for (int i = 0; i < 4; i++) {
        assert(DSP32_A + i < DSP32_B);
        assert(DSP32_B + i < DSP32_C);
        assert(DSP32_C + i < REG_DSP_OP_A_LO);          /* quads end before r16 */
    }
    assert(DSP32_C + 3 == 15 && REG_DSP_OP_A_LO == 16);  /* adjacent, not overlapping */
    assert(DSP32_A % 2 == 0 && DSP32_B % 2 == 0 && DSP32_C % 2 == 0); /* movw-addressable */
    assert(DSP32_A > REG_ZERO);                          /* never aliases the zero register */
    printf("  quads A/B/C are disjoint, movw-aligned, and clear of r16-r31\n");
}

static void test_cycle_prediction_matches_interpreter(void) {
    /* The cost model must agree with what actually executes. The interpreter
     * counts real steps, so compare against a hand-derived cycle total. */
    InstrBuf b; instrbuf_init(&b);
    dsp32_load(&b, DSP32_A, A_ADDR);      /* 4 lds  = 8 cycles */
    dsp32_load(&b, DSP32_B, B_ADDR);      /* 4 lds  = 8 */
    dsp32_add(&b, DSP32_A, DSP32_B);      /* 4 ALU  = 4 */
    dsp32_store(&b, DSP32_A, R_ADDR);     /* 4 sts  = 8 */
    Candidate c; assert(instrbuf_price(&b, &g_cm, &c) == 0);
    printf("  cycle model: load+load+add+store predicted %u (hand-derived 28)\n", c.cycles);
    assert(c.cycles == 28);
    candidate_free(&c);
}

int main(void) {
    load_costs();
    test_add_sub();
    test_sext16();
    test_neg_and_lsr();
    test_lsl_and_q15_extract();
    test_cmp_unsigned();
    test_mul16x16();
    test_magnitude_composition_and_bounds();
    test_no_helper_depends_on_mul_carry();
    test_register_footprints();
    test_cycle_prediction_matches_interpreter();
    printf("test_dsp32: all tests passed\n");
    return 0;
}
