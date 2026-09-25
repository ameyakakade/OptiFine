/* DSP pipeline construction -- builder API, not a parser. Unlike the ML path there is no standard portable graph format for a
 * fixed-size audio pipeline to ingest from, so the IR is constructed
 * directly here instead of parsed from a file. */
#ifndef OPTIFINE_DSP_BUILD_H
#define OPTIFINE_DSP_BUILD_H

#include "optifine/ir.h"

/* Fixed transform size: exactly one hardcoded, power-of-2 transform length
 * -- no runtime-configurable size. 64 was picked over 128 as the smaller of
 * the two sizes considered. */
#define DSP_FFT_SIZE 64
#define DSP_FFT_LOG2 6  /* log2(DSP_FFT_SIZE) = number of butterfly stages */

/* Fixed cap on extracted peaks -- same fixed-size-not-general-purpose spirit
 * as DSP_FFT_SIZE, not a tunable parameter. */
#define DSP_MAX_PEAKS 8

/* Builds Window -> BitReverse -> FftButterfly (xDSP_FFT_LOG2) -> Magnitude
 * -> PeakExtract -> Output into `out`. `out` must not already be
 * initialized (this calls ir_graph_init itself). Returns 0 on success. */
int dsp_build_pipeline(IrGraph *out);

#endif /* OPTIFINE_DSP_BUILD_H */
