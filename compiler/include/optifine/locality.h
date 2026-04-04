/* Module 7: memory locality pass. SRAM access costs meaningfully more
 * energy than register arithmetic on AVR-class hardware -- reorder
 * instructions / restructure loop tiling within a basic block to maximize
 * register/data reuse and reduce redundant loads. */
#ifndef OPTIFINE_LOCALITY_H
#define OPTIFINE_LOCALITY_H

#include "optifine/codegen/candidates.h"

/* Reorders `candidate`'s instructions in place to improve register reuse. */
void locality_optimize(Candidate *candidate);

#endif /* OPTIFINE_LOCALITY_H */
