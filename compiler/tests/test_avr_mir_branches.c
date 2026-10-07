/* Generic MIR control flow at any distance. Every program here is selected
 * to AVR and checked twice: statically, by laying the code out in flash
 * words independently of the selector and requiring every rjmp and breq to
 * reach its label; and dynamically, by running it in the AVR interpreter
 * against the MIR reference interpreter on every path. Covers short and long
 * forward, backward and conditional branches, the 150-add loop whose back
 * edge once assembled into an out-of-range rjmp, and a sweep where one jump
 * turning long pushes another one out of reach. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_interp.h"
#include "mir_interp.h"
#include "mir_programs.h"
#include "optifine/codegen/avr_mir.h"
#include "optifine/codegen/instr_buf.h"

static CostModel g_cm;

typedef struct {
    size_t rjmp, jmp, breq;
} Branches;

/* The entry-prologue selection of `f`, then `break`, as one Candidate. */
static Candidate select_linked(const MirModule *m, const AvrMirLayout *l, uint32_t f) {
    AvrMirCode code;
    assert(avr_mir_select_function(m, l, f, 1, &g_cm, &code) == 0);
    size_t total = 1;
    for (size_t i = 0; i < code.count; i++) total += code.segments[i].code.num_instructions;
    Candidate c = {0};
    c.instructions = calloc(total, sizeof(AvrInstr));
    assert(c.instructions);
    for (size_t i = 0; i < code.count; i++) {
        const Candidate *s = &code.segments[i].code;
        memcpy(c.instructions + c.num_instructions, s->instructions, s->num_instructions * sizeof(AvrInstr));
        c.num_instructions += s->num_instructions;
    }
    strcpy(c.instructions[c.num_instructions++].mnemonic, "break");
    avr_mir_code_free(&code);
    return c;
}

/* Independent reach check: word address of every label, then every branch
 * against its encoding's range (k = target - (pc + 1)). */
static Branches check_reach(const Candidate *c) {
    long *word = calloc(c->num_instructions + 1, sizeof(long));
    assert(word);
    long w = 0;
    for (size_t i = 0; i < c->num_instructions; i++) {
        word[i] = w;
        w += (long)(avr_instr_flash_bytes(&c->instructions[i]) / 2);
    }
    Branches n = {0, 0, 0};
    for (size_t i = 0; i < c->num_instructions; i++) {
        const AvrInstr *in = &c->instructions[i];
        int rjmp = !strcmp(in->mnemonic, "rjmp"), jmp = !strcmp(in->mnemonic, "jmp"), breq = !strcmp(in->mnemonic, "breq");
        if (!rjmp && !jmp && !breq) continue;
        long target = -1;
        for (size_t j = 0; j < c->num_instructions; j++) {
            if (avr_instr_is_label(&c->instructions[j]) && !strcmp(c->instructions[j].operands[0], in->operands[0])) {
                target = word[j];
            }
        }
        assert(target >= 0);
        long k = target - (word[i] + 1);
        if (rjmp) assert(k >= -2048 && k <= 2047);
        if (breq) assert(k >= -64 && k <= 63);
        if (jmp) assert(k < -2048 || k > 2047); /* long only when needed */
        n.rjmp += (size_t)rjmp;
        n.jmp += (size_t)jmp;
        n.breq += (size_t)breq;
    }
    free(word);
    return n;
}

/* Runs `f`(x) on AVR and in the MIR reference; they must agree. */
static void agree(const MirModule *m, const AvrMirLayout *l, uint32_t f, const Candidate *c, int64_t x) {
    const MirFunction *fn = &m->functions[f];
    MirInterp ref;
    int64_t args[2] = {x, 0}, want = 0;
    assert(mir_interp_init(&ref, m) == 0);
    assert(mir_interp_call(&ref, f, args, &want) == 0);
    mir_interp_free(&ref);
    AvrInterp *avr = malloc(sizeof(AvrInterp));
    assert(avr);
    avr_interp_init(avr);
    for (size_t p = 0; p < fn->num_params; p++) {
        for (size_t i = 0; i < mir_int_bytes(fn->value_types[p]); i++) {
            avr->mem[l->value_slot[f][p] + i] = (uint8_t)((uint64_t)args[p] >> (8 * i));
        }
    }
    assert(avr_interp_run(avr, c) == 0);
    uint64_t got = 0;
    for (size_t i = 0; i < mir_int_bytes(fn->return_type); i++) {
        got |= (uint64_t)avr->mem[l->return_slot[f] + i] << (8 * i);
    }
    if (got != (uint64_t)want) {
        fprintf(stderr, "%s(%lld): avr %llu, mir %lld\n", fn->name, (long long)x, (unsigned long long)got,
                (long long)want);
        abort();
    }
    free(avr);
}

