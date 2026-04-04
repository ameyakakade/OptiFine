#include "optifine/ingest.h"

#include <stdio.h>

int ingest_load_onnx(const char *path, IrGraph *out) {
    (void)path;
    (void)out;
    /* TODO(milestone 3): parse the ONNX protobuf (e.g. via protobuf-c
     * against the onnx.proto schema) and lower supported node types into
     * IrOp. */
    fprintf(stderr, "ingest_load_onnx: not yet implemented\n");
    return 1;
}
