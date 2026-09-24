/* OP_PEAK_EXTRACT: the 8 largest of 64 unsigned magnitudes, largest first.
 *
 * The oracle sorts (value descending, index ascending) and takes the first
 * eight -- structurally unlike the AVR's repeated branch-free scans. The
 * public output is values only, so each run also recovers the SELECTED INDEX
 * of every pass from selected[]'s rank markers (pass counter k..1) and checks
 * it against the oracle's indices: that is what proves distinct selections and
 * the lower-index tie rule, which values alone cannot show.
 *
 * Every run poisons the scratch arena first and is repeated under a second,
 * different poison: identical results prove the op reads no scratch state it
 * did not write itself.
 *
 *   test_dsp_peak --emit-fixture <out.s>      standalone Avrora fixture
 *   test_dsp_peak --emit-integrated <out.s>   FFT x6 + Magnitude + PeakExtract */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/registers.h"
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

/* -------------------------------------------------------------- oracle */

typedef struct { uint16_t v; int idx; } Pair;
static int by_value_then_index(const void *a, const void *b) {
    const Pair *x = a, *y = b;
    if (x->v != y->v) return x->v > y->v ? -1 : 1;
    return x->idx - y->idx;
}
static void host_topk(const uint16_t *v, uint16_t *out_v, int *out_idx) {
    Pair p[N];
    for (int i = 0; i < N; i++) { p[i].v = v[i]; p[i].idx = i; }
    qsort(p, N, sizeof(Pair), by_value_then_index);
    for (int i = 0; i < K; i++) { out_v[i] = p[i].v; out_idx[i] = p[i].idx; }
}

/* Host FFT and magnitude, same definitions as test_dsp_fft/test_dsp_magnitude. */
typedef struct { int16_t re[N], im[N]; } Buf;
static int16_t h_qmul(int16_t x, int16_t y) { return (int16_t)(((int32_t)x * (int32_t)y) >> 15); }
static int16_t h_asr(int16_t v)             { return (int16_t)(v >> 1); }
static int16_t h_add(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a + (uint16_t)b); }
static int16_t h_sub(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a - (uint16_t)b); }
static int brev6(int i) { int r = 0; for (int b = 0; b < STAGES; b++) { r = (r << 1) | (i & 1); i >>= 1; } return r; }
static void host_fft(const Buf *nat, Buf *out) {
    Buf a, b;
    for (int i = 0; i < N; i++) { a.re[brev6(i)] = nat->re[i]; a.im[brev6(i)] = nat->im[i]; }
    for (int s = 0; s < STAGES; s++) {
        int m = 2 << s;
        for (int k = 0; k < N; k += m) {
            for (int j = 0; j < m / 2; j++) {
                int p = k + j, q = p + m / 2;
                int16_t wr, wi;
                dsp_twiddle_q15(j * (N / m), &wr, &wi);
                int16_t t_re = h_sub(h_qmul(wr, a.re[q]), h_qmul(wi, a.im[q]));
                int16_t t_im = h_add(h_qmul(wr, a.im[q]), h_qmul(wi, a.re[q]));
                int16_t ph = h_asr(a.re[p]), th = h_asr(t_re);
                b.re[p] = h_add(ph, th); b.re[q] = h_sub(ph, th);
                ph = h_asr(a.im[p]); th = h_asr(t_im);
                b.im[p] = h_add(ph, th); b.im[q] = h_sub(ph, th);
            }
        }
        a = b;
    }
    *out = a;
}
static uint16_t host_mag(int16_t re, int16_t im) {
    int64_t sum = (int64_t)re * re + (int64_t)im * im;
    int64_t r = (int64_t)sqrt((double)sum);
    while (r * r > sum) r--;
    while ((r + 1) * (r + 1) <= sum) r++;
    return (uint16_t)r;
}

/* ------------------------------------------------------------- fixture */

typedef struct {
    IrGraph graph;
    SramLayout layout;
    RegAllocResult ra;
    size_t bitrev_op, fft5_op, mag_op, pk_op;
    uint16_t bitrev_addr, fft5_addr, mag_addr, pk_addr, sel_addr;
    Candidate pk, mag;
} Fx;

