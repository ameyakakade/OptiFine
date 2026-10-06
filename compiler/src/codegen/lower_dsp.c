#include "lower_internal.h"

#include "optifine/codegen/registers.h"
#include "optifine/dsp_build.h"

/* ---- DSP path: Q15 arithmetic primitives shared by Window, FftButterfly,
 * Magnitude, and PeakExtract ---- */

/* out = a - b, 16-bit signed (sub for the low byte, sbc for the high --
 * same multi-byte-with-borrow convention lower_add32_element already
 * uses for addition). */
void lower_sub16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr) {
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
void lower_add16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr) {
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
void lower_asr16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr) {
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
void lower_copy16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr) {
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
void lower_const16(InstrBuf *buf, int16_t value, uint16_t addr) {
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
 * the cross term's overflow into p3 (found during implementation,
 * re-derived correctly, and cross-checked against this file's own working
 * precedent -- lower_matmul's sign-before-add ordering for the same class
 * of widen-and-accumulate -- plus a from-scratch AVR carry-flag simulation
 * over the known test vectors and 5000 random Q15 pairs; corrected here
 * before the FFT stages were built on it).
 * A single <<1 across the 3-byte accumulator then aligns the result --
 * NOT a 15-bit shift chain -- and the top two bytes are the Q15 product.
 * Truncates rather than rounds (no rounding-bias correction before the
 * shift): the result is exactly floor(x*y / 32768), i.e. (x*y) >> 15, which
 * the FFT and full-pipeline tests check bit-exactly against integer oracles.
 * Measured through the real avr-gcc: 72 bytes, 30 instructions (this instruction count
 * is unaffected by the sign-byte reordering -- same 7 instructions per
 * cross term, just reordered). */
void lower_fixed_mul_q15(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr) {
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

/* Test-only seam: the correctness test in test_dsp_lower.c prices and runs
 * one multiply in isolation. */
int lower_fixed_mul_q15_test_hook(uint16_t a_addr, uint16_t b_addr, uint16_t out_addr,
                                   const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    lower_fixed_mul_q15(&buf, a_addr, b_addr, out_addr);
    return instrbuf_price(&buf, cost_model, out);
}

/* Loads one byte through a pointer and parks it at an absolute scratch cell. */
void ptr_byte_to_scratch(InstrBuf *buf, char ptr, uint16_t dst) {
    char r[AVR_OPERAND_LEN], p[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r, REG_SCRATCH0);
    fmt_ptr(p, ptr, 1);
    fmt_addr(a, dst);
    ins2(buf, ptr == 'Z' ? "lpm" : "ld", r, p);
    ins2(buf, "sts", a, r);
}
void scratch_byte_to_ptr(InstrBuf *buf, uint16_t src, char ptr) {
    char r[AVR_OPERAND_LEN], p[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r, REG_SCRATCH0);
    fmt_ptr(p, ptr, 1);
    fmt_addr(a, src);
    ins2(buf, "lds", r, a);
    ins2(buf, "st", p, r);
}
void load_ptr_imm(InstrBuf *buf, int lo_reg, uint16_t addr) {
    char r[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(r, lo_reg);     fmt_lo8(imm, addr); ins2(buf, "ldi", r, imm);
    fmt_reg(r, lo_reg + 1); fmt_hi8(imm, addr); ins2(buf, "ldi", r, imm);
}

/* Adds a signed compile-time constant to pointer pair lo_reg:lo_reg+1.
 * ADIW/SBIW reach 0..63; past that, SUBI/SBCI subtract the negated constant.
 * Both forms take 2 cycles; the second is one word longer. */
void ptr_add(InstrBuf *buf, int lo_reg, int delta) {
    char lo[AVR_OPERAND_LEN], hi[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(lo, lo_reg);
    if (delta == 0) return;
    if (delta > 0 && delta <= 63) {
        fmt_imm(imm, (uint8_t)delta);
        ins2(buf, "adiw", lo, imm);
        return;
    }
    if (delta < 0 && delta >= -63) {
        fmt_imm(imm, (uint8_t)-delta);
        ins2(buf, "sbiw", lo, imm);
        return;
    }
    uint16_t neg = (uint16_t)-delta;
    fmt_reg(hi, lo_reg + 1);
    fmt_imm(imm, (uint8_t)(neg & 0xFF));
    ins2(buf, "subi", lo, imm);
    fmt_imm(imm, (uint8_t)(neg >> 8));
    ins2(buf, "sbci", hi, imm);
}

/* ---- OP_WINDOW: elementwise Q15 multiply by a fixed window ---- */

void lower_window(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
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

/* ---- OP_BIT_REVERSE: fixed compile-time index permutation from the real
 * Q15 window output into the complex Q15 working buffer ---- */

/* Reverses the low `bits` bits of i. Six bits for N=64.
 *
 * The permutation is an involution: reversing a bit pattern twice restores
 * it, so brev(brev(i)) == i. That is why writing `out[i] = in[brev(i)]` here
 * and specifying it as `out[brev(i)] = in[i]` describe the same permutation --
 * substituting j = brev(i) turns one into the other. The test checks both
 * framings against an independent host oracle rather than relying on that
 * argument. */
static size_t dsp_bit_reverse_index(size_t i, int bits) {
    size_t r = 0;
    for (int b = 0; b < bits; b++) {
        r = (r << 1) | (i & 1);
        i >>= 1;
    }
    return r;
}

/* Deliberately unrolled, unlike the rest of the DSP path.
 *
 * Every other op loops because unrolling it costs kilobytes; this one is 64
 * copies of six instructions, ~1.8 KB, about 1.4% of the ATmega128's flash.
 * A loop would need the permutation as runtime data -- a 128-byte program-
 * memory table plus lpm loads and pointer arithmetic per element -- to save
 * roughly 1.6 KB out of a budget with over 120 KB spare. The complexity is
 * not worth it, and an unrolled permutation has no control flow to get wrong.
 *
 * Reads FIXED_Q15 (2-byte stride), writes COMPLEX_Q15 (4-byte stride) with
 * the imaginary half zeroed: this op is where the pipeline crosses from real
 * samples to the complex buffer the butterfly stages operate on. */
void lower_bit_reverse(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
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

/* Test seam: the permutation itself, so a test can prove it is a bijection
 * without re-deriving it. */
size_t dsp_bit_reverse_index_test_hook(size_t i, int bits) {
    return dsp_bit_reverse_index(i, bits);
}
