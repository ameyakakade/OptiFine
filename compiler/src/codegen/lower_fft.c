#include "lower_internal.h"

#include <math.h>
#include <stdio.h>

#include "optifine/codegen/registers.h"
#include "optifine/dsp_build.h"
#include "optifine/invariant.h"

/* M_PI is POSIX, not ISO C11; see dsp_build.c. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- OP_FFT_BUTTERFLY: one radix-2 decimation-in-time stage ----
 *
 * Convention, taken from the repository rather than assumed: radix-2 DIT,
 * bit-reversal applied BEFORE the
 * stages (dsp_build.c pushes OP_BIT_REVERSE then six OP_FFT_BUTTERFLY), and
 * a forward transform, since the twiddle generator uses
 * angle = -2*pi*k/N, i.e. W^k = e^(-2*pi*i*k/N).
 *
 * Complex layout is DT_COMPLEX_Q15: 4 bytes per element, little-endian
 * int16 real then int16 imaginary.
 *
 * Per-butterfly equations, with p the lower index and q = p + half_block:
 *
 *     t_re = qmul(w_re, q_re) - qmul(w_im, q_im)
 *     t_im = qmul(w_re, q_im) + qmul(w_im, q_re)
 *     out[p] = p/2 + t/2
 *     out[q] = p/2 - t/2
 *
 * where qmul(x,y) = floor(x*y / 32768), the Q15 product, and /2 is an
 * arithmetic shift right (floor toward -inf).
 *
 * The halving happens BEFORE combining, not after. Combining first would
 * compute p + t, which reaches 2.0 in Q15 whenever |p| and |t| both approach
 * 1.0 and wraps int16 before the shift could halve it -- the /2 meant to
 * bound growth would then be applied to an already-corrupted sum. Scaling
 * each operand first keeps every intermediate in range by construction, at a
 * cost of at most one extra LSB of truncation per output and no extra
 * instructions. Stage scaling is 1/2 per stage, 1/64 over the six stages.
 *
 * No saturation is performed: the per-stage halving is what bounds growth,
 * and the invariant |out| <= max(|p|,|t|) keeps every value inside Q15.
 *
 * Structure: the butterfly body is emitted once per stage and run from
 * counted loops. Data addresses advance through X (input) and Y (output);
 * the twiddle comes from the canonical 32-entry program-memory table through
 * Z. Intermediates live at fixed scratch addresses, which are loop-invariant
 * even though the data addresses are not, so the already-validated
 * lower_fixed_mul_q15 / lower_add16 / lower_sub16 / lower_asr16 helpers apply
 * unchanged.
 *
 * Geometry of stage s (half = 2^s, blocks = 32/half, span = 4*half bytes
 * between a butterfly's p and q):
 *
 *     stage  blocks  half  twiddle k = j*blocks   Z step after 4x lpm Z+
 *       0      32      1   0                      -4  (W^0 reused)
 *       1      16      2   0,16                   +60
 *       2       8      4   0,8,..,24              +28
 *       3       4      8   0,4,..,28              +12
 *       4       2     16   0,2,..,30              +4
 *       5       1     32   0,1,..,31              0
 *
 * Stage 0 is one flat loop of 32: p and q are adjacent, so X and Y walk the
 * buffers without ever stepping back. Stages 1-5 are nested: a block loop
 * resets Z to the table base and wraps a loop of `half` butterflies, and each
 * butterfly steps X/Y forward by span-4 from p to q and back by span to the
 * next p. After the butterfly loop X/Y sit at block_start+span, so the block
 * loop adds span to reach the next block. Stage 5 has a single block and no
 * block loop.
 *
 * The butterfly loop holds REG_DSP_LOOP_COUNTER (r3), and the body owns every
 * register from r16 up, so the block counter lives in one SRAM byte
 * (DSP_SCRATCH_FFT_OUTER_COUNT) touched only at block boundaries -- 7-8
 * cycles per block instead of a permanently reserved register.
 *
 * Buffers: every stage reads its input op's tensor and writes its own, which
 * sram_layout places in disjoint regions, so no stage overwrites a value it
 * has yet to read. BitReverse -> stage 0 -> ... -> stage 5, and the final
 * transform is stage 5's buffer. */

/* Butterfly scratch cells, offsets within layout->dsp_scratch_addr. */
#define BF_AC   0
#define BF_BD   2
#define BF_AD   4
#define BF_BC   6
#define BF_TRE  8
#define BF_TIM  10
#define BF_TWRE 12
#define BF_TWIM 14
#define BF_PRE  16
#define BF_PIM  18
#define BF_QRE  20
#define BF_QIM  22
#define BF_HALF 24
#define BF_SCRATCH_BYTES 26
_Static_assert(BF_SCRATCH_BYTES <= DSP_SCRATCH_FFT_OUTER_COUNT,
               "butterfly scratch must not reach the FFT block counter");
