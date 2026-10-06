#include "optifine/mir.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char *message;
    size_t message_len;
} Diag;

static int fail(Diag *d, const char *format, ...) {
    if (d->message_len > 0) {
        va_list args;
        va_start(args, format);
        vsnprintf(d->message, d->message_len, format, args);
        va_end(args);
    }
    return -1;
}

static int is_value_type(MirType t) {
    return t == MIR_TYPE_I8 || t == MIR_TYPE_I16 || t == MIR_TYPE_I32 || t == MIR_TYPE_PTR;
}

static int is_int_type(MirType t) {
    return mir_int_bytes(t) != 0;
}

/* An immediate of an integer type must be representable in it, read either
 * as signed or as unsigned. */
static int imm_fits(int64_t imm, MirType t) {
    switch (t) {
        case MIR_TYPE_I8:
            return imm >= -128 && imm <= 255;
        case MIR_TYPE_I16:
            return imm >= -32768 && imm <= 65535;
        case MIR_TYPE_I32:
            return imm >= -2147483648LL && imm <= 4294967295LL;
        default:
            return 0;
    }
}

typedef struct {
    const MirModule *module;
    const MirFunction *fn;
    uint32_t fn_id;
    size_t block;
    size_t inst;
    Diag *d;
} Ctx;

#define WHERE "function %s block %zu inst %zu: "
#define AT(c) (c)->fn->name, (c)->block, (c)->inst

static int check_value(Ctx *c, uint32_t v, MirType expect, const char *role) {
    if (v == MIR_NONE || v >= c->fn->num_values) {
        return fail(c->d, WHERE "%s names value %u, which does not exist", AT(c), role, (unsigned)v);
    }
    if (expect != MIR_TYPE_VOID && c->fn->value_types[v] != expect) {
        return fail(c->d, WHERE "%s value %u has the wrong type", AT(c), role, (unsigned)v);
    }
    return 0;
}

/* `o` must be a value or an immediate of integer type `t` (or a PTR value
 * when t is PTR). */
static int check_operand(Ctx *c, MirOperand o, MirType t, const char *role) {
    if (o.kind == MIR_OPND_VALUE) return check_value(c, o.value, t, role);
    if (o.kind == MIR_OPND_IMM) {
        if (!imm_fits(o.imm, t)) return fail(c->d, WHERE "%s immediate does not fit its type", AT(c), role);
        return 0;
    }
    return fail(c->d, WHERE "%s operand is missing", AT(c), role);
}

static int check_object(Ctx *c, uint32_t object) {
    if (object >= c->module->num_objects) {
        return fail(c->d, WHERE "names memory object %u, which does not exist", AT(c), (unsigned)object);
    }
    const MirMemObject *obj = &c->module->objects[object];
    if (obj->kind == MIR_MEM_STACK && obj->function != c->fn_id) {
        return fail(c->d, WHERE "uses stack object '%s' of another function", AT(c), obj->name);
    }
    return 0;
}

/* An access of `bytes` bytes (0 for an address computation) at `addr`. */
static int check_address(Ctx *c, MirAddress addr, size_t bytes, int is_store) {
    if ((addr.object == MIR_NONE) == (addr.pointer == MIR_NONE)) {
        return fail(c->d, WHERE "address must name exactly one of an object and a pointer", AT(c));
    }
    if (addr.pointer != MIR_NONE) return check_value(c, addr.pointer, MIR_TYPE_PTR, "pointer");
    if (check_object(c, addr.object) != 0) return -1;
    const MirMemObject *obj = &c->module->objects[addr.object];
    if (addr.offset < 0 || (size_t)addr.offset > obj->size || bytes > obj->size - (size_t)addr.offset) {
        return fail(c->d, WHERE "access at offset %ld lies outside '%s' (%zu bytes)", AT(c), (long)addr.offset,
                    obj->name, obj->size);
    }
    if (is_store && obj->kind == MIR_MEM_CONST) {
        return fail(c->d, WHERE "stores to constant object '%s'", AT(c), obj->name);
    }
    return 0;
}

