#include "optifine/codegen/dsp32.h"

#include <assert.h>

#include "optifine/codegen/registers.h"

static void reg(char *out, int r) { fmt_reg(out, r); }

void dsp32_clear(InstrBuf *b, int quad) {
    char r[AVR_OPERAND_LEN];
    for (int i = 0; i < DSP32_BYTES; i++) { reg(r, quad + i); ins1(b, "clr", r); }
}

void dsp32_sext16(InstrBuf *b, int quad, int lo_reg, int hi_reg) {
    char d[AVR_OPERAND_LEN], s[AVR_OPERAND_LEN];
    reg(d, quad + 0); reg(s, lo_reg); ins2(b, "mov", d, s);
    reg(d, quad + 1); reg(s, hi_reg); ins2(b, "mov", d, s);
    /* Build the sign byte from the high byte's bit 7 without a branch and
     * without consuming any flag set earlier: `lsl` puts bit 7 into carry,
     * `sbc x,x` turns that into 0x00 or 0xFF. Same idiom lower_matmul uses;
     * it establishes the carry it consumes, so it is independent of whatever
     * a preceding mul may have left in the flag. */
    reg(d, quad + 2); reg(s, quad + 1);
    ins2(b, "mov", d, s);
    ins1(b, "lsl", d);
    ins2(b, "sbc", d, d);
    char e[AVR_OPERAND_LEN];
    reg(e, quad + 3); ins2(b, "mov", e, d);
}

static void chain(InstrBuf *b, int dst, int src, const char *first, const char *rest) {
    char d[AVR_OPERAND_LEN], s[AVR_OPERAND_LEN];
    for (int i = 0; i < DSP32_BYTES; i++) {
        reg(d, dst + i); reg(s, src + i);
        ins2(b, i == 0 ? first : rest, d, s);
    }
}
void dsp32_add(InstrBuf *b, int dst, int src) { chain(b, dst, src, "add", "adc"); }
void dsp32_sub(InstrBuf *b, int dst, int src) { chain(b, dst, src, "sub", "sbc"); }

void dsp32_neg(InstrBuf *b, int dst) {
    /* -x == (~x) + 1, done as 0 - x so the borrow chain is explicit:
     * com each byte then add 1 with carry propagation. */
    char d[AVR_OPERAND_LEN], z[AVR_OPERAND_LEN];
    reg(z, REG_ZERO);
    for (int i = 0; i < DSP32_BYTES; i++) { reg(d, dst + i); ins1(b, "com", d); }
    char one[AVR_OPERAND_LEN];
    reg(one, REG_SCRATCH0);
    char imm[AVR_OPERAND_LEN];
    fmt_imm(imm, 1);
    ins2(b, "ldi", one, imm);
    reg(d, dst + 0); ins2(b, "add", d, one);
    for (int i = 1; i < DSP32_BYTES; i++) { reg(d, dst + i); ins2(b, "adc", d, z); }
}

void dsp32_lsl(InstrBuf *b, int dst) {
    char d[AVR_OPERAND_LEN];
    reg(d, dst + 0); ins1(b, "lsl", d);                          /* zero into bit 0 */
    for (int i = 1; i < DSP32_BYTES; i++) { reg(d, dst + i); ins1(b, "rol", d); }
}

void dsp32_lsr(InstrBuf *b, int dst) {
    char d[AVR_OPERAND_LEN];
    reg(d, dst + DSP32_BYTES - 1); ins1(b, "lsr", d);           /* zero into bit 7 */
    for (int i = DSP32_BYTES - 2; i >= 0; i--) { reg(d, dst + i); ins1(b, "ror", d); }
}

void dsp32_cmp(InstrBuf *b, int a, int b_quad) {
    char x[AVR_OPERAND_LEN], y[AVR_OPERAND_LEN];
    for (int i = 0; i < DSP32_BYTES; i++) {
        reg(x, a + i); reg(y, b_quad + i);
        ins2(b, i == 0 ? "cp" : "cpc", x, y);
    }
}

