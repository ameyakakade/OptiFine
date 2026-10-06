/* FFT Stage 0 validation -- the first full integration of counted loops, LPM
 * twiddle loading, complex Q15 layout, butterfly arithmetic, stage scaling and
 * exact cost accounting.
 *
 * The host oracle below is written from the equations in lower_fft.c's comment,
 * using the same Q15 definitions, and is compared against the interpreter over
 * the entire 64-element complex buffer for every vector. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/ir.h"

#include "avr_interp.h"

#define BITREV_OP 3
#define STAGE0_OP 4

static CostModel g_cm;
static void load_costs(void) {
    const char *p = getenv("OPTIFINE_COST_TABLE");
    assert(cost_model_load(p ? p : "cost_table.toml", &g_cm) == 0);
}

/* --- host oracle, from lower_fft.c's stated equations --------------------- */
static int16_t h_qmul(int16_t x, int16_t y) { return (int16_t)(((int32_t)x * (int32_t)y) >> 15); }
static int16_t h_asr(int16_t v)             { return (int16_t)(v >> 1); }
static int16_t h_add(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a + (uint16_t)b); }
static int16_t h_sub(int16_t a, int16_t b)  { return (int16_t)((uint16_t)a - (uint16_t)b); }

/* Stage 0: 32 blocks, half_block 1, so butterflies act on adjacent pairs and
 * every twiddle is W^0. */
static void host_stage0(const int16_t *in_re, const int16_t *in_im,
                        int16_t *out_re, int16_t *out_im) {
    int16_t wr, wi;
    dsp_twiddle_q15(0, &wr, &wi);
    for (int b = 0; b < 32; b++) {
        int p = b * 2, q = p + 1;
        int16_t t_re = h_sub(h_qmul(wr, in_re[q]), h_qmul(wi, in_im[q]));
        int16_t t_im = h_add(h_qmul(wr, in_im[q]), h_qmul(wi, in_re[q]));
        int16_t ph = h_asr(in_re[p]), th = h_asr(t_re);
        out_re[p] = h_add(ph, th);
        out_re[q] = h_sub(ph, th);
        ph = h_asr(in_im[p]); th = h_asr(t_im);
        out_im[p] = h_add(ph, th);
        out_im[q] = h_sub(ph, th);
    }
}

typedef struct { IrGraph graph; SramLayout layout; RegAllocResult ra; uint16_t in_addr, out_addr; } Fx;
static void fx_init(Fx *f) {
    assert(dsp_build_pipeline(&f->graph) == 0);
    assert(sram_layout_build(&f->graph, &f->layout) == 0);
    assert(regalloc_next_use(&f->graph, &f->ra) == 0);
    assert(f->graph.ops[STAGE0_OP].kind == OP_FFT_BUTTERFLY);
    f->in_addr = sram_layout_addr(&f->layout, &f->graph, BITREV_OP, 0);
    f->out_addr = sram_layout_addr(&f->layout, &f->graph, STAGE0_OP, 0);
}
static void fx_free(Fx *f) { regalloc_result_free(&f->ra); sram_layout_free(&f->layout); ir_graph_free(&f->graph); }

static void check_vector(Fx *f, const int16_t *re, const int16_t *im, const char *name) {
    Candidate c;
    assert(lower_fft_stage0_test_hook(&f->graph, STAGE0_OP, &f->layout, &g_cm, &c) == 0);
    AvrInterp in; avr_interp_init(&in);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        in.mem[f->in_addr + i*4    ] = (uint8_t)((uint16_t)re[i] & 0xFF);
        in.mem[f->in_addr + i*4 + 1] = (uint8_t)(((uint16_t)re[i] >> 8) & 0xFF);
        in.mem[f->in_addr + i*4 + 2] = (uint8_t)((uint16_t)im[i] & 0xFF);
        in.mem[f->in_addr + i*4 + 3] = (uint8_t)(((uint16_t)im[i] >> 8) & 0xFF);
    }
    assert(avr_interp_run(&in, &c) == 0);

    int16_t ore[DSP_FFT_SIZE], oim[DSP_FFT_SIZE];
    host_stage0(re, im, ore, oim);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int16_t gre = (int16_t)((uint16_t)in.mem[f->out_addr + i*4] |
                                ((uint16_t)in.mem[f->out_addr + i*4+1] << 8));
        int16_t gim = (int16_t)((uint16_t)in.mem[f->out_addr + i*4+2] |
                                ((uint16_t)in.mem[f->out_addr + i*4+3] << 8));
        if (gre != ore[i] || gim != oim[i]) {
            printf("  %s MISMATCH at %d: got (%d,%d) want (%d,%d)\n", name, i, gre, gim, ore[i], oim[i]);
        }
        assert(gre == ore[i] && gim == oim[i]);
    }
    printf("  %-26s all 64 complex elements exact\n", name);
    candidate_free(&c);
}