static int check_inst(Ctx *c, const MirInst *in) {
    switch (in->op) {
        case MIR_CONST:
            if (!is_int_type(in->type) || in->a.kind != MIR_OPND_IMM) {
                return fail(c->d, WHERE "const needs an integer type and an immediate", AT(c));
            }
            if (check_operand(c, in->a, in->type, "const") != 0) return -1;
            return check_value(c, in->dst, in->type, "result");
        case MIR_COPY:
            if (!is_value_type(in->type)) return fail(c->d, WHERE "copy of an illegal type", AT(c));
            if (in->type == MIR_TYPE_PTR && in->a.kind != MIR_OPND_VALUE) {
                return fail(c->d, WHERE "pointer copy needs a value operand", AT(c));
            }
            if (check_operand(c, in->a, in->type, "source") != 0) return -1;
            return check_value(c, in->dst, in->type, "result");
        case MIR_ADD:
        case MIR_SUB:
        case MIR_MUL:
        case MIR_AND:
        case MIR_OR:
        case MIR_XOR:
            if (!is_int_type(in->type)) return fail(c->d, WHERE "arithmetic needs an integer type", AT(c));
            if (check_operand(c, in->a, in->type, "left") != 0 || check_operand(c, in->b, in->type, "right") != 0) {
                return -1;
            }
            return check_value(c, in->dst, in->type, "result");
        case MIR_CMP:
            if (!is_int_type(in->type)) return fail(c->d, WHERE "compare needs an integer type", AT(c));
            if (in->pred != MIR_CMP_EQ && in->pred != MIR_CMP_NE && in->pred != MIR_CMP_ULT &&
                in->pred != MIR_CMP_SLT) {
                return fail(c->d, WHERE "unknown compare predicate", AT(c));
            }
            if (check_operand(c, in->a, in->type, "left") != 0 || check_operand(c, in->b, in->type, "right") != 0) {
                return -1;
            }
            return check_value(c, in->dst, MIR_TYPE_I8, "result");
        case MIR_ZEXT:
        case MIR_SEXT:
        case MIR_TRUNC: {
            if (!is_int_type(in->type) || in->a.kind != MIR_OPND_VALUE) {
                return fail(c->d, WHERE "conversion needs an integer type and a value operand", AT(c));
            }
            if (check_value(c, in->a.value, MIR_TYPE_VOID, "source") != 0) return -1;
            size_t from = mir_int_bytes(c->fn->value_types[in->a.value]), to = mir_int_bytes(in->type);
            if (from == 0 || (in->op == MIR_TRUNC ? from <= to : from >= to)) {
                return fail(c->d, WHERE "%s from %zu to %zu bytes", AT(c),
                            in->op == MIR_TRUNC ? "trunc must narrow" : "extension must widen", from, to);
            }
            return check_value(c, in->dst, in->type, "result");
        }
        case MIR_LOAD:
            if (!is_int_type(in->type)) return fail(c->d, WHERE "load needs an integer type", AT(c));
            if (check_address(c, in->addr, mir_int_bytes(in->type), 0) != 0) return -1;
            return check_value(c, in->dst, in->type, "result");
        case MIR_STORE:
            if (!is_int_type(in->type)) return fail(c->d, WHERE "store needs an integer type", AT(c));
            if (check_operand(c, in->a, in->type, "stored") != 0) return -1;
            return check_address(c, in->addr, mir_int_bytes(in->type), 1);
        case MIR_ADDR:
            if (in->addr.object == MIR_NONE) return fail(c->d, WHERE "addr needs an object", AT(c));
            if (check_address(c, in->addr, 0, 0) != 0) return -1;
            return check_value(c, in->dst, MIR_TYPE_PTR, "result");
        case MIR_PTR_ADD:
            if (in->a.kind != MIR_OPND_VALUE) return fail(c->d, WHERE "ptr_add needs a pointer value", AT(c));
            if (check_value(c, in->a.value, MIR_TYPE_PTR, "pointer") != 0 ||
                check_operand(c, in->b, MIR_TYPE_I16, "offset") != 0) {
                return -1;
            }
            return check_value(c, in->dst, MIR_TYPE_PTR, "result");
        case MIR_TARGET: {
            const MirTargetCode *t = in->target;
            if (!t || !t->target || !t->payload) return fail(c->d, WHERE "target region without code", AT(c));
            for (size_t i = 0; i < t->num_reads; i++) {
                if (check_object(c, t->reads[i]) != 0) return -1;
            }
            for (size_t i = 0; i < t->num_writes; i++) {
                if (check_object(c, t->writes[i]) != 0) return -1;
                if (c->module->objects[t->writes[i]].kind == MIR_MEM_CONST) {
                    return fail(c->d, WHERE "target region writes a constant object", AT(c));
                }
            }
            return 0;
        }
    }
    return fail(c->d, WHERE "unknown opcode %d", AT(c), (int)in->op);
}

