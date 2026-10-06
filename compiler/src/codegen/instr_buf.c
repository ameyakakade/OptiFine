#include "optifine/codegen/instr_buf.h"
#include "optifine/invariant.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>


#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/registers.h"

/* Must stay in sync with cost_table.toml's per-cycle constant; see
 * SOURCES.md "Per-cycle energy constant". */
#define AVR_CYCLE_ENERGY_NJ 2.8375

void instrbuf_init(InstrBuf *b) {
    b->items = NULL;
    b->count = 0;
    b->capacity = 0;
    b->num_loops = 0;
    b->out_of_memory = 0;
}

void instrbuf_push(InstrBuf *b, const char *mnemonic, int num_operands,
                    const char *o1, const char *o2, const char *o3) {
    if (b->out_of_memory) {
        return;
    }
    if (b->count == b->capacity) {
        size_t new_cap = b->capacity ? b->capacity * 2 : 64;
        AvrInstr *grown = NULL;
        if (new_cap > b->capacity && new_cap <= SIZE_MAX / sizeof(AvrInstr)) {
            grown = realloc(b->items, new_cap * sizeof(AvrInstr));
        }
        if (!grown) {
            /* Sticky: later pushes are dropped and instrbuf_price fails, so
             * a lowering routine need not check every single push. */
            b->out_of_memory = 1;
            return;
        }
        b->items = grown;
        b->capacity = new_cap;
    }
    AvrInstr *ins = &b->items[b->count++];
    memset(ins, 0, sizeof(*ins));
    /* A truncated mnemonic or operand would still assemble, to the wrong
     * thing, so every copy is checked to fit. */
    OPTIFINE_INVARIANT(num_operands >= 0 && num_operands <= AVR_MAX_OPERANDS);
    OPTIFINE_INVARIANT(strlen(mnemonic) < AVR_MNEMONIC_LEN);
    memcpy(ins->mnemonic, mnemonic, strlen(mnemonic) + 1);
    ins->num_operands = num_operands;
    const char *operands[AVR_MAX_OPERANDS] = {o1, o2, o3};
    for (int i = 0; i < num_operands; i++) {
        OPTIFINE_INVARIANT(operands[i] != NULL && strlen(operands[i]) < AVR_OPERAND_LEN);
        memcpy(ins->operands[i], operands[i], strlen(operands[i]) + 1);
    }
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

int avr_instr_is_data_word(const AvrInstr *instr) {
    return strcmp(instr->mnemonic, AVR_DATA_WORD_MNEMONIC) == 0;
}

void ins_data_word(InstrBuf *b, uint16_t value) {
    char w[AVR_OPERAND_LEN];
    snprintf(w, AVR_OPERAND_LEN, "0x%04X", value);
    ins1(b, AVR_DATA_WORD_MNEMONIC, w);
}

/* snprintf truncates silently, and a truncated address operand assembles to a
 * wrong-but-plausible address rather than failing -- exactly how the first
 * version of this read twiddle k=0 correctly and every other entry from the
 * wrong offset. Truncation is a codegen bug, so it aborts here. */
static void fmt_sym(char *out, const char *half, const char *symbol, int offset) {
    int n = offset ? snprintf(out, AVR_OPERAND_LEN, "%s(%s+%d)", half, symbol, offset)
                   : snprintf(out, AVR_OPERAND_LEN, "%s(%s)", half, symbol);
    OPTIFINE_INVARIANT(n > 0 && n < AVR_OPERAND_LEN);
}
void fmt_lo8_sym(char *out, const char *symbol, int offset) { fmt_sym(out, "lo8", symbol, offset); }
void fmt_hi8_sym(char *out, const char *symbol, int offset) { fmt_sym(out, "hi8", symbol, offset); }

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

/* Loop labels are named after the loop's region index, so within one
 * Candidate a label maps back to a region in the pricing table. That makes
 * them unique per Candidate only; emit.h's EmitUnit renumbers them when
 * candidates are concatenated into one assembly unit. */
#define LOCAL_HEAD_FMT ".Ldsp%u"
#define LOCAL_EXIT_FMT ".Lex%u"

static int parse_local(const char *name, const char *prefix, unsigned *index) {
    size_t n = strlen(prefix);
    if (strncmp(name, prefix, n) != 0) return 0;
    const char *d = name + n;
    if (*d < '0' || *d > '9') return 0;
    unsigned v = 0;
    for (; *d; d++) {
        if (*d < '0' || *d > '9') return 0;
        v = v * 10 + (unsigned)(*d - '0');
    }
    *index = v;
    return 1;
}

int avr_local_label_index(const char *name, unsigned *index) {
    return parse_local(name, ".Ldsp", index) || parse_local(name, ".Lex", index);
}

int avr_local_label_rebase(const char *name, unsigned base, char *out) {
    unsigned idx;
    int n;
    if (parse_local(name, ".Ldsp", &idx)) {
        n = snprintf(out, AVR_OPERAND_LEN, LOCAL_HEAD_FMT, base + idx);
    } else if (parse_local(name, ".Lex", &idx)) {
        n = snprintf(out, AVR_OPERAND_LEN, LOCAL_EXIT_FMT, base + idx);
    } else {
        return 0;
    }
    OPTIFINE_INVARIANT(n > 0 && n < AVR_OPERAND_LEN);
    return 1;
}

static void loop_open(InstrBuf *b, LoopCtx *ctx, uint32_t trip) {
    /* trip must fit the 8-bit counter `dec` drives, and a zero trip would
     * wrap to 256 rather than skipping the body. Both are compile-time
     * properties of the lowering, never of user input, so they are internal
     * invariants -- checked in every build type, since a wrong trip count
     * would assemble into a loop that runs the wrong number of times. */
    OPTIFINE_INVARIANT(trip >= 1 && trip <= 256);
    OPTIFINE_INVARIANT(b->num_loops < INSTRBUF_MAX_LOOPS);
    ctx->trip = trip;
    ctx->region = b->num_loops++;
    snprintf(ctx->label, AVR_OPERAND_LEN, LOCAL_HEAD_FMT, (unsigned)ctx->region);
}

static void loop_place_label(InstrBuf *b, LoopCtx *ctx) {
    ins1(b, AVR_LABEL_MNEMONIC, ctx->label);
    ctx->body_first = b->count;
}

void instrbuf_loop_begin(InstrBuf *b, LoopCtx *ctx, uint32_t trip, int counter_reg) {
    loop_open(b, ctx, trip);
    ctx->counter_reg = counter_reg;
    ctx->counter_addr = 0;

    char r[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
    fmt_reg(r, counter_reg);
    /* 256 iterations are expressed as ldi 0: dec wraps 0 -> 255 and the loop
     * runs the full 256 times, the standard AVR counted-loop idiom. */
    fmt_imm(imm, (uint8_t)(trip & 0xFF));
    if (counter_reg >= 16) {
        ins2(b, "ldi", r, imm);
    } else {
        /* ldi can only target r16-r31. A DSP butterfly body has no free
         * register up there -- r16-r23 belong to the Q15 multiply, r24/r25 to
         * byte scratch, r26-r31 to the pointers -- so the counter lives lower
         * and the constant is staged through REG_SCRATCH0, which every such
         * body reloads before its own first use. Clobbering it here is
         * therefore free, and stated rather than assumed. */
        char t[AVR_OPERAND_LEN];
        fmt_reg(t, REG_SCRATCH0);
        ins2(b, "ldi", t, imm);
        ins2(b, "mov", r, t);
    }
    loop_place_label(b, ctx);
}

void instrbuf_loop_begin_sram(InstrBuf *b, LoopCtx *ctx, uint32_t trip, uint16_t counter_addr) {
    loop_open(b, ctx, trip);
    ctx->counter_reg = -1;
    ctx->counter_addr = counter_addr;

    char t[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(t, REG_SCRATCH0);
    fmt_imm(imm, (uint8_t)(trip & 0xFF));
    fmt_addr(a, counter_addr);
    ins2(b, "ldi", t, imm);
    ins2(b, "sts", a, t);
    loop_place_label(b, ctx);
}

size_t avr_instr_flash_bytes(const AvrInstr *ins) {
    if (avr_instr_is_label(ins)) return 0;
    if (avr_instr_is_data_word(ins)) return 2;
    /* lds/sts with a 16-bit address are the only 32-bit encodings this
     * project emits; everything else is a single 16-bit word. */
    if (!strcmp(ins->mnemonic, "lds") || !strcmp(ins->mnemonic, "sts")) return 4;
    return 2;
}

/* BRNE is a 7-bit PC-relative branch: it reaches -64..+63 WORDS from the
 * instruction after it. A butterfly body is several hundred bytes, so the
 * backward branch to the loop head does not fit and the assembler rejects it
 * with "relocation truncated to fit: R_AVR_7_PCREL". RJMP's 12-bit word
 * offset (+/-2K words) covers every body this project emits. */
#define AVR_BRNE_REACH_WORDS 64
#define AVR_RJMP_REACH_WORDS 2048

void instrbuf_loop_end(InstrBuf *b, LoopCtx *ctx) {
    char r[AVR_OPERAND_LEN];
    if (ctx->counter_reg >= 0) {
        fmt_reg(r, ctx->counter_reg);
        ins1(b, "dec", r);
    } else {
        /* sts leaves SREG alone, so Z from the dec still decides the branch. */
        char a[AVR_OPERAND_LEN];
        fmt_reg(r, REG_SCRATCH0);
        fmt_addr(a, ctx->counter_addr);
        ins2(b, "lds", r, a);
        ins1(b, "dec", r);
        ins2(b, "sts", a, r);
    }

    /* Distance from the branch back to the loop label, in words. */
    size_t bytes = 0;
    for (size_t i = ctx->body_first; i < b->count; i++) bytes += avr_instr_flash_bytes(&b->items[i]);
    size_t words = bytes / 2 + 1;

    int is_long = words > AVR_BRNE_REACH_WORDS;
    if (!is_long) {
        ins1(b, "brne", ctx->label);
    } else {
        /* Invert: skip past an unconditional jump back to the head. */
        OPTIFINE_INVARIANT(words <= AVR_RJMP_REACH_WORDS);
        char exit_label[AVR_OPERAND_LEN];
        snprintf(exit_label, AVR_OPERAND_LEN, LOCAL_EXIT_FMT, (unsigned)ctx->region);
        ins1(b, "breq", exit_label);
        ins1(b, "rjmp", ctx->label);
        ins1(b, AVR_LABEL_MNEMONIC, exit_label);
    }
    b->loops[ctx->region].first = ctx->body_first;
    b->loops[ctx->region].last = b->count - 1; /* includes dec + brne */
    b->loops[ctx->region].trip = ctx->trip;
    b->loops[ctx->region].is_long = is_long;
}

int instrbuf_price(InstrBuf *buf, const CostModel *cost_model, Candidate *out) {
    if (buf->out_of_memory) {
        fprintf(stderr, "instr_buf: out of memory building an instruction sequence\n");
        free(buf->items);
        buf->items = NULL;
        return -1;
    }
    double energy_nj = 0.0;
    for (size_t i = 0; i < buf->count; i++) {
        /* A label is an assembler directive, not an instruction: it occupies
         * no flash and consumes no cycles, so it is skipped here rather than
         * given a zero-energy cost_table.toml entry it would not deserve. */
        if (avr_instr_is_label(&buf->items[i]) || avr_instr_is_data_word(&buf->items[i])) {
            continue; /* directives: flash for .dw, nothing for .L; no cycles either way */
        }
        uint32_t direct = 1;
        for (size_t l = 0; l < buf->num_loops; l++) {
            if (i >= buf->loops[l].first && i <= buf->loops[l].last) direct *= buf->loops[l].trip;
        }
        int direct_cycles = avr_direct_cycles(buf->items[i].mnemonic);
        if (direct_cycles) {
            energy_nj += AVR_CYCLE_ENERGY_NJ * (double)direct_cycles * (double)direct;
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
    /* Conditional branches are priced at their TAKEN cost (2 cycles), but a
     * counted loop resolves the opposite way exactly once:
     *   short form  `dec / brne head`   -- brne falls through once (1 not 2)
     *   long form   `dec / breq exit / rjmp head` -- breq is NOT taken on
     *               every iteration but the last, so it costs 1 rather than
     *               2 for (trip-1) iterations, and the rjmp does not execute
     *               at all on the final iteration.
     * Both corrections are exact, not approximations. */
    for (size_t l = 0; l < buf->num_loops; l++) {
        uint32_t outer = 1;
        for (size_t o = 0; o < buf->num_loops; o++) {
            if (o != l && buf->loops[l].first >= buf->loops[o].first &&
                buf->loops[l].last <= buf->loops[o].last) {
                outer *= buf->loops[o].trip;
            }
        }
        /* The closing form is recorded when the loop is closed rather than
         * inferred by scanning the region for an rjmp. An outer region
         * contains its inner loops' rjmps, so a scan would be reading another
         * loop's closure; it only gave the right answer because an outer body
         * is always at least as long as the inner one it wraps. */
        uint32_t trip = buf->loops[l].trip;
        double give_back;
        if (buf->loops[l].is_long) {
            /* breq not taken for trip-1 iterations: -1 cycle each.
             * rjmp skipped on the final iteration: -2 cycles once. */
            give_back = (double)(trip - 1) * 1.0 + 2.0;
        } else {
            give_back = 1.0; /* brne falls through once */
        }
        energy_nj -= AVR_CYCLE_ENERGY_NJ * give_back * (double)outer;
    }
    out->instructions = buf->items;
    out->num_instructions = buf->count;
    out->cycles = (uint32_t)llround(energy_nj / AVR_CYCLE_ENERGY_NJ);
    out->energy_nj = energy_nj;
    return 0;
}
