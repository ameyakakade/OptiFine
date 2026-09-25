#include "optifine/cost_model.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>

/* Hand-rolled parser for the flat `NAME = { energy_nj = X, source = "..." }`
 * shape cost_table.toml uses. If the table grows
 * beyond that one shape, replace this with a real TOML library rather than
 * extending the parsing by hand. */

static void trim(char *s) {
    char *start = s;
    while (isspace((unsigned char)*start)) start++;
    memmove(s, start, strlen(start) + 1);

    size_t len = strlen(s);
    while (len > 0 && isspace((unsigned char)s[len - 1])) {
        s[--len] = '\0';
    }
}

int cost_model_load(const char *toml_path, CostModel *out) {
    out->count = 0;

    FILE *f = fopen(toml_path, "r");
    if (!f) {
        return 1;
    }

    char line[512];
    while (fgets(line, sizeof(line), f) && out->count < COST_MODEL_MAX_ENTRIES) {
        char *hash = strchr(line, '#');
        if (hash) *hash = '\0';

        char *eq = strchr(line, '=');
        char *brace = line[0] ? strchr(line, '{') : NULL;
        if (!eq || !brace || eq > brace) continue;

        char name[COST_MODEL_MNEMONIC_LEN];
        size_t name_len = (size_t)(eq - line);
        if (name_len >= sizeof(name)) continue;
        memcpy(name, line, name_len);
        name[name_len] = '\0';
        trim(name);
        if (name[0] == '\0') continue;

        CostEntry *entry = &out->entries[out->count];
        memset(entry, 0, sizeof(*entry));
        strncpy(entry->mnemonic, name, sizeof(entry->mnemonic) - 1);

        double energy = 0.0;
        char source[COST_MODEL_SOURCE_LEN] = {0};

        char *energy_key = strstr(brace, "energy_nj");
        if (energy_key) {
            sscanf(energy_key, "energy_nj = %lf", &energy);
        }

        char *source_key = strstr(brace, "source");
        if (source_key) {
            char *quote1 = strchr(source_key, '"');
            if (quote1) {
                char *quote2 = strchr(quote1 + 1, '"');
                if (quote2) {
                    size_t src_len = (size_t)(quote2 - quote1 - 1);
                    if (src_len >= sizeof(source)) src_len = sizeof(source) - 1;
                    memcpy(source, quote1 + 1, src_len);
                    source[src_len] = '\0';
                }
            }
        }

        entry->energy_nj = energy;
        strncpy(entry->source, source, sizeof(entry->source) - 1);
        entry->is_placeholder = strstr(source, "PLACEHOLDER") != NULL;

        out->count++;
    }

    fclose(f);
    return 0;
}

const CostEntry *cost_model_lookup(const CostModel *model, const char *mnemonic) {
    for (size_t i = 0; i < model->count; i++) {
        if (strncmp(model->entries[i].mnemonic, mnemonic, COST_MODEL_MNEMONIC_LEN) == 0) {
            return &model->entries[i];
        }
    }
    return NULL;
}