/* Straight-line padding of exactly `words` flash words (words >= 8, or a
 * multiple of 3, or 5): `v = imm` is ldi+sts (3 words), `v = imm + imm` is
 * ldi+ldi+add+sts (5). */
static void pad(MirModule *m, uint32_t f, uint32_t b, uint32_t v, long words) {
    while (words > 0) {
        if (words % 3 == 0) {
            mir_emit_const(m, f, b, v, MIR_TYPE_I8, words & 0x7F, 0);
            words -= 3;
        } else {
            mir_emit_binary(m, f, b, MIR_ADD, v, MIR_TYPE_I8, mir_imm(1), mir_imm(2), 0);
            words -= 5;
        }
    }
    assert(words == 0);
}

/* i8 f(i8 x):
 *   b0: c = x == 0; cbr c, b<first>, b<second>
 *   b1: r = 1; pad a; br b3      -- b1 and b2 swap roles with `swap`
 *   b2: r = 2; pad b
 *   b3: ret r
 * Unswapped: b1's jump to b3 runs forward over b2. Swapped (cbr c, b2, b1):
 * the taken edge jumps forward over b1, which holds a jump of its own. */
static uint32_t build_far(MirModule *m, const char *name, long a, long b, int swap) {
    uint32_t f = mir_add_function(m, name, MIR_TYPE_I8);
    uint32_t x = mir_add_param(m, f, MIR_TYPE_I8);
    uint32_t c = mir_new_value(m, f, MIR_TYPE_I8), r = mir_new_value(m, f, MIR_TYPE_I8);
    uint32_t p = mir_new_value(m, f, MIR_TYPE_I8);
    uint32_t b0 = mir_add_block(m, f), b1 = mir_add_block(m, f), b2 = mir_add_block(m, f), b3 = mir_add_block(m, f);
    mir_emit_cmp(m, f, b0, MIR_CMP_EQ, c, MIR_TYPE_I8, mir_value(x), mir_imm(0), 0);
    if (swap) mir_cbr(m, f, b0, mir_value(c), b2, b1);
    else mir_cbr(m, f, b0, mir_value(c), b1, b2);
    mir_emit_const(m, f, b1, r, MIR_TYPE_I8, 1, 0);
    pad(m, f, b1, p, a);
    mir_br(m, f, b1, b3);
    mir_emit_const(m, f, b2, r, MIR_TYPE_I8, 2, 0);
    pad(m, f, b2, p, b);
    mir_br(m, f, b2, b3);
    mir_ret(m, f, b3, mir_value(r));
    return f;
}

/* i32 f(): s = 0; i = 0; do { s += 0; s += 1; ... s += 149; i++; } while (i != 3); return s;
 * -- the loop body is ~3,600 words, so its back edge needs jmp. */
static uint32_t build_long_loop(MirModule *m) {
    uint32_t f = mir_add_function(m, "long_loop", MIR_TYPE_I32);
    uint32_t i = mir_new_value(m, f, MIR_TYPE_I8), c = mir_new_value(m, f, MIR_TYPE_I8);
    uint32_t s = mir_new_value(m, f, MIR_TYPE_I32);
    uint32_t e = mir_add_block(m, f), h = mir_add_block(m, f), x = mir_add_block(m, f);
    mir_emit_const(m, f, e, i, MIR_TYPE_I8, 0, 0);
    mir_emit_const(m, f, e, s, MIR_TYPE_I32, 0, 0);
    mir_br(m, f, e, h);
    for (int k = 0; k < 150; k++) mir_emit_binary(m, f, h, MIR_ADD, s, MIR_TYPE_I32, mir_value(s), mir_imm(k), 0);
    mir_emit_binary(m, f, h, MIR_ADD, i, MIR_TYPE_I8, mir_value(i), mir_imm(1), 0);
    mir_emit_cmp(m, f, h, MIR_CMP_EQ, c, MIR_TYPE_I8, mir_value(i), mir_imm(3), 0);
    mir_cbr(m, f, h, mir_value(c), x, h);
    mir_ret(m, f, x, mir_value(s));
    return f;
}

