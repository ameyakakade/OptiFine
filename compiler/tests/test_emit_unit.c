/* The assembly unit's label namespace, independent of any workload:
 *   - a function with hundreds of blocks emits, every label defined once
 *     (the unit once held a fixed 256 names and refused a 201-block function)
 *   - the same function-local labels emitted twice into one unit are refused,
 *     and are fine in separate units
 *   - a global symbol defined twice is refused, and a refused candidate
 *     leaves nothing of itself recorded
 *   - candidate-local loop labels are renumbered per candidate, references
 *     included */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/avr_mir.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/emit.h"
#include "optifine/mir.h"

static CostModel g_cm;

/* void f(i8 c): `ifs` times { if (c) v = 1; } -- 2 * ifs + 1 blocks */
static uint32_t build_ifs(MirModule *m, int ifs) {
    uint32_t f = mir_add_function(m, "ifs", MIR_TYPE_VOID);
    uint32_t c = mir_add_param(m, f, MIR_TYPE_I8), v = mir_new_value(m, f, MIR_TYPE_I8);
    uint32_t prev = mir_add_block(m, f);
    for (int k = 0; k < ifs; k++) {
        uint32_t t = mir_add_block(m, f), j = mir_add_block(m, f);
        mir_cbr(m, f, prev, mir_value(c), t, j);
        mir_emit_const(m, f, t, v, MIR_TYPE_I8, 1, 0);
        mir_br(m, f, t, j);
        prev = j;
    }
    mir_ret(m, f, prev, mir_none());
    return f;
}

static int emit_code(EmitUnit *unit, const AvrMirCode *code, FILE *out) {
    for (size_t i = 0; i < code->count; i++) {
        if (emit_candidate(unit, &code->segments[i].code, out) != 0) return -1;
    }
    return 0;
}

static char *read_all(FILE *f) {
    long n = ftell(f);
    rewind(f);
    char *text = malloc((size_t)n + 1);
    assert(text && fread(text, 1, (size_t)n, f) == (size_t)n);
    text[n] = '\0';
    return text;
}

/* Counts label definitions in `text`, asserting each is unique. */
static size_t unique_definitions(const char *text) {
    size_t n = 0, cap = 1024;
    char **seen = malloc(cap * sizeof(char *));
    assert(seen);
    for (const char *line = text; *line;) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > 1 && line[0] != ' ' && line[0] != ';' && line[len - 1] == ':') {
            for (size_t i = 0; i < n; i++) assert(strncmp(seen[i], line, len) != 0 || seen[i][len] != '\0');
            if (n == cap) {
                cap *= 2;
                seen = realloc(seen, cap * sizeof(char *));
                assert(seen);
            }
            seen[n] = malloc(len + 1);
            assert(seen[n]);
            memcpy(seen[n], line, len);
            seen[n++][len] = '\0';
        }
        line = end ? end + 1 : line + len;
    }
    for (size_t i = 0; i < n; i++) free(seen[i]);
    free(seen);
    return n;
}

static void test_many_blocks(void) {
    static const int sizes[] = {100, 600};
    for (size_t k = 0; k < 2; k++) {
        MirModule m;
        mir_module_init(&m);
        uint32_t f = build_ifs(&m, sizes[k]);
        AvrMirLayout l;
        uint16_t end;
        assert(avr_mir_layout_init(&m, &l) == 0);
        assert(avr_mir_layout_place_rest(&m, &l, AVR_MIR_SRAM_BASE, AVR_MIR_SRAM_LIMIT, &end) == 0);
        AvrMirCode code;
        assert(avr_mir_select_function(&m, &l, f, 1, &g_cm, &code) == 0);
        FILE *out = tmpfile();
        assert(out);
        EmitUnit unit;
        emit_unit_init(&unit);
        emit_program_prologue(out);
        assert(emit_code(&unit, &code, out) == 0);
        char *text = read_all(out);
        /* blocks + one hop per cbr + _start */
        size_t defs = unique_definitions(text);
        assert(defs == m.functions[f].num_blocks + (size_t)sizes[k] + 1);
        printf("  %zu blocks: %zu labels, each defined once\n", m.functions[f].num_blocks, defs);
        free(text);
        fclose(out);
        emit_unit_free(&unit);
        avr_mir_code_free(&code);
        avr_mir_layout_free(&l);
        mir_module_free(&m);
    }
}

