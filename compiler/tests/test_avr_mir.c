/* The AVR backend for generic MIR: every shared MIR program (mir_programs.c)
 * is selected to AVR, run in the AVR interpreter, and must agree with the
 * reference MIR interpreter -- return values and memory -- over a spread of
 * arguments. Also checks the backend's own contracts: labels unique per
 * function, segments split by origin, refusals for what it does not support,
 * and that a folded load needs no storage. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_interp.h"
#include "mir_interp.h"
#include "mir_programs.h"
#include "optifine/codegen/avr_mir.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/sram_layout.h"

static CostModel g_cm;

/* One Candidate: the function's segments, `break`, then constant data. */
static Candidate link(const MirModule *m, const AvrMirLayout *layout, uint32_t f, size_t *segments) {
    AvrMirCode code;
    assert(avr_mir_select_function(m, layout, f, 1, &g_cm, &code) == 0);
    Candidate data;
    assert(avr_mir_select_constants(m, &g_cm, &data) == 0);
    size_t total = 1 + data.num_instructions;
    for (size_t i = 0; i < code.count; i++) total += code.segments[i].code.num_instructions;
    Candidate c = {0};
    c.instructions = malloc(total * sizeof(AvrInstr));
    assert(c.instructions);
    for (size_t i = 0; i < code.count; i++) {
        const Candidate *s = &code.segments[i].code;
        memcpy(c.instructions + c.num_instructions, s->instructions, s->num_instructions * sizeof(AvrInstr));
        c.num_instructions += s->num_instructions;
    }
    AvrInstr brk = {0};
    strcpy(brk.mnemonic, "break");
    c.instructions[c.num_instructions++] = brk;
    memcpy(c.instructions + c.num_instructions, data.instructions, data.num_instructions * sizeof(AvrInstr));
    c.num_instructions += data.num_instructions;
    if (segments) *segments = code.count;
    candidate_free(&data);
    avr_mir_code_free(&code);
    return c;
}

static uint64_t read_le(const uint8_t *p, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; i++) v |= (uint64_t)p[i] << (8 * i);
    return v;
}

/* Runs f(a, b) both ways; compares the result and every SRAM object. */
static void agree(const MirModule *m, const AvrMirLayout *layout, uint32_t f, int64_t a, int64_t b) {
    const MirFunction *fn = &m->functions[f];
    int64_t args[2] = {a, b}, want = 0;
    MirInterp ref;
    assert(mir_interp_init(&ref, m) == 0);
    assert(mir_interp_call(&ref, f, args, &want) == 0);

    Candidate c = link(m, layout, f, NULL);
    AvrInterp *avr = malloc(sizeof(AvrInterp));
    assert(avr);
    avr_interp_init(avr);
    for (size_t p = 0; p < fn->num_params; p++) {
        uint16_t slot = layout->value_slot[f][p];
        size_t n = mir_int_bytes(fn->value_types[p]);
        for (size_t i = 0; i < n; i++) avr->mem[slot + i] = (uint8_t)((uint64_t)args[p] >> (8 * i));
    }
    assert(avr_interp_run(avr, &c) == 0);
    if (fn->return_type != MIR_TYPE_VOID) {
        size_t n = mir_int_bytes(fn->return_type);
        uint64_t got = read_le(&avr->mem[layout->return_slot[f]], n);
        if (got != (uint64_t)want) {
            fprintf(stderr, "%s(%lld, %lld): avr %llu, mir %lld\n", fn->name, (long long)a, (long long)b,
                    (unsigned long long)got, (long long)want);
            abort();
        }
    }
    for (size_t o = 0; o < m->num_objects; o++) {
        if (m->objects[o].kind == MIR_MEM_CONST) continue;
        if (m->objects[o].kind == MIR_MEM_STACK && m->objects[o].function != f) continue;
        assert(memcmp(&avr->mem[layout->object_addr[o]], ref.memory[o], m->objects[o].size) == 0);
    }
    free(avr);
    candidate_free(&c);
    mir_interp_free(&ref);
}

/* Straight-line code: the interpreter's executed cycles equal the summed
 * segment predictions plus the 1-cycle break. */
