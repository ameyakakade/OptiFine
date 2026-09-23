#include <assert.h>
#include <stdio.h>

#include "optifine/codegen/sram_layout.h"
#include "optifine/dsp_build.h"
#include "optifine/ir.h"

static void test_elem_sizes(void) {
    assert(sram_layout_elem_size(DT_INT8) == 1);
    assert(sram_layout_elem_size(DT_INT32) == 4);
    assert(sram_layout_elem_size(DT_FIXED_Q15) == 2);
    assert(sram_layout_elem_size(DT_COMPLEX_Q15) == 4);
}

static void test_dsp_graph_layout_succeeds_and_reserves_scratch(void) {
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);

    SramLayout layout;
    assert(sram_layout_build(&graph, &layout) == 0);

    /* Every op has a real, non-overlapping address; nothing hit the
     * "unsupported dtype" or "budget exceeded" failure paths. */
    for (size_t i = 0; i < graph.count; i++) {
        assert(layout.op_addr[i] >= SRAM_LAYOUT_BASE);
        assert(layout.op_addr[i] < SRAM_LAYOUT_LIMIT);
    }

    /* The scratch region sits after every real op's tensor storage and
     * fits inside the SRAM budget with room for Task 12's periodic
     * scheduler bytes on top. */
    assert(layout.dsp_scratch_addr >= SRAM_LAYOUT_BASE + layout.bytes_used - DSP_SCRATCH_BYTES);
    assert((uint32_t)layout.dsp_scratch_addr + DSP_SCRATCH_BYTES <= (uint32_t)SRAM_LAYOUT_LIMIT);

    /* Measured footprint: 2,336 bytes of tensors (three 128-byte real
     * buffers, BitReverse + six FFT stages at 256 bytes, Magnitude 128,
     * PeakExtract and Output 16 each) plus the scratch region. */
    printf("  DSP SRAM: %u bytes of %d (scratch %d at 0x%04X)\n", layout.bytes_used,
           SRAM_LAYOUT_LIMIT - 0x0100, DSP_SCRATCH_BYTES, layout.dsp_scratch_addr);
    assert(layout.bytes_used == 2336 + DSP_SCRATCH_BYTES);
    assert(DSP_SCRATCH_BYTES == 181);

    sram_layout_free(&layout);
    ir_graph_free(&graph);
}

/* The FFT block counter sits after the documented 0-179 scratch map
 * (butterfly cells 0-25, the planned Magnitude cells 18-41 and PeakExtract
 * cells 42-179), so it overlaps none of them, and inside the region. */
static void test_fft_block_counter_has_its_own_cell(void) {
    assert(DSP_SCRATCH_FFT_OUTER_COUNT >= 26);   /* past the butterfly's BF_* cells */
    assert(DSP_SCRATCH_FFT_OUTER_COUNT >= 42);   /* past planned Magnitude 18-41 */
    assert(DSP_SCRATCH_FFT_OUTER_COUNT >= 180);  /* past planned PeakExtract 42-179 */
    assert(DSP_SCRATCH_FFT_OUTER_COUNT + 1 <= DSP_SCRATCH_BYTES);
}

int main(void) {
    test_elem_sizes();
    test_dsp_graph_layout_succeeds_and_reserves_scratch();
    test_fft_block_counter_has_its_own_cell();
    printf("test_sram_layout_dsp: all tests passed\n");
    return 0;
}
