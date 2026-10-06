#include "optifine/mir.h"

#include <stdlib.h>
#include <string.h>

size_t mir_int_bytes(MirType type) {
    switch (type) {
        case MIR_TYPE_I8:
            return 1;
        case MIR_TYPE_I16:
            return 2;
        case MIR_TYPE_I32:
            return 4;
        default:
            return 0;
    }
}

void mir_module_init(MirModule *module) {
    memset(module, 0, sizeof(*module));
}

void mir_target_code_free(MirTargetCode *code) {
    if (!code) return;
    if (code->free_payload) code->free_payload(code->payload);
    free(code->reads);
    free(code->writes);
    free(code);
}

void mir_module_free(MirModule *module) {
    for (size_t i = 0; i < module->num_objects; i++) free(module->objects[i].init);
    free(module->objects);
    for (size_t f = 0; f < module->num_functions; f++) {
        MirFunction *fn = &module->functions[f];
        for (size_t b = 0; b < fn->num_blocks; b++) {
            MirBlock *block = &fn->blocks[b];
            for (size_t i = 0; i < block->count; i++) {
                if (block->insts[i].op == MIR_TARGET) mir_target_code_free(block->insts[i].target);
            }
            free(block->insts);
        }
        free(fn->blocks);
        free(fn->value_types);
    }
    free(module->functions);
    memset(module, 0, sizeof(*module));
}

/* Grows *items (count used, *capacity allocated, `size` bytes each) to hold
 * one more. Returns 0, or -1 leaving everything unchanged. */
static int grow(void **items, size_t count, size_t *capacity, size_t size) {
    if (count < *capacity) return 0;
    size_t new_capacity = *capacity ? *capacity * 2 : 8;
    if (new_capacity < *capacity || new_capacity > SIZE_MAX / size) return -1;
    void *grown = realloc(*items, new_capacity * size);
    if (!grown) return -1;
    *items = grown;
    *capacity = new_capacity;
    return 0;
}

static void copy_name(char *dst, size_t cap, const char *src) {
    size_t n = src ? strlen(src) : 0;
    if (n >= cap) n = cap - 1; /* mir_verify rejects a name that does not fit */
    memcpy(dst, src ? src : "", n);
    dst[n] = '\0';
}

uint32_t mir_add_object(MirModule *module, MirMemKind kind, size_t size, const char *name, uint32_t function) {
    if (grow((void **)&module->objects, module->num_objects, &module->objects_capacity, sizeof(MirMemObject)) != 0) {
        module->out_of_memory = 1;
        return MIR_NONE;
    }
    MirMemObject *obj = &module->objects[module->num_objects];
    memset(obj, 0, sizeof(*obj));
    obj->kind = kind;
    obj->size = size;
    obj->function = function;
    copy_name(obj->name, sizeof(obj->name), name);
    if (name && strlen(name) >= sizeof(obj->name)) obj->name[0] = '\0';
    return (uint32_t)module->num_objects++;
}

int mir_object_set_init(MirModule *module, uint32_t object, const uint8_t *bytes, size_t size) {
    MirMemObject *obj = &module->objects[object];
    uint8_t *copy = malloc(size > 0 ? size : 1);
    if (!copy) {
        module->out_of_memory = 1;
        return -1;
    }
    memcpy(copy, bytes, size);
    free(obj->init);
    obj->init = copy;
    return 0;
}

uint32_t mir_add_function(MirModule *module, const char *name, MirType return_type) {
    if (grow((void **)&module->functions, module->num_functions, &module->functions_capacity,
             sizeof(MirFunction)) != 0) {
        module->out_of_memory = 1;
        return MIR_NONE;
    }
    MirFunction *fn = &module->functions[module->num_functions];
    memset(fn, 0, sizeof(*fn));
    copy_name(fn->name, sizeof(fn->name), name);
    fn->return_type = return_type;
    return (uint32_t)module->num_functions++;
}

uint32_t mir_new_value(MirModule *module, uint32_t function, MirType type) {
    MirFunction *fn = &module->functions[function];
    if (grow((void **)&fn->value_types, fn->num_values, &fn->values_capacity, sizeof(MirType)) != 0) {
        module->out_of_memory = 1;
        return MIR_NONE;
    }
    fn->value_types[fn->num_values] = type;
    return (uint32_t)fn->num_values++;
}

uint32_t mir_add_param(MirModule *module, uint32_t function, MirType type) {
    MirFunction *fn = &module->functions[function];
    if (fn->num_values != fn->num_params) {
        /* A parameter after an ordinary value: rejected by mir_verify, since
         * parameters must be values 0 .. num_params-1. */
        fn->num_params = SIZE_MAX;
        return MIR_NONE;
    }
    uint32_t v = mir_new_value(module, function, type);
    if (v != MIR_NONE) fn->num_params++;
    return v;
}

uint32_t mir_add_block(MirModule *module, uint32_t function) {
    MirFunction *fn = &module->functions[function];
    if (grow((void **)&fn->blocks, fn->num_blocks, &fn->blocks_capacity, sizeof(MirBlock)) != 0) {
        module->out_of_memory = 1;
        return MIR_NONE;
    }
    memset(&fn->blocks[fn->num_blocks], 0, sizeof(MirBlock));
    return (uint32_t)fn->num_blocks++;
}

