#include "optifine/codegen/avr_mir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/registers.h"

/* Scratch registers of the generic selector. Every MIR instruction is
 * self-contained: it loads what it needs into these, and nothing survives
 * into the next instruction. */
#define R_A REG_SCRATCH0  /* r24: first operand byte, and the result byte */
#define R_B REG_SCRATCH1  /* r25: second operand byte */
#define R_HI_A 22         /* SLT: sign-flipped top bytes; EQ/NE: difference accumulator */
#define R_HI_B 23
#define R_X 26            /* X: data pointers */
#define R_Z 30            /* Z: program-memory reads */

/* ---- the verification boundary ----
 *
 * Every public entry point below verifies the module it is handed before it
 * reads anything else from it. Verification is linear in the module and
 * repeated per call on purpose: a module can be changed between calls, and
 * nothing here records "already verified" state that such a change would
 * leave stale. */
static int require_verified(const MirModule *module, const char *who) {
    char message[256];
    if (mir_verify(module, message, sizeof(message)) != 0) {
        fprintf(stderr, "avr_mir: %s: refusing invalid MIR: %s\n", who, message);
        return -1;
    }
    return 0;
}

/* ---- layout ---- */

static size_t value_bytes(MirType t) {
    return t == MIR_TYPE_PTR ? AVR_MIR_PTR_BYTES : mir_int_bytes(t);
}

int avr_mir_layout_init(const MirModule *module, AvrMirLayout *layout) {
    memset(layout, 0, sizeof(*layout));
    if (require_verified(module, "layout") != 0) return -1;
    layout->num_objects = module->num_objects;
    layout->num_functions = module->num_functions;
    layout->object_addr = calloc(module->num_objects ? module->num_objects : 1, sizeof(uint16_t));
    layout->value_slot = calloc(module->num_functions ? module->num_functions : 1, sizeof(uint16_t *));
    layout->num_values = calloc(module->num_functions ? module->num_functions : 1, sizeof(size_t));
    layout->return_slot = calloc(module->num_functions ? module->num_functions : 1, sizeof(uint16_t));
    if (!layout->object_addr || !layout->value_slot || !layout->num_values || !layout->return_slot) {
        avr_mir_layout_free(layout);
        return -1;
    }
    for (size_t f = 0; f < module->num_functions; f++) {
        size_t n = module->functions[f].num_values;
        layout->num_values[f] = n;
        layout->value_slot[f] = calloc(n ? n : 1, sizeof(uint16_t));
        if (!layout->value_slot[f]) {
            avr_mir_layout_free(layout);
            return -1;
        }
    }
    return 0;
}

void avr_mir_layout_free(AvrMirLayout *layout) {
    for (size_t f = 0; layout->value_slot && f < layout->num_functions; f++) free(layout->value_slot[f]);
    free(layout->value_slot);
    free(layout->num_values);
    free(layout->object_addr);
    free(layout->return_slot);
    memset(layout, 0, sizeof(*layout));
}

static int value_folded(const MirModule *module, uint32_t f, uint32_t value);

/* The layout was made for this module as it is now: same objects, functions
 * and values. Anything added since has no address or slot. */
static int layout_matches(const MirModule *module, const AvrMirLayout *layout) {
    int ok = layout->num_objects == module->num_objects && layout->num_functions == module->num_functions;
    for (size_t f = 0; ok && f < module->num_functions; f++) ok = layout->num_values[f] == module->functions[f].num_values;
    if (!ok) fprintf(stderr, "avr_mir: the layout was made for a different module (or before it changed)\n");
    return ok;
}

static int place(uint32_t *cursor, size_t bytes, uint16_t limit, uint16_t *out) {
    if (bytes > limit || *cursor > (uint32_t)limit - bytes) return -1;
    *out = (uint16_t)*cursor;
    *cursor += (uint32_t)bytes;
    return 0;
}

