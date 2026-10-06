/* Reference interpreter for MIR (optifine/mir.h), test support only.
 *
 * It executes a verified module directly from the MIR definition --
 * little-endian objects as byte arrays, wrapping integer arithmetic, pointers
 * as (object, offset) pairs -- with no knowledge of any target. Tests run the
 * same module here and, after AVR instruction selection, in the AVR
 * interpreter; agreement checks the backend against MIR's meaning. MIR_TARGET
 * regions are opaque and refused. */
#ifndef OPTIFINE_TEST_MIR_INTERP_H
#define OPTIFINE_TEST_MIR_INTERP_H

#include <stdint.h>

#include "optifine/mir.h"

typedef struct {
    const MirModule *module;
    uint8_t **memory;        /* per object, `size` bytes; CONST objects hold their initializer */
    unsigned long budget;    /* instructions left before giving up */
} MirInterp;

int mir_interp_init(MirInterp *interp, const MirModule *module);
void mir_interp_free(MirInterp *interp);

/* Runs `function` with `args` (one per parameter) and stores the returned
 * value (0 for void) in *result. Returns 0, or -1 on a fault: an out-of-
 * object access, a MIR_TARGET region, budget exhaustion. */
int mir_interp_call(MirInterp *interp, uint32_t function, const int64_t *args, int64_t *result);

#endif /* OPTIFINE_TEST_MIR_INTERP_H */