int mir_append(MirModule *module, uint32_t function, uint32_t block, const MirInst *inst) {
    MirBlock *b = &module->functions[function].blocks[block];
    if (grow((void **)&b->insts, b->count, &b->capacity, sizeof(MirInst)) != 0) {
        module->out_of_memory = 1;
        return -1;
    }
    b->insts[b->count++] = *inst;
    return 0;
}

void mir_set_terminator(MirModule *module, uint32_t function, uint32_t block, const MirTerminator *term) {
    module->functions[function].blocks[block].term = *term;
}

MirOperand mir_value(uint32_t value) {
    MirOperand o = {MIR_OPND_VALUE, value, 0};
    return o;
}

MirOperand mir_imm(int64_t imm) {
    MirOperand o = {MIR_OPND_IMM, MIR_NONE, imm};
    return o;
}

MirOperand mir_none(void) {
    MirOperand o = {MIR_OPND_NONE, MIR_NONE, 0};
    return o;
}

MirAddress mir_at_object(uint32_t object, int32_t offset) {
    MirAddress a = {object, MIR_NONE, offset};
    return a;
}

MirAddress mir_at_pointer(uint32_t pointer, int32_t offset) {
    MirAddress a = {MIR_NONE, pointer, offset};
    return a;
}

static MirInst blank(MirOpcode op, MirType type, uint32_t dst, uint32_t origin) {
    MirInst inst;
    memset(&inst, 0, sizeof(inst));
    inst.op = op;
    inst.type = type;
    inst.dst = dst;
    inst.a = mir_none();
    inst.b = mir_none();
    inst.addr = mir_at_object(MIR_NONE, 0);
    inst.origin = origin;
    return inst;
}

int mir_emit_const(MirModule *m, uint32_t fn, uint32_t block, uint32_t dst, MirType type, int64_t imm,
                   uint32_t origin) {
    MirInst inst = blank(MIR_CONST, type, dst, origin);
    inst.a = mir_imm(imm);
    return mir_append(m, fn, block, &inst);
}

int mir_emit_binary(MirModule *m, uint32_t fn, uint32_t block, MirOpcode op, uint32_t dst, MirType type,
                    MirOperand a, MirOperand b, uint32_t origin) {
    MirInst inst = blank(op, type, dst, origin);
    inst.a = a;
    inst.b = b;
    return mir_append(m, fn, block, &inst);
}

int mir_emit_convert(MirModule *m, uint32_t fn, uint32_t block, MirOpcode op, uint32_t dst, MirType type,
                     uint32_t a, uint32_t origin) {
    MirInst inst = blank(op, type, dst, origin);
    inst.a = mir_value(a);
    return mir_append(m, fn, block, &inst);
}

int mir_emit_cmp(MirModule *m, uint32_t fn, uint32_t block, MirCmpPred pred, uint32_t dst, MirType type,
                 MirOperand a, MirOperand b, uint32_t origin) {
    MirInst inst = blank(MIR_CMP, type, dst, origin);
    inst.pred = pred;
    inst.a = a;
    inst.b = b;
    return mir_append(m, fn, block, &inst);
}

int mir_emit_load(MirModule *m, uint32_t fn, uint32_t block, uint32_t dst, MirType type, MirAddress addr,
                  uint32_t origin) {
    MirInst inst = blank(MIR_LOAD, type, dst, origin);
    inst.addr = addr;
    return mir_append(m, fn, block, &inst);
}

int mir_emit_store(MirModule *m, uint32_t fn, uint32_t block, MirType type, MirAddress addr, MirOperand value,
                   uint32_t origin) {
    MirInst inst = blank(MIR_STORE, type, MIR_NONE, origin);
    inst.addr = addr;
    inst.a = value;
    return mir_append(m, fn, block, &inst);
}

int mir_emit_addr(MirModule *m, uint32_t fn, uint32_t block, uint32_t dst, uint32_t object, int32_t offset,
                  uint32_t origin) {
    MirInst inst = blank(MIR_ADDR, MIR_TYPE_PTR, dst, origin);
    inst.addr = mir_at_object(object, offset);
    return mir_append(m, fn, block, &inst);
}

int mir_emit_target(MirModule *m, uint32_t fn, uint32_t block, MirTargetCode *code, uint32_t origin) {
    MirInst inst = blank(MIR_TARGET, MIR_TYPE_VOID, MIR_NONE, origin);
    inst.target = code;
    if (mir_append(m, fn, block, &inst) != 0) {
        mir_target_code_free(code);
        return -1;
    }
    return 0;
}

void mir_br(MirModule *m, uint32_t fn, uint32_t block, uint32_t target) {
    MirTerminator t = {MIR_TERM_BR, mir_none(), target, MIR_NONE, mir_none()};
    mir_set_terminator(m, fn, block, &t);
}

void mir_cbr(MirModule *m, uint32_t fn, uint32_t block, MirOperand cond, uint32_t then_block,
             uint32_t else_block) {
    MirTerminator t = {MIR_TERM_CBR, cond, then_block, else_block, mir_none()};
    mir_set_terminator(m, fn, block, &t);
}

void mir_ret(MirModule *m, uint32_t fn, uint32_t block, MirOperand value) {
    MirTerminator t = {MIR_TERM_RET, mir_none(), MIR_NONE, MIR_NONE, value};
    mir_set_terminator(m, fn, block, &t);
}
