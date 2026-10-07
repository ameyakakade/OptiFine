/* AVR backend for the generic MIR (optifine/mir.h).
 *
 * Three steps, all target-specific and all independent of where the MIR came
 * from (the workload translation in hir_to_mir.h today, a C frontend later):
 *
 *   1. Address assignment (AvrMirLayout). MIR objects have no addresses; the
 *      backend gives every SRAM object (GLOBAL, STACK, SCRATCH, TENSOR) an
 *      address and every value that needs storage a slot. CONST objects live
 *      in program memory under their own name as the label. A caller may fix
 *      some object addresses first -- the workload path places tensors with
 *      sram_layout -- and let avr_mir_layout_place_rest assign the others.
 *
 *   2. Instruction selection (avr_mir_select_function). Each MIR instruction
 *      becomes AVR instructions in an InstrBuf, priced into Candidates. The
 *      selector is deliberately simple: every value that is not folded lives
 *      in an SRAM slot, and each instruction loads its operands into fixed
 *      scratch registers (r22-r25, X, Z; r0:r1 for mul), computes byte by
 *      byte and stores the result. A LOAD whose only use is the STORE right
 *      after it is folded into a register move (lds/sts pairs). No value stays
 *      in a register across instructions; a real register allocator belongs
 *      here, below MIR, and does not exist yet.
 *
 *      Control flow: blocks get labels .Lf<fn>b<blk>; br is rjmp (omitted for
 *      a fall-through), cbr is a compare against r2 and breq over two rjmps,
 *      so no conditional branch has to reach further than one word. An entry
 *      function is inlined by the program wrapper, so its `ret` falls through
 *      to the end of the function (after storing any return value in the
 *      function's return slot). There is no call instruction or calling
 *      convention yet.
 *
 *      MIR_TARGET regions whose target is "avr" carry a priced Candidate,
 *      which is emitted as is.
 *
 *      Requires r2 == 0 (REG_ZERO, registers.h), established by the entry
 *      prologue segment. Unsupported for now, and refused with a diagnostic:
 *      MIR_MUL wider than 16 bits, and MIR_ADDR of a CONST object (a pointer
 *      into program memory needs an address-space-qualified pointer type MIR
 *      does not have).
 *
 *   3. Constant data (avr_mir_select_constants): every CONST object as a
 *      label and .dw words, to be placed where control flow cannot reach it.
 *
 * Costs: each segment's Candidate is priced by instrbuf_price. For straight-
 * line code that is the executed cost; for a function with branches it is the
 * cost of executing every emitted instruction once, which a caller must not
 * present as a path cost. */
#ifndef OPTIFINE_CODEGEN_AVR_MIR_H
#define OPTIFINE_CODEGEN_AVR_MIR_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/codegen/avr_instr.h"
#include "optifine/cost_model.h"
#include "optifine/mir.h"

#define AVR_MIR_PTR_BYTES 2

/* The ATmega128 SRAM the backend may place objects and values in: from
 * 0x0200 (clear of the 0x0100 start of internal SRAM, as the workload layout
 * keeps it) up to, not including, 0x1100, the end of internal SRAM. The
 * workload layout (sram_layout.h) uses the same bounds. */
#define AVR_MIR_SRAM_BASE 0x0200
#define AVR_MIR_SRAM_LIMIT 0x1100

typedef struct {
    uint16_t *object_addr;  /* per object; 0 = not yet placed (CONST objects stay 0) */
    uint16_t **value_slot;  /* per function, per value; 0 = needs no storage (folded) */
    uint16_t *return_slot;  /* per function; 0 for void */
    size_t num_objects, num_functions;
} AvrMirLayout;

/* Allocates an empty layout for `module`. Returns 0 or -1 (out of memory). */
int avr_mir_layout_init(const MirModule *module, AvrMirLayout *layout);
void avr_mir_layout_free(AvrMirLayout *layout);

/* Places every unplaced SRAM object, then every value slot and return slot,
 * consecutively from `base`, refusing to pass `limit` (exclusive). Returns
 * the first unused address in *end, or -1 with a diagnostic. */
int avr_mir_layout_place_rest(const MirModule *module, AvrMirLayout *layout, uint16_t base, uint16_t limit,
                              uint16_t *end);

/* True when `value` of function `fn` is a LOAD result consumed only by the
 * STORE that immediately follows it, so selection moves it through a
 * register and it needs no slot. */
int avr_mir_value_folded(const MirModule *module, uint32_t fn, uint32_t value);

/* One emitted piece of a function: a priced Candidate and the `origin` of
 * the MIR instructions it came from (MIR_NONE for code the backend adds
 * itself, such as the entry prologue). Consecutive instructions with the
 * same origin share a segment, and a MIR_TARGET region is always its own. */
typedef struct {
    Candidate code;
    uint32_t origin;
} AvrMirSegment;

typedef struct {
    AvrMirSegment *segments;
    size_t count, capacity;
} AvrMirCode;

void avr_mir_code_free(AvrMirCode *code);

/* Selects AVR code for function `fn` of a verified module. With
 * `entry_prologue`, the code starts with the `clr r2` segment that every
 * program needs before anything else runs. Returns 0, or -1 with a
 * diagnostic (and `out` empty). */
int avr_mir_select_function(const MirModule *module, const AvrMirLayout *layout, uint32_t fn,
                            int entry_prologue, const CostModel *cost_model, AvrMirCode *out);

/* All CONST objects, in object order, as one Candidate of labels and .dw
 * data words (zero cycles). An odd-sized object is padded with a zero byte.
 * Empty when the module has none. */
int avr_mir_select_constants(const MirModule *module, const CostModel *cost_model, Candidate *out);

#endif /* OPTIFINE_CODEGEN_AVR_MIR_H */
