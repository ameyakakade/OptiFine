/* OP_BIT_REVERSE validation.
 *
 * BitReverse is where the pipeline crosses from a real Q15 window (2-byte
 * stride) into the complex Q15 working buffer (4-byte stride), so the two
 * things most likely to go wrong are the permutation itself and a stride
 * confusion between the two layouts. Both are checked against an independent
 * host oracle over the entire 64-element buffer, byte for byte, rather than
 * at a handful of sampled indices. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/lower.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/ir.h"

#include "avr_interp.h"

#define BITREV_OP 3   /* Input, Const, Window, BitReverse */
#define WINDOW_OP 2

static CostModel g_cm;
static void load_costs(void) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
}

/* Independent host oracle: written from the definition, not by calling the
 * compiler's own helper. */
static int host_bitrev6(int i) {
    int r = 0;
    for (int b = 0; b < 6; b++) { r = (r << 1) | ((i >> b) & 1); }
    return r;
}

/* --- A. the permutation is a bijection -------------------------------- */
static void test_permutation_is_a_bijection(void) {
    int seen[64];
    memset(seen, 0, sizeof(seen));
    for (int i = 0; i < 64; i++) {
        size_t r = dsp_bit_reverse_index_test_hook((size_t)i, 6);
        assert(r < 64);                       /* in range */
        assert((int)r == host_bitrev6(i));    /* agrees with the host oracle */
        seen[r]++;
    }
    for (int i = 0; i < 64; i++) assert(seen[i] == 1); /* no duplicates, none omitted */

    /* The documented mappings, checked explicitly. */
    struct { int in, out; } known[] = {{0,0},{1,32},{2,16},{3,48},{31,62},{32,1},{63,63}};
    for (size_t k = 0; k < sizeof(known)/sizeof(known[0]); k++) {
        assert((int)dsp_bit_reverse_index_test_hook((size_t)known[k].in, 6) == known[k].out);
    }
    /* Involution: brev(brev(i)) == i, which is why "out[i] = in[brev(i)]" and
     * "out[brev(i)] = in[i]" specify the same permutation. */
    for (int i = 0; i < 64; i++) {
        assert(dsp_bit_reverse_index_test_hook(dsp_bit_reverse_index_test_hook((size_t)i,6),6) == (size_t)i);
    }
    printf("  A. permutation: bijective over 0..63, matches host oracle, involutive\n");
}

/* --- shared harness ---------------------------------------------------- */
typedef struct {
    IrGraph graph; SramLayout layout; RegAllocResult regalloc;
    uint16_t in_addr, out_addr;
} Fixture;

static void fixture_init(Fixture *f) {
    assert(dsp_build_pipeline(&f->graph) == 0);
    assert(sram_layout_build(&f->graph, &f->layout) == 0);
    assert(regalloc_next_use(&f->graph, &f->regalloc) == 0);
    f->in_addr = sram_layout_addr(&f->layout, &f->graph, WINDOW_OP, 0);
    f->out_addr = sram_layout_addr(&f->layout, &f->graph, BITREV_OP, 0);
}
static void fixture_free(Fixture *f) {
    regalloc_result_free(&f->regalloc); sram_layout_free(&f->layout); ir_graph_free(&f->graph);
}

/* Seeds the window output, runs BitReverse, returns the candidate. */
static void run_bitreverse(Fixture *f, const int16_t *samples, AvrInterp *in, Candidate *c) {
    avr_interp_init(in);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        in->mem[f->in_addr + i * 2]     = (uint8_t)((uint16_t)samples[i] & 0xFF);
        in->mem[f->in_addr + i * 2 + 1] = (uint8_t)(((uint16_t)samples[i] >> 8) & 0xFF);
    }
    assert(lower_op(&f->graph, BITREV_OP, &f->layout, &f->regalloc, &g_cm, NULL, 0, c) == 0);
    assert(avr_interp_run(in, c) == 0);
}

/* --- B/C/D. full-buffer comparison against the host oracle ------------- */
static void check_full_buffer(Fixture *f, const int16_t *samples, const char *name) {
    AvrInterp in; Candidate c;
    run_bitreverse(f, samples, &in, &c);

    /* Host oracle builds the whole complex buffer independently. */
    uint8_t expect[DSP_FFT_SIZE * 4];
    memset(expect, 0, sizeof(expect));
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int16_t v = samples[host_bitrev6(i)];        /* out[i].re = in[brev(i)] */
        expect[i * 4]     = (uint8_t)((uint16_t)v & 0xFF);
        expect[i * 4 + 1] = (uint8_t)(((uint16_t)v >> 8) & 0xFF);
        expect[i * 4 + 2] = 0;                        /* imaginary half zeroed */
        expect[i * 4 + 3] = 0;
    }
    assert(memcmp(&in.mem[f->out_addr], expect, sizeof(expect)) == 0);

    /* The equivalent framing from the contract, checked separately so a
     * self-consistent-but-wrong permutation cannot pass both. */
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int d = host_bitrev6(i);
        int16_t got = (int16_t)((uint16_t)in.mem[f->out_addr + d * 4] |
                                ((uint16_t)in.mem[f->out_addr + d * 4 + 1] << 8));
        assert(got == samples[i]);                    /* out[brev(i)].re == in[i] */
        assert(in.mem[f->out_addr + d * 4 + 2] == 0);
        assert(in.mem[f->out_addr + d * 4 + 3] == 0);
    }
    printf("  D. %-22s all 64 complex elements byte-exact vs host oracle\n", name);
    candidate_free(&c);
}