static void fx_init(Fx *f) {
    assert(dsp_build_pipeline(&f->graph) == 0);
    assert(sram_layout_build(&f->graph, &f->layout) == 0);
    assert(regalloc_next_use(&f->graph, &f->ra) == 0);
    for (size_t i = 0; i < f->graph.count; i++) {
        OpKind k = f->graph.ops[i].kind;
        if (k == OP_BIT_REVERSE) f->bitrev_op = i;
        if (k == OP_FFT_BUTTERFLY) f->fft5_op = i;
        if (k == OP_MAGNITUDE) f->mag_op = i;
        if (k == OP_PEAK_EXTRACT) f->pk_op = i;
    }
    /* The contract this test holds the op to, read from the IR. */
    const IrOp *pk = &f->graph.ops[f->pk_op];
    assert(pk->inputs[0] == f->mag_op && pk->num_inputs == 1);
    assert(f->graph.ops[f->mag_op].dtype == DT_FIXED_Q15 && sram_layout_num_elements(&f->graph.ops[f->mag_op]) == N);
    assert(pk->dtype == DT_FIXED_Q15 && sram_layout_num_elements(pk) == K && K == 8);
    for (size_t i = 0; i < f->graph.count; i++) {       /* Output copies PeakExtract's 8 x 2 bytes */
        if (f->graph.ops[i].kind == OP_OUTPUT) {
            assert(f->graph.ops[i].inputs[0] == f->pk_op && f->graph.ops[i].dtype == DT_FIXED_Q15);
            assert(sram_layout_num_elements(&f->graph.ops[i]) == K);
        }
    }
    f->bitrev_addr = sram_layout_addr(&f->layout, &f->graph, f->bitrev_op, 0);
    f->fft5_addr = sram_layout_addr(&f->layout, &f->graph, f->fft5_op, 0);
    f->mag_addr = sram_layout_addr(&f->layout, &f->graph, f->mag_op, 0);
    f->pk_addr = sram_layout_addr(&f->layout, &f->graph, f->pk_op, 0);
    f->sel_addr = (uint16_t)(f->layout.dsp_scratch_addr + DSP_SCRATCH_PK_SELECTED);
    assert(lower_op(&f->graph, f->pk_op, &f->layout, &f->ra, &g_cm, NULL, 0, &f->pk) == 0);
    assert(lower_op(&f->graph, f->mag_op, &f->layout, &f->ra, &g_cm, NULL, 0, &f->mag) == 0);
}
static void fx_free(Fx *f) {
    candidate_free(&f->pk);
    candidate_free(&f->mag);
    regalloc_result_free(&f->ra);
    sram_layout_free(&f->layout);
    ir_graph_free(&f->graph);
}

static void poison_scratch(Fx *f, uint8_t *mem, uint8_t seed) {
    for (int i = 0; i < DSP_SCRATCH_BYTES; i++) mem[f->layout.dsp_scratch_addr + i] = (uint8_t)(seed + 53 * i + 1);
}

static unsigned long g_cycles, g_instrs;

typedef struct { uint16_t v[K]; int idx[K]; } Result;

/* PeakExtract on whatever sits in Magnitude's tensor of `in`. Checks the
 * write footprint and the constant cycle count, and decodes the trace. */
