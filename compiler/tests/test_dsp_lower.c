#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/lower.h"
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

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    test_fixed_mul_q15_against_known_products();
    printf("test_dsp_lower: all tests passed\n");
    return 0;
}
