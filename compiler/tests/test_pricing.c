/* Cycle accounting is independent of energy pricing.
 *
 * Under the current cost table every entry is cycles x one per-cycle
 * constant, so energy is proportional to cycles -- the first part checks that
 * consistency opcode by opcode. The second part prices the same DSP and ML
 * programs under a table whose energies are deliberately not proportional to
 * cycles: every op's cycle count must be unchanged and its energy must move.
 *
 * argv: <tiny_classifier.onnx> <cost_table.toml> <scratch dir> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/reuse_analysis.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/ingest.h"

#define CURRENT_NJ_PER_CYCLE 2.8375

static const char *kEmitted[] = {
    "add", "adc", "sub", "sbc", "subi", "sbci", "and", "eor", "com", "clr", "lsl", "lsr", "rol", "ror",
    "asr", "dec", "cp", "cpc", "mov", "movw", "ldi", "mul", "muls", "mulsu", "adiw", "sbiw", "lds", "sts",
    "ld", "ldd", "st", "std", "lpm", "brne", "breq", "rjmp", "break",
};

/* Prices every op of `g` (lower_op, the naive lowering) into cycles[] and
 * energy[]. */
static void price_graph(const IrGraph *g, const CostModel *cm, const int8_t *input, size_t input_len,
                        uint32_t *cycles, double *energy) {
    SramLayout layout;
    ReuseAnalysis reuse;
    assert(sram_layout_build(g, &layout) == 0);
    assert(reuse_analyze(g, &reuse) == 0);
    for (size_t i = 0; i < g->count; i++) {
        Candidate c;
        assert(lower_op(g, i, &layout, &reuse, cm, input, input_len, &c) == 0);
        cycles[i] = c.cycles;
        energy[i] = c.energy_nj;
        candidate_free(&c);
    }
    reuse_analysis_free(&reuse);
    sram_layout_free(&layout);
}

static void compare(const char *what, const IrGraph *g, const CostModel *real, const CostModel *skewed,
                    const int8_t *input, size_t input_len) {
    uint32_t c_real[64], c_skew[64];
    double e_real[64], e_skew[64];
    assert(g->count <= 64);
    price_graph(g, real, input, input_len, c_real, e_real);
    price_graph(g, skewed, input, input_len, c_skew, e_skew);
    unsigned long total = 0;
    int energy_moved = 0;
    for (size_t i = 0; i < g->count; i++) {
        assert(c_real[i] == c_skew[i]);
        /* the current table: energy is exactly cycles x the constant */
        assert((long long)(e_real[i] / CURRENT_NJ_PER_CYCLE + 0.5) == (long long)c_real[i]);
        if (e_real[i] != e_skew[i]) energy_moved = 1;
        total += c_real[i];
    }
    assert(energy_moved);
    printf("  %s: %zu ops, %lu cycles identical under both tables; energy differs\n", what, g->count, total);
}

int main(int argc, char **argv) {
    assert(argc == 4);
    CostModel real, skewed;
    assert(cost_model_load(argv[2], &real) == 0);

    for (size_t i = 0; i < sizeof(kEmitted) / sizeof(kEmitted[0]); i++) {
        const char *m = kEmitted[i];
        int cycles = avr_instr_cycles(m);
        assert(cycles >= 1 && cycles <= 3);
        int direct = avr_direct_cycles(m);
        if (direct) {
            assert(direct == cycles);
        } else {
            const CostEntry *e = cost_model_lookup(&real, avr_cost_category(m));
            assert(e && e->energy_nj == CURRENT_NJ_PER_CYCLE * cycles);
        }
    }
    assert(avr_instr_cycles("nop_not_emitted") == 0);
    printf("  %zu emitted opcodes: cycle table and current energy table agree\n",
           sizeof(kEmitted) / sizeof(kEmitted[0]));

    /* Energies that no cycle count could produce. */
    char path[1024];
    snprintf(path, sizeof(path), "%s/pricing_skewed.toml", argv[3]);
    FILE *f = fopen(path, "w");
    assert(f);
    fputs("[instructions]\n"
          "ADD = { energy_nj = 1.0, source = \"test\" }\n"
          "SUB = { energy_nj = 1.5, source = \"test\" }\n"
          "MOV = { energy_nj = 0.25, source = \"test\" }\n"
          "LDI = { energy_nj = 0.75, source = \"test\" }\n"
          "MUL = { energy_nj = 40.0, source = \"test\" }\n"
          "LD_SRAM = { energy_nj = 9.0, source = \"test\" }\n"
          "ST_SRAM = { energy_nj = 11.0, source = \"test\" }\n"
          "FIXED_MUL_Q15 = { energy_nj = 3.0, source = \"test\" }\n"
          "COMPLEX_ADD = { energy_nj = 2.0, source = \"test\" }\n",
          f);
    fclose(f);
    assert(cost_model_load(path, &skewed) == 0);

    IrGraph dsp;
    assert(dsp_build_pipeline(&dsp) == 0);
    int8_t samples[DSP_FFT_SIZE * 2];
    for (size_t i = 0; i < sizeof(samples); i++) samples[i] = (int8_t)(i * 37);
    compare("DSP pipeline", &dsp, &real, &skewed, samples, sizeof(samples));
    ir_graph_free(&dsp);

    IrGraph ml;
    assert(ingest_load_onnx(argv[1], &ml) == 0);
    int8_t input[16] = {0};
    compare("ML classifier", &ml, &real, &skewed, input, sizeof(input));
    ir_graph_free(&ml);

    printf("test_pricing: all tests passed\n");
    return 0;
}