static void run_peak(Fx *f, AvrInterp *in, Result *r) {
    uint8_t *before = malloc(AVR_INTERP_MEM_SIZE);
    memcpy(before, in->mem, AVR_INTERP_MEM_SIZE);
    unsigned long c0 = in->cycles, i0 = in->instructions;
    assert(avr_interp_run(in, &f->pk) == 0);
    unsigned long cyc = in->cycles - c0, ins = in->instructions - i0;
    assert(cyc == f->pk.cycles);
    if (g_cycles == 0) { g_cycles = cyc; g_instrs = ins; }
    assert(cyc == g_cycles && ins == g_instrs);          /* input-independent */
    for (uint32_t a = 0; a < AVR_INTERP_MEM_SIZE; a++) {
        if (in->mem[a] == before[a]) continue;
        int ok = (a >= f->pk_addr && a < (uint32_t)f->pk_addr + K * 2) ||
                 (a >= f->sel_addr && a < (uint32_t)f->sel_addr + N);
        if (!ok) { printf("    stray write at 0x%04X\n", a); assert(0); }
    }
    free(before);

    for (int p = 0; p < K; p++) {
        r->v[p] = (uint16_t)(in->mem[f->pk_addr + 2 * p] | (in->mem[f->pk_addr + 2 * p + 1] << 8));
        r->idx[p] = -1;
    }
    int marked = 0;
    for (int i = 0; i < N; i++) {
        uint8_t m = in->mem[f->sel_addr + i];
        if (m == 0) continue;
        assert(m >= 1 && m <= K);
        int pass = K - m;                /* marker K on the first pass, 1 on the last */
        assert(r->idx[pass] == -1);      /* one index per pass */
        r->idx[pass] = i;
        marked++;
    }
    assert(marked == K);                 /* K distinct indices, each chosen once */
}

static void write_mag(uint8_t *mem, uint16_t addr, const uint16_t *v) {
    for (int i = 0; i < N; i++) { mem[addr + 2 * i] = (uint8_t)(v[i] & 0xFF); mem[addr + 2 * i + 1] = (uint8_t)(v[i] >> 8); }
}

static int check_against_oracle(const Result *r, const uint16_t *v, const char *name) {
    uint16_t want_v[K]; int want_i[K];
    host_topk(v, want_v, want_i);
    int bad = 0;
    for (int p = 0; p < K; p++) {
        if (r->v[p] != want_v[p] || r->idx[p] != want_i[p]) {
            if (bad < 3) printf("    %s pass %d: got %u@%d want %u@%d\n", name, p, r->v[p], r->idx[p], want_v[p], want_i[p]);
            bad++;
        }
    }
    return bad;
}

/* Runs one magnitude array twice under different scratch poison and checks
 * both runs against the oracle and each other. */
static int run_vector(Fx *f, const uint16_t *v, const char *name, Result *out) {
    Result r[2];
    for (int t = 0; t < 2; t++) {
        AvrInterp *in = malloc(sizeof(AvrInterp));
        avr_interp_init(in);
        poison_scratch(f, in->mem, (uint8_t)(t ? 0x00 : 0xA7));  /* all-zero poison is the nasty one */
        if (t) memset(&in->mem[f->sel_addr], 0xFF, N);
        write_mag(in->mem, f->mag_addr, v);
        run_peak(f, in, &r[t]);
        free(in);
    }
    assert(memcmp(&r[0], &r[1], sizeof(Result)) == 0);       /* poison invariance */
    if (out) *out = r[0];
    return check_against_oracle(&r[0], v, name);
}

static void print_trace(const char *name, const Result *r) {
    printf("  %-34s idx", name);
    for (int p = 0; p < K; p++) printf(" %2d", r->idx[p]);
    printf("  val");
    for (int p = 0; p < K; p++) printf(" %u", r->v[p]);
    printf("\n");
}

/* ---------------------------------------------------------------- tests */

static uint32_t g_rng;
static uint16_t rnd(void) { g_rng = g_rng * 1664525u + 1013904223u; return (uint16_t)(g_rng >> 16); }

