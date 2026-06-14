#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/periodic.h"

/* Windows' CRT assert() opens a modal abort dialog when a RED test fails.
 * Keep every check in this focused binary console-only and deterministic. */
#define TEST_CHECK(condition)                                                        \
    do {                                                                             \
        if (!(condition)) {                                                          \
            fprintf(stderr, "test_periodic check failed: %s (%s:%d)\n",           \
                    #condition, __FILE__, __LINE__);                                 \
            exit(EXIT_FAILURE);                                                      \
        }                                                                            \
    } while (0)

/* test_periodic links periodic.c directly. These deterministic lower-layer
 * emitters keep the test focused on the real periodic wrapper while making
 * the generated BODY a small, valid AVR fixture whose expected bytes do not
 * depend on the classifier lowering implementation. */
int codegen_emit_initialization(const IrGraph *graph, const SramLayout *layout,
                                const RegAllocResult *regalloc, const CostModel *cost_model,
                                const int8_t *demo_input, size_t demo_input_len,
                                int use_real_candidates,
                                FILE *out, ProgramRegionCost *out_cost) {
    (void)graph;
    (void)layout;
    (void)regalloc;
    (void)cost_model;
    (void)demo_input;
    (void)demo_input_len;
    (void)use_real_candidates;
    out_cost->energy_nj = 1.25;
    out_cost->cycles = 2;
    return fprintf(out, "    ldi r20, 0\n    sts 0x0240, r20\n") < 0 ? -1 : 0;
}

int codegen_emit_inference_body(const IrGraph *graph, const SramLayout *layout,
                                const RegAllocResult *regalloc, const CostModel *cost_model,
                                const int8_t *demo_input, size_t demo_input_len,
                                int use_real_candidates,
                                FILE *out, ProgramRegionCost *out_cost) {
    (void)graph;
    (void)layout;
    (void)regalloc;
    (void)cost_model;
    (void)demo_input;
    (void)demo_input_len;
    (void)use_real_candidates;
    out_cost->energy_nj = 2.5;
    out_cost->cycles = 8;
    return fprintf(out,
                   "    ldi r20, 42\n"
                   "    sts 0x0240, r20\n"
                   "    ldi r20, 43\n"
                   "    sts 0x0241, r20\n"
                   "    ldi r20, 44\n"
                   "    sts 0x0242, r20\n"
                   "    ldi r20, 45\n"
                   "    sts 0x0243, r20\n") < 0 ? -1 : 0;
}

typedef struct {
    IrOp output_op;
    IrGraph graph;
    size_t output_shape;
    uint16_t output_addr;
    SramLayout layout;
    RegAllocResult regalloc;
    CostModel cost_model;
    PeriodicOptions options;
} PeriodicFixture;

static void periodic_fixture_init(PeriodicFixture *fixture, WaitPolicy policy) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->output_shape = 4;
    fixture->output_op.id = 0;
    fixture->output_op.kind = OP_OUTPUT;
    fixture->output_op.output_shape = &fixture->output_shape;
    fixture->output_op.output_shape_len = 1;
    fixture->output_op.dtype = DT_INT8;
    fixture->graph.ops = &fixture->output_op;
    fixture->graph.count = 1;
    fixture->graph.capacity = 1;
    fixture->output_addr = 0x0240;
    fixture->layout.op_addr = &fixture->output_addr;
    fixture->layout.count = 1;
    fixture->layout.bytes_used = 0x0050;
    fixture->options.policy = policy;
    fixture->options.timer_prescaler = 32;
    fixture->options.inference_count = 3;
    fixture->options.use_real_candidates = 1;
}

static char *read_stream(FILE *stream) {
    TEST_CHECK(fflush(stream) == 0);
    TEST_CHECK(fseek(stream, 0, SEEK_END) == 0);
    long length = ftell(stream);
    TEST_CHECK(length >= 0);
    TEST_CHECK(fseek(stream, 0, SEEK_SET) == 0);

    char *text = malloc((size_t)length + 1);
    TEST_CHECK(text != NULL);
    TEST_CHECK(fread(text, 1, (size_t)length, stream) == (size_t)length);
    text[length] = '\0';
    return text;
}

