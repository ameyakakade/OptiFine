/* Shared AVR register assignments for this project's codegen. These are
 * intra-op scratch registers, live only within one op's own lowering --
 * NOT what regalloc.c's assignment[] represents (see regalloc.h). r2 is
 * the one exception: it is a program-global always-zero register (see
 * lower_init_zero_reg in lower.h). */
#ifndef OPTIFINE_CODEGEN_REGISTERS_H
#define OPTIFINE_CODEGEN_REGISTERS_H

#define REG_ZERO 2      /* always 0 after lower_init_zero_reg; carry-propagation source */
#define REG_MAC_A 16    /* muls/mulsu require operands in r16-r31 */
#define REG_MAC_B 17
#define REG_ACC0 18     /* 32-bit accumulator, LSB..MSB, live across one MatMul output element */
#define REG_ACC1 19
#define REG_ACC2 20
#define REG_ACC3 21
#define REG_SIGN0 22    /* sign-extension / ReLU mask scratch */
#define REG_SIGN1 23
#define REG_SCRATCH0 24 /* general scratch (Const/Input byte loading, Requantize temporaries) */
#define REG_SCRATCH1 25
/* r3..r8: 48-bit (6-byte) product accumulator for Requantize's 32x16 wide
 * multiply. Free (not used by any of the above). */
#define REG_PROD_BASE 3
#define REQUANT_PRODUCT_BYTES 6

/* Registers never touched by lower_matmul's own scratch use (REG_MAC_A/B,
 * REG_ACC0-3, REG_SIGN0/1, r0/r1) -- safe to repurpose as extra storage
 * for the duration of one MatMul's own execution only (this is a
 * within-one-op register cache, not cross-op residency across the whole
 * program -- see regalloc.h's module comment for why the latter isn't
 * attempted):
 *   - r9-r15, r26-r31: unused anywhere in lower.c.
 *   - r24/r25 (REG_SCRATCH0/1): used by lower_bytes/lower_output/
 *     lower_requantize_element, but never by lower_matmul.
 *   - r3-r8 (REG_PROD_BASE..+REQUANT_PRODUCT_BYTES): Requantize's 48-bit
 *     product buffer. Safe too, for a different reason than the others --
 *     it IS used elsewhere in the same program (by every Requantize op),
 *     but never *concurrently* with a MatMul: this is a straight-line
 *     program with no interleaving, so by the time any Requantize op
 *     runs, every MatMul that used this pool has already finished and
 *     stored its result to SRAM.
 * codegen/candidates.c's cached-input MatMul candidate uses these to hold
 * the reused activation input. 21 registers covers every MatMul in this
 * project's graphs (largest K is 16, fc1's input) with room to spare. */
#define MATMUL_CACHE_POOL_SIZE 21
extern const int kMatmulCacheRegs[MATMUL_CACHE_POOL_SIZE];

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