static void test_synthetic(Fx *f) {
    uint16_t v[N];
    int arrays = 0, bad = 0;
    Result r;
#define RUN(name) do { bad += run_vector(f, v, name, &r); arrays++; } while (0)

    memset(v, 0, sizeof(v)); RUN("all zeros");
    print_trace("all zeros", &r);
    for (int p = 0; p < K; p++) assert(r.idx[p] == p && r.v[p] == 0);   /* 0,1,...,7 */

    for (int i = 0; i < N; i++) v[i] = 1234; RUN("all equal 1234");
    print_trace("all equal 1234", &r);
    for (int p = 0; p < K; p++) assert(r.idx[p] == p);

    memset(v, 0, sizeof(v)); v[37] = 5; RUN("one nonzero (bin 37)");
    print_trace("one nonzero (bin 37)", &r);
    assert(r.idx[0] == 37 && r.v[0] == 5);
    for (int p = 1; p < K; p++) assert(r.idx[p] == p - 1 && r.v[p] == 0);  /* then 0..6 */

    memset(v, 0, sizeof(v)); v[60] = 9; v[3] = 700; v[41] = 9; RUN("three nonzero");
    print_trace("three nonzero", &r);

    memset(v, 0, sizeof(v));
    for (int j = 0; j < 8; j++) v[63 - 7 * j] = (uint16_t)(100 + j); RUN("exactly eight nonzero");
    memset(v, 0, sizeof(v));
    for (int j = 0; j < 20; j++) v[(j * 13) % N] = (uint16_t)(50 + (j % 7)); RUN("twenty nonzero, repeats");

    for (int i = 0; i < N; i++) v[i] = (uint16_t)(i * 3);
    v[10] = 1000; v[20] = 1000; v[50] = 1000; RUN("duplicate maxima 10/20/50");
    print_trace("duplicate maxima 10/20/50", &r);
    assert(r.idx[0] == 10 && r.idx[1] == 20 && r.idx[2] == 50);

    for (int i = 0; i < N; i++) v[i] = (uint16_t)((i % 4 == 0) ? 900 : (i % 4 == 1) ? 500 : 7);
    RUN("duplicate groups 900/500/7");
    print_trace("duplicate groups 900/500/7", &r);

    for (int i = 0; i < N; i++) v[i] = (uint16_t)(i * 700); RUN("strictly increasing");
    for (int i = 0; i < N; i++) v[i] = (uint16_t)(46340 - i * 700); RUN("strictly decreasing");
    for (int i = 0; i < N; i++) v[i] = (i & 1) ? 30000 : 12; RUN("alternating high/low");

    for (int i = 0; i < N; i++) v[i] = (uint16_t)(32760 + (i % 16));   /* straddles 32767/32768 */
    RUN("around 32767/32768");
    print_trace("around 32767/32768", &r);
    assert(r.v[0] == 32775 && r.v[K - 1] >= 32768);                   /* unsigned, not signed */

    for (int i = 0; i < N; i++) v[i] = (uint16_t)(i < 32 ? 32767 - i : 40000 + i);
    RUN("high unsigned > 32767");
    for (int i = 0; i < N; i++) v[i] = (uint16_t)((i * 37) % 46341);
    v[5] = 46340; v[44] = 46340; RUN("46340 twice");
    print_trace("46340 twice", &r);
    assert(r.v[0] == 46340 && r.idx[0] == 5 && r.idx[1] == 44);
    for (int i = 0; i < N; i++) v[i] = (i & 1) ? 0xFFFF : 0x8000; RUN("0xFFFF/0x8000 (full uint16)");

    static const uint32_t seeds[] = {1u, 42u, 0xC0FFEEu, 20260924u, 0xDEADBEEFu, 7u, 99991u, 0x2545F491u};
    for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); s++) {
        g_rng = seeds[s];
        for (int i = 0; i < N; i++) v[i] = (uint16_t)(rnd() % 46341u);
        RUN("random in 0..46340");
        g_rng = seeds[s] ^ 0x5A5A5A5Au;
        for (int i = 0; i < N; i++) v[i] = (uint16_t)(rnd() % 8u);          /* heavy ties */
        RUN("random 0..7, heavy ties");
    }
#undef RUN
    printf("  synthetic: %d arrays, %d outputs (value + index) compared, %d mismatched; "
           "each run twice under different scratch poison, identical\n", arrays, arrays * K, bad);
    assert(bad == 0);
}

/* FFT -> Magnitude -> PeakExtract in one interpreter, each op checked against
 * its host reference before the next runs, scratch re-poisoned in between. */
