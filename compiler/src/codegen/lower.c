#include "optifine/codegen/lower.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/dsp32.h"
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
 * shift): the result is exactly floor(x*y / 32768), i.e. (x*y) >> 15, which
 * the FFT and full-pipeline tests check bit-exactly against integer oracles.
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

/* Test seam: the permutation itself, so a test can prove it is a bijection
 * without re-deriving it. */
size_t dsp_bit_reverse_index_test_hook(size_t i, int bits) {
    return dsp_bit_reverse_index(i, bits);
}

/* ---- OP_FFT_BUTTERFLY: one radix-2 decimation-in-time stage ----
 *
 * Convention, taken from the repository rather than assumed: radix-2 DIT
 * (spec v2 sections on the DSP workload), bit-reversal applied BEFORE the
 * stages (dsp_build.c pushes OP_BIT_REVERSE then six OP_FFT_BUTTERFLY), and
 * a forward transform, since the twiddle generator uses
 * angle = -2*pi*k/N, i.e. W^k = e^(-2*pi*i*k/N).
 *
 * Complex layout is DT_COMPLEX_Q15: 4 bytes per element, little-endian
 * int16 real then int16 imaginary.
 *
 * Per-butterfly equations, with p the lower index and q = p + half_block:
 *
 *     t_re = qmul(w_re, q_re) - qmul(w_im, q_im)
 *     t_im = qmul(w_re, q_im) + qmul(w_im, q_re)
 *     out[p] = p/2 + t/2
 *     out[q] = p/2 - t/2
 *
 * where qmul(x,y) = floor(x*y / 32768), the Q15 product, and /2 is an
 * arithmetic shift right (floor toward -inf).
 *
 * The halving happens BEFORE combining, not after. Combining first would
 * compute p + t, which reaches 2.0 in Q15 whenever |p| and |t| both approach
 * 1.0 and wraps int16 before the shift could halve it -- the /2 meant to
 * bound growth would then be applied to an already-corrupted sum. Scaling
 * each operand first keeps every intermediate in range by construction, at a
 * cost of at most one extra LSB of truncation per output and no extra
 * instructions. Stage scaling is 1/2 per stage, 1/64 over the six stages.
 *
 * No saturation is performed: the per-stage halving is what bounds growth,
 * and the invariant |out| <= max(|p|,|t|) keeps every value inside Q15.
 *
 * Structure: the butterfly body is emitted once per stage and run from
 * counted loops. Data addresses advance through X (input) and Y (output);
 * the twiddle comes from the canonical 32-entry program-memory table through
 * Z. Intermediates live at fixed scratch addresses, which are loop-invariant
 * even though the data addresses are not, so the already-validated
 * lower_fixed_mul_q15 / lower_add16 / lower_sub16 / lower_asr16 helpers apply
 * unchanged.
 *
 * Geometry of stage s (half = 2^s, blocks = 32/half, span = 4*half bytes
 * between a butterfly's p and q):
 *
 *     stage  blocks  half  twiddle k = j*blocks   Z step after 4x lpm Z+
 *       0      32      1   0                      -4  (W^0 reused)
 *       1      16      2   0,16                   +60
 *       2       8      4   0,8,..,24              +28
 *       3       4      8   0,4,..,28              +12
 *       4       2     16   0,2,..,30              +4
 *       5       1     32   0,1,..,31              0
 *
 * Stage 0 is one flat loop of 32: p and q are adjacent, so X and Y walk the
 * buffers without ever stepping back. Stages 1-5 are nested: a block loop
 * resets Z to the table base and wraps a loop of `half` butterflies, and each
 * butterfly steps X/Y forward by span-4 from p to q and back by span to the
 * next p. After the butterfly loop X/Y sit at block_start+span, so the block
 * loop adds span to reach the next block. Stage 5 has a single block and no
 * block loop.
 *
 * The butterfly loop holds REG_DSP_LOOP_COUNTER (r3), and the body owns every
 * register from r16 up, so the block counter lives in one SRAM byte
 * (DSP_SCRATCH_FFT_OUTER_COUNT) touched only at block boundaries -- 7-8
 * cycles per block instead of a permanently reserved register.
 *
 * Buffers: every stage reads its input op's tensor and writes its own, which
 * sram_layout places in disjoint regions, so no stage overwrites a value it
 * has yet to read. BitReverse -> stage 0 -> ... -> stage 5, and the final
 * transform is stage 5's buffer. */

