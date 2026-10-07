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

static int operand_kind_ok(MirOperand o) {
    return o.kind == MIR_OPND_NONE || o.kind == MIR_OPND_VALUE || o.kind == MIR_OPND_IMM;
}

static int address_empty(MirAddress a) {
    return a.object == MIR_NONE && a.pointer == MIR_NONE && a.offset == 0;
}

/* Every field the opcode does not use holds its empty form (mir.h, "Fields
 * each opcode uses"). Checked before anything reads a field, so the checks
 * below and the dataflow only ever see ids in fields that mean something. */
static int check_shape(Ctx *c, const MirInst *in) {
    unsigned fields = mir_opcode_fields(in->op);
    if (fields == 0) return fail(c->d, WHERE "unknown opcode %d", AT(c), (int)in->op);
    if (!operand_kind_ok(in->a) || !operand_kind_ok(in->b)) {
        return fail(c->d, WHERE "operand of unknown kind", AT(c));
    }
    if (!(fields & MIR_FIELD_DST) && in->dst != MIR_NONE) {
        return fail(c->d, WHERE "this opcode assigns no value, but its destination names value %u", AT(c),
                    (unsigned)in->dst);
    }
    if (!(fields & MIR_FIELD_A) && in->a.kind != MIR_OPND_NONE) {
        return fail(c->d, WHERE "this opcode takes no first operand, but one is set", AT(c));
    }
    if (!(fields & MIR_FIELD_B) && in->b.kind != MIR_OPND_NONE) {
        return fail(c->d, WHERE "this opcode takes no second operand, but one is set", AT(c));
    }
    if (!(fields & MIR_FIELD_ADDR) && !address_empty(in->addr)) {
        return fail(c->d, WHERE "this opcode takes no address, but one is set", AT(c));
    }
    if (!(fields & MIR_FIELD_TARGET) && in->target != NULL) {
        return fail(c->d, WHERE "only a target region carries target code", AT(c));
    }
    return 0;
}

static int check_inst(Ctx *c, const MirInst *in) {
    if (check_shape(c, in) != 0) return -1;
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
            if (in->type != MIR_TYPE_VOID) return fail(c->d, WHERE "target region with a type", AT(c));
            if ((t->num_reads && !t->reads) || (t->num_writes && !t->writes)) {
                return fail(c->d, WHERE "target region with a missing read or write list", AT(c));
            }
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
    if (t->kind != MIR_TERM_BR && t->kind != MIR_TERM_CBR && t->kind != MIR_TERM_RET) {
        return fail(c->d, WHERE "block has no terminator", AT(c));
    }
    /* As for instructions: fields this kind does not use hold their empty
     * form (mir.h). */
    if (!operand_kind_ok(t->cond) || !operand_kind_ok(t->value)) {
        return fail(c->d, WHERE "terminator operand of unknown kind", AT(c));
    }
    if (t->kind != MIR_TERM_CBR && t->cond.kind != MIR_OPND_NONE) {
        return fail(c->d, WHERE "only cbr takes a condition", AT(c));
    }
    if (t->kind != MIR_TERM_RET && t->value.kind != MIR_OPND_NONE) {
        return fail(c->d, WHERE "only ret takes a value", AT(c));
    }
    if ((t->kind == MIR_TERM_RET && t->then_block != MIR_NONE) ||
        (t->kind != MIR_TERM_CBR && t->else_block != MIR_NONE)) {
        return fail(c->d, WHERE "terminator names a successor block it does not use", AT(c));
    }
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

/* Applies block `b` (of c->fn) to the defined set `def` (in place). Reads
 * only the fields each opcode uses (mir_inst_uses / mir_inst_def /
 * mir_term_use), and bounds-checks every id before it indexes `def`; the
 * structural checks have already rejected out-of-range ids, so a failure here
 * is a verifier bug, reported rather than acted on. With `report`, also fails
 * on the first use of a value not in the set. */
static int transfer(Ctx *c, size_t b, uint8_t *def, int report) {
    const MirBlock *block = &c->fn->blocks[b];
    size_t nv = c->fn->num_values;
    c->block = b;
    for (size_t i = 0; i < block->count; i++) {
        const MirInst *in = &block->insts[i];
        uint32_t used[3];
        size_t n = mir_inst_uses(in, used);
        c->inst = i;
        for (size_t k = 0; k < n; k++) {
            if (used[k] >= nv) return fail(c->d, WHERE "uses value %u, which does not exist", AT(c), (unsigned)used[k]);
            if (report && !def[used[k]]) {
                return fail(c->d, WHERE "uses value %u before any assignment reaches it", AT(c),
                            (unsigned)used[k]);
            }
        }
        uint32_t dst = mir_inst_def(in);
        if (dst != MIR_NONE) {
            if (dst >= nv) return fail(c->d, WHERE "assigns value %u, which does not exist", AT(c), (unsigned)dst);
            def[dst] = 1;
        }
    }
    c->inst = block->count;
    uint32_t v = mir_term_use(&block->term);
    if (v != MIR_NONE) {
        if (v >= nv) return fail(c->d, WHERE "terminator uses value %u, which does not exist", AT(c), (unsigned)v);
        if (report && !def[v]) {
            return fail(c->d, WHERE "terminator uses value %u before any assignment reaches it", AT(c),
                        (unsigned)v);
        }
    }
    return 0;
}

/* The successors of a (structurally checked) terminator. */
static size_t successors(const MirTerminator *t, uint32_t succ[2]) {
    size_t n = 0;
    if (t->kind == MIR_TERM_BR || t->kind == MIR_TERM_CBR) succ[n++] = t->then_block;
    if (t->kind == MIR_TERM_CBR) succ[n++] = t->else_block;
    return n;
}

static int check_definitions(Ctx *c) {
    const MirFunction *fn = c->fn;
    size_t nv = fn->num_values, nb = fn->num_blocks;
    if (nv == 0) return 0;
    if (nb > SIZE_MAX / nv) return fail(c->d, "function %s is too large to verify", fn->name);
    /* in[b]: values assigned on every path to the start of b. Start at
     * "everything" for all but the entry -- whose only incoming state is the
     * function start, with just the parameters assigned; a back edge into the
     * entry intersects with that -- and intersect down to a fixpoint. */
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

    int rc = 0, changed = 1;
    while (changed && rc == 0) {
        changed = 0;
        for (size_t b = 0; b < nb && rc == 0; b++) {
            if (!reached[b]) continue;
            memcpy(tmp, in + b * nv, nv);
            rc = transfer(c, b, tmp, 0);
            uint32_t succ[2];
            size_t ns = successors(&fn->blocks[b].term, succ);
            for (size_t s = 0; s < ns && rc == 0; s++) {
                if (succ[s] >= nb) {
                    rc = fail(c->d, WHERE "branch to a block that does not exist", AT(c));
                    break;
                }
                uint8_t *row = in + (size_t)succ[s] * nv;
                if (!reached[succ[s]]) {
                    reached[succ[s]] = 1;
                    memcpy(row, tmp, nv);
                    changed = 1;
                    continue;
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

    for (size_t b = 0; b < nb && rc == 0; b++) {
        if (!reached[b]) continue; /* unreachable code has no path to check */
        memcpy(tmp, in + b * nv, nv);
        rc = transfer(c, b, tmp, 1);
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
