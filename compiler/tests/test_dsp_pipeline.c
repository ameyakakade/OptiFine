/* The complete DSP program, end to end, through the production path.
 *
 * codegen_emit_dsp_program -- what `optifine --dsp` calls -- writes the .s.
 * The test builds the same candidates through the same public lowering calls,
 * checks that their instruction stream is exactly the production file's (so
 * what it executes IS what ships), and runs that stream in the interpreter as
 * one program image, so the twiddle table sits at the flash address the
 * linker gives it.
 *
 * The host reference is independent of the lowering: Window as the Q15
 * product with the graph's own coefficients, the bit-reversal permutation, a
 * textbook-indexed FFT, integer magnitude, and a sort for the top 8. Every
 * graph buffer -- Input through Output -- is compared exactly after each run.
 *
 * Each vector runs twice as one program under different SRAM poison, and a
 * third time op by op with the scratch arena re-poisoned between ops: all
 * three must agree. Writes outside graph tensors + scratch fail the test. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/program.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/emit.h"
#include "optifine/ir.h"

#include "avr_interp.h"

#define N DSP_FFT_SIZE
#define K DSP_MAX_PEAKS
#define STAGES DSP_FFT_LOG2

static CostModel g_cm;

/* ------------------------------------------------------ host reference */

typedef struct { int16_t re[N], im[N]; } Cbuf;
typedef struct {
    int16_t input[N], coeff[N], window[N];
    Cbuf bitrev, stage[STAGES];
    uint16_t mag[N], peak[K];
} Ref;

static int16_t h_qmul(int16_t x, int16_t y) { return (int16_t)(((int32_t)x * (int32_t)y) >> 15); }
static int16_t h_asr(int16_t v)             { return (int16_t)(v >> 1); }
static int16_t h_add(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a + (uint16_t)b); }
static int16_t h_sub(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a - (uint16_t)b); }
static int brev6(int i) { int r = 0; for (int b = 0; b < STAGES; b++) { r = (r << 1) | (i & 1); i >>= 1; } return r; }
static uint16_t isqrt64(int64_t s) {
    int64_t r = (int64_t)sqrt((double)s);
    while (r * r > s) r--;
    while ((r + 1) * (r + 1) <= s) r++;
    return (uint16_t)r;
}
static int cmp_desc(const void *a, const void *b) {
    uint16_t x = *(const uint16_t *)a, y = *(const uint16_t *)b;
    return x == y ? 0 : (x > y ? -1 : 1);
}

static void host_pipeline(const int16_t *x, const int16_t *coeff, Ref *r) {
    memcpy(r->input, x, sizeof(r->input));
    memcpy(r->coeff, coeff, sizeof(r->coeff));
    for (int n = 0; n < N; n++) r->window[n] = h_qmul(x[n], coeff[n]);
    for (int n = 0; n < N; n++) { r->bitrev.re[brev6(n)] = r->window[n]; r->bitrev.im[brev6(n)] = 0; }
    const Cbuf *a = &r->bitrev;
    for (int s = 0; s < STAGES; s++) {
        Cbuf *b = &r->stage[s];
        int m = 2 << s;
        for (int k = 0; k < N; k += m) {
            for (int j = 0; j < m / 2; j++) {
                int p = k + j, q = p + m / 2;
                int16_t wr, wi;
                dsp_twiddle_q15(j * (N / m), &wr, &wi);
                int16_t t_re = h_sub(h_qmul(wr, a->re[q]), h_qmul(wi, a->im[q]));
                int16_t t_im = h_add(h_qmul(wr, a->im[q]), h_qmul(wi, a->re[q]));
                int16_t ph = h_asr(a->re[p]), th = h_asr(t_re);
                b->re[p] = h_add(ph, th); b->re[q] = h_sub(ph, th);
                ph = h_asr(a->im[p]); th = h_asr(t_im);
                b->im[p] = h_add(ph, th); b->im[q] = h_sub(ph, th);
            }
        }
        a = b;
    }
    for (int k = 0; k < N; k++) {
        int64_t re = r->stage[STAGES - 1].re[k], im = r->stage[STAGES - 1].im[k];
        r->mag[k] = isqrt64(re * re + im * im);
    }
    uint16_t sorted[N];
    memcpy(sorted, r->mag, sizeof(sorted));
    qsort(sorted, N, sizeof(uint16_t), cmp_desc);
    memcpy(r->peak, sorted, sizeof(r->peak));
}

/* -------------------------------------------------------------- program */

