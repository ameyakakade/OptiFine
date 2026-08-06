#include "optifine/codegen/lower.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/registers.h"
#include "optifine/dsp_build.h"

/* ---------------------------------------------------------------------- */

int lower_init_zero_reg(const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    char r2[AVR_OPERAND_LEN];
    fmt_reg(r2, REG_ZERO);
    ins1(&buf, "clr", r2);
    return instrbuf_price(&buf, cost_model, out);
}

/* ---- OP_INPUT / OP_CONST: byte-for-byte ldi+sts into SRAM ---- */

static void lower_bytes(InstrBuf *buf, const uint8_t *bytes, size_t len, uint16_t base_addr) {
    char reg[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN], addr[AVR_OPERAND_LEN];
    fmt_reg(reg, REG_SCRATCH0);
    for (size_t i = 0; i < len; i++) {
        fmt_imm(imm, bytes[i]);
        ins2(buf, "ldi", reg, imm);
        fmt_addr(addr, (uint16_t)(base_addr + i));
        ins2(buf, "sts", addr, reg);
    }
}

/* ---- OP_MATMUL: fully unrolled int8x8 MAC, 16->32 branch-free sign extension ---- */

static void lower_matmul(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    size_t w_id = op->inputs[1];
    const IrOp *in_op = &graph->ops[in_id];
    const IrOp *w_op = &graph->ops[w_id];
    size_t k_dim = in_op->output_shape[in_op->output_shape_len - 1];
    size_t n_dim = w_op->output_shape[0]; /* weight shape [N, K] */

    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t w_addr = sram_layout_addr(layout, graph, w_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);

    char r_a[AVR_OPERAND_LEN], r_b[AVR_OPERAND_LEN];
    char r_acc0[AVR_OPERAND_LEN], r_acc1[AVR_OPERAND_LEN], r_acc2[AVR_OPERAND_LEN], r_acc3[AVR_OPERAND_LEN];
    char r_sign0[AVR_OPERAND_LEN], r_sign1[AVR_OPERAND_LEN];
    char r0[AVR_OPERAND_LEN], r1[AVR_OPERAND_LEN];
    fmt_reg(r_a, REG_MAC_A);
    fmt_reg(r_b, REG_MAC_B);
    fmt_reg(r_acc0, REG_ACC0);
    fmt_reg(r_acc1, REG_ACC1);
    fmt_reg(r_acc2, REG_ACC2);
    fmt_reg(r_acc3, REG_ACC3);
    fmt_reg(r_sign0, REG_SIGN0);
    fmt_reg(r_sign1, REG_SIGN1);
    fmt_reg(r0, 0);
    fmt_reg(r1, 1);

    for (size_t n = 0; n < n_dim; n++) {
        ins1(buf, "clr", r_acc0);
        ins1(buf, "clr", r_acc1);
        ins1(buf, "clr", r_acc2);
        ins1(buf, "clr", r_acc3);
        for (size_t k = 0; k < k_dim; k++) {
            char addr_in[AVR_OPERAND_LEN], addr_w[AVR_OPERAND_LEN];
            fmt_addr(addr_in, (uint16_t)(in_addr + k));
            fmt_addr(addr_w, (uint16_t)(w_addr + n * k_dim + k));
            ins2(buf, "lds", r_a, addr_in);
            ins2(buf, "lds", r_b, addr_w);
            ins2(buf, "muls", r_a, r_b);        /* r1:r0 = signed 16-bit product */
            ins2(buf, "mov", r_sign0, r1);
            ins1(buf, "lsl", r_sign0);           /* Carry = sign bit of the product's high byte */
            ins2(buf, "sbc", r_sign0, r_sign0);  /* r_sign0 = 0xFF if negative else 0x00 */
            ins2(buf, "mov", r_sign1, r_sign0);
            ins2(buf, "add", r_acc0, r0);
            ins2(buf, "adc", r_acc1, r1);
            ins2(buf, "adc", r_acc2, r_sign0);
            ins2(buf, "adc", r_acc3, r_sign1);
        }
        char addr0[AVR_OPERAND_LEN], addr1[AVR_OPERAND_LEN], addr2[AVR_OPERAND_LEN], addr3[AVR_OPERAND_LEN];
        fmt_addr(addr0, (uint16_t)(out_addr + n * 4 + 0));
        fmt_addr(addr1, (uint16_t)(out_addr + n * 4 + 1));
        fmt_addr(addr2, (uint16_t)(out_addr + n * 4 + 2));
        fmt_addr(addr3, (uint16_t)(out_addr + n * 4 + 3));
        ins2(buf, "sts", addr0, r_acc0);
        ins2(buf, "sts", addr1, r_acc1);
        ins2(buf, "sts", addr2, r_acc2);
        ins2(buf, "sts", addr3, r_acc3);
    }
}

