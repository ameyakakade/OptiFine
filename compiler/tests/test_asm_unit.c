/* One assembly unit built from several loop-generating DSP ops (the six FFT
 * stages, Magnitude, which nests isqrt's loop inside its own, and PeakExtract,
 * with three loops), each lowered
 * through the real lower_op path and therefore each numbering its own loops
 * from .Ldsp0. Before EmitUnit, concatenating them produced duplicate labels
 * that avr-as rejects; this pins that every label in the unit is defined
 * exactly once, every branch target is defined, the global twiddle table
 * cannot be emitted twice, and pricing is untouched by the renaming.
 *
 *   test_asm_unit --emit-fixture <out.s>   writes the unit for avr-gcc/Avrora
 *   test_asm_unit --emit-naive <out.s>     writes it the pre-fix way (one fresh
 *                                          namespace per op), which must NOT
 *                                          assemble -- kept to demonstrate the
 *                                          failure the unit prevents */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/reuse_analysis.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/emit.h"
#include "optifine/ir.h"

static CostModel g_cm;

#define MAX_PARTS 16
typedef struct {
    Candidate parts[MAX_PARTS];
    const char *names[MAX_PARTS];
    size_t count;
    uint32_t cycles;
} Unit;

/* Every loop-generating DSP op the backend currently lowers, in graph order,
 * then a trailer: break + the twiddle table, the one global label. */
static void build_unit(Unit *u) {
    IrGraph g;
    assert(dsp_build_pipeline(&g) == 0);
    SramLayout l;
    assert(sram_layout_build(&g, &l) == 0);
    ReuseAnalysis ra;
    assert(reuse_analyze(&g, &ra) == 0);
    u->count = 0;
    u->cycles = 0;
    for (size_t i = 0; i < g.count; i++) {
        OpKind k = g.ops[i].kind;
        if (k != OP_FFT_BUTTERFLY && k != OP_MAGNITUDE && k != OP_PEAK_EXTRACT) continue;
        assert(u->count < MAX_PARTS);
        assert(lower_op(&g, i, &l, &ra, &g_cm, NULL, 0, &u->parts[u->count]) == 0);
        u->names[u->count] = k == OP_FFT_BUTTERFLY ? "fft stage" : k == OP_MAGNITUDE ? "magnitude" : "peak extract";
        u->cycles += u->parts[u->count].cycles;
        u->count++;
    }
    InstrBuf t;
    instrbuf_init(&t);
    ins0(&t, "break");
    dsp_emit_twiddle_table(&t);
    assert(instrbuf_price(&t, &g_cm, &u->parts[u->count]) == 0);
    u->names[u->count] = "break + table";
    u->cycles += u->parts[u->count].cycles;
    u->count++;
    reuse_analysis_free(&ra);
    sram_layout_free(&l);
    ir_graph_free(&g);
}
static void free_unit(Unit *u) { for (size_t i = 0; i < u->count; i++) candidate_free(&u->parts[i]); }

static int emit_unit(const Unit *u, FILE *out, int naive) {
    EmitUnit unit;
    emit_unit_init(&unit);
    emit_program_prologue(out);
    for (size_t i = 0; i < u->count; i++) {
        fprintf(out, "\n    ; ---- %s ----\n", u->names[i]);
        if (naive) emit_unit_init(&unit); /* the pre-fix behaviour: a namespace per candidate */
        if (emit_candidate(&unit, &u->parts[i], out) != 0) return -1;
    }
    return 0;
}

/* Parses the emitted text back: every `name:` is a definition, and the one
 * operand of brne/breq/rjmp is a reference. */