typedef struct {
    IrGraph graph;
    SramLayout layout;
    RegAllocResult ra;
    size_t op_of[16];            /* op id by role, see ROLE_* */
    int n_ops;
} Fx;

enum { ROLE_INPUT, ROLE_CONST, ROLE_WINDOW, ROLE_BITREV, ROLE_FFT0, ROLE_MAG = ROLE_FFT0 + STAGES,
       ROLE_PEAK, ROLE_OUTPUT, ROLE_COUNT };

static void fx_init(Fx *f) {
    assert(dsp_build_pipeline(&f->graph) == 0);
    assert(sram_layout_build(&f->graph, &f->layout) == 0);
    assert(regalloc_next_use(&f->graph, &f->ra) == 0);
    /* The emitted operation order the rest of the test assumes, read from
     * the graph: ids ascend in exactly this role order. */
    static const OpKind kinds[ROLE_COUNT] = {OP_INPUT, OP_CONST, OP_WINDOW, OP_BIT_REVERSE,
        OP_FFT_BUTTERFLY, OP_FFT_BUTTERFLY, OP_FFT_BUTTERFLY, OP_FFT_BUTTERFLY, OP_FFT_BUTTERFLY,
        OP_FFT_BUTTERFLY, OP_MAGNITUDE, OP_PEAK_EXTRACT, OP_OUTPUT};
    assert(f->graph.count == ROLE_COUNT);
    for (int r = 0; r < ROLE_COUNT; r++) {
        assert(f->graph.ops[r].kind == kinds[r]);
        f->op_of[r] = (size_t)r;
    }
}
static void fx_free(Fx *f) { regalloc_result_free(&f->ra); sram_layout_free(&f->layout); ir_graph_free(&f->graph); }
static uint16_t addr_of(const Fx *f, int role) { return sram_layout_addr(&f->layout, &f->graph, f->op_of[role], 0); }

/* The production candidates, in emission order: zero-register init, the
 * initialization ops, the body ops, then the program end -- the order
 * codegen_emit_initialization / _inference_body / codegen_emit_dsp_program
 * use. */
#define MAX_PARTS 20
typedef struct { Candidate part[MAX_PARTS]; int role[MAX_PARTS]; size_t n; } Parts;

static void build_parts(Fx *f, const int8_t *input, size_t len, Parts *p) {
    p->n = 0;
    assert(lower_init_zero_reg(&g_cm, &p->part[p->n]) == 0); p->role[p->n++] = -1;
    for (int pass = 0; pass < 2; pass++) {
        for (int r = 0; r < ROLE_COUNT; r++) {
            int prologue = r == ROLE_INPUT || r == ROLE_CONST;
            if (prologue != (pass == 0)) continue;
            assert(lower_op(&f->graph, f->op_of[r], &f->layout, &f->ra, &g_cm, input, len, &p->part[p->n]) == 0);
            p->role[p->n++] = r;
        }
    }
    assert(lower_dsp_program_end(&f->graph, &g_cm, &p->part[p->n]) == 0); p->role[p->n++] = -2;
}
static void free_parts(Parts *p) { for (size_t i = 0; i < p->n; i++) candidate_free(&p->part[i]); }

/* Concatenates candidates into one, renumbering local labels exactly as an
 * EmitUnit does, so the interpreter sees the program as the linker lays it. */
static void concat(const Candidate *parts, size_t n, Candidate *out) {
    size_t total = 0;
    for (size_t i = 0; i < n; i++) total += parts[i].num_instructions;
    out->instructions = malloc(total * sizeof(AvrInstr));
    out->num_instructions = 0;
    out->cycles = 0;
    out->energy_nj = 0;
    unsigned base = 0;
    for (size_t i = 0; i < n; i++) {
        unsigned span = 0;
        for (size_t j = 0; j < parts[i].num_instructions; j++) {
            AvrInstr ins = parts[i].instructions[j];
            unsigned idx;
            if (avr_instr_is_label(&ins) && avr_local_label_index(ins.operands[0], &idx) && idx + 1 > span) span = idx + 1;
            for (int o = 0; o < ins.num_operands; o++) {
                char re[AVR_OPERAND_LEN];
                if (avr_local_label_rebase(ins.operands[o], base, re)) snprintf(ins.operands[o], AVR_OPERAND_LEN, "%s", re);
            }
            out->instructions[out->num_instructions++] = ins;
        }
        out->cycles += parts[i].cycles;
        out->energy_nj += parts[i].energy_nj;
        base += span;
    }
}