/* Butterfly scratch cells, offsets within layout->dsp_scratch_addr. */
#define BF_AC   0
#define BF_BD   2
#define BF_AD   4
#define BF_BC   6
#define BF_TRE  8
#define BF_TIM  10
#define BF_TWRE 12
#define BF_TWIM 14
#define BF_PRE  16
#define BF_PIM  18
#define BF_QRE  20
#define BF_QIM  22
#define BF_HALF 24
#define BF_SCRATCH_BYTES 26
_Static_assert(BF_SCRATCH_BYTES <= DSP_SCRATCH_FFT_OUTER_COUNT,
               "butterfly scratch must not reach the FFT block counter");
_Static_assert(DSP_SCRATCH_FFT_OUTER_COUNT < DSP_SCRATCH_BYTES,
               "FFT block counter must lie inside the reserved DSP scratch region");

#define DSP_TWIDDLE_LABEL ".Ltw"
#define DSP_TWIDDLE_ENTRIES 32

/* Host-side canonical twiddle, Q15. Shared with the table emitter and with
 * the tests' oracle so both cannot drift. */
void dsp_twiddle_q15(int k, int16_t *wr, int16_t *wi) {
    double a = -2.0 * M_PI * (double)k / (double)DSP_FFT_SIZE;
    long r = lround(cos(a) * 32767.0), i = lround(sin(a) * 32767.0);
    *wr = (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
    *wi = (int16_t)(i > 32767 ? 32767 : (i < -32768 ? -32768 : i));
}

/* Emits the 32-entry canonical table as program-memory data. Callers must
 * place this where control flow cannot reach it (after a terminating break);
 * the interpreter refuses to execute a data word, and a test asserts the
 * placement. */
void dsp_emit_twiddle_table(InstrBuf *buf) {
    ins1(buf, AVR_LABEL_MNEMONIC, DSP_TWIDDLE_LABEL);
    for (int k = 0; k < DSP_TWIDDLE_ENTRIES; k++) {
        int16_t wr, wi;
        dsp_twiddle_q15(k, &wr, &wi);
        ins_data_word(buf, (uint16_t)wr);
        ins_data_word(buf, (uint16_t)wi);
    }
}

/* Loads one byte through a pointer and parks it at an absolute scratch cell. */
static void ptr_byte_to_scratch(InstrBuf *buf, char ptr, uint16_t dst) {
    char r[AVR_OPERAND_LEN], p[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r, REG_SCRATCH0);
    fmt_ptr(p, ptr, 1);
    fmt_addr(a, dst);
    ins2(buf, ptr == 'Z' ? "lpm" : "ld", r, p);
    ins2(buf, "sts", a, r);
}
static void scratch_byte_to_ptr(InstrBuf *buf, uint16_t src, char ptr) {
    char r[AVR_OPERAND_LEN], p[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r, REG_SCRATCH0);
    fmt_ptr(p, ptr, 1);
    fmt_addr(a, src);
    ins2(buf, "lds", r, a);
    ins2(buf, "st", p, r);
}
static void load_ptr_imm(InstrBuf *buf, int lo_reg, uint16_t addr) {
    char r[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(r, lo_reg);     fmt_lo8(imm, addr); ins2(buf, "ldi", r, imm);
    fmt_reg(r, lo_reg + 1); fmt_hi8(imm, addr); ins2(buf, "ldi", r, imm);
}

/* Adds a signed compile-time constant to pointer pair lo_reg:lo_reg+1.
 * ADIW/SBIW reach 0..63; past that, SUBI/SBCI subtract the negated constant.
 * Both forms take 2 cycles; the second is one word longer. */
static void ptr_add(InstrBuf *buf, int lo_reg, int delta) {
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

/* One butterfly: twiddle from Z, p and q from X, out[p] and out[q] to Y.
 * `z_step` is applied after the four lpm Z+ loads; `to_q` after reading or
 * writing p, `to_next_p` after reading or writing q. Stage 0 passes 0/0,
 * since its q directly follows p and its next p directly follows q. */
static void emit_butterfly(InstrBuf *buf, uint16_t sc, int z_step, int to_q, int to_next_p) {
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWRE));
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWRE + 1));
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWIM));
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWIM + 1));
    ptr_add(buf, 30, z_step);

    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PRE));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PRE + 1));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PIM));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PIM + 1));
    ptr_add(buf, 26, to_q);
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QRE));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QRE + 1));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QIM));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QIM + 1));
    ptr_add(buf, 26, to_next_p);

    /* t = W * q */
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWRE), (uint16_t)(sc + BF_QRE), (uint16_t)(sc + BF_AC));
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWIM), (uint16_t)(sc + BF_QIM), (uint16_t)(sc + BF_BD));
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWRE), (uint16_t)(sc + BF_QIM), (uint16_t)(sc + BF_AD));
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWIM), (uint16_t)(sc + BF_QRE), (uint16_t)(sc + BF_BC));
    lower_sub16(buf, (uint16_t)(sc + BF_AC), (uint16_t)(sc + BF_BD), (uint16_t)(sc + BF_TRE));
    lower_add16(buf, (uint16_t)(sc + BF_AD), (uint16_t)(sc + BF_BC), (uint16_t)(sc + BF_TIM));

    /* halve each operand, then combine */
    lower_asr16(buf, (uint16_t)(sc + BF_PRE), (uint16_t)(sc + BF_HALF));
    lower_asr16(buf, (uint16_t)(sc + BF_TRE), (uint16_t)(sc + BF_TRE));
    lower_add16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TRE), (uint16_t)(sc + BF_AC));
    lower_sub16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TRE), (uint16_t)(sc + BF_BD));
    lower_asr16(buf, (uint16_t)(sc + BF_PIM), (uint16_t)(sc + BF_HALF));
    lower_asr16(buf, (uint16_t)(sc + BF_TIM), (uint16_t)(sc + BF_TIM));
    lower_add16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TIM), (uint16_t)(sc + BF_AD));
    lower_sub16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TIM), (uint16_t)(sc + BF_BC));

    /* out[p] then out[q] */
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AC), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AC + 1), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AD), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AD + 1), 'Y');
    ptr_add(buf, 28, to_q);
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BD), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BD + 1), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BC), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BC + 1), 'Y');
    ptr_add(buf, 28, to_next_p);
}

