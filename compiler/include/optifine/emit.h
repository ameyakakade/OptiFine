/* Emits AVR assembly text for a selected candidate sequence, ready to be
 * handed to avr-gcc/avr-as (see sim/run_avrora.sh). */
#ifndef OPTIFINE_EMIT_H
#define OPTIFINE_EMIT_H

#include <stdio.h>

#include "optifine/codegen/candidates.h"

/* Writes `.s` text for `candidate` to `out`. Returns 0 on success. */
int emit_candidate(const Candidate *candidate, FILE *out);

/* Writes the fixed `.s` header (.arch/.section/.global/_start:) that every
 * program needs before its first real instruction. Returns 0 on success. */
int emit_program_prologue(FILE *out);

/* Writes the trailing `break` that halts Avrora's simulation. Not priced
 * through the cost model (matches the existing hand-written fixtures in
 * sim/fixtures/, none of which price their own `break` either) -- it is a
 * simulation-harness artifact, not part of a real deployed program's
 * energy budget. Returns 0 on success. */
int emit_program_epilogue(FILE *out);

#endif /* OPTIFINE_EMIT_H */