/* ------------------------------------------------------- checks per run */

static void poison(uint8_t *mem, uint8_t seed) {
    for (size_t a = 0; a < AVR_INTERP_MEM_SIZE; a++) mem[a] = (uint8_t)(seed ? (seed ^ (a * 29)) : 0);
}

/* Every write must land in a graph tensor or the scratch arena; everything
 * below the graph area and past the arena is a guard region. */
static void check_writes(const Fx *f, const uint8_t *before, const uint8_t *after) {
    uint32_t lo = SRAM_LAYOUT_BASE, scratch = f->layout.dsp_scratch_addr, hi = scratch + DSP_SCRATCH_BYTES;
    assert(lo + f->layout.bytes_used == hi);
    for (uint32_t a = 0; a < AVR_INTERP_MEM_SIZE; a++) {
        if (before[a] == after[a]) continue;
        if (a < lo || a >= hi) { printf("    write outside graph+scratch at 0x%04X\n", a); assert(0); }
    }
}

static int16_t rd16(const uint8_t *m, uint32_t a) { return (int16_t)(m[a] | (m[a + 1] << 8)); }

/* Compares every graph buffer against the reference; counts compared
 * elements per stage into `counts` (one slot per role). */
static int check_buffers(const Fx *f, const uint8_t *m, const Ref *r, unsigned long *counts) {
    int bad = 0;
#define EQ(role, got, want) do { counts[role]++; if ((got) != (want)) { if (bad < 4) \
    printf("    role %d elem %d: got %d want %d\n", role, i, (int)(got), (int)(want)); bad++; } } while (0)
    for (int i = 0; i < N; i++) {
        EQ(ROLE_INPUT, rd16(m, addr_of(f, ROLE_INPUT) + 2 * i), r->input[i]);
        EQ(ROLE_CONST, rd16(m, addr_of(f, ROLE_CONST) + 2 * i), r->coeff[i]);
        EQ(ROLE_WINDOW, rd16(m, addr_of(f, ROLE_WINDOW) + 2 * i), r->window[i]);
        uint16_t b = addr_of(f, ROLE_BITREV);
        EQ(ROLE_BITREV, rd16(m, b + 4 * i), r->bitrev.re[i]);
        EQ(ROLE_BITREV, rd16(m, b + 4 * i + 2), r->bitrev.im[i]);
        for (int s = 0; s < STAGES; s++) {
            uint16_t a = addr_of(f, ROLE_FFT0 + s);
            EQ(ROLE_FFT0 + s, rd16(m, a + 4 * i), r->stage[s].re[i]);
            EQ(ROLE_FFT0 + s, rd16(m, a + 4 * i + 2), r->stage[s].im[i]);
        }
        EQ(ROLE_MAG, (uint16_t)rd16(m, addr_of(f, ROLE_MAG) + 2 * i), r->mag[i]);
    }
    for (int i = 0; i < K; i++) {
        EQ(ROLE_PEAK, (uint16_t)rd16(m, addr_of(f, ROLE_PEAK) + 2 * i), r->peak[i]);
    }
    for (int i = 0; i < K * 2; i++) {    /* Output: all 16 bytes, byte by byte */
        uint8_t want = (uint8_t)(i & 1 ? r->peak[i / 2] >> 8 : r->peak[i / 2] & 0xFF);
        EQ(ROLE_OUTPUT, m[addr_of(f, ROLE_OUTPUT) + i], want);
    }
#undef EQ
    return bad;
}

/* ------------------------------------------------------------ vectors */

#define MAX_VEC 24
static int16_t g_vec[MAX_VEC][N];
static const char *g_name[MAX_VEC];
static int g_nvec;

static int16_t *add_vec(const char *name) {
    assert(g_nvec < MAX_VEC);
    g_name[g_nvec] = name;
    memset(g_vec[g_nvec], 0, sizeof(g_vec[0]));
    return g_vec[g_nvec++];
}

