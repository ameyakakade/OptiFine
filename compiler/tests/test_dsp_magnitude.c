/* OP_MAGNITUDE: exact floor(sqrt(re^2 + im^2)) over all 64 bins.
 *
 * The oracle widens BEFORE multiplying (int64 re * re, never int16 * int16)
 * and takes an integer square root checked by 64-bit bracketing. Every
 * comparison is exact over all 64 outputs; there is no tolerance.
 *
 * Two kinds of input: synthetic complex buffers written straight into the
 * FFT's output tensor (covering the full int16 domain, including the 46340
 * maximum), and the real output of the AVR six-stage FFT, run first in the
 * same interpreter with the scratch arena poisoned in between -- Magnitude
 * must not depend on anything the FFT left in scratch.
 *
 *   test_dsp_magnitude --emit-fixture <out.s>   Avrora fixture: harness that
 *   writes a complex input buffer, then OP_MAGNITUDE, then break. */
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
#define STAGES DSP_FFT_LOG2

static CostModel g_cm;

/* ------------------------------------------------------------- oracles */

static uint16_t host_mag(int16_t re, int16_t im) {
    int64_t sum = (int64_t)re * (int64_t)re + (int64_t)im * (int64_t)im;
    int64_t r = (int64_t)sqrt((double)sum);
    while (r * r > sum) r--;
    while ((r + 1) * (r + 1) <= sum) r++;
    assert(r >= 0 && r <= 65535);
    return (uint16_t)r;
}

typedef struct { int16_t re[N], im[N]; } Buf;

static int16_t h_qmul(int16_t x, int16_t y) { return (int16_t)(((int32_t)x * (int32_t)y) >> 15); }
static int16_t h_asr(int16_t v)             { return (int16_t)(v >> 1); }
static int16_t h_add(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a + (uint16_t)b); }
static int16_t h_sub(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a - (uint16_t)b); }