int avr_mir_layout_place_rest(const MirModule *module, AvrMirLayout *layout, uint16_t base, uint16_t limit,
                              uint16_t *end) {
    if (require_verified(module, "layout") != 0 || !layout_matches(module, layout)) return -1;
    uint32_t cursor = base;
    for (size_t i = 0; i < module->num_objects; i++) {
        if (module->objects[i].kind == MIR_MEM_CONST || layout->object_addr[i] != 0) continue;
        if (place(&cursor, module->objects[i].size, limit, &layout->object_addr[i]) != 0) {
            fprintf(stderr, "avr_mir: object '%s' does not fit in SRAM\n", module->objects[i].name);
            return -1;
        }
    }
    for (size_t f = 0; f < module->num_functions; f++) {
        const MirFunction *fn = &module->functions[f];
        for (size_t v = 0; v < fn->num_values; v++) {
            if (value_folded(module, (uint32_t)f, (uint32_t)v)) continue;
            if (place(&cursor, value_bytes(fn->value_types[v]), limit, &layout->value_slot[f][v]) != 0) {
                fprintf(stderr, "avr_mir: values of function %s do not fit in SRAM\n", fn->name);
                return -1;
            }
        }
        if (fn->return_type != MIR_TYPE_VOID &&
            place(&cursor, value_bytes(fn->return_type), limit, &layout->return_slot[f]) != 0) {
            fprintf(stderr, "avr_mir: return slot of function %s does not fit in SRAM\n", fn->name);
            return -1;
        }
    }
    if (end) *end = (uint16_t)cursor;
    return 0;
}

/* ---- folding ---- */

static size_t count_uses(const MirFunction *fn, uint32_t value) {
    size_t uses = 0;
    for (size_t b = 0; b < fn->num_blocks; b++) {
        const MirBlock *block = &fn->blocks[b];
        for (size_t i = 0; i < block->count; i++) {
            uint32_t used[3];
            size_t n = mir_inst_uses(&block->insts[i], used);
            for (size_t k = 0; k < n; k++) uses += used[k] == value;
        }
        uses += mir_term_use(&block->term) == value;
    }
    return uses;
}

static int is_sram_object(const MirModule *m, MirAddress a) {
    return a.object != MIR_NONE && m->objects[a.object].kind != MIR_MEM_CONST;
}

/* The LOAD at blocks[b].insts[i], if its result is folded into the STORE at
 * i + 1. */
static int folds_into_next(const MirModule *m, const MirFunction *fn, const MirBlock *block, size_t i) {
    const MirInst *ld = &block->insts[i];
    if (ld->op != MIR_LOAD || !is_sram_object(m, ld->addr) || i + 1 >= block->count) return 0;
    const MirInst *st = &block->insts[i + 1];
    return st->op == MIR_STORE && st->type == ld->type && is_sram_object(m, st->addr) &&
           st->a.kind == MIR_OPND_VALUE && st->a.value == ld->dst && count_uses(fn, ld->dst) == 1;
}

/* True when `value` of function `f` is a LOAD result consumed only by the
 * STORE that immediately follows it, so selection moves it through a
 * register and it needs no slot. */
static int value_folded(const MirModule *module, uint32_t f, uint32_t value) {
    const MirFunction *fn = &module->functions[f];
    if (value < fn->num_params) return 0;
    size_t defs = 0;
    int folded = 0;
    for (size_t b = 0; b < fn->num_blocks; b++) {
        const MirBlock *block = &fn->blocks[b];
        for (size_t i = 0; i < block->count; i++) {
            if (block->insts[i].dst != value) continue;
            defs++;
            folded = folds_into_next(module, fn, block, i);
        }
    }
    return defs == 1 && folded;
}

/* ---- selection ---- */

typedef struct {
    const MirModule *m;
    const AvrMirLayout *layout;
    const CostModel *cost_model;
    uint32_t fn_id;
    const MirFunction *fn;
    AvrMirCode *out;
    InstrBuf buf;
    uint32_t origin;
    int open;
    unsigned hops;
    int failed;
    const uint8_t *long_jump; /* per block x JUMP_SLOTS: emit jmp, not rjmp */
} Sel;

static void reg(char *o, int r) { fmt_reg(o, r); }

static int code_push(Sel *s, Candidate *c, uint32_t origin) {
    AvrMirCode *out = s->out;
    if (out->count == out->capacity) {
        size_t cap = out->capacity ? out->capacity * 2 : 16;
        AvrMirSegment *grown = realloc(out->segments, cap * sizeof(AvrMirSegment));
        if (!grown) return -1;
        out->segments = grown;
        out->capacity = cap;
    }
    out->segments[out->count].code = *c;
    out->segments[out->count].origin = origin;
    out->count++;
    return 0;
}

