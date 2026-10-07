/* Emits AVR assembly text for a selected candidate sequence, ready to be
 * handed to avr-gcc/avr-as (see sim/run_avrora.sh). */
#ifndef OPTIFINE_EMIT_H
#define OPTIFINE_EMIT_H

#include <stdio.h>

#include "optifine/codegen/avr_instr.h"

/* One assembly unit (one .s file). Every candidate emitted into the same
 * unit shares its label namespace: candidate-local loop labels are renumbered
 * past those already emitted (see instr_buf.h), and a global label defined
 * twice is refused rather than left for the assembler to reject. Numbering
 * depends only on emission order, so the output is deterministic. A program
 * with a single labelled candidate keeps that candidate's label text as-is.
 *
 * "Global" here means every label that is not renumbered: program-memory
 * data labels, and the generic MIR selector's per-function block, hop and
 * exit labels (.Lf<fn>b<k>, avr_mir.h), which span several candidates and so
 * cannot be renumbered per candidate. A function has one block label per
 * block, so the unit records as many names as its code defines; the record
 * grows as needed (it once held a fixed 256, which a 201-block function
 * exhausted). Release it with emit_unit_free. */
typedef struct {
    unsigned next_local;
    char (*globals)[AVR_OPERAND_LEN];
    size_t num_globals, globals_capacity;
} EmitUnit;

static inline void emit_unit_init(EmitUnit *unit) {
    unit->next_local = 0;
    unit->globals = NULL;
    unit->num_globals = 0;
    unit->globals_capacity = 0;
}

void emit_unit_free(EmitUnit *unit);

/* Records `name` as defined in the unit, for a symbol written outside
 * emit_candidate (such as the entry convention's, avr_mir.h). Returns 0, or
 * -1 (with an error printed) if it is already defined or memory runs out. */
int emit_unit_define(EmitUnit *unit, const char *name);

/* Writes `.s` text for `candidate` to `out` as part of `unit`. Returns 0 on
 * success, -1 (with an error printed) on a duplicate global label or when
 * memory runs out. */
int emit_candidate(EmitUnit *unit, const Candidate *candidate, FILE *out);

/* Writes the fixed `.s` header (.arch/.section/.global/_start:) that every
 * program needs before its first real instruction. Returns 0 on success. */
int emit_program_prologue(FILE *out);

/* Writes the trailing `break` that halts Avrora's simulation. Not priced
 * through the cost model (as in the hand-written sim/smoke.s, which does
 * not price its `break` either) -- it is a
 * simulation-harness artifact, not part of a real deployed program's
 * energy budget. Returns 0 on success. */
int emit_program_epilogue(FILE *out);

#endif /* OPTIFINE_EMIT_H */