/* Host FFT, textbook indexing, same Q15 rules as test_dsp_fft's oracle. */
static void host_fft(const Buf *natural, Buf *out) {
    Buf a, b;
    for (int i = 0; i < N; i++) {
        int r = 0, x = i;
        for (int k = 0; k < STAGES; k++) { r = (r << 1) | (x & 1); x >>= 1; }
        a.re[r] = natural->re[i];
        a.im[r] = natural->im[i];
    }
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

/* ------------------------------------------------------------- fixture */

typedef struct {
    IrGraph graph;
    SramLayout layout;
    RegAllocResult ra;
    size_t window_op, coeff_op, bitrev_op, fft5_op, mag_op;
    uint16_t bitrev_addr, fft5_addr, mag_addr;
    Candidate mag;          /* lower_op(OP_MAGNITUDE), the real path */
} Fx;

static void fx_init(Fx *f) {
    assert(dsp_build_pipeline(&f->graph) == 0);
    assert(sram_layout_build(&f->graph, &f->layout) == 0);
    assert(regalloc_next_use(&f->graph, &f->ra) == 0);
    f->window_op = f->coeff_op = f->bitrev_op = f->fft5_op = f->mag_op = (size_t)-1;
    for (size_t i = 0; i < f->graph.count; i++) {
        const IrOp *op = &f->graph.ops[i];
        if (op->kind == OP_WINDOW) { f->window_op = i; f->coeff_op = op->inputs[1]; }
        if (op->kind == OP_BIT_REVERSE) f->bitrev_op = i;
        if (op->kind == OP_FFT_BUTTERFLY) f->fft5_op = i;
        if (op->kind == OP_MAGNITUDE) f->mag_op = i;
    }
    const IrOp *mag = &f->graph.ops[f->mag_op];
    /* The semantic facts this test relies on, read from the IR, not assumed. */
    assert(mag->inputs[0] == f->fft5_op);
    assert(f->graph.ops[f->fft5_op].dtype == DT_COMPLEX_Q15);
    assert(mag->dtype == DT_FIXED_Q15);
    assert(sram_layout_num_elements(mag) == N && mag->output_shape_len == 1);
    assert(sram_layout_elem_size(mag->dtype) == 2);
    f->bitrev_addr = sram_layout_addr(&f->layout, &f->graph, f->bitrev_op, 0);
    f->fft5_addr = sram_layout_addr(&f->layout, &f->graph, f->fft5_op, 0);
    f->mag_addr = sram_layout_addr(&f->layout, &f->graph, f->mag_op, 0);
    assert(lower_op(&f->graph, f->mag_op, &f->layout, &f->ra, &g_cm, NULL, 0, &f->mag) == 0);
}
static void fx_free(Fx *f) {
    candidate_free(&f->mag);
    regalloc_result_free(&f->ra);
    sram_layout_free(&f->layout);
    ir_graph_free(&f->graph);
}

static void store_buf(uint8_t *mem, uint16_t addr, const Buf *b) {
    for (int i = 0; i < N; i++) {
        mem[addr + i * 4 + 0] = (uint8_t)((uint16_t)b->re[i] & 0xFF);
        mem[addr + i * 4 + 1] = (uint8_t)((uint16_t)b->re[i] >> 8);
        mem[addr + i * 4 + 2] = (uint8_t)((uint16_t)b->im[i] & 0xFF);
        mem[addr + i * 4 + 3] = (uint8_t)((uint16_t)b->im[i] >> 8);
    }
}
static void load_buf(const uint8_t *mem, uint16_t addr, Buf *b) {
    for (int i = 0; i < N; i++) {
        b->re[i] = (int16_t)((uint16_t)mem[addr + i * 4] | ((uint16_t)mem[addr + i * 4 + 1] << 8));
        b->im[i] = (int16_t)((uint16_t)mem[addr + i * 4 + 2] | ((uint16_t)mem[addr + i * 4 + 3] << 8));
    }
}
static void poison_scratch(Fx *f, uint8_t *mem, uint8_t seed) {
    for (int i = 0; i < DSP_SCRATCH_BYTES; i++) mem[f->layout.dsp_scratch_addr + i] = (uint8_t)(seed + 97 * i);
}

static unsigned long g_mag_cycles, g_mag_instrs;

/* Runs Magnitude on whatever is in the FFT output tensor of `in`, checks the
 * 64 outputs against the oracle, the write footprint, and the cycle count. */
static int run_and_check(Fx *f, AvrInterp *in, const char *name, uint16_t *max_out) {
    Buf x;
    load_buf(in->mem, f->fft5_addr, &x);
    uint8_t *before = malloc(AVR_INTERP_MEM_SIZE);
    memcpy(before, in->mem, AVR_INTERP_MEM_SIZE);
    unsigned long c0 = in->cycles, i0 = in->instructions;
    assert(avr_interp_run(in, &f->mag) == 0);
    unsigned long cyc = in->cycles - c0, ins = in->instructions - i0;
    assert(cyc == f->mag.cycles);
    if (g_mag_cycles == 0) { g_mag_cycles = cyc; g_mag_instrs = ins; }
    assert(cyc == g_mag_cycles && ins == g_mag_instrs);   /* data-independent */

    for (uint32_t a = 0; a < AVR_INTERP_MEM_SIZE; a++) {
        if (in->mem[a] == before[a]) continue;
        int ok = (a >= f->mag_addr && a < (uint32_t)f->mag_addr + N * 2) ||
                 a == (uint32_t)f->layout.dsp_scratch_addr + DSP_SCRATCH_MAG_COUNT;
        if (!ok) { printf("    %s: stray write at 0x%04X\n", name, a); assert(0); }
    }
    free(before);

    int bad = 0;
    for (int i = 0; i < N; i++) {
        uint16_t got = (uint16_t)(in->mem[f->mag_addr + 2 * i] | (in->mem[f->mag_addr + 2 * i + 1] << 8));
        uint16_t want = host_mag(x.re[i], x.im[i]);
        if (got != want) {
            if (bad < 4) printf("    %s MISMATCH bin %d (%d,%d): got %u want %u\n", name, i,
                                x.re[i], x.im[i], got, want);
            bad++;
        }
        if (max_out && got > *max_out) *max_out = got;
    }
    return bad;
}

/* --------------------------------------------------------------- tests */

static uint32_t g_rng;
static int16_t rnd16(void) { g_rng = g_rng * 1664525u + 1013904223u; return (int16_t)(g_rng >> 16); }

static void test_synthetic_vectors(Fx *f) {
    static const int16_t edge[] = {0, 1, -1, 2, -2, 3, 4, 5, 127, -128, 255, 256, -256,
                                   16384, -16384, 23170, -23170, 23171, 32766, -32767, 32767, -32768};
    const int ne = (int)(sizeof(edge) / sizeof(edge[0]));
    Buf v[16];
    const char *names[16];
    int nv = 0;

    memset(&v[nv], 0, sizeof(Buf)); names[nv++] = "all zero";
    for (int i = 0; i < N; i++) { v[nv].re[i] = edge[i % ne]; v[nv].im[i] = 0; }
    names[nv++] = "pure real (edge values)";
    for (int i = 0; i < N; i++) { v[nv].re[i] = 0; v[nv].im[i] = edge[i % ne]; }
    names[nv++] = "pure imaginary (edge values)";
    for (int i = 0; i < N; i++) { v[nv].re[i] = (i & 1) ? 1 : -1; v[nv].im[i] = (i & 2) ? 1 : -1; }
    names[nv++] = "+/-1";
    for (int i = 0; i < N; i++) {
        static const int16_t ext[] = {INT16_MAX, INT16_MIN, 0, -1};
        v[nv].re[i] = ext[i % 4]; v[nv].im[i] = ext[(i / 4) % 4];
    }
    names[nv++] = "INT16_MAX / INT16_MIN pairs (incl. 46340)";
    for (int i = 0; i < N; i++) { int16_t d = (int16_t)(i * 1024 - 32768); v[nv].re[i] = d; v[nv].im[i] = (i & 1) ? d : (int16_t)-d; }
    names[nv++] = "diagonals re = +/-im";
    for (int i = 0; i < N; i++) {                  /* 3-4-5, 5-12-13, 8-15-17, 7-24-25 scaled */
        static const int16_t trip[][2] = {{3, 4}, {5, 12}, {8, 15}, {7, 24}, {20, 21}, {9, 40}};
        int t = i % 6, k = 1 + (i * 37) % 800;
        v[nv].re[i] = (int16_t)(trip[t][0] * k * ((i & 1) ? -1 : 1));
        v[nv].im[i] = (int16_t)(trip[t][1] * k);
    }
    names[nv++] = "perfect-square sums";
    for (int i = 0; i < N; i++) {                  /* a^2 + b^2 = m^2 - 1 and m^2 + 1 */
        int m = 100 + i * 511;                     /* up to ~32300 */
        int16_t a = (int16_t)(m - 1);              /* (m-1)^2 + b^2 vs m^2: b^2 = 2m - 1 +/- 1 */
        int bsq = 2 * m - 1 + ((i & 1) ? 1 : -1);
        int16_t b = (int16_t)lround(sqrt((double)bsq));
        v[nv].re[i] = a; v[nv].im[i] = (i & 2) ? b : (int16_t)-b;
    }
    names[nv++] = "sums around square boundaries";
    for (int s = 0; s < 4; s++) {
        static const uint32_t seeds[] = {1u, 0xC0FFEEu, 20260923u, 0x9E3779B9u};
        g_rng = seeds[s];
        for (int i = 0; i < N; i++) { v[nv].re[i] = rnd16(); v[nv].im[i] = rnd16(); }
        names[nv++] = s == 0 ? "random seed 1" : s == 1 ? "random seed 0xC0FFEE"
                    : s == 2 ? "random seed 20260923" : "random seed 0x9E3779B9";
    }

    uint16_t max = 0;
    int bad = 0;
    for (int k = 0; k < nv; k++) {
        AvrInterp *in = malloc(sizeof(AvrInterp));
        avr_interp_init(in);
        poison_scratch(f, in->mem, (uint8_t)k);
        store_buf(in->mem, f->fft5_addr, &v[k]);
        int b = run_and_check(f, in, names[k], &max);
        printf("  %-42s 64/64 exact\n", names[k]);
        bad += b;
        free(in);
    }
    printf("  synthetic: %d vectors, %d outputs compared, %d mismatched; largest magnitude %u\n",
           nv, nv * N, bad, max);
    assert(bad == 0);
    assert(max == 46340); /* re = im = -32768 reaches the op's full range */
}

/* The FFT output this op really consumes. The FFT runs from the bit-reversed
 * buffer first, in the same interpreter; its result is checked against the
 * host FFT, the scratch arena is then poisoned, and Magnitude runs on the
 * FFT's tensor as it stands. */
static void test_on_real_fft_output(Fx *f) {
    const double pi = 3.14159265358979323846;
    Buf nat[24];
    const char *names[24];
    int nv = 0;
    memset(&nat[nv], 0, sizeof(Buf)); names[nv++] = "zero";
    memset(&nat[nv], 0, sizeof(Buf)); nat[nv].re[0] = 32767; names[nv++] = "impulse at 0";
    memset(&nat[nv], 0, sizeof(Buf)); nat[nv].re[1] = 32767; names[nv++] = "impulse at 1";
    memset(&nat[nv], 0, sizeof(Buf)); for (int i = 0; i < N; i++) nat[nv].re[i] = 32767; names[nv++] = "DC max";
    memset(&nat[nv], 0, sizeof(Buf)); for (int i = 0; i < N; i++) nat[nv].re[i] = INT16_MIN; names[nv++] = "DC min";
    memset(&nat[nv], 0, sizeof(Buf));
    for (int i = 0; i < N; i++) nat[nv].re[i] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * i / N));
    names[nv++] = "tone k=1";
    memset(&nat[nv], 0, sizeof(Buf));
    for (int i = 0; i < N; i++) nat[nv].re[i] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * 5 * i / N));
    names[nv++] = "tone k=5";
    memset(&nat[nv], 0, sizeof(Buf));
    for (int i = 0; i < N; i++) nat[nv].re[i] = (int16_t)lround(0.9 * 32767 * sin(2 * pi * 23 * i / N));
    names[nv++] = "tone k=23";
    for (int i = 0; i < N; i++) {
        nat[nv].re[i] = (int16_t)lround(0.7 * 32767 * cos(2 * pi * 31 * i / N));
        nat[nv].im[i] = (int16_t)lround(0.7 * 32767 * sin(2 * pi * 31 * i / N));
    }
    names[nv++] = "complex tone k=31";
    for (int i = 0; i < N; i++) { nat[nv].re[i] = (i & 1) ? INT16_MIN : INT16_MAX; nat[nv].im[i] = 0; }
    names[nv++] = "alternating extremes";
    for (int i = 0; i < N; i++) { nat[nv].re[i] = (i & 1) ? INT16_MIN : INT16_MAX; nat[nv].im[i] = (i & 2) ? INT16_MAX : INT16_MIN; }
    names[nv++] = "alternating complex extremes";
    for (int i = 0; i < N; i++) { nat[nv].re[i] = (int16_t)(i * 1031 - 16384); nat[nv].im[i] = (int16_t)(-i * 617 + 8192); }
    names[nv++] = "deterministic mixed";
    for (int i = 0; i < N; i++) { nat[nv].re[i] = (int16_t)(-32768 + (i * 97) % 4000); nat[nv].im[i] = (int16_t)(-20000 - i * 150); }
    names[nv++] = "negative-heavy";
    static const uint32_t seeds[] = {1u, 42u, 1234567u, 0xDEADBEEFu, 20260923u, 7u};
    static char rn[6][32];
    for (int s = 0; s < 6; s++) {
        g_rng = seeds[s];
        for (int i = 0; i < N; i++) { nat[nv].re[i] = rnd16(); nat[nv].im[i] = (s & 1) ? rnd16() : 0; }
        snprintf(rn[s], sizeof(rn[s]), "random seed 0x%08X", (unsigned)seeds[s]);
        names[nv++] = rn[s];
    }

    uint16_t max = 0;
    int bad = 0;
    for (int k = 0; k < nv; k++) {
        Buf br, host;
        for (int i = 0; i < N; i++) {
            int r = 0, x = i;
            for (int b = 0; b < STAGES; b++) { r = (r << 1) | (x & 1); x >>= 1; }
            br.re[r] = nat[k].re[i]; br.im[r] = nat[k].im[i];
        }
        host_fft(&nat[k], &host);

        Candidate fft;
        assert(lower_fft_stages_test_hook(&f->graph, 0, STAGES - 1, &f->layout, &g_cm, &fft) == 0);
        AvrInterp *in = malloc(sizeof(AvrInterp));
        avr_interp_init(in);
        store_buf(in->mem, f->bitrev_addr, &br);
        assert(avr_interp_run(in, &fft) == 0);
        Buf got;
        load_buf(in->mem, f->fft5_addr, &got);
        assert(memcmp(&got, &host, sizeof(Buf)) == 0);     /* the FFT itself, exact */
        candidate_free(&fft);

        poison_scratch(f, in->mem, (uint8_t)(0x5A + k));   /* nothing may survive the FFT */
        int b = run_and_check(f, in, names[k], &max);
        printf("  FFT -> Magnitude  %-30s 64/64 exact\n", names[k]);
        bad += b;
        free(in);
    }
    printf("  FFT-fed: %d vectors, %d outputs compared, %d mismatched; largest magnitude %u\n",
           nv, nv * N, bad, max);
    assert(bad == 0);
}