#define MAX_LABELS 256
static void check_text_labels(const char *text, int *defs_out, int *refs_out) {
    static char defs[MAX_LABELS][AVR_OPERAND_LEN];
    int nd = 0, nr = 0;
    const char *line = text;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        if (len > 1 && line[0] == '.' && line[len - 1] == ':') {
            char name[AVR_OPERAND_LEN];
            snprintf(name, sizeof(name), "%.*s", (int)(len - 1), line);
            for (int i = 0; i < nd; i++) {
                if (!strcmp(defs[i], name)) { printf("  duplicate label %s\n", name); assert(0); }
            }
            assert(nd < MAX_LABELS);
            snprintf(defs[nd++], AVR_OPERAND_LEN, "%s", name);
        }
        line = end ? end + 1 : line + len;
    }
    line = text;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        char m[16], op[AVR_OPERAND_LEN];
        char buf[128];
        snprintf(buf, sizeof(buf), "%.*s", (int)len, line);
        if (sscanf(buf, " %15s %23s", m, op) == 2 &&
            (!strcmp(m, "brne") || !strcmp(m, "breq") || !strcmp(m, "rjmp"))) {
            int found = 0;
            for (int i = 0; i < nd; i++) found |= !strcmp(defs[i], op);
            if (!found) { printf("  undefined branch target %s\n", op); assert(0); }
            nr++;
        }
        line = end ? end + 1 : line + len;
    }
    *defs_out = nd;
    *refs_out = nr;
}

static char *emit_to_string(const Unit *u, int naive, int *rc) {
    FILE *f = tmpfile();
    assert(f);
    *rc = emit_unit(u, f, naive);
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);
    assert(fread(s, 1, (size_t)n, f) == (size_t)n);
    s[n] = '\0';
    fclose(f);
    return s;
}

static void test_unit_labels_are_unique(void) {
    Unit u;
    build_unit(&u);
    int rc, defs, refs;
    char *text = emit_to_string(&u, 0, &rc);
    assert(rc == 0);
    check_text_labels(text, &defs, &refs);
    printf("  %zu candidates in one unit: %d label definitions, all distinct; %d branch references, all defined\n",
           u.count, defs, refs);
    free(text);

    /* The naive emission really does collide -- the regression this guards. */
    text = emit_to_string(&u, 1, &rc);
    int collisions = 0;
    {
        const char *p = text;
        while ((p = strstr(p, "\n.Ldsp0:\n")) != NULL) { collisions++; p++; }
    }
    printf("  naive per-candidate emission defines .Ldsp0 %d times\n", collisions);
    assert(collisions > 1);
    free(text);
    free_unit(&u);
}

static void test_global_label_defined_once(void) {
    Unit u;
    build_unit(&u);
    EmitUnit unit;
    emit_unit_init(&unit);
    FILE *f = tmpfile();
    const Candidate *table = &u.parts[u.count - 1];
    assert(emit_candidate(&unit, table, f) == 0);
    assert(emit_candidate(&unit, table, f) != 0); /* second .Ltw refused */
    fclose(f);
    printf("  a second definition of .Ltw in one unit is refused\n");
    free_unit(&u);
}

/* Renaming is textual only: the Candidate, and hence its price, is the same
 * object the emitter was handed. Checked by re-lowering and comparing. */
static void test_pricing_unaffected(void) {
    Unit a, b;
    build_unit(&a);
    build_unit(&b);
    int rc;
    char *text = emit_to_string(&a, 0, &rc);
    free(text);
    assert(a.count == b.count);
    for (size_t i = 0; i < a.count; i++) {
        assert(a.parts[i].cycles == b.parts[i].cycles);
        assert(a.parts[i].num_instructions == b.parts[i].num_instructions);
    }
    printf("  unit total %u cycles predicted (sum of per-op candidates, break included)\n", a.cycles);
    free_unit(&a);
    free_unit(&b);
}

/* The scratch lifetime rule (sram_layout.h): every DSP op reads a scratch
 * cell only after writing it in its own code, so nothing crosses an op
 * boundary through scratch and different ops' cells may alias. Program order
 * is first-iteration order for these single-entry loops, so a read with no
 * earlier write in the candidate is a read of another op's leftover. Data
 * pointers (X/Y/Z loaded with ldi pairs) must never aim into the arena. */