static void load_twiddle_base(InstrBuf *buf) {
    char zl[AVR_OPERAND_LEN], zh[AVR_OPERAND_LEN], sym[AVR_OPERAND_LEN];
    fmt_reg(zl, 30); fmt_reg(zh, 31);
    fmt_lo8_sym(sym, DSP_TWIDDLE_LABEL, 0); ins2(buf, "ldi", zl, sym);
    fmt_hi8_sym(sym, DSP_TWIDDLE_LABEL, 0); ins2(buf, "ldi", zh, sym);
}

static void lower_fft_butterfly_stage(InstrBuf *buf, const IrGraph *graph,
                                       const SramLayout *layout, size_t op_id, int stage) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    uint16_t sc = layout->dsp_scratch_addr;

    assert(stage >= 0 && stage < DSP_FFT_LOG2);
    int half = 1 << stage;
    int blocks = DSP_FFT_SIZE / (half * 2);
    int span = half * 4; /* bytes from p to q */

    load_ptr_imm(buf, 26, in_addr);   /* X */
    load_ptr_imm(buf, 28, out_addr);  /* Y */

    if (half == 1) {
        /* Stage 0: W^0 throughout, so Z rewinds over the four bytes it just
         * read, and X/Y run straight through the buffers. */
        load_twiddle_base(buf);
        LoopCtx loop;
        instrbuf_loop_begin(buf, &loop, (uint32_t)blocks, REG_DSP_LOOP_COUNTER);
        emit_butterfly(buf, sc, -4, 0, 0);
        instrbuf_loop_end(buf, &loop);
        return;
    }

    LoopCtx block_loop;
    if (blocks > 1) {
        instrbuf_loop_begin_sram(buf, &block_loop, (uint32_t)blocks,
                                 (uint16_t)(sc + DSP_SCRATCH_FFT_OUTER_COUNT));
    }
    load_twiddle_base(buf);
    LoopCtx bf_loop;
    instrbuf_loop_begin(buf, &bf_loop, (uint32_t)half, REG_DSP_LOOP_COUNTER);
    emit_butterfly(buf, sc, blocks * 4 - 4, span - 4, -span);
    instrbuf_loop_end(buf, &bf_loop);
    if (blocks > 1) {
        ptr_add(buf, 26, span);
        ptr_add(buf, 28, span);
        instrbuf_loop_end(buf, &block_loop);
    }
}

