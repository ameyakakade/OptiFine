#include "mir_programs.h"

#define I8 MIR_TYPE_I8
#define I16 MIR_TYPE_I16
#define I32 MIR_TYPE_I32
#define V(x) mir_value(x)
#define K(x) mir_imm(x)

uint32_t mirp_arith32(MirModule *m) {
    uint32_t f = mir_add_function(m, "arith32", I32);
    uint32_t a = mir_add_param(m, f, I32), b = mir_add_param(m, f, I32);
    uint32_t s = mir_new_value(m, f, I32), t = mir_new_value(m, f, I32), u = mir_new_value(m, f, I32);
    uint32_t e = mir_add_block(m, f);
    mir_emit_binary(m, f, e, MIR_ADD, s, I32, V(a), V(b), 0);
    mir_emit_binary(m, f, e, MIR_SUB, s, I32, V(s), K(7), 0);
    mir_emit_binary(m, f, e, MIR_AND, t, I32, V(a), V(b), 0);
    mir_emit_binary(m, f, e, MIR_OR, t, I32, V(t), K(0x0F0F), 0);
    mir_emit_binary(m, f, e, MIR_XOR, u, I32, V(s), V(t), 0);
    mir_ret(m, f, e, V(u));
    return f;
}

uint32_t mirp_mul8(MirModule *m) {
    uint32_t f = mir_add_function(m, "mul8", I8);
    uint32_t a = mir_add_param(m, f, I8), b = mir_add_param(m, f, I8);
    uint32_t p = mir_new_value(m, f, I8);
    uint32_t e = mir_add_block(m, f);
    mir_emit_binary(m, f, e, MIR_MUL, p, I8, V(a), V(b), 0);
    mir_emit_binary(m, f, e, MIR_ADD, p, I8, V(p), K(1), 0);
    mir_ret(m, f, e, V(p));
    return f;
}

uint32_t mirp_load_store(MirModule *m) {
    uint32_t g = mir_add_object(m, MIR_MEM_GLOBAL, 4, "g", MIR_NONE);
    uint32_t tab = mir_add_object(m, MIR_MEM_CONST, 8, "table", MIR_NONE);
    const uint8_t init[8] = {1, 0, 2, 0, 0x34, 0x12, 4, 0};
    mir_object_set_init(m, tab, init, sizeof(init));
    uint32_t f = mir_add_function(m, "load_store", MIR_TYPE_VOID);
    uint32_t x = mir_new_value(m, f, I16), y = mir_new_value(m, f, I16);
    uint32_t e = mir_add_block(m, f);
    mir_emit_load(m, f, e, x, I16, mir_at_object(g, 0), 0);
    mir_emit_load(m, f, e, y, I16, mir_at_object(tab, 4), 0);
    mir_emit_binary(m, f, e, MIR_ADD, x, I16, V(x), V(y), 0);
    mir_emit_store(m, f, e, I16, mir_at_object(g, 2), V(x), 0);
    mir_ret(m, f, e, mir_none());
    return f;
}

uint32_t mirp_diamond(MirModule *m) {
    uint32_t f = mir_add_function(m, "diamond", I16);
    uint32_t a = mir_add_param(m, f, I16), b = mir_add_param(m, f, I16);
    uint32_t lt = mir_new_value(m, f, I8), r = mir_new_value(m, f, I16);
    uint32_t entry = mir_add_block(m, f), then_b = mir_add_block(m, f), else_b = mir_add_block(m, f),
             join = mir_add_block(m, f);
    mir_emit_cmp(m, f, entry, MIR_CMP_SLT, lt, I16, V(a), V(b), 0);
    mir_cbr(m, f, entry, V(lt), then_b, else_b);
    mir_emit_binary(m, f, then_b, MIR_SUB, r, I16, V(b), V(a), 0);
    mir_br(m, f, then_b, join);
    mir_emit_binary(m, f, else_b, MIR_SUB, r, I16, V(a), V(b), 0);
    mir_br(m, f, else_b, join);
    mir_ret(m, f, join, V(r));
    return f;
}