/* Prices and appends the open segment. */
static void flush(Sel *s) {
    if (!s->open) return;
    s->open = 0;
    if (s->buf.count == 0 && !s->buf.out_of_memory) {
        free(s->buf.items);
        return;
    }
    Candidate c;
    if (instrbuf_price(&s->buf, s->cost_model, &c) != 0) {
        s->failed = 1;
        return;
    }
    if (code_push(s, &c, s->origin) != 0) {
        candidate_free(&c);
        s->failed = 1;
    }
}

/* Makes the open segment one of `origin`'s. */
static InstrBuf *seg(Sel *s, uint32_t origin) {
    if (s->open && s->origin != origin) flush(s);
    if (!s->open) {
        instrbuf_init(&s->buf);
        s->origin = origin;
        s->open = 1;
    }
    return &s->buf;
}

static int refuse(Sel *s, const char *why) {
    fprintf(stderr, "avr_mir: function %s: %s\n", s->fn->name, why);
    s->failed = 1;
    return -1;
}

static uint16_t slot(Sel *s, uint32_t value) {
    return s->layout->value_slot[s->fn_id][value];
}

/* Loads byte `i` of operand `o` (a value's slot or an immediate) into `r`. */
static void load_byte(InstrBuf *b, int r, Sel *s, MirOperand o, size_t i) {
    char rs[AVR_OPERAND_LEN], x[AVR_OPERAND_LEN];
    reg(rs, r);
    if (o.kind == MIR_OPND_IMM) {
        fmt_imm(x, (uint8_t)((uint64_t)o.imm >> (8 * i)));
        ins2(b, "ldi", rs, x);
    } else {
        fmt_addr(x, (uint16_t)(slot(s, o.value) + i));
        ins2(b, "lds", rs, x);
    }
}

static void store_reg(InstrBuf *b, uint16_t addr, int r) {
    char a[AVR_OPERAND_LEN], rs[AVR_OPERAND_LEN];
    fmt_addr(a, addr);
    reg(rs, r);
    ins2(b, "sts", a, rs);
}

static void op2(InstrBuf *b, const char *m, int rd, int rr) {
    char d[AVR_OPERAND_LEN], r[AVR_OPERAND_LEN];
    reg(d, rd);
    reg(r, rr);
    ins2(b, m, d, r);
}

/* X = pointer value `p` + `offset`. */
static void load_x(InstrBuf *b, Sel *s, uint32_t p, int32_t offset) {
    char r[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN], k[AVR_OPERAND_LEN];
    for (int i = 0; i < 2; i++) {
        reg(r, R_X + i);
        fmt_addr(a, (uint16_t)(slot(s, p) + i));
        ins2(b, "lds", r, a);
    }
    if (offset == 0) return;
    reg(r, R_X);
    if (offset > 0 && offset <= 63) {
        fmt_imm(k, (uint8_t)offset);
        ins2(b, "adiw", r, k);
    } else if (offset < 0 && offset >= -63) {
        fmt_imm(k, (uint8_t)-offset);
        ins2(b, "sbiw", r, k);
    } else {
        uint16_t neg = (uint16_t)-offset;
        fmt_imm(k, (uint8_t)neg);
        ins2(b, "subi", r, k);
        reg(r, R_X + 1);
        fmt_imm(k, (uint8_t)(neg >> 8));
        ins2(b, "sbci", r, k);
    }
}

static void block_label(char *out, uint32_t fn, uint32_t block) {
    snprintf(out, AVR_OPERAND_LEN, ".Lf%ub%u", (unsigned)fn, (unsigned)block);
}

static void exit_label(char *out, uint32_t fn) {
    snprintf(out, AVR_OPERAND_LEN, ".Lf%ux", (unsigned)fn);
}

/* ---- branch reach ----
 *
 * A block's terminator jumps at most twice: to a block (slot 0: br's target
 * or cbr's taken edge; slot 1: cbr's other edge) or to the function's exit
 * (slot 2: ret from any block but the last). Each jump is an rjmp, which
 * reaches -2048..+2047 words from the instruction after it, unless its slot
 * is marked long, when it is a jmp (two words, 3 cycles, all of flash).
 * Conditional branches never need reach: cbr's breq only hops the one jump
 * after it. Selection starts with every slot short, lays the function out in
 * flash words, marks the slots whose rjmp falls short, and selects again
 * until nothing changes. Marking only ever lengthens code, so each round
 * marks at least one new slot or ends, and a jump marked long stays long. */
