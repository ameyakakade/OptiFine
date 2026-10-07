#include "optifine/codegen/avr_instr.h"

#include <stdlib.h>

void candidate_free(Candidate *candidate) {
    free(candidate->instructions);
    candidate->instructions = NULL;
    candidate->num_instructions = 0;
}