static void test_scratch_is_op_local(void) {
    IrGraph g;
    assert(dsp_build_pipeline(&g) == 0);
    SramLayout l;
    assert(sram_layout_build(&g, &l) == 0);
    ReuseAnalysis ra;
    assert(reuse_analyze(&g, &ra) == 0);
    uint32_t lo = l.dsp_scratch_addr, hi = lo + DSP_SCRATCH_BYTES;
    int ops = 0;
    for (size_t i = 0; i < g.count; i++) {
        OpKind k = g.ops[i].kind;
        if (k != OP_WINDOW && k != OP_BIT_REVERSE && k != OP_FFT_BUTTERFLY && k != OP_MAGNITUDE &&
            k != OP_PEAK_EXTRACT) continue;
        Candidate c;
        assert(lower_op(&g, i, &l, &ra, &g_cm, NULL, 0, &c) == 0);
        static uint8_t written[AVR_OPERAND_LEN * 64];
        memset(written, 0, sizeof(written));
        int touched = 0, ptr_lo[32];
        for (int r = 0; r < 32; r++) ptr_lo[r] = -1;
        for (size_t j = 0; j < c.num_instructions; j++) {
            const AvrInstr *ins = &c.instructions[j];
            if (!strcmp(ins->mnemonic, "sts") || !strcmp(ins->mnemonic, "lds")) {
                int is_st = ins->mnemonic[0] == 's';
                uint32_t a = (uint32_t)strtoul(ins->operands[is_st ? 0 : 1], NULL, 16);
                if (a < lo || a >= hi) continue;
                assert(a - lo < sizeof(written));
                if (is_st) { if (!written[a - lo]) touched++; written[a - lo] = 1; }
                else if (!written[a - lo]) {
                    printf("  op %zu reads scratch+%u before writing it\n", i, a - lo);
                    assert(0);
                }
            }
            if (!strcmp(ins->mnemonic, "ldi") && ins->operands[1][0] == '0') {
                int r = atoi(ins->operands[0] + 1);
                int v = (int)strtol(ins->operands[1], NULL, 16);
                if (r == 26 || r == 28 || r == 30) ptr_lo[r] = v;
                if ((r == 27 || r == 29 || r == 31) && ptr_lo[r - 1] >= 0) {
                    uint32_t a = (uint32_t)(ptr_lo[r - 1] | (v << 8));
                    /* The one sanctioned exception: PeakExtract's X walks its
                     * own selected[], which it zeroes on entry. */
                    int sanctioned = k == OP_PEAK_EXTRACT && r == 27 &&
                                     a == lo + DSP_SCRATCH_PK_SELECTED;
                    assert(sanctioned || a < lo || a >= hi);
                    ptr_lo[r - 1] = -1;
                }
            }
        }
        printf("  op %2zu (%s): %d scratch bytes, each written before read\n", i,
               k == OP_WINDOW ? "window" : k == OP_BIT_REVERSE ? "bitreverse"
               : k == OP_FFT_BUTTERFLY ? "fft stage" : k == OP_MAGNITUDE ? "magnitude"
               : "peak extract", touched);
        candidate_free(&c);
        ops++;
    }
    assert(ops == 10);
    reuse_analysis_free(&ra);
    sram_layout_free(&l);
    ir_graph_free(&g);
}

int main(int argc, char **argv) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
    test_unit_labels_are_unique();
    test_global_label_defined_once();
    test_pricing_unaffected();
    test_scratch_is_op_local();

    if (argc == 3 && (!strcmp(argv[1], "--emit-fixture") || !strcmp(argv[1], "--emit-naive"))) {
        Unit u;
        build_unit(&u);
        FILE *out = fopen(argv[2], "w");
        assert(out);
        assert(emit_unit(&u, out, !strcmp(argv[1], "--emit-naive")) == 0);
        fclose(out);
        printf("wrote %s: %u cycles predicted\n", argv[2], u.cycles);
        free_unit(&u);
    }
    printf("test_asm_unit: all tests passed\n");
    return 0;
}
