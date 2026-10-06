#include "optifine/codegen/candidates.h"

#include <stdio.h>
#include <stdlib.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/registers.h"

void candidate_free(Candidate *candidate) {
    free(candidate->instructions);
    candidate->instructions = NULL;
    candidate->num_instructions = 0;
}

/* Candidate 2 for OP_MATMUL: pre-loads the reused activation input
 * (inputs[0], read once per output channel -- N times total, see
 * regalloc.h's next-use analysis) into free registers ONCE, then reuses
 * it via `mov` (1 cycle) instead of re-`lds`-ing it from SRAM (2 cycles)
 * on every one of the N output channels that need it. This is the
 * dominant redundancy in the naive baseline (lower_ml.c's
 * lower_matmul): fc1's MatMul re-loads its 16-byte input 8 times (128
 * loads for 16 unique bytes); this candidate loads each byte once, up to
 * MATMUL_CACHE_POOL_SIZE. Weight bytes are never reused -- each (n,k)
 * pair uses a distinct weight byte exactly once -- so they're still
 * `lds` per use here too; there's no reuse to exploit there. Bytes
 * beyond the cache pool's capacity (only relevant if K exceeds
 * MATMUL_CACHE_POOL_SIZE -- not the case for this project's graphs) fall
 * back to `lds` per use, identical to the baseline, for those k only:
 * this candidate is always at least as good as the baseline, never
 * worse. */
static int lower_matmul_cached(const IrGraph *graph, size_t op_id, const SramLayout *layout,
                                const CostModel *cost_model, Candidate *out) {
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

    size_t cached_k = k_dim < MATMUL_CACHE_POOL_SIZE ? k_dim : MATMUL_CACHE_POOL_SIZE;

    InstrBuf buf;
    instrbuf_init(&buf);

    /* Pre-load the cacheable prefix of the activation input once. */
    char cache_reg_str[MATMUL_CACHE_POOL_SIZE][AVR_OPERAND_LEN];
    for (size_t k = 0; k < cached_k; k++) {
        fmt_reg(cache_reg_str[k], kMatmulCacheRegs[k]);
        char addr[AVR_OPERAND_LEN];
        fmt_addr(addr, (uint16_t)(in_addr + k));
        ins2(&buf, "lds", cache_reg_str[k], addr);
    }

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
        ins1(&buf, "clr", r_acc0);
        ins1(&buf, "clr", r_acc1);
        ins1(&buf, "clr", r_acc2);
        ins1(&buf, "clr", r_acc3);
        for (size_t k = 0; k < k_dim; k++) {
            if (k < cached_k) {
                ins2(&buf, "mov", r_a, cache_reg_str[k]); /* cache hit: 1 cycle instead of lds's 2 */
            } else {
                char addr_in[AVR_OPERAND_LEN];
                fmt_addr(addr_in, (uint16_t)(in_addr + k));
                ins2(&buf, "lds", r_a, addr_in);
            }
            char addr_w[AVR_OPERAND_LEN];
            fmt_addr(addr_w, (uint16_t)(w_addr + n * k_dim + k));
            ins2(&buf, "lds", r_b, addr_w);
            ins2(&buf, "muls", r_a, r_b);        /* r1:r0 = signed 16-bit product */
            ins2(&buf, "mov", r_sign0, r1);
            ins1(&buf, "lsl", r_sign0);           /* Carry = sign bit of the product's high byte */
            ins2(&buf, "sbc", r_sign0, r_sign0);  /* r_sign0 = 0xFF if negative else 0x00 */
            ins2(&buf, "mov", r_sign1, r_sign0);
            ins2(&buf, "add", r_acc0, r0);
            ins2(&buf, "adc", r_acc1, r1);
            ins2(&buf, "adc", r_acc2, r_sign0);
            ins2(&buf, "adc", r_acc3, r_sign1);
        }
        char addr0[AVR_OPERAND_LEN], addr1[AVR_OPERAND_LEN], addr2[AVR_OPERAND_LEN], addr3[AVR_OPERAND_LEN];
        fmt_addr(addr0, (uint16_t)(out_addr + n * 4 + 0));
        fmt_addr(addr1, (uint16_t)(out_addr + n * 4 + 1));
        fmt_addr(addr2, (uint16_t)(out_addr + n * 4 + 2));
        fmt_addr(addr3, (uint16_t)(out_addr + n * 4 + 3));
        ins2(&buf, "sts", addr0, r_acc0);
        ins2(&buf, "sts", addr1, r_acc1);
        ins2(&buf, "sts", addr2, r_acc2);
        ins2(&buf, "sts", addr3, r_acc3);
    }

    return instrbuf_price(&buf, cost_model, out);
}

size_t candidates_generate(const IrGraph *graph, size_t op_id,
                            const SramLayout *layout, const RegAllocResult *regalloc,
                            const CostModel *cost_model,
                            const int8_t *demo_input, size_t demo_input_len,
                            Candidate *out_candidates, size_t max_candidates) {
    if (max_candidates == 0) {
        return 0;
    }
    size_t n = 0;

    /* Candidate 1: the existing, always-correct spill lowering (every
     * value reloaded from SRAM per use) -- the naive baseline, and always
     * a valid fallback for every OpKind. */
    if (lower_op(graph, op_id, layout, regalloc, cost_model, demo_input, demo_input_len, &out_candidates[n]) != 0) {
        fprintf(stderr, "candidates_generate: op %zu failed to lower\n", op_id);
        return 0;
    }
    n++;

    /* Candidate 2: only for OP_MATMUL, only when regalloc's next-use
     * analysis marked this op's activation input as worth caching (see
     * regalloc.h) -- true for every MatMul in this project's graphs,
     * since every MatMul has more than one output channel. */
    if (n < max_candidates && graph->ops[op_id].kind == OP_MATMUL) {
        size_t activation_id = graph->ops[op_id].inputs[0];
        if (regalloc->assignment[activation_id] == 1) {
            if (lower_matmul_cached(graph, op_id, layout, cost_model, &out_candidates[n]) == 0) {
                n++;
            }
        }
    }

    return n;
}
