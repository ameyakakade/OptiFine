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

    /* --- DSP counted-loop machinery ---
     *
     * All nine map onto categories that already exist rather than adding new
     * ones, for a specific reason: `cost_table.toml` is a recorded input of
     * every retained experiment (each manifest under `sim/fixtures/` pins
     * its SHA-256), so it is kept to the categories the ML path established.
     * Reusing categories costs nothing, because every entry is
     * `cycles x one constant` (SOURCES.md) and these are placed by
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
    {"breq", "COMPLEX_ADD"},   /* same 2-cycle taken cost as BRNE */

    /* Comparisons, logical shift and exclusive-or: all 1-cycle ALU ops, placed
     * in 1-cycle families. `lsr` is deliberately separate from `asr`: the
     * 32-bit isqrt operates on unsigned values, where an arithmetic shift
     * would smear the sign bit. */
    {"cp",   "SUB"},
    {"cpc",  "SUB"},
    {"lsr",  "SUB"},
    {"eor",  "ADD"},

    /* Immediate subtract, low byte then with borrow: the 16-bit pointer step
     * the FFT needs between a butterfly's p and q elements, which are up to
     * 128 bytes apart -- beyond ADIW/SBIW's 0..63. Both are 1-cycle ALU ops,
     * the same family as SUB/SBC. */
    {"subi", "SUB"},
    {"sbci", "SUB"},
};

/* Opcodes whose cycle count no cost_table.toml category expresses.
 *
 * `lpm` takes 3 cycles and every category in the table is 1 or 2, so there is
 * nothing honest to map it onto. Rather than add an entry -- cost_table.toml
 * is a recorded input of every retained experiment -- these are priced
 * directly as `cycles x the per-cycle constant`, which is precisely what
 * every category in the table already reduces to (SOURCES.md).
 * The indirection through a category name is a lookup convenience, not part of
 * the energy model. */
static const struct { const char *mnemonic; int cycles; } kDirectCycles[] = {
    {"lpm", 3},
    /* RJMP is 2 cycles and unconditional. It exists here because BRNE only
     * reaches +/-64 words; a DSP butterfly body is several hundred bytes, so
     * a long loop closes with an inverted BREQ over an RJMP instead. */
    {"rjmp", 2},
    /* BREAK halts the simulator and costs 1 cycle. This is the "+1 fixed
     * harness overhead" of every simulated program
     * (sim/fixtures/active_ml/smoke.avrora.txt: 7 instruction cycles, 8
     * reported); pricing it makes that constant explicit instead of magic. */
    {"break", 1},
};

int avr_direct_cycles(const char *avr_mnemonic) {
    if (!avr_mnemonic) return 0;
    for (size_t i = 0; i < sizeof(kDirectCycles) / sizeof(kDirectCycles[0]); i++) {
        if (strcmp(kDirectCycles[i].mnemonic, avr_mnemonic) == 0) {
            return kDirectCycles[i].cycles;
        }
    }
    return 0;
}

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
