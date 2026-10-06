/* Private interface between the lowering files (lower.c and lower_*.c). Not
 * installed under include/: nothing outside src/codegen/ should call these.
 * The public lowering API is optifine/codegen/lower.h.
 *
 * Every function here appends AVR instructions for one op, or one shared
 * piece of an op, to `buf`. Register use follows the contracts in
 * optifine/codegen/registers.h. */
#ifndef OPTIFINE_CODEGEN_LOWER_INTERNAL_H
#define OPTIFINE_CODEGEN_LOWER_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/lower.h"
#include "optifine/codegen/sram_layout.h"
#include "optifine/ir.h"

/* ---- ML path (lower_ml.c, lower_requantize.c) ---- */
void lower_matmul(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id);
void lower_add(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id);
void lower_relu(InstrBuf *buf, uint16_t in_addr, uint16_t out_addr, size_t n);
/* Returns -1 (with a diagnostic) when the scale ratio has no supported
 * fixed-point multiplier. */
int lower_requantize(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id);

/* ---- DSP Q15 primitives on absolute SRAM addresses (lower_dsp.c) ---- */
void lower_sub16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr);
void lower_add16(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr);
void lower_asr16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr);
void lower_copy16(InstrBuf *buf, uint16_t a_addr, uint16_t out_addr);
void lower_const16(InstrBuf *buf, int16_t value, uint16_t addr);
void lower_fixed_mul_q15(InstrBuf *buf, uint16_t a_addr, uint16_t b_addr, uint16_t out_addr);

/* ---- DSP pointer helpers (lower_dsp.c). `ptr` is 'X', 'Y' or 'Z'; a pointer
 * pair is named by its low register (26 X, 28 Y, 30 Z). ---- */
void ptr_byte_to_scratch(InstrBuf *buf, char ptr, uint16_t dst);
void scratch_byte_to_ptr(InstrBuf *buf, uint16_t src, char ptr);
void load_ptr_imm(InstrBuf *buf, int lo_reg, uint16_t addr);
void ptr_add(InstrBuf *buf, int lo_reg, int delta);

/* ---- DSP ops ---- */
void lower_window(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id);
void lower_bit_reverse(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id);
void lower_fft_butterfly_stage(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id,
                               int stage);
/* Which FFT stage op_id is: its position among the graph's butterflies. */
int dsp_butterfly_stage_index(const IrGraph *graph, size_t op_id);
void lower_fft_constant_data(InstrBuf *buf, const IrGraph *graph);
void lower_magnitude(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id);
void lower_peak_extract(InstrBuf *buf, const IrGraph *graph, const SramLayout *layout, size_t op_id);

#endif /* OPTIFINE_CODEGEN_LOWER_INTERNAL_H */