/* Which OP_FFT_BUTTERFLY this op is, by position among the graph's others. */
static int dsp_butterfly_stage_index(const IrGraph *graph, size_t op_id) {
    int stage = 0;
    for (size_t i = 0; i < op_id; i++) {
        if (graph->ops[i].kind == OP_FFT_BUTTERFLY) stage++;
    }
    return stage;
}

/* Test/fixture seam: stage 0 as a self-contained program -- the butterfly
 * loop, the terminating break, then the twiddle table. The table must share a
 * candidate with the code that references it so lo8/hi8(.Ltw) resolves, and
 * it must follow the break so control flow cannot execute it. */
int lower_fft_stage0_test_hook(const IrGraph *graph, size_t op_id,
                                const SramLayout *layout, const CostModel *cost_model,
                                Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    lower_fft_butterfly_stage(&buf, graph, layout, op_id, 0);
    ins0(&buf, "break");
    dsp_emit_twiddle_table(&buf);
    return instrbuf_price(&buf, cost_model, out);
}

/* Test/fixture seam: FFT stages first..last as one self-contained program,
 * laid out exactly like the stage 0 seam. Each stage reads the previous
 * stage's buffer, so a run leaves every intermediate stage output in SRAM for
 * the tests to check one by one. */
int lower_fft_stages_test_hook(const IrGraph *graph, int first_stage, int last_stage,
                               const SramLayout *layout, const CostModel *cost_model,
                               Candidate *out) {
    if (first_stage < 0 || last_stage >= DSP_FFT_LOG2 || first_stage > last_stage) {
        fprintf(stderr, "lower: bad FFT stage range %d..%d\n", first_stage, last_stage);
        return -1;
    }
    InstrBuf buf;
    instrbuf_init(&buf);
    int stage = 0;
    for (size_t i = 0; i < graph->count; i++) {
        if (graph->ops[i].kind != OP_FFT_BUTTERFLY) continue;
        if (stage >= first_stage && stage <= last_stage) {
            lower_fft_butterfly_stage(&buf, graph, layout, i, stage);
        }
        stage++;
    }
    ins0(&buf, "break");
    dsp_emit_twiddle_table(&buf);
    return instrbuf_price(&buf, cost_model, out);
}

/* ---- OP_MAGNITUDE: exact |X| per bin ----
 *
 * Semantics, per bin i of the COMPLEX_Q15 input:
 *
 *     mag[i] = floor( sqrt( (int32)re^2 + (int32)im^2 ) )
 *
 * an UNSIGNED 16-bit integer in the same Q15 units as re and im, stored in the
 * op's 2-byte DT_FIXED_Q15 slot. Over arbitrary int16 inputs the sum reaches
 * 2^31 (re = im = -32768) and mag reaches 46340, which fits uint16 but not
 * int16, so the value is defined as unsigned; PeakExtract's planned compare
 * is the unsigned sub/sbc borrow. In this pipeline the value never gets that
 * far: Window bounds |sample| by its Hamming coefficient c[n], and each
 * output bin of the 1/64-scaled FFT is bounded by sum(c)/64 plus at most 15
 * of accumulated rounding (6 stages x < 2.5 each, ell-infinity gain <= 1 per
 * stage) -- about 17,470 for this window, well inside 32767, so the stored
 * bit pattern also reads identically as signed Q15. test_dsp_magnitude
 * derives that bound from the real coefficients and checks it.
 *
 * Not an approximation, and not the abandoned Q15-square + isqrt16 form, whose
 * Q15 rescaling of each square put the result off by sqrt(32768).
 *
 * Structure: X walks the complex input, Y the output, one 64-trip loop with
 * its counter in scratch (the body owns every register from r3 up), and
 * inside it dsp32_isqrt's fixed 16-trip loop on REG_DSP_LOOP_COUNTER:
 *
 *     r16:r17 = re, r18:r19 = im        (ld X+ x4)
 *     A = re*re, B = im*im              (dsp32_mul16x16; exact, each <= 2^30)
 *     A += B                            (uint32 sum <= 2^31)
 *     B = isqrt32(A)                    (C, T = r16-r19 and r25 as temporaries)
 *     out = B[15:0]                     (st Y+ x2)
 *
 * r16-r19 hold re/im only until both squares are formed; isqrt then reuses
 * them as its T quad, which is why the loads happen first each iteration. */
