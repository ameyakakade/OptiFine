/* Lowering of one workload-graph op to AVR: the shared entry points
 * (lower.h), program-end code, and the dispatch to the per-family kernel
 * files. The data-movement ops (Input, Const, Output) are lowered through the
 * generic MIR path instead (hir_to_mir.c). Kernel files:
 *
 *   lower_ml.c          MatMul, Add, Relu
 *   lower_requantize.c  Requantize and the demo-input forward-pass check
 *   lower_dsp.c         Q15 and pointer primitives, Window, BitReverse
 *   lower_fft.c         FFT butterfly stages and the twiddle table
 *   lower_magnitude.c   Magnitude
 *   lower_peak.c        PeakExtract
 *
 * lower_internal.h is the private interface between them. */
#include "optifine/codegen/lower.h"

#include <stdio.h>
#include <stdlib.h>

#include "optifine/codegen/hir_to_mir.h"
#include "optifine/codegen/registers.h"
#include "optifine/dsp_build.h"
#include "optifine/invariant.h"
#include "lower_internal.h"

/* ---------------------------------------------------------------------- */

int lower_init_zero_reg(const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    char r2[AVR_OPERAND_LEN];
    fmt_reg(r2, REG_ZERO);
    ins1(&buf, "clr", r2);
    return instrbuf_price(&buf, cost_model, out);
}

int lower_dsp_program_end(const IrGraph *graph, const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    ins0(&buf, "break");
    lower_fft_constant_data(&buf, graph);
    return instrbuf_price(&buf, cost_model, out);
}

/* ---------------------------------------------------------------------- */

int lower_op(const IrGraph *graph, size_t op_id,
             const SramLayout *layout, const ReuseAnalysis *reuse,
             const CostModel *cost_model,
             const int8_t *demo_input, size_t demo_input_len,
             Candidate *out) {
    /* The naive lowering ignores the reuse analysis: every op reads its
     * inputs from SRAM on each use and stores its output to SRAM. `reuse`
     * is accepted only so lower_op and candidates_generate share one
     * signature; candidates.c's cached-input MatMul is its sole reader. */
    (void)reuse;

    const IrOp *op = &graph->ops[op_id];
    InstrBuf buf;
    instrbuf_init(&buf);

    switch (op->kind) {
        case OP_INPUT:
        case OP_CONST:
        case OP_OUTPUT:
            /* Data movement has one implementation: the generic MIR path
             * (hir_to_mir.c), which whole programs use too. */
            return hir_lower_data_movement(graph, op_id, layout, cost_model, demo_input, demo_input_len, out);
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
        case OP_WINDOW:
            lower_window(&buf, graph, layout, op_id);
            break;
        case OP_BIT_REVERSE:
            lower_bit_reverse(&buf, graph, layout, op_id);
            break;
        case OP_FFT_BUTTERFLY: {
            int stage = dsp_butterfly_stage_index(graph, op_id);
            OPTIFINE_INVARIANT(stage < DSP_FFT_LOG2); /* ir_verify caps the stage count */
            lower_fft_butterfly_stage(&buf, graph, layout, op_id, stage);
            break;
        }
        case OP_MAGNITUDE: {
            /* COMPLEX_Q15[n] -> FIXED_Q15[n] with n <= 256 is ir_verify's
             * contract; the 8-bit counted loop depends on the bound. */
            size_t n = sram_layout_num_elements(op);
            OPTIFINE_INVARIANT(n >= 1 && n <= 256);
            lower_magnitude(&buf, graph, layout, op_id);
            break;
        }
        case OP_PEAK_EXTRACT: {
            const IrOp *in_op = &graph->ops[op->inputs[0]];
            size_t n = sram_layout_num_elements(in_op), k = sram_layout_num_elements(op);
            /* Indices and pass markers are single bytes and at least one bin
             * must stay eligible for every pass: 1 <= k <= n <= 255 is
             * ir_verify's contract. Whether selected[] fits the scratch arena
             * is this target's constraint, so it is checked here. */
            OPTIFINE_INVARIANT(k >= 1 && k <= n && n <= 255);
            if (DSP_SCRATCH_PK_SELECTED + n > DSP_SCRATCH_BYTES) {
                fprintf(stderr, "lower: OP_PEAK_EXTRACT %zu needs %zu selected[] bytes, more than the "
                                "%d-byte DSP scratch arena holds\n", op_id, n, DSP_SCRATCH_BYTES);
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
