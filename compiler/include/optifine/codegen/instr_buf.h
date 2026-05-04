/* Shared instruction-building and pricing utilities, factored out of
 * lower.c so candidates.c can build genuinely different instruction
 * sequences for the same op without duplicating this plumbing. Pure
 * mechanical extraction -- no arithmetic/lowering logic lives here. */
#ifndef OPTIFINE_CODEGEN_INSTR_BUF_H
#define OPTIFINE_CODEGEN_INSTR_BUF_H

#include <stdint.h>

#include "optifine/codegen/candidates.h"
#include "optifine/cost_model.h"

typedef struct {
    AvrInstr *items;
    size_t count;
    size_t capacity;
} InstrBuf;

void instrbuf_init(InstrBuf *b);
void instrbuf_push(InstrBuf *b, const char *mnemonic, int num_operands,
                    const char *o1, const char *o2, const char *o3);
void ins1(InstrBuf *b, const char *m, const char *o1);
void ins2(InstrBuf *b, const char *m, const char *o1, const char *o2);

void fmt_reg(char *out, int r);
void fmt_addr(char *out, uint16_t addr);
void fmt_imm(char *out, uint8_t v);

/* Converts a finished InstrBuf into a priced Candidate, taking ownership of
 * buf->items. Returns 0 on success, -1 (with an error printed) if any
 * instruction has no cost-category mapping or no cost_table.toml entry.
 * Every cost_table.toml entry is cycles x a single per-cycle constant (see
 * SOURCES.md's "Per-cycle energy constant" section, spec v2 section 13);
 * `cycles` is derived from energy rather than tracked separately, so the
 * 2.8375 constant here MUST be kept in sync with cost_table.toml's actual
 * per-cycle value. */
int instrbuf_price(InstrBuf *buf, const CostModel *cost_model, Candidate *out);

#endif /* OPTIFINE_CODEGEN_INSTR_BUF_H */
