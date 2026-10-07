/* A C-shaped function compiled through the generic backend alone: built with
 * the MIR API (no workload graph, no MIR_TARGET region), verified, lowered by
 * avr_mir_build_program, and run in the AVR interpreter through the entry
 * convention -- parameters written at, and the result read from, the
 * addresses the program reports, never at layout addresses worked out here.
 * Built BACKEND_ONLY, so it also links without the workload lowering.
 *
 * Two references, kept apart on purpose:
 *   - MIR semantics: 16-bit two's-complement wrapping arithmetic, which is
 *     what MIR_ADD/SUB define. Checked for every input, overflowing or not.
 *   - Host C with int arithmetic: checked only for inputs whose computation
 *     never leaves the int16 range, since signed overflow is undefined in C
 *     and a wrapped result is no claim about C at all.
 * Both encodings (values; stack objects with loads and stores) must agree
 * with both references and with the MIR interpreter, on both arms of the
 * if, through the loop's five iterations and its merges. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avr_interp.h"
#include "mir_interp.h"
#include "mir_programs.h"
#include "optifine/codegen/avr_mir.h"

static CostModel g_cm;

/* MIR semantics, fixed-width and fully defined: every step wraps mod 2^16. */
static uint16_t reference_mir(uint16_t x) {
    uint16_t y = (uint16_t)(x + 3u);
    if ((int16_t)y > 10) y = (uint16_t)(y - 2u); /* 10 <s y */
    else y = (uint16_t)(y + 4u);
    uint16_t sum = 0;
    for (int i = 0; i < 5; i++) sum = (uint16_t)(sum + y);
    return sum;
}

/* The C source, in host int, for inputs where it is defined as int16 code:
 * returns 0 (and leaves *out alone) when any intermediate would overflow
 * int16, so the case is not a C claim. */
static int reference_c(int x, int *out) {
    if (x < -32768 || x > 32767) return 0;
    int y = x + 3;
    if (y > 32767) return 0;
    if (y > 10) y = y - 2;
    else y = y + 4;
    if (y > 32767 || y < -32768) return 0;
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        sum += y;
        if (sum > 32767 || sum < -32768) return 0;
    }
    *out = sum;
    return 1;
}

/* The program, run once with parameter `x` through the entry convention. */
static uint16_t run(const AvrMirProgram *p, uint16_t x, unsigned long *cycles) {
    size_t total = 1;
    for (size_t i = 0; i < p->code.count; i++) total += p->code.segments[i].code.num_instructions;
    Candidate c = {0};
    c.instructions = calloc(total, sizeof(AvrInstr));
    assert(c.instructions);
    for (size_t i = 0; i < p->code.count; i++) {
        const Candidate *s = &p->code.segments[i].code;
        memcpy(c.instructions + c.num_instructions, s->instructions, s->num_instructions * sizeof(AvrInstr));
        c.num_instructions += s->num_instructions;
    }
    c.instructions[c.num_instructions++] = p->termination.instructions[0];
    AvrInterp *avr = malloc(sizeof(AvrInterp));
    assert(avr);
    avr_interp_init(avr);
    /* garbage everywhere first: the program must not rely on cleared SRAM */
    for (size_t a = 0; a < AVR_INTERP_MEM_SIZE; a++) avr->mem[a] = (uint8_t)(a * 37u + 11u);
    assert(p->num_params == 1 && p->param_bytes[0] == 2 && p->return_bytes == 2);
    avr->mem[p->param_addr[0]] = (uint8_t)x;
    avr->mem[p->param_addr[0] + 1] = (uint8_t)(x >> 8);
    assert(avr_interp_run(avr, &c) == 0);
    uint16_t r = (uint16_t)(avr->mem[p->return_addr] | (avr->mem[p->return_addr + 1] << 8));
    *cycles = avr->cycles;
    free(avr);
    candidate_free(&c);
    return r;
}

static void check_encoding(uint32_t (*build)(MirModule *), const char *what) {
    MirModule m;
    mir_module_init(&m);
    uint32_t f = build(&m);
    char msg[256];
    assert(mir_verify(&m, msg, sizeof(msg)) == 0);
    assert(m.functions[f].num_blocks == 7);
    for (size_t b = 0; b < m.functions[f].num_blocks; b++) {
        for (size_t i = 0; i < m.functions[f].blocks[b].count; i++) {
            assert(m.functions[f].blocks[b].insts[i].op != MIR_TARGET);
        }
    }

    AvrMirProgram p;
    assert(avr_mir_build_program(&m, f, &g_cm, &p) == 0);
    /* Branching code: the backend's figures are static, and say so. */
    assert(!p.cost.exact && p.cost.instructions > 0 && p.cost.code_bytes > 0);

    size_t runs = 0, c_checked = 0, then_arm = 0, else_arm = 0;
    unsigned long min_cycles = ~0UL, max_cycles = 0;
    for (long xi = -32768; xi <= 32767; xi += (xi > -40 && xi < 40) ? 1 : (xi > -7000 && xi < 7000) ? 97 : 641) {
        uint16_t x = (uint16_t)xi;
        unsigned long cycles;
        uint16_t got = run(&p, x, &cycles);
        uint16_t want = reference_mir(x);
        int64_t args[1] = {(int16_t)x}, interp = 0;
        MirInterp ref;
        assert(mir_interp_init(&ref, &m) == 0);
        assert(mir_interp_call(&ref, f, args, &interp) == 0);
        mir_interp_free(&ref);
        if (got != want || (uint16_t)interp != want) {
            fprintf(stderr, "%s(%ld): avr %u, mir interpreter %u, MIR semantics %u\n", what, xi, got,
                    (unsigned)(uint16_t)interp, want);
            abort();
        }
        int c_result;
        if (reference_c((int)xi, &c_result)) {
            assert((int16_t)got == c_result);
            c_checked++;
        }
        if ((int16_t)(uint16_t)(x + 3u) > 10) then_arm++;
        else else_arm++;
        if (cycles < min_cycles) min_cycles = cycles;
        if (cycles > max_cycles) max_cycles = cycles;
        runs++;
    }
    assert(then_arm > 0 && else_arm > 0 && c_checked > 0 && c_checked < runs);
    /* The static figure prices each instruction once; the loop runs its body
     * five times, so execution costs more than the static sum. */
    assert(p.cost.static_cycles < min_cycles);
    printf("  %s: %zu inputs (%zu then-arm, %zu else-arm) == MIR semantics and the MIR interpreter; "
           "%zu also == host C (no int16 overflow)\n",
           what, runs, then_arm, else_arm, c_checked);
    printf("    %zu instructions, %zu code bytes; static %llu cycles vs executed %lu..%lu (not exact: branches)\n",
           p.cost.instructions, p.cost.code_bytes, (unsigned long long)p.cost.static_cycles, min_cycles, max_cycles);
    avr_mir_program_free(&p);
    mir_module_free(&m);
}