static int check_terminator(Ctx *c, const MirTerminator *t) {
    size_t blocks = c->fn->num_blocks;
    switch (t->kind) {
        case MIR_TERM_BR:
            if (t->then_block >= blocks) return fail(c->d, WHERE "branch to a block that does not exist", AT(c));
            return 0;
        case MIR_TERM_CBR:
            if (t->then_block >= blocks || t->else_block >= blocks) {
                return fail(c->d, WHERE "branch to a block that does not exist", AT(c));
            }
            if (t->cond.kind != MIR_OPND_VALUE) return fail(c->d, WHERE "cbr needs a condition value", AT(c));
            return check_value(c, t->cond.value, MIR_TYPE_I8, "condition");
        case MIR_TERM_RET:
            if (c->fn->return_type == MIR_TYPE_VOID) {
                if (t->value.kind != MIR_OPND_NONE) return fail(c->d, WHERE "void function returns a value", AT(c));
                return 0;
            }
            return check_operand(c, t->value, c->fn->return_type, "returned");
        case MIR_TERM_NONE:
            break;
    }
    return fail(c->d, WHERE "block has no terminator", AT(c));
}

/* ---- use-before-assignment: a forward must-be-defined dataflow ---- */

typedef struct {
    uint8_t *bits; /* num_blocks rows of num_values bytes */
    size_t values;
} DefSets;

static int uses_value(MirOperand o, uint32_t *v) {
    if (o.kind != MIR_OPND_VALUE) return 0;
    *v = o.value;
    return 1;
}

/* Applies block `b` to the defined set `def` (in place). With `c` set,
 * reports the first use of a value not in the set. */
static int transfer(Ctx *c, const MirBlock *block, uint8_t *def) {
    for (size_t i = 0; i < block->count; i++) {
        const MirInst *in = &block->insts[i];
        uint32_t used[3];
        size_t n = 0;
        uint32_t v;
        if (uses_value(in->a, &v)) used[n++] = v;
        if (uses_value(in->b, &v)) used[n++] = v;
        if ((in->op == MIR_LOAD || in->op == MIR_STORE) && in->addr.pointer != MIR_NONE) used[n++] = in->addr.pointer;
        for (size_t k = 0; c && k < n; k++) {
            if (!def[used[k]]) {
                c->inst = i;
                return fail(c->d, WHERE "uses value %u before any assignment reaches it", AT(c),
                            (unsigned)used[k]);
            }
        }
        if (in->dst != MIR_NONE) def[in->dst] = 1;
    }
    uint32_t v;
    if (c) {
        c->inst = block->count;
        if ((uses_value(block->term.cond, &v) || uses_value(block->term.value, &v)) && !def[v]) {
            return fail(c->d, WHERE "terminator uses value %u before any assignment reaches it", AT(c),
                        (unsigned)v);
        }
    }
    return 0;
}

