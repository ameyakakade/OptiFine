/* Deterministic SRAM address allocation for Phase A's naive codegen
 * (compiler/src/codegen/lower.c). regalloc_next_use always reports every
 * op's output value as spilled (-1, see regalloc.c) -- this module decides
 * *where* each spilled value lives in SRAM: one contiguous slot per op, in
 * ascending op id order. */
#ifndef OPTIFINE_CODEGEN_SRAM_LAYOUT_H
#define OPTIFINE_CODEGEN_SRAM_LAYOUT_H

#include <stddef.h>
#include <stdint.h>

#include "optifine/ir.h"

/* Base address: clears the ATmega128's I/O (0x0000-0x001F) and extended
 * I/O (0x0020-0x005F) memory-mapped register space, and sits above the
 * hand-written ms1/ms2 fixtures' scratch addresses (0x0100 range) in
 * sim/fixtures/, for visual distinction from those hand-written fixtures. */
#define SRAM_LAYOUT_BASE 0x0200
/* ATmega128 has 4096 bytes of internal SRAM starting at 0x0100 (exclusive
 * upper bound of the valid address range). */
#define SRAM_LAYOUT_LIMIT 0x1100

/* DSP scratch arena: DSP_SCRATCH_BYTES bytes placed after every op's tensor,
 * for the DSP ops' own temporaries.
 *
 * It is an OVERLAY, not a partition. The program runs the graph's ops once
 * each, in op-id order, every op's code a single-entry/single-exit region
 * emitted after the previous one's (codegen/program.c), so no two op
 * lifetimes overlap. Any op may therefore use any arena cell, provided it
 * reads a cell only after writing it itself: then no value crosses an op
 * boundary through scratch, and results cross only through graph tensors.
 * test_asm_unit checks that rule over every lowered DSP op's code, and that no
 * data pointer is aimed into the arena. Ranges of different ops may alias;
 * the arena must cover the largest single op's extent, never the sum.
 *
 * Ownership, as offsets into the arena:
 *   OP_FFT_BUTTERFLY  0-25  butterfly temporaries (lower.c BF_*)
 *                     180   block counter        (DSP_SCRATCH_FFT_OUTER_COUNT)
 *   OP_MAGNITUDE      0     element counter      (DSP_SCRATCH_MAG_COUNT),
 *                           aliasing BF_AC across the op boundary
 *   OP_PEAK_EXTRACT   not implemented; the milestone plan's layout reaches 179
 *   Window, BitReverse, Input, Const, Output: none
 *
 * 181 is the FFT's extent: its block counter was put at 180, past the old
 * plan's map, before this lifetime rule was written down. It stays there so
 * the FFT's code and fixtures are unchanged; the most any op touches at once
 * is 27 bytes (the FFT's 26 cells plus its counter). */
#define DSP_SCRATCH_FFT_OUTER_COUNT 180 /* 1 byte: remaining blocks in one FFT stage */
#define DSP_SCRATCH_MAG_COUNT 0         /* 1 byte: remaining bins in OP_MAGNITUDE */
#define DSP_SCRATCH_BYTES 181

typedef struct {
    uint16_t *op_addr; /* indexed by op id -> base SRAM address of that op's output tensor */
    size_t count;
    uint16_t bytes_used;
    uint16_t dsp_scratch_addr; /* base of a reserved DSP_SCRATCH_BYTES region,
                                 * computed unconditionally (harmless overhead
                                 * for ML graphs, which never read it) */
} SramLayout;

/* Number of elements in `op`'s output tensor (product of output_shape, or 1
 * if output_shape_len == 0). */
size_t sram_layout_num_elements(const IrOp *op);

/* Bytes per element for `dtype`: 1 (DT_INT8), 4 (DT_INT32), 2
 * (DT_FIXED_Q15, one Q15 fixed-point real), 4 (DT_COMPLEX_Q15, an
 * interleaved real:imaginary Q15 pair). */
size_t sram_layout_elem_size(DType dtype);

/* Builds a one-pass address table over `graph->ops` in id order. Returns 0
 * on success, non-zero (with an error already printed to stderr) if the
 * layout would exceed SRAM_LAYOUT_LIMIT or if any op uses an unsupported
 * dtype. */
int sram_layout_build(const IrGraph *graph, SramLayout *out);
void sram_layout_free(SramLayout *out);

/* Address of element `elem_index` within op `op_id`'s tensor. */
uint16_t sram_layout_addr(const SramLayout *layout, const IrGraph *graph,
                           size_t op_id, size_t elem_index);

#endif /* OPTIFINE_CODEGEN_SRAM_LAYOUT_H */
