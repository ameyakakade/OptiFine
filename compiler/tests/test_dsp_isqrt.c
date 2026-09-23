/* isqrt32 in isolation: exact floor(sqrt(n)) for unsigned 32-bit n.
 *
 * The oracle is a host integer square root checked with 64-bit arithmetic
 * (r*r <= n < (r+1)*(r+1)), independent of the AVR algorithm. Every case
 * compares the interpreter's result to it exactly, and every run must execute
 * the same number of cycles -- the algorithm makes no data-dependent branch --
 * equal to the compiler's prediction.
 *
 *   test_dsp_isqrt --full                  the ~367k-case sweep (see test_corpus)
 *   test_dsp_isqrt --emit-fixtures <dir>   writes Avrora microfixtures:
 *     isqrt_<name>.s   one invocation on one value, for several values
 *     isqrt_multi.s    all of those values, one after another */
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/dsp32.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/registers.h"
#include "optifine/cost_model.h"
#include "optifine/emit.h"

#include "avr_interp.h"

#define IN_ADDR  0x0400
#define OUT_ADDR 0x0410

static CostModel g_cm;

static uint32_t host_isqrt(uint32_t n) {
    uint64_t r = (uint64_t)sqrt((double)n);
    while (r * r > n) r--;
    while ((r + 1) * (r + 1) <= n) r++;
    assert(r * r <= n && (r + 1) * (r + 1) > n);
    return (uint32_t)r;
}

static void emit_isqrt(InstrBuf *b) {
    dsp32_isqrt(b, DSP32_A, DSP32_B, DSP32_C, DSP32_T, REG_DSP_MASK, REG_DSP_LOOP_COUNTER);
}

/* The program under test: n from SRAM, isqrt, result to SRAM, break. */
static Candidate g_prog;
static unsigned long g_cycles, g_instrs;

static void build_prog(void) {
    InstrBuf b; instrbuf_init(&b);
    dsp32_load(&b, DSP32_A, IN_ADDR);
    emit_isqrt(&b);
    dsp32_store(&b, DSP32_B, OUT_ADDR);
    ins0(&b, "break");
    assert(instrbuf_price(&b, &g_cm, &g_prog) == 0);
}

static AvrInterp g_in;
static uint32_t run_one(uint32_t n) {
    avr_interp_init(&g_in);
    for (int i = 0; i < 4; i++) g_in.mem[IN_ADDR + i] = (uint8_t)(n >> (8 * i));
    /* Poison every register the routine should initialise itself. */
    for (int r = 3; r < 32; r++) g_in.regs[r] = (uint8_t)(0xA5 ^ r);
    g_in.regs[REG_ZERO] = 0;
    assert(avr_interp_run(&g_in, &g_prog) == 0);
    if (g_cycles == 0) { g_cycles = g_in.cycles; g_instrs = g_in.instructions; }
    assert(g_in.cycles == g_cycles);          /* input-independent */
    assert(g_in.instructions == g_instrs);
    uint32_t r = 0;
    for (int i = 0; i < 4; i++) r |= (uint32_t)g_in.mem[OUT_ADDR + i] << (8 * i);
    return r;
}

static unsigned long g_cases, g_mismatches;
static void check(uint32_t n) {
    uint32_t got = run_one(n), want = host_isqrt(n);
    g_cases++;
    if (got != want) {
        if (g_mismatches < 8) printf("  MISMATCH isqrt32(%u): got %u want %u\n", n, got, want);
        g_mismatches++;
    }
}

static const struct { const char *name; uint32_t n; } kEdges[] = {
    {"zero", 0u},
    {"one", 1u},
    {"two", 2u},
    {"three", 3u},
    {"four", 4u},
    {"nonsquare_1000000007", 1000000007u},
    {"int16_max_sq", 32767u * 32767u},          /* 1073676289 */
    {"int16_min_sq", 32768u * 32768u},          /* 2^30 */
    {"two_pow_30_minus_1", (1u << 30) - 1u},
    {"two_pow_30_plus_1", (1u << 30) + 1u},
    {"two_pow_31_minus_1", 0x7FFFFFFFu},
    {"two_pow_31", 0x80000000u},                /* the Magnitude maximum, re = im = -32768 */
    {"two_pow_31_plus_1", 0x80000001u},
    {"sq_46340", 46340u * 46340u},
    {"sq_46340_minus_1", 46340u * 46340u - 1u},
    {"sq_46341", 46341u * 46341u},
    {"sq_46341_minus_1", 46341u * 46341u - 1u},
    {"sq_65535", 65535u * 65535u},
    {"sq_65535_minus_1", 65535u * 65535u - 1u},
    {"uint32_max", 0xFFFFFFFFu},
};
#define N_EDGES (sizeof(kEdges) / sizeof(kEdges[0]))

