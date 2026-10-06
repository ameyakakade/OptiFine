#include "optifine/dsp_build.h"

#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
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
    if (s) s[0] = n;
    return s;
}

static size_t *make_inputs(size_t count, ...) {
    size_t *arr = malloc(count * sizeof(size_t));
    if (!arr) return NULL;
    va_list args;
    va_start(args, count);
    for (size_t i = 0; i < count; i++) {
        arr[i] = va_arg(args, size_t);
    }
    va_end(args);
    return arr;
}

/* Pushes one op of the fixed pipeline: a 1-D tensor of `len` elements. Any
 * allocation failure frees what was allocated and fails the build. */
static int push1(IrGraph *g, OpKind kind, size_t *inputs, size_t num_inputs, size_t len, DType dtype,
                 size_t *out_id) {
    size_t *shape = shape1(len);
    if ((num_inputs > 0 && !inputs) || !shape) {
        free(inputs);
        free(shape);
        return -1;
    }
    return ir_graph_push(g, kind, inputs, num_inputs, shape, 1, dtype, NULL, out_id);
}

int dsp_build_pipeline(IrGraph *out) {
    ir_graph_init(out);

    /* Raw audio samples and the fixed window coefficient table (e.g.
     * Hamming) are the two leaves the pipeline is built from. */
    size_t raw_input, window_coeffs, windowed, reversed, stage, magnitude, peaks, output;
    if (push1(out, OP_INPUT, NULL, 0, DSP_FFT_SIZE, DT_FIXED_Q15, &raw_input) != 0 ||
        push1(out, OP_CONST, NULL, 0, DSP_FFT_SIZE, DT_FIXED_Q15, &window_coeffs) != 0) {
        goto fail;
    }
    {
        /* Hamming window, computed host-side via libm at compiler-build
         * time (everything is baked in at compile time --
         * no target-side trigonometry, no runtime coefficient generation). */
        int16_t *coeffs = malloc(DSP_FFT_SIZE * sizeof(int16_t));
        if (!coeffs) goto fail;
        for (int n = 0; n < DSP_FFT_SIZE; n++) {
            double w = 0.54 - 0.46 * cos(2.0 * M_PI * n / (DSP_FFT_SIZE - 1));
            long q = lround(w * 32767.0);
            if (q > 32767) q = 32767;
            coeffs[n] = (int16_t)q;
        }
        ir_op_set_data(out, window_coeffs, coeffs, DSP_FFT_SIZE * sizeof(int16_t));
    }

    if (push1(out, OP_WINDOW, make_inputs(2, raw_input, window_coeffs), 2, DSP_FFT_SIZE, DT_FIXED_Q15,
              &windowed) != 0) {
        goto fail;
    }

    /* Bit-reversal also marks where the pipeline goes from real Q15 samples
     * to the complex Q15 working buffer the butterfly stages operate on
     * (imaginary halves start at zero) -- an IR-level typing decision that
     * lower_bit_reverse implements by zeroing each imaginary half. */
    if (push1(out, OP_BIT_REVERSE, make_inputs(1, windowed), 1, DSP_FFT_SIZE, DT_COMPLEX_Q15,
              &reversed) != 0) {
        goto fail;
    }

    stage = reversed;
    for (int i = 0; i < DSP_FFT_LOG2; i++) {
        if (push1(out, OP_FFT_BUTTERFLY, make_inputs(1, stage), 1, DSP_FFT_SIZE, DT_COMPLEX_Q15,
                  &stage) != 0) {
            goto fail;
        }
    }

    if (push1(out, OP_MAGNITUDE, make_inputs(1, stage), 1, DSP_FFT_SIZE, DT_FIXED_Q15, &magnitude) != 0 ||
        push1(out, OP_PEAK_EXTRACT, make_inputs(1, magnitude), 1, DSP_MAX_PEAKS, DT_FIXED_Q15, &peaks) != 0 ||
        push1(out, OP_OUTPUT, make_inputs(1, peaks), 1, DSP_MAX_PEAKS, DT_FIXED_Q15, &output) != 0) {
        goto fail;
    }
    return 0;

fail:
    fprintf(stderr, "dsp_build: out of memory building the DSP graph\n");
    ir_graph_free(out);
    return -1;
}
