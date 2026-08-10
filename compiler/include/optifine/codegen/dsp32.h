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

/* SRAM <-> quad, absolute addressing. */
void dsp32_load(InstrBuf *b, int quad, uint16_t addr);
void dsp32_store(InstrBuf *b, int quad, uint16_t addr);

/* 16-bit halves, for moving Q15 values in and out of the 16-bit window. */
void dsp16_load(InstrBuf *b, int lo_reg, int hi_reg, uint16_t addr);
void dsp16_store(InstrBuf *b, int lo_reg, int hi_reg, uint16_t addr);

#endif /* OPTIFINE_CODEGEN_DSP32_H */
