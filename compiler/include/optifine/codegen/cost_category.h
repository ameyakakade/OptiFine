/* Maps a literal AVR opcode mnemonic (as emitted to .s, e.g. "muls") to the
 * cost_table.toml category key that prices it (e.g. "MUL"). Per SOURCES.md,
 * every cost_table.toml entry reduces to cycles(instruction) x a single
 * per-cycle constant -- every 1-cycle category is numerically identical
 * (2.8375 nJ) and every 2-cycle category is numerically identical
 * (5.675 nJ). So which same-cycle-count category a
 * new opcode maps to changes zero energy numbers; this table exists for
 * traceability, not because the specific choice is load-bearing. Dedicated
 * cost_table.toml rows with real AVR Instruction Set Manual section
 * citations for these opcodes are future work. */
#ifndef OPTIFINE_CODEGEN_COST_CATEGORY_H
#define OPTIFINE_CODEGEN_COST_CATEGORY_H

/* Returns the cost_table.toml category key for `avr_mnemonic` (case
 * sensitive, expects lowercase as emitted), or NULL if unmapped. Callers
 * must treat NULL as fatal -- never default to a silent zero cost. */
const char *avr_cost_category(const char *avr_mnemonic);

/* Opcodes no cost-table category can express (lpm at 3 cycles, rjmp, break)
 * are priced in energy as their cycle count times the active-mode per-cycle
 * energy. Returns that cycle count, or 0 when the opcode is not one of them,
 * in which case avr_cost_category supplies its energy. See cost_category.c
 * for why these are not given their own cost_table.toml entries. */
int avr_direct_cycles(const char *avr_mnemonic);

/* The AVR cycle model: CPU cycles `avr_mnemonic` takes on the ATmega128 (a
 * conditional branch at its taken cost), or 0 for an opcode the compiler
 * does not know. Independent of the energy model above. */
int avr_instr_cycles(const char *avr_mnemonic);

#endif /* OPTIFINE_CODEGEN_COST_CATEGORY_H */
