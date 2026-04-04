/* ONNX protobuf -> IrGraph. Milestone 3.
 *
 * Scope: only enough of the ONNX graph proto to build the OpKind set in
 * ir.h (Input, Const, MatMul, Add, Relu, Requantize, Output). No general
 * ONNX operator coverage -- see spec section 9 (non-goals). */
#ifndef OPTIFINE_INGEST_H
#define OPTIFINE_INGEST_H

#include "optifine/ir.h"

/* Returns 0 on success, non-zero on failure. On success, `out` owns the
 * resulting graph and must be freed with ir_graph_free(). */
int ingest_load_onnx(const char *path, IrGraph *out);

#endif /* OPTIFINE_INGEST_H */