#define JUMP_SLOTS 3
#define RJMP_MIN_WORDS (-2048L)
#define RJMP_MAX_WORDS 2047L

/* Block id of `label` if it is one of function `fn`'s block labels. */
static int parse_block_label(const char *label, uint32_t fn, uint32_t *block) {
    char prefix[AVR_OPERAND_LEN];
    int n = snprintf(prefix, sizeof(prefix), ".Lf%ub", (unsigned)fn);
    if (strncmp(label, prefix, (size_t)n) != 0) return 0;
    const char *digits = label + n;
    if (*digits < '0' || *digits > '9') return 0;
    char *end;
    unsigned long v = strtoul(digits, &end, 10);
    if (*end != '\0' || v > UINT32_MAX) return 0;
    *block = (uint32_t)v;
    return 1;
}

/* Marks the slots of `code` (function `fn`, `num_blocks` blocks) whose rjmp
 * does not reach its label. Returns how many it newly marked, or (size_t)-1
 * when out of memory. */
static size_t mark_long_jumps(const AvrMirCode *code, uint32_t fn, size_t num_blocks, uint8_t *long_jump) {
    long *block_word = malloc(num_blocks * sizeof(long));
    if (!block_word) return (size_t)-1;
    for (size_t b = 0; b < num_blocks; b++) block_word[b] = -1;
    long exit_word = -1, words = 0;
    char exit_name[AVR_OPERAND_LEN];
    exit_label(exit_name, fn);
    for (size_t g = 0; g < code->count; g++) {
        const Candidate *c = &code->segments[g].code;
        for (size_t i = 0; i < c->num_instructions; i++) {
            const AvrInstr *in = &c->instructions[i];
            uint32_t b;
            if (avr_instr_is_label(in)) {
                if (parse_block_label(in->operands[0], fn, &b) && b < num_blocks) block_word[b] = words;
                if (strcmp(in->operands[0], exit_name) == 0) exit_word = words;
            }
            words += (long)(avr_instr_flash_bytes(in) / 2);
        }
    }

    size_t marked = 0;
    uint32_t block = 0;
    unsigned ordinal = 0;
    words = 0;
    for (size_t g = 0; g < code->count; g++) {
        const Candidate *c = &code->segments[g].code;
        for (size_t i = 0; i < c->num_instructions; i++) {
            const AvrInstr *in = &c->instructions[i];
            uint32_t b;
            if (avr_instr_is_label(in) && parse_block_label(in->operands[0], fn, &b)) {
                block = b;
                ordinal = 0;
            }
            int is_rjmp = strcmp(in->mnemonic, "rjmp") == 0;
            if ((is_rjmp || strcmp(in->mnemonic, "jmp") == 0) && in->num_operands == 1) {
                long target = -1;
                size_t slot = JUMP_SLOTS;
                if (parse_block_label(in->operands[0], fn, &b) && b < num_blocks) {
                    target = block_word[b];
                    slot = ordinal++;
                } else if (strcmp(in->operands[0], exit_name) == 0) {
                    target = exit_word;
                    slot = 2;
                }
                if (slot < JUMP_SLOTS && target >= 0 && is_rjmp) {
                    long k = target - (words + 1);
                    if ((k < RJMP_MIN_WORDS || k > RJMP_MAX_WORDS) && !long_jump[block * JUMP_SLOTS + slot]) {
                        long_jump[block * JUMP_SLOTS + slot] = 1;
                        marked++;
                    }
                }
            }
            words += (long)(avr_instr_flash_bytes(in) / 2);
        }
    }
    free(block_word);
    return marked;
}

/* The jump in `slot` of block `b`: rjmp, or jmp once marked long. */
static void jump(Sel *s, InstrBuf *b, uint32_t b_id, size_t slot, const char *label) {
    ins1(b, s->long_jump[(size_t)b_id * JUMP_SLOTS + slot] ? "jmp" : "rjmp", label);
}

