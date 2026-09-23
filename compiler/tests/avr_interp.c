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
    memset(interp->progmem, 0, sizeof(interp->progmem));
    interp->progmem_size = 0;
    interp->cycles = 0;
    interp->instructions = 0;
}

/* X/Y/Z are register pairs r26:r27, r28:r29, r30:r31. */
/* Flash footprint of one emitted entry, matching the real AVR encoding.
 * lds/sts with a 16-bit address are 32-bit instructions; everything else this
 * project emits is 16-bit. Calibrated against avr-size on real output. */
static size_t instr_flash_bytes(const AvrInstr *ins) {
    return avr_instr_flash_bytes(ins); /* one definition, shared with codegen */
}

/* Lays the candidate out at its real flash addresses and fills in the .dw
 * payload, so `lpm` reads what the linked image would hold. Labels record
 * their byte address for lo8/hi8 resolution. */
static int build_progmem(AvrInterp *interp, const Candidate *c) {
    size_t addr = 0;
    for (size_t i = 0; i < c->num_instructions; i++) {
        const AvrInstr *ins = &c->instructions[i];
        size_t size = instr_flash_bytes(ins);
        if (addr + size > AVR_INTERP_PROGMEM_SIZE) {
            fprintf(stderr, "avr_interp: program image exceeds the %u-byte LPM range at index %zu\n",
                    (unsigned)AVR_INTERP_PROGMEM_SIZE, i);
            return -1;
        }
        if (avr_instr_is_data_word(ins)) {
            uint32_t w = (uint32_t)strtoul(ins->operands[0], NULL, 16);
            interp->progmem[addr] = (uint8_t)(w & 0xFF);       /* little-endian */
            interp->progmem[addr + 1] = (uint8_t)((w >> 8) & 0xFF);
        }
        addr += size;
    }
    interp->progmem_size = addr;
    return 0;
}

/* Byte address of a label in the laid-out image. */
static long label_flash_address(const Candidate *c, const char *name) {
    size_t addr = 0;
    for (size_t i = 0; i < c->num_instructions; i++) {
        const AvrInstr *ins = &c->instructions[i];
        if (avr_instr_is_label(ins) && strcmp(ins->operands[0], name) == 0) return (long)addr;
        addr += instr_flash_bytes(ins);
    }
    return -1;
}

/* Cycle cost of one executed instruction on ATmega128, from the AVR
 * Instruction Set Manual. Transcribed here deliberately rather than shared
 * with the compiler's cost model, so the two can disagree and be caught.
 * `taken` distinguishes BRNE's two costs. */
