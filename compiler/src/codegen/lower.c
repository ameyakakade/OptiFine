/* Lowering of one workload-graph op to AVR: the shared entry points
 * (lower.h), the data-movement ops (Input, Const, Output), program-end code,
 * and the dispatch to the per-family lowering files:
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

int lower_dsp_program_end(const IrGraph *graph, const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    ins0(&buf, "break");
    lower_fft_constant_data(&buf, graph);
    return instrbuf_price(&buf, cost_model, out);
}

int lower_dsp_constant_data(const IrGraph *graph, const CostModel *cost_model, Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    lower_fft_constant_data(&buf, graph);
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
            /* ir_verify guarantees the initializer is exactly the tensor
             * layout reserved; more would overwrite the next tensor. */
            size_t bytes = 0;
            OPTIFINE_INVARIANT(ir_op_tensor_size(op, NULL, &bytes) == 0 && op->data_len == bytes);
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
