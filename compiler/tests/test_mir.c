/* MIR on its own, with no workload and no target: the shared programs
 * verify and compute the right results in the reference interpreter, and
 * each malformed module is rejected by mir_verify with a diagnostic. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mir_interp.h"
#include "mir_programs.h"

static int64_t call(const MirModule *m, uint32_t f, int64_t a, int64_t b, MirInterp *keep) {
    MirInterp local, *in = keep ? keep : &local;
    if (!keep) assert(mir_interp_init(in, m) == 0);
    int64_t args[2] = {a, b}, r = -1;
    assert(mir_interp_call(in, f, args, &r) == 0);
    if (!keep) mir_interp_free(in);
    return r;
}

static void verify_ok(const MirModule *m) {
    char msg[256];
    if (mir_verify(m, msg, sizeof(msg)) != 0) {
        fprintf(stderr, "unexpected rejection: %s\n", msg);
        abort();
    }
}

static void expect_reject(MirModule *m, const char *expect, const char *what) {
    char msg[256];
    int rc = mir_verify(m, msg, sizeof(msg));
    if (rc == 0 || strstr(msg, expect) == NULL) {
        fprintf(stderr, "%s: rc=%d message='%s', expected '%s'\n", what, rc, msg, expect);
        abort();
    }
    printf("  rejected %-34s %s\n", what, msg);
    mir_module_free(m);
}

int main(void) {
    MirModule m;
    mir_module_init(&m);
    uint32_t arith = mirp_arith32(&m), mul = mirp_mul8(&m), ls = mirp_load_store(&m), dia = mirp_diamond(&m),
             cmp = mirp_compares(&m), loop = mirp_loop(&m);
    verify_ok(&m);
    printf("  six programs verify (%zu functions, %zu objects)\n", m.num_functions, m.num_objects);

    int64_t a = 0x12345678, b = 0x0F0F00FF;
    int64_t want = (int64_t)(uint32_t)((uint32_t)(a + b - 7) ^ (uint32_t)((a & b) | 0x0F0F));
    assert(call(&m, arith, a, b, NULL) == want);
    assert(call(&m, mul, 13, 21, NULL) == (13 * 21 + 1) % 256);
    assert(call(&m, dia, 5, 900, NULL) == 895);
    assert(call(&m, dia, (int16_t)-300, 2, NULL) == (uint16_t)302);
    assert(call(&m, dia, 7, 7, NULL) == 0);
    assert(call(&m, cmp, 3, 9, NULL) == 2 + 4);
    assert(call(&m, cmp, 9, 9, NULL) == 1);
    assert(call(&m, cmp, 200, 9, NULL) == 2);
    assert(call(&m, loop, 0, 0, NULL) == 135);
    {
        MirInterp in;
        assert(mir_interp_init(&in, &m) == 0);
        in.memory[0][0] = 0x10; /* g[0] = 0x0110 */
        in.memory[0][1] = 0x01;
        call(&m, ls, 0, 0, &in);
        assert(in.memory[0][2] == 0x44 && in.memory[0][3] == 0x13); /* 0x0110 + 0x1234 */
        mir_interp_free(&in);
    }
    printf("  arithmetic, i8 multiply, load/store, diamond, compares, counted loop: correct\n");
    mir_module_free(&m);

    /* --- malformed modules --- */
