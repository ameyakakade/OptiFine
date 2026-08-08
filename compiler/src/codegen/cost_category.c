#include "optifine/codegen/cost_category.h"

#include <string.h>

typedef struct {
    const char *mnemonic;
    const char *category;
} CategoryEntry;

/* 1-cycle and 2-cycle opcodes this project's naive codegen (compiler/src/
 * codegen/lower.c) emits beyond the 7 already priced directly in
 * cost_table.toml. Bucket choice within a cycle-count class is arbitrary
 * (see header comment) except where the opcode family genuinely matches an
 * existing category (muls/mulsu -> MUL, adc -> ADD, sbc -> SUB, lsl is the
 * same physical opcode as `add Rd,Rd`). */
static const CategoryEntry kCategories[] = {
    {"add", "ADD"},
    {"sub", "SUB"},
    {"mov", "MOV"},
    {"ldi", "LDI"},
    {"mul", "MUL"},
    {"lds", "LD_SRAM"},
    {"sts", "ST_SRAM"},
    {"muls", "MUL"},    /* signed 8x8->16, same cycle count as MUL */
    {"mulsu", "MUL"},   /* signed x unsigned 8x8->16, same cycle count as MUL */
    {"adc", "ADD"},     /* add with carry, same cycle count as ADD */
    {"sbc", "SUB"},     /* subtract with carry, same cycle count as SUB */
    {"lsl", "ADD"},     /* assembler alias for `add Rd,Rd` -- literally the same opcode */
    {"rol", "ADD"},     /* assembler alias for `adc Rd,Rd`, same precedent as lsl->ADD */
    {"clr", "MOV"},     /* register-init family */
    {"com", "SUB"},     /* 1-cycle bucket, arbitrary within-bucket choice */
    {"and", "ADD"},     /* 1-cycle bucket, arbitrary within-bucket choice */
    {"asr", "SUB"},     /* 1-cycle bucket, arbitrary within-bucket choice */
    {"ror", "SUB"},     /* 1-cycle bucket, arbitrary within-bucket choice */

    /* --- DSP counted-loop machinery (milestone 6) ---
     *
     * All nine map onto categories that already exist rather than adding new
     * ones, for a specific reason: `cost_table.toml` is a recorded input of
     * the retained Phase B experiment (`sim/fixtures/phase_b/manifest.json`
     * pins its SHA-256), so extending that file would invalidate the
     * canonical result's provenance for opcodes the ML path never emits.
     * Reusing categories costs nothing, because every entry is
     * `cycles x one constant` (spec v2 section 12) and these are placed by
     * cycle count.
     *
     * Pointer loads/stores are genuinely the 2-cycle SRAM access
     * LD_SRAM/ST_SRAM name, reached through X/Y/Z instead of a 16-bit
     * absolute address. `movw` is a register move and `dec` a 1-cycle ALU
     * op, so both sit in their natural families. ADIW/SBIW are 16-bit adds,
     * which is what COMPLEX_ADD already prices ("2x ADD", 2 cycles). BRNE's
     * taken cost is also 2 cycles and lands in the same class -- placement
     * by cycle count, not a claim that a branch resembles an addition; see
     * this file's header on within-bucket choice being arbitrary. BRNE's
     * not-taken cost is 1 cycle, and instrbuf_price corrects for the single
     * fall-through per loop rather than leaving it overcounted. */
    {"ld",   "LD_SRAM"},
    {"ldd",  "LD_SRAM"},
    {"st",   "ST_SRAM"},
    {"std",  "ST_SRAM"},
    {"movw", "MOV"},
    {"dec",  "SUB"},
    {"adiw", "COMPLEX_ADD"},
    {"sbiw", "COMPLEX_ADD"},
    {"brne", "COMPLEX_ADD"},
};

const char *avr_cost_category(const char *avr_mnemonic) {
    if (!avr_mnemonic) {
        return NULL;
    }
    size_t n = sizeof(kCategories) / sizeof(kCategories[0]);
    for (size_t i = 0; i < n; i++) {
        if (strcmp(kCategories[i].mnemonic, avr_mnemonic) == 0) {
            return kCategories[i].category;
        }
    }
    return NULL;
}
