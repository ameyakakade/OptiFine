#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/candidates.h"
#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/program.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/select.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/ingest.h"
#include "optifine/ir.h"
#include "optifine/locality.h"

#include "avr_interp.h"

#define PROGRAM_MAX_CANDIDATES 4

/* Every emitted instruction must resolve, through cost_category.c and the
 * real cost table, to a finite positive price. This is a structural check
 * (well-formedness), not a numeric-correctness check -- see the golden-
 * value comparison in run_and_check_golden for that. */
static void assert_priced(const Candidate *c) {
    assert(c->num_instructions > 0);
    for (size_t i = 0; i < c->num_instructions; i++) {
        const char *category = avr_cost_category(c->instructions[i].mnemonic);
        assert(category != NULL);
    }
    assert(isfinite(c->energy_nj));
    assert(c->energy_nj > 0.0);
    assert(c->cycles > 0);
}

/* Same whitespace-separated-decimal-int8, `#`-comment-stripping format as
 * compiler/src/main.c's read_demo_input (duplicated rather than shared --
 * this is a test binary, not linked against main.c). */
static int read_int8_file(const char *path, int8_t *out, size_t max_len, size_t *out_len) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "failed to open %s\n", path);
        return -1;
    }
    size_t len = 0;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *cursor = line;
        char *endptr;
        while (1) {
            long v = strtol(cursor, &endptr, 10);
            if (endptr == cursor) break;
            if (len >= max_len) {
                fprintf(stderr, "%s: more than %zu values\n", path, max_len);
                fclose(f);
                return -1;
            }
            out[len++] = (int8_t)v;
            cursor = endptr;
        }
    }
    fclose(f);
    *out_len = len;
    return 0;
}

/* Returns the complete emitted text from `stream`; the caller frees it.
 * This works with tmpfile() without imposing a named temporary-file policy
 * on the test environment. */
static char *read_stream(FILE *stream) {
    assert(fflush(stream) == 0);
    assert(fseek(stream, 0, SEEK_END) == 0);
    long length = ftell(stream);
    assert(length >= 0);
    assert(fseek(stream, 0, SEEK_SET) == 0);

    char *text = malloc((size_t)length + 1);
    assert(text != NULL);
    assert(fread(text, 1, (size_t)length, stream) == (size_t)length);
    text[length] = '\0';
    return text;
}

/* Catches an accidental return to a monolithic emitter, or a periodic
 * wrapper leaking standalone-only boilerplate into either reusable region. */
static void assert_reusable_classifier_regions(const IrGraph *graph, const SramLayout *layout,
                                                const RegAllocResult *regalloc,
                                                const CostModel *cost_model,
                                                const int8_t *demo_input,
                                                size_t demo_input_len) {
    FILE *init_out = tmpfile();
    FILE *body_out = tmpfile();
    assert(init_out != NULL);
    assert(body_out != NULL);

    ProgramRegionCost init = {0};
    ProgramRegionCost body = {0};
    EmitUnit unit;
    emit_unit_init(&unit);
    assert(codegen_emit_initialization(graph, layout, regalloc, cost_model,
                                       demo_input, demo_input_len, 1,
                                       &unit, init_out, &init) == 0);
    assert(codegen_emit_inference_body(graph, layout, regalloc, cost_model,
                                       demo_input, demo_input_len, 1,
                                       &unit, body_out, &body) == 0);
    assert(init.energy_nj > 0.0 && body.energy_nj > 0.0);
    assert(init.cycles > 0 && body.cycles > 0);

    char *init_text = read_stream(init_out);
    char *body_text = read_stream(body_out);
    assert(strstr(init_text, "sts") != NULL);
    assert(strstr(body_text, "muls") != NULL);
    assert(strstr(init_text, "_start:") == NULL);
    assert(strstr(body_text, "_start:") == NULL);
    assert(strstr(init_text, "break") == NULL);
    assert(strstr(body_text, "break") == NULL);

    free(init_text);
    free(body_text);
    fclose(init_out);
    fclose(body_out);
}

/* Catches a failed region leaving stale totals visible to a caller that
 * reuses ProgramCost across emission attempts. */
static void assert_program_cost_zeroed_on_region_failure(const IrGraph *graph,
                                                          const SramLayout *layout,
                                                          const RegAllocResult *regalloc,
                                                          const CostModel *cost_model,
                                                          const int8_t *demo_input,
                                                          size_t demo_input_len) {
    IrOp unsupported_op = graph->ops[0];
    unsupported_op.kind = (OpKind)999;
    IrGraph unsupported_graph = *graph;
    unsupported_graph.ops = &unsupported_op;
    unsupported_graph.count = 1;
    unsupported_graph.capacity = 1;

    FILE *out = tmpfile();
    assert(out != NULL);
    ProgramCost cost = {123.0, 456, 789.0, 1011};
    assert(codegen_emit_program(&unsupported_graph, layout, regalloc, cost_model,
                               demo_input, demo_input_len, 0, out, &cost) != 0);
    assert(cost.prologue_energy_nj == 0.0);
    assert(cost.prologue_cycles == 0);
    assert(cost.body_energy_nj == 0.0);
    assert(cost.body_cycles == 0);
    fclose(out);
}

