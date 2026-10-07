/* Full 64-point FFT validation: stages 0-5, individually and chained.
 *
 * The oracle is written in the textbook iterative radix-2 DIT form (span m =
 * 2, 4, .., 64; twiddle W^(j*N/m)) rather than lower_fft.c's blocks/half form, so
 * the two formulations are cross-checked instead of one being copied into the
 * other. It uses the same Q15 definitions the equations in lower_fft.c state:
 * qmul = floor(x*y / 32768), /2 = arithmetic shift, halve before combining,
 * int16 wrap on add/sub (which the scaling proves never happens).
 *
 * Every comparison is exact equality over all 64 complex values. There is no
 * tolerance on the integer path; the floating-point DFT check at the end is
 * secondary and only bounds quantization error.
 *
 * Run with `--emit-fixture <out.s>` to also write the full-FFT Avrora fixture
 * and print its predicted cycle count. */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/registers.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/emit.h"
#include "optifine/ir.h"

#include "avr_interp.h"

#define N DSP_FFT_SIZE
#define STAGES DSP_FFT_LOG2
#define BITREV_OP 3
#define FIRST_STAGE_OP 4

static CostModel g_cm;

/* ---------------------------------------------------------------- oracle */

static int16_t h_qmul(int16_t x, int16_t y) { return (int16_t)(((int32_t)x * (int32_t)y) >> 15); }
static int16_t h_asr(int16_t v)             { return (int16_t)(v >> 1); }
static int16_t h_add(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a + (uint16_t)b); }
static int16_t h_sub(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a - (uint16_t)b); }

typedef struct { int16_t re[N], im[N]; } Buf;

/* One stage, textbook indexing: m is the butterfly span of the stage. */
static void host_stage(int s, const Buf *in, Buf *out) {
    int m = 2 << s;
    for (int k = 0; k < N; k += m) {
        for (int j = 0; j < m / 2; j++) {
            int p = k + j, q = k + j + m / 2;
            int16_t wr, wi;
            dsp_twiddle_q15(j * (N / m), &wr, &wi);
            int16_t t_re = h_sub(h_qmul(wr, in->re[q]), h_qmul(wi, in->im[q]));
            int16_t t_im = h_add(h_qmul(wr, in->im[q]), h_qmul(wi, in->re[q]));
            int16_t ph = h_asr(in->re[p]), th = h_asr(t_re);
            out->re[p] = h_add(ph, th);
            out->re[q] = h_sub(ph, th);
            ph = h_asr(in->im[p]); th = h_asr(t_im);
            out->im[p] = h_add(ph, th);
            out->im[q] = h_sub(ph, th);
        }
    }
}

static int brev6(int i) {
    int r = 0;
    for (int b = 0; b < STAGES; b++) { r = (r << 1) | (i & 1); i >>= 1; }
    return r;
}

/* Natural-order samples into the bit-reversed buffer the stages consume,
 * exactly what OP_BIT_REVERSE produces (out[brev(i)] = in[i]). */
static void bit_reverse(const Buf *natural, Buf *out) {
    for (int i = 0; i < N; i++) {
        out->re[brev6(i)] = natural->re[i];
        out->im[brev6(i)] = natural->im[i];
    }
}

/* ------------------------------------------------------------ fixture */

typedef struct {
    IrGraph graph;
    SramLayout layout;
    uint16_t buf_addr[STAGES + 1]; /* [0] = BitReverse output, [s+1] = stage s output */
} Fx;