static void cycles_agree(const MirModule *m, const AvrMirLayout *layout, uint32_t f) {
    AvrMirCode code;
    assert(avr_mir_select_function(m, layout, f, 1, &g_cm, &code) == 0);
    unsigned long predicted = 1;
    for (size_t i = 0; i < code.count; i++) predicted += code.segments[i].code.cycles;
    avr_mir_code_free(&code);
    Candidate c = link(m, layout, f, NULL);
    AvrInterp *avr = malloc(sizeof(AvrInterp));
    assert(avr);
    avr_interp_init(avr);
    assert(avr_interp_run(avr, &c) == 0);
    assert(avr->cycles == predicted);
    printf("  %s: %lu cycles predicted == executed\n", m->functions[f].name, predicted);
    free(avr);
    candidate_free(&c);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(cost_model_load(argv[1], &g_cm) == 0);

    MirModule m;
    mir_module_init(&m);
    uint32_t arith = mirp_arith32(&m), mul = mirp_mul8(&m), ls = mirp_load_store(&m), dia = mirp_diamond(&m),
             cmp = mirp_compares(&m), loop = mirp_loop(&m);
    /* i16 f(i16 a, i16 b) { return a * b; } -- the 16-bit multiply */
    uint32_t mul16 = mir_add_function(&m, "mul16", MIR_TYPE_I16);
    {
        uint32_t a = mir_add_param(&m, mul16, MIR_TYPE_I16), b = mir_add_param(&m, mul16, MIR_TYPE_I16);
        uint32_t p = mir_new_value(&m, mul16, MIR_TYPE_I16), e = mir_add_block(&m, mul16);
        mir_emit_binary(&m, mul16, e, MIR_MUL, p, MIR_TYPE_I16, mir_value(a), mir_value(b), 0);
        mir_ret(&m, mul16, e, mir_value(p));
    }
    char msg[256];
    assert(mir_verify(&m, msg, sizeof(msg)) == 0);
    AvrMirLayout layout;
    assert(avr_mir_layout_init(&m, &layout) == 0);
    uint16_t end = 0;
    assert(avr_mir_layout_place_rest(&m, &layout, SRAM_LAYOUT_BASE, SRAM_LAYOUT_LIMIT, &end) == 0);
    printf("  layout: objects and value slots in 0x%04X..0x%04X\n", SRAM_LAYOUT_BASE, end);

    static const int64_t a32[] = {0, 1, 0x12345678, 0x7FFFFFFF, -1, -0x1000};
    for (size_t i = 0; i < 6; i++)
        for (size_t j = 0; j < 6; j++) agree(&m, &layout, arith, a32[i], a32[(i + j) % 6] ^ 0x0F0F00FF);
    for (int x = 0; x < 256; x += 17)
        for (int y = 0; y < 256; y += 13) {
            agree(&m, &layout, mul, x, y);
            agree(&m, &layout, cmp, x, y);
            agree(&m, &layout, cmp, x, x);
        }
    static const int64_t a16[] = {0, 1, -1, 300, -300, 32767, -32768, 1000};
    for (size_t i = 0; i < 8; i++)
        for (size_t j = 0; j < 8; j++) agree(&m, &layout, dia, a16[i], a16[j]);
    for (size_t i = 0; i < 8; i++)
        for (size_t j = 0; j < 8; j++) agree(&m, &layout, mul16, a16[i], a16[j] + 7);
    agree(&m, &layout, ls, 0, 0);
    agree(&m, &layout, loop, 0, 0);
    printf("  arith32, mul8, mul16, compares, diamond, load/store, counted loop: AVR == MIR reference\n");
    cycles_agree(&m, &layout, arith);
    cycles_agree(&m, &layout, mul16);
    cycles_agree(&m, &layout, cmp);
    cycles_agree(&m, &layout, ls);

    /* Block labels are per function and every branch stays within reach. */
    size_t segments = 0;
    Candidate c = link(&m, &layout, loop, &segments);
    size_t labels = 0, branches = 0;
    for (size_t i = 0; i < c.num_instructions; i++) {
        if (avr_instr_is_label(&c.instructions[i])) {
            if (!strcmp(c.instructions[i].operands[0], "table")) continue; /* constant data */
            assert(strncmp(c.instructions[i].operands[0], ".Lf5", 4) == 0);
            labels++;
        }
        if (!strcmp(c.instructions[i].mnemonic, "breq") || !strcmp(c.instructions[i].mnemonic, "rjmp")) branches++;
    }
    assert(labels >= 4 && branches >= 3 && segments >= 2);
    candidate_free(&c);
    printf("  loop: %zu labels, %zu branches, %zu segments (prologue + body)\n", labels, branches, segments);

    /* The copy in load_store is not folded (its result feeds an add); a
     * load consumed only by the next store is, and needs no slot. */
    assert(!avr_mir_value_folded(&m, ls, 0));
    avr_mir_layout_free(&layout);
    mir_module_free(&m);

    mir_module_init(&m);
    {
        uint32_t g = mir_add_object(&m, MIR_MEM_GLOBAL, 2, "g", MIR_NONE);
        uint32_t f = mir_add_function(&m, "copy", MIR_TYPE_VOID);
        uint32_t v = mir_new_value(&m, f, MIR_TYPE_I8);
        uint32_t e = mir_add_block(&m, f);
        mir_emit_load(&m, f, e, v, MIR_TYPE_I8, mir_at_object(g, 0), 7);
        mir_emit_store(&m, f, e, MIR_TYPE_I8, mir_at_object(g, 1), mir_value(v), 7);
        mir_emit_store(&m, f, e, MIR_TYPE_I8, mir_at_object(g, 0), mir_imm(0x5A), 8);
        mir_ret(&m, f, e, mir_none());
        assert(mir_verify(&m, msg, sizeof(msg)) == 0);
        assert(avr_mir_value_folded(&m, f, v));
        assert(avr_mir_layout_init(&m, &layout) == 0);
        layout.object_addr[g] = 0x0300; /* a caller-fixed address is kept */
        assert(avr_mir_layout_place_rest(&m, &layout, SRAM_LAYOUT_BASE, SRAM_LAYOUT_LIMIT, &end) == 0);
        assert(layout.value_slot[f][v] == 0 && end == SRAM_LAYOUT_BASE);
        AvrMirCode code;
        assert(avr_mir_select_function(&m, &layout, f, 0, &g_cm, &code) == 0);
        assert(code.count == 2 && code.segments[0].origin == 7 && code.segments[1].origin == 8);
        const Candidate *s0 = &code.segments[0].code, *s1 = &code.segments[1].code;
        assert(s0->num_instructions == 2 && !strcmp(s0->instructions[0].mnemonic, "lds") &&
               !strcmp(s0->instructions[0].operands[1], "0x0300") &&
               !strcmp(s0->instructions[1].operands[0], "0x0301"));
        assert(s1->num_instructions == 2 && !strcmp(s1->instructions[0].mnemonic, "ldi") &&
               !strcmp(s1->instructions[0].operands[1], "0x5A"));
        assert(s0->cycles == 4 && s1->cycles == 3);
        avr_mir_code_free(&code);
        avr_mir_layout_free(&layout);
    }
    mir_module_free(&m);
    printf("  folded load/store: lds/sts through r24, no slot; segments split by origin\n");

    /* Refusals: 32-bit multiply, a pointer into program memory. */
    mir_module_init(&m);
    {
        uint32_t t = mir_add_object(&m, MIR_MEM_CONST, 2, "k", MIR_NONE);
        const uint8_t two[2] = {1, 2};
        mir_object_set_init(&m, t, two, 2);
        uint32_t f = mir_add_function(&m, "bad", MIR_TYPE_I32);
        uint32_t a = mir_add_param(&m, f, MIR_TYPE_I32), p = mir_new_value(&m, f, MIR_TYPE_PTR);
        uint32_t e = mir_add_block(&m, f);
        mir_emit_binary(&m, f, e, MIR_MUL, a, MIR_TYPE_I32, mir_value(a), mir_value(a), 0);
        mir_ret(&m, f, e, mir_value(a));
        uint32_t g = mir_add_function(&m, "bad2", MIR_TYPE_VOID);
        p = mir_new_value(&m, g, MIR_TYPE_PTR);
        e = mir_add_block(&m, g);
        mir_emit_addr(&m, g, e, p, t, 0, 0);
        mir_ret(&m, g, e, mir_none());
        assert(mir_verify(&m, msg, sizeof(msg)) == 0);
        assert(avr_mir_layout_init(&m, &layout) == 0);
        assert(avr_mir_layout_place_rest(&m, &layout, SRAM_LAYOUT_BASE, SRAM_LAYOUT_LIMIT, &end) == 0);
        AvrMirCode code;
        assert(avr_mir_select_function(&m, &layout, f, 0, &g_cm, &code) != 0 && code.count == 0);
        assert(avr_mir_select_function(&m, &layout, g, 0, &g_cm, &code) != 0 && code.count == 0);
        avr_mir_layout_free(&layout);
    }
    mir_module_free(&m);
    printf("  refused: 32-bit multiply, address of a program-memory constant\n");

    /* Layout refuses what does not fit. */
    mir_module_init(&m);
    mir_add_object(&m, MIR_MEM_GLOBAL, 0x2000, "huge", MIR_NONE);
    assert(avr_mir_layout_init(&m, &layout) == 0);
    assert(avr_mir_layout_place_rest(&m, &layout, SRAM_LAYOUT_BASE, SRAM_LAYOUT_LIMIT, &end) != 0);
    avr_mir_layout_free(&layout);
    mir_module_free(&m);
    printf("  refused: an object larger than SRAM\n");

    printf("test_avr_mir: all tests passed\n");
    return 0;
}
