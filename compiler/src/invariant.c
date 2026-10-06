#include "optifine/invariant.h"

#include <stdio.h>
#include <stdlib.h>

void optifine_invariant_failed(const char *condition, const char *file, int line) {
    fprintf(stderr, "optifine: internal invariant failed: %s (%s:%d)\n", condition, file, line);
    abort();
}
