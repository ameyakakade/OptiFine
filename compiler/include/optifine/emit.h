/* Emits AVR assembly text for a selected candidate sequence, ready to be
 * handed to avr-gcc/avr-as (see sim/run_avrora.sh). */
#ifndef OPTIFINE_EMIT_H
#define OPTIFINE_EMIT_H

#include <stdio.h>

#include "optifine/codegen/candidates.h"

/* Writes `.s` text for `candidate` to `out`. Returns 0 on success. */
int emit_candidate(const Candidate *candidate, FILE *out);

#endif /* OPTIFINE_EMIT_H */
