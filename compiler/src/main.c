#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/lower.h"
#include "optifine/codegen/program.h"
#include "optifine/codegen/regalloc.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/cost_model.h"
#include "optifine/ingest.h"
#include "optifine/ir.h"

static void usage(const char *argv0) {
    fprintf(stderr,
            "usage: %s <model.onnx> --cost-table <cost_table.toml> --out <out.s> "
            "[--input <golden_input.txt>] [--optimized]\n"
            "  --optimized  use codegen/candidates.c's real candidate diversity "
            "(milestone 5) instead of Phase A's naive single-candidate baseline\n",
            argv0);
}

/* Reads whitespace-separated decimal int8 values from `path` into a
 * malloc'd buffer, one line at a time, stripping `#`-prefixed comments
 * (same convention as cost_model.c's TOML parser). Returns the buffer
 * (caller frees) and sets *out_len, or returns NULL on failure. */
static int8_t *read_demo_input(const char *path, size_t *out_len) {
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "failed to open input file: %s\n", path);
        return NULL;
    }
    size_t cap = 64, len = 0;
    int8_t *buf = malloc(cap * sizeof(int8_t));
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';
        char *cursor = line;
        char *endptr;
        while (1) {
            long v = strtol(cursor, &endptr, 10);
            if (endptr == cursor) break; /* no more numbers on this line */
            if (v < -127 || v > 127) {
                fprintf(stderr, "input file %s: value %ld out of int8 range [-127,127]\n", path, v);
                free(buf);
                fclose(f);
                return NULL;
            }
            if (len == cap) {
                cap *= 2;
                buf = realloc(buf, cap * sizeof(int8_t));
            }
            buf[len++] = (int8_t)v;
            cursor = endptr;
        }
    }
    fclose(f);
    *out_len = len;
    return buf;
}

int main(int argc, char **argv) {
    const char *model_path = NULL;
    const char *cost_table_path = "cost_table.toml";
    const char *out_path = "out.s";
    const char *input_path = "models/tiny_classifier_golden_input.txt";
    int use_real_candidates = 0;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cost-table") == 0 && i + 1 < argc) {
            cost_table_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
        } else if (strcmp(argv[i], "--input") == 0 && i + 1 < argc) {
            input_path = argv[++i];
        } else if (strcmp(argv[i], "--optimized") == 0) {
            use_real_candidates = 1;
        } else if (!model_path) {
            model_path = argv[i];
        } else {
            usage(argv[0]);
            return 2;
        }
    }

    if (!model_path) {
        usage(argv[0]);
        return 2;
    }

    CostModel cost_model;
    if (cost_model_load(cost_table_path, &cost_model) != 0) {
        fprintf(stderr, "failed to load cost table: %s\n", cost_table_path);
        return 1;
    }

    IrGraph graph;
    ir_graph_init(&graph);
    if (ingest_load_onnx(model_path, &graph) != 0) {
        fprintf(stderr, "failed to ingest model: %s\n", model_path);
        ir_graph_free(&graph);
        return 1;
    }

    size_t demo_input_len = 0;
    int8_t *demo_input = read_demo_input(input_path, &demo_input_len);
    if (!demo_input) {
        ir_graph_free(&graph);
        return 1;
    }

    /* Must run before any codegen: refuses to compile if this specific
     * input would overflow int8 through any Requantize stage (see
     * lower.h/lower.c for why this checks the actual input rather than a
     * worst-case bound over all possible inputs). */
    if (lower_verify_demo_forward_pass(&graph, demo_input, demo_input_len) != 0) {
        fprintf(stderr, "failed to verify demo input against the model: %s\n", input_path);
        free(demo_input);
        ir_graph_free(&graph);
        return 1;
    }

    SramLayout layout;
    if (sram_layout_build(&graph, &layout) != 0) {
        free(demo_input);
        ir_graph_free(&graph);
        return 1;
    }

    RegAllocResult regalloc;
    /* Real next-use analysis (see regalloc.h) -- only meaningfully consulted
     * when --optimized routes through candidates_generate; the naive path
     * (lower_op) ignores it and always spills, matching Phase A. */
    regalloc_next_use(&graph, &regalloc);

    FILE *out = fopen(out_path, "w");
    if (!out) {
        fprintf(stderr, "failed to open output file: %s\n", out_path);
        free(demo_input);
        regalloc_result_free(&regalloc);
        sram_layout_free(&layout);
        ir_graph_free(&graph);
        return 1;
    }

    ProgramCost cost;
    int rc = codegen_emit_program(&graph, &layout, &regalloc, &cost_model,
                                   demo_input, demo_input_len, use_real_candidates, out, &cost);
    fclose(out);

    if (rc == 0) {
        fprintf(stderr, "prologue (const/input load): %u cycles, %.3f nJ (predicted)\n",
                cost.prologue_cycles, cost.prologue_energy_nj);
        fprintf(stderr, "inference body: %u cycles, %.3f nJ (predicted)\n",
                cost.body_cycles, cost.body_energy_nj);
        fprintf(stderr, "total: %u cycles, %.3f nJ (predicted; compare against a real "
                        "sim/run_avrora.sh run for the actual simulated numbers)\n",
                cost.prologue_cycles + cost.body_cycles, cost.prologue_energy_nj + cost.body_energy_nj);
    }

    free(demo_input);
    regalloc_result_free(&regalloc);
    sram_layout_free(&layout);
    ir_graph_free(&graph);
    return rc == 0 ? 0 : 1;
}