#define FRESH(builder) (mir_module_init(&m), builder(&m))
    uint32_t f;

    f = FRESH(mirp_diamond); m.functions[f].blocks[1].term.then_block = 9;
    expect_reject(&m, "does not exist", "branch to a missing block");
    f = FRESH(mirp_diamond); m.functions[f].blocks[0].term.else_block = MIR_NONE;
    expect_reject(&m, "does not exist", "cbr with a missing edge");
    f = FRESH(mirp_diamond); m.functions[f].blocks[3].term.kind = MIR_TERM_NONE;
    expect_reject(&m, "no terminator", "block without a terminator");
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].insts[1].a.value = 77;
    expect_reject(&m, "does not exist", "reference to a missing value");
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].insts[0].type = MIR_TYPE_I16;
    expect_reject(&m, "wrong type", "operand type mismatch");
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].insts[1].b = mir_imm(1LL << 40);
    expect_reject(&m, "does not fit", "immediate wider than its type");
    f = FRESH(mirp_diamond); m.functions[f].blocks[0].insts[0].dst = 3; /* cmp result into an i16 */
    expect_reject(&m, "wrong type", "compare into a non-I8 value");
    f = FRESH(mirp_diamond); m.functions[f].blocks[0].term.cond = mir_value(0); /* an i16 condition */
    expect_reject(&m, "wrong type", "branch on a non-I8 condition");
    f = FRESH(mirp_diamond); m.functions[f].blocks[2].count = 0; /* r assigned on one path only */
    expect_reject(&m, "before any assignment", "use before assignment on a path");
    f = FRESH(mirp_loop); m.functions[f].blocks[0].insts[1].op = MIR_COPY; /* s never assigned: copy of s */
    m.functions[f].blocks[0].insts[1].a = mir_value(1);
    expect_reject(&m, "before any assignment", "self-use before assignment");
    f = FRESH(mirp_load_store); m.functions[f].blocks[0].insts[3].addr.object = 1; /* store into `table` */
    expect_reject(&m, "constant object", "store to a constant");
    f = FRESH(mirp_load_store); m.functions[f].blocks[0].insts[1].addr.offset = 7;
    expect_reject(&m, "outside", "load past an object's end");
    f = FRESH(mirp_load_store); m.functions[f].blocks[0].insts[0].addr.object = 42;
    expect_reject(&m, "does not exist", "missing memory object");
    f = FRESH(mirp_load_store); m.objects[1].init[0] = 1; free(m.objects[1].init); m.objects[1].init = NULL;
    expect_reject(&m, "initializer", "constant without initializer");
    mir_module_init(&m);
    mirp_loop(&m);
    f = mirp_arith32(&m);
    {
        MirInst use = {0};
        use.op = MIR_ADDR;
        use.type = MIR_TYPE_PTR;
        use.dst = mir_new_value(&m, f, MIR_TYPE_PTR);
        use.addr = mir_at_object(0, 0); /* loop.a belongs to `loop` */
        mir_append(&m, f, 0, &use);
    }
    expect_reject(&m, "another function", "stack object of another function");
    f = FRESH(mirp_loop); m.functions[f].blocks[2].insts[3].op = MIR_TRUNC;
    expect_reject(&m, "trunc must narrow", "widening trunc");
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].term.value = mir_none();
    expect_reject(&m, "missing", "non-void function returning nothing");
    mir_module_init(&m);
    mirp_arith32(&m);
    mir_add_object(&m, MIR_MEM_GLOBAL, 4, "dup", MIR_NONE);
    mir_add_object(&m, MIR_MEM_GLOBAL, 4, "dup", MIR_NONE);
    expect_reject(&m, "repeated", "duplicate object name");
    mir_module_init(&m);
    mir_add_function(&m, "empty", MIR_TYPE_VOID);
    expect_reject(&m, "no blocks", "function without blocks");
    mir_module_init(&m);
    mirp_arith32(&m);
    m.out_of_memory = 1;
    expect_reject(&m, "out of memory", "construction out of memory");

    /* --- noncanonical fields: an id in a field the opcode or terminator
     * does not use. Each of the first four once made mir_verify itself read
     * or write out of bounds; all must be rejected with a diagnostic. --- */