/* `full` sweeps every perfect square with a 16-bit root (and its
 * neighbours) plus 0..70000 exhaustively: ~367k cases, ~50 s through the
 * interpreter, run on demand with --full. The default keeps every boundary
 * region that matters -- small n, the int16 square range, the 46340 region
 * Magnitude's 2^31 maximum sits in, the top of uint32 -- and samples the rest. */
static int wanted_root(uint32_t k, int full) {
    if (full) return 1;
    return k <= 1024u || (k >= 32640u && k <= 32896u) || (k >= 46000u && k <= 46341u) ||
           k >= 65280u || (k % 61u) == 0;
}

static void test_corpus(int full) {
    g_cases = g_mismatches = 0;
    for (size_t i = 0; i < N_EDGES; i++) check(kEdges[i].n);
    unsigned long edges = g_cases;

    uint32_t small_top = full ? 70000u : 4096u;
    for (uint32_t n = 0; n <= small_top; n++) check(n);                 /* exhaustive small range */
    unsigned long small = g_cases - edges;

    /* Perfect squares and their neighbours: the only places where
     * floor(sqrt) changes value. */
    for (uint32_t k = 0; k <= 65535u; k++) {
        if (!wanted_root(k, full)) continue;
        uint32_t sq = k * k;
        check(sq);
        if (sq > 0) check(sq - 1);
        if (sq < 0xFFFFFFFFu) check(sq + 1);
    }
    unsigned long squares = g_cases - edges - small;

    /* Powers of two and their neighbours. */
    for (int s = 0; s < 32; s++) {
        uint32_t p = 1u << s;
        check(p); check(p - 1); check(p + 1);
    }

    uint32_t x = 0x2545F491u; /* fixed seed, xorshift32 */
    int n_random = full ? 50000 : 5000;
    for (int i = 0; i < n_random; i++) {
        x ^= x << 13; x ^= x >> 17; x ^= x << 5;
        check(x);
        check(x >> 1);  /* the <= 2^31 half Magnitude actually produces */
    }
    printf("  %s corpus: %lu cases (%lu edge, %lu exhaustive 0..%u, %lu squares +/-1, "
           "96 powers of 2 +/-1, %d random): %lu mismatches\n", full ? "full" : "default",
           g_cases, edges, small, small_top, squares, 2 * n_random, g_mismatches);
    assert(g_mismatches == 0);
    printf("  every case: %lu cycles, %lu instructions executed (predicted %u)\n",
           g_cycles, g_instrs, g_prog.cycles);
    assert(g_prog.cycles == g_cycles);
}

/* The routine alone, hand-derived from the AVR Instruction Set Manual: 11
 * setup instructions and 16 iterations of a 42-instruction body in which every
 * instruction is 1 cycle, closed by dec + brne (short form). */
static void test_hand_derived_cycles(void) {
    InstrBuf b; instrbuf_init(&b);
    emit_isqrt(&b);
    Candidate c; assert(instrbuf_price(&b, &g_cm, &c) == 0);
    /* setup: clr res x4 (4) + bit: clr x3, ldi, mov (5) + counter ldi, mov (2) = 11
     * loop:  16 x (42 body + 1 dec) + 15 x 2 brne taken + 1 fall-through       = 719 */
    const uint32_t expected = 11 + 16 * 43 + 15 * 2 + 1;
    size_t emitted = 0;
    for (size_t i = 0; i < c.num_instructions; i++) if (!avr_instr_is_label(&c.instructions[i])) emitted++;
    printf("  isqrt32 alone: %zu emitted instructions, %u cycles predicted, %u hand-derived\n",
           emitted, c.cycles, expected);
    assert(c.cycles == expected);
    assert(emitted == 11 + 42 + 2);
    candidate_free(&c);
}