static char *emit_periodic(WaitPolicy policy, PeriodicProgramCost *cost) {
    PeriodicFixture fixture;
    periodic_fixture_init(&fixture, policy);
    FILE *out = tmpfile();
    TEST_CHECK(out != NULL);
    TEST_CHECK(codegen_emit_periodic_program(&fixture.graph, &fixture.layout,
                                         &fixture.regalloc, &fixture.cost_model,
                                         NULL, 0, &fixture.options, out, cost) == 0);
    char *text = read_stream(out);
    fclose(out);
    return text;
}

static void write_text_file(const char *path, const char *text) {
    FILE *out = fopen(path, "wb");
    TEST_CHECK(out != NULL);
    size_t length = strlen(text);
    TEST_CHECK(fwrite(text, 1, length, out) == length);
    TEST_CHECK(fclose(out) == 0);
}

static char *read_text_file(const char *path) {
    FILE *in = fopen(path, "rb");
    TEST_CHECK(in != NULL);
    TEST_CHECK(fseek(in, 0, SEEK_END) == 0);
    long length = ftell(in);
    TEST_CHECK(length >= 0);
    TEST_CHECK(fseek(in, 0, SEEK_SET) == 0);

    char *text = malloc((size_t)length + 1);
    TEST_CHECK(text != NULL);
    TEST_CHECK(fread(text, 1, (size_t)length, in) == (size_t)length);
    TEST_CHECK(fclose(in) == 0);
    text[length] = '\0';
    return text;
}

static void normalize_newlines(char *text) {
    char *read = text;
    char *write = text;
    while (*read != '\0') {
        if (read[0] == '\r' && read[1] == '\n') {
            read++;
        }
        *write++ = *read++;
    }
    *write = '\0';
}

static int run_optifine_cli(const char *compiler_path, const char *model_path,
                            const char *cost_table_path, const char *input_path,
                            const char *arguments, const char *stderr_path) {
    char command[2048];
#ifdef _WIN32
    int written = snprintf(command, sizeof(command),
                           "\"\"%s\" \"%s\" --cost-table \"%s\" --input \"%s\" %s "
                           "> \"%s\" 2>&1\"",
                           compiler_path, model_path, cost_table_path, input_path,
                           arguments, stderr_path);
#else
    int written = snprintf(command, sizeof(command),
                           "\"%s\" \"%s\" --cost-table \"%s\" --input \"%s\" %s "
                           "> \"%s\" 2>&1",
                           compiler_path, model_path, cost_table_path, input_path,
                           arguments, stderr_path);
#endif
    TEST_CHECK(written >= 0 && (size_t)written < sizeof(command));
    return system(command);
}

/* These commands exercise the compiler binary, rather than the periodic
 * emitter directly. Removing periodic CLI parsing, accepting an incomplete
 * scheduling request, or losing the metadata makes this test fail. */