static void test_real_spectra(Fx *f) {
    const double pi = 3.14159265358979323846;
    Buf nat[24];
    const char *names[24];
    int nv = 0;
#define NEW(name) (memset(&nat[nv], 0, sizeof(Buf)), names[nv] = (name), &nat[nv++])
    Buf *b;
    b = NEW("impulse at 0"); b->re[0] = 32767;
    b = NEW("impulse at 1"); b->re[1] = 32767;
    b = NEW("DC"); for (int i = 0; i < N; i++) b->re[i] = 16384;
    b = NEW("tone k=1");  for (int i = 0; i < N; i++) b->re[i] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * i / N));
    b = NEW("tone k=5");  for (int i = 0; i < N; i++) b->re[i] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * 5 * i / N));
    b = NEW("tone k=23"); for (int i = 0; i < N; i++) b->re[i] = (int16_t)lround(0.9 * 32767 * sin(2 * pi * 23 * i / N));
    b = NEW("complex tone k=31");
    for (int i = 0; i < N; i++) {
        b->re[i] = (int16_t)lround(0.7 * 32767 * cos(2 * pi * 31 * i / N));
        b->im[i] = (int16_t)lround(0.7 * 32767 * sin(2 * pi * 31 * i / N));
    }
    b = NEW("two tones k=3,k=12");
    for (int i = 0; i < N; i++) b->re[i] = (int16_t)lround(0.5 * 32767 * cos(2 * pi * 3 * i / N) + 0.3 * 32767 * cos(2 * pi * 12 * i / N));
    b = NEW("deterministic mixed"); for (int i = 0; i < N; i++) { b->re[i] = (int16_t)(i * 1031 - 16384); b->im[i] = (int16_t)(-i * 617 + 8192); }
    b = NEW("alternating extremes"); for (int i = 0; i < N; i++) b->re[i] = (i & 1) ? INT16_MIN : INT16_MAX;
    b = NEW("alternating complex extremes");
    for (int i = 0; i < N; i++) { b->re[i] = (i & 1) ? INT16_MIN : INT16_MAX; b->im[i] = (i & 2) ? INT16_MAX : INT16_MIN; }
    b = NEW("negative-heavy"); for (int i = 0; i < N; i++) { b->re[i] = (int16_t)(-32768 + (i * 97) % 4000); b->im[i] = (int16_t)(-20000 - i * 150); }
    b = NEW("Q15 boundary pattern");
    { static const int16_t e[] = {0, 1, -1, INT16_MAX, INT16_MIN, 16384, -16384, 32766, -32767};
      for (int i = 0; i < N; i++) { b->re[i] = e[i % 9]; b->im[i] = e[(i + 4) % 9]; } }
    static const uint32_t seeds[] = {1u, 42u, 1234567u, 0xDEADBEEFu, 20260923u, 7u};
    for (int s = 0; s < 6; s++) {
        b = NEW(s < 3 ? "random real" : "random complex");
        g_rng = seeds[s];
        for (int i = 0; i < N; i++) { b->re[i] = (int16_t)rnd(); b->im[i] = s >= 3 ? (int16_t)rnd() : 0; }
    }