static void test_repeated_function_labels(void) {
    MirModule m;
    mir_module_init(&m);
    uint32_t f = build_ifs(&m, 3);
    AvrMirLayout l;
    uint16_t end;
    assert(avr_mir_layout_init(&m, &l) == 0);
    assert(avr_mir_layout_place_rest(&m, &l, AVR_MIR_SRAM_BASE, AVR_MIR_SRAM_LIMIT, &end) == 0);
    AvrMirCode code;
    assert(avr_mir_select_function(&m, &l, f, 0, &g_cm, &code) == 0);
    FILE *out = tmpfile();
    assert(out);
    EmitUnit unit;
    emit_unit_init(&unit);
    assert(emit_code(&unit, &code, out) == 0);
    size_t recorded = unit.num_globals;
    assert(emit_code(&unit, &code, out) != 0); /* .Lf0b0 again */
    assert(unit.num_globals == recorded);       /* the refused candidate left nothing behind */
    emit_unit_free(&unit);
    emit_unit_init(&unit);
    assert(emit_code(&unit, &code, out) == 0); /* a separate unit is a separate namespace */
    emit_unit_free(&unit);
    fclose(out);
    avr_mir_code_free(&code);
    avr_mir_layout_free(&l);
    mir_module_free(&m);
    printf("  a function's labels emitted twice into one unit: refused; into two units: fine\n");
}

static void test_duplicate_globals(void) {
    EmitUnit unit;
    emit_unit_init(&unit);
    char name[AVR_OPERAND_LEN];
    for (int i = 0; i < 1000; i++) {
        snprintf(name, sizeof(name), "sym%d", i);
        assert(emit_unit_define(&unit, name) == 0);
    }
    assert(emit_unit_define(&unit, "sym500") != 0);
    InstrBuf b;
    instrbuf_init(&b);
    ins1(&b, AVR_LABEL_MNEMONIC, "fresh");
    ins1(&b, AVR_LABEL_MNEMONIC, "sym7");
    Candidate c;
    assert(instrbuf_price(&b, &g_cm, &c) == 0);
    FILE *out = tmpfile();
    assert(out);
    assert(emit_candidate(&unit, &c, out) != 0);
    assert(unit.num_globals == 1000 && emit_unit_define(&unit, "fresh") == 0); /* rolled back */
    assert(ftell(out) == 0);                                                   /* nothing written */
    candidate_free(&c);
    /* defined twice inside one candidate */
    instrbuf_init(&b);
    ins1(&b, AVR_LABEL_MNEMONIC, "twice");
    ins1(&b, AVR_LABEL_MNEMONIC, "twice");
    assert(instrbuf_price(&b, &g_cm, &c) == 0);
    assert(emit_candidate(&unit, &c, out) != 0);
    candidate_free(&c);
    fclose(out);
    emit_unit_free(&unit);
    printf("  1000 global symbols recorded; duplicates refused, a refused candidate rolled back\n");
}

static void test_local_rebasing(void) {
    /* Two candidates, each numbering its loop labels from .Ldsp0. */
    Candidate c[2];
    for (int k = 0; k < 2; k++) {
        InstrBuf b;
        instrbuf_init(&b);
        LoopCtx loop;
        instrbuf_loop_begin(&b, &loop, 3, 20);
        ins0(&b, "nop_body");
        instrbuf_loop_end(&b, &loop);
        Candidate tmp = {0};
        tmp.instructions = b.items;
        tmp.num_instructions = b.count;
        c[k] = tmp;
    }
    FILE *out = tmpfile();
    assert(out);
    EmitUnit unit;
    emit_unit_init(&unit);
    assert(emit_candidate(&unit, &c[0], out) == 0 && emit_candidate(&unit, &c[1], out) == 0);
    char *text = read_all(out);
    assert(strstr(text, ".Ldsp0:") && strstr(text, ".Ldsp1:"));
    assert(strstr(text, "brne .Ldsp0") && strstr(text, "brne .Ldsp1"));
    assert(unique_definitions(text) == 2 && unit.num_globals == 0);
    free(text);
    fclose(out);
    emit_unit_free(&unit);
    candidate_free(&c[0]);
    candidate_free(&c[1]);
    printf("  candidate-local loop labels renumbered per candidate, references with them\n");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(cost_model_load(argv[1], &g_cm) == 0);
    test_many_blocks();
    test_repeated_function_labels();
    test_duplicate_globals();
    test_local_rebasing();
    printf("test_emit_unit: all tests passed\n");
    return 0;
}