static void test_cli_periodic_mode(const char *compiler_path, const char *model_path,
                                   const char *cost_table_path, const char *input_path) {
    static const char *const expected_metadata[] = {
        "periodic policy: active\n"
        "periodic timer prescaler: 32 (Timer0 divisor)\n"
        "periodic inference count: 3\n"
        "periodic initialization predicted cost: 673 cycles, 1909.637 nJ\n"
        "periodic per-inference predicted compute cost: 5344 cycles, 15163.600 nJ\n",
        "periodic policy: powersave\n"
        "periodic timer prescaler: 32 (Timer0 divisor)\n"
        "periodic inference count: 3\n"
        "periodic initialization predicted cost: 673 cycles, 1909.637 nJ\n"
        "periodic per-inference predicted compute cost: 5344 cycles, 15163.600 nJ\n",
    };
    static const char *const policies[] = {"active", "powersave"};

    remove("cli_standalone.S");
    remove("cli_standalone.stderr");
    TEST_CHECK(run_optifine_cli(compiler_path, model_path, cost_table_path, input_path,
                                "--optimized --out cli_standalone.S",
                                "cli_standalone.stderr") == 0);
    char *standalone = read_text_file("cli_standalone.S");
    TEST_CHECK(strstr(standalone, "wait_for_tick:") == NULL);
    free(standalone);

    for (size_t i = 0; i < sizeof(policies) / sizeof(policies[0]); i++) {
        char arguments[256];
        char assembly_path[64];
        char stderr_path[64];
        int written = snprintf(arguments, sizeof(arguments),
                               "--optimized --periodic-count 3 --wait-policy %s "
                               "--timer-prescaler 32 --out cli_%s.S",
                               policies[i], policies[i]);
        TEST_CHECK(written >= 0 && (size_t)written < sizeof(arguments));
        written = snprintf(assembly_path, sizeof(assembly_path), "cli_%s.S", policies[i]);
        TEST_CHECK(written >= 0 && (size_t)written < sizeof(assembly_path));
        written = snprintf(stderr_path, sizeof(stderr_path), "cli_%s.stderr", policies[i]);
        TEST_CHECK(written >= 0 && (size_t)written < sizeof(stderr_path));
        remove(assembly_path);
        remove(stderr_path);

        TEST_CHECK(run_optifine_cli(compiler_path, model_path, cost_table_path, input_path,
                                    arguments, stderr_path) == 0);
        char *assembly = read_text_file(assembly_path);
        char *stderr_text = read_text_file(stderr_path);
        normalize_newlines(stderr_text);
        TEST_CHECK(strstr(assembly, "wait_for_tick:") != NULL);
        TEST_CHECK(strcmp(stderr_text, expected_metadata[i]) == 0);
        free(assembly);
        free(stderr_text);
    }

    static const char *const invalid_requests[] = {
        "--periodic-count 3 --wait-policy active --out cli_invalid.S",
        "--periodic-count 3 --timer-prescaler 32 --out cli_invalid.S",
        "--wait-policy active --timer-prescaler 32 --out cli_invalid.S",
        "--periodic-count 3 --wait-policy idle --timer-prescaler 32 --out cli_invalid.S",
        "--periodic-count 3 --wait-policy active --timer-prescaler 7 --out cli_invalid.S",
        "--periodic-count 0 --wait-policy active --timer-prescaler 32 --out cli_invalid.S",
        "--periodic-count 3 --periodic-count 4 --wait-policy active --timer-prescaler 32 --out cli_invalid.S",
        "--periodic-count 3 --wait-policy active --wait-policy powersave --timer-prescaler 32 --out cli_invalid.S",
        "--periodic-count 3 --wait-policy active --timer-prescaler 32 --timer-prescaler 128 --out cli_invalid.S",
        "--periodic-count 3x --wait-policy active --timer-prescaler 32 --out cli_invalid.S",
        "--periodic-count \"+3\" --wait-policy active --timer-prescaler 32 --out cli_invalid.S",
        "--periodic-count 3 --wait-policy active --timer-prescaler \" 32\" --out cli_invalid.S",
    };
    for (size_t i = 0; i < sizeof(invalid_requests) / sizeof(invalid_requests[0]); i++) {
        remove("cli_invalid.S");
        remove("cli_invalid.stderr");
        TEST_CHECK(run_optifine_cli(compiler_path, model_path, cost_table_path, input_path,
                                    invalid_requests[i], "cli_invalid.stderr") != 0);
    }

    remove("cli_standalone.S");
    remove("cli_standalone.stderr");
    remove("cli_active.S");
    remove("cli_active.stderr");
    remove("cli_powersave.S");
    remove("cli_powersave.stderr");
    remove("cli_invalid.S");
    remove("cli_invalid.stderr");
}