/* The in-pipeline bound, derived from the real window rather than asserted.
 * |window(s)[n]| <= c[n] exactly (floor(s*c/32768) with |s| <= 32768), each
 * final bin of the ideal 1/64-scaled FFT is at most sum|x|/64, and fixed-point
 * rounding adds under 2.5 per stage with no amplification (each stage's
 * ell-infinity gain is <= 1 since |W| < 1), so under 15 over six stages. */
static void test_pipeline_bound(Fx *f) {
    const int16_t *c = (const int16_t *)f->graph.ops[f->coeff_op].data;
    int64_t sum_c = 0;
    for (int i = 0; i < N; i++) { assert(c[i] > 0); sum_c += c[i]; }
    for (int k = 0; k < 32; k++) {           /* twiddle modulus < 1, as the gain argument needs */
        int16_t wr, wi; dsp_twiddle_q15(k, &wr, &wi);
        assert((int64_t)wr * wr + (int64_t)wi * wi < (int64_t)32768 * 32768);
    }
    double bound = (double)sum_c / 64.0 + 15.0;
    printf("  bound: sum(c) = %lld, sum(c)/64 + 15 = %.1f < 32768\n", (long long)sum_c, bound);
    assert(bound < 32768.0);

    /* Adversarial inputs through the host window + FFT: all-max, all-min,
     * and for every bin the sign pattern that aligns the samples with it. */
    const double pi = 3.14159265358979323846;
    uint16_t worst = 0;
    for (int trial = 0; trial < 2 + 2 * N; trial++) {
        Buf s, out;
        memset(&s, 0, sizeof(s));
        for (int n = 0; n < N; n++) {
            int16_t v;
            if (trial == 0) v = INT16_MAX;
            else if (trial == 1) v = INT16_MIN;
            else {
                int k = (trial - 2) / 2;
                double ph = (trial & 1) ? sin(2 * pi * k * n / N) : cos(2 * pi * k * n / N);
                v = ph >= 0 ? INT16_MAX : INT16_MIN;
            }
            s.re[n] = h_qmul(v, c[n]);       /* OP_WINDOW's Q15 multiply */
        }
        host_fft(&s, &out);
        for (int k = 0; k < N; k++) {
            uint16_t m = host_mag(out.re[k], out.im[k]);
            if (m > worst) worst = m;
        }
    }
    printf("  adversarial windowed inputs: largest magnitude %u (bound %.1f)\n", worst, bound);
    assert(worst <= bound);
}