static void lower_magnitude(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    size_t n = sram_layout_num_elements(op);
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    uint16_t sc = layout->dsp_scratch_addr;

    load_ptr_imm(buf, 26, in_addr);   /* X */
    load_ptr_imm(buf, 28, out_addr);  /* Y */

    LoopCtx bins;
    instrbuf_loop_begin_sram(buf, &bins, (uint32_t)n, (uint16_t)(sc + DSP_SCRATCH_MAG_COUNT));

    char r[AVR_OPERAND_LEN], xp[AVR_OPERAND_LEN], yp[AVR_OPERAND_LEN];
    fmt_ptr(xp, 'X', 1);
    fmt_ptr(yp, 'Y', 1);
    for (int i = 0; i < 4; i++) {        /* re lo, re hi, im lo, im hi */
        fmt_reg(r, REG_DSP_OP_A_LO + i);
        ins2(buf, "ld", r, xp);
    }
    dsp32_mul16x16(buf, DSP32_A, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI, REG_DSP_OP_A_LO, REG_DSP_OP_A_HI);
    dsp32_mul16x16(buf, DSP32_B, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI, REG_DSP_OP_B_LO, REG_DSP_OP_B_HI);
    dsp32_add(buf, DSP32_A, DSP32_B);
    dsp32_isqrt(buf, DSP32_A, DSP32_B, DSP32_C, DSP32_T, REG_DSP_MASK, REG_DSP_LOOP_COUNTER);
    for (int i = 0; i < 2; i++) {        /* the root is < 2^16: its low two bytes */
        fmt_reg(r, DSP32_B + i);
        ins2(buf, "st", yp, r);
    }

    instrbuf_loop_end(buf, &bins);
}

/* ---- OP_PEAK_EXTRACT: top-k magnitudes, deterministic, branch-free ----
 *
 * Contract (IR: FIXED_Q15[n] -> FIXED_Q15[k], n = 64, k = DSP_MAX_PEAKS = 8):
 * out[0..k) are the k largest input values, largest first, compared as
 * UNSIGNED 16-bit -- Magnitude's values reach 46340, past int16. The output
 * carries values only; which bin produced each is internal. That identity is
 * still fixed: candidates are ordered by (value descending, index ascending),
 * and every index is chosen at most once.
 *
 * Identity, not value, is what excludes a winner. Each pass marks its winner
 * in selected[], a byte per bin in scratch that this op zeroes on entry, and
 * only unmarked bins are eligible. Zeroing the winning VALUE instead -- the
 * old plan -- cannot exclude anything when the winner is already 0: with an
 * all-zero spectrum it re-selects bin 0 every pass.
 *
 * One pass, over candidates i = 0..n-1 in ascending order:
 *
 *     eligible = selected[i] == 0
 *     better   = eligible & (!found | v[i] > best)      -- strict >
 *     if better: best = v[i], best_idx = i, found = 1  -- masked selects
 *
 * Strict > on an ascending scan is the tie rule: a later equal value never
 * displaces an earlier one, so the lower index wins. `found` makes the first
 * eligible bin the initial best whatever its value, so 0 is an ordinary value
 * rather than a "nothing yet" sentinel. At least n-k+1 bins stay eligible, so
 * every pass finds one. The winner's selected[] byte gets the pass counter
 * (k for the first pass down to 1), nonzero and distinct, which also leaves
 * the selection order readable for tests.
 *
 * Every data decision is a mask, so the executed path and the cycle count do
 * not depend on the input. Loops: selected[] clearing (n trips, r3), then k
 * passes (r8) around the n-candidate scan (r3). */
