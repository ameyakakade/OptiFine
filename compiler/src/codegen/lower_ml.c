#include "lower_internal.h"

#include "optifine/codegen/registers.h"

/* ---- OP_MATMUL: fully unrolled int8x8 MAC, 16->32 branch-free sign extension ---- */

void lower_matmul(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
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

void lower_add(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id) {
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

void lower_relu(InstrBuf *buf, uint16_t in_addr, uint16_t out_addr, size_t n) {
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