_Static_assert(DSP_SCRATCH_FFT_OUTER_COUNT < DSP_SCRATCH_BYTES,
               "FFT block counter must lie inside the reserved DSP scratch region");


/* Host-side canonical twiddle, Q15. Shared with the table emitter and with
 * the tests' oracle so both cannot drift. */
void dsp_twiddle_q15(int k, int16_t *wr, int16_t *wi) {
    double a = -2.0 * M_PI * (double)k / (double)DSP_FFT_SIZE;
    long r = lround(cos(a) * 32767.0), i = lround(sin(a) * 32767.0);
    *wr = (int16_t)(r > 32767 ? 32767 : (r < -32768 ? -32768 : r));
    *wi = (int16_t)(i > 32767 ? 32767 : (i < -32768 ? -32768 : i));
}

/* Emits the 32-entry canonical table as program-memory data. Callers must
 * place this where control flow cannot reach it (after a terminating break);
 * the interpreter refuses to execute a data word, and a test asserts the
 * placement. */
void dsp_emit_twiddle_table(InstrBuf *buf) {
    ins1(buf, AVR_LABEL_MNEMONIC, DSP_TWIDDLE_LABEL);
    for (int k = 0; k < DSP_TWIDDLE_ENTRIES; k++) {
        int16_t wr, wi;
        dsp_twiddle_q15(k, &wr, &wi);
        ins_data_word(buf, (uint16_t)wr);
        ins_data_word(buf, (uint16_t)wi);
    }
}

/* One butterfly: twiddle from Z, p and q from X, out[p] and out[q] to Y.
 * `z_step` is applied after the four lpm Z+ loads; `to_q` after reading or
 * writing p, `to_next_p` after reading or writing q. Stage 0 passes 0/0,
 * since its q directly follows p and its next p directly follows q. */
static void emit_butterfly(InstrBuf *buf, uint16_t sc, int z_step, int to_q, int to_next_p) {
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWRE));
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWRE + 1));
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWIM));
    ptr_byte_to_scratch(buf, 'Z', (uint16_t)(sc + BF_TWIM + 1));
    ptr_add(buf, 30, z_step);

    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PRE));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PRE + 1));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PIM));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_PIM + 1));
    ptr_add(buf, 26, to_q);
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QRE));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QRE + 1));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QIM));
    ptr_byte_to_scratch(buf, 'X', (uint16_t)(sc + BF_QIM + 1));
    ptr_add(buf, 26, to_next_p);

    /* t = W * q */
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWRE), (uint16_t)(sc + BF_QRE), (uint16_t)(sc + BF_AC));
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWIM), (uint16_t)(sc + BF_QIM), (uint16_t)(sc + BF_BD));
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWRE), (uint16_t)(sc + BF_QIM), (uint16_t)(sc + BF_AD));
    lower_fixed_mul_q15(buf, (uint16_t)(sc + BF_TWIM), (uint16_t)(sc + BF_QRE), (uint16_t)(sc + BF_BC));
    lower_sub16(buf, (uint16_t)(sc + BF_AC), (uint16_t)(sc + BF_BD), (uint16_t)(sc + BF_TRE));
    lower_add16(buf, (uint16_t)(sc + BF_AD), (uint16_t)(sc + BF_BC), (uint16_t)(sc + BF_TIM));

    /* halve each operand, then combine */
    lower_asr16(buf, (uint16_t)(sc + BF_PRE), (uint16_t)(sc + BF_HALF));
    lower_asr16(buf, (uint16_t)(sc + BF_TRE), (uint16_t)(sc + BF_TRE));
    lower_add16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TRE), (uint16_t)(sc + BF_AC));
    lower_sub16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TRE), (uint16_t)(sc + BF_BD));
    lower_asr16(buf, (uint16_t)(sc + BF_PIM), (uint16_t)(sc + BF_HALF));
    lower_asr16(buf, (uint16_t)(sc + BF_TIM), (uint16_t)(sc + BF_TIM));
    lower_add16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TIM), (uint16_t)(sc + BF_AD));
    lower_sub16(buf, (uint16_t)(sc + BF_HALF), (uint16_t)(sc + BF_TIM), (uint16_t)(sc + BF_BC));

    /* out[p] then out[q] */
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AC), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AC + 1), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AD), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_AD + 1), 'Y');
    ptr_add(buf, 28, to_q);
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BD), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BD + 1), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BC), 'Y');
    scratch_byte_to_ptr(buf, (uint16_t)(sc + BF_BC + 1), 'Y');
    ptr_add(buf, 28, to_next_p);
}

static void load_twiddle_base(InstrBuf *buf) {
    char zl[AVR_OPERAND_LEN], zh[AVR_OPERAND_LEN], sym[AVR_OPERAND_LEN];
    fmt_reg(zl, 30); fmt_reg(zh, 31);
    fmt_lo8_sym(sym, DSP_TWIDDLE_LABEL, 0); ins2(buf, "ldi", zl, sym);
    fmt_hi8_sym(sym, DSP_TWIDDLE_LABEL, 0); ins2(buf, "ldi", zh, sym);
}