static int select_inst(Sel *s, const MirInst *in, const MirBlock *block, size_t index, size_t *skip) {
    const MirModule *m = s->m;
    if (in->op == MIR_TARGET) {
        if (strcmp(in->target->target, "avr") != 0) return refuse(s, "target region for another target");
        flush(s);
        const Candidate *src = in->target->payload;
        Candidate copy = *src;
        copy.instructions = malloc((src->num_instructions ? src->num_instructions : 1) * sizeof(AvrInstr));
        if (!copy.instructions) return refuse(s, "out of memory");
        memcpy(copy.instructions, src->instructions, src->num_instructions * sizeof(AvrInstr));
        if (code_push(s, &copy, in->origin) != 0) {
            candidate_free(&copy);
            return refuse(s, "out of memory");
        }
        return 0;
    }

    InstrBuf *b = seg(s, in->origin);
    size_t n = value_bytes(in->type);
    char r1[AVR_OPERAND_LEN], r2[AVR_OPERAND_LEN], x[AVR_OPERAND_LEN];
    uint16_t dst = in->dst != MIR_NONE ? slot(s, in->dst) : 0;

    switch (in->op) {
        case MIR_CONST:
        case MIR_COPY:
            for (size_t i = 0; i < n; i++) {
                load_byte(b, R_A, s, in->a, i);
                store_reg(b, (uint16_t)(dst + i), R_A);
            }
            return 0;
        case MIR_ADD:
        case MIR_SUB:
        case MIR_AND:
        case MIR_OR:
        case MIR_XOR:
            for (size_t i = 0; i < n; i++) {
                const char *mn = in->op == MIR_ADD   ? (i ? "adc" : "add")
                                 : in->op == MIR_SUB ? (i ? "sbc" : "sub")
                                 : in->op == MIR_AND ? "and"
                                 : in->op == MIR_OR  ? "or"
                                                     : "eor";
                load_byte(b, R_A, s, in->a, i);
                load_byte(b, R_B, s, in->b, i);
                op2(b, mn, R_A, R_B);
                store_reg(b, (uint16_t)(dst + i), R_A);
            }
            return 0;
        case MIR_MUL:
            if (n == 1) {
                load_byte(b, R_A, s, in->a, 0);
                load_byte(b, R_B, s, in->b, 0);
                op2(b, "mul", R_A, R_B);
                store_reg(b, dst, 0);
                return 0;
            }
            if (n == 2) {
                /* low 16 bits: a0*b0 + ((a0*b1 + a1*b0) << 8) */
                load_byte(b, R_A, s, in->a, 0);
                load_byte(b, R_B, s, in->b, 0);
                op2(b, "mul", R_A, R_B);
                op2(b, "movw", R_HI_A, 0);
                load_byte(b, R_B, s, in->b, 1);
                op2(b, "mul", R_A, R_B);
                op2(b, "add", R_HI_B, 0);
                load_byte(b, R_A, s, in->a, 1);
                load_byte(b, R_B, s, in->b, 0);
                op2(b, "mul", R_A, R_B);
                op2(b, "add", R_HI_B, 0);
                store_reg(b, dst, R_HI_A);
                store_reg(b, (uint16_t)(dst + 1), R_HI_B);
                return 0;
            }
            return refuse(s, "MIR_MUL wider than 16 bits is not supported by the AVR backend yet");
        case MIR_CMP: {
            size_t w = mir_int_bytes(in->type);
            if (in->pred == MIR_CMP_EQ || in->pred == MIR_CMP_NE) {
                /* OR of the bytewise differences: zero iff equal. */
                reg(r1, R_HI_B);
                ins1(b, "clr", r1);
                for (size_t i = 0; i < w; i++) {
                    load_byte(b, R_A, s, in->a, i);
                    load_byte(b, R_B, s, in->b, i);
                    op2(b, "eor", R_A, R_B);
                    op2(b, "or", R_HI_B, R_A);
                }
                op2(b, "cp", REG_ZERO, R_HI_B); /* C = (difference != 0) */
            } else {
                size_t top = w - 1;
                if (in->pred == MIR_CMP_SLT) {
                    /* Signed order is unsigned order with the sign bits
                     * flipped. Flip the top bytes before the borrow chain
                     * starts, so subi cannot disturb its carry. */
                    load_byte(b, R_HI_A, s, in->a, top);
                    load_byte(b, R_HI_B, s, in->b, top);
                    fmt_imm(x, 0x80);
                    reg(r1, R_HI_A);
                    ins2(b, "subi", r1, x);
                    reg(r1, R_HI_B);
                    ins2(b, "subi", r1, x);
                }
                for (size_t i = 0; i < w; i++) {
                    int ra = R_A, rb = R_B;
                    if (in->pred == MIR_CMP_SLT && i == top) {
                        ra = R_HI_A;
                        rb = R_HI_B;
                    } else {
                        load_byte(b, R_A, s, in->a, i);
                        load_byte(b, R_B, s, in->b, i);
                    }
                    op2(b, i ? "cpc" : "cp", ra, rb); /* C = a < b after the last byte */
                }
            }
            /* r24 = C, without disturbing it first: clr leaves C alone. */
            reg(r1, R_A);
            ins1(b, "clr", r1);
            op2(b, "adc", R_A, REG_ZERO);
            if (in->pred == MIR_CMP_EQ) {
                reg(r1, R_B);
                fmt_imm(x, 1);
                ins2(b, "ldi", r1, x);
                op2(b, "eor", R_A, R_B);
            }
            store_reg(b, dst, R_A);
            return 0;
        }
        case MIR_ZEXT:
        case MIR_SEXT:
        case MIR_TRUNC: {
            size_t from = value_bytes(s->fn->value_types[in->a.value]);
            size_t copy = from < n ? from : n;
            for (size_t i = 0; i < copy; i++) {
                load_byte(b, R_A, s, in->a, i);
                store_reg(b, (uint16_t)(dst + i), R_A);
            }
            if (in->op == MIR_TRUNC) return 0;
            int fill = REG_ZERO;
            if (in->op == MIR_SEXT) {
                load_byte(b, R_A, s, in->a, from - 1);
                reg(r1, R_A);
                ins1(b, "lsl", r1);    /* C = sign bit */
                op2(b, "sbc", R_A, R_A); /* 0xFF or 0x00 */
                fill = R_A;
            }
            for (size_t i = from; i < n; i++) store_reg(b, (uint16_t)(dst + i), fill);
            return 0;
        }
        case MIR_LOAD: {
            int folded = folds_into_next(m, s->fn, block, index) && value_folded(m, s->fn_id, in->dst);
            const MirInst *st = folded ? &block->insts[index + 1] : NULL;
            if (in->addr.pointer != MIR_NONE) {
                load_x(b, s, in->addr.pointer, in->addr.offset);
                fmt_ptr(x, 'X', 1);
            } else if (m->objects[in->addr.object].kind == MIR_MEM_CONST) {
                const char *label = m->objects[in->addr.object].name;
                reg(r1, R_Z);
                fmt_lo8_sym(x, label, in->addr.offset);
                ins2(b, "ldi", r1, x);
                reg(r1, R_Z + 1);
                fmt_hi8_sym(x, label, in->addr.offset);
                ins2(b, "ldi", r1, x);
                fmt_ptr(x, 'Z', 1);
            }
            for (size_t i = 0; i < n; i++) {
                reg(r1, R_A);
                if (in->addr.pointer != MIR_NONE) {
                    ins2(b, "ld", r1, x);
                } else if (m->objects[in->addr.object].kind == MIR_MEM_CONST) {
                    ins2(b, "lpm", r1, x);
                } else {
                    fmt_addr(r2, (uint16_t)(s->layout->object_addr[in->addr.object] + in->addr.offset + i));
                    ins2(b, "lds", r1, r2);
                }
                if (st) {
                    store_reg(b, (uint16_t)(s->layout->object_addr[st->addr.object] + st->addr.offset + i), R_A);
                } else {
                    store_reg(b, (uint16_t)(dst + i), R_A);
                }
            }
            if (st) *skip = 1;
            return 0;
        }
        case MIR_STORE:
            if (in->addr.pointer != MIR_NONE) {
                load_x(b, s, in->addr.pointer, in->addr.offset);
                fmt_ptr(x, 'X', 1);
                for (size_t i = 0; i < n; i++) {
                    load_byte(b, R_A, s, in->a, i);
                    reg(r1, R_A);
                    ins2(b, "st", x, r1);
                }
                return 0;
            }
            for (size_t i = 0; i < n; i++) {
                load_byte(b, R_A, s, in->a, i);
                store_reg(b, (uint16_t)(s->layout->object_addr[in->addr.object] + in->addr.offset + i), R_A);
            }
            return 0;
        case MIR_ADDR: {
            if (m->objects[in->addr.object].kind == MIR_MEM_CONST) {
                return refuse(s, "the address of a program-memory constant needs an address-space-qualified "
                                 "pointer, which MIR does not have yet");
            }
            uint16_t addr = (uint16_t)(s->layout->object_addr[in->addr.object] + in->addr.offset);
            reg(r1, R_A);
            fmt_lo8(x, addr);
            ins2(b, "ldi", r1, x);
            store_reg(b, dst, R_A);
            fmt_hi8(x, addr);
            ins2(b, "ldi", r1, x);
            store_reg(b, (uint16_t)(dst + 1), R_A);
            return 0;
        }
        case MIR_PTR_ADD:
            for (size_t i = 0; i < 2; i++) {
                load_byte(b, R_A, s, in->a, i);
                load_byte(b, R_B, s, in->b, i);
                op2(b, i ? "adc" : "add", R_A, R_B);
                store_reg(b, (uint16_t)(dst + i), R_A);
            }
            return 0;
        case MIR_TARGET:
            break;
    }
    return refuse(s, "unknown MIR opcode");
}