static void build_vectors(void) {
    const double pi = 3.14159265358979323846;
    int16_t *v;
    add_vec("all zeros");
    v = add_vec("impulse at 0"); v[0] = 32767;
    v = add_vec("impulse at 1"); v[1] = 32767;
    v = add_vec("DC 16384");     for (int n = 0; n < N; n++) v[n] = 16384;
    v = add_vec("DC max");       for (int n = 0; n < N; n++) v[n] = INT16_MAX;
    v = add_vec("tone k=1");     for (int n = 0; n < N; n++) v[n] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * n / N));
    v = add_vec("tone k=5");     for (int n = 0; n < N; n++) v[n] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * 5 * n / N));
    v = add_vec("tone k=23");    for (int n = 0; n < N; n++) v[n] = (int16_t)lround(0.9 * 32767 * sin(2 * pi * 23 * n / N));
    v = add_vec("tone k=31");    for (int n = 0; n < N; n++) v[n] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * 31 * n / N));
    v = add_vec("alternating INT16_MIN/MAX"); for (int n = 0; n < N; n++) v[n] = (n & 1) ? INT16_MIN : INT16_MAX;
    v = add_vec("deterministic mixed"); for (int n = 0; n < N; n++) v[n] = (int16_t)(n * 1031 - 16384);
    v = add_vec("negative-heavy"); for (int n = 0; n < N; n++) v[n] = (int16_t)(-32768 + (n * 97) % 4000);
    v = add_vec("Q15 boundary");
    { static const int16_t e[] = {0, 1, -1, INT16_MAX, INT16_MIN, 16384, -16384, 32766, -32767};
      for (int n = 0; n < N; n++) v[n] = e[n % 9]; }
    static const uint32_t seeds[] = {1u, 42u, 0xC0FFEEu, 20260924u};
    static char rn[4][24];
    for (int s = 0; s < 4; s++) {
        snprintf(rn[s], sizeof(rn[s]), "random seed %u", (unsigned)seeds[s]);
        v = add_vec(rn[s]);
        uint32_t x = seeds[s];
        for (int n = 0; n < N; n++) { x = x * 1664525u + 1013904223u; v[n] = (int16_t)(x >> 16); }
    }
    /* The production default input, read from the same file `optifine --dsp` uses. */
    v = add_vec("models/dsp_demo_input.txt");
    FILE *in = fopen("models/dsp_demo_input.txt", "r");
    assert(in);
    char line[512];
    int n = 0;
    while (fgets(line, sizeof(line), in)) {
        char *h = strchr(line, '#'); if (h) *h = 0;
        char *c = line, *e;
        for (long x; (x = strtol(c, &e, 10)), e != c; c = e) { assert(n < N); v[n++] = (int16_t)x; }
    }
    fclose(in);
    assert(n == N);
}

static void to_bytes(const int16_t *v, int8_t *bytes) {
    for (int n = 0; n < N; n++) {
        bytes[2 * n] = (int8_t)(uint8_t)((uint16_t)v[n] & 0xFF);
        bytes[2 * n + 1] = (int8_t)(uint8_t)((uint16_t)v[n] >> 8);
    }
}

/* -------------------------------------------------------------- tests */

static char *read_all(FILE *f) {
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);
    assert(fread(s, 1, (size_t)n, f) == (size_t)n);
    s[n] = 0;
    return s;
}

/* Instruction lines only: comments, blank lines and the prologue directives
 * differ by design between the production file and a bare re-emission. */
static char *instruction_lines(const char *text) {
    char *out = malloc(strlen(text) + 1);
    size_t o = 0;
    const char *line = text;
    while (*line) {
        const char *end = strchr(line, '\n');
        size_t len = end ? (size_t)(end - line) : strlen(line);
        size_t k = 0;
        while (k < len && (line[k] == ' ' || line[k] == '\t')) k++;
        int keep = k < len && line[k] != ';' && line[k] != '.' ? 1 : 0;
        if (k < len && line[k] == '.' && len > 1 && line[len - 1] == ':') keep = 1;   /* labels */
        if (k < len && !strncmp(line + k, ".word", 5)) keep = 1;                    /* table data */
        if (keep) { memcpy(out + o, line + k, len - k); o += len - k; out[o++] = '\n'; }
        line = end ? end + 1 : line + len;
    }
    out[o] = 0;
    return out;
}

