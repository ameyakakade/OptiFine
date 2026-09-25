/* Basic-block cost comparison. Classical peephole optimization,
 * with the comparator swapped from cycle count to energy_nj. */
#ifndef OPTIFINE_CODEGEN_SELECT_H
#define OPTIFINE_CODEGEN_SELECT_H

#include <stddef.h>

#include "optifine/codegen/candidates.h"

/* Returns a pointer into `candidates` (does not copy or take ownership). */
const Candidate *select_min_energy(const Candidate *candidates, size_t count);

#endif /* OPTIFINE_CODEGEN_SELECT_H */
