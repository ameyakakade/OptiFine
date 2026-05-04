#include "optifine/dsp_build.h"

#include <stdarg.h>
#include <stdlib.h>

static size_t *shape1(size_t n) {
    size_t *s = malloc(sizeof(size_t));
    s[0] = n;
    return s;
}

static size_t *make_inputs(size_t count, ...) {
    if (count == 0) {
        return NULL;
    }
    size_t *arr = malloc(count * sizeof(size_t));
    va_list args;
    va_start(args, count);
    for (size_t i = 0; i < count; i++) {
        arr[i] = va_arg(args, size_t);
    }
    va_end(args);
    return arr;
}

int dsp_build_pipeline(IrGraph *out) {
    ir_graph_init(out);

    /* Raw audio samples and the fixed window coefficient table (e.g.
     * Hamming) are the two leaves the pipeline is built from. */
    size_t raw_input = ir_graph_push(out, OP_INPUT, NULL, 0,
                                      shape1(DSP_FFT_SIZE), 1, DT_FIXED_Q15, NULL);
    size_t window_coeffs = ir_graph_push(out, OP_CONST, NULL, 0,
                                          shape1(DSP_FFT_SIZE), 1, DT_FIXED_Q15, NULL);

    size_t windowed = ir_graph_push(out, OP_WINDOW,
                                     make_inputs(2, raw_input, window_coeffs), 2,
                                     shape1(DSP_FFT_SIZE), 1, DT_FIXED_Q15, NULL);

    /* Bit-reversal also marks where the pipeline goes from real Q15 samples
     * to the complex Q15 working buffer the butterfly stages operate on
     * (imaginary halves start at zero) -- this is an IR-level typing
     * decision, not a codegen detail yet, since candidate generation for
     * DSP ops doesn't exist until milestone 5. */
    size_t reversed = ir_graph_push(out, OP_BIT_REVERSE,
                                     make_inputs(1, windowed), 1,
                                     shape1(DSP_FFT_SIZE), 1, DT_COMPLEX_Q15, NULL);

    size_t stage = reversed;
    for (int i = 0; i < DSP_FFT_LOG2; i++) {
        stage = ir_graph_push(out, OP_FFT_BUTTERFLY,
                               make_inputs(1, stage), 1,
                               shape1(DSP_FFT_SIZE), 1, DT_COMPLEX_Q15, NULL);
    }

    size_t magnitude = ir_graph_push(out, OP_MAGNITUDE,
                                      make_inputs(1, stage), 1,
                                      shape1(DSP_FFT_SIZE), 1, DT_FIXED_Q15, NULL);

    size_t peaks = ir_graph_push(out, OP_PEAK_EXTRACT,
                                  make_inputs(1, magnitude), 1,
                                  shape1(DSP_MAX_PEAKS), 1, DT_FIXED_Q15, NULL);

    ir_graph_push(out, OP_OUTPUT,
                  make_inputs(1, peaks), 1,
                  shape1(DSP_MAX_PEAKS), 1, DT_FIXED_Q15, NULL);

    return 0;
}
