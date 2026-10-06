#include "optifine/cost_model.h"

#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Hand-rolled parser for the one flat shape cost_table.toml uses:
 *
 *     [instructions]
 *     NAME = { energy_nj = <number>, source = "<citation>" }
 *
 * with `#` comments and blank lines. It fails closed: anything else -- an
 * unknown section or key, a malformed or non-positive number, a missing or
 * empty source, a duplicate NAME, an over-long line or field, more entries
 * than COST_MODEL_MAX_ENTRIES -- is reported with its line number and makes
 * cost_model_load fail, rather than being skipped or read as zero. If the
 * table ever needs more of TOML than this, replace the parser with a real TOML
 * library instead of extending it. */

typedef struct {
    const char *path;
    unsigned line;
    const char *p; /* cursor into the current line */
} Parser;

static int fail(const Parser *ps, const char *what) {
    fprintf(stderr, "cost table %s:%u: %s\n", ps->path, ps->line, what);
    return -1;
}

static void skip_space(Parser *ps) {
    while (*ps->p == ' ' || *ps->p == '\t') ps->p++;
}

/* True when nothing but whitespace and an optional comment remains. */
static int at_end(Parser *ps) {
    skip_space(ps);
    return *ps->p == '\0' || *ps->p == '#' || *ps->p == '\n' || *ps->p == '\r';
}

static int expect(Parser *ps, char c) {
    skip_space(ps);
    if (*ps->p != c) return -1;
    ps->p++;
    return 0;
}

/* [A-Za-z_][A-Za-z0-9_]*, copied into out (capacity cap). */
static int read_identifier(Parser *ps, char *out, size_t cap) {
    skip_space(ps);
    size_t n = 0;
    if (!(isalpha((unsigned char)*ps->p) || *ps->p == '_')) return -1;
    while (isalnum((unsigned char)*ps->p) || *ps->p == '_') {
        if (n + 1 >= cap) return -2;
        out[n++] = *ps->p++;
    }
    out[n] = '\0';
    return 0;
}

static int read_number(Parser *ps, double *out) {
    skip_space(ps);
    char *end = NULL;
    errno = 0;
    double v = strtod(ps->p, &end);
    if (end == ps->p || errno != 0 || !isfinite(v)) return -1;
    ps->p = end;
    *out = v;
    return 0;
}

/* A double-quoted string with no escapes, copied into out (capacity cap). */
static int read_string(Parser *ps, char *out, size_t cap) {
    skip_space(ps);
    if (*ps->p != '"') return -1;
    ps->p++;
    size_t n = 0;
    while (*ps->p != '"') {
        if (*ps->p == '\0' || *ps->p == '\n' || *ps->p == '\\') return -1;
        if (n + 1 >= cap) return -2;
        out[n++] = *ps->p++;
    }
    ps->p++;
    out[n] = '\0';
    return 0;
}

static int parse_entry(Parser *ps, CostModel *model) {
    if (model->count == COST_MODEL_MAX_ENTRIES) {
        return fail(ps, "more entries than COST_MODEL_MAX_ENTRIES");
    }
    CostEntry *entry = &model->entries[model->count];
    memset(entry, 0, sizeof(*entry));

    int rc = read_identifier(ps, entry->mnemonic, sizeof(entry->mnemonic));
    if (rc == -2) return fail(ps, "entry name is longer than COST_MODEL_MNEMONIC_LEN allows");
    if (rc != 0) return fail(ps, "expected an entry name");
    for (size_t i = 0; i < model->count; i++) {
        if (strcmp(model->entries[i].mnemonic, entry->mnemonic) == 0) {
            return fail(ps, "duplicate entry name");
        }
    }
    if (expect(ps, '=') != 0 || expect(ps, '{') != 0) return fail(ps, "expected `= {` after the entry name");

    int have_energy = 0, have_source = 0;
    for (;;) {
        char key[16];
        if (read_identifier(ps, key, sizeof(key)) != 0) return fail(ps, "expected a key inside `{ }`");
        if (expect(ps, '=') != 0) return fail(ps, "expected `=` after the key");
        if (strcmp(key, "energy_nj") == 0) {
            if (have_energy) return fail(ps, "energy_nj given twice");
            if (read_number(ps, &entry->energy_nj) != 0) return fail(ps, "energy_nj is not a finite number");
            if (!(entry->energy_nj > 0.0)) return fail(ps, "energy_nj must be positive");
            have_energy = 1;
        } else if (strcmp(key, "source") == 0) {
            if (have_source) return fail(ps, "source given twice");
            rc = read_string(ps, entry->source, sizeof(entry->source));
            if (rc == -2) return fail(ps, "source is longer than COST_MODEL_SOURCE_LEN allows");
            if (rc != 0) return fail(ps, "source is not a double-quoted string without escapes");
            if (entry->source[0] == '\0') return fail(ps, "source is empty");
            have_source = 1;
        } else {
            return fail(ps, "unknown key (expected energy_nj or source)");
        }
        skip_space(ps);
        if (*ps->p == ',') {
            ps->p++;
            continue;
        }
        if (*ps->p == '}') {
            ps->p++;
            break;
        }
        return fail(ps, "expected `,` or `}`");
    }
    if (!have_energy) return fail(ps, "entry has no energy_nj");
    if (!have_source) return fail(ps, "entry has no source");
    if (!at_end(ps)) return fail(ps, "unexpected text after the entry");

    entry->is_placeholder = strstr(entry->source, "PLACEHOLDER") != NULL;
    model->count++;
    return 0;
}

int cost_model_load(const char *toml_path, CostModel *out) {
    out->count = 0;

    FILE *f = fopen(toml_path, "r");
    if (!f) {
        fprintf(stderr, "cost table %s: cannot open\n", toml_path);
        return 1;
    }

    Parser ps = {toml_path, 0, NULL};
    int in_instructions = 0;
    int rc = 0;
    char line[1024];
    while (rc == 0 && fgets(line, sizeof(line), f)) {
        ps.line++;
        ps.p = line;
        if (strchr(line, '\n') == NULL && !feof(f)) {
            rc = fail(&ps, "line is longer than the parser's line buffer");
            break;
        }
        if (at_end(&ps)) continue;
        if (*ps.p == '[') {
            if (strncmp(ps.p, "[instructions]", 14) != 0) {
                rc = fail(&ps, "unknown section (only [instructions] is supported)");
                break;
            }
            ps.p += 14;
            if (!at_end(&ps)) {
                rc = fail(&ps, "unexpected text after the section header");
                break;
            }
            in_instructions = 1;
            continue;
        }
        if (!in_instructions) {
            rc = fail(&ps, "entry outside the [instructions] section");
            break;
        }
        rc = parse_entry(&ps, out);
    }
    if (rc == 0 && ferror(f)) {
        fprintf(stderr, "cost table %s: read error\n", toml_path);
        rc = -1;
    }
    fclose(f);
    if (rc == 0 && out->count == 0) {
        fprintf(stderr, "cost table %s: no entries\n", toml_path);
        rc = -1;
    }
    if (rc != 0) {
        out->count = 0;
        return 1;
    }
    return 0;
}

const CostEntry *cost_model_lookup(const CostModel *model, const char *mnemonic) {
    for (size_t i = 0; i < model->count; i++) {
        if (strcmp(model->entries[i].mnemonic, mnemonic) == 0) {
            return &model->entries[i];
        }
    }
    return NULL;
}
