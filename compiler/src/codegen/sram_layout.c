#include "optifine/codegen/sram_layout.h"

#include <stdio.h>
#include <stdlib.h>

size_t sram_layout_num_elements(const IrOp *op) {
    if (op->output_shape_len == 0) {
        return 1;
    }
    size_t n = 1;
    for (size_t i = 0; i < op->output_shape_len; i++) {
        n *= op->output_shape[i];
    }
    return n;
}

size_t sram_layout_elem_size(DType dtype) {
    switch (dtype) {
        case DT_INT8:
            return 1;
        case DT_INT32:
            return 4;
        default:
            return 0; /* unsupported on the ML path Phase A targets */
    }
}

int sram_layout_build(const IrGraph *graph, SramLayout *out) {
    out->op_addr = calloc(graph->count, sizeof(uint16_t));
    out->count = graph->count;
    out->bytes_used = 0;

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
        size_t bytes = sram_layout_num_elements(op) * elem_size;
        if ((uint32_t)SRAM_LAYOUT_BASE + offset + bytes > (uint32_t)SRAM_LAYOUT_LIMIT) {
            fprintf(stderr, "sram_layout_build: op %zu would push SRAM usage to %u bytes, exceeding the %d-byte budget\n",
                    i, (unsigned)(offset + bytes), SRAM_LAYOUT_LIMIT - SRAM_LAYOUT_BASE);
            free(out->op_addr);
            out->op_addr = NULL;
            return -1;
        }
        out->op_addr[i] = (uint16_t)(SRAM_LAYOUT_BASE + offset);
        offset += (uint32_t)bytes;
    }
    out->bytes_used = (uint16_t)offset;
    return 0;
}

void sram_layout_free(SramLayout *out) {
    free(out->op_addr);
    out->op_addr = NULL;
    out->count = 0;
    out->bytes_used = 0;
}

uint16_t sram_layout_addr(const SramLayout *layout, const IrGraph *graph,
                           size_t op_id, size_t elem_index) {
    size_t elem_size = sram_layout_elem_size(graph->ops[op_id].dtype);
    return (uint16_t)(layout->op_addr[op_id] + elem_index * elem_size);
}