#undef NEW

    int bad = 0;
    for (int k = 0; k < nv; k++) {
        Buf br, host_fft_out, got;
        for (int i = 0; i < N; i++) { br.re[brev6(i)] = nat[k].re[i]; br.im[brev6(i)] = nat[k].im[i]; }
        host_fft(&nat[k], &host_fft_out);

        Candidate fft;
        assert(lower_fft_stages_test_hook(&f->graph, 0, STAGES - 1, &f->layout, &g_cm, &fft) == 0);
        AvrInterp *in = malloc(sizeof(AvrInterp));
        avr_interp_init(in);
        for (int i = 0; i < N; i++) {
            in->mem[f->bitrev_addr + 4 * i + 0] = (uint8_t)((uint16_t)br.re[i] & 0xFF);
            in->mem[f->bitrev_addr + 4 * i + 1] = (uint8_t)((uint16_t)br.re[i] >> 8);
            in->mem[f->bitrev_addr + 4 * i + 2] = (uint8_t)((uint16_t)br.im[i] & 0xFF);
            in->mem[f->bitrev_addr + 4 * i + 3] = (uint8_t)((uint16_t)br.im[i] >> 8);
        }
        assert(avr_interp_run(in, &fft) == 0);
        candidate_free(&fft);
        for (int i = 0; i < N; i++) {
            got.re[i] = (int16_t)(in->mem[f->fft5_addr + 4 * i] | (in->mem[f->fft5_addr + 4 * i + 1] << 8));
            got.im[i] = (int16_t)(in->mem[f->fft5_addr + 4 * i + 2] | (in->mem[f->fft5_addr + 4 * i + 3] << 8));
        }
        assert(memcmp(&got, &host_fft_out, sizeof(Buf)) == 0);          /* AVR FFT == host FFT */

        poison_scratch(f, in->mem, (uint8_t)(0x31 + k));
        assert(avr_interp_run(in, &f->mag) == 0);
        uint16_t mag[N];
        for (int i = 0; i < N; i++) {
            mag[i] = (uint16_t)(in->mem[f->mag_addr + 2 * i] | (in->mem[f->mag_addr + 2 * i + 1] << 8));
            assert(mag[i] == host_mag(host_fft_out.re[i], host_fft_out.im[i]));  /* AVR Magnitude == host */
        }

        poison_scratch(f, in->mem, (uint8_t)(0x77 + k));
        Result r;
        run_peak(f, in, &r);
        int e = check_against_oracle(&r, mag, names[k]);                 /* AVR PeakExtract == host */
        bad += e;
        print_trace(names[k], &r);
        free(in);
    }
    printf("  real spectra: %d vectors, FFT (64) + Magnitude (64) + PeakExtract (8 values + 8 indices) "
           "exact at every stage, %d mismatched\n", nv, bad);
    assert(bad == 0);
}

static void test_structure(Fx *f) {
    int written[32] = {0};
    size_t bytes = 0, emitted = 0;
    int loops_r3 = 0, loops_pass = 0;
    for (size_t i = 0; i < f->pk.num_instructions; i++) {
        const AvrInstr *ins = &f->pk.instructions[i];
        bytes += avr_instr_flash_bytes(ins);
        if (avr_instr_is_label(ins)) continue;
        emitted++;
        const char *m = ins->mnemonic;
        assert(strcmp(m, "mul") && strcmp(m, "muls") && strcmp(m, "mulsu"));
        if (!strcmp(m, "dec")) { loops_r3 += !strcmp(ins->operands[0], "r3"); loops_pass += !strcmp(ins->operands[0], "r8"); }
        if (!strcmp(m, "st")) { int p = ins->operands[0][0] == 'X' ? 26 : 28; if (ins->operands[0][1] == '+') written[p] = written[p + 1] = 1; continue; }
        if (!strcmp(m, "ld")) { int p = ins->operands[1][0] == 'X' ? 26 : 30; written[p] = written[p + 1] = 1; }
        if (!strcmp(m, "cp") || !strcmp(m, "cpc") || !strcmp(m, "brne") || !strcmp(m, "breq") || !strcmp(m, "rjmp")) continue;
        if (ins->operands[0][0] != 'r') continue;
        written[atoi(ins->operands[0] + 1)] = 1;
    }
    printf("  registers written:");
    for (int r = 0; r < 32; r++) if (written[r]) printf(" r%d", r);
    printf("\n");
    const int allowed[] = {REG_DSP_LOOP_COUNTER, REG_PK_BEST_LO, REG_PK_BEST_HI, REG_PK_BEST_IDX, REG_PK_FOUND,
                           REG_PK_PASS, REG_PK_V_LO, REG_PK_V_HI, REG_PK_SEL, REG_PK_ELIGIBLE, REG_PK_IDX,
                           REG_PK_BETTER, REG_PK_TMP, REG_PK_ONES, REG_SCRATCH0, 26, 27, 28, 29, 30, 31};
    for (int r = 0; r < 32; r++) {
        int ok = 0;
        for (size_t j = 0; j < sizeof(allowed) / sizeof(allowed[0]); j++) ok |= allowed[j] == r;
        if (written[r] && !ok) { printf("  undeclared write r%d\n", r); assert(0); }
    }
    assert(!written[REG_ZERO] && !written[0] && !written[1]);
    assert(loops_r3 == 2 && loops_pass == 1);    /* clear + scan on r3, passes on r8 */
    printf("  loops: selected[] clear x%d (r3), passes x%d (r8) around scan x%d (r3)\n", N, K, N);
    printf("  code: %zu emitted instructions, %zu bytes; %lu executed, %u cycles predicted, %lu interpreted\n",
           emitted, bytes, g_instrs, f->pk.cycles, g_cycles);
}

