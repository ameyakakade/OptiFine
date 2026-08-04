#include "avr_interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* AvrInstr operands are always either "rN" (a register) or a "0x..." hex
 * literal (an immediate or a direct SRAM address) -- see
 * compiler/src/codegen/lower.c's fmt_reg/fmt_imm/fmt_addr. Which operand
 * position is which is fixed per-mnemonic below, matching exactly what
 * lower.c emits. */
static int reg_of(const char *s) {
    return atoi(s + 1);
}
static uint32_t hex_of(const char *s) {
    return (uint32_t)strtoul(s, NULL, 16);
}

void avr_interp_init(AvrInterp *interp) {
    memset(interp->regs, 0, sizeof(interp->regs));
    memset(interp->mem, 0, sizeof(interp->mem));
    interp->carry = 0;
}

int avr_interp_step(AvrInterp *interp, const AvrInstr *instr) {
    const char *m = instr->mnemonic;
    uint8_t *r = interp->regs;

    if (strcmp(m, "ldi") == 0) {
        r[reg_of(instr->operands[0])] = (uint8_t)hex_of(instr->operands[1]);
        return 0;
    }
    if (strcmp(m, "sts") == 0) {
        uint32_t addr = hex_of(instr->operands[0]);
        if (addr >= AVR_INTERP_MEM_SIZE) {
            fprintf(stderr, "avr_interp: sts address 0x%X out of range\n", addr);
            return -1;
        }
        interp->mem[addr] = r[reg_of(instr->operands[1])];
        return 0;
    }
    if (strcmp(m, "lds") == 0) {
        uint32_t addr = hex_of(instr->operands[1]);
        if (addr >= AVR_INTERP_MEM_SIZE) {
            fprintf(stderr, "avr_interp: lds address 0x%X out of range\n", addr);
            return -1;
        }
        r[reg_of(instr->operands[0])] = interp->mem[addr];
        return 0;
    }
    if (strcmp(m, "mov") == 0) {
        r[reg_of(instr->operands[0])] = r[reg_of(instr->operands[1])];
        return 0;
    }
    if (strcmp(m, "clr") == 0) {
        r[reg_of(instr->operands[0])] = 0;
        return 0;
    }
    if (strcmp(m, "add") == 0) {
        int rd = reg_of(instr->operands[0]);
        int result = r[rd] + r[reg_of(instr->operands[1])];
        interp->carry = (result > 0xFF) ? 1 : 0;
        r[rd] = (uint8_t)result;
        return 0;
    }
    if (strcmp(m, "adc") == 0) {
        int rd = reg_of(instr->operands[0]);
        int result = r[rd] + r[reg_of(instr->operands[1])] + interp->carry;
        interp->carry = (result > 0xFF) ? 1 : 0;
        r[rd] = (uint8_t)result;
        return 0;
    }
    if (strcmp(m, "sbc") == 0) {
        int rd = reg_of(instr->operands[0]);
        int result = (int)r[rd] - (int)r[reg_of(instr->operands[1])] - (int)interp->carry;
        interp->carry = (result < 0) ? 1 : 0;
        r[rd] = (uint8_t)(result & 0xFF);
        return 0;
    }
    if (strcmp(m, "lsl") == 0) { /* the assembler alias for `add Rd,Rd` */
        int rd = reg_of(instr->operands[0]);
        int result = r[rd] + r[rd];
        interp->carry = (result > 0xFF) ? 1 : 0;
        r[rd] = (uint8_t)result;
        return 0;
    }
    if (strcmp(m, "com") == 0) {
        int rd = reg_of(instr->operands[0]);
        r[rd] = (uint8_t)(~r[rd]);
        interp->carry = 1; /* real COM always sets carry; unused downstream, kept accurate anyway */
        return 0;
    }
    if (strcmp(m, "and") == 0) {
        int rd = reg_of(instr->operands[0]);
        r[rd] = (uint8_t)(r[rd] & r[reg_of(instr->operands[1])]);
        return 0;
    }
    if (strcmp(m, "asr") == 0) {
        int rd = reg_of(instr->operands[0]);
        uint8_t v = r[rd];
        interp->carry = v & 1;
        r[rd] = (uint8_t)((v >> 1) | (v & 0x80)); /* sign bit stays, arithmetic shift */
        return 0;
    }
    if (strcmp(m, "ror") == 0) {
        int rd = reg_of(instr->operands[0]);
        uint8_t v = r[rd];
        uint8_t new_carry = v & 1;
        r[rd] = (uint8_t)((v >> 1) | (interp->carry ? 0x80 : 0));
        interp->carry = new_carry;
        return 0;
    }
    if (strcmp(m, "sub") == 0) {
        int rd = reg_of(instr->operands[0]);
        int result = (int)r[rd] - (int)r[reg_of(instr->operands[1])];
        interp->carry = (result < 0) ? 1 : 0;
        r[rd] = (uint8_t)(result & 0xFF);
        return 0;
    }
    if (strcmp(m, "rol") == 0) { /* the assembler alias for `adc Rd,Rd` */
        int rd = reg_of(instr->operands[0]);
        uint8_t v = r[rd];
        uint8_t new_carry = (v & 0x80) ? 1 : 0;
        r[rd] = (uint8_t)((v << 1) | (interp->carry ? 1 : 0));
        interp->carry = new_carry;
        return 0;
    }
    if (strcmp(m, "mul") == 0) { /* unsigned x unsigned -> r1:r0 */
        uint16_t product = (uint16_t)(r[reg_of(instr->operands[0])] * r[reg_of(instr->operands[1])]);
        r[0] = (uint8_t)(product & 0xFF);
        r[1] = (uint8_t)(product >> 8);
        return 0;
    }
    if (strcmp(m, "muls") == 0) { /* signed x signed -> r1:r0 */
        int8_t a = (int8_t)r[reg_of(instr->operands[0])];
        int8_t b = (int8_t)r[reg_of(instr->operands[1])];
        uint16_t bits = (uint16_t)(int16_t)((int)a * (int)b);
        r[0] = (uint8_t)(bits & 0xFF);
        r[1] = (uint8_t)(bits >> 8);
        return 0;
    }
    if (strcmp(m, "mulsu") == 0) { /* signed x unsigned -> r1:r0 */
        int8_t a = (int8_t)r[reg_of(instr->operands[0])];
        uint8_t b = r[reg_of(instr->operands[1])];
        uint16_t bits = (uint16_t)(int16_t)((int)a * (int)b);
        r[0] = (uint8_t)(bits & 0xFF);
        r[1] = (uint8_t)(bits >> 8);
        return 0;
    }

    fprintf(stderr,
            "avr_interp: unsupported opcode '%s' -- this is a test-only interpreter covering "
            "only what lower.c emits, not a general AVR simulator\n",
            m);
    return -1;
}

int avr_interp_run(AvrInterp *interp, const Candidate *candidate) {
    for (size_t i = 0; i < candidate->num_instructions; i++) {
        if (avr_interp_step(interp, &candidate->instructions[i]) != 0) {
            return -1;
        }
    }
    return 0;
}
