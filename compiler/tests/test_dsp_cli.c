/* `optifine --dsp` through the real binary, as a user runs it: the default
 * invocation writes a complete program, a bad or short --input fails, and
 * every combination the DSP mode refuses exits with a usage error rather than
 * failing deep inside codegen. Shells out like test_periodic does.
 *
 * argv: <optifine> <cost_table.toml> <scratch dir> */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *g_bin, *g_cost, *g_dir;

static int run(const char *args) {
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "%s %s > %s/cli.log 2>&1", g_bin, args, g_dir);
    int rc = system(cmd);
    return rc;
}

static char *slurp(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    rewind(f);
    char *s = malloc((size_t)n + 1);
    size_t got = fread(s, 1, (size_t)n, f);
    s[got] = 0;
    fclose(f);
    return s;
}

int main(int argc, char **argv) {
    assert(argc == 4);
    g_bin = argv[1]; g_cost = argv[2]; g_dir = argv[3];
    char args[1024], out[512], in[512];

    snprintf(out, sizeof(out), "%s/dsp_cli_out.s", g_dir);
    snprintf(args, sizeof(args), "--dsp --cost-table %s --out %s", g_cost, out);
    assert(run(args) == 0);
    char *s = slurp(out);
    assert(s);
    assert(strstr(s, "_start:") && strstr(s, "break") && strstr(s, ".Ltw:") && strstr(s, "mulsu"));
    assert(strstr(strstr(s, ".Ltw:") + 1, ".Ltw:") == NULL);        /* one table */
    char *log = slurp(argv[3] ? (snprintf(in, sizeof(in), "%s/cli.log", g_dir), in) : NULL);
    assert(log && strstr(log, "total: ") && strstr(log, "break included"));
    free(s); free(log);
    printf("  --dsp (default input): program written, one twiddle table, cost reported\n");

    /* An explicit --input in the documented format. */
    snprintf(in, sizeof(in), "%s/dsp_cli_in.txt", g_dir);
    FILE *f = fopen(in, "w");
    fprintf(f, "# ramp\n");
    for (int i = 0; i < 64; i++) fprintf(f, "%d%c", i * 500 - 16000, (i % 8 == 7) ? '\n' : ' ');
    fclose(f);
    snprintf(args, sizeof(args), "--dsp --cost-table %s --input %s --out %s", g_cost, in, out);
    assert(run(args) == 0);
    printf("  --dsp --input <64 samples>: accepted\n");

    /* 63 samples, and an out-of-range sample, are refused. */
    f = fopen(in, "w");
    for (int i = 0; i < 63; i++) fprintf(f, "%d ", i);
    fclose(f);
    assert(run(args) != 0);
    f = fopen(in, "w");
    for (int i = 0; i < 64; i++) fprintf(f, "%d ", i == 10 ? 40000 : i);
    fclose(f);
    assert(run(args) != 0);
    printf("  63 samples / an int16-overflowing sample: refused\n");

    /* Combinations DSP mode refuses, each a usage error (exit 2). */
    const char *bad[] = {"--dsp --optimized", "--dsp some_model.onnx",
                         "--dsp --periodic-count 4 --wait-policy active --timer-prescaler 8"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        snprintf(args, sizeof(args), "%s --cost-table %s --out %s", bad[i], g_cost, out);
        int rc = run(args);
        assert(rc != 0 && WEXITSTATUS(rc) == 2);
        printf("  '%s': usage error\n", bad[i]);
    }
    printf("test_dsp_cli: all tests passed\n");
    return 0;
}
