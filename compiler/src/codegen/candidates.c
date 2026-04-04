#include "optifine/codegen/candidates.h"

#include <stdlib.h>

void candidate_free(Candidate *candidate) {
    free(candidate->instructions);
    candidate->instructions = NULL;
    candidate->num_instructions = 0;
}

size_t candidates_generate(const IrOp *op, Candidate *out_candidates, size_t max_candidates) {
    (void)op;
    (void)out_candidates;
    (void)max_candidates;
    /* TODO(milestone 4): for each OpKind, emit >=2 provably-equivalent
     * instruction sequences (differing register/memory access pattern,
     * addressing mode, or operand order), each priced via cost_model.c. */
    return 0;
}