static size_t occurrence_count(const char *text, const char *needle) {
    size_t count = 0;
    size_t needle_len = strlen(needle);
    for (const char *at = text; (at = strstr(at, needle)) != NULL; at += needle_len) {
        count++;
    }
    return count;
}

static void assert_contains_in_order(const char *text, const char *const *needles,
                                     size_t needle_count) {
    const char *cursor = text;
    for (size_t i = 0; i < needle_count; i++) {
        cursor = strstr(cursor, needles[i]);
        TEST_CHECK(cursor != NULL);
        cursor += strlen(needles[i]);
    }
}

static void assert_body_regions_identical(const char *active, const char *power_save) {
    static const char begin[] = "; BODY BEGIN\n";
    static const char end[] = "; BODY END";
    const char *active_begin = strstr(active, begin);
    const char *sleep_begin = strstr(power_save, begin);
    TEST_CHECK(active_begin != NULL && sleep_begin != NULL);
    active_begin += strlen(begin);
    sleep_begin += strlen(begin);
    const char *active_end = strstr(active_begin, end);
    const char *sleep_end = strstr(sleep_begin, end);
    TEST_CHECK(active_end != NULL && sleep_end != NULL);
    size_t active_len = (size_t)(active_end - active_begin);
    size_t sleep_len = (size_t)(sleep_end - sleep_begin);
    TEST_CHECK(active_len == sleep_len);
    TEST_CHECK(memcmp(active_begin, sleep_begin, active_len) == 0);
}

/* Missing or policy-divergent wrapper assembly must fail these checks. The
 * literals are hand-derived from the ATmega128 Timer0 scheduling contract. */
