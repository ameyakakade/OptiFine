#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/dsp_build.h"
#include "optifine/ir.h"

#include "avr_interp.h"

static void assert_priced(const Candidate *c) {
    assert(c->num_instructions > 0);
    for (size_t i = 0; i < c->num_instructions; i++) {
        assert(avr_cost_category(c->instructions[i].mnemonic) != NULL);
    }
    assert(c->energy_nj > 0.0);
    assert(c->cycles > 0);
}

/* Q15 <-> double, matching the convention every later task's golden
 * fixtures use: raw int16 v represents v/32768.0. */
static int16_t q15_of(double x) {
    long v = lround(x * 32768.0);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    return (int16_t)v;
}
static double double_of_q15(int16_t v) {
    return (double)v / 32768.0;
}

/* Writes a and b as Q15 values directly into interp->mem at fixed test
 * addresses, runs lower_fixed_mul_q15 through the interpreter, and
 * checks the Q15 product against a host double reference within 2 LSB
 * (this routine truncates rather than rounds -- see lower.c's comment on
 * lower_fixed_mul_q15 -- so a small, documented tolerance is expected,
 * not a bug). */
static void check_one_multiply(CostModel *cost_model, double a, double b) {
    AvrInterp interp;
    avr_interp_init(&interp);

    uint16_t a_addr = 0x0300, b_addr = 0x0302, out_addr = 0x0304;
    int16_t qa = q15_of(a), qb = q15_of(b);
    interp.mem[a_addr] = (uint8_t)(qa & 0xFF);
    interp.mem[a_addr + 1] = (uint8_t)((qa >> 8) & 0xFF);
    interp.mem[b_addr] = (uint8_t)(qb & 0xFF);
    interp.mem[b_addr + 1] = (uint8_t)((qb >> 8) & 0xFF);

    Candidate c;
    assert(lower_fixed_mul_q15_test_hook(a_addr, b_addr, out_addr, cost_model, &c) == 0);
    assert_priced(&c);
    assert(avr_interp_run(&interp, &c) == 0);

    int16_t raw_out = (int16_t)((uint16_t)interp.mem[out_addr] | ((uint16_t)interp.mem[out_addr + 1] << 8));
    double actual = double_of_q15(raw_out);
    double expected = a * b;
    double diff = actual - expected;
    if (diff < 0) diff = -diff;
    printf("q15 multiply %.5f * %.5f = %.5f (expected %.5f, diff %.6f)\n", a, b, actual, expected, diff);
    assert(diff < (2.0 / 32768.0));

    candidate_free(&c);
}

static void test_fixed_mul_q15_against_known_products(void) {
    CostModel cost_model;
    assert(cost_model_load(getenv("OPTIFINE_COST_TABLE") ? getenv("OPTIFINE_COST_TABLE") : "cost_table.toml",
                            &cost_model) == 0);

    check_one_multiply(&cost_model, 0.5, 0.5);
    check_one_multiply(&cost_model, -0.5, 0.5);
    check_one_multiply(&cost_model, -0.5, -0.5);
    check_one_multiply(&cost_model, 0.70710678, 0.70710678); /* cos(pi/4)^2 */
    check_one_multiply(&cost_model, 0.999969, 0.999969);     /* near the Q15 ceiling */
    check_one_multiply(&cost_model, 0.0, 0.5);
    check_one_multiply(&cost_model, -1.0, 1.0 - 1.0 / 32768.0);
}

static void test_lower_window_op(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);

    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    /* raw_input = op 0; write a known Q15 signal (all 0.25) directly into
     * its SRAM slot via a real lower_op(OP_INPUT) call, matching how the
     * real pipeline populates it. */
    int16_t signal[DSP_FFT_SIZE];
    for (int i = 0; i < DSP_FFT_SIZE; i++) signal[i] = q15_of(0.25);

    AvrInterp interp;
    avr_interp_init(&interp);

    Candidate zero_init;
    assert(lower_init_zero_reg(&cost_model, &zero_init) == 0);
    assert(avr_interp_run(&interp, &zero_init) == 0);
    candidate_free(&zero_init);

    Candidate input_c;
    assert(lower_op(&graph, 0, &layout, &regalloc, &cost_model,
                     (const int8_t *)signal, sizeof(signal), &input_c) == 0);
    assert_priced(&input_c);
    assert(avr_interp_run(&interp, &input_c) == 0);
    candidate_free(&input_c);

    Candidate const_c;
    assert(lower_op(&graph, 1, &layout, &regalloc, &cost_model, NULL, 0, &const_c) == 0);
    assert_priced(&const_c);
    assert(avr_interp_run(&interp, &const_c) == 0);
    candidate_free(&const_c);

    Candidate window_c;
    assert(lower_op(&graph, 2, &layout, &regalloc, &cost_model, NULL, 0, &window_c) == 0);
    assert_priced(&window_c);
    assert(avr_interp_run(&interp, &window_c) == 0);

    const int16_t *host_coeffs = (const int16_t *)graph.ops[1].data;
    uint16_t out_addr = sram_layout_addr(&layout, &graph, 2, 0);
    for (int i = 0; i < DSP_FFT_SIZE; i++) {
        int16_t raw = (int16_t)((uint16_t)interp.mem[out_addr + i * 2] |
                                 ((uint16_t)interp.mem[out_addr + i * 2 + 1] << 8));
        double actual = (double)raw / 32768.0;
        double expected = 0.25 * ((double)host_coeffs[i] / 32768.0);
        double diff = actual - expected;
        if (diff < 0) diff = -diff;
        assert(diff < (2.0 / 32768.0));
    }

    candidate_free(&window_c);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}

/* Regression guard for the OP_OUTPUT byte-width defect: lower_output used a
 * bare element count, so the DSP path's DT_FIXED_Q15 peak list (8 elements,
 * 2 bytes each) was copied 8 bytes instead of 16 -- exactly half, silently.
 * Asserting the emitted instruction count pins the stride so it cannot
 * revert without a test failure. */
static void test_lower_output_copies_full_q15_width(void) {
    CostModel cost_model;
    assert(cost_model_load("cost_table.toml", &cost_model) == 0);
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);
    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    size_t out_id = graph.count - 1;
    assert(graph.ops[out_id].kind == OP_OUTPUT);
    assert(graph.ops[out_id].dtype == DT_FIXED_Q15);
    size_t want_bytes = sram_layout_num_elements(&graph.ops[out_id]) *
                        sram_layout_elem_size(graph.ops[out_id].dtype);
    assert(want_bytes == DSP_MAX_PEAKS * 2);

    Candidate c;
    assert(lower_op(&graph, out_id, &layout, &regalloc, &cost_model, NULL, 0, &c) == 0);
    assert_priced(&c);
    /* one lds + one sts per byte copied */
    assert(c.num_instructions == want_bytes * 2);

    candidate_free(&c);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    test_fixed_mul_q15_against_known_products();
    test_lower_window_op();
    test_lower_output_copies_full_q15_width();
    printf("test_dsp_lower: all tests passed\n");
    return 0;
}