/* --- DSP 32-bit register-resident primitive layer (codegen/dsp32.c) ---
 *
 * Magnitude needs exact `isqrt32(re^2 + im^2)`, and the only affordable way
 * to run 16 isqrt iterations 64 times is to keep the working values in
 * registers rather than round-tripping every one through SRAM. These three
 * quads are that working set.
 *
 * Quads start on even registers because `movw` requires an even register
 * pair; DSP32_A/B/C are therefore movw-addressable as two pairs each.
 *
 * THE CONTRACT, which FFT and Magnitude lowering must not violate:
 *
 *   r0, r1        hardwired mul destination. Clobbered by every mul/muls/
 *                 mulsu. Never hold anything across a multiply.
 *   r2            REG_ZERO, always zero, program-global. Read-only here.
 *   r3            REG_DSP_LOOP_COUNTER. Counted-loop trip counter for DSP
 *                 bodies. It has to live below r16 because every register
 *                 from r16 up is already committed inside a butterfly body
 *                 (r16-r23 to the Q15 multiply, r24/r25 to byte scratch,
 *                 r26-r31 to X/Y/Z), and it must not be one of the DSP32
 *                 quads either. `ldi` cannot target r0-r15, so
 *                 instrbuf_loop_begin stages the trip count through
 *                 REG_SCRATCH0 for counters down here.
 *   r4  - r7      DSP32_A. Caller-owned operand / accumulator.
 *   r8  - r11     DSP32_B. Caller-owned operand.
 *   r12 - r15     DSP32_C. Result / helper-owned temporary. A helper that
 *                 declares it clobbers C may overwrite it freely.
 *   r16 - r19     16-bit operand window (REG_DSP_OP_*). muls/mulsu require
 *                 r16-r23, so the 16x16->32 multiply's inputs live here.
 *                 Also DSP32_T, a fourth quad, but ONLY while no multiply
 *                 operand is live: dsp32_isqrt uses it as its t = res + bit
 *                 temporary, and it performs no multiply. Magnitude's squares
 *                 have consumed r16-r19 before isqrt starts.
 *   r20 - r23     multiply partial-product scratch (REG_DSP_ACC_P*,
 *                 REG_DSP_SIGNEXT). Clobbered by dsp32_mul16x16.
 *   r24, r25      REG_SCRATCH0/1, byte scratch.
 *   r26 - r31     X, Y, Z pointers. Never used as data by this layer.
 *
 * Inputs, outputs and temporaries therefore never alias: a helper writes
 * only its declared destination quad plus the scratch window its doc
 * comment names. The one overlap that would matter -- the 16-bit multiply
 * window (r16-r23) against the quads -- cannot occur, because r16-r23 lie
 * entirely outside r4-r15.
 *
 * ML-path safety: none of r4-r15 is live during any ML op. r4-r8 fall
 * inside Requantize's r3-r8 product buffer and r9-r15 inside the MatMul
 * cache pool, but a --dsp build never lowers an ML op and vice versa
 * (main.c rejects the combination), so the two windows are never live in
 * the same program. This is the same non-concurrency argument the MatMul
 * cache pool already rests on, applied across paths rather than within
 * one. */
#define REG_DSP_LOOP_COUNTER 3
#define DSP32_A 4
#define DSP32_B 8
#define DSP32_C 12
#define DSP32_T 16  /* aliases REG_DSP_OP_A_LO..REG_DSP_OP_B_HI -- see above */
#define DSP32_BYTES 4
/* dsp32_isqrt's keep/discard mask byte. REG_SCRATCH1 rather than
 * REG_SCRATCH0, so it never meets the loop machinery's trip-count staging or
 * the SRAM outer-counter close, both of which go through REG_SCRATCH0. */
#define REG_DSP_MASK 25

/* OP_PEAK_EXTRACT (lower.c) does no 32-bit arithmetic and no multiply, so it
 * owns these as plain bytes for its duration -- the same caller-owned use of
 * the DSP32 quads and the 16-bit window every DSP op makes, never live across
 * an op boundary:
 *   r4:r5  best value this pass      r16:r17  candidate value
 *   r6     best index this pass      r18      candidate's selected[] byte
 *   r7     found (0x00 / 0xFF)       r19      eligible mask
 *   r8     pass counter (k..1)       r20      candidate index
 *                                    r21      "better" mask
 *                                    r22      select temporary
 *                                    r23      constant 0xFF
 * r3 is the candidate loop's counter (and the selected[] clearing loop's
 * before it), r2 is read as zero, X/Y/Z are selected[] / output / input. */
#define REG_PK_BEST_LO 4
#define REG_PK_BEST_HI 5
#define REG_PK_BEST_IDX 6
#define REG_PK_FOUND 7
#define REG_PK_PASS 8
#define REG_PK_V_LO 16
#define REG_PK_V_HI 17
#define REG_PK_SEL 18
#define REG_PK_ELIGIBLE 19
#define REG_PK_IDX 20
#define REG_PK_BETTER 21
#define REG_PK_TMP 22
#define REG_PK_ONES 23

#endif /* OPTIFINE_CODEGEN_REGISTERS_H */
