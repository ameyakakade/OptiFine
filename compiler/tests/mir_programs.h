/* Small MIR programs shared by test_mir (verifier and reference
 * interpreter) and test_avr_mir (AVR instruction selection). Each builder
 * adds one function to `m` and returns its id. */
#ifndef OPTIFINE_TEST_MIR_PROGRAMS_H
#define OPTIFINE_TEST_MIR_PROGRAMS_H

#include "optifine/mir.h"

/* i32 f(i32 a, i32 b) { return ((a + b) - 7) ^ (a & b | 0x0F0F); } */
uint32_t mirp_arith32(MirModule *m);
/* i8 f(i8 a, i8 b) { return a * b + 1; } */
uint32_t mirp_mul8(MirModule *m);
/* void f(): g[1] = g[0] + table[2] (global i16 g[2], const i16 table[4]) */
uint32_t mirp_load_store(MirModule *m);
/* i16 f(i16 a, i16 b) { if (a <s b) r = b - a; else r = a - b; return r; } -- a diamond */
uint32_t mirp_diamond(MirModule *m);
/* i8 f(i8 x, i8 y) { return (x == y) + 2*(x != y) + 4*(x <u y); } in straight-line CMPs */
uint32_t mirp_compares(MirModule *m);
/* i32 f() { s = 0; p = &a[0]; for (i = 0; i != 10; i++) { *p = i*3 (i8);
 * s += zext(*p); p++; } return s; } -- a counted loop over a stack array
 * through a pointer; returns 135 */
uint32_t mirp_loop(MirModule *m);

/* The imperative reference function, as a C frontend would hand it over:
 *
 *   int16 f(int16 x) { int16 y = x + 3; if (y > 10) y = y - 2; else y = y + 4;
 *                      int16 sum = 0; for (int16 i = 0; i < 5; ++i) sum += y;
 *                      return sum; }
 *
 * in seven blocks (entry / then / else / loop init / loop head / loop body /
 * exit), with MIR's wrapping 16-bit arithmetic. Two encodings:
 *   mirp_imperative_values  every local a (non-SSA) virtual value
 *   mirp_imperative_memory  every local a MIR_MEM_STACK object, read and
 *                           written by LOAD and STORE, as an unoptimizing
 *                           frontend emits it */
uint32_t mirp_imperative_values(MirModule *m);
uint32_t mirp_imperative_memory(MirModule *m);

#endif /* OPTIFINE_TEST_MIR_PROGRAMS_H */
