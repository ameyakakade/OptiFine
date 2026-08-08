#include "avr_interp.h"

#include "optifine/codegen/instr_buf.h"

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
    interp->zero = 0;
    interp->budget = AVR_INTERP_DEFAULT_BUDGET;
}

/* X/Y/Z are register pairs r26:r27, r28:r29, r30:r31. */
static int ptr_base(char name) {
    switch (name) {
        case 'X': return 26;
        case 'Y': return 28;
        case 'Z': return 30;
        default:  return -1;
    }
}

static uint16_t ptr_get(const AvrInterp *interp, int base) {
    return (uint16_t)(interp->regs[base] | ((uint16_t)interp->regs[base + 1] << 8));
}

static void ptr_set(AvrInterp *interp, int base, uint16_t value) {
    interp->regs[base] = (uint8_t)(value & 0xFF);
    interp->regs[base + 1] = (uint8_t)(value >> 8);
}

/* Parses "Z", "Z+" or "Z+6" into a pointer base, a post-increment flag and a
 * displacement. Returns -1 for anything else. */
static int parse_ptr(const char *s, int *base, int *post_inc, int *disp) {
    int b = ptr_base(s[0]);
    if (b < 0) return -1;
    *base = b;
    *post_inc = 0;
    *disp = 0;
    if (s[1] == '\0') return 0;
    if (s[1] != '+') return -1;
    if (s[2] == '\0') { *post_inc = 1; return 0; }
    *disp = atoi(s + 2);
    return 0;
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

    /* --- counted-loop and pointer machinery (DSP path) --- */
    if (avr_instr_is_label(instr)) { /* directive: no effect, no cost */
        return 0;
    }
    if (strcmp(m, "dec") == 0) {
        int rd = reg_of(instr->operands[0]);
        r[rd] = (uint8_t)(r[rd] - 1);
        interp->zero = (r[rd] == 0) ? 1 : 0; /* dec does not touch Carry */
        return 0;
    }
    if (strcmp(m, "brne") == 0) { /* control flow -- handled by the caller */
        return 0;
    }
    if (strcmp(m, "movw") == 0) {
        int rd = reg_of(instr->operands[0]), rr = reg_of(instr->operands[1]);
        r[rd] = r[rr];
        r[rd + 1] = r[rr + 1];
        return 0;
    }
    if (strcmp(m, "adiw") == 0 || strcmp(m, "sbiw") == 0) {
        int base = ptr_base(instr->operands[0][0]);
        if (base < 0) base = reg_of(instr->operands[0]);
        uint32_t k = hex_of(instr->operands[1]);
        uint16_t v = ptr_get(interp, base);
        ptr_set(interp, base, (uint16_t)(m[0] == 'a' ? v + k : v - k));
        return 0;
    }
    if (strcmp(m, "ld") == 0 || strcmp(m, "ldd") == 0) {
        int base, inc, disp;
        if (parse_ptr(instr->operands[1], &base, &inc, &disp) != 0) {
            fprintf(stderr, "avr_interp: bad pointer operand '%s'\n", instr->operands[1]);
            return -1;
        }
        uint16_t addr = (uint16_t)(ptr_get(interp, base) + disp);
        if (addr >= AVR_INTERP_MEM_SIZE) {
            fprintf(stderr, "avr_interp: ld address 0x%X out of range\n", addr);
            return -1;
        }
        r[reg_of(instr->operands[0])] = interp->mem[addr];
        if (inc) ptr_set(interp, base, (uint16_t)(addr + 1));
        return 0;
    }
    if (strcmp(m, "st") == 0 || strcmp(m, "std") == 0) {
        int base, inc, disp;
        if (parse_ptr(instr->operands[0], &base, &inc, &disp) != 0) {
            fprintf(stderr, "avr_interp: bad pointer operand '%s'\n", instr->operands[0]);
            return -1;
        }
        uint16_t addr = (uint16_t)(ptr_get(interp, base) + disp);
        if (addr >= AVR_INTERP_MEM_SIZE) {
            fprintf(stderr, "avr_interp: st address 0x%X out of range\n", addr);
            return -1;
        }
        interp->mem[addr] = r[reg_of(instr->operands[1])];
        if (inc) ptr_set(interp, base, (uint16_t)(addr + 1));
        return 0;
    }

    fprintf(stderr,
            "avr_interp: unsupported opcode '%s' -- this is a test-only interpreter covering "
            "only what lower.c emits, not a general AVR simulator\n",
            m);
    return -1;
}

static long find_label(const Candidate *candidate, const char *name) {
    for (size_t i = 0; i < candidate->num_instructions; i++) {
        const AvrInstr *ins = &candidate->instructions[i];
        if (avr_instr_is_label(ins) && strcmp(ins->operands[0], name) == 0) {
            return (long)i;
        }
    }
    return -1;
}

int avr_interp_run(AvrInterp *interp, const Candidate *candidate) {
    size_t pc = 0;
    while (pc < candidate->num_instructions) {
        if (interp->budget-- == 0) {
            fprintf(stderr, "avr_interp: instruction budget exhausted -- runaway loop?\n");
            return -1;
        }
        const AvrInstr *ins = &candidate->instructions[pc];
        if (avr_interp_step(interp, ins) != 0) {
            return -1;
        }
        if (strcmp(ins->mnemonic, "brne") == 0 && !interp->zero) {
            long target = find_label(candidate, ins->operands[0]);
            if (target < 0) {
                fprintf(stderr, "avr_interp: brne to unknown label '%s'\n", ins->operands[0]);
                return -1;
            }
            pc = (size_t)target;
            continue;
        }
        pc++;
    }
    return 0;
}