static int check_definitions(Ctx *c) {
    const MirFunction *fn = c->fn;
    size_t nv = fn->num_values, nb = fn->num_blocks;
    if (nv == 0) return 0;
    /* in[b]: values assigned on every path to the start of b. Start at
     * "everything" for all but the entry and intersect down to a fixpoint. */
    uint8_t *in = malloc(nb * nv);
    uint8_t *reached = calloc(nb, 1);
    uint8_t *tmp = malloc(nv);
    if (!in || !reached || !tmp) {
        free(in);
        free(reached);
        free(tmp);
        return fail(c->d, "out of memory verifying function %s", fn->name);
    }
    memset(in, 1, nb * nv);
    memset(in, 0, nv);
    for (size_t p = 0; p < fn->num_params; p++) in[p] = 1;
    reached[0] = 1;

    int changed = 1;
    while (changed) {
        changed = 0;
        for (size_t b = 0; b < nb; b++) {
            if (!reached[b]) continue;
            memcpy(tmp, in + b * nv, nv);
            transfer(NULL, &fn->blocks[b], tmp);
            const MirTerminator *t = &fn->blocks[b].term;
            uint32_t succ[2];
            size_t ns = 0;
            if (t->kind == MIR_TERM_BR) succ[ns++] = t->then_block;
            if (t->kind == MIR_TERM_CBR) {
                succ[ns++] = t->then_block;
                succ[ns++] = t->else_block;
            }
            for (size_t s = 0; s < ns; s++) {
                uint8_t *row = in + (size_t)succ[s] * nv;
                if (!reached[succ[s]]) {
                    reached[succ[s]] = 1;
                    memcpy(row, tmp, nv);
                    if (succ[s] != 0) {
                        changed = 1;
                        continue;
                    }
                }
                for (size_t v = 0; v < nv; v++) {
                    if (row[v] && !tmp[v]) {
                        row[v] = 0;
                        changed = 1;
                    }
                }
            }
        }
    }

    int rc = 0;
    for (size_t b = 0; b < nb && rc == 0; b++) {
        if (!reached[b]) continue; /* unreachable code has no path to check */
        memcpy(tmp, in + b * nv, nv);
        c->block = b;
        rc = transfer(c, &fn->blocks[b], tmp);
    }
    free(in);
    free(reached);
    free(tmp);
    return rc;
}

int mir_verify(const MirModule *module, char *message, size_t message_len) {
    Diag diag = {message, message_len};
    Diag *d = &diag;
    if (message_len > 0) message[0] = '\0';
    if (module->out_of_memory) return fail(d, "module construction ran out of memory");

    for (size_t i = 0; i < module->num_objects; i++) {
        const MirMemObject *obj = &module->objects[i];
        if (obj->name[0] == '\0') return fail(d, "object %zu has no name (or one too long)", i);
        for (size_t j = 0; j < i; j++) {
            if (strcmp(module->objects[j].name, obj->name) == 0) return fail(d, "object name '%s' is repeated", obj->name);
        }
        if (obj->size == 0) return fail(d, "object '%s' has zero size", obj->name);
        if (obj->kind > MIR_MEM_TENSOR) return fail(d, "object '%s' has an unknown kind", obj->name);
        if ((obj->kind == MIR_MEM_CONST) != (obj->init != NULL)) {
            return fail(d, "object '%s': exactly the constant objects carry an initializer", obj->name);
        }
        if ((obj->kind == MIR_MEM_STACK) != (obj->function != MIR_NONE) ||
            (obj->kind == MIR_MEM_STACK && obj->function >= module->num_functions)) {
            return fail(d, "object '%s': a stack object needs an owning function, nothing else has one",
                        obj->name);
        }
    }

    for (size_t f = 0; f < module->num_functions; f++) {
        const MirFunction *fn = &module->functions[f];
        Ctx c = {module, fn, (uint32_t)f, 0, 0, d};
        if (fn->name[0] == '\0') return fail(d, "function %zu has no name", f);
        if (fn->num_blocks == 0) return fail(d, "function %s has no blocks", fn->name);
        if (fn->num_params > fn->num_values) {
            return fail(d, "function %s: parameters must be its first values", fn->name);
        }
        if (fn->return_type != MIR_TYPE_VOID && !is_value_type(fn->return_type)) {
            return fail(d, "function %s has an illegal return type", fn->name);
        }
        for (size_t v = 0; v < fn->num_values; v++) {
            if (!is_value_type(fn->value_types[v])) {
                return fail(d, "function %s: value %zu has an illegal type", fn->name, v);
            }
        }
        for (size_t b = 0; b < fn->num_blocks; b++) {
            const MirBlock *block = &fn->blocks[b];
            c.block = b;
            for (size_t i = 0; i < block->count; i++) {
                c.inst = i;
                if (check_inst(&c, &block->insts[i]) != 0) return -1;
            }
            c.inst = block->count;
            if (check_terminator(&c, &block->term) != 0) return -1;
        }
        if (check_definitions(&c) != 0) return -1;
    }
    return 0;
}