#define FAR 1000000u
    f = FRESH(mirp_load_store); m.functions[f].blocks[0].insts[3].dst = FAR; /* store */
    expect_reject(&m, "assigns no value", "store with a stray destination");
    mir_module_init(&m);
    f = mir_add_function(&m, "region", MIR_TYPE_VOID);
    {
        static int payload;
        uint32_t b = mir_add_block(&m, f);
        MirTargetCode *t = calloc(1, sizeof(*t));
        assert(t);
        t->target = "avr";
        t->payload = &payload;
        mir_emit_target(&m, f, b, t, 0);
        mir_ret(&m, f, b, mir_none());
        verify_ok(&m);
        m.functions[f].blocks[0].insts[0].dst = FAR;
    }
    expect_reject(&m, "assigns no value", "target region with a stray destination");
    f = FRESH(mirp_loop); m.functions[f].blocks[0].insts[0].op = MIR_COPY; /* i = 0 as a copy */
    m.functions[f].blocks[0].insts[0].b = mir_value(FAR);
    expect_reject(&m, "no second operand", "copy with a stray second operand");
    f = FRESH(mirp_diamond); m.functions[f].blocks[1].term.cond = mir_value(FAR);
    expect_reject(&m, "only cbr takes a condition", "br with a stray condition");

    /* Neighbouring families: unused fields of other opcodes and terminators. */
    f = FRESH(mirp_load_store); m.functions[f].blocks[0].insts[3].dst = 0; /* even a real value */
    expect_reject(&m, "assigns no value", "store naming an existing value");
    f = FRESH(mirp_load_store); m.functions[f].blocks[0].insts[0].a = mir_value(FAR);
    expect_reject(&m, "no first operand", "load with a stray operand");
    f = FRESH(mirp_load_store); m.functions[f].blocks[0].insts[3].b = mir_imm(1);
    expect_reject(&m, "no second operand", "store with a second operand");
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].insts[0].addr = mir_at_pointer(FAR, 0);
    expect_reject(&m, "takes no address", "add with a stray address");
    f = FRESH(mirp_loop); m.functions[f].blocks[2].insts[3].b = mir_value(0); /* zext */
    expect_reject(&m, "no second operand", "conversion with a second operand");
    f = FRESH(mirp_loop); m.functions[f].blocks[0].insts[0].b = mir_imm(3); /* const */
    expect_reject(&m, "no second operand", "const with a second operand");
    f = FRESH(mirp_loop); m.functions[f].blocks[0].insts[2].a = mir_value(FAR); /* addr */
    expect_reject(&m, "no first operand", "addr with a stray operand");
    f = FRESH(mirp_loop); m.functions[f].blocks[2].insts[5].addr = mir_at_object(0, 0); /* ptr_add */
    expect_reject(&m, "takes no address", "ptr_add with an address");
    {
        MirTargetCode stray = {0};
        f = FRESH(mirp_arith32);
        m.functions[f].blocks[0].insts[0].target = &stray;
        expect_reject(&m, "only a target region", "add carrying target code");
    }
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].insts[0].a.kind = (MirOperandKind)7;
    expect_reject(&m, "unknown kind", "operand of an unknown kind");
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].insts[0].op = (MirOpcode)99;
    expect_reject(&m, "unknown opcode", "unknown opcode");

    /* Invalid ids in fields that are used. */
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].insts[0].dst = FAR;
    expect_reject(&m, "does not exist", "add into a missing value");
    f = FRESH(mirp_loop); m.functions[f].blocks[2].insts[1].addr.pointer = FAR; /* store through p */
    expect_reject(&m, "does not exist", "store through a missing pointer");
    f = FRESH(mirp_diamond); m.functions[f].blocks[0].term.cond = mir_value(FAR);
    expect_reject(&m, "does not exist", "cbr on a missing value");
    f = FRESH(mirp_arith32); m.functions[f].blocks[0].term.value = mir_value(FAR);
    expect_reject(&m, "does not exist", "ret of a missing value");

    /* Malformed terminators. */
    f = FRESH(mirp_diamond); m.functions[f].blocks[3].term.kind = (MirTermKind)42;
    expect_reject(&m, "no terminator", "terminator of an unknown kind");
    f = FRESH(mirp_diamond); m.functions[f].blocks[1].term.value = mir_value(0);
    expect_reject(&m, "only ret takes a value", "br carrying a value");
    f = FRESH(mirp_diamond); m.functions[f].blocks[1].term.else_block = 2;
    expect_reject(&m, "does not use", "br naming a second successor");
    f = FRESH(mirp_diamond); m.functions[f].blocks[3].term.then_block = 0;
    expect_reject(&m, "does not use", "ret naming a successor");
    f = FRESH(mirp_diamond); m.functions[f].blocks[0].term.value = mir_imm(1);
    expect_reject(&m, "only ret takes a value", "cbr carrying a value");
    f = FRESH(mirp_diamond); m.functions[f].blocks[0].term.cond.kind = (MirOperandKind)9;
    expect_reject(&m, "unknown kind", "condition of an unknown kind");
#undef FAR

    printf("test_mir: all tests passed\n");
    return 0;
}