static void test_production_program(Fx *f, const int8_t *input) {
    FILE *prod = tmpfile();
    DspProgramCost cost;
    assert(codegen_emit_dsp_program(&f->graph, &f->layout, &f->ra, &g_cm, input, N * 2, prod, &cost) == 0);
    char *text = read_all(prod);
    fclose(prod);

    Parts p;
    build_parts(f, input, N * 2, &p);
    FILE *re = tmpfile();
    EmitUnit unit; emit_unit_init(&unit);
    emit_program_prologue(re);
    for (size_t i = 0; i < p.n; i++) assert(emit_candidate(&unit, &p.part[i], re) == 0);
    char *mine = read_all(re);
    fclose(re);
    char *a = instruction_lines(text), *b = instruction_lines(mine);
    assert(strcmp(a, b) == 0);          /* the interpreted stream IS the production stream */

    /* Assembly-unit properties of the production file. */
    int defs = 0, ltw = 0, brk_line = -1, ltw_line = -1, line_no = 0;
    char names[64][AVR_OPERAND_LEN];
    for (char *l = a; *l; line_no++) {
        char *e = strchr(l, '\n'); *e = 0;
        size_t len = strlen(l);
        if (l[0] == '.' && l[len - 1] == ':') {
            l[len - 1] = 0;
            for (int i = 0; i < defs; i++) assert(strcmp(names[i], l) != 0);
            assert(defs < 64);
            snprintf(names[defs++], AVR_OPERAND_LEN, "%s", l);
            if (!strcmp(l, ".Ltw")) { ltw++; ltw_line = line_no; }
        }
        if (!strcmp(l, "break") && brk_line < 0) brk_line = line_no;
        l = e + 1;
    }
    assert(ltw == 1 && brk_line >= 0 && ltw_line == brk_line + 1);
    const char *s = b;                  /* every branch target defined */
    int refs = 0;
    while ((s = strstr(s, "\n")) != NULL) {
        s++;
        char m[16], op[AVR_OPERAND_LEN];
        if (sscanf(s, "%15s %23s", m, op) == 2 && (!strcmp(m, "brne") || !strcmp(m, "breq") || !strcmp(m, "rjmp"))) {
            int found = 0;
            for (int i = 0; i < defs; i++) found |= !strcmp(names[i], op);
            assert(found);
            refs++;
        }
    }
    uint32_t total = cost.initialization.cycles + cost.body.cycles + cost.termination.cycles;
    uint32_t sum = 0;
    for (size_t i = 0; i < p.n; i++) sum += p.part[i].cycles;
    assert(total == sum);
    printf("  production file == interpreted stream; %d labels, all unique; %d branches, all defined; "
           "one .Ltw, directly after the only break\n", defs, refs);
    printf("  production cost: initialization %u + pipeline %u + termination %u = %u cycles predicted\n",
           cost.initialization.cycles, cost.body.cycles, cost.termination.cycles, total);
    free(a); free(b); free(text); free(mine);
    free_parts(&p);
}

static unsigned long g_cycles, g_instrs;