static void test_data_and_layout(void) {
    Fixture f; fixture_init(&f);
    int16_t s[DSP_FFT_SIZE];

    for (int i = 0; i < DSP_FFT_SIZE; i++) s[i] = 0;
    check_full_buffer(&f, s, "all zeros");

    for (int i = 0; i < DSP_FFT_SIZE; i++) s[i] = (int16_t)(i + 1);  /* distinct, non-zero */
    check_full_buffer(&f, s, "ramp 1..64");

    for (int i = 0; i < DSP_FFT_SIZE; i++) s[i] = (int16_t)(i * 517 - 16384);
    check_full_buffer(&f, s, "mixed signed");

    for (int i = 0; i < DSP_FFT_SIZE; i++) s[i] = (i & 1) ? INT16_MIN : INT16_MAX;
    check_full_buffer(&f, s, "alternating extremes");

    memset(s, 0, sizeof(s)); s[1] = INT16_MAX;   /* impulse at the index that moves furthest */
    check_full_buffer(&f, s, "impulse at i=1");

    /* Stride contract, read from the layout rather than assumed. */
    assert(sram_layout_elem_size(f.graph.ops[WINDOW_OP].dtype) == 2);
    assert(sram_layout_elem_size(f.graph.ops[BITREV_OP].dtype) == 4);
    assert(f.graph.ops[WINDOW_OP].dtype == DT_FIXED_Q15);
    assert(f.graph.ops[BITREV_OP].dtype == DT_COMPLEX_Q15);
    printf("  C. layout: input stride 2 (FIXED_Q15), output stride 4 (COMPLEX_Q15)\n");
    fixture_free(&f);
}

/* --- E. bounds --------------------------------------------------------- */
static void test_touches_nothing_outside_its_regions(void) {
    Fixture f; fixture_init(&f);
    int16_t s[DSP_FFT_SIZE];
    for (int i = 0; i < DSP_FFT_SIZE; i++) s[i] = (int16_t)(0x1111 + i);

    AvrInterp in; Candidate c;
    /* Fill all of SRAM with a sentinel, then check only the output region moved. */
    avr_interp_init(&in);
    memset(in.mem, 0xA5, AVR_INTERP_MEM_SIZE);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        in.mem[f.in_addr + i*2] = (uint8_t)((uint16_t)s[i] & 0xFF);
        in.mem[f.in_addr + i*2+1] = (uint8_t)(((uint16_t)s[i] >> 8) & 0xFF);
    }
    assert(lower_op(&f.graph, BITREV_OP, &f.layout, &f.regalloc, &g_cm, NULL, 0, &c) == 0);
    assert(avr_interp_run(&in, &c) == 0);

    size_t out_lo = f.out_addr, out_hi = f.out_addr + DSP_FFT_SIZE * 4;
    size_t touched_outside = 0;
    for (size_t a = 0x0100; a < AVR_INTERP_MEM_SIZE; a++) {
        if (a >= out_lo && a < out_hi) continue;
        if (a >= f.in_addr && a < (size_t)f.in_addr + DSP_FFT_SIZE * 2) continue; /* seeded input */
        if (in.mem[a] != 0xA5) touched_outside++;
    }
    printf("  E. bounds: wrote %zu bytes outside [0x%04X,0x%04X) (expect 0)\n",
           touched_outside, (unsigned)out_lo, (unsigned)out_hi);
    assert(touched_outside == 0);

    /* Every emitted absolute address lies inside the two regions. */
    for (size_t i = 0; i < c.num_instructions; i++) {
        const AvrInstr *ins = &c.instructions[i];
        int is_ld = !strcmp(ins->mnemonic, "lds"), is_st = !strcmp(ins->mnemonic, "sts");
        if (!is_ld && !is_st) continue;
        unsigned addr = (unsigned)strtoul(ins->operands[is_ld ? 1 : 0], NULL, 16);
        if (is_ld) assert(addr >= f.in_addr && addr < (unsigned)f.in_addr + DSP_FFT_SIZE * 2);
        else       assert(addr >= out_lo && addr < out_hi);
    }
    printf("  E. every emitted lds/sts address is inside its declared region\n");
    candidate_free(&c);
    fixture_free(&f);
}

/* --- F. cycle accounting ---------------------------------------------- */
static void test_predicted_cycles_match_interpreter(void) {
    Fixture f; fixture_init(&f);
    int16_t s[DSP_FFT_SIZE];
    for (int i = 0; i < DSP_FFT_SIZE; i++) s[i] = (int16_t)i;
    AvrInterp in; Candidate c;
    run_bitreverse(&f, s, &in, &c);
    printf("  F. cycles: compiler predicted %u, interpreter executed %lu, emitted %zu instructions\n",
           c.cycles, in.cycles, c.num_instructions);
    assert(c.cycles == in.cycles);
    /* 64 elements x (copy16: 2 lds + 2 sts, const16: 2 ldi + 2 sts) = 512 instructions */
    assert(c.num_instructions == 64 * 8);
    candidate_free(&c);
    fixture_free(&f);
}

int main(void) {
    load_costs();
    test_permutation_is_a_bijection();
    test_data_and_layout();
    test_touches_nothing_outside_its_regions();
    test_predicted_cycles_match_interpreter();
    printf("test_dsp_bitreverse: all tests passed\n");
    return 0;
}