void dsp32_mul16x16(InstrBuf *b, int dst, int xlo, int xhi, int ylo, int yhi) {
    char d0[AVR_OPERAND_LEN], d1[AVR_OPERAND_LEN], d2[AVR_OPERAND_LEN], d3[AVR_OPERAND_LEN];
    char r0[AVR_OPERAND_LEN], r1[AVR_OPERAND_LEN], sg[AVR_OPERAND_LEN], z[AVR_OPERAND_LEN];
    char xl[AVR_OPERAND_LEN], xh[AVR_OPERAND_LEN], yl[AVR_OPERAND_LEN], yh[AVR_OPERAND_LEN];
    reg(d0, dst + 0); reg(d1, dst + 1); reg(d2, dst + 2); reg(d3, dst + 3);
    reg(r0, 0); reg(r1, 1); reg(sg, REG_DSP_SIGNEXT); reg(z, REG_ZERO);
    reg(xl, xlo); reg(xh, xhi); reg(yl, ylo); reg(yh, yhi);

    /* Standard signed 16x16 -> 32 decomposition. Each partial product's own
     * sign extension is derived with the lsl/sbc idiom rather than by reading
     * the carry mul leaves behind, so the sequence does not depend on
     * MUL-family flag behaviour at all -- the interpreter now models that
     * flag, but not depending on it keeps the helper checkable either way. */
    ins2(b, "mul", xl, yl);                 /* unsigned low x low */
    ins2(b, "mov", d0, r0);
    ins2(b, "mov", d1, r1);
    ins1(b, "clr", d2);
    ins1(b, "clr", d3);

    ins2(b, "muls", xh, yh);                /* signed high x high -> bytes 2,3 */
    ins2(b, "add", d2, r0);
    ins2(b, "adc", d3, r1);

    ins2(b, "mulsu", xh, yl);               /* signed x unsigned -> bytes 1,2 (+sign) */
    ins2(b, "mov", sg, r1);
    ins1(b, "lsl", sg);
    ins2(b, "sbc", sg, sg);
    ins2(b, "add", d1, r0);
    ins2(b, "adc", d2, r1);
    ins2(b, "adc", d3, sg);

    ins2(b, "mulsu", yh, xl);               /* the symmetric cross term */
    ins2(b, "mov", sg, r1);
    ins1(b, "lsl", sg);
    ins2(b, "sbc", sg, sg);
    ins2(b, "add", d1, r0);
    ins2(b, "adc", d2, r1);
    ins2(b, "adc", d3, sg);
}

void dsp32_mov(InstrBuf *b, int dst, int src) {
    assert(dst % 2 == 0 && src % 2 == 0);
    char d[AVR_OPERAND_LEN], s[AVR_OPERAND_LEN];
    reg(d, dst);     reg(s, src);     ins2(b, "movw", d, s);
    reg(d, dst + 2); reg(s, src + 2); ins2(b, "movw", d, s);
}