static void fx_init(Fx *f) {
    assert(dsp_build_pipeline(&f->graph) == 0);
    assert(sram_layout_build(&f->graph, &f->layout) == 0);
    assert(f->graph.ops[BITREV_OP].kind == OP_BIT_REVERSE);
    f->buf_addr[0] = sram_layout_addr(&f->layout, &f->graph, BITREV_OP, 0);
    for (int s = 0; s < STAGES; s++) {
        assert(f->graph.ops[FIRST_STAGE_OP + s].kind == OP_FFT_BUTTERFLY);
        assert(f->graph.ops[FIRST_STAGE_OP + s].inputs[0] == (size_t)(FIRST_STAGE_OP + s - 1));
        f->buf_addr[s + 1] = sram_layout_addr(&f->layout, &f->graph, (size_t)(FIRST_STAGE_OP + s), 0);
    }
}
static void fx_free(Fx *f) { sram_layout_free(&f->layout); ir_graph_free(&f->graph); }

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

/* Exact comparison over all 64 complex values; prints the first mismatches
 * before failing so a broken stage is localized, not just detected. */
static int compare(const Buf *got, const Buf *want, const char *what) {
    int bad = 0;
    for (int i = 0; i < N; i++) {
        if (got->re[i] != want->re[i] || got->im[i] != want->im[i]) {
            if (bad < 4) {
                printf("    %s MISMATCH at %d: got (%d,%d) want (%d,%d)\n", what, i,
                       got->re[i], got->im[i], want->re[i], want->im[i]);
            }
            bad++;
        }
    }
    return bad;
}

/* SRAM bytes a run is allowed to change: the named output buffers plus the
 * cells of the DSP scratch region the FFT owns. Everything else, inputs
 * included, must come back untouched. */
static int write_allowed(const Fx *f, int first, int last, uint32_t a) {
    for (int s = first; s <= last; s++) {
        if (a >= f->buf_addr[s + 1] && a < (uint32_t)f->buf_addr[s + 1] + N * 4) return 1;
    }
    uint32_t sc = f->layout.dsp_scratch_addr;
    if (a >= sc && a < sc + 26) return 1;                          /* butterfly cells, BF_* */
    if (a == sc + DSP_SCRATCH_FFT_OUTER_COUNT) return 1;           /* block counter */
    return 0;
}

typedef struct {
    Candidate c;
    AvrInterp in;
} Run;

static Run *run_stages(Fx *f, int first, int last, const Buf *input) {
    Run *r = malloc(sizeof(Run));
    assert(lower_fft_stages_test_hook(&f->graph, first, last, &f->layout, &g_cm, &r->c) == 0);
    avr_interp_init(&r->in);
    /* Poison SRAM so a read of anything the program did not write shows up. */
    for (size_t a = 0; a < AVR_INTERP_MEM_SIZE; a++) r->in.mem[a] = (uint8_t)(a * 37 + 11);
    store_buf(r->in.mem, f->buf_addr[first], input);
    uint8_t *before = malloc(AVR_INTERP_MEM_SIZE);
    memcpy(before, r->in.mem, AVR_INTERP_MEM_SIZE);
    assert(avr_interp_run(&r->in, &r->c) == 0);
    int stray = 0;
    for (uint32_t a = 0; a < AVR_INTERP_MEM_SIZE; a++) {
        if (r->in.mem[a] != before[a] && !write_allowed(f, first, last, a)) {
            if (stray < 4) printf("    stray write at 0x%04X\n", a);
            stray++;
        }
    }
    free(before);
    assert(stray == 0);
    assert(r->c.cycles == r->in.cycles);
    return r;
}
static void run_free(Run *r) { candidate_free(&r->c); free(r); }

/* --------------------------------------------------------------- vectors */

#define MAX_VECTORS 24
typedef struct { const char *name; Buf natural; } Vector;
static Vector g_vec[MAX_VECTORS];
static int g_nvec;

static Buf *add_vector(const char *name) {
    assert(g_nvec < MAX_VECTORS);
    Vector *v = &g_vec[g_nvec++];
    v->name = name;
    memset(&v->natural, 0, sizeof(v->natural));
    return &v->natural;
}

static uint32_t g_rng;
static int16_t rng_q15(void) {
    g_rng = g_rng * 1664525u + 1013904223u; /* Numerical Recipes LCG, fixed seeds below */
    return (int16_t)(g_rng >> 16);
}

