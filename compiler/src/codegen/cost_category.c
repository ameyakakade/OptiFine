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
    {"clr", "MOV"},     /* register-init family */
    {"com", "SUB"},     /* 1-cycle bucket, arbitrary within-bucket choice */
    {"and", "ADD"},     /* 1-cycle bucket, arbitrary within-bucket choice */
    {"asr", "SUB"},     /* 1-cycle bucket, arbitrary within-bucket choice */
    {"ror", "SUB"},     /* 1-cycle bucket, arbitrary within-bucket choice */
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
