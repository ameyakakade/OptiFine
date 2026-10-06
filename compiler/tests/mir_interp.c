#include "mir_interp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int mir_interp_init(MirInterp *in, const MirModule *m) {
    in->module = m;
    in->budget = 10000000UL;
    in->memory = calloc(m->num_objects ? m->num_objects : 1, sizeof(uint8_t *));
    if (!in->memory) return -1;
    for (size_t i = 0; i < m->num_objects; i++) {
        in->memory[i] = calloc(m->objects[i].size, 1);
        if (!in->memory[i]) return -1;
        if (m->objects[i].init) memcpy(in->memory[i], m->objects[i].init, m->objects[i].size);
    }
    return 0;
}

void mir_interp_free(MirInterp *in) {
    for (size_t i = 0; in->memory && i < in->module->num_objects; i++) free(in->memory[i]);
    free(in->memory);
    in->memory = NULL;
}

/* Pointers are (object << 32) | offset. */
#define PTR(obj, off) (((int64_t)(obj) << 32) | (uint32_t)(off))
#define PTR_OBJ(p) ((uint32_t)((uint64_t)(p) >> 32))
#define PTR_OFF(p) ((int32_t)(uint32_t)(p))

static int64_t wrap(int64_t v, MirType t) {
    switch (t) {
        case MIR_TYPE_I8:
            return (int64_t)(uint8_t)v;
        case MIR_TYPE_I16:
            return (int64_t)(uint16_t)v;
        case MIR_TYPE_I32:
            return (int64_t)(uint32_t)v;
        default:
            return v;
    }
}

static int64_t sign_extend(int64_t v, MirType t) {
    switch (t) {
        case MIR_TYPE_I8:
            return (int8_t)v;
        case MIR_TYPE_I16:
            return (int16_t)v;
        case MIR_TYPE_I32:
            return (int32_t)v;
        default:
            return v;
    }
}

static int64_t operand(const int64_t *values, MirOperand o, MirType t) {
    return o.kind == MIR_OPND_IMM ? wrap(o.imm, t) : values[o.value];
}

static uint8_t *resolve(MirInterp *in, const int64_t *values, MirAddress a, size_t bytes) {
    uint32_t obj;
    int64_t off;
    if (a.object != MIR_NONE) {
        obj = a.object;
        off = a.offset;
    } else {
        int64_t p = values[a.pointer];
        obj = PTR_OBJ(p);
        off = (int64_t)PTR_OFF(p) + a.offset;
    }
    if (obj >= in->module->num_objects || off < 0 || (uint64_t)off + bytes > in->module->objects[obj].size) {
        fprintf(stderr, "mir_interp: access outside object %u at offset %lld\n", (unsigned)obj, (long long)off);
        return NULL;
    }
    return in->memory[obj] + off;
}

int mir_interp_call(MirInterp *in, uint32_t function, const int64_t *args, int64_t *result) {
    const MirFunction *fn = &in->module->functions[function];
    int64_t *values = calloc(fn->num_values ? fn->num_values : 1, sizeof(int64_t));
    if (!values) return -1;
    for (size_t p = 0; p < fn->num_params; p++) values[p] = wrap(args[p], fn->value_types[p]);

    size_t b = 0;
    int rc = 0;
    for (;;) {
        const MirBlock *block = &fn->blocks[b];
        for (size_t i = 0; i < block->count && rc == 0; i++) {
            const MirInst *x = &block->insts[i];
            if (in->budget-- == 0) {
                rc = -1;
                break;
            }
            int64_t a = 0, c = 0;
            if (x->op == MIR_ZEXT || x->op == MIR_SEXT || x->op == MIR_TRUNC) {
                MirType from = fn->value_types[x->a.value];
                int64_t v = values[x->a.value];
                values[x->dst] = wrap(x->op == MIR_SEXT ? sign_extend(v, from) : v, x->type);
                continue;
            }
            if (x->op != MIR_LOAD && x->op != MIR_STORE && x->op != MIR_ADDR && x->op != MIR_TARGET &&
                x->op != MIR_PTR_ADD) {
                a = operand(values, x->a, x->type);
                if (x->b.kind != MIR_OPND_NONE) c = operand(values, x->b, x->type);
            }
            switch (x->op) {
                case MIR_CONST:
                case MIR_COPY: values[x->dst] = a; break;
                case MIR_ADD: values[x->dst] = wrap(a + c, x->type); break;
                case MIR_SUB: values[x->dst] = wrap(a - c, x->type); break;
                case MIR_MUL: values[x->dst] = wrap((int64_t)((uint64_t)a * (uint64_t)c), x->type); break;
                case MIR_AND: values[x->dst] = a & c; break;
                case MIR_OR: values[x->dst] = a | c; break;
                case MIR_XOR: values[x->dst] = a ^ c; break;
                case MIR_CMP: {
                    int r = 0;
                    if (x->pred == MIR_CMP_EQ) r = a == c;
                    if (x->pred == MIR_CMP_NE) r = a != c;
                    if (x->pred == MIR_CMP_ULT) r = a < c;
                    if (x->pred == MIR_CMP_SLT) r = sign_extend(a, x->type) < sign_extend(c, x->type);
                    values[x->dst] = r;
                    break;
                }
                case MIR_LOAD:
                case MIR_STORE: {
                    size_t n = mir_int_bytes(x->type);
                    uint8_t *p = resolve(in, values, x->addr, n);
                    if (!p) {
                        rc = -1;
                        break;
                    }
                    if (x->op == MIR_LOAD) {
                        uint64_t v = 0;
                        for (size_t k = 0; k < n; k++) v |= (uint64_t)p[k] << (8 * k);
                        values[x->dst] = (int64_t)v;
                    } else {
                        uint64_t v = (uint64_t)operand(values, x->a, x->type);
                        for (size_t k = 0; k < n; k++) p[k] = (uint8_t)(v >> (8 * k));
                    }
                    break;
                }
                case MIR_ADDR: values[x->dst] = PTR(x->addr.object, x->addr.offset); break;
                case MIR_PTR_ADD: {
                    int64_t p = values[x->a.value];
                    int64_t off = sign_extend(operand(values, x->b, MIR_TYPE_I16), MIR_TYPE_I16);
                    values[x->dst] = PTR(PTR_OBJ(p), PTR_OFF(p) + off);
                    break;
                }
                case MIR_ZEXT:
                case MIR_SEXT:
                case MIR_TRUNC:
                    break;
                case MIR_TARGET:
                    fprintf(stderr, "mir_interp: cannot execute a target region\n");
                    rc = -1;
                    break;
            }
        }
        if (rc != 0) break;
        const MirTerminator *t = &block->term;
        if (t->kind == MIR_TERM_BR) {
            b = t->then_block;
        } else if (t->kind == MIR_TERM_CBR) {
            b = values[t->cond.value] != 0 ? t->then_block : t->else_block;
        } else {
            *result = t->value.kind == MIR_OPND_NONE ? 0 : operand(values, t->value, fn->return_type);
            break;
        }
    }
    free(values);
    return rc;
}