static int select_terminator(Sel *s, uint32_t b_id, const MirTerminator *t, int *needs_exit) {
    uint32_t origin = s->open ? s->origin : MIR_NONE;
    InstrBuf *b = seg(s, origin);
    char l[AVR_OPERAND_LEN], r[AVR_OPERAND_LEN];
    uint32_t next = b_id + 1;
    switch (t->kind) {
        case MIR_TERM_BR:
            if (t->then_block != next) {
                block_label(l, s->fn_id, t->then_block);
                jump(s, b, b_id, 0, l);
            }
            return 0;
        case MIR_TERM_CBR: {
            /* breq hops over the taken edge's jump (one or two words), so no
             * conditional branch ever needs more reach than that. */
            char hop[AVR_OPERAND_LEN];
            snprintf(hop, AVR_OPERAND_LEN, ".Lf%uh%u", (unsigned)s->fn_id, s->hops++);
            load_byte(b, R_A, s, t->cond, 0);
            op2(b, "cp", R_A, REG_ZERO);
            ins1(b, "breq", hop);
            block_label(l, s->fn_id, t->then_block);
            jump(s, b, b_id, 0, l);
            ins1(b, AVR_LABEL_MNEMONIC, hop);
            if (t->else_block != next) {
                block_label(l, s->fn_id, t->else_block);
                jump(s, b, b_id, 1, l);
            }
            return 0;
        }
        case MIR_TERM_RET:
            if (t->value.kind != MIR_OPND_NONE) {
                uint16_t ret = s->layout->return_slot[s->fn_id];
                for (size_t i = 0; i < value_bytes(s->fn->return_type); i++) {
                    load_byte(b, R_A, s, t->value, i);
                    store_reg(b, (uint16_t)(ret + i), R_A);
                }
            }
            if (b_id + 1 != s->fn->num_blocks) {
                exit_label(r, s->fn_id);
                jump(s, b, b_id, 2, r);
                *needs_exit = 1;
            }
            return 0;
        case MIR_TERM_NONE:
            break;
    }
    return refuse(s, "block without a terminator");
}

