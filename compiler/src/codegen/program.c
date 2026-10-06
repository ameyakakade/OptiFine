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
static int emit_best(Candidate *candidates, size_t count, EmitUnit *unit, FILE *out,
                     double *energy_acc, uint32_t *cycles_acc) {
    const Candidate *best = select_min_energy(candidates, count);
    Candidate mutable_best = *best;
    locality_optimize(&mutable_best);

    int rc = emit_candidate(unit, &mutable_best, out);
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
                      const ReuseAnalysis *reuse, const CostModel *cost_model,
                      const int8_t *demo_input, size_t demo_input_len,
                      int use_real_candidates, EmitUnit *unit, FILE *out,
                      double *energy_acc, uint32_t *cycles_acc) {
    if (!use_real_candidates) {
        Candidate c;
        if (lower_op(graph, op_id, layout, reuse, cost_model, demo_input, demo_input_len, &c) != 0) {
            return -1;
        }
        return emit_best(&c, 1, unit, out, energy_acc, cycles_acc);
    }

    Candidate candidates[PROGRAM_MAX_CANDIDATES];
    size_t count = candidates_generate(graph, op_id, layout, reuse, cost_model,
                                        demo_input, demo_input_len,
                                        candidates, PROGRAM_MAX_CANDIDATES);
    if (count == 0) {
        return -1;
    }
    return emit_best(candidates, count, unit, out, energy_acc, cycles_acc);
}

int codegen_emit_initialization(const IrGraph *graph, const SramLayout *layout,
                                const ReuseAnalysis *reuse, const CostModel *cost_model,
                                const int8_t *demo_input, size_t demo_input_len,
                                int use_real_candidates,
                                EmitUnit *unit, FILE *out, ProgramRegionCost *out_cost) {
    out_cost->energy_nj = 0.0;
    out_cost->cycles = 0;

    Candidate zero_init;
    if (lower_init_zero_reg(cost_model, &zero_init) != 0) {
        return -1;
    }
    if (emit_best(&zero_init, 1, unit, out, &out_cost->energy_nj, &out_cost->cycles) != 0) {
        return -1;
    }

    for (size_t i = 0; i < graph->count; i++) {
        int is_prologue = (graph->ops[i].kind == OP_INPUT || graph->ops[i].kind == OP_CONST);
        if (is_prologue) {
            if (lower_one(graph, i, layout, reuse, cost_model, demo_input, demo_input_len,
                          use_real_candidates, unit, out,
                          &out_cost->energy_nj, &out_cost->cycles) != 0) {
                fprintf(stderr, "codegen_emit_initialization: failed to lower op %zu\n", i);
                return -1;
            }
        }
    }
    return 0;
}

int codegen_emit_inference_body(const IrGraph *graph, const SramLayout *layout,
                                const ReuseAnalysis *reuse, const CostModel *cost_model,
                                const int8_t *demo_input, size_t demo_input_len,
                                int use_real_candidates,
                                EmitUnit *unit, FILE *out, ProgramRegionCost *out_cost) {
    out_cost->energy_nj = 0.0;
    out_cost->cycles = 0;

    int boundary_written = 0;
    for (size_t i = 0; i < graph->count; i++) {
        int is_prologue = (graph->ops[i].kind == OP_INPUT || graph->ops[i].kind == OP_CONST);
        if (!is_prologue) {
            if (!boundary_written) {
                fprintf(out, "\n    ; ---- inference begins here ----\n");
                boundary_written = 1;
            }
            if (lower_one(graph, i, layout, reuse, cost_model, demo_input, demo_input_len,
                          use_real_candidates, unit, out,
                          &out_cost->energy_nj, &out_cost->cycles) != 0) {
                fprintf(stderr, "codegen_emit_inference_body: failed to lower op %zu\n", i);
                return -1;
            }
        }
    }

    return 0;
}

int codegen_emit_program(const IrGraph *graph, const SramLayout *layout,
                          const ReuseAnalysis *reuse, const CostModel *cost_model,
                          const int8_t *demo_input, size_t demo_input_len,
                          int use_real_candidates,
                          FILE *out, ProgramCost *out_cost) {
    out_cost->prologue_energy_nj = 0.0;
    out_cost->prologue_cycles = 0;
    out_cost->body_energy_nj = 0.0;
    out_cost->body_cycles = 0;

    ProgramRegionCost initialization = {0};
    ProgramRegionCost inference_body = {0};

    EmitUnit unit;
    emit_unit_init(&unit);
    emit_program_prologue(out);
    if (codegen_emit_initialization(graph, layout, reuse, cost_model,
                                    demo_input, demo_input_len, use_real_candidates,
                                    &unit, out, &initialization) != 0) {
        return -1;
    }
    if (codegen_emit_inference_body(graph, layout, reuse, cost_model,
                                    demo_input, demo_input_len, use_real_candidates,
                                    &unit, out, &inference_body) != 0) {
        return -1;
    }

    out_cost->prologue_energy_nj = initialization.energy_nj;
    out_cost->prologue_cycles = initialization.cycles;
    out_cost->body_energy_nj = inference_body.energy_nj;
    out_cost->body_cycles = inference_body.cycles;

    emit_program_epilogue(out);
    return 0;
}

int codegen_emit_constant_data(const IrGraph *graph, const CostModel *cost_model,
                               EmitUnit *unit, FILE *out) {
    Candidate data;
    if (lower_dsp_constant_data(graph, cost_model, &data) != 0) {
        return -1;
    }
    int rc = 0;
    if (data.num_instructions > 0) {
        fprintf(out, "\n; ---- constant data (program memory, never executed) ----\n");
        rc = emit_candidate(unit, &data, out);
    }
    candidate_free(&data);
    return rc;
}

int codegen_emit_dsp_program(const IrGraph *graph, const SramLayout *layout,
                             const ReuseAnalysis *reuse, const CostModel *cost_model,
                             const int8_t *input_bytes, size_t input_len,
                             FILE *out, DspProgramCost *out_cost) {
    ProgramRegionCost zero = {0};
    out_cost->initialization = zero;
    out_cost->body = zero;
    out_cost->termination = zero;

    EmitUnit unit;
    emit_unit_init(&unit);
    emit_program_prologue(out);
    if (codegen_emit_initialization(graph, layout, reuse, cost_model, input_bytes, input_len, 0,
                                    &unit, out, &out_cost->initialization) != 0) {
        return -1;
    }
    if (codegen_emit_inference_body(graph, layout, reuse, cost_model, input_bytes, input_len, 0,
                                    &unit, out, &out_cost->body) != 0) {
        return -1;
    }
    Candidate end;
    if (lower_dsp_program_end(graph, cost_model, &end) != 0) {
        return -1;
    }
    fprintf(out, "\n    ; ---- program end: break, then constant data ----\n");
    return emit_best(&end, 1, &unit, out, &out_cost->termination.energy_nj, &out_cost->termination.cycles);
}
