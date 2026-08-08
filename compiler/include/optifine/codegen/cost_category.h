/* Maps a literal AVR opcode mnemonic (as emitted to .s, e.g. "muls") to the
 * cost_table.toml category key that prices it (e.g. "MUL"). Per SOURCES.md /
 * spec v2 section 12, every cost_table.toml entry reduces to
 * cycles(instruction) x a single per-cycle constant -- every 1-cycle
 * category is numerically identical (10.625 nJ) and every 2-cycle category
 * is numerically identical (21.25 nJ). So which same-cycle-count category a
 * new opcode maps to changes zero energy numbers; this table exists for
 * traceability, not because the specific choice is load-bearing. Dedicated
 * cost_table.toml rows with real AVR Instruction Set Manual section
 * citations for these opcodes are deferred to milestone 7's sourcing pass. */
#ifndef OPTIFINE_CODEGEN_COST_CATEGORY_H
#define OPTIFINE_CODEGEN_COST_CATEGORY_H

/* Returns the cost_table.toml category key for `avr_mnemonic` (case
 * sensitive, expects lowercase as emitted), or NULL if unmapped. Callers
 * must treat NULL as fatal -- never default to a silent zero cost. */
const char *avr_cost_category(const char *avr_mnemonic);

/* Cycle count for opcodes no cost-table category can express (currently only
 * `lpm`, at 3 cycles). Returns 0 when the opcode is not one of them, in which
 * case avr_cost_category applies. See cost_category.c for why these are not
 * given their own cost_table.toml entries. */
int avr_direct_cycles(const char *avr_mnemonic);

#endif /* OPTIFINE_CODEGEN_COST_CATEGORY_H */
