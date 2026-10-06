#include "lower_internal.h"

#include "optifine/codegen/dsp32.h"
#include "optifine/codegen/registers.h"

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
void lower_peak_extract(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
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
