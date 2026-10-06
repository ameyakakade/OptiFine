/* cost_table.toml parsing: the real table loads with every entry intact, and
 * each malformed variant is refused rather than half-read. The negative cases
 * are written to scratch files in the build directory.
 *
 * argv: <cost_table.toml> <scratch dir> */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "optifine/cost_model.h"

static const char *g_dir;

static int load_text(const char *text, CostModel *model) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/cost_model_case.toml", g_dir);
    FILE *f = fopen(path, "w");
    assert(f);
    fputs(text, f);
    fclose(f);
    return cost_model_load(path, model);
}

#define ENTRY(name, value) name " = { energy_nj = " value ", source = \"cited\" }\n"

int main(int argc, char **argv) {
    assert(argc == 3);
    g_dir = argv[2];
    static CostModel model;

    /* The real table: nine entries, all positive, all cited. */
    assert(cost_model_load(argv[1], &model) == 0);
    assert(model.count == 9);
    const CostEntry *add = cost_model_lookup(&model, "ADD");
    const CostEntry *mul = cost_model_lookup(&model, "MUL");
    assert(add && add->energy_nj == 2.8375 && !add->is_placeholder);
    assert(mul && mul->energy_nj == 5.675);
    assert(strstr(add->source, "Avrora") != NULL);
    assert(cost_model_lookup(&model, "AD") == NULL);
    assert(cost_model_lookup(&model, "add") == NULL);
    printf("  real table: 9 entries, ADD 2.8375 nJ, MUL 5.675 nJ\n");

    /* Accepted variants: comments, blank lines, either key order. */
    assert(load_text("# header\n\n[instructions]\n"
                     "A = { source = \"s # not a comment\", energy_nj = 1.5 } # trailing\n",
                     &model) == 0);
    assert(model.count == 1 && model.entries[0].energy_nj == 1.5);
    assert(strcmp(model.entries[0].source, "s # not a comment") == 0);
    printf("  comments, blank lines and key order: accepted\n");

    static const struct {
        const char *text;
        const char *why;
    } bad[] = {
        {"", "empty file"},
        {"[instructions]\n", "no entries"},
        {ENTRY("ADD", "2.8"), "entry before [instructions]"},
        {"[other]\n" ENTRY("ADD", "2.8"), "unknown section"},
        {"[instructions]\n" ENTRY("ADD", "abc"), "non-numeric energy"},
        {"[instructions]\n" ENTRY("ADD", "2.8x"), "trailing junk after the number"},
        {"[instructions]\n" ENTRY("ADD", "0"), "zero energy"},
        {"[instructions]\n" ENTRY("ADD", "-1"), "negative energy"},
        {"[instructions]\n" ENTRY("ADD", "inf"), "infinite energy"},
        {"[instructions]\nADD = { source = \"cited\" }\n", "missing energy_nj"},
        {"[instructions]\nADD = { energy_nj = 2.8 }\n", "missing source"},
        {"[instructions]\nADD = { energy_nj = 2.8, source = \"\" }\n", "empty source"},
        {"[instructions]\nADD = { energy_nj = 2.8, source = \"open }\n", "unterminated source"},
        {"[instructions]\nADD = { energy_nj = 2.8, energy_nj = 2.8, source = \"s\" }\n", "repeated key"},
        {"[instructions]\nADD = { energy_nj = 2.8, cycles = 1, source = \"s\" }\n", "unknown key"},
        {"[instructions]\n" ENTRY("ADD", "2.8") ENTRY("ADD", "5.6"), "duplicate name"},
        {"[instructions]\n" ENTRY("THIS_NAME_IS_FAR_TOO_LONG", "2.8"), "over-long name"},
        {"[instructions]\nADD = { energy_nj = 2.8, source = \"s\" } extra\n", "text after the entry"},
        {"[instructions]\nADD { energy_nj = 2.8, source = \"s\" }\n", "missing `=`"},
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        model.count = 99;
        assert(load_text(bad[i].text, &model) != 0);
        assert(model.count == 0);
        printf("  refused: %s\n", bad[i].why);
    }

    /* Over-long source, and more entries than the table holds. */
    {
        static char text[8192];
        char source[COST_MODEL_SOURCE_LEN + 8];
        memset(source, 's', sizeof(source) - 1);
        source[sizeof(source) - 1] = '\0';
        snprintf(text, sizeof(text), "[instructions]\nADD = { energy_nj = 1, source = \"%s\" }\n", source);
        assert(load_text(text, &model) != 0);
        printf("  refused: over-long source\n");

        size_t len = (size_t)snprintf(text, sizeof(text), "[instructions]\n");
        for (int i = 0; i <= COST_MODEL_MAX_ENTRIES; i++) {
            len += (size_t)snprintf(text + len, sizeof(text) - len, "E%d = { energy_nj = 1, source = \"s\" }\n", i);
        }
        assert(len < sizeof(text));
        assert(load_text(text, &model) != 0);
        printf("  refused: %d entries (capacity %d)\n", COST_MODEL_MAX_ENTRIES + 1, COST_MODEL_MAX_ENTRIES);
    }

    printf("test_cost_model: all tests passed\n");
    return 0;
}
