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

typedef struct {
    uint16_t *op_addr; /* indexed by op id -> base SRAM address of that op's output tensor */
    size_t count;
    uint16_t bytes_used;
} SramLayout;

/* Number of elements in `op`'s output tensor (product of output_shape, or 1
 * if output_shape_len == 0). */
size_t sram_layout_num_elements(const IrOp *op);

/* Bytes per element for `dtype`. Only DT_INT8 (1 byte) and DT_INT32
 * (4 bytes) are supported -- Phase A's naive codegen is ML-path only; the
 * DSP path's DT_FIXED_Q15/DT_COMPLEX_Q15 are out of scope here. */
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
