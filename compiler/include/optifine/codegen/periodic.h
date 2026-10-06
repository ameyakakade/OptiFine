#ifndef OPTIFINE_CODEGEN_PERIODIC_H
#define OPTIFINE_CODEGEN_PERIODIC_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "optifine/codegen/program.h"
#include "optifine/codegen/sram_layout.h"

typedef enum { WAIT_ACTIVE = 0, WAIT_POWER_SAVE = 1 } WaitPolicy;

typedef struct {
    WaitPolicy policy;
    uint16_t timer_prescaler;
    uint8_t inference_count;
    int use_real_candidates;
} PeriodicOptions;

typedef struct {
    ProgramRegionCost initialization;
    ProgramRegionCost inference;
    uint16_t tick_addr, running_addr, overrun_addr, completed_addr;
} PeriodicProgramCost;

/* Validates a Timer0 periodic scheduling request. Only the Timer0 divisors
 * supported by the ATmega128 encoding (8, 32, 128, and 1024) are accepted,
 * and periodic execution must request at least one inference. */
int periodic_options_validate(const PeriodicOptions *options);

/* Validates the scheduling request, reserves four scheduler bytes after
 * the tensor layout, and emits the matched busy-wait/Power-save AVR
 * wrapper. */
int codegen_emit_periodic_program(const IrGraph *graph, const SramLayout *layout,
                                  const ReuseAnalysis *reuse, const CostModel *cost_model,
                                  const int8_t *demo_input, size_t demo_input_len,
                                  const PeriodicOptions *options,
                                  FILE *out, PeriodicProgramCost *out_cost);

#endif /* OPTIFINE_CODEGEN_PERIODIC_H */