static void build_vectors(void) {
    const double pi = 3.14159265358979323846;
    Buf *b;
    add_vector("zero");
    b = add_vector("impulse at 0");      b->re[0] = 32767;
    b = add_vector("impulse at 1");      b->re[1] = 32767;
    b = add_vector("DC");                for (int i = 0; i < N; i++) b->re[i] = 16384;
    b = add_vector("real tone k=1");     for (int i = 0; i < N; i++) b->re[i] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * i / N));
    b = add_vector("real tone k=5");     for (int i = 0; i < N; i++) b->re[i] = (int16_t)lround(0.9 * 32767 * cos(2 * pi * 5 * i / N));
    b = add_vector("real tone k=23");    for (int i = 0; i < N; i++) b->re[i] = (int16_t)lround(0.9 * 32767 * sin(2 * pi * 23 * i / N));
    b = add_vector("complex tone k=31"); for (int i = 0; i < N; i++) {
        b->re[i] = (int16_t)lround(0.7 * 32767 * cos(2 * pi * 31 * i / N));
        b->im[i] = (int16_t)lround(0.7 * 32767 * sin(2 * pi * 31 * i / N));
    }
    b = add_vector("alternating INT16_MIN/MAX"); for (int i = 0; i < N; i++) b->re[i] = (i & 1) ? INT16_MIN : INT16_MAX;
    b = add_vector("alternating complex extremes"); for (int i = 0; i < N; i++) {
        b->re[i] = (i & 1) ? INT16_MIN : INT16_MAX;
        b->im[i] = (i & 2) ? INT16_MAX : INT16_MIN;
    }
    b = add_vector("deterministic mixed"); for (int i = 0; i < N; i++) {
        b->re[i] = (int16_t)(i * 1031 - 16384);
        b->im[i] = (int16_t)(-i * 617 + 8192);
    }
    b = add_vector("negative-heavy");    for (int i = 0; i < N; i++) {
        b->re[i] = (int16_t)(-32768 + (i * 97) % 4000);
        b->im[i] = (int16_t)(-20000 - i * 150);
    }
    b = add_vector("all INT16_MIN");     for (int i = 0; i < N; i++) { b->re[i] = INT16_MIN; b->im[i] = INT16_MIN; }
    b = add_vector("Q15 boundary pattern"); {
        const int16_t edge[] = {0, 1, -1, INT16_MAX, INT16_MIN, 16384, -16384, 32766, -32767};
        for (int i = 0; i < N; i++) { b->re[i] = edge[i % 9]; b->im[i] = edge[(i + 4) % 9]; }
    }
    static const uint32_t seeds[] = {1u, 42u, 1234567u, 0xDEADBEEFu, 20260923u, 7u, 99991u, 0x13579BDFu};
    static char names[8][32];
    for (int k = 0; k < 8; k++) {
        snprintf(names[k], sizeof(names[k]), "random seed 0x%08X", (unsigned)seeds[k]);
        b = add_vector(names[k]);
        g_rng = seeds[k];
        for (int i = 0; i < N; i++) { b->re[i] = rng_q15(); b->im[i] = (k & 1) ? rng_q15() : 0; }
    }
}

/* ----------------------------------------------------------------- tests */

/* Host result of stages 0..s for a vector: states[0] is the bit-reversed
 * input, states[s+1] the output of stage s. */
static void host_states(const Vector *v, Buf states[STAGES + 1]) {
    bit_reverse(&v->natural, &states[0]);
    for (int s = 0; s < STAGES; s++) host_stage(s, &states[s], &states[s + 1]);
}

