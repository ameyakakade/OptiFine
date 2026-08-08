#include "optifine/codegen/instr_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <assert.h>

#include "optifine/codegen/cost_category.h"

/* Must stay in sync with cost_table.toml's per-cycle constant; see
 * SOURCES.md "Per-cycle energy constant". */
#define AVR_CYCLE_ENERGY_NJ 2.8375

void instrbuf_init(InstrBuf *b) {
    b->items = NULL;
    b->count = 0;
    b->capacity = 0;
    b->num_loops = 0;
}

void instrbuf_push(InstrBuf *b, const char *mnemonic, int num_operands,
                    const char *o1, const char *o2, const char *o3) {
    if (b->count == b->capacity) {
        size_t new_cap = b->capacity ? b->capacity * 2 : 64;
        b->items = realloc(b->items, new_cap * sizeof(AvrInstr));
        b->capacity = new_cap;
    }
    AvrInstr *ins = &b->items[b->count++];
    memset(ins, 0, sizeof(*ins));
    snprintf(ins->mnemonic, AVR_MNEMONIC_LEN, "%s", mnemonic);
    ins->num_operands = num_operands;
    if (num_operands >= 1) snprintf(ins->operands[0], AVR_OPERAND_LEN, "%s", o1);
    if (num_operands >= 2) snprintf(ins->operands[1], AVR_OPERAND_LEN, "%s", o2);
    if (num_operands >= 3) snprintf(ins->operands[2], AVR_OPERAND_LEN, "%s", o3);
}

void ins1(InstrBuf *b, const char *m, const char *o1) {
    instrbuf_push(b, m, 1, o1, NULL, NULL);
}
void ins2(InstrBuf *b, const char *m, const char *o1, const char *o2) {
    instrbuf_push(b, m, 2, o1, o2, NULL);
}
void ins0(InstrBuf *b, const char *m) {
    instrbuf_push(b, m, 0, NULL, NULL, NULL);
}

int avr_instr_is_label(const AvrInstr *instr) {
    return strcmp(instr->mnemonic, AVR_LABEL_MNEMONIC) == 0;
}

void fmt_reg(char *out, int r) {
    snprintf(out, AVR_OPERAND_LEN, "r%d", r);
}
void fmt_addr(char *out, uint16_t addr) {
    snprintf(out, AVR_OPERAND_LEN, "0x%04X", addr);
}
void fmt_imm(char *out, uint8_t v) {
    snprintf(out, AVR_OPERAND_LEN, "0x%02X", v);
}
void fmt_ptr(char *out, char reg, int post_increment) {
    snprintf(out, AVR_OPERAND_LEN, post_increment ? "%c+" : "%c", reg);
}
void fmt_ptr_disp(char *out, char reg, int displacement) {
    snprintf(out, AVR_OPERAND_LEN, "%c+%d", reg, displacement);
}
void fmt_lo8(char *out, uint16_t addr) {
    snprintf(out, AVR_OPERAND_LEN, "0x%02X", (unsigned)(addr & 0xFF));
}
void fmt_hi8(char *out, uint16_t addr) {
    snprintf(out, AVR_OPERAND_LEN, "0x%02X", (unsigned)((addr >> 8) & 0xFF));
}

/* Label names are unique per InstrBuf, which is all that is needed: each
 * Candidate is emitted as its own straight run of assembly and never spliced
 * into another. Named after the loop's region index so a label in generated
 * assembly maps back to a region in the pricing table. */

void instrbuf_loop_begin(InstrBuf *b, LoopCtx *ctx, uint32_t trip, int counter_reg) {
    /* trip must fit the 8-bit counter register `dec`/`brne` drive, and a
     * zero trip would wrap to 256 rather than skipping the body. Both are
     * compile-time properties of this project's fixed-size DSP pipeline, so
     * an assert is the right check: there is no runtime path that can reach
     * it with a bad value. */
    assert(trip >= 1 && trip <= 256);
    assert(b->num_loops < INSTRBUF_MAX_LOOPS);
    ctx->trip = trip;
    ctx->counter_reg = counter_reg;
    ctx->region = b->num_loops++;
    snprintf(ctx->label, AVR_OPERAND_LEN, ".Ldsp%zu", ctx->region);

    char r[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(r, counter_reg);
    /* 256 iterations are expressed as ldi 0: dec wraps 0 -> 255 and the loop
     * runs the full 256 times, the standard AVR counted-loop idiom. */
    fmt_imm(imm, (uint8_t)(trip & 0xFF));
    ins2(b, "ldi", r, imm);
    ins1(b, AVR_LABEL_MNEMONIC, ctx->label);
    ctx->body_first = b->count;
}

void instrbuf_loop_end(InstrBuf *b, LoopCtx *ctx) {
    char r[AVR_OPERAND_LEN];
    fmt_reg(r, ctx->counter_reg);
    ins1(b, "dec", r);
    ins1(b, "brne", ctx->label);
    b->loops[ctx->region].first = ctx->body_first;
    b->loops[ctx->region].last = b->count - 1; /* includes dec + brne */
    b->loops[ctx->region].trip = ctx->trip;
}

int instrbuf_price(InstrBuf *buf, const CostModel *cost_model, Candidate *out) {
    double energy_nj = 0.0;
    for (size_t i = 0; i < buf->count; i++) {
        /* A label is an assembler directive, not an instruction: it occupies
         * no flash and consumes no cycles, so it is skipped here rather than
         * given a zero-energy cost_table.toml entry it would not deserve. */
        if (avr_instr_is_label(&buf->items[i])) {
            continue;
        }
        const char *category = avr_cost_category(buf->items[i].mnemonic);
        if (!category) {
            fprintf(stderr, "instr_buf: opcode '%s' has no cost category mapping\n", buf->items[i].mnemonic);
            free(buf->items);
            return -1;
        }
        const CostEntry *entry = cost_model_lookup(cost_model, category);
        if (!entry) {
            fprintf(stderr, "instr_buf: cost category '%s' (for opcode '%s') has no cost_table.toml entry\n",
                    category, buf->items[i].mnemonic);
            free(buf->items);
            return -1;
        }
        /* Executed cost, not emitted cost: an instruction inside a counted
         * loop runs once per iteration, and once per iteration of every
         * enclosing loop. num_instructions below stays the emitted count,
         * because that is what occupies flash. */
        uint32_t repeat = 1;
        for (size_t l = 0; l < buf->num_loops; l++) {
            if (i >= buf->loops[l].first && i <= buf->loops[l].last) {
                repeat *= buf->loops[l].trip;
            }
        }
        energy_nj += entry->energy_nj * (double)repeat;
    }
    /* BRANCH is priced at BRNE's taken cost (2 cycles). Each loop falls
     * through exactly once at 1 cycle, so give back one cycle per region --
     * exact rather than approximately right. */
    for (size_t l = 0; l < buf->num_loops; l++) {
        uint32_t outer = 1;
        for (size_t o = 0; o < buf->num_loops; o++) {
            if (o != l && buf->loops[l].first >= buf->loops[o].first &&
                buf->loops[l].last <= buf->loops[o].last) {
                outer *= buf->loops[o].trip;
            }
        }
        energy_nj -= AVR_CYCLE_ENERGY_NJ * (double)outer;
    }
    out->instructions = buf->items;
    out->num_instructions = buf->count;
    out->cycles = (uint32_t)llround(energy_nj / AVR_CYCLE_ENERGY_NJ);
    out->energy_nj = energy_nj;
    return 0;
}