void dsp32_load_imm(InstrBuf *b, int quad, uint32_t value, int scratch_reg) {
    assert(scratch_reg >= 16);
    char d[AVR_OPERAND_LEN], t[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    reg(t, scratch_reg);
    for (int i = 0; i < DSP32_BYTES; i++) {
        uint8_t v = (uint8_t)(value >> (8 * i));
        reg(d, quad + i);
        if (v == 0) { ins1(b, "clr", d); continue; }
        fmt_imm(imm, v);
        if (quad + i >= 16) { ins2(b, "ldi", d, imm); continue; }
        ins2(b, "ldi", t, imm);
        ins2(b, "mov", d, t);
    }
}

void dsp32_and_mask(InstrBuf *b, int quad, int mask_reg) {
    char d[AVR_OPERAND_LEN], m[AVR_OPERAND_LEN];
    reg(m, mask_reg);
    for (int i = 0; i < DSP32_BYTES; i++) { reg(d, quad + i); ins2(b, "and", d, m); }
}

void dsp8_ge_mask(InstrBuf *b, int mask_reg) {
    char m[AVR_OPERAND_LEN];
    reg(m, mask_reg);
    ins2(b, "sbc", m, m); /* 0xFF if a < b (borrow), 0x00 otherwise */
    ins1(b, "com", m);    /* 0xFF if a >= b */
}

void dsp16_gt_mask(InstrBuf *b, int mask_reg, int a_lo, int a_hi, int b_lo, int b_hi) {
    char m[AVR_OPERAND_LEN], x[AVR_OPERAND_LEN], y[AVR_OPERAND_LEN];
    reg(m, mask_reg);
    reg(x, b_lo); reg(y, a_lo); ins2(b, "cp", x, y);   /* b - a: borrows iff a > b */
    reg(x, b_hi); reg(y, a_hi); ins2(b, "cpc", x, y);
    ins2(b, "sbc", m, m);
}

void dsp8_zero_mask(InstrBuf *b, int mask_reg, int r) {
    char m[AVR_OPERAND_LEN], z[AVR_OPERAND_LEN], x[AVR_OPERAND_LEN];
    reg(m, mask_reg); reg(z, REG_ZERO); reg(x, r);
    ins2(b, "cp", z, x);   /* 0 - r: borrows iff r != 0 */
    ins2(b, "sbc", m, m);  /* 0xFF iff r != 0 */
    ins1(b, "com", m);     /* 0xFF iff r == 0 */
}

void dsp8_select(InstrBuf *b, int dst, int src, int mask_reg, int tmp_reg) {
    char d[AVR_OPERAND_LEN], s[AVR_OPERAND_LEN], m[AVR_OPERAND_LEN], t[AVR_OPERAND_LEN];
    reg(d, dst); reg(s, src); reg(m, mask_reg); reg(t, tmp_reg);
    ins2(b, "mov", t, d);
    ins2(b, "eor", t, s);
    ins2(b, "and", t, m);
    ins2(b, "eor", d, t);
}

void dsp32_isqrt(InstrBuf *b, int num, int res, int bit, int tmp, int mask_reg, int counter_reg) {
    assert(num != res && num != bit && num != tmp && res != bit && res != tmp && bit != tmp);
    dsp32_clear(b, res);
    dsp32_load_imm(b, bit, 1u << 30, mask_reg);

    LoopCtx loop;
    instrbuf_loop_begin(b, &loop, 16, counter_reg);
    dsp32_mov(b, tmp, res);
    dsp32_add(b, tmp, bit);          /* t = res + bit; never carries out: res + bit < 2^32 */
    dsp32_cmp(b, num, tmp);
    dsp8_ge_mask(b, mask_reg);       /* keep = num >= t */
    dsp32_and_mask(b, tmp, mask_reg);
    dsp32_sub(b, num, tmp);          /* num -= t & keep */
    dsp32_lsr(b, res);
    dsp32_mov(b, tmp, bit);
    dsp32_and_mask(b, tmp, mask_reg);
    dsp32_add(b, res, tmp);          /* res = (res >> 1) + (bit & keep) */
    dsp32_lsr(b, bit);
    dsp32_lsr(b, bit);               /* bit >>= 2 */
    instrbuf_loop_end(b, &loop);
}

void dsp32_load(InstrBuf *b, int quad, uint16_t addr) {
    char r[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    for (int i = 0; i < DSP32_BYTES; i++) {
        reg(r, quad + i); fmt_addr(a, (uint16_t)(addr + i)); ins2(b, "lds", r, a);
    }
}
void dsp32_store(InstrBuf *b, int quad, uint16_t addr) {
    char r[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    for (int i = 0; i < DSP32_BYTES; i++) {
        reg(r, quad + i); fmt_addr(a, (uint16_t)(addr + i)); ins2(b, "sts", a, r);
    }
}
void dsp16_load(InstrBuf *b, int lo_reg, int hi_reg, uint16_t addr) {
    char r[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    reg(r, lo_reg); fmt_addr(a, addr); ins2(b, "lds", r, a);
    reg(r, hi_reg); fmt_addr(a, (uint16_t)(addr + 1)); ins2(b, "lds", r, a);
}
void dsp16_store(InstrBuf *b, int lo_reg, int hi_reg, uint16_t addr) {
    char r[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    reg(r, lo_reg); fmt_addr(a, addr); ins2(b, "sts", a, r);
    reg(r, hi_reg); fmt_addr(a, (uint16_t)(addr + 1)); ins2(b, "sts", a, r);
}