static void test_periodic_program_structure_and_matched_body(void) {
    PeriodicProgramCost active_cost = {0};
    PeriodicProgramCost sleep_cost = {0};
    char *active = emit_periodic(WAIT_ACTIVE, &active_cost);
    char *power_save = emit_periodic(WAIT_POWER_SAVE, &sleep_cost);

    const char *outputs[] = {active, power_save};
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); i++) {
        const char *assembly = outputs[i];
        TEST_CHECK(strstr(assembly, "#include <avr/io.h>") != NULL);
        TEST_CHECK(strstr(assembly, ".org 0x0000\n    jmp reset") != NULL);
        TEST_CHECK(strstr(assembly, ".org (TIMER0_OVF_vect_num * 4)\n    jmp timer0_ovf_isr") != NULL);
        TEST_CHECK(strstr(assembly, "ldi r16, lo8(RAMEND)") != NULL);
        TEST_CHECK(strstr(assembly, "ldi r16, hi8(RAMEND)") != NULL);
        TEST_CHECK(strstr(assembly, "ldi r16, (1 << AS0)") != NULL);
        TEST_CHECK(strstr(assembly, "ldi r16, 0x03") != NULL);
        TEST_CHECK(strstr(assembly, "wait_timer0_busy:") != NULL);
        TEST_CHECK(strstr(assembly, "(1 << TCR0UB) | (1 << OCR0UB) | (1 << TCN0UB)") != NULL);
        TEST_CHECK(strstr(assembly, "ldi r16, (1 << TOIE0)") != NULL);
        TEST_CHECK(strstr(assembly, "cpi r16, 3") != NULL);
        TEST_CHECK(strstr(assembly, "; META completed_count_addr=0x0253") != NULL);
        TEST_CHECK(strstr(assembly, "; META overrun_addr=0x0252") != NULL);
        TEST_CHECK(strstr(assembly, "; META output_addr=0x0240 output_len=4") != NULL);
        TEST_CHECK(occurrence_count(assembly, "    break") == 1);

        static const char *const isr_sequence[] = {
            "timer0_ovf_isr:\n",
            "    push r16\n",
            "    in r16, _SFR_IO_ADDR(SREG)\n",
            "    push r16\n",
            "    lds r16, 0x0251\n",
            "    tst r16\n",
            "    breq timer0_no_overrun\n",
            "    ldi r16, 1\n",
            "    sts 0x0252, r16\n",
            "timer0_no_overrun:\n",
            "    ldi r16, 1\n",
            "    sts 0x0250, r16\n",
            "    pop r16\n",
            "    out _SFR_IO_ADDR(SREG), r16\n",
            "    pop r16\n",
            "    reti\n",
        };
        assert_contains_in_order(assembly, isr_sequence,
                                 sizeof(isr_sequence) / sizeof(isr_sequence[0]));

        static const char *const terminal_sequence[] = {
            "periodic_done:\n",
            "    cli\n",
            "    lds r16, 0x0253\n",
            "    sts 0x0253, r16\n",
            "    lds r16, 0x0252\n",
            "    sts 0x0252, r16\n",
            "    lds r16, 0x0240\n",
            "    sts 0x0240, r16\n",
            "    lds r16, 0x0241\n",
            "    sts 0x0241, r16\n",
            "    lds r16, 0x0242\n",
            "    sts 0x0242, r16\n",
            "    lds r16, 0x0243\n",
            "    sts 0x0243, r16\n",
            "    break\n",
        };
        assert_contains_in_order(assembly, terminal_sequence,
                                 sizeof(terminal_sequence) / sizeof(terminal_sequence[0]));

        static const char *const body_lifecycle[] = {
            "tick_ready:\n",
            "    clr r16\n",
            "    sts 0x0250, r16\n",
            "    ldi r16, 1\n",
            "    sts 0x0251, r16\n",
            "    sei\n",
            "; BODY BEGIN\n",
            "; BODY END\n",
            "    cli\n",
            "    clr r16\n",
            "    sts 0x0251, r16\n",
            "    lds r16, 0x0253\n",
            "    inc r16\n",
            "    sts 0x0253, r16\n",
            "    lds r16, 0x0252\n",
            "    tst r16\n",
            "    brne periodic_done\n",
            "    lds r16, 0x0253\n",
            "    cpi r16, 3\n",
            "    breq periodic_done\n",
            "    sei\n",
            "    jmp wait_for_tick\n",
        };
        assert_contains_in_order(assembly, body_lifecycle,
                                 sizeof(body_lifecycle) / sizeof(body_lifecycle[0]));
    }

    TEST_CHECK(strstr(active, "\n    sleep\n") == NULL);
    static const char *const active_wait[] = {
        "wait_for_tick:\n",
        "    cli\n",
        "    lds r16, 0x0250\n",
        "    tst r16\n",
        "    brne tick_ready\n",
        "    sei\n",
        "    rjmp wait_for_tick\n",
    };
    assert_contains_in_order(active, active_wait, sizeof(active_wait) / sizeof(active_wait[0]));

    TEST_CHECK(occurrence_count(power_save, "\n    sleep\n") == 1);
    static const char *const atomic_power_save_wait[] = {
        "wait_for_tick:\n",
        "    cli\n",
        "    lds r16, 0x0250\n",
        "    tst r16\n",
        "    brne tick_ready\n",
        "    ldi r16, (1 << SE) | (1 << SM1) | (1 << SM0)\n",
        "    out _SFR_IO_ADDR(MCUCR), r16\n",
        "    sei\n",
        "    sleep\n",
        "    rjmp wait_for_tick\n",
    };
    assert_contains_in_order(power_save, atomic_power_save_wait,
                             sizeof(atomic_power_save_wait) / sizeof(atomic_power_save_wait[0]));

    assert_body_regions_identical(active, power_save);
    TEST_CHECK(active_cost.tick_addr == 0x0250 && sleep_cost.tick_addr == 0x0250);
    TEST_CHECK(active_cost.running_addr == 0x0251 && sleep_cost.running_addr == 0x0251);
    TEST_CHECK(active_cost.overrun_addr == 0x0252 && sleep_cost.overrun_addr == 0x0252);
    TEST_CHECK(active_cost.completed_addr == 0x0253 && sleep_cost.completed_addr == 0x0253);
    TEST_CHECK(active_cost.initialization.cycles == sleep_cost.initialization.cycles);
    TEST_CHECK(active_cost.inference.cycles == sleep_cost.inference.cycles);

    free(active);
    free(power_save);
}