/* Runs the full 13-op graph through either lower_op directly (the
 * naive baseline) or candidates_generate + select_min_energy +
 * locality_optimize (active-mode optimization), executes
 * every emitted instruction through the AVR interpreter, and checks the
 * final OP_OUTPUT bytes against the independently-computed golden
 * reference within +/-1 LSB. Both paths must produce a numerically
 * correct classifier -- the optimized path picking a cheaper instruction
 * sequence must never come at the cost of a wrong answer. */
static void run_and_check_golden(const char *label, int use_optimized,
                                  const IrGraph *graph, const SramLayout *layout,
                                  const RegAllocResult *regalloc, const CostModel *cost_model,
                                  const int8_t *demo_input, size_t demo_input_len,
                                  const int8_t *golden_output, size_t golden_len) {
    printf("=== %s ===\n", label);
    AvrInterp interp;
    avr_interp_init(&interp);

    Candidate zero_init;
    assert(lower_init_zero_reg(cost_model, &zero_init) == 0);
    assert_priced(&zero_init);
    assert(avr_interp_run(&interp, &zero_init) == 0);
    candidate_free(&zero_init);

    uint32_t total_cycles = 0;
    double total_energy = 0.0;

    for (size_t i = 0; i < graph->count; i++) {
        Candidate candidates[PROGRAM_MAX_CANDIDATES];
        size_t count;
        if (use_optimized) {
            count = candidates_generate(graph, i, layout, regalloc, cost_model,
                                         demo_input, demo_input_len,
                                         candidates, PROGRAM_MAX_CANDIDATES);
            assert(count >= 1);
        } else {
            assert(lower_op(graph, i, layout, regalloc, cost_model, demo_input, demo_input_len, &candidates[0]) == 0);
            count = 1;
        }
        for (size_t c = 0; c < count; c++) assert_priced(&candidates[c]);

        const Candidate *best = select_min_energy(candidates, count);
        Candidate mutable_best = *best;
        locality_optimize(&mutable_best);

        printf("op %zu (kind=%d): %zu candidate(s), picked %zu instructions, %u cycles, %.3f nJ\n",
               i, (int)graph->ops[i].kind, count, mutable_best.num_instructions,
               mutable_best.cycles, mutable_best.energy_nj);

        assert(avr_interp_run(&interp, &mutable_best) == 0);
        total_cycles += mutable_best.cycles;
        total_energy += mutable_best.energy_nj;

        for (size_t c = 0; c < count; c++) {
            if (&candidates[c] != best) candidate_free(&candidates[c]);
        }
        candidate_free(&mutable_best);
    }
    printf("%s total: %u cycles, %.3f nJ\n", label, total_cycles, total_energy);

    size_t output_op_id = graph->count - 1;
    assert(graph->ops[output_op_id].kind == OP_OUTPUT);
    size_t n = sram_layout_num_elements(&graph->ops[output_op_id]);
    assert(n == golden_len);

    printf("%s golden comparison (actual vs. gen_golden.py reference):\n", label);
    for (size_t k = 0; k < n; k++) {
        uint16_t addr = sram_layout_addr(layout, graph, output_op_id, k);
        int actual = (int8_t)interp.mem[addr];
        int expected = golden_output[k];
        int diff = actual - expected;
        if (diff < 0) diff = -diff;
        printf("  [%zu] actual=%d expected=%d (diff=%d)\n", k, actual, expected, diff);
        assert(diff <= 1);
    }
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "usage: %s <model.onnx> <cost_table.toml> <golden_input.txt> <golden_output.txt>\n", argv[0]);
        return 2;
    }

    CostModel cost_model;
    assert(cost_model_load(argv[2], &cost_model) == 0);

    IrGraph graph;
    assert(ingest_load_onnx(argv[1], &graph) == 0);
    assert(graph.count == 13);

    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);
    printf("sram layout: %u bytes used\n", layout.bytes_used);

    RegAllocResult regalloc;
    assert(regalloc_next_use(&graph, &regalloc) == 0);

    int8_t demo_input[64];
    size_t demo_input_len = 0;
    assert(read_int8_file(argv[3], demo_input, 64, &demo_input_len) == 0);
    assert(demo_input_len == 16);

    assert_reusable_classifier_regions(&graph, &layout, &regalloc, &cost_model,
                                       demo_input, demo_input_len);
    assert_program_cost_zeroed_on_region_failure(&graph, &layout, &regalloc, &cost_model,
                                                  demo_input, demo_input_len);

    /* This exact input was chosen (see models/tiny_classifier_golden_input.txt's
     * header) to actually pass both Requantize stages for this model's real
     * calibrated scales -- a worst-case-magnitude input like all-127s
     * reliably overflows even a mathematically correct int8 pipeline
     * (MinMax calibration cannot bound arbitrary inputs, see lower_requantize.c). */
    assert(lower_verify_demo_forward_pass(&graph, demo_input, demo_input_len) == 0);

    int8_t golden_output[64];
    size_t golden_len = 0;
    assert(read_int8_file(argv[4], golden_output, 64, &golden_len) == 0);

    run_and_check_golden("naive", 0, &graph, &layout, &regalloc, &cost_model,
                          demo_input, demo_input_len, golden_output, golden_len);
    run_and_check_golden("optimized", 1, &graph, &layout, &regalloc, &cost_model,
                          demo_input, demo_input_len, golden_output, golden_len);

    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);

    printf("test_lower: all tests passed\n");
    return 0;
}
