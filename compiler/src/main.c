#include <stdio.h>
#include <string.h>

#include "optifine/cost_model.h"
#include "optifine/emit.h"
#include "optifine/ingest.h"
#include "optifine/ir.h"

static void usage(const char *argv0) {
    fprintf(stderr, "usage: %s <model.onnx> --cost-table <cost_table.toml> --out <out.s>\n", argv0);
}

int main(int argc, char **argv) {
    const char *model_path = NULL;
    const char *cost_table_path = "cost_table.toml";
    const char *out_path = "out.s";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--cost-table") == 0 && i + 1 < argc) {
            cost_table_path = argv[++i];
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            out_path = argv[++i];
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

    /* TODO(milestone 4/5): run candidate generation + regalloc + select
     * over `graph`, then locality_optimize, then emit_candidate to
     * `out_path`. See sim/run_avrora.sh for what happens after emission. */
    (void)out_path;

    ir_graph_free(&graph);
    return 0;
}