/* Every SRAM object, every value of `fn` that needs a slot, and its return
 * value have an address: an unplaced one would read and write address 0,
 * which on AVR is register r0. */
static int layout_complete(const MirModule *module, const AvrMirLayout *layout, uint32_t fn) {
    for (size_t i = 0; i < module->num_objects; i++) {
        if (module->objects[i].kind != MIR_MEM_CONST && layout->object_addr[i] == 0) {
            fprintf(stderr, "avr_mir: selection: object '%s' has no address\n", module->objects[i].name);
            return 0;
        }
    }
    const MirFunction *f = &module->functions[fn];
    for (size_t v = 0; v < f->num_values; v++) {
        if (layout->value_slot[fn][v] == 0 && !value_folded(module, fn, (uint32_t)v)) {
            fprintf(stderr, "avr_mir: selection: value %zu of function %s has no slot\n", v, f->name);
            return 0;
        }
    }
    if (f->return_type != MIR_TYPE_VOID && layout->return_slot[fn] == 0) {
        fprintf(stderr, "avr_mir: selection: function %s has no return slot\n", f->name);
        return 0;
    }
    return 1;
}

void avr_mir_code_free(AvrMirCode *code) {
    for (size_t i = 0; i < code->count; i++) candidate_free(&code->segments[i].code);
    free(code->segments);
    memset(code, 0, sizeof(*code));
}