static void test_vectors(Fx *f) {
    const int16_t *coeff = (const int16_t *)f->graph.ops[f->op_of[ROLE_CONST]].data;
    unsigned long counts[ROLE_COUNT] = {0};
    int bad = 0;
    long table_addr = -1;
    for (int vi = 0; vi < g_nvec; vi++) {
        Ref ref;
        host_pipeline(g_vec[vi], coeff, &ref);
        int8_t bytes[N * 2];
        to_bytes(g_vec[vi], bytes);
        Parts p;
        build_parts(f, bytes, sizeof(bytes), &p);
        Candidate prog;
        concat(p.part, p.n, &prog);

        uint8_t result[2][AVR_INTERP_MEM_SIZE];
        for (int t = 0; t < 2; t++) {                /* whole program, two poisons */
            AvrInterp *in = malloc(sizeof(AvrInterp));
            avr_interp_init(in);
            poison(in->mem, t ? 0xA5 : 0x00);
            uint8_t *before = malloc(AVR_INTERP_MEM_SIZE);
            memcpy(before, in->mem, AVR_INTERP_MEM_SIZE);
            assert(avr_interp_run(in, &prog) == 0);
            check_writes(f, before, in->mem);
            assert(in->cycles == prog.cycles);
            if (g_cycles == 0) { g_cycles = in->cycles; g_instrs = in->instructions; }
            assert(in->cycles == g_cycles && in->instructions == g_instrs);
            bad += check_buffers(f, in->mem, &ref, counts);
            memcpy(result[t], in->mem, AVR_INTERP_MEM_SIZE);
            if (table_addr < 0) {                     /* the table as the program image holds it */
                size_t addr = 0;
                for (size_t j = 0; j < prog.num_instructions; j++) {
                    if (avr_instr_is_label(&prog.instructions[j]) && !strcmp(prog.instructions[j].operands[0], ".Ltw")) break;
                    addr += avr_instr_flash_bytes(&prog.instructions[j]);
                }
                table_addr = (long)addr;
                assert(table_addr + 128 <= (long)in->progmem_size && table_addr < 0x10000);
                for (int k = 0; k < 32; k++) {
                    int16_t wr, wi; dsp_twiddle_q15(k, &wr, &wi);
                    assert(rd16(in->progmem, (uint32_t)table_addr + 4 * k) == wr);
                    assert(rd16(in->progmem, (uint32_t)table_addr + 4 * k + 2) == wi);
                }
            }
            free(before);
            free(in);
        }
        /* Graph tensors must not depend on the poison (scratch may differ). */
        uint32_t lo = SRAM_LAYOUT_BASE, hi = f->layout.dsp_scratch_addr;
        assert(memcmp(result[0] + lo, result[1] + lo, hi - lo) == 0);

        /* Op by op, scratch re-poisoned between every op. FFT stages carry
         * the table, so each op runs with the program end appended. */
        AvrInterp *in = malloc(sizeof(AvrInterp));
        avr_interp_init(in);
        poison(in->mem, 0x3C);
        Candidate end;
        assert(lower_dsp_program_end(&f->graph, &g_cm, &end) == 0);
        for (size_t i = 0; i + 1 < p.n; i++) {
            for (int b = 0; b < DSP_SCRATCH_BYTES; b++) in->mem[f->layout.dsp_scratch_addr + b] = (uint8_t)(0x5A + 31 * b + 7 * i);
            Candidate pair[2] = {p.part[i], end}, one;
            concat(pair, 2, &one);
            assert(avr_interp_run(in, &one) == 0);
            free(one.instructions);
        }
        candidate_free(&end);
        assert(memcmp(in->mem + lo, result[0] + lo, hi - lo) == 0);
        free(in);

        printf("  %-28s all 13 graph buffers exact (x2 poisons, + op-by-op re-poisoned); peaks", g_name[vi]);
        for (int k = 0; k < K; k++) printf(" %u", ref.peak[k]);
        printf("\n");
        free(prog.instructions);
        free_parts(&p);
    }
    static const char *role_name[ROLE_COUNT] = {"Input", "Const", "Window", "BitReverse", "FFT 0", "FFT 1",
        "FFT 2", "FFT 3", "FFT 4", "FFT 5", "Magnitude", "PeakExtract", "Output"};
    printf("  per-buffer comparisons (all vectors, both poisons):\n");
    for (int r = 0; r < ROLE_COUNT; r++) printf("    %-11s %6lu values, all exact\n", role_name[r], counts[r]);
    printf("  %d vectors, %d mismatches; twiddle table at program byte 0x%04lX, all 32 entries exact\n",
           g_nvec, bad, (unsigned long)table_addr);
    assert(bad == 0);
}

static void report_program(Fx *f) {
    int8_t bytes[N * 2];
    to_bytes(g_vec[0], bytes);
    Parts p;
    build_parts(f, bytes, sizeof(bytes), &p);
    size_t emitted = 0, code = 0, data = 0;
    printf("  per-op (emitted instructions, code bytes, predicted cycles):\n");
    for (size_t i = 0; i < p.n; i++) {
        size_t e = 0, c = 0, d = 0;
        for (size_t j = 0; j < p.part[i].num_instructions; j++) {
            const AvrInstr *ins = &p.part[i].instructions[j];
            if (avr_instr_is_label(ins)) continue;
            if (avr_instr_is_data_word(ins)) { d += 2; continue; }
            e++; c += avr_instr_flash_bytes(ins);
        }
        const char *nm = p.role[i] == -1 ? "clr r2" : p.role[i] == -2 ? "break + twiddle table"
                       : (const char *[]){"Input", "Const", "Window", "BitReverse", "FFT 0", "FFT 1", "FFT 2",
                                          "FFT 3", "FFT 4", "FFT 5", "Magnitude", "PeakExtract", "Output"}[p.role[i]];
        printf("    %-22s %5zu %6zu B %s %7u\n", nm, e, c, d ? "+ data" : "      ", p.part[i].cycles);
        if (d) printf("    %-22s       %6zu B\n", "  (table data)", d);
        emitted += e; code += c; data += d;
    }
    printf("  total: %zu instructions, %zu B code + %zu B data = %zu B; %lu executed, %lu cycles\n",
           emitted, code, data, code + data, g_instrs, g_cycles);
    free_parts(&p);
}

int main(void) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
    build_vectors();
    Fx f; fx_init(&f);
    int8_t demo[N * 2];
    to_bytes(g_vec[g_nvec - 1], demo);
    test_production_program(&f, demo);
    test_vectors(&f);
    report_program(&f);
    fx_free(&f);
    printf("test_dsp_pipeline: all tests passed\n");
    return 0;
}