static size_t emitted(const Candidate *c) {
    size_t n = 0;
    for (size_t i = 0; i < c->num_instructions; i++) if (!avr_instr_is_label(&c->instructions[i])) n++;
    return n;
}

static void test_structure(Fx *f) {
    /* Registers: exactly the documented contract. r2 and pointers Z untouched. */
    int written[32] = {0};
    size_t bytes = 0;
    for (size_t i = 0; i < f->mag.num_instructions; i++) {
        const AvrInstr *ins = &f->mag.instructions[i];
        bytes += avr_instr_flash_bytes(ins);
        const char *m = ins->mnemonic;
        if (avr_instr_is_label(ins)) continue;
        if (!strcmp(m, "mul") || !strcmp(m, "muls") || !strcmp(m, "mulsu")) { written[0] = written[1] = 1; continue; }
        if (!strcmp(m, "st")) { written[28] = written[29] = 1; continue; }
        if (!strcmp(m, "ld")) written[26] = written[27] = 1;
        if (!strcmp(m, "sts") || !strcmp(m, "cp") || !strcmp(m, "cpc") || !strcmp(m, "brne") ||
            !strcmp(m, "breq") || !strcmp(m, "rjmp")) continue;
        if (ins->operands[0][0] != 'r') continue;
        int r = atoi(ins->operands[0] + 1);
        written[r] = 1;
        if (!strcmp(m, "movw")) written[r + 1] = 1;
    }
    printf("  registers written:");
    for (int r = 0; r < 32; r++) if (written[r]) printf(" r%d", r);
    printf("\n");
    assert(!written[REG_ZERO] && !written[30] && !written[31] && !written[2]);
    for (int r = 20; r <= 22; r++) assert(!written[r]);

    /* Loops: the 64-trip bin loop (SRAM counter, long form) around isqrt's
     * 16-trip loop (r3, short form). */
    char cell[AVR_OPERAND_LEN];
    fmt_addr(cell, (uint16_t)(f->layout.dsp_scratch_addr + DSP_SCRATCH_MAG_COUNT));
    int rjmp = 0, brne = 0, cell_sts = 0, dec_r3 = 0;
    for (size_t i = 0; i < f->mag.num_instructions; i++) {
        const AvrInstr *ins = &f->mag.instructions[i];
        rjmp += !strcmp(ins->mnemonic, "rjmp");
        brne += !strcmp(ins->mnemonic, "brne");
        cell_sts += !strcmp(ins->mnemonic, "sts") && !strcmp(ins->operands[0], cell);
        dec_r3 += !strcmp(ins->mnemonic, "dec") && !strcmp(ins->operands[0], "r3");
    }
    assert(rjmp == 1 && brne == 1 && cell_sts == 2 && dec_r3 == 1);
    printf("  loops: bins x64 (SRAM counter at scratch+%d, breq/rjmp) around isqrt x16 (r3, brne)\n",
           DSP_SCRATCH_MAG_COUNT);
    printf("  code: %zu emitted instructions, %zu bytes; %lu executed, %u cycles predicted, %lu interpreted\n",
           emitted(&f->mag), bytes, g_mag_instrs, f->mag.cycles, g_mag_cycles);
}