void lower_fft_butterfly_stage(InstrBuf *buf, const IrGraph *graph,
                                       const SramLayout *layout, size_t op_id, int stage) {
    const IrOp *op = &graph->ops[op_id];
    size_t in_id = op->inputs[0];
    uint16_t in_addr = sram_layout_addr(layout, graph, in_id, 0);
    uint16_t out_addr = sram_layout_addr(layout, graph, op_id, 0);
    uint16_t sc = layout->dsp_scratch_addr;

    OPTIFINE_INVARIANT(stage >= 0 && stage < DSP_FFT_LOG2);
    int half = 1 << stage;
    int blocks = DSP_FFT_SIZE / (half * 2);
    int span = half * 4; /* bytes from p to q */

    load_ptr_imm(buf, 26, in_addr);   /* X */
    load_ptr_imm(buf, 28, out_addr);  /* Y */

    if (half == 1) {
        /* Stage 0: W^0 throughout, so Z rewinds over the four bytes it just
         * read, and X/Y run straight through the buffers. */
        load_twiddle_base(buf);
        LoopCtx loop;
        instrbuf_loop_begin(buf, &loop, (uint32_t)blocks, REG_DSP_LOOP_COUNTER);
        emit_butterfly(buf, sc, -4, 0, 0);
        instrbuf_loop_end(buf, &loop);
        return;
    }

    LoopCtx block_loop;
    if (blocks > 1) {
        instrbuf_loop_begin_sram(buf, &block_loop, (uint32_t)blocks,
                                 (uint16_t)(sc + DSP_SCRATCH_FFT_OUTER_COUNT));
    }
    load_twiddle_base(buf);
    LoopCtx bf_loop;
    instrbuf_loop_begin(buf, &bf_loop, (uint32_t)half, REG_DSP_LOOP_COUNTER);
    emit_butterfly(buf, sc, blocks * 4 - 4, span - 4, -span);
    instrbuf_loop_end(buf, &bf_loop);
    if (blocks > 1) {
        ptr_add(buf, 26, span);
        ptr_add(buf, 28, span);
        instrbuf_loop_end(buf, &block_loop);
    }
}

/* Which OP_FFT_BUTTERFLY this op is, by position among the graph's others. */
int dsp_butterfly_stage_index(const IrGraph *graph, size_t op_id) {
    int stage = 0;
    for (size_t i = 0; i < op_id; i++) {
        if (graph->ops[i].kind == OP_FFT_BUTTERFLY) stage++;
    }
    return stage;
}

/* Test/fixture seam: stage 0 as a self-contained program -- the butterfly
 * loop, the terminating break, then the twiddle table. The table must share a
 * candidate with the code that references it so lo8/hi8(.Ltw) resolves, and
 * it must follow the break so control flow cannot execute it. */
int lower_fft_stage0_test_hook(const IrGraph *graph, size_t op_id,
                                const SramLayout *layout, const CostModel *cost_model,
                                Candidate *out) {
    InstrBuf buf;
    instrbuf_init(&buf);
    lower_fft_butterfly_stage(&buf, graph, layout, op_id, 0);
    ins0(&buf, "break");
    dsp_emit_twiddle_table(&buf);
    return instrbuf_price(&buf, cost_model, out);
}

/* Test/fixture seam: FFT stages first..last as one self-contained program,
 * laid out exactly like the stage 0 seam. Each stage reads the previous
 * stage's buffer, so a run leaves every intermediate stage output in SRAM for
 * the tests to check one by one. */
int lower_fft_stages_test_hook(const IrGraph *graph, int first_stage, int last_stage,
                               const SramLayout *layout, const CostModel *cost_model,
                               Candidate *out) {
    if (first_stage < 0 || last_stage >= DSP_FFT_LOG2 || first_stage > last_stage) {
        fprintf(stderr, "lower: bad FFT stage range %d..%d\n", first_stage, last_stage);
        return -1;
    }
    InstrBuf buf;
    instrbuf_init(&buf);
    int stage = 0;
    for (size_t i = 0; i < graph->count; i++) {
        if (graph->ops[i].kind != OP_FFT_BUTTERFLY) continue;
        if (stage >= first_stage && stage <= last_stage) {
            lower_fft_butterfly_stage(&buf, graph, layout, i, stage);
        }
        stage++;
    }
    ins0(&buf, "break");
    dsp_emit_twiddle_table(&buf);
    return instrbuf_price(&buf, cost_model, out);
}

/* The FFT's program-memory constant data: the canonical twiddle table, once,
 * if the graph has any butterfly stage; nothing otherwise. */
void lower_fft_constant_data(InstrBuf *buf, const IrGraph *graph) {
    for (size_t i = 0; i < graph->count; i++) {
        if (graph->ops[i].kind == OP_FFT_BUTTERFLY) {
            dsp_emit_twiddle_table(buf);
            return;
        }
    }
}
