/* Verification boundary between workload-graph construction and the rest of
 * the compiler.
 *
 * Every frontend -- ONNX ingestion (ingest.c) and the DSP builder
 * (dsp_build.c) today -- hands its IrGraph to ir_verify before SRAM layout,
 * lowering or emission sees it. Code downstream of the boundary may then
 * rely on the contracts below instead of re-checking them; what it still
 * checks for itself are target constraints (SRAM budget, scratch capacity)
 * and its own internal invariants (invariant.h).
 *
 * Checked for every op:
 *   - kind and dtype are enumerators of OpKind and DType
 *   - op id equals its index; the input count matches the kind's arity
 *   - every input names an EARLIER op (topological order, no self-reference;
 *     ops execute once each in id order)
 *   - a shape with dimensions has a shape array, every dimension is nonzero,
 *     and neither the element count nor the byte size overflows size_t
 *   - only OP_CONST carries data, and its length is exactly the tensor's
 *     byte size (layout reserves that many bytes; a longer initializer would
 *     overwrite the next tensor)
 * Checked for the graph:
 *   - at least one op; exactly one OP_INPUT (the one embedded input); exactly
 *     one OP_OUTPUT, and it is the last op (the periodic wrapper reads it
 *     there)
 *   - ML and DSP operators are not mixed (the two lowerings share registers
 *     on the premise that they never meet in one program, registers.h)
 * Checked per operator (the contracts the lowering in lower*.c implements):
 *   ML    Input INT8; Const INT8 or INT32
 *         MatMul  INT8[K] x INT8[N,K] -> INT32[N]
 *         Add     INT32[n] + INT32[n] -> INT32[n]
 *         Relu    INT32[n] -> INT32[n]
 *         Requantize INT32[n] -> INT8[n], both scales positive and finite
 *         ML quantization is symmetric: every zero point is 0
 *   DSP   Input/Const FIXED_Q15
 *         Window      FIXED_Q15[n] x FIXED_Q15[n] -> FIXED_Q15[n]
 *         BitReverse  FIXED_Q15[N] -> COMPLEX_Q15[N], N = DSP_FFT_SIZE
 *         FftButterfly COMPLEX_Q15[N] -> COMPLEX_Q15[N]; the first stage reads
 *                     the BitReverse, each later one the previous stage, and
 *                     there are at most DSP_FFT_LOG2 stages
 *         Magnitude   COMPLEX_Q15[n] -> FIXED_Q15[n], 1 <= n <= 256
 *         PeakExtract FIXED_Q15[n] -> FIXED_Q15[k], 1 <= k <= n <= 255
 *   Output  same dtype and element count as its producer
 *
 * This is the verifier for the workload HIR only. A future generic MIR has
 * its own verifier (mir_verify); see docs/ARCHITECTURE.md. */
#ifndef OPTIFINE_IR_VERIFY_H
#define OPTIFINE_IR_VERIFY_H

#include <stddef.h>

#include "optifine/ir.h"

/* Returns 0 when `graph` satisfies every contract above. Otherwise returns -1
 * and writes a one-line diagnostic naming the first offending op into
 * `message` (capacity `message_len`, may be 0). */
int ir_verify(const IrGraph *graph, char *message, size_t message_len);

/* ir_verify, printing the diagnostic to stderr prefixed with `what` (e.g. the
 * model path). For the compiler driver. */
int ir_verify_or_report(const IrGraph *graph, const char *what);

#endif /* OPTIFINE_IR_VERIFY_H */