/* ---- OP_ADD: elementwise 32-bit accumulator + 32-bit bias ---- */

static void lower_add32_element(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr) {
    char ra[AVR_OPERAND_LEN], rb[AVR_OPERAND_LEN];
    fmt_reg(ra, REG_ACC0);
    fmt_reg(rb, REG_ACC1);
    for (int i = 0; i < 4; i++) {
        char addr_a[AVR_OPERAND_LEN], addr_b[AVR_OPERAND_LEN], addr_out[AVR_OPERAND_LEN];
        fmt_addr(addr_a, (uint16_t)(a_addr + i));
        fmt_addr(addr_b, (uint16_t)(b_addr + i));
        fmt_addr(addr_out, (uint16_t)(out_addr + i));
        ins2(buf, "lds", ra, addr_a);
        ins2(buf, "lds", rb, addr_b);
        ins2(buf, i == 0 ? "add" : "adc", ra, rb);
        ins2(buf, "sts", addr_out, ra);
    }
}

static void lower_add(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t a_id = op->inputs[0];
    size_t b_id = op->inputs[1];
    size_t n = sram_layout_num_elements(op);
    uint16_t a_addr = sram_layout_addr(layout, graph, a_id, 0);
    uint16_t b_addr = sram_layout_addr(layout, graph, b_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    for (size_t i = 0; i < n; i++) {
        lower_add32_element(buf, (uint16_t)(a_addr + i * 4), (uint16_t)(b_addr + i * 4), (uint16_t)(out_addr + i * 4));
    }
}

/* ---- OP_RELU: branch-free 32-bit max-with-zero via a sign-derived mask ---- */

static void lower_relu(InstrBuf *buf, uint16_t in_addr, uint16_t out_addr, size_t n) {
    char r_msb[AVR_OPERAND_LEN], r_mask[AVR_OPERAND_LEN], r_val[AVR_OPERAND_LEN];
    fmt_reg(r_msb, REG_SIGN0);
    fmt_reg(r_mask, REG_SIGN1);
    fmt_reg(r_val, REG_ACC0);
    for (size_t i = 0; i < n; i++) {
        uint16_t elem_in = (uint16_t)(in_addr + i * 4);
        uint16_t elem_out = (uint16_t)(out_addr + i * 4);
        char addr_msb[AVR_OPERAND_LEN];
        fmt_addr(addr_msb, (uint16_t)(elem_in + 3));
        ins2(buf, "lds", r_msb, addr_msb);
        ins1(buf, "lsl", r_msb);            /* Carry = sign bit; r_msb's own value is discarded */
        ins2(buf, "sbc", r_mask, r_mask);   /* r_mask = 0xFF if negative else 0x00 ("zero-out" mask) */
        ins1(buf, "com", r_mask);           /* r_mask = 0x00 if negative else 0xFF ("keep" mask) */
        for (int b = 0; b < 4; b++) {
            char addr_i[AVR_OPERAND_LEN], addr_o[AVR_OPERAND_LEN];
            fmt_addr(addr_i, (uint16_t)(elem_in + b));
            fmt_addr(addr_o, (uint16_t)(elem_out + b));
            ins2(buf, "lds", r_val, addr_i);
            ins2(buf, "and", r_val, r_mask);
            ins2(buf, "sts", addr_o, r_val);
        }
    }
}

/* ---- OP_REQUANTIZE: compile-time 16-bit Q15 fixed multiplier + shift ---- */

typedef struct {
    int16_t m;  /* always in [16384, 32767]: mantissa*32768, mantissa in [0.5,1) */
    int shift;  /* right-shift count applied after the 32x16 multiply, before narrowing */
} FixedMultiplier;

#define REQUANT_MAX_SHIFT 40

static int compute_fixed_multiplier(double ratio, FixedMultiplier *out) {
    if (!(ratio > 0.0) || !isfinite(ratio)) {
        fprintf(stderr, "lower: requantize scale ratio %.9g is not a positive finite number\n", ratio);
        return -1;
    }
    int e;
    double mantissa = frexp(ratio, &e); /* ratio = mantissa * 2^e, mantissa in [0.5, 1.0) */
    long m = lround(mantissa * 32768.0);
    if (m >= 32768) {
        m = 16384;
        e += 1;
    }
    int shift = 15 - e;
    if (shift < 0 || shift > REQUANT_MAX_SHIFT) {
        fprintf(stderr, "lower: requantize scale ratio %.9g needs shift=%d, outside the supported [0,%d] range\n",
                ratio, shift, REQUANT_MAX_SHIFT);
        return -1;
    }
    out->m = (int16_t)m;
    out->shift = shift;
    return 0;
}

static void lower_requantize_element(InstrBuf *buf, uint16_t acc_addr, uint16_t out_addr,
                                      const FixedMultiplier *fm) {
    char r_a[4][AVR_OPERAND_LEN];
    for (int i = 0; i < 4; i++) fmt_reg(r_a[i], REG_ACC0 + i);
    /* MULSU (needed below for the sign-bearing top byte, a3) requires both
     * operands in r16-r23 -- REG_MAC_A/REG_MAC_B (16/17) qualify and are
     * free here (Requantize never runs concurrently with MatMul's own use
     * of them), unlike REG_SCRATCH0/1 (24/25) which would be out of range. */
    char r_m0[AVR_OPERAND_LEN], r_m1[AVR_OPERAND_LEN];
    fmt_reg(r_m0, REG_MAC_A);
    fmt_reg(r_m1, REG_MAC_B);
    char r_prod[REQUANT_PRODUCT_BYTES][AVR_OPERAND_LEN];
    for (int i = 0; i < REQUANT_PRODUCT_BYTES; i++) fmt_reg(r_prod[i], REG_PROD_BASE + i);
    char r0[AVR_OPERAND_LEN], r1[AVR_OPERAND_LEN], r_zero[AVR_OPERAND_LEN];
    fmt_reg(r0, 0);
    fmt_reg(r1, 1);
    fmt_reg(r_zero, REG_ZERO);

    /* load the 32-bit accumulator (a0 = LSB .. a3 = MSB, signed) */
    for (int i = 0; i < 4; i++) {
        char addr[AVR_OPERAND_LEN];
        fmt_addr(addr, (uint16_t)(acc_addr + i));
        ins2(buf, "lds", r_a[i], addr);
    }
    /* load the compile-time-constant multiplier (unsigned range, top bit
     * never set -- see compute_fixed_multiplier) */
    char imm[AVR_OPERAND_LEN];
    fmt_imm(imm, (uint8_t)(fm->m & 0xFF));
    ins2(buf, "ldi", r_m0, imm);
    fmt_imm(imm, (uint8_t)((fm->m >> 8) & 0xFF));
    ins2(buf, "ldi", r_m1, imm);

    /* clear the 48-bit product accumulator */
    for (int i = 0; i < REQUANT_PRODUCT_BYTES; i++) {
        ins1(buf, "clr", r_prod[i]);
    }

    /* Signed(32) x unsigned(16) multi-precision multiply, standard
     * byte-decomposed algorithm (structure per Atmel/Microchip application
     * notes AVR200/AVR201 "Multiply and Divide Routines" -- cited for the
     * algorithm only, not for any cost figure): treat a0..a2 as unsigned
     * bytes (correct in two's complement multi-word arithmetic) and only
     * a3 (the sign-bearing byte) as signed via mulsu. m0/m1 are always
     * non-negative (m <= 32767), so mul suffices for both. */
    const char *m_bytes[2] = {r_m0, r_m1};
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 2; j++) {
            const char *mnem = (i == 3) ? "mulsu" : "mul";
            ins2(buf, mnem, r_a[i], m_bytes[j]);
            int lo = i + j;
            ins2(buf, "add", r_prod[lo], r0);
            ins2(buf, "adc", r_prod[lo + 1], r1);
            for (int k = lo + 2; k < REQUANT_PRODUCT_BYTES; k++) {
                ins2(buf, "adc", r_prod[k], r_zero);
            }
        }
    }

    /* round-to-nearest before shifting: add 1 << (shift-1) as a 48-bit
     * compile-time constant (all 6 bytes, uniformly, even where 0 -- kept
     * simple/uniform rather than special-cased, and still constant-cost
     * either way). */
    if (fm->shift >= 1) {
        uint64_t round_const = (uint64_t)1 << (fm->shift - 1);
        for (int i = 0; i < REQUANT_PRODUCT_BYTES; i++) {
            uint8_t byte = (uint8_t)((round_const >> (8 * i)) & 0xFF);
            fmt_imm(imm, byte);
            ins2(buf, "ldi", r_m0, imm); /* r_m0 reused as scratch, no longer needed */
            ins2(buf, i == 0 ? "add" : "adc", r_prod[i], r_m0);
        }
    }

    /* arithmetic shift right by the compile-time-constant `shift`, across
     * the 6-byte register value (r_prod[0] = LSB .. r_prod[5] = MSB). */
    for (int s = 0; s < fm->shift; s++) {
        ins1(buf, "asr", r_prod[REQUANT_PRODUCT_BYTES - 1]);
        for (int i = REQUANT_PRODUCT_BYTES - 2; i >= 0; i--) {
            ins1(buf, "ror", r_prod[i]);
        }
    }

    /* Narrow to int8: the low byte is the requantized result. Compile-time
     * verification (see lower_requantize) already confirmed this model's
     * real worst-case magnitude fits in [-127,127] -- no runtime
     * saturation is performed. */
    char addr_out[AVR_OPERAND_LEN];
    fmt_addr(addr_out, out_addr);
    ins2(buf, "sts", addr_out, r_prod[0]);
}

