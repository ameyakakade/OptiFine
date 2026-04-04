#include "optifine/emit.h"

int emit_candidate(const Candidate *candidate, FILE *out) {
    for (size_t i = 0; i < candidate->num_instructions; i++) {
        const AvrInstr *instr = &candidate->instructions[i];
        fprintf(out, "    %s", instr->mnemonic);
        for (int j = 0; j < instr->num_operands; j++) {
            fprintf(out, "%s%s", j == 0 ? " " : ", ", instr->operands[j]);
        }
        fprintf(out, "\n");
    }
    return 0;
}
