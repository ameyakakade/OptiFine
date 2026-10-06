#include "optifine/codegen/registers.h"

const int kMatmulCacheRegs[MATMUL_CACHE_POOL_SIZE] = {
    9, 10, 11, 12, 13, 14, 15, /* unused by any lowering */
    24, 25,                    /* REG_SCRATCH0/1, unused by lower_matmul */
    26, 27, 28, 29, 30, 31,    /* unused by any lowering */
    3, 4, 5, 6, 7, 8,          /* Requantize's product buffer, never live during a MatMul */
};
