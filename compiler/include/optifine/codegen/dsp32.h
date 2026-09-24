/* 32-bit register-resident primitives for the DSP path.
 *
 * Exact magnitude needs isqrt32(re^2 + im^2), and running 16 isqrt iterations
 * over 64 bins is only affordable if the working values stay in registers
 * instead of round-tripping through SRAM on every operation. These helpers are
 * that layer: they operate on the DSP32_A/B/C register quads declared in
 * codegen/registers.h, which also states the aliasing contract every caller
 * must respect.
 *
 * Deliberately narrow. There is no general 32-bit ALU here -- only the
 * operations the fixed 64-point Q15 pipeline actually performs. Nothing in
 * this file is reachable from the ML path.
 *
 * Byte order within a quad is little-endian: quad+0 is the least significant
 * byte, quad+3 the most significant, matching how the rest of this project
 * lays out multi-byte values in SRAM. */
#ifndef OPTIFINE_CODEGEN_DSP32_H
#define OPTIFINE_CODEGEN_DSP32_H

#include <stdint.h>

#include "optifine/codegen/instr_buf.h"

/* quad = 0 */
void dsp32_clear(InstrBuf *b, int quad);

/* quad = sign_extend_32(hi:lo), where hi:lo is a 16-bit two's-complement
 * value in two registers. The upper two bytes become 0x0000 or 0xFFFF.
 * Clobbers: nothing beyond `quad`. Does not read the carry flag. */
void dsp32_sext16(InstrBuf *b, int quad, int lo_reg, int hi_reg);

/* dst += src / dst -= src, full 4-byte carry/borrow chain. */
void dsp32_add(InstrBuf *b, int dst, int src);
void dsp32_sub(InstrBuf *b, int dst, int src);

/* dst = -dst, two's complement across all four bytes. */
void dsp32_neg(InstrBuf *b, int dst);

/* dst <<= 1. Used to turn a 32-bit product into a Q15 result: the Q15
 * product is (x*y) >> 15, and shifting the 32-bit product left by one makes
 * the wanted bits land in the top two bytes, which is cheaper than a 15-step
 * right shift and exact. */
void dsp32_lsl(InstrBuf *b, int dst);

/* dst >>= 1, LOGICAL (zero shifted in). The isqrt32 working values are
 * unsigned, so an arithmetic shift would smear a sign bit that is really a
 * magnitude bit. */
void dsp32_lsr(InstrBuf *b, int dst);

/* Sets the carry flag to the borrow of (a - b) over four bytes, leaving both
 * operands intact: carry clear means a >= b, treating both as UNSIGNED.
 * Uses cp/cpc, whose conjunctive Z behaviour makes the multi-byte compare
 * work. Clobbers no register. */
void dsp32_cmp(InstrBuf *b, int a, int b_quad);

/* dst(32) = (int16)(xh:xl) * (int16)(yh:yl), signed 16x16 -> 32.
 * x and y registers must lie in r16-r23 (muls/mulsu operand constraint).
 * Clobbers: dst, r0, r1, and REG_DSP_SIGNEXT. */
void dsp32_mul16x16(InstrBuf *b, int dst, int xlo, int xhi, int ylo, int yhi);

/* dst = src, as two movw. Both quads must start on an even register. */
void dsp32_mov(InstrBuf *b, int dst, int src);

/* quad = value. Bytes that are zero become clr; the rest are staged through
 * `scratch_reg` (r16-r31, since ldi cannot target r0-r15) unless the byte's
 * own register can take ldi directly. Clobbers: quad, scratch_reg. */
void dsp32_load_imm(InstrBuf *b, int quad, uint32_t value, int scratch_reg);

/* quad &= mask, the same mask byte applied to all four bytes. With a mask of
 * 0x00 or 0xFF this is a branch-free select between the value and zero. */
void dsp32_and_mask(InstrBuf *b, int quad, int mask_reg);

/* Immediately after dsp32_cmp(a, b): mask_reg = 0xFF if a >= b (unsigned),
 * else 0x00. `sbc m,m` turns the compare's borrow into a whole byte and `com`
 * inverts it. Consumes exactly the carry dsp32_cmp just produced, so it must
 * follow it with nothing in between. Clobbers: mask_reg. */
void dsp8_ge_mask(InstrBuf *b, int mask_reg);

/* --- 8/16-bit mask primitives (PeakExtract) ---
 *
 * Each produces a whole-byte mask, 0xFF for true and 0x00 for false, from a
 * compare it performs itself -- none reads a flag set before it -- so a data
 * decision becomes and/eor arithmetic instead of a branch. */

/* mask = 0xFF if (a_hi:a_lo) > (b_hi:b_lo) as UNSIGNED 16-bit, else 0x00.
 * cp/cpc compute b - a for its borrow only; sbc turns the borrow into the
 * mask. Unsigned throughout: 0x8000 > 0x7FFF. Clobbers: mask_reg. */
void dsp16_gt_mask(InstrBuf *b, int mask_reg, int a_lo, int a_hi, int b_lo, int b_hi);

/* mask = 0xFF if reg == 0, else 0x00: cp REG_ZERO,reg borrows exactly when
 * reg != 0. Needs REG_ZERO to hold zero. Clobbers: mask_reg. */
void dsp8_zero_mask(InstrBuf *b, int mask_reg, int reg);

/* dst = mask ? src : dst, as dst ^= (dst ^ src) & mask. Clobbers: dst,
 * tmp_reg. mask must be 0x00 or 0xFF. */
void dsp8_select(InstrBuf *b, int dst, int src, int mask_reg, int tmp_reg);

/* res = floor(sqrt(num)), unsigned 32-bit num, exact over all of uint32.
 *
 * Bit-by-bit restoring square root with a FIXED 16 iterations (one per result
 * bit), as a counted loop on `counter_reg`:
 *
 *     res = 0; bit = 1 << 30
 *     repeat 16:
 *         t = res + bit
 *         keep = (num >= t)                 -- mask, no branch
 *         num -= t & keep
 *         res = (res >> 1) + (bit & keep)
 *         bit >>= 2
 *
 * Every data decision is a mask, so the executed path -- and the cycle count
 * -- is identical for every input; the only branch is the loop's own.
 *
 * `num`, `res`, `bit` and `tmp` are four distinct movw-aligned quads.
 * Clobbers: all four (num ends as the remainder, bit as 0), mask_reg,
 * counter_reg, and REG_SCRATCH0 (trip-count staging when counter_reg < 16).
 * Reads no incoming flag. */
void dsp32_isqrt(InstrBuf *b, int num, int res, int bit, int tmp, int mask_reg, int counter_reg);

/* SRAM <-> quad, absolute addressing. */
void dsp32_load(InstrBuf *b, int quad, uint16_t addr);
void dsp32_store(InstrBuf *b, int quad, uint16_t addr);

/* 16-bit halves, for moving Q15 values in and out of the 16-bit window. */
void dsp16_load(InstrBuf *b, int lo_reg, int hi_reg, uint16_t addr);
void dsp16_store(InstrBuf *b, int lo_reg, int hi_reg, uint16_t addr);

#endif /* OPTIFINE_CODEGEN_DSP32_H */
