/* AVR backend for the generic MIR (optifine/mir.h).
 *
 * A frontend that builds MIR uses avr_mir_build_program and
 * avr_mir_emit_program (bottom of this file): one call verifies, lays out,
 * selects and prices, the other writes the assembly. Underneath are three
 * steps, all target-specific and all independent of where the MIR came from
 * (the workload translation in hir_to_mir.h, or a frontend):
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
 *      Control flow: blocks get labels .Lf<fn>b<blk>; br is a jump (omitted
 *      for a fall-through), cbr is a compare against r2 and a breq that hops
 *      over the taken edge's jump, so no conditional branch needs more reach
 *      than that one jump. Each jump is an rjmp (+/-2K words) unless the
 *      function's layout in flash words shows it cannot reach, when it is a
 *      jmp (two words, 3 cycles); selection repeats until every jump reaches,
 *      so the assembler never sees an out-of-range branch. An entry function
 *      is inlined by the program wrapper, so its `ret` falls through to the
 *      end of the function (after storing any return value in the function's
 *      return slot). There is no call instruction or calling convention yet;
 *      see the entry convention below.
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
 * Costs: each segment's Candidate is priced by instrbuf_price: cycles from the
 * AVR timing table, energy from the cost model. For a single-block function
 * (AvrMirCode.straight_line) that is the executed cost -- the workload
 * programs are all single-block, so their printed cycles are exact. For a
 * function with branches it is a STATIC figure, every emitted instruction
 * priced once whatever path runs, and must never be reported as predicted
 * execution cycles or energy: path-sensitive costing is future work, and
 * nothing here selects between alternative codings of generic MIR by energy
 * (that happens only for workload kernels, above MIR). */
#ifndef OPTIFINE_CODEGEN_AVR_MIR_H
#define OPTIFINE_CODEGEN_AVR_MIR_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

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
    size_t *num_values;     /* per function: its value count when the layout was made */
    size_t num_objects, num_functions;
} AvrMirLayout;

/* Verification. Every function below that takes a module runs mir_verify on
 * it first and refuses an invalid one with a diagnostic, so no caller can
 * reach layout, selection or emission with malformed MIR by skipping its own
 * verification call. Nothing records that a module was verified: a module
 * changed after one call is verified again by the next. Selection also
 * refuses a layout made for a different module, or before values or objects
 * were added, and one that leaves an object or value without an address. */

/* Allocates an empty layout for a verified `module`. Returns 0, or -1 with a
 * diagnostic (invalid MIR, out of memory). */
int avr_mir_layout_init(const MirModule *module, AvrMirLayout *layout);
void avr_mir_layout_free(AvrMirLayout *layout);

/* Places every unplaced SRAM object, then every value slot and return slot,
 * consecutively from `base`, refusing to pass `limit` (exclusive). Returns
 * the first unused address in *end, or -1 with a diagnostic. */
int avr_mir_layout_place_rest(const MirModule *module, AvrMirLayout *layout, uint16_t base, uint16_t limit,
                              uint16_t *end);

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
    /* The function is one basic block, so it runs every emitted instruction
     * once (or, inside a target region, as its counted loops say) and the
     * segments' summed cycles and energy are its execution cost. With more
     * than one block the sums are static -- each instruction priced once,
     * whatever path runs -- and must not be presented as an execution
     * estimate; path-sensitive costing does not exist yet. */
    int straight_line;
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

/* ---- Standalone programs and the research entry convention ----
 *
 * The supported way for a frontend to compile MIR: one entry function, no
 * calls, run once from reset to a halt. It is a research convention for
 * single-function programs pending real call support, NOT an AVR C ABI:
 *
 *   entry        `_start` runs `clr r2`, then the entry function inlined; no
 *                other function of the module is compiled.
 *   parameters   parameter i lives in SRAM at the absolute address of the
 *                symbol `optifine_param_<i>` (also in AvrMirProgram), in
 *                the parameter's width, little-endian. Whoever runs the
 *                program -- a simulator, a debugger, a test -- writes it
 *                before execution; nothing initializes it, and SRAM is not
 *                cleared at reset.
 *   return       a non-void entry function leaves its value at the symbol
 *                `optifine_return`, in the return type's width, little-endian.
 *   termination  `break` (priced: Avrora counts it), after which the program
 *                does nothing; CONST data follows, never executed.
 *   symbols      `.set` absolute data-space addresses (0x0200..0x10FF), so a
 *                debugger addresses them in the data space (0x800000 + addr
 *                for avr-gdb). A CONST object may not use these names.
 *
 * Not supported: calls, recursion, reentrancy, a stack frame (STACK objects
 * are statically placed), initialized or zeroed globals at reset, more than
 * one entry, linking several units. */
typedef struct {
    size_t instructions;    /* AVR instructions executed-or-not, break included; no labels or data */
    size_t code_bytes;      /* flash for code, break included */
    size_t data_bytes;      /* flash for CONST data */
    uint64_t static_cycles; /* every emitted instruction priced once (break included) */
    double static_energy_nj;
    /* The entry function is one basic block, so it executes each emitted
     * instruction exactly once and static_cycles / static_energy_nj ARE its
     * predicted execution cost. When 0 they are only static figures. */
    int exact;
} AvrMirStaticCost;

typedef struct {
    AvrMirLayout layout;
    AvrMirCode code;        /* the entry prologue and the entry function */
    Candidate termination;  /* `break` */
    Candidate constants;    /* CONST objects */
    uint32_t entry;
    size_t num_params;
    uint16_t *param_addr;   /* per parameter: its SRAM address */
    size_t *param_bytes;    /* per parameter: its width */
    uint16_t return_addr;   /* 0 for a void entry function */
    size_t return_bytes;
    AvrMirStaticCost cost;
} AvrMirProgram;

/* Verifies `module` and compiles function `entry` as a standalone program,
 * placing every SRAM object and value from AVR_MIR_SRAM_BASE. Returns 0, or
 * -1 with a diagnostic (and `out` empty). */
int avr_mir_build_program(const MirModule *module, uint32_t entry, const CostModel *cost_model,
                          AvrMirProgram *out);
/* Writes the program as one assembly unit: header, entry-convention
 * symbols, code, break, constant data. Returns 0, or -1 with a diagnostic. */
int avr_mir_emit_program(const AvrMirProgram *program, FILE *out);
void avr_mir_program_free(AvrMirProgram *program);

#endif /* OPTIFINE_CODEGEN_AVR_MIR_H */