static void test_each_stage_in_isolation(Fx *f) {
    printf("per-stage oracle (each stage alone, fed the oracle's previous stage):\n");
    for (int s = 0; s < STAGES; s++) {
        int compared = 0, mismatched = 0;
        uint32_t cycles = 0;
        size_t emitted = 0;
        for (int vi = 0; vi < g_nvec; vi++) {
            Buf states[STAGES + 1], got;
            host_states(&g_vec[vi], states);
            Run *r = run_stages(f, s, s, &states[s]);
            load_buf(r->in.mem, f->buf_addr[s + 1], &got);
            mismatched += compare(&got, &states[s + 1], g_vec[vi].name);
            compared += N;
            cycles = r->c.cycles;
            emitted = r->c.num_instructions;
            run_free(r);
        }
        printf("  stage %d: %2d vectors, %4d complex values compared, %d mismatched; "
               "%zu emitted entries, %u cycles predicted == interpreted\n",
               s, g_nvec, compared, mismatched, emitted, cycles);
        assert(mismatched == 0);
    }
}

static void test_full_fft_chained(Fx *f) {
    printf("full six-stage FFT (one program, every intermediate buffer checked):\n");
    int compared = 0, mismatched = 0;
    for (int vi = 0; vi < g_nvec; vi++) {
        Buf states[STAGES + 1], got;
        host_states(&g_vec[vi], states);
        Run *r = run_stages(f, 0, STAGES - 1, &states[0]);
        for (int s = 0; s < STAGES; s++) {
            load_buf(r->in.mem, f->buf_addr[s + 1], &got);
            int bad = compare(&got, &states[s + 1], g_vec[vi].name);
            if (bad) printf("    ... in stage %d's buffer\n", s);
            mismatched += bad;
            compared += N;
        }
        printf("  %-30s exact through all 6 stages\n", g_vec[vi].name);
        run_free(r);
    }
    printf("  %d vectors, %d complex values compared (%d final-output), %d mismatched\n",
           g_nvec, compared, g_nvec * N, mismatched);
    assert(mismatched == 0);
}

/* The oracle proves what the code computes; this proves the oracle is an FFT.
 * Final output should be DFT(x)/64 up to Q15 truncation and twiddle
 * quantization. The bound is loose on purpose -- it is not a tolerance on the
 * AVR result, which is already exact against the integer oracle. */
static void test_oracle_against_float_dft(void) {
    const double pi = 3.14159265358979323846;
    double worst = 0.0;
    for (int vi = 0; vi < g_nvec; vi++) {
        Buf states[STAGES + 1];
        host_states(&g_vec[vi], states);
        for (int k = 0; k < N; k++) {
            double sr = 0, si = 0;
            for (int n = 0; n < N; n++) {
                double a = -2 * pi * k * n / N;
                sr += g_vec[vi].natural.re[n] * cos(a) - g_vec[vi].natural.im[n] * sin(a);
                si += g_vec[vi].natural.re[n] * sin(a) + g_vec[vi].natural.im[n] * cos(a);
            }
            double er = fabs(states[STAGES].re[k] - sr / N), ei = fabs(states[STAGES].im[k] - si / N);
            if (er > worst) worst = er;
            if (ei > worst) worst = ei;
        }
    }
    printf("oracle vs float DFT/64: worst deviation %.3f LSB over %d vectors\n", worst, g_nvec);
    assert(worst < 8.0);
}

/* Tone at bin k lands in bin k (and its mirror for a real tone): a direct,
 * readable demonstration that the output is in natural bin order. */
static void test_tone_lands_in_its_bin(Fx *f) {
    for (int vi = 0; vi < g_nvec; vi++) {
        int bin;
        if (!strcmp(g_vec[vi].name, "real tone k=5")) bin = 5;
        else if (!strcmp(g_vec[vi].name, "complex tone k=31")) bin = 31;
        else continue;
        Buf states[STAGES + 1], got;
        host_states(&g_vec[vi], states);
        Run *r = run_stages(f, 0, STAGES - 1, &states[0]);
        load_buf(r->in.mem, f->buf_addr[STAGES], &got);
        int best = 0; double best_mag = -1;
        for (int k = 0; k < N; k++) {
            double m = (double)got.re[k] * got.re[k] + (double)got.im[k] * got.im[k];
            if (m > best_mag) { best_mag = m; best = k; }
        }
        printf("  %s: peak in bin %d (|X|=%.0f)\n", g_vec[vi].name, best, sqrt(best_mag));
        /* A real cosine splits evenly between bin k and its mirror N-k. */
        int is_real = strncmp(g_vec[vi].name, "real", 4) == 0;
        assert(best == bin || (is_real && best == N - bin));
        run_free(r);
    }
}

