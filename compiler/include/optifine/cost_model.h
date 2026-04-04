/* cost_table.toml loader. Every entry needs a `source` before it's treated
 * as real -- see ../../cost_table.toml and SOURCES.md. */
#ifndef OPTIFINE_COST_MODEL_H
#define OPTIFINE_COST_MODEL_H

#include <stddef.h>

#define COST_MODEL_MAX_ENTRIES 64
#define COST_MODEL_MNEMONIC_LEN 8
#define COST_MODEL_SOURCE_LEN 256

typedef struct {
    char mnemonic[COST_MODEL_MNEMONIC_LEN];
    double energy_nj;
    char source[COST_MODEL_SOURCE_LEN];
    int is_placeholder;
} CostEntry;

typedef struct {
    CostEntry entries[COST_MODEL_MAX_ENTRIES];
    size_t count;
} CostModel;

/* Returns 0 on success, non-zero on failure (missing file, parse error). */
int cost_model_load(const char *toml_path, CostModel *out);

/* Returns NULL if `mnemonic` has no entry in the table. */
const CostEntry *cost_model_lookup(const CostModel *model, const char *mnemonic);

#endif /* OPTIFINE_COST_MODEL_H */