/* A straight-line function: here the static figures are the execution cost,
 * and the program says so. */
static void check_exact_cost(void) {
    MirModule m;
    mir_module_init(&m);
    uint32_t f = mirp_arith32(&m);
    AvrMirProgram p;
    assert(avr_mir_build_program(&m, f, &g_cm, &p) == 0);
    assert(p.cost.exact && p.num_params == 2 && p.param_bytes[0] == 4 && p.return_bytes == 4);
    size_t total = 1;
    for (size_t i = 0; i < p.code.count; i++) total += p.code.segments[i].code.num_instructions;
    Candidate c = {0};
    c.instructions = calloc(total, sizeof(AvrInstr));
    assert(c.instructions);
    for (size_t i = 0; i < p.code.count; i++) {
        const Candidate *s = &p.code.segments[i].code;
        memcpy(c.instructions + c.num_instructions, s->instructions, s->num_instructions * sizeof(AvrInstr));
        c.num_instructions += s->num_instructions;
    }
    c.instructions[c.num_instructions++] = p.termination.instructions[0];
    AvrInterp *avr = malloc(sizeof(AvrInterp));
    assert(avr);
    avr_interp_init(avr);
    assert(avr_interp_run(avr, &c) == 0);
    assert(avr->cycles == p.cost.static_cycles);
    printf("  straight-line arith32: exact, %llu cycles predicted == executed\n",
           (unsigned long long)p.cost.static_cycles);
    free(avr);
    candidate_free(&c);
    avr_mir_program_free(&p);
    mir_module_free(&m);
}

/* The emitted unit carries the convention's symbols at the reported
 * addresses, and halts with break before the constant data. */
static void check_emitted_symbols(void) {
    MirModule m;
    mir_module_init(&m);
    uint32_t f = mirp_imperative_values(&m);
    AvrMirProgram p;
    assert(avr_mir_build_program(&m, f, &g_cm, &p) == 0);
    FILE *out = tmpfile();
    assert(out);
    assert(avr_mir_emit_program(&p, out) == 0);
    long n = ftell(out);
    rewind(out);
    char *text = malloc((size_t)n + 1);
    assert(text && fread(text, 1, (size_t)n, out) == (size_t)n);
    text[n] = '\0';
    fclose(out);
    char want[96];
    snprintf(want, sizeof(want), ".set optifine_param_0, 0x%04X", (unsigned)p.param_addr[0]);
    assert(strstr(text, want));
    snprintf(want, sizeof(want), ".set optifine_return, 0x%04X", (unsigned)p.return_addr);
    assert(strstr(text, want));
    assert(strstr(text, "_start:") && strstr(text, "    break"));
    free(text);
    avr_mir_program_free(&p);
    mir_module_free(&m);

    /* A constant may not take a convention name. */
    mir_module_init(&m);
    uint32_t k = mir_add_object(&m, MIR_MEM_CONST, 2, "optifine_return", MIR_NONE);
    const uint8_t two[2] = {1, 2};
    mir_object_set_init(&m, k, two, 2);
    f = mirp_mul8(&m);
    assert(avr_mir_build_program(&m, f, &g_cm, &p) != 0);
    mir_module_free(&m);
    printf("  emitted: optifine_param_0 / optifine_return at the reported addresses; reserved names refused\n");
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert(cost_model_load(argv[1], &g_cm) == 0);
    check_encoding(mirp_imperative_values, "values");
    check_encoding(mirp_imperative_memory, "stack objects");
    check_exact_cost();
    check_emitted_symbols();

    /* Malformed MIR never reaches the program builder's output. */
    MirModule m;
    mir_module_init(&m);
    uint32_t f = mirp_imperative_values(&m);
    m.functions[f].blocks[1].term.cond = mir_value(1000000);
    AvrMirProgram p;
    assert(avr_mir_build_program(&m, f, &g_cm, &p) != 0 && p.code.count == 0);
    mir_module_free(&m);
    printf("  malformed MIR refused by avr_mir_build_program\n");

    printf("test_mir_standalone: all tests passed\n");
    return 0;
}