static int avr_interp_cycles(const AvrInstr *ins, int taken) {
    const char *m = ins->mnemonic;
    if (avr_instr_is_label(ins) || avr_instr_is_data_word(ins)) return 0;
    if (!strcmp(m, "brne") || !strcmp(m, "breq")) return taken ? 2 : 1;
    if (!strcmp(m, "rjmp")) return 2;
    if (!strcmp(m, "lpm")) return 3;
    if (!strcmp(m, "lds") || !strcmp(m, "sts")) return 2;
    if (!strcmp(m, "ld") || !strcmp(m, "st") ||
        !strcmp(m, "ldd") || !strcmp(m, "std")) return 2;
    if (!strcmp(m, "mul") || !strcmp(m, "muls") || !strcmp(m, "mulsu")) return 2;
    if (!strcmp(m, "adiw") || !strcmp(m, "sbiw")) return 2;
    if (!strcmp(m, "break")) return 1;
    return 1; /* every remaining single-word ALU/move opcode */
}

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
        const char *op = instr->operands[1];
        if (strncmp(op, "lo8(", 4) == 0 || strncmp(op, "hi8(", 4) == 0) {
            /* Resolved against the laid-out image by avr_interp_run, which
             * rewrites these before execution; reaching here means no
             * candidate context was available. */
            fprintf(stderr, "avr_interp: unresolved symbolic operand '%s'\n", op);
            return -1;
        }
        r[reg_of(instr->operands[0])] = (uint8_t)hex_of(op);
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
        interp->carry = (product >> 15) & 1; /* real AVR: C = bit 15 of the result */
        return 0;
    }
    if (strcmp(m, "muls") == 0) { /* signed x signed -> r1:r0 */
        int8_t a = (int8_t)r[reg_of(instr->operands[0])];
        int8_t b = (int8_t)r[reg_of(instr->operands[1])];
        uint16_t bits = (uint16_t)(int16_t)((int)a * (int)b);
        r[0] = (uint8_t)(bits & 0xFF);
        r[1] = (uint8_t)(bits >> 8);
        interp->carry = (bits >> 15) & 1;
        return 0;
    }
    if (strcmp(m, "mulsu") == 0) { /* signed x unsigned -> r1:r0 */
        int8_t a = (int8_t)r[reg_of(instr->operands[0])];
        uint8_t b = r[reg_of(instr->operands[1])];
        uint16_t bits = (uint16_t)(int16_t)((int)a * (int)b);
        r[0] = (uint8_t)(bits & 0xFF);
        r[1] = (uint8_t)(bits >> 8);
        interp->carry = (bits >> 15) & 1;
        return 0;
    }

    if (strcmp(m, "break") == 0) {
        return 0; /* halt is handled by avr_interp_run */
    }
    if (strcmp(m, "lpm") == 0) {
        int base, inc, disp;
        if (parse_ptr(instr->operands[1], &base, &inc, &disp) != 0 || base != 30) {
            fprintf(stderr, "avr_interp: lpm requires Z, got '%s'\n", instr->operands[1]);
            return -1;
        }
        uint16_t addr = (uint16_t)(ptr_get(interp, base) + disp);
        if (addr >= interp->progmem_size) {
            fprintf(stderr, "avr_interp: lpm address 0x%X is past the program image (0x%zX bytes)\n",
                    addr, interp->progmem_size);
            return -1;
        }
        r[reg_of(instr->operands[0])] = interp->progmem[addr];
        if (inc) ptr_set(interp, base, (uint16_t)(addr + 1));
        return 0;
    }
    if (strcmp(m, "cp") == 0 || strcmp(m, "cpc") == 0) {
        int rd = reg_of(instr->operands[0]), rr = reg_of(instr->operands[1]);
        int borrow = (m[2] == 'c') ? interp->carry : 0;
        int result = (int)r[rd] - (int)r[rr] - borrow;
        interp->carry = (result < 0) ? 1 : 0;
        /* CPC keeps Z set only if it was already set and this byte is zero --
         * that is what makes a multi-byte compare work. CP sets it outright. */
        uint8_t z = ((result & 0xFF) == 0);
        interp->zero = (m[2] == 'c') ? (uint8_t)(interp->zero && z) : z;
        return 0; /* neither writes a register */
    }
    if (strcmp(m, "subi") == 0 || strcmp(m, "sbci") == 0) {
        int rd = reg_of(instr->operands[0]);
        if (rd < 16) {
            fprintf(stderr, "avr_interp: %s only encodes r16-r31, got r%d\n", m, rd);
            return -1;
        }
        int with_carry = (m[1] == 'b');
        int result = (int)r[rd] - (int)hex_of(instr->operands[1]) - (with_carry ? interp->carry : 0);
        interp->carry = (result < 0) ? 1 : 0;
        r[rd] = (uint8_t)(result & 0xFF);
        /* SBCI keeps Z only if it was already set, same chaining as CPC. */
        uint8_t z = (r[rd] == 0);
        interp->zero = with_carry ? (uint8_t)(interp->zero && z) : z;
        return 0;
    }
    if (strcmp(m, "lsr") == 0) {
        int rd = reg_of(instr->operands[0]);
        uint8_t v = r[rd];
        interp->carry = v & 1;
        r[rd] = (uint8_t)(v >> 1); /* logical: zero in, unlike asr */
        interp->zero = (r[rd] == 0);
        return 0;
    }
    if (strcmp(m, "eor") == 0) {
        int rd = reg_of(instr->operands[0]);
        r[rd] = (uint8_t)(r[rd] ^ r[reg_of(instr->operands[1])]);
        interp->zero = (r[rd] == 0);
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
    if (strcmp(m, "brne") == 0 || strcmp(m, "breq") == 0 || strcmp(m, "rjmp") == 0) {
        return 0; /* control flow -- handled by avr_interp_run */
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

/* Resolves lo8(sym+off)/hi8(sym+off) against the laid-out image. */
static int resolve_symbolic(const Candidate *c, const char *op, uint8_t *out) {
    int high = (op[0] == 'h');
    const char *inner = op + 4;
    char sym[AVR_OPERAND_LEN]; size_t n = 0;
    while (inner[n] && inner[n] != '+' && inner[n] != ')' && n + 1 < sizeof(sym)) { sym[n] = inner[n]; n++; }
    sym[n] = '\0';
    int off = (inner[n] == '+') ? atoi(inner + n + 1) : 0;
    long base = label_flash_address(c, sym);
    if (base < 0) return -1;
    uint16_t addr = (uint16_t)(base + off);
    *out = high ? (uint8_t)(addr >> 8) : (uint8_t)(addr & 0xFF);
    return 0;
}

int avr_interp_run(AvrInterp *interp, const Candidate *candidate) {
    size_t pc = 0;
    if (build_progmem(interp, candidate) != 0) {
        return -1;
    }
    while (pc < candidate->num_instructions) {
        if (interp->budget-- == 0) {
            fprintf(stderr, "avr_interp: instruction budget exhausted -- runaway loop?\n");
            return -1;
        }
        const AvrInstr *ins = &candidate->instructions[pc];
        if (avr_instr_is_data_word(ins)) {
            /* Constant data is never executed: the DSP path emits its table
             * after the program's terminating `break`. Reaching one here means
             * control flow fell into the table, which is a codegen bug. */
            fprintf(stderr, "avr_interp: executed a .dw data word at index %zu -- "
                            "control flow fell through into constant data\n", pc);
            return -1;
        }
        if (strcmp(ins->mnemonic, "ldi") == 0 &&
            (strncmp(ins->operands[1], "lo8(", 4) == 0 || strncmp(ins->operands[1], "hi8(", 4) == 0)) {
            uint8_t v;
            if (resolve_symbolic(candidate, ins->operands[1], &v) != 0) {
                fprintf(stderr, "avr_interp: cannot resolve '%s'\n", ins->operands[1]);
                return -1;
            }
            interp->regs[reg_of(ins->operands[0])] = v;
            interp->cycles += avr_interp_cycles(ins, 0);
            interp->instructions++;
            pc++;
            continue;
        }
        if (strcmp(ins->mnemonic, "break") == 0) {
            interp->cycles += avr_interp_cycles(ins, 0);
            interp->instructions++;
            return 0; /* halts, exactly as Avrora does; anything after is data */
        }
        if (avr_interp_step(interp, ins) != 0) {
            return -1;
        }
        int is_brne = !strcmp(ins->mnemonic, "brne");
        int is_breq = !strcmp(ins->mnemonic, "breq");
        int is_rjmp = !strcmp(ins->mnemonic, "rjmp");
        int take = is_rjmp || (is_brne && !interp->zero) || (is_breq && interp->zero);
        interp->cycles += avr_interp_cycles(ins, (is_brne || is_breq) ? take : 0);
        if (!avr_instr_is_label(ins)) interp->instructions++;
        if ((is_brne || is_breq || is_rjmp) && take) {
            long target = find_label(candidate, ins->operands[0]);
            if (target < 0) {
                fprintf(stderr, "avr_interp: branch to unknown label '%s'\n", ins->operands[0]);
                return -1;
            }
            pc = (size_t)target;
            continue;
        }
        pc++;
    }
    return 0;
}