static int lower_requantize(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t producer_id = op->inputs[0];
    double ratio = (double)graph->ops[producer_id].quant.scale / (double)op->quant.scale;

    /* compute_fixed_multiplier only sanity-checks the *shift range* here --
     * it cannot verify the actual output fits int8 without knowing real
     * input values. That verification happens once, up front, in
     * lower_verify_demo_forward_pass -- see its comment for why a static
     * worst-case-over-all-inputs bound is mathematically infeasible for
     * MinMax-calibrated per-tensor quantization on unbounded-support
     * inputs (confirmed empirically, not just in theory) and had to be
     * replaced with an exact check against the one fixed demo input this
     * Phase A program actually runs. */
    FixedMultiplier fm;
    if (compute_fixed_multiplier(ratio, &fm) != 0) {
        fprintf(stderr, "lower: requantize op %zu failed to derive a fixed-point multiplier\n", op_id);
        return -1;
    }

    size_t n = sram_layout_num_elements(op);
    uint16_t acc_addr = sram_layout_addr(layout, graph, producer_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    for (size_t i = 0; i < n; i++) {
        lower_requantize_element(buf, (uint16_t)(acc_addr + i * 4), (uint16_t)(out_addr + i), &fm);
    }
    return 0;
}

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
 * A single <<1 across the 3-byte accumulator then aligns the result --
 * NOT a 15-bit shift chain -- and the top two bytes are the Q15 product.
 * Truncates rather than rounds (no rounding-bias correction before the
 * shift); test_dsp_lower.c's tolerance check confirms this stays well
 * within the numpy.fft comparison tolerance used later in this plan.
 * Measured through the real avr-gcc while writing the Milestone 6 spec's
 * 2026-08-03 amendment: 72 bytes, 30 instructions. */
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

/* ---- OP_OUTPUT: copy to a fixed, dedicated address ---- */

static void lower_output(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t producer_id = op->inputs[0];
    size_t n = sram_layout_num_elements(op);
    uint16_t in_addr = sram_layout_addr(layout, graph, producer_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    char r[AVR_OPERAND_LEN];
    fmt_reg(r, REG_SCRATCH0);
    for (size_t i = 0; i < n; i++) {
        char addr_in[AVR_OPERAND_LEN], addr_out[AVR_OPERAND_LEN];
        fmt_addr(addr_in, (uint16_t)(in_addr + i));
        fmt_addr(addr_out, (uint16_t)(out_addr + i));
        ins2(buf, "lds", r, addr_in);
        ins2(buf, "sts", addr_out, r);
    }
}

/* ---- pre-pass: verify the actual demo input never overflows int8 through
 * any Requantize stage ---- */

/* A static worst-case-over-all-inputs bound (K x 127 x 127) is
 * mathematically infeasible to satisfy for MinMax-calibrated per-tensor
 * symmetric quantization on an unbounded-support input distribution: no
 * finite calibration sample size drives the overflow probability to zero,
 * so a sound worst-case bound is always far above what real (even
 * in-distribution) inputs produce -- confirmed empirically against this
 * project's own export/export_model.py calibration, not just in theory.
 *
 * What Phase A actually needs is narrower and fully decidable: this
 * program is compiled for exactly one fixed, compile-time-known
 * `demo_input`, not an arbitrary future one (see REPORT.md's limitations
 * on that scope cut). So instead of bounding all possible inputs, this
 * evaluates the real forward pass for the one input that will actually
 * run, in host int64 arithmetic using the identical fixed-point M/S
 * rescale (compute_fixed_multiplier) the generated AVR code uses, and
 * refuses to compile only if THAT computation overflows int8. */
int lower_verify_demo_forward_pass(const IrGraph *graph, const int8_t *demo_input, size_t demo_input_len) {
    int64_t **vals = calloc(graph->count, sizeof(int64_t *));
    int rc = 0;

    for (size_t i = 0; i < graph->count && rc == 0; i++) {
        const IrOp *op = &graph->ops[i];
        size_t n = sram_layout_num_elements(op);
        vals[i] = calloc(n, sizeof(int64_t));

        switch (op->kind) {
            case OP_INPUT: {
                size_t expected_bytes = n * sram_layout_elem_size(op->dtype);
                if (demo_input_len != expected_bytes) {
                    fprintf(stderr, "lower: demo input has %zu bytes, graph expects %zu\n", demo_input_len,
                            expected_bytes);
                    rc = -1;
                    break;
                }
                for (size_t k = 0; k < n; k++) vals[i][k] = demo_input[k];
                break;
            }
            case OP_CONST:
                if (op->dtype == DT_INT8) {
                    const int8_t *bytes = (const int8_t *)op->data;
                    for (size_t k = 0; k < n; k++) vals[i][k] = bytes[k];
                } else { /* DT_INT32, raw bytes are native-endian (host is little-endian x86, same as the AVR target) */
                    const int32_t *words = (const int32_t *)op->data;
                    for (size_t k = 0; k < n; k++) vals[i][k] = words[k];
                }
                break;
            case OP_MATMUL: {
                const IrOp *in_op = &graph->ops[op->inputs[0]];
                const IrOp *w_op = &graph->ops[op->inputs[1]];
                size_t k_dim = in_op->output_shape[in_op->output_shape_len - 1];
                for (size_t out_n = 0; out_n < n; out_n++) {
                    int64_t sum = 0;
                    for (size_t k = 0; k < k_dim; k++) {
                        sum += vals[op->inputs[0]][k] * vals[op->inputs[1]][out_n * k_dim + k];
                    }
                    vals[i][out_n] = sum;
                }
                break;
            }
            case OP_ADD:
                for (size_t k = 0; k < n; k++) vals[i][k] = vals[op->inputs[0]][k] + vals[op->inputs[1]][k];
                break;
            case OP_RELU:
                for (size_t k = 0; k < n; k++) vals[i][k] = vals[op->inputs[0]][k] > 0 ? vals[op->inputs[0]][k] : 0;
                break;
            case OP_REQUANTIZE: {
                size_t producer_id = op->inputs[0];
                double ratio = (double)graph->ops[producer_id].quant.scale / (double)op->quant.scale;
                FixedMultiplier fm;
                if (compute_fixed_multiplier(ratio, &fm) != 0) {
                    rc = -1;
                    break;
                }
                for (size_t k = 0; k < n; k++) {
                    int64_t product = vals[producer_id][k] * (int64_t)fm.m;
                    int64_t round_term = fm.shift >= 1 ? ((int64_t)1 << (fm.shift - 1)) : 0;
                    /* Arithmetic (sign-extending) right shift of a signed
                     * value is implementation-defined by the C standard,
                     * but universal in practice on every real-world
                     * compiler this project targets (GCC/Clang/MSVC) --
                     * same reliance this project already documents for
                     * assuming a little-endian host elsewhere (pb_reader.c). */
                    int64_t shifted = (product + round_term) >> fm.shift;
                    if (shifted > 127 || shifted < -127) {
                        fprintf(stderr,
                                "lower: requantize op %zu element %zu: demo input produces %lld, "
                                "outside int8 range -- refusing to compile rather than silently wrap\n",
                                i, k, (long long)shifted);
                        rc = -1;
                        break;
                    }
                    vals[i][k] = shifted;
                }
                break;
            }
            case OP_OUTPUT:
                for (size_t k = 0; k < n; k++) vals[i][k] = vals[op->inputs[0]][k];
                break;
            default:
                fprintf(stderr, "lower: op %zu has OpKind %d, which is out of scope for Phase A (ML path only)\n",
                        i, (int)op->kind);
                rc = -1;
                break;
        }
    }

    for (size_t i = 0; i < graph->count; i++) free(vals[i]);
    free(vals);
    return rc;
}

/* ---------------------------------------------------------------------- */

int lower_op(const IrGraph *graph, size_t op_id,
             const SramLayout *layout, const RegAllocResult *regalloc,
             const CostModel *cost_model,
             const int8_t *demo_input, size_t demo_input_len,
             Candidate *out) {
    /* lower_op always spills its own op's output to SRAM unconditionally
     * -- `regalloc` is accepted for interface symmetry with
     * candidates_generate, but a value's own regalloc->assignment doesn't
     * change how it stores itself; it's consumer-side information (see
     * regalloc.h) that codegen/candidates.c's cached-input MatMul
     * candidate reads instead, for ops it consumes. */
    (void)regalloc;

    const IrOp *op = &graph->ops[op_id];
    InstrBuf buf;
    instrbuf_init(&buf);

    switch (op->kind) {
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
        case OP_CONST: {
            uint16_t addr = sram_layout_addr(layout, graph, op_id, 0);
            lower_bytes(&buf, (const uint8_t *)op->data, op->data_len, addr);
            break;
        }
        case OP_MATMUL:
            lower_matmul(&buf, graph, layout, op_id);
            break;
        case OP_ADD:
            lower_add(&buf, graph, layout, op_id);
            break;
        case OP_RELU: {
            size_t producer_id = op->inputs[0];
            size_t n = sram_layout_num_elements(op);
            uint16_t in_addr = sram_layout_addr(layout, graph, producer_id, 0);
            uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
            lower_relu(&buf, in_addr, out_addr, n);
            break;
        }
        case OP_REQUANTIZE:
            if (lower_requantize(&buf, graph, layout, op_id) != 0) {
                free(buf.items);
                return -1;
            }
            break;
        case OP_OUTPUT:
            lower_output(&buf, graph, layout, op_id);
            break;
        default:
            fprintf(stderr, "lower: op %zu has OpKind %d, which is out of scope for Phase A (ML path only)\n",
                    op_id, (int)op->kind);
            free(buf.items);
            return -1;
    }

    return instrbuf_price(&buf, cost_model, out);
}