static int reg_num(const char *op) { return (op[0] == 'r') ? atoi(op + 1) : -1; }

/* Registers the FFT writes: exactly the documented contract (r0/r1 by the
 * multiplies, r3 the butterfly counter, r16-r25 the multiply window and byte
 * scratch, r26-r31 the pointers). r2 and the DSP32 quads r4-r15 are never
 * touched. */
static void test_register_footprint(Fx *f) {
    Candidate c;
    assert(lower_fft_stages_test_hook(&f->graph, 0, STAGES - 1, &f->layout, &g_cm, &c) == 0);
    int written[32] = {0};
    for (size_t i = 0; i < c.num_instructions; i++) {
        const AvrInstr *ins = &c.instructions[i];
        const char *m = ins->mnemonic;
        if (!strcmp(m, "mul") || !strcmp(m, "muls") || !strcmp(m, "mulsu")) { written[0] = written[1] = 1; continue; }
        if (!strcmp(m, "st") && ins->operands[0][1] == '+') {
            int base = ins->operands[0][0] == 'X' ? 26 : ins->operands[0][0] == 'Y' ? 28 : 30;
            written[base] = written[base + 1] = 1;
            continue;
        }
        if (!strcmp(m, "st") || !strcmp(m, "sts") || !strcmp(m, "brne") || !strcmp(m, "breq") ||
            !strcmp(m, "rjmp") || !strcmp(m, "break") || avr_instr_is_label(ins) || avr_instr_is_data_word(ins)) {
            continue;
        }
        int rd = reg_num(ins->operands[0]);
        if (rd < 0) continue;
        written[rd] = 1;
        if (!strcmp(m, "adiw") || !strcmp(m, "sbiw")) written[rd + 1] = 1;
        /* post-increment through a pointer also writes the pointer pair */
        if ((!strcmp(m, "ld") || !strcmp(m, "lpm")) && ins->operands[1][1] == '+') {
            int base = ins->operands[1][0] == 'X' ? 26 : ins->operands[1][0] == 'Y' ? 28 : 30;
            written[base] = written[base + 1] = 1;
        }
    }
    printf("registers written by the full FFT:");
    for (int r = 0; r < 32; r++) if (written[r]) printf(" r%d", r);
    printf("\n");
    assert(!written[REG_ZERO]);
    for (int r = DSP32_A; r < DSP32_C + DSP32_BYTES; r++) assert(!written[r]);
    assert(written[REG_DSP_LOOP_COUNTER]);
    candidate_free(&c);
}

/* Geometry, read back from the emitted code: one butterfly counter per stage
 * on r3, one SRAM block counter per stage with more than one block, and the Z
 * step after the lpm quartet equal to 4*blocks-4. */
