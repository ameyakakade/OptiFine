#include "lower_internal.h"

#include "optifine/codegen/dsp32.h"
#include "optifine/codegen/registers.h"

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
void lower_magnitude(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
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
