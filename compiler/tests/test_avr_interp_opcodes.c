#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "avr_interp.h"
#include "optifine/codegen/cost_category.h"

static AvrInstr mk1(const char *m, const char *o1) {
    AvrInstr i;
    memset(&i, 0, sizeof(i));
    strncpy(i.mnemonic, m, sizeof(i.mnemonic) - 1);
    strncpy(i.operands[0], o1, sizeof(i.operands[0]) - 1);
    i.num_operands = 1;
    return i;
}

static AvrInstr mk2(const char *m, const char *o1, const char *o2) {
    AvrInstr i;
    memset(&i, 0, sizeof(i));
    strncpy(i.mnemonic, m, sizeof(i.mnemonic) - 1);
    strncpy(i.operands[0], o1, sizeof(i.operands[0]) - 1);
    strncpy(i.operands[1], o2, sizeof(i.operands[1]) - 1);
    i.num_operands = 2;
    return i;
}

static void test_rol_shifts_left_with_carry_in_and_out(void) {
    AvrInterp interp;
    avr_interp_init(&interp);
    interp.regs[5] = 0x81; /* 1000_0001 */
    interp.carry = 1;
    AvrInstr i = mk1("rol", "r5");
    assert(avr_interp_step(&interp, &i) == 0);
    assert(interp.regs[5] == 0x03); /* (0x81<<1 | carry-in 1) truncated to 8 bits = 0x03 */
    assert(interp.carry == 1);      /* old bit 7 (1) shifted out */
}

static void test_sub_computes_difference_and_borrow(void) {
    AvrInterp interp;
    avr_interp_init(&interp);
    interp.regs[5] = 0x03;
    interp.regs[6] = 0x05;
    AvrInstr i = mk2("sub", "r5", "r6");
    assert(avr_interp_step(&interp, &i) == 0);
    assert(interp.regs[5] == (uint8_t)(0x03 - 0x05));
    assert(interp.carry == 1); /* 3 - 5 borrows */
}

static void test_sub_no_borrow(void) {
    AvrInterp interp;
    avr_interp_init(&interp);
    interp.regs[5] = 0x09;
    interp.regs[6] = 0x05;
    AvrInstr i = mk2("sub", "r5", "r6");
    assert(avr_interp_step(&interp, &i) == 0);
    assert(interp.regs[5] == 0x04);
    assert(interp.carry == 0);
}

static void test_rol_and_sub_are_cost_mapped(void) {
    assert(avr_cost_category("rol") != NULL);
    assert(avr_cost_category("sub") != NULL);
}

/* subi/sbci as the FFT uses them: X += 124 written as X -= (-124), i.e.
 * subi lo, 0x84 / sbci hi, 0xFF. */
static void test_subi_sbci_add_a_16bit_constant(void) {
    AvrInterp interp;
    avr_interp_init(&interp);
    interp.regs[26] = 0xF0; interp.regs[27] = 0x04;   /* X = 0x04F0 */
    AvrInstr lo = mk2("subi", "r26", "0x84");
    AvrInstr hi = mk2("sbci", "r27", "0xFF");
    assert(avr_interp_step(&interp, &lo) == 0);
    assert(avr_interp_step(&interp, &hi) == 0);
    assert(interp.regs[26] == 0x6C && interp.regs[27] == 0x05); /* 0x04F0 + 124 = 0x056C */
    /* and X -= 128: subi 0x80 / sbci 0x00 */
    lo = mk2("subi", "r26", "0x80");
    hi = mk2("sbci", "r27", "0x00");
    assert(avr_interp_step(&interp, &lo) == 0);
    assert(avr_interp_step(&interp, &hi) == 0);
    assert(interp.regs[26] == 0xEC && interp.regs[27] == 0x04); /* 0x056C - 128 = 0x04EC */
    /* only r16-r31 encode */
    AvrInstr bad = mk2("subi", "r3", "0x01");
    assert(avr_interp_step(&interp, &bad) != 0);
    assert(avr_cost_category("subi") != NULL && avr_cost_category("sbci") != NULL);
}

int main(void) {
    test_rol_shifts_left_with_carry_in_and_out();
    test_sub_computes_difference_and_borrow();
    test_sub_no_borrow();
    test_rol_and_sub_are_cost_mapped();
    test_subi_sbci_add_a_16bit_constant();
    printf("test_avr_interp_opcodes: all tests passed\n");
    return 0;
}