static void test_geometry_in_emitted_code(Fx *f) {
    printf("geometry read back from each stage's code:\n");
    printf("  stage blocks half total  twiddle k        Z step  in -> out\n");
    for (int s = 0; s < STAGES; s++) {
        Candidate c;
        assert(lower_fft_stages_test_hook(&f->graph, s, s, &f->layout, &g_cm, &c) == 0);
        int half = 1 << s, blocks = N / (2 * half);
        int lpm_seen = 0, z_step = 0, z_step_found = 0, sram_counter = 0, r3_counter = 0;
        uint16_t counter_cell = (uint16_t)(f->layout.dsp_scratch_addr + DSP_SCRATCH_FFT_OUTER_COUNT);
        char cell[AVR_OPERAND_LEN]; fmt_addr(cell, counter_cell);
        for (size_t i = 0; i < c.num_instructions; i++) {
            const AvrInstr *ins = &c.instructions[i];
            if (!strcmp(ins->mnemonic, "lpm")) { lpm_seen++; continue; }
            /* each lpm is followed by the sts that parks its byte */
            if (lpm_seen == 4 && !z_step_found && strcmp(ins->mnemonic, "sts") != 0) {
                if (!strcmp(ins->operands[0], "r30") && !strcmp(ins->mnemonic, "adiw")) z_step = (int)strtol(ins->operands[1], NULL, 16);
                else if (!strcmp(ins->operands[0], "r30") && !strcmp(ins->mnemonic, "sbiw")) z_step = -(int)strtol(ins->operands[1], NULL, 16);
                z_step_found = 1;
            }
            if (!strcmp(ins->mnemonic, "mov") && !strcmp(ins->operands[0], "r3")) r3_counter++;
            if (!strcmp(ins->mnemonic, "sts") && !strcmp(ins->operands[0], cell)) sram_counter++;
            if (!strcmp(ins->mnemonic, "ldi") && strstr(ins->operands[1], "pm_")) assert(!"pm_lo8/pm_hi8 would read the wrong table bytes");
        }
        assert(lpm_seen == 4);
        assert(z_step == ((s == 0) ? -4 : blocks * 4 - 4));
        assert(r3_counter == 1);
        assert(sram_counter == ((s == 0 || blocks == 1) ? 0 : 2)); /* init + close */
        printf("  %5d %6d %4d %5d  0..%-2d step %-2d  %+4d   0x%04X -> 0x%04X\n", s, blocks, half,
               blocks * half, (half - 1) * blocks, blocks, z_step, f->buf_addr[s], f->buf_addr[s + 1]);
        candidate_free(&c);
    }
    /* Twiddle indices over all stages cover 0..31, the whole shared table. */
    int used[32] = {0};
    for (int s = 0; s < STAGES; s++) {
        int half = 1 << s, blocks = N / (2 * half);
        for (int j = 0; j < half; j++) used[j * blocks] = 1;
    }
    for (int k = 0; k < 32; k++) assert(used[k]);
}

static void test_program_layout(Fx *f) {
    Candidate c;
    assert(lower_fft_stages_test_hook(&f->graph, 0, STAGES - 1, &f->layout, &g_cm, &c) == 0);
    size_t brk = (size_t)-1, tbl = (size_t)-1, breaks = 0, words = 0, code_bytes = 0, data_bytes = 0;
    for (size_t i = 0; i < c.num_instructions; i++) {
        const AvrInstr *ins = &c.instructions[i];
        if (!strcmp(ins->mnemonic, "break")) { breaks++; if (brk == (size_t)-1) brk = i; }
        if (avr_instr_is_label(ins) && !strcmp(ins->operands[0], ".Ltw")) tbl = i;
        if (!strcmp(ins->mnemonic, "brne") || !strcmp(ins->mnemonic, "breq") || !strcmp(ins->mnemonic, "rjmp")) {
            assert(strcmp(ins->operands[0], ".Ltw") != 0);
        }
        if (avr_instr_is_data_word(ins)) { words++; data_bytes += 2; }
        else if (brk == (size_t)-1) code_bytes += avr_instr_flash_bytes(ins);
    }
    assert(breaks == 1 && tbl != (size_t)-1 && tbl == brk + 1);
    assert(words == 64 && data_bytes == 128);
    printf("program layout: %zu B FFT code, 2 B break, %zu B twiddle table after it (label index %zu)\n",
           code_bytes, data_bytes, tbl);
    candidate_free(&c);
}

/* ---------------------------------------------------------------- fixture */

/* Writes the full-FFT Avrora fixture: clr r2 and the bit-reversed input
 * written byte by byte (the harness, priced like any other code), then the
 * six stages, break and the twiddle table. Predicted cycles = harness + FFT;
 * BREAK is priced inside the FFT candidate, so there is no extra constant. */