static void layout_of(const MirModule *m, AvrMirLayout *l) {
    uint16_t end;
    assert(avr_mir_layout_init(m, l) == 0);
    assert(avr_mir_layout_place_rest(m, l, AVR_MIR_SRAM_BASE, AVR_MIR_SRAM_LIMIT, &end) == 0);
}

/* Builds, checks reach, runs both paths; returns the branch census. */
static Branches run_far(long a, long b, int swap) {
    MirModule m;
    mir_module_init(&m);
    uint32_t f = build_far(&m, "far", a, b, swap);
    AvrMirLayout l;
    layout_of(&m, &l);
    Candidate c = select_linked(&m, &l, f);
    Branches n = check_reach(&c);
    agree(&m, &l, f, &c, 0);
    agree(&m, &l, f, &c, 5);
    candidate_free(&c);
    avr_mir_layout_free(&l);
    mir_module_free(&m);
    return n;
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(cost_model_load(argv[1], &g_cm) == 0);

    /* Short forward (diamond) and backward (counted loop): rjmp only. */
    {
        MirModule m;
        mir_module_init(&m);
        uint32_t dia = mirp_diamond(&m), loop = mirp_loop(&m);
        AvrMirLayout l;
        layout_of(&m, &l);
        Candidate c = select_linked(&m, &l, dia);
        Branches n = check_reach(&c);
        assert(n.jmp == 0 && n.rjmp >= 2 && n.breq == 1);
        agree(&m, &l, dia, &c, 3);
        candidate_free(&c);
        c = select_linked(&m, &l, loop);
        n = check_reach(&c);
        assert(n.jmp == 0 && n.rjmp >= 2);
        agree(&m, &l, loop, &c, 0);
        candidate_free(&c);
        avr_mir_layout_free(&l);
        mir_module_free(&m);
        printf("  short forward and backward branches: rjmp, in reach, correct\n");
    }

    /* Long forward, unconditional: b1 jumps to b3 over a 2,200-word b2. */
    Branches n = run_far(8, 2200, 0);
    assert(n.jmp == 1);
    printf("  long forward jump: jmp over 2,200 words (%zu rjmp, %zu jmp), both paths correct\n", n.rjmp, n.jmp);

    /* Long conditional: the taken edge of b0's cbr jumps over a 2,200-word b1. */
    n = run_far(2200, 8, 1);
    assert(n.jmp == 1);
    printf("  long conditional branch: breq hops a jmp (%zu rjmp, %zu jmp), both paths correct\n", n.rjmp, n.jmp);

    /* Long backward: the loop whose rjmp once failed to link. */
    {
        MirModule m;
        mir_module_init(&m);
        uint32_t f = build_long_loop(&m);
        AvrMirLayout l;
        layout_of(&m, &l);
        Candidate c = select_linked(&m, &l, f);
        n = check_reach(&c);
        assert(n.jmp == 1);
        agree(&m, &l, f, &c, 0);
        candidate_free(&c);
        avr_mir_layout_free(&l);
        mir_module_free(&m);
        printf("  long backward branch: 150-add loop closes with jmp, returns 3 x 11175\n");
    }

    /* Interacting jumps: b0's taken edge spans b1, and b1 ends in a jump of
     * its own over b2. When b2 grows past rjmp reach, b1's jump becomes a
     * two-word jmp, which can push b0's edge out of reach in turn. Sweep b1
     * across the threshold with b2 short and with b2 long; somewhere the
     * second jump must be what tips the first. */
    int tipped = 0;
    for (long a = 2030; a <= 2050; a++) {
        Branches short_b2 = run_far(a, 8, 1);
        Branches long_b2 = run_far(a, 2200, 1);
        if (short_b2.jmp == 0 && long_b2.jmp == 2) tipped = 1;
    }
    assert(tipped);
    printf("  interacting long jumps: a jump turning long pushes its neighbour long; all in reach, correct\n");

    printf("test_avr_mir_branches: all tests passed\n");
    return 0;
}