/* A regression in the timer divisor table or count range must make this
 * fail. The expected validity is hand-listed from the scheduling contract. */
static void test_periodic_options_accept_only_supported_ranges(void) {
    static const uint16_t valid_prescalers[] = {8, 32, 128, 1024};
    for (size_t i = 0; i < sizeof(valid_prescalers) / sizeof(valid_prescalers[0]); i++) {
        PeriodicOptions options = {
            .policy = WAIT_ACTIVE,
            .timer_prescaler = valid_prescalers[i],
            .inference_count = 1,
            .use_real_candidates = 0,
        };
        TEST_CHECK(periodic_options_validate(&options) == 0);
        options.inference_count = 255;
        TEST_CHECK(periodic_options_validate(&options) == 0);
    }

    static const uint16_t invalid_prescalers[] = {0, 1, 7, 9, 16, 64, 256, 512, 2048, UINT16_MAX};
    for (size_t i = 0; i < sizeof(invalid_prescalers) / sizeof(invalid_prescalers[0]); i++) {
        PeriodicOptions options = {
            .policy = WAIT_POWER_SAVE,
            .timer_prescaler = invalid_prescalers[i],
            .inference_count = 1,
            .use_real_candidates = 1,
        };
        TEST_CHECK(periodic_options_validate(&options) != 0);
    }

    PeriodicOptions zero_count = {
        .policy = WAIT_ACTIVE,
        .timer_prescaler = 8,
        .inference_count = 0,
        .use_real_candidates = 0,
    };
    TEST_CHECK(periodic_options_validate(&zero_count) != 0);
}

/* This catches an off-by-one in the four-byte scheduler allocation. */
static void test_periodic_scheduler_bytes_fit_only_through_0x10ff(void) {
    PeriodicOptions options = {
        .policy = WAIT_ACTIVE,
        .timer_prescaler = 8,
        .inference_count = 1,
        .use_real_candidates = 0,
    };
    SramLayout exact_fit = {.bytes_used = 0x0efc};
    PeriodicProgramCost cost = {0};
    TEST_CHECK(codegen_emit_periodic_program(NULL, &exact_fit, NULL, NULL,
                                         NULL, 0, &options, NULL, &cost) == 0);
    TEST_CHECK(cost.tick_addr == 0x10fc);
    TEST_CHECK(cost.running_addr == 0x10fd);
    TEST_CHECK(cost.overrun_addr == 0x10fe);
    TEST_CHECK(cost.completed_addr == 0x10ff);

    SramLayout too_large = {.bytes_used = 0x0efd};
    TEST_CHECK(codegen_emit_periodic_program(NULL, &too_large, NULL, NULL,
                                         NULL, 0, &options, NULL, &cost) != 0);
}

int main(int argc, char **argv) {
    if (argc != 1 && argc != 3 && argc != 5) {
        fprintf(stderr,
                "usage: %s [active.S powersave.S | optifine model.onnx cost.toml input.txt]\n",
                argv[0]);
        return EXIT_FAILURE;
    }

    test_periodic_options_accept_only_supported_ranges();
    test_periodic_scheduler_bytes_fit_only_through_0x10ff();
    test_periodic_program_structure_and_matched_body();
    if (argc == 5) {
        test_cli_periodic_mode(argv[1], argv[2], argv[3], argv[4]);
    }

    if (argc == 3) {
        PeriodicProgramCost active_cost = {0};
        PeriodicProgramCost sleep_cost = {0};
        char *active = emit_periodic(WAIT_ACTIVE, &active_cost);
        char *power_save = emit_periodic(WAIT_POWER_SAVE, &sleep_cost);
        write_text_file(argv[1], active);
        write_text_file(argv[2], power_save);
        free(active);
        free(power_save);
    }

    puts("test_periodic: all tests passed");
    return 0;
}
