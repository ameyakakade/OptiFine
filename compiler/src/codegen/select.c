#include "optifine/codegen/select.h"

const Candidate *select_min_energy(const Candidate *candidates, size_t count) {
    if (count == 0) {
        return NULL;
    }

    const Candidate *best = &candidates[0];
    for (size_t i = 1; i < count; i++) {
        if (candidates[i].energy_nj < best->energy_nj) {
            best = &candidates[i];
        }
    }
    return best;
}
