#include "optifine/codegen/program.h"

#include <stdlib.h>

#include "optifine/codegen/candidates.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/select.h"
#include "optifine/emit.h"
#include "optifine/locality.h"

/* Picks the cheapest of `candidates[0..count)` via select_min_energy,
 * applies locality_optimize, emits it, and frees every candidate exactly
 * once (the winner is a shallow copy sharing its `instructions` pointer
 * with the original `candidates[]` slot select_min_energy pointed at, so
 * that slot must NOT also be freed separately). */
static int emit_best(Candidate *candidates, size_t count, FILE *out, double *energy_acc, uint32_t *cycles_acc) {
    const Candidate *best = select_min_energy(candidates, count);
    Candidate mutable_best = *best;
    locality_optimize(&mutable_best);

    int rc = emit_candidate(&mutable_best, out);
    *energy_acc += mutable_best.energy_nj;
    *cycles_acc += mutable_best.cycles;

    for (size_t i = 0; i < count; i++) {
        if (&candidates[i] != best) {
            candidate_free(&candidates[i]);
        }
    }
    candidate_free(&mutable_best);
    return rc;
}

#define PROGRAM_MAX_CANDIDATES 4

static int lower_one(const IrGraph *graph, size_t op_id, const SramLayout *layout,
                      const RegAllocResult *regalloc, const CostModel *cost_model,
                      const int8_t *demo_input, size_t demo_input_len,
                      int use_real_candidates, FILE *out,
                      double *energy_acc, uint32_t *cycles_acc) {
    if (!use_real_candidates) {
        Candidate c;
        if (lower_op(graph, op_id, layout, regalloc, cost_model, demo_input, demo_input_len, &c) != 0) {
            return -1;
        }
        return emit_best(&c, 1, out, energy_acc, cycles_acc);
    }

    Candidate candidates[PROGRAM_MAX_CANDIDATES];
    size_t count = candidates_generate(graph, op_id, layout, regalloc, cost_model,
                                        demo_input, demo_input_len,
                                        candidates, PROGRAM_MAX_CANDIDATES);
    if (count == 0) {
        return -1;
    }
    return emit_best(candidates, count, out, energy_acc, cycles_acc);
}

int codegen_emit_program(const IrGraph *graph, const SramLayout *layout,
                          const RegAllocResult *regalloc, const CostModel *cost_model,
                          const int8_t *demo_input, size_t demo_input_len,
                          int use_real_candidates,
                          FILE *out, ProgramCost *out_cost) {
    out_cost->prologue_energy_nj = 0.0;
    out_cost->prologue_cycles = 0;
    out_cost->body_energy_nj = 0.0;
    out_cost->body_cycles = 0;

    emit_program_prologue(out);

    Candidate zero_init;
    if (lower_init_zero_reg(cost_model, &zero_init) != 0) {
        return -1;
    }
    if (emit_best(&zero_init, 1, out, &out_cost->prologue_energy_nj, &out_cost->prologue_cycles) != 0) {
        return -1;
    }

    int boundary_written = 0;
    for (size_t i = 0; i < graph->count; i++) {
        int is_prologue = (graph->ops[i].kind == OP_INPUT || graph->ops[i].kind == OP_CONST);

        if (is_prologue) {
            if (lower_one(graph, i, layout, regalloc, cost_model, demo_input, demo_input_len,
                          use_real_candidates, out,
                          &out_cost->prologue_energy_nj, &out_cost->prologue_cycles) != 0) {
                fprintf(stderr, "codegen_emit_program: failed to lower op %zu\n", i);
                return -1;
            }
        } else {
            if (!boundary_written) {
                fprintf(out, "\n    ; ---- inference begins here ----\n");
                boundary_written = 1;
            }
            if (lower_one(graph, i, layout, regalloc, cost_model, demo_input, demo_input_len,
                          use_real_candidates, out,
                          &out_cost->body_energy_nj, &out_cost->body_cycles) != 0) {
                fprintf(stderr, "codegen_emit_program: failed to lower op %zu\n", i);
                return -1;
            }
        }
    }

    emit_program_epilogue(out);
    return 0;
}