/* ----------------------------------------------------------- fixtures */

static Candidate price_buf(InstrBuf *b) { Candidate c; assert(instrbuf_price(b, &g_cm, &c) == 0); return c; }

static Candidate harness_bytes(const uint8_t *image, uint16_t addr, size_t n) {
    InstrBuf h; instrbuf_init(&h);
    char r2[AVR_OPERAND_LEN], r24[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r2, REG_ZERO); fmt_reg(r24, REG_SCRATCH0);
    ins1(&h, "clr", r2);
    for (size_t i = 0; i < n; i++) {
        fmt_imm(imm, image[i]); ins2(&h, "ldi", r24, imm);
        fmt_addr(a, (uint16_t)(addr + i)); ins2(&h, "sts", a, r24);
    }
    return price_buf(&h);
}

static void emit_parts(const char *path, Candidate *parts, const char **names, size_t n) {
    FILE *out = fopen(path, "w");
    assert(out);
    EmitUnit unit; emit_unit_init(&unit);
    emit_program_prologue(out);
    for (size_t i = 0; i < n; i++) {
        fprintf(out, "\n    ; ---- %s ----\n", names[i]);
        assert(emit_candidate(&unit, &parts[i], out) == 0);
    }
    fclose(out);
}

static void emit_standalone(Fx *f, const char *path) {
    uint16_t v[N];
    for (int i = 0; i < N; i++) v[i] = (uint16_t)((i * 7919u) % 46341u);
    v[9] = 46340; v[30] = 46340; v[31] = 32768; v[32] = 32767;
    uint8_t image[N * 2];
    for (int i = 0; i < N; i++) { image[2 * i] = (uint8_t)(v[i] & 0xFF); image[2 * i + 1] = (uint8_t)(v[i] >> 8); }
    Candidate h = harness_bytes(image, f->mag_addr, sizeof(image));
    InstrBuf t; instrbuf_init(&t); ins0(&t, "break");
    Candidate brk = price_buf(&t);

    AvrInterp *in = malloc(sizeof(AvrInterp));
    avr_interp_init(in);
    assert(avr_interp_run(in, &h) == 0);
    unsigned long hc = in->cycles;
    Result r;
    run_peak(f, in, &r);
    assert(check_against_oracle(&r, v, "fixture") == 0);
    assert(avr_interp_run(in, &brk) == 0);
    unsigned long total = in->cycles;
    free(in);

    Candidate parts[3] = {h, f->pk, brk};
    const char *names[3] = {"harness: clr r2, Magnitude tensor", "OP_PEAK_EXTRACT", "break"};
    emit_parts(path, parts, names, 3);
    printf("fixture %s: harness %u + peak %u + break %u = %u predicted; interpreter %lu\n", path,
           h.cycles, f->pk.cycles, brk.cycles, h.cycles + f->pk.cycles + brk.cycles, total);
    assert(total == h.cycles + f->pk.cycles + brk.cycles);
    candidate_free(&h);
    candidate_free(&brk);
}

/* FFT x6 + Magnitude + PeakExtract from a bit-reversed two-tone input, every
 * op through lower_op, then break and the twiddle table. The interpreter runs
 * each candidate against the one memory image; the FFT stages come from the
 * stages hook, whose one-candidate program carries its own copy of the table
 * so lpm resolves, and whose cycles equal the six lower_op stages plus break. */
