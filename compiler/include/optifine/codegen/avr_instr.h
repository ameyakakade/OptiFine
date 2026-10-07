/* AVR instructions and priced instruction sequences: the data every part of
 * the AVR backend exchanges -- instruction building and pricing (instr_buf.h),
 * MIR selection (avr_mir.h), emission (emit.h) -- and that the workload
 * lowering (lower.h, candidates.h) produces. It depends on nothing above the
 * target, so the generic backend builds and links without the workload
 * graph (ir.h) or its lowering. */
#ifndef OPTIFINE_CODEGEN_AVR_INSTR_H
#define OPTIFINE_CODEGEN_AVR_INSTR_H

#include <stddef.h>
#include <stdint.h>

#define AVR_MAX_OPERANDS 3
/* `mnemonic` holds the literal AVR opcode as emitted to .s (e.g. "muls",
 * "adc"), not a cost_table.toml key -- see codegen/cost_category.h for the
 * mapping from opcode to cost-table category. Longest current opcode is
 * "mulsu" (5 chars). */
#define AVR_MNEMONIC_LEN 16
/* Widened from 16 for the DSP path's symbolic program-memory operands:
 * "lo8(.Ltw+124)" does not fit 16 bytes, and snprintf truncates silently,
 * which produced a wrong-but-plausible address rather than an error. The
 * formatters now detect truncation outright; this keeps ordinary operands
 * comfortably inside the buffer. */
#define AVR_OPERAND_LEN 24

typedef struct {
    char mnemonic[AVR_MNEMONIC_LEN];
    char operands[AVR_MAX_OPERANDS][AVR_OPERAND_LEN];
    int num_operands;
} AvrInstr;

/* A priced instruction sequence. `cycles` and `energy_nj` are each emitted
 * instruction priced as executed the number of times instrbuf_price counts
 * it: exactly once for straight-line code, the trip count for a counted
 * loop's body. For code with branches between its own labels that is a
 * static figure, not an execution estimate (see avr_mir.h). */
typedef struct {
    AvrInstr *instructions;
    size_t num_instructions;
    uint32_t cycles;    /* from the AVR timing table; not the selection criterion */
    double energy_nj;   /* predicted (simulated-model) energy, from the cost table */
} Candidate;

void candidate_free(Candidate *candidate);

#endif /* OPTIFINE_CODEGEN_AVR_INSTR_H */