static void test_vectors(void) {
    Fx f; fx_init(&f);
    int16_t re[DSP_FFT_SIZE], im[DSP_FFT_SIZE];

    memset(re,0,sizeof(re)); memset(im,0,sizeof(im));
    check_vector(&f, re, im, "all zeros");

    memset(re,0,sizeof(re)); memset(im,0,sizeof(im)); re[0] = 32767;
    check_vector(&f, re, im, "impulse at 0");
    memset(re,0,sizeof(re)); re[1] = 32767;
    check_vector(&f, re, im, "impulse at 1 (q side)");

    for (int i=0;i<DSP_FFT_SIZE;i++){re[i]=8192;im[i]=0;}
    check_vector(&f, re, im, "DC constant");

    for (int i=0;i<DSP_FFT_SIZE;i++){
        double a = 2.0*3.14159265358979323846*5.0*i/64.0;
        re[i]=(int16_t)(0.4*32767.0*cos(a)); im[i]=(int16_t)(0.4*32767.0*sin(a));
    }
    check_vector(&f, re, im, "single-bin tone (k=5)");

    for (int i=0;i<DSP_FFT_SIZE;i++){ re[i]=(i&1)?INT16_MIN:INT16_MAX; im[i]=(i&1)?INT16_MAX:INT16_MIN; }
    check_vector(&f, re, im, "alternating extremes");

    for (int i=0;i<DSP_FFT_SIZE;i++){ re[i]=(int16_t)(i*1031-16384); im[i]=(int16_t)(-i*617+8192); }
    check_vector(&f, re, im, "deterministic mixed");

    for (int i=0;i<DSP_FFT_SIZE;i++){ re[i]=(int16_t)(-1-i); im[i]=(int16_t)(1+i); }
    check_vector(&f, re, im, "negative real, positive imag");

    const int16_t edge[] = {0,1,-1,INT16_MAX,INT16_MIN,16384,-16384,32766,-32767};
    for (int i=0;i<DSP_FFT_SIZE;i++){ re[i]=edge[i%9]; im[i]=edge[(i+4)%9]; }
    check_vector(&f, re, im, "Q15 boundary values");
    fx_free(&f);
}

static void test_cycles_and_structure(void) {
    Fx f; fx_init(&f);
    Candidate c;
    assert(lower_fft_stage0_test_hook(&f.graph, STAGE0_OP, &f.layout, &g_cm, &c) == 0);
    int16_t re[DSP_FFT_SIZE], im[DSP_FFT_SIZE];
    for (int i=0;i<DSP_FFT_SIZE;i++){ re[i]=(int16_t)(i*211); im[i]=(int16_t)(-i*97); }
    AvrInterp in; avr_interp_init(&in);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        in.mem[f.in_addr+i*4  ]=(uint8_t)((uint16_t)re[i]&0xFF);
        in.mem[f.in_addr+i*4+1]=(uint8_t)(((uint16_t)re[i]>>8)&0xFF);
        in.mem[f.in_addr+i*4+2]=(uint8_t)((uint16_t)im[i]&0xFF);
        in.mem[f.in_addr+i*4+3]=(uint8_t)(((uint16_t)im[i]>>8)&0xFF);
    }
    assert(avr_interp_run(&in, &c) == 0);
    printf("  emitted %zu instructions, predicted %u cycles, interpreter executed %lu\n",
           c.num_instructions, c.cycles, in.cycles);
    assert(c.cycles == in.cycles);

    /* table placement: after break, never branched to, and the interpreter
     * refuses to execute a data word if control ever reached it. */
    size_t brk = (size_t)-1, lbl = (size_t)-1;
    for (size_t i = 0; i < c.num_instructions; i++) {
        const AvrInstr *ins = &c.instructions[i];
        if (!strcmp(ins->mnemonic,"break") && brk==(size_t)-1) brk=i;
        if (avr_instr_is_label(ins) && !strcmp(ins->operands[0],".Ltw")) lbl=i;
        if (!strcmp(ins->mnemonic,"brne")) assert(strcmp(ins->operands[0],".Ltw") != 0);
    }
    assert(brk != (size_t)-1 && lbl != (size_t)-1 && lbl > brk);
    printf("  twiddle table follows break at index %zu (break at %zu), no branch targets it\n", lbl, brk);
    candidate_free(&c);
    fx_free(&f);
}

int main(void) {
    load_costs();
    test_vectors();
    test_cycles_and_structure();
    printf("test_dsp_fft_stage0: all tests passed\n");
    return 0;
}