uint32_t mirp_compares(MirModule *m) {
    uint32_t f = mir_add_function(m, "compares", I8);
    uint32_t x = mir_add_param(m, f, I8), y = mir_add_param(m, f, I8);
    uint32_t eq = mir_new_value(m, f, I8), ne = mir_new_value(m, f, I8), lt = mir_new_value(m, f, I8);
    uint32_t e = mir_add_block(m, f);
    mir_emit_cmp(m, f, e, MIR_CMP_EQ, eq, I8, V(x), V(y), 0);
    mir_emit_cmp(m, f, e, MIR_CMP_NE, ne, I8, V(x), V(y), 0);
    mir_emit_cmp(m, f, e, MIR_CMP_ULT, lt, I8, V(x), V(y), 0);
    mir_emit_binary(m, f, e, MIR_ADD, ne, I8, V(ne), V(ne), 0);
    mir_emit_binary(m, f, e, MIR_ADD, lt, I8, V(lt), V(lt), 0);
    mir_emit_binary(m, f, e, MIR_ADD, lt, I8, V(lt), V(lt), 0);
    mir_emit_binary(m, f, e, MIR_ADD, eq, I8, V(eq), V(ne), 0);
    mir_emit_binary(m, f, e, MIR_ADD, eq, I8, V(eq), V(lt), 0);
    mir_ret(m, f, e, V(eq));
    return f;
}

uint32_t mirp_loop(MirModule *m) {
    uint32_t f = mir_add_function(m, "loop", I32);
    uint32_t arr = mir_add_object(m, MIR_MEM_STACK, 10, "loop.a", f);
    uint32_t i = mir_new_value(m, f, I8), s = mir_new_value(m, f, I32), p = mir_new_value(m, f, MIR_TYPE_PTR);
    uint32_t v = mir_new_value(m, f, I8), w = mir_new_value(m, f, I8), wide = mir_new_value(m, f, I32);
    uint32_t done = mir_new_value(m, f, I8);
    uint32_t entry = mir_add_block(m, f), head = mir_add_block(m, f), body = mir_add_block(m, f),
             exit = mir_add_block(m, f);
    mir_emit_const(m, f, entry, i, I8, 0, 0);
    mir_emit_const(m, f, entry, s, I32, 0, 0);
    mir_emit_addr(m, f, entry, p, arr, 0, 0);
    mir_br(m, f, entry, head);
    mir_emit_cmp(m, f, head, MIR_CMP_EQ, done, I8, V(i), K(10), 0);
    mir_cbr(m, f, head, V(done), exit, body);
    mir_emit_binary(m, f, body, MIR_MUL, v, I8, V(i), K(3), 0);
    mir_emit_store(m, f, body, I8, mir_at_pointer(p, 0), V(v), 0);
    mir_emit_load(m, f, body, w, I8, mir_at_pointer(p, 0), 0);
    mir_emit_convert(m, f, body, MIR_ZEXT, wide, I32, w, 0);
    mir_emit_binary(m, f, body, MIR_ADD, s, I32, V(s), V(wide), 0);
    {
        MirInst step = {0};
        step.op = MIR_PTR_ADD;
        step.type = MIR_TYPE_PTR;
        step.dst = p;
        step.a = V(p);
        step.b = K(1);
        step.addr = mir_at_object(MIR_NONE, 0);
        mir_append(m, f, body, &step);
    }
    mir_emit_binary(m, f, body, MIR_ADD, i, I8, V(i), K(1), 0);
    mir_br(m, f, body, head);
    mir_ret(m, f, exit, V(s));
    return f;
}

uint32_t mirp_imperative_values(MirModule *m) {
    uint32_t f = mir_add_function(m, "imperative_values", I16);
    uint32_t x = mir_add_param(m, f, I16);
    uint32_t y = mir_new_value(m, f, I16), c = mir_new_value(m, f, I8);
    uint32_t sum = mir_new_value(m, f, I16), i = mir_new_value(m, f, I16);
    uint32_t b[7];
    for (int k = 0; k < 7; k++) b[k] = mir_add_block(m, f);
    mir_emit_binary(m, f, b[0], MIR_ADD, y, I16, V(x), K(3), 0);
    mir_emit_cmp(m, f, b[0], MIR_CMP_SLT, c, I16, K(10), V(y), 0); /* y > 10 is 10 <s y */
    mir_cbr(m, f, b[0], V(c), b[1], b[2]);
    mir_emit_binary(m, f, b[1], MIR_SUB, y, I16, V(y), K(2), 0);
    mir_br(m, f, b[1], b[3]);
    mir_emit_binary(m, f, b[2], MIR_ADD, y, I16, V(y), K(4), 0);
    mir_br(m, f, b[2], b[3]); /* merge */
    mir_emit_const(m, f, b[3], sum, I16, 0, 0);
    mir_emit_const(m, f, b[3], i, I16, 0, 0);
    mir_br(m, f, b[3], b[4]);
    mir_emit_cmp(m, f, b[4], MIR_CMP_SLT, c, I16, V(i), K(5), 0);
    mir_cbr(m, f, b[4], V(c), b[5], b[6]);
    mir_emit_binary(m, f, b[5], MIR_ADD, sum, I16, V(sum), V(y), 0);
    mir_emit_binary(m, f, b[5], MIR_ADD, i, I16, V(i), K(1), 0);
    mir_br(m, f, b[5], b[4]); /* back edge */
    mir_ret(m, f, b[6], V(sum));
    return f;
}