/* ------------------------------------------------------------ fixture */

static void emit_fixture(Fx *f, const char *path) {
    /* Input: the extremes buffer, so the fixture exercises the 46340 case. */
    Buf x;
    for (int i = 0; i < N; i++) {
        static const int16_t ext[] = {INT16_MAX, INT16_MIN, 0, -1, 12345, -23456, 1, 30000};
        x.re[i] = ext[i % 8]; x.im[i] = ext[(i / 8) % 8];
    }
    static uint8_t image[AVR_INTERP_MEM_SIZE];
    store_buf(image, f->fft5_addr, &x);

    InstrBuf h; instrbuf_init(&h);
    char r2[AVR_OPERAND_LEN], r24[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r2, REG_ZERO); fmt_reg(r24, REG_SCRATCH0);
    ins1(&h, "clr", r2);
    for (int i = 0; i < N * 4; i++) {
        fmt_imm(imm, image[f->fft5_addr + i]); ins2(&h, "ldi", r24, imm);
        fmt_addr(a, (uint16_t)(f->fft5_addr + i)); ins2(&h, "sts", a, r24);
    }
    Candidate harness; assert(instrbuf_price(&h, &g_cm, &harness) == 0);
    InstrBuf t; instrbuf_init(&t); ins0(&t, "break");
    Candidate brk; assert(instrbuf_price(&t, &g_cm, &brk) == 0);

    AvrInterp *in = malloc(sizeof(AvrInterp));
    avr_interp_init(in);
    assert(avr_interp_run(in, &harness) == 0);
    unsigned long hc = in->cycles;
    assert(run_and_check(f, in, "fixture", NULL) == 0);
    unsigned long mc = in->cycles - hc;
    assert(avr_interp_run(in, &brk) == 0);
    free(in);

    FILE *out = fopen(path, "w");
    assert(out);
    EmitUnit unit; emit_unit_init(&unit);
    emit_program_prologue(out);
    assert(emit_candidate(&unit, &harness, out) == 0);
    fprintf(out, "\n    ; ---- OP_MAGNITUDE ----\n");
    assert(emit_candidate(&unit, &f->mag, out) == 0);
    assert(emit_candidate(&unit, &brk, out) == 0);
    fclose(out);
    printf("fixture %s: harness %u + magnitude %u + break %u = %u cycles predicted "
           "(interpreter %lu + %lu + 1)\n", path, harness.cycles, f->mag.cycles, brk.cycles,
           harness.cycles + f->mag.cycles + brk.cycles, hc, mc);
    candidate_free(&harness);
    candidate_free(&brk);
}

int main(int argc, char **argv) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
    Fx f; fx_init(&f);
    test_synthetic_vectors(&f);
    test_on_real_fft_output(&f);
    test_pipeline_bound(&f);
    test_structure(&f);
    if (argc == 3 && !strcmp(argv[1], "--emit-fixture")) emit_fixture(&f, argv[2]);
    fx_free(&f);
    printf("test_dsp_magnitude: all tests passed\n");
    return 0;
}
