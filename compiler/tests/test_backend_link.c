/* The generic backend stands on its own. This test is built BACKEND_ONLY --
 * linked against optifine_backend, not optifine_core -- so it fails to link
 * if anything in the backend comes to need the workload graph or its
 * lowering, and fails to compile if a backend header comes to include a
 * workload header. It then compiles a small MIR function the way a frontend
 * would: MIR builder, verification, layout, selection, assembly text. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "optifine/codegen/avr_instr.h"
#include "optifine/codegen/avr_mir.h"
#include "optifine/codegen/cost_category.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/codegen/registers.h"
#include "optifine/cost_model.h"
#include "optifine/emit.h"
#include "optifine/invariant.h"
#include "optifine/mir.h"

#if defined(OPTIFINE_IR_H) || defined(OPTIFINE_IR_VERIFY_H) || defined(OPTIFINE_CODEGEN_CANDIDATES_H) ||   \
    defined(OPTIFINE_CODEGEN_LOWER_H) || defined(OPTIFINE_CODEGEN_SRAM_LAYOUT_H) ||                        \
    defined(OPTIFINE_CODEGEN_REUSE_ANALYSIS_H) || defined(OPTIFINE_CODEGEN_HIR_TO_MIR_H) ||                \
    defined(OPTIFINE_CODEGEN_SELECT_H) || defined(OPTIFINE_CODEGEN_PROGRAM_H) ||                           \
    defined(OPTIFINE_CODEGEN_PERIODIC_H) || defined(OPTIFINE_CODEGEN_DSP32_H) || defined(OPTIFINE_INGEST_H) || \
    defined(OPTIFINE_DSP_BUILD_H)
#error "a backend header includes a workload header"
#endif

int main(int argc, char **argv) {
    assert(argc == 2);
    CostModel cm;
    assert(cost_model_load(argv[1], &cm) == 0);

    /* u8 f(u8 a) { return a < 9 ? a + 1 : a; } */
    MirModule m;
    mir_module_init(&m);
    uint32_t f = mir_add_function(&m, "f", MIR_TYPE_I8);
    uint32_t a = mir_add_param(&m, f, MIR_TYPE_I8), c = mir_new_value(&m, f, MIR_TYPE_I8);
    uint32_t e = mir_add_block(&m, f), t = mir_add_block(&m, f), j = mir_add_block(&m, f);
    mir_emit_cmp(&m, f, e, MIR_CMP_ULT, c, MIR_TYPE_I8, mir_value(a), mir_imm(9), 0);
    mir_cbr(&m, f, e, mir_value(c), t, j);
    mir_emit_binary(&m, f, t, MIR_ADD, a, MIR_TYPE_I8, mir_value(a), mir_imm(1), 0);
    mir_br(&m, f, t, j);
    mir_ret(&m, f, j, mir_value(a));

    char msg[256];
    assert(mir_verify(&m, msg, sizeof(msg)) == 0);
    AvrMirLayout layout;
    uint16_t end;
    assert(avr_mir_layout_init(&m, &layout) == 0);
    assert(avr_mir_layout_place_rest(&m, &layout, AVR_MIR_SRAM_BASE, AVR_MIR_SRAM_LIMIT, &end) == 0);
    AvrMirCode code;
    assert(avr_mir_select_function(&m, &layout, f, 1, &cm, &code) == 0);
    FILE *out = tmpfile();
    assert(out);
    EmitUnit unit;
    emit_unit_init(&unit);
    assert(emit_program_prologue(out) == 0);
    for (size_t i = 0; i < code.count; i++) assert(emit_candidate(&unit, &code.segments[i].code, out) == 0);
    assert(emit_program_epilogue(out) == 0);
    emit_unit_free(&unit);
    rewind(out);
    char text[8192];
    size_t n = fread(text, 1, sizeof(text) - 1, out);
    text[n] = '\0';
    fclose(out);
    assert(strstr(text, "_start:") && strstr(text, ".Lf0b2:") && strstr(text, "    break"));
    avr_mir_code_free(&code);
    avr_mir_layout_free(&layout);
    mir_module_free(&m);
    printf("test_backend_link: MIR -> AVR assembly through optifine_backend alone\n");
    return 0;
}