static void lower_peak_extract(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    size_t n = sram_layout_num_elements(&graph->ops[in_id]);
    size_t k = sram_layout_num_elements(op);
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    uint16_t sel = (uint16_t)(layout->dsp_scratch_addr + DSP_SCRATCH_PK_SELECTED);

    char r[AVR_OPERAND_LEN], s[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    char xp[AVR_OPERAND_LEN], x[AVR_OPERAND_LEN], yp[AVR_OPERAND_LEN], zp[AVR_OPERAND_LEN];
    fmt_ptr(xp, 'X', 1); fmt_ptr(x, 'X', 0); fmt_ptr(yp, 'Y', 1); fmt_ptr(zp, 'Z', 1);

    /* selected[0..n) = 0, owned and initialised here: no state from any
     * earlier op is assumed. */
    load_ptr_imm(buf, 26, sel);
    LoopCtx clear;
    instrbuf_loop_begin(buf, &clear, (uint32_t)n, REG_DSP_LOOP_COUNTER);
    fmt_reg(s, REG_ZERO);
    ins2(buf, "st", xp, s);
    instrbuf_loop_end(buf, &clear);

    fmt_reg(r, REG_PK_ONES); fmt_imm(imm, 0xFF); ins2(buf, "ldi", r, imm);
    load_ptr_imm(buf, 28, out_addr);  /* Y walks the output across passes */

    LoopCtx passes;
    instrbuf_loop_begin(buf, &passes, (uint32_t)k, REG_PK_PASS);
    load_ptr_imm(buf, 30, in_addr);   /* Z: values */
    load_ptr_imm(buf, 26, sel);       /* X: selected[] */
    const int cleared[] = {REG_PK_BEST_LO, REG_PK_BEST_HI, REG_PK_BEST_IDX, REG_PK_FOUND, REG_PK_IDX};
    for (size_t i = 0; i < sizeof(cleared) / sizeof(cleared[0]); i++) {
        fmt_reg(r, cleared[i]); ins1(buf, "clr", r);
    }

    LoopCtx scan;
    instrbuf_loop_begin(buf, &scan, (uint32_t)n, REG_DSP_LOOP_COUNTER);
    fmt_reg(r, REG_PK_V_LO); ins2(buf, "ld", r, zp);
    fmt_reg(r, REG_PK_V_HI); ins2(buf, "ld", r, zp);
    fmt_reg(r, REG_PK_SEL);  ins2(buf, "ld", r, xp);
    dsp8_zero_mask(buf, REG_PK_ELIGIBLE, REG_PK_SEL);
    dsp16_gt_mask(buf, REG_PK_BETTER, REG_PK_V_LO, REG_PK_V_HI, REG_PK_BEST_LO, REG_PK_BEST_HI);
    /* better = eligible & (!found | gt), as eligible & ~(found & ~gt): no `or` */
    fmt_reg(r, REG_PK_BETTER);
    ins1(buf, "com", r);
    fmt_reg(s, REG_PK_FOUND);    ins2(buf, "and", r, s);
    ins1(buf, "com", r);
    fmt_reg(s, REG_PK_ELIGIBLE); ins2(buf, "and", r, s);
    dsp8_select(buf, REG_PK_BEST_LO, REG_PK_V_LO, REG_PK_BETTER, REG_PK_TMP);
    dsp8_select(buf, REG_PK_BEST_HI, REG_PK_V_HI, REG_PK_BETTER, REG_PK_TMP);
    dsp8_select(buf, REG_PK_BEST_IDX, REG_PK_IDX, REG_PK_BETTER, REG_PK_TMP);
    dsp8_select(buf, REG_PK_FOUND, REG_PK_ONES, REG_PK_BETTER, REG_PK_TMP);
    fmt_reg(r, REG_PK_IDX); fmt_imm(imm, 0xFF); ins2(buf, "subi", r, imm);   /* index += 1 */
    instrbuf_loop_end(buf, &scan);

    /* selected[best_idx] = pass marker; out[pass] = best */
    load_ptr_imm(buf, 26, sel);
    fmt_reg(r, 26); fmt_reg(s, REG_PK_BEST_IDX); ins2(buf, "add", r, s);
    fmt_reg(r, 27); fmt_reg(s, REG_ZERO);        ins2(buf, "adc", r, s);
    fmt_reg(s, REG_PK_PASS);    ins2(buf, "st", x, s);
    fmt_reg(s, REG_PK_BEST_LO); ins2(buf, "st", yp, s);
    fmt_reg(s, REG_PK_BEST_HI); ins2(buf, "st", yp, s);
    instrbuf_loop_end(buf, &passes);
}

static void emit_constant_data(InstrBuf *buf, const IrGraph *graph) {
    for (size_t i = 0; i < graph->count; i++) {
        if (graph->ops[i].kind == OP_FFT_BUTTERFLY) {
            dsp_emit_twiddle_table(buf);
            return;
        }
    }
}

int lower_dsp_program_end(const IrGraph *graph, const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    ins0(&buf, "break");
    emit_constant_data(&buf, graph);
    return instrbuf_price(&buf, cost_model, out);
}

int lower_dsp_constant_data(const IrGraph *graph, const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    emit_constant_data(&buf, graph);
    return instrbuf_price(&buf, cost_model, out);
}

/* ---- OP_OUTPUT: copy to a fixed, dedicated address ---- */

static void lower_output(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
    const IrOp *op = &graph->ops[op_id];
    size_t producer_id = op->inputs[0];
    /* Byte count, not element count: OP_OUTPUT is a flat byte copy, and the
     * DSP path's output is DT_FIXED_Q15 (2 bytes/element), not the ML path's
     * DT_INT8. With a bare element count this copied exactly half the DSP
     * peak list. Same elem_size fix lower_op's OP_INPUT length check already
     * applies, and non-regressive for the ML path where elem_size == 1. */
    size_t n = sram_layout_num_elements(op) * sram_layout_elem_size(op->dtype);
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
        case OP_WINDOW:
            lower_window(&buf, graph, layout, op_id);
            break;
        case OP_BIT_REVERSE:
            lower_bit_reverse(&buf, graph, layout, op_id);
            break;
        case OP_FFT_BUTTERFLY: {
            int stage = dsp_butterfly_stage_index(graph, op_id);
            if (stage >= DSP_FFT_LOG2) {
                fprintf(stderr, "lower: FFT stage %d exceeds the %d stages of a %d-point transform\n",
                        stage, DSP_FFT_LOG2, DSP_FFT_SIZE);
                free(buf.items);
                return -1;
            }
            lower_fft_butterfly_stage(&buf, graph, layout, op_id, stage);
            break;
        }
        case OP_MAGNITUDE: {
            const IrOp *in_op = &graph->ops[op->inputs[0]];
            size_t n = sram_layout_num_elements(op);
            if (in_op->dtype != DT_COMPLEX_Q15 || op->dtype != DT_FIXED_Q15 ||
                sram_layout_num_elements(in_op) != n || n < 1 || n > 256) {
                fprintf(stderr, "lower: OP_MAGNITUDE %zu expects COMPLEX_Q15[n] -> FIXED_Q15[n], "
                                "1 <= n <= 256\n", op_id);
                free(buf.items);
                return -1;
            }
            lower_magnitude(&buf, graph, layout, op_id);
            break;
        }
        case OP_PEAK_EXTRACT: {
            const IrOp *in_op = &graph->ops[op->inputs[0]];
            size_t n = sram_layout_num_elements(in_op), k = sram_layout_num_elements(op);
            /* indices and pass markers are single bytes, and at least one bin
             * must stay eligible for every pass */
            if (in_op->dtype != DT_FIXED_Q15 || op->dtype != DT_FIXED_Q15 ||
                n < 1 || n > 255 || k < 1 || k > n ||
                DSP_SCRATCH_PK_SELECTED + n > DSP_SCRATCH_BYTES) {
                fprintf(stderr, "lower: OP_PEAK_EXTRACT %zu expects FIXED_Q15[n] -> FIXED_Q15[k], "
                                "1 <= k <= n <= 255, n selected[] bytes in scratch\n", op_id);
                free(buf.items);
                return -1;
            }
            lower_peak_extract(&buf, graph, layout, op_id);
            break;
        }
        default:
            fprintf(stderr, "lower: op %zu has OpKind %d, which has no lowering\n", op_id, (int)op->kind);
            free(buf.items);
            return -1;
    }

    return instrbuf_price(&buf, cost_model, out);
}
