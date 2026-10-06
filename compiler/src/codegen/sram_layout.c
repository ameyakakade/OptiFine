#include "optifine/codegen/sram_layout.h"

#include <stdio.h>
#include <stdlib.h>

#include "optifine/invariant.h"

size_t sram_layout_num_elements(const IrOp *op) {
    /* Only verified ops reach code generation, and ir_verify rejects any
     * shape whose product overflows; a wrapped count here would size a
     * tensor wrongly, so it stays checked. */
    size_t n = 0;
    OPTIFINE_INVARIANT(ir_op_tensor_size(op, &n, NULL) == 0);
    return n;
}

size_t sram_layout_elem_size(DType dtype) {
    return ir_dtype_size(dtype);
}

int sram_layout_build(const IrGraph *graph, SramLayout *out) {
    out->op_addr = calloc(graph->count > 0 ? graph->count : 1, sizeof(uint16_t));
    out->count = graph->count;
    out->bytes_used = 0;
    out->dsp_scratch_addr = 0;
    if (!out->op_addr) {
        fprintf(stderr, "sram_layout_build: out of memory\n");
        out->count = 0;
        return -1;
    }

    uint32_t offset = 0; /* wider than uint16_t so the overflow check below is exact */
    for (size_t i = 0; i < graph->count; i++) {
        const IrOp *op = &graph->ops[i];
        size_t elem_size = sram_layout_elem_size(op->dtype);
        if (elem_size == 0) {
            fprintf(stderr, "sram_layout_build: op %zu has unsupported dtype %d for the ML-path layout\n",
                    i, (int)op->dtype);
            free(out->op_addr);
            out->op_addr = NULL;
            return -1;
        }
        size_t bytes = 0;
        if (ir_op_tensor_size(op, NULL, &bytes) != 0 ||
            bytes > (size_t)(SRAM_LAYOUT_LIMIT - SRAM_LAYOUT_BASE) ||
            (uint32_t)SRAM_LAYOUT_BASE + offset + (uint32_t)bytes > (uint32_t)SRAM_LAYOUT_LIMIT) {
            fprintf(stderr, "sram_layout_build: op %zu would push SRAM usage to %u bytes, exceeding the %d-byte budget\n",
                    i, (unsigned)(offset + bytes), SRAM_LAYOUT_LIMIT - SRAM_LAYOUT_BASE);
            free(out->op_addr);
            out->op_addr = NULL;
            return -1;
        }
        out->op_addr[i] = (uint16_t)(SRAM_LAYOUT_BASE + offset);
        offset += (uint32_t)bytes;
    }
    if ((uint32_t)SRAM_LAYOUT_BASE + offset + DSP_SCRATCH_BYTES > (uint32_t)SRAM_LAYOUT_LIMIT) {
        fprintf(stderr,
                "sram_layout_build: reserving the %d-byte DSP scratch region would push SRAM usage "
                "past the %d-byte budget\n",
                DSP_SCRATCH_BYTES, SRAM_LAYOUT_LIMIT - SRAM_LAYOUT_BASE);
        free(out->op_addr);
        out->op_addr = NULL;
        return -1;
    }
    out->dsp_scratch_addr = (uint16_t)(SRAM_LAYOUT_BASE + offset);
    offset += DSP_SCRATCH_BYTES;
    out->bytes_used = (uint16_t)offset;
    return 0;
}

void sram_layout_free(SramLayout *out) {
    free(out->op_addr);
    out->op_addr = NULL;
    out->count = 0;
    out->bytes_used = 0;
    out->dsp_scratch_addr = 0;
}

uint16_t sram_layout_addr(const SramLayout *layout, const IrGraph *graph,
                           size_t op_id, size_t elem_index) {
    size_t elem_size = sram_layout_elem_size(graph->ops[op_id].dtype);
    return (uint16_t)(layout->op_addr[op_id] + elem_index * elem_size);
}
