/* Standalone programs from MIR: the research entry convention (avr_mir.h). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "optifine/codegen/avr_mir.h"
#include "optifine/codegen/instr_buf.h"
#include "optifine/emit.h"

#define PARAM_SYMBOL_FMT "optifine_param_%zu"
#define RETURN_SYMBOL "optifine_return"

static size_t type_bytes(MirType t) {
    return t == MIR_TYPE_PTR ? AVR_MIR_PTR_BYTES : mir_int_bytes(t);
}

void avr_mir_program_free(AvrMirProgram *program) {
    avr_mir_layout_free(&program->layout);
    avr_mir_code_free(&program->code);
    candidate_free(&program->termination);
    candidate_free(&program->constants);
    free(program->param_addr);
    free(program->param_bytes);
    memset(program, 0, sizeof(*program));
}

static void add_cost(AvrMirStaticCost *cost, const Candidate *c) {
    for (size_t i = 0; i < c->num_instructions; i++) {
        const AvrInstr *in = &c->instructions[i];
        if (avr_instr_is_label(in)) continue;
        if (avr_instr_is_data_word(in)) {
            cost->data_bytes += avr_instr_flash_bytes(in);
            continue;
        }
        cost->instructions++;
        cost->code_bytes += avr_instr_flash_bytes(in);
    }
    cost->static_cycles += c->cycles;
    cost->static_energy_nj += c->energy_nj;
}

/* The entry-convention names must not clash with a CONST object's label. */
static int names_reserved(const MirModule *module) {
    for (size_t i = 0; i < module->num_objects; i++) {
        if (module->objects[i].kind == MIR_MEM_CONST && strncmp(module->objects[i].name, "optifine_", 9) == 0) {
            fprintf(stderr, "avr_mir: program: constant '%s' uses the reserved prefix optifine_\n",
                    module->objects[i].name);
            return 0;
        }
    }
    return 1;
}

int avr_mir_build_program(const MirModule *module, uint32_t entry, const CostModel *cost_model,
                          AvrMirProgram *out) {
    memset(out, 0, sizeof(*out));
    /* avr_mir_layout_init verifies the module; nothing is read before it. */
    if (avr_mir_layout_init(module, &out->layout) != 0) return -1;
    if (entry >= module->num_functions) {
        fprintf(stderr, "avr_mir: program: entry function %u does not exist\n", (unsigned)entry);
        avr_mir_program_free(out);
        return -1;
    }
    uint16_t end;
    if (!names_reserved(module) ||
        avr_mir_layout_place_rest(module, &out->layout, AVR_MIR_SRAM_BASE, AVR_MIR_SRAM_LIMIT, &end) != 0 ||
        avr_mir_select_function(module, &out->layout, entry, 1, cost_model, &out->code) != 0 ||
        avr_mir_select_constants(module, cost_model, &out->constants) != 0) {
        avr_mir_program_free(out);
        return -1;
    }
    InstrBuf brk;
    instrbuf_init(&brk);
    ins0(&brk, "break");
    if (instrbuf_price(&brk, cost_model, &out->termination) != 0) {
        avr_mir_program_free(out);
        return -1;
    }

    const MirFunction *fn = &module->functions[entry];
    out->entry = entry;
    out->num_params = fn->num_params;
    out->param_addr = calloc(fn->num_params ? fn->num_params : 1, sizeof(uint16_t));
    out->param_bytes = calloc(fn->num_params ? fn->num_params : 1, sizeof(size_t));
    if (!out->param_addr || !out->param_bytes) {
        fprintf(stderr, "avr_mir: program: out of memory\n");
        avr_mir_program_free(out);
        return -1;
    }
    for (size_t p = 0; p < fn->num_params; p++) {
        out->param_addr[p] = out->layout.value_slot[entry][p];
        out->param_bytes[p] = type_bytes(fn->value_types[p]);
    }
    if (fn->return_type != MIR_TYPE_VOID) {
        out->return_addr = out->layout.return_slot[entry];
        out->return_bytes = type_bytes(fn->return_type);
    }

    for (size_t i = 0; i < out->code.count; i++) add_cost(&out->cost, &out->code.segments[i].code);
    add_cost(&out->cost, &out->termination);
    add_cost(&out->cost, &out->constants);
    out->cost.exact = out->code.straight_line;
    return 0;
}

int avr_mir_emit_program(const AvrMirProgram *program, FILE *out) {
    EmitUnit unit;
    emit_unit_init(&unit);
    int rc = emit_program_prologue(out);
    char name[48]; /* "optifine_param_" and any size_t; emit_unit_define bounds what it records */
    if (rc == 0) {
        fprintf(out, "\n; entry convention (research, not an ABI; avr_mir.h): parameters and the\n"
                     "; return value at fixed SRAM addresses, written and read by whoever runs this\n");
    }
    for (size_t p = 0; rc == 0 && p < program->num_params; p++) {
        snprintf(name, sizeof(name), PARAM_SYMBOL_FMT, p);
        rc = emit_unit_define(&unit, name);
        if (rc == 0) {
            fprintf(out, ".global %s\n.set %s, 0x%04X ; %zu byte(s)\n", name, name, (unsigned)program->param_addr[p],
                    program->param_bytes[p]);
        }
    }
    if (rc == 0 && program->return_bytes > 0) {
        rc = emit_unit_define(&unit, RETURN_SYMBOL);
        if (rc == 0) {
            fprintf(out, ".global %s\n.set %s, 0x%04X ; %zu byte(s)\n", RETURN_SYMBOL, RETURN_SYMBOL,
                    (unsigned)program->return_addr, program->return_bytes);
        }
    }
    if (rc == 0) fprintf(out, "\n");
    for (size_t i = 0; rc == 0 && i < program->code.count; i++) {
        rc = emit_candidate(&unit, &program->code.segments[i].code, out);
    }
    if (rc == 0) {
        fprintf(out, "\n    ; ---- program end: break, then constant data ----\n");
        rc = emit_candidate(&unit, &program->termination, out);
    }
    if (rc == 0 && program->constants.num_instructions > 0) rc = emit_candidate(&unit, &program->constants, out);
    emit_unit_free(&unit);
    return rc;
}
