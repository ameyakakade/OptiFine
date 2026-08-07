#include "optifine/dsp_build.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>

/* M_PI is POSIX, not C11: absent under a strict -std=c11 and on MSVC
 * without _USE_MATH_DEFINES, both of which this project supports (see
 * README's toolchain matrix). It resolves today only because CMake's
 * C_EXTENSIONS defaults ON (giving -std=gnu11), which is an unset
 * default rather than a decision -- confirmed empirically, not assumed.
 * Define it locally rather than depend on that. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

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
    {
        /* Hamming window, computed host-side via libm at compiler-build
         * time (spec v2's "everything compile-time-baked" philosophy --
         * no target-side trigonometry, no runtime coefficient generation). */
        int16_t *coeffs = malloc(DSP_FFT_SIZE * sizeof(int16_t));
        for (int n = 0; n < DSP_FFT_SIZE; n++) {
            double w = 0.54 - 0.46 * cos(2.0 * M_PI * n / (DSP_FFT_SIZE - 1));
            long q = lround(w * 32767.0);
            if (q > 32767) q = 32767;
            coeffs[n] = (int16_t)q;
        }
        ir_op_set_data(out, window_coeffs, coeffs, DSP_FFT_SIZE * sizeof(int16_t));
    }

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