static void emit_fixture(Fx *f, const char *path) {
    const Vector *v = NULL;
    for (int vi = 0; vi < g_nvec; vi++) if (!strcmp(g_vec[vi].name, "deterministic mixed")) v = &g_vec[vi];
    assert(v);
    Buf states[STAGES + 1];
    host_states(v, states);

    InstrBuf h; instrbuf_init(&h);
    char r2[AVR_OPERAND_LEN], r24[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN], a[AVR_OPERAND_LEN];
    fmt_reg(r2, REG_ZERO); fmt_reg(r24, REG_SCRATCH0);
    ins1(&h, "clr", r2);
    static uint8_t image[AVR_INTERP_MEM_SIZE];
    store_buf(image, f->buf_addr[0], &states[0]);
    for (int i = 0; i < N * 4; i++) {
        fmt_imm(imm, image[f->buf_addr[0] + i]); ins2(&h, "ldi", r24, imm);
        fmt_addr(a, (uint16_t)(f->buf_addr[0] + i)); ins2(&h, "sts", a, r24);
    }
    Candidate harness; assert(instrbuf_price(&h, &g_cm, &harness) == 0);
    Candidate fft; assert(lower_fft_stages_test_hook(&f->graph, 0, STAGES - 1, &f->layout, &g_cm, &fft) == 0);

    FILE *out = fopen(path, "w");
    assert(out);
    EmitUnit unit;
    emit_unit_init(&unit);
    emit_program_prologue(out);
    assert(emit_candidate(&unit, &harness, out) == 0);
    fprintf(out, "\n    ; ---- FFT stages 0-5 ----\n");
    assert(emit_candidate(&unit, &fft, out) == 0);
    emit_unit_free(&unit);
    fclose(out);

    /* Same run in the interpreter, so the fixture's own cycles are checked. */
    AvrInterp in; avr_interp_init(&in);
    assert(avr_interp_run(&in, &harness) == 0);
    unsigned long harness_cycles = in.cycles;
    assert(avr_interp_run(&in, &fft) == 0);
    Buf got; load_buf(in.mem, f->buf_addr[STAGES], &got);
    assert(compare(&got, &states[STAGES], "fixture") == 0);
    printf("fixture %s: harness %u + FFT %u = %u cycles predicted; interpreter %lu + %lu = %lu\n",
           path, harness.cycles, fft.cycles, harness.cycles + fft.cycles,
           harness_cycles, in.cycles - harness_cycles, in.cycles);
    assert(harness.cycles + fft.cycles == in.cycles);
    candidate_free(&harness);
    candidate_free(&fft);
}

static void report_totals(Fx *f) {
    Buf states[STAGES + 1];
    host_states(&g_vec[0], states);
    Run *r = run_stages(f, 0, STAGES - 1, &states[0]);
    size_t emitted = 0;
    for (size_t i = 0; i < r->c.num_instructions; i++) {
        const AvrInstr *ins = &r->c.instructions[i];
        if (!avr_instr_is_label(ins) && !avr_instr_is_data_word(ins)) emitted++;
    }
    printf("full FFT: %zu emitted instructions (incl. break), %lu executed, "
           "%u cycles predicted, %lu interpreted\n",
           emitted, r->in.instructions, r->c.cycles, r->in.cycles);
    run_free(r);
}

int main(int argc, char **argv) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
    build_vectors();
    Fx f; fx_init(&f);

    test_geometry_in_emitted_code(&f);
    test_register_footprint(&f);
    test_program_layout(&f);
    test_each_stage_in_isolation(&f);
    test_full_fft_chained(&f);
    test_tone_lands_in_its_bin(&f);
    test_oracle_against_float_dft();
    report_totals(&f);

    if (argc == 3 && !strcmp(argv[1], "--emit-fixture")) emit_fixture(&f, argv[2]);

    fx_free(&f);
    printf("test_dsp_fft: all tests passed\n");
    return 0;
}
