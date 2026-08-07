#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "optifine/dsp_build.h"
#include "optifine/ir.h"

/* M_PI is POSIX, not C11: absent under a strict -std=c11 and on MSVC
 * without _USE_MATH_DEFINES, both of which this project supports (see
 * README's toolchain matrix). It resolves here only because CMake's
 * C_EXTENSIONS defaults ON, which is an unset default, not a decision.
 * Define it locally rather than depend on that. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static void test_pipeline_shape(void) {
    IrGraph graph;
    int rc = dsp_build_pipeline(&graph);
    assert(rc == 0);

    /* Input, Const(window) -> Window -> BitReverse -> 6x FftButterfly ->
     * Magnitude -> PeakExtract -> Output */
    size_t expected_count = 2 + 1 + 1 + DSP_FFT_LOG2 + 1 + 1 + 1;
    assert(graph.count == expected_count);

    assert(graph.ops[0].kind == OP_INPUT);
    assert(graph.ops[0].output_shape[0] == DSP_FFT_SIZE);

    assert(graph.ops[1].kind == OP_CONST);
    assert(graph.ops[1].output_shape[0] == DSP_FFT_SIZE);

    assert(graph.ops[2].kind == OP_WINDOW);
    assert(graph.ops[2].num_inputs == 2);
    assert(graph.ops[2].inputs[0] == 0);
    assert(graph.ops[2].inputs[1] == 1);
    assert(graph.ops[2].dtype == DT_FIXED_Q15);

    assert(graph.ops[3].kind == OP_BIT_REVERSE);
    assert(graph.ops[3].inputs[0] == 2);
    assert(graph.ops[3].dtype == DT_COMPLEX_Q15);

    size_t butterfly_start = 4;
    for (int i = 0; i < DSP_FFT_LOG2; i++) {
        const IrOp *op = &graph.ops[butterfly_start + (size_t)i];
        assert(op->kind == OP_FFT_BUTTERFLY);
        assert(op->dtype == DT_COMPLEX_Q15);
        size_t expected_input = (i == 0) ? 3 : butterfly_start + (size_t)i - 1;
        assert(op->inputs[0] == expected_input);
    }

    size_t magnitude_id = butterfly_start + DSP_FFT_LOG2;
    assert(graph.ops[magnitude_id].kind == OP_MAGNITUDE);
    assert(graph.ops[magnitude_id].dtype == DT_FIXED_Q15);
    assert(graph.ops[magnitude_id].inputs[0] == magnitude_id - 1);

    size_t peaks_id = magnitude_id + 1;
    assert(graph.ops[peaks_id].kind == OP_PEAK_EXTRACT);
    assert(graph.ops[peaks_id].output_shape[0] == DSP_MAX_PEAKS);

    size_t output_id = peaks_id + 1;
    assert(graph.ops[output_id].kind == OP_OUTPUT);
    assert(graph.ops[output_id].inputs[0] == peaks_id);

    ir_graph_free(&graph);
}

static void test_window_coeffs_are_real_hamming_values(void) {
    IrGraph graph;
    assert(dsp_build_pipeline(&graph) == 0);

    const IrOp *window_op = &graph.ops[1];
    assert(window_op->kind == OP_CONST);
    assert(window_op->data != NULL);
    assert(window_op->data_len == DSP_FFT_SIZE * 2);

    const int16_t *coeffs = (const int16_t *)window_op->data;
    /* Hamming window: w[n] = 0.54 - 0.46*cos(2*pi*n/(N-1)). Endpoints are
     * the well-known 0.08 value; the midpoint is the well-known 1.0
     * value -- checked in Q15 within 2 LSB rather than re-deriving the
     * formula in the test. */
    double expected_first = 0.54 - 0.46 * cos(0.0);
    double actual_first = (double)coeffs[0] / 32768.0;
    assert(fabs(actual_first - expected_first) < 2.0 / 32768.0);

    double expected_mid = 0.54 - 0.46 * cos(2.0 * M_PI * (DSP_FFT_SIZE / 2) / (DSP_FFT_SIZE - 1));
    double actual_mid = (double)coeffs[DSP_FFT_SIZE / 2] / 32768.0;
    assert(fabs(actual_mid - expected_mid) < 2.0 / 32768.0);

    ir_graph_free(&graph);
}

int main(void) {
    test_pipeline_shape();
    test_window_coeffs_are_real_hamming_values();
    printf("test_dsp_build: all tests passed\n");
    return 0;
}