uint32_t mirp_imperative_memory(MirModule *m) {
    uint32_t f = mir_add_function(m, "imperative_memory", I16);
    uint32_t x = mir_add_param(m, f, I16);
    uint32_t oy = mir_add_object(m, MIR_MEM_STACK, 2, "imperative_memory.y", f);
    uint32_t os = mir_add_object(m, MIR_MEM_STACK, 2, "imperative_memory.sum", f);
    uint32_t oi = mir_add_object(m, MIR_MEM_STACK, 2, "imperative_memory.i", f);
    uint32_t t = mir_new_value(m, f, I16), u = mir_new_value(m, f, I16), c = mir_new_value(m, f, I8);
    uint32_t b[7];
    for (int k = 0; k < 7; k++) b[k] = mir_add_block(m, f);
    mir_emit_binary(m, f, b[0], MIR_ADD, t, I16, V(x), K(3), 0);
    mir_emit_store(m, f, b[0], I16, mir_at_object(oy, 0), V(t), 0);
    mir_emit_load(m, f, b[0], t, I16, mir_at_object(oy, 0), 0);
    mir_emit_cmp(m, f, b[0], MIR_CMP_SLT, c, I16, K(10), V(t), 0);
    mir_cbr(m, f, b[0], V(c), b[1], b[2]);
    mir_emit_load(m, f, b[1], t, I16, mir_at_object(oy, 0), 0);
    mir_emit_binary(m, f, b[1], MIR_SUB, t, I16, V(t), K(2), 0);
    mir_emit_store(m, f, b[1], I16, mir_at_object(oy, 0), V(t), 0);
    mir_br(m, f, b[1], b[3]);
    mir_emit_load(m, f, b[2], t, I16, mir_at_object(oy, 0), 0);
    mir_emit_binary(m, f, b[2], MIR_ADD, t, I16, V(t), K(4), 0);
    mir_emit_store(m, f, b[2], I16, mir_at_object(oy, 0), V(t), 0);
    mir_br(m, f, b[2], b[3]);
    mir_emit_store(m, f, b[3], I16, mir_at_object(os, 0), K(0), 0);
    mir_emit_store(m, f, b[3], I16, mir_at_object(oi, 0), K(0), 0);
    mir_br(m, f, b[3], b[4]);
    mir_emit_load(m, f, b[4], t, I16, mir_at_object(oi, 0), 0);
    mir_emit_cmp(m, f, b[4], MIR_CMP_SLT, c, I16, V(t), K(5), 0);
    mir_cbr(m, f, b[4], V(c), b[5], b[6]);
    mir_emit_load(m, f, b[5], t, I16, mir_at_object(os, 0), 0);
    mir_emit_load(m, f, b[5], u, I16, mir_at_object(oy, 0), 0);
    mir_emit_binary(m, f, b[5], MIR_ADD, t, I16, V(t), V(u), 0);
    mir_emit_store(m, f, b[5], I16, mir_at_object(os, 0), V(t), 0);
    mir_emit_load(m, f, b[5], t, I16, mir_at_object(oi, 0), 0);
    mir_emit_binary(m, f, b[5], MIR_ADD, t, I16, V(t), K(1), 0);
    mir_emit_store(m, f, b[5], I16, mir_at_object(oi, 0), V(t), 0);
    mir_br(m, f, b[5], b[4]);
    mir_emit_load(m, f, b[6], t, I16, mir_at_object(os, 0), 0);
    mir_ret(m, f, b[6], V(t));
    return f;
}
