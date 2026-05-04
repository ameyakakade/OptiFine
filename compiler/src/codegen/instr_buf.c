#include "optifine/codegen/instr_buf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "optifine/codegen/cost_category.h"

void instrbuf_init(InstrBuf *b) {
    b->items = NULL;
    b->count = 0;
    b->capacity = 0;
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

void fmt_reg(char *out, int r) {
    snprintf(out, AVR_OPERAND_LEN, "r%d", r);
}
void fmt_addr(char *out, uint16_t addr) {
    snprintf(out, AVR_OPERAND_LEN, "0x%04X", addr);
}
void fmt_imm(char *out, uint8_t v) {
    snprintf(out, AVR_OPERAND_LEN, "0x%02X", v);
}

int instrbuf_price(InstrBuf *buf, const CostModel *cost_model, Candidate *out) {
    double energy_nj = 0.0;
    for (size_t i = 0; i < buf->count; i++) {
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
        energy_nj += entry->energy_nj;
    }
    out->instructions = buf->items;
    out->num_instructions = buf->count;
    out->cycles = (uint32_t)llround(energy_nj / 2.8375);
    out->energy_nj = energy_nj;
    return 0;
}
