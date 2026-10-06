/* A small test-only AVR interpreter covering exactly the opcode subset
 * compiler/src/codegen/lower*.c emit: the 18 straight-line opcodes (ldi,
 * sts, lds, mov, clr, add, adc, sub, sbc, lsl, rol, com, and, asr, ror,
 * mul, muls, mulsu) plus the DSP path's counted-loop machinery (dec, brne,
 * ld, st, ldd, std, adiw, sbiw, movw, subi, sbci and the .L label
 * pseudo-instruction).
 * This is NOT a general AVR simulator: it exists only to execute this
 * project's own generated code for golden-value correctness testing, and
 * must never be used as a substitute for Avrora's real energy numbers or
 * treated as validating any instruction outside this exact list. */
#ifndef OPTIFINE_TEST_AVR_INTERP_H
#define OPTIFINE_TEST_AVR_INTERP_H

#include <stdint.h>

#include "optifine/codegen/candidates.h"

/* Sized to cover the largest address compiler/src/codegen/sram_layout.h's
 * SRAM_LAYOUT_LIMIT allows (0x1100) -- addresses are used directly as
 * array indices, matching real AVR direct addressing. */
#define AVR_INTERP_MEM_SIZE 0x1100

/* Program-memory image size: the 64 KiB that plain LPM (Z only, no RAMPZ)
 * can address, not the SRAM size. It was once AVR_INTERP_MEM_SIZE, and a
 * program longer than 0x1100 bytes -- the six-stage FFT -- had its twiddle
 * table silently left out of the image. Code or data past this bound is a
 * hard error, since reaching it would need ELPM, which the project does not
 * use. */
#define AVR_INTERP_PROGMEM_SIZE 0x10000

typedef struct {
    uint8_t regs[32];
    uint8_t carry; /* SREG's Carry bit */
    uint8_t zero;  /* SREG's Zero bit -- set by dec, consumed by brne */
    uint8_t mem[AVR_INTERP_MEM_SIZE];
    /* Guards against a malformed counted loop spinning forever in a unit
     * test. Generous: the whole 64-point DSP pipeline executes ~250k
     * instructions, and a runaway loop hits this rather than hanging CI. */
    unsigned long budget;

    /* Program-memory image, read by `lpm` through Z.
     *
     * Built from the candidate before execution by laying every instruction
     * out at its real flash byte address, exactly as the assembler would: two
     * bytes for an ordinary opcode, four for lds/sts and the other 32-bit
     * encodings, two for a `.dw` data word (little-endian), zero for a label.
     * Addresses therefore match the linked AVR image, so a `lo8/hi8(label)`
     * the compiler emitted resolves here to the same byte the real device
     * would read. Only `.dw` payload bytes are meaningful; opcode bytes are
     * left zero because nothing in this project reads its own code. */
    uint8_t progmem[AVR_INTERP_PROGMEM_SIZE];
    size_t progmem_size;

    /* Cycles actually executed, accumulated from this interpreter's OWN cycle
     * table (avr_interp.c), which is transcribed from the AVR Instruction Set
     * Manual independently of the compiler's cost model. Comparing the two is
     * then a real cross-check rather than a tautology: the compiler predicts
     * from cost_table.toml categories, the interpreter counts from the manual,
     * and Avrora measures. All three must agree. */
    unsigned long cycles;
    /* Instructions actually executed (labels and data words excluded). */
    unsigned long instructions;
} AvrInterp;

#define AVR_INTERP_DEFAULT_BUDGET 20000000UL

void avr_interp_init(AvrInterp *interp);

/* Executes one instruction. Returns 0 on success, -1 for any opcode or
 * operand outside this interpreter's supported subset (with an error
 * printed to stderr). */
int avr_interp_step(AvrInterp *interp, const AvrInstr *instr);

/* Executes `candidate` from its first instruction until it runs off the
 * end, following branches. Returns 0 on success, -1 on the first
 * unsupported instruction, an unknown branch target, or budget exhaustion.
 *
 * Unlike a straight walk of the instruction array, this maintains a real
 * program counter, so a counted loop executes its body the number of times
 * the generated code actually says -- which is the whole point of testing
 * looped lowering rather than assuming it. */
int avr_interp_run(AvrInterp *interp, const Candidate *candidate);

#endif /* OPTIFINE_TEST_AVR_INTERP_H */