static void test_footprint_and_flags(void) {
    InstrBuf b; instrbuf_init(&b);
    emit_isqrt(&b);
    Candidate c; assert(instrbuf_price(&b, &g_cm, &c) == 0);
    int written[32] = {0};
    for (size_t i = 0; i < c.num_instructions; i++) {
        const AvrInstr *ins = &c.instructions[i];
        if (avr_instr_is_label(ins) || ins->operands[0][0] != 'r') continue;
        const char *m = ins->mnemonic;
        if (!strcmp(m, "cp") || !strcmp(m, "cpc")) continue;
        int r = atoi(ins->operands[0] + 1);
        written[r] = 1;
        if (!strcmp(m, "movw")) written[r + 1] = 1;
    }
    for (int r = 0; r < 32; r++) {
        int allowed = (r >= DSP32_A && r < DSP32_C + 4) || (r >= DSP32_T && r < DSP32_T + 4) ||
                      r == REG_DSP_MASK || r == REG_DSP_LOOP_COUNTER || r == REG_SCRATCH0;
        if (written[r] && !allowed) { printf("  isqrt32 writes undeclared r%d\n", r); assert(0); }
    }
    assert(!written[0] && !written[1] && !written[REG_ZERO]);
    assert(!written[26] && !written[27] && !written[28] && !written[29] && !written[30] && !written[31]);
    printf("  footprint: A, B, C, T(r16-r19), r25 mask, r3 counter, r24 staging -- nothing else\n");
    candidate_free(&c);

    /* Incoming carry and zero set versus clear must not matter. */
    for (int pre = 0; pre < 2; pre++) {
        InstrBuf p; instrbuf_init(&p);
        char x[AVR_OPERAND_LEN], imm[AVR_OPERAND_LEN];
        fmt_reg(x, 26); fmt_imm(imm, pre ? 0xFF : 0x01);
        ins2(&p, "ldi", x, imm);
        ins2(&p, "mul", x, x);                 /* C = bit 15 of the product */
        dsp32_load(&p, DSP32_A, IN_ADDR);
        emit_isqrt(&p);
        dsp32_store(&p, DSP32_B, OUT_ADDR);
        ins0(&p, "break");
        Candidate pc; assert(instrbuf_price(&p, &g_cm, &pc) == 0);
        const uint32_t probes[] = {0u, 99u, 0x80000000u, 0xFFFFFFFFu, 1073676289u};
        for (size_t i = 0; i < sizeof(probes) / sizeof(probes[0]); i++) {
            AvrInterp in; avr_interp_init(&in);
            in.carry = (uint8_t)pre; in.zero = (uint8_t)pre;
            for (int k = 0; k < 4; k++) in.mem[IN_ADDR + k] = (uint8_t)(probes[i] >> (8 * k));
            assert(avr_interp_run(&in, &pc) == 0);
            uint32_t r = 0;
            for (int k = 0; k < 4; k++) r |= (uint32_t)in.mem[OUT_ADDR + k] << (8 * k);
            assert(r == host_isqrt(probes[i]));
        }
        candidate_free(&pc);
    }
    printf("  flag independence: identical results with incoming C/Z set and clear\n");
}

/* --- Avrora microfixtures --------------------------------------------- */

static uint32_t emit_fixture(const char *path, const uint32_t *values, size_t n) {
    InstrBuf b; instrbuf_init(&b);
    for (size_t i = 0; i < n; i++) {
        dsp32_load_imm(&b, DSP32_A, values[i], REG_SCRATCH0);
        emit_isqrt(&b);
        dsp32_store(&b, DSP32_B, (uint16_t)(OUT_ADDR + 4 * i));
    }
    ins0(&b, "break");
    Candidate c; assert(instrbuf_price(&b, &g_cm, &c) == 0);

    AvrInterp in; avr_interp_init(&in);
    assert(avr_interp_run(&in, &c) == 0);
    assert(in.cycles == c.cycles);
    for (size_t i = 0; i < n; i++) {
        uint32_t r = 0;
        for (int k = 0; k < 4; k++) r |= (uint32_t)in.mem[OUT_ADDR + 4 * i + k] << (8 * k);
        assert(r == host_isqrt(values[i]));
    }

    FILE *out = fopen(path, "w");
    assert(out);
    EmitUnit unit; emit_unit_init(&unit);
    emit_program_prologue(out);
    assert(emit_candidate(&unit, &c, out) == 0);
    fclose(out);
    uint32_t cyc = c.cycles;
    candidate_free(&c);
    return cyc;
}

static void emit_fixtures(const char *dir) {
    const struct { const char *name; uint32_t n; } picks[] = {
        {"zero", 0u}, {"one", 1u}, {"nonsquare", 1000000007u},
        {"large", 0xFFFFFFFFu}, {"two_pow_31", 0x80000000u},
    };
    const size_t np = sizeof(picks) / sizeof(picks[0]);
    uint32_t values[8];
    char path[512];
    for (size_t i = 0; i < np; i++) {
        snprintf(path, sizeof(path), "%s/isqrt_%s.s", dir, picks[i].name);
        values[i] = picks[i].n;
        uint32_t cyc = emit_fixture(path, &values[i], 1);
        printf("fixture %s: n=%u -> %u, %u cycles predicted\n", path, picks[i].n,
               host_isqrt(picks[i].n), cyc);
    }
    snprintf(path, sizeof(path), "%s/isqrt_multi.s", dir);
    uint32_t cyc = emit_fixture(path, values, np);
    printf("fixture %s: %zu values, %u cycles predicted\n", path, np, cyc);
}

int main(int argc, char **argv) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
    build_prog();
    test_hand_derived_cycles();
    test_footprint_and_flags();
    test_corpus(argc == 2 && !strcmp(argv[1], "--full"));
    if (argc == 3 && !strcmp(argv[1], "--emit-fixtures")) emit_fixtures(argv[2]);
    candidate_free(&g_prog);
    printf("test_dsp_isqrt: all tests passed\n");
    return 0;
}