static void emit_integrated(Fx *f, const char *path) {
    const double pi = 3.14159265358979323846;
    Buf nat, br, host;
    memset(&nat, 0, sizeof(nat));
    for (int i = 0; i < N; i++) nat.re[i] = (int16_t)lround(0.5 * 32767 * cos(2 * pi * 3 * i / N) + 0.3 * 32767 * cos(2 * pi * 12 * i / N));
    for (int i = 0; i < N; i++) { br.re[brev6(i)] = nat.re[i]; br.im[brev6(i)] = nat.im[i]; }
    host_fft(&nat, &host);
    uint8_t image[N * 4];
    for (int i = 0; i < N; i++) {
        image[4 * i + 0] = (uint8_t)((uint16_t)br.re[i] & 0xFF); image[4 * i + 1] = (uint8_t)((uint16_t)br.re[i] >> 8);
        image[4 * i + 2] = (uint8_t)((uint16_t)br.im[i] & 0xFF); image[4 * i + 3] = (uint8_t)((uint16_t)br.im[i] >> 8);
    }
    Candidate parts[12];
    const char *names[12];
    size_t np = 0;
    parts[np] = harness_bytes(image, f->bitrev_addr, sizeof(image)); names[np++] = "harness: clr r2, bit-reversed input";
    uint32_t op_cycles = 0;
    for (size_t i = 0; i < f->graph.count; i++) {
        OpKind k = f->graph.ops[i].kind;
        if (k != OP_FFT_BUTTERFLY && k != OP_MAGNITUDE && k != OP_PEAK_EXTRACT) continue;
        assert(lower_op(&f->graph, i, &f->layout, &f->ra, &g_cm, NULL, 0, &parts[np]) == 0);
        names[np] = k == OP_FFT_BUTTERFLY ? "fft stage" : k == OP_MAGNITUDE ? "magnitude" : "peak extract";
        op_cycles += parts[np].cycles;
        np++;
    }
    InstrBuf t; instrbuf_init(&t); ins0(&t, "break"); dsp_emit_twiddle_table(&t);
    parts[np] = price_buf(&t); names[np++] = "break + twiddle table";
    uint32_t predicted = 0;
    for (size_t i = 0; i < np; i++) predicted += parts[i].cycles;

    /* Interpreter: harness, FFT (stages hook, includes its break), Magnitude, PeakExtract. */
    Candidate fft_hook;
    assert(lower_fft_stages_test_hook(&f->graph, 0, STAGES - 1, &f->layout, &g_cm, &fft_hook) == 0);
    AvrInterp *in = malloc(sizeof(AvrInterp));
    avr_interp_init(in);
    assert(avr_interp_run(in, &parts[0]) == 0);
    assert(avr_interp_run(in, &fft_hook) == 0);
    assert(avr_interp_run(in, &f->mag) == 0);
    uint16_t mag[N];
    for (int i = 0; i < N; i++) {
        mag[i] = (uint16_t)(in->mem[f->mag_addr + 2 * i] | (in->mem[f->mag_addr + 2 * i + 1] << 8));
        assert(mag[i] == host_mag(host.re[i], host.im[i]));
    }
    Result r;
    run_peak(f, in, &r);
    assert(check_against_oracle(&r, mag, "integrated") == 0);
    unsigned long interp = in->cycles;   /* fft_hook's break stands in for the unit's final break */
    free(in);
    candidate_free(&fft_hook);
    print_trace("integrated two-tone", &r);

    emit_parts(path, parts, names, np);
    printf("integrated %s: harness %u + ops %u (FFT x6 + Magnitude + PeakExtract) + break 1 = %u predicted; "
           "interpreter %lu\n", path, parts[0].cycles, op_cycles, predicted, interp);
    assert(interp == predicted);
    for (size_t i = 0; i < np; i++) candidate_free(&parts[i]);
}

int main(int argc, char **argv) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
    Fx f; fx_init(&f);
    test_synthetic(&f);
    test_real_spectra(&f);
    test_structure(&f);
    if (argc == 3 && !strcmp(argv[1], "--emit-fixture")) emit_standalone(&f, argv[2]);
    if (argc == 3 && !strcmp(argv[1], "--emit-integrated")) emit_integrated(&f, argv[2]);
    fx_free(&f);
    printf("test_dsp_peak: all tests passed\n");
    return 0;
}