/* One selection of function `fn_id` with the given long-jump slots. */
static int select_once(const MirModule *module, const AvrMirLayout *layout, uint32_t fn_id, int entry_prologue,
                       const CostModel *cost_model, const uint8_t *long_jump, AvrMirCode *out) {
    memset(out, 0, sizeof(*out));
    Sel s;
    memset(&s, 0, sizeof(s));
    s.m = module;
    s.layout = layout;
    s.cost_model = cost_model;
    s.fn_id = fn_id;
    s.fn = &module->functions[fn_id];
    s.out = out;
    s.long_jump = long_jump;

    if (entry_prologue) {
        char r[AVR_OPERAND_LEN];
        reg(r, REG_ZERO);
        ins1(seg(&s, MIR_NONE), "clr", r);
        flush(&s);
    }
    int needs_exit = 0;
    for (uint32_t b = 0; b < s.fn->num_blocks && !s.failed; b++) {
        const MirBlock *block = &s.fn->blocks[b];
        int labelled = s.fn->num_blocks > 1;
        size_t first = 0;
        if (labelled) {
            char l[AVR_OPERAND_LEN];
            block_label(l, fn_id, b);
            uint32_t origin = block->count ? block->insts[0].origin : (s.open ? s.origin : MIR_NONE);
            ins1(seg(&s, block->count && block->insts[0].op == MIR_TARGET ? MIR_NONE : origin),
                 AVR_LABEL_MNEMONIC, l);
        }
        for (size_t i = first; i < block->count && !s.failed; i++) {
            size_t skip = 0;
            select_inst(&s, &block->insts[i], block, i, &skip);
            i += skip;
        }
        if (!s.failed) select_terminator(&s, b, &block->term, &needs_exit);
    }
    if (!s.failed && needs_exit) {
        char l[AVR_OPERAND_LEN];
        exit_label(l, fn_id);
        ins1(seg(&s, s.open ? s.origin : MIR_NONE), AVR_LABEL_MNEMONIC, l);
    }
    if (!s.failed) flush(&s);
    if (s.open) free(s.buf.items);
    if (s.failed) {
        avr_mir_code_free(out);
        return -1;
    }
    out->straight_line = s.fn->num_blocks == 1;
    return 0;
}

int avr_mir_select_function(const MirModule *module, const AvrMirLayout *layout, uint32_t fn_id,
                            int entry_prologue, const CostModel *cost_model, AvrMirCode *out) {
    memset(out, 0, sizeof(*out));
    if (require_verified(module, "selection") != 0 || !layout_matches(module, layout)) return -1;
    if (fn_id >= module->num_functions) {
        fprintf(stderr, "avr_mir: selection: function %u does not exist\n", (unsigned)fn_id);
        return -1;
    }
    if (!layout_complete(module, layout, fn_id)) return -1;
    size_t num_blocks = module->functions[fn_id].num_blocks;
    uint8_t *long_jump = calloc(num_blocks * JUMP_SLOTS, 1);
    if (!long_jump) {
        fprintf(stderr, "avr_mir: selection: out of memory\n");
        return -1;
    }
    int rc;
    for (;;) {
        rc = select_once(module, layout, fn_id, entry_prologue, cost_model, long_jump, out);
        if (rc != 0) break;
        size_t marked = mark_long_jumps(out, fn_id, num_blocks, long_jump);
        if (marked == 0) break;
        avr_mir_code_free(out);
        if (marked == (size_t)-1) {
            fprintf(stderr, "avr_mir: selection: out of memory\n");
            rc = -1;
            break;
        }
    }
    free(long_jump);
    return rc;
}

int avr_mir_select_constants(const MirModule *module, const CostModel *cost_model, Candidate *out) {
    memset(out, 0, sizeof(*out));
    if (require_verified(module, "constants") != 0) return -1;
    InstrBuf buf;
    instrbuf_init(&buf);
    for (size_t i = 0; i < module->num_objects; i++) {
        const MirMemObject *obj = &module->objects[i];
        if (obj->kind != MIR_MEM_CONST) continue;
        ins1(&buf, AVR_LABEL_MNEMONIC, obj->name);
        for (size_t k = 0; k < obj->size; k += 2) {
            uint16_t word = obj->init[k];
            if (k + 1 < obj->size) word = (uint16_t)(word | (obj->init[k + 1] << 8));
            ins_data_word(&buf, word);
        }
    }
    return instrbuf_price(&buf, cost_model, out);
}
