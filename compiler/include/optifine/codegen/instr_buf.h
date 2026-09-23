/* Shared instruction-building and pricing utilities, factored out of
 * lower.c so candidates.c can build genuinely different instruction
 * sequences for the same op without duplicating this plumbing. Pure
 * mechanical extraction -- no arithmetic/lowering logic lives here. */
#ifndef OPTIFINE_CODEGEN_INSTR_BUF_H
#define OPTIFINE_CODEGEN_INSTR_BUF_H

#include <stdint.h>

#include "optifine/codegen/candidates.h"
#include "optifine/cost_model.h"

/* A statically bounded counted loop: instructions [first,last] execute
 * `trip` times. Regions may nest; a nested region's instructions execute
 * the product of the enclosing trip counts. */
typedef struct {
    size_t first;
    size_t last;
    uint32_t trip;
    int is_long; /* closed as `breq exit / rjmp head` rather than `brne head` */
} LoopRegion;

#define INSTRBUF_MAX_LOOPS 32

typedef struct {
    AvrInstr *items;
    size_t count;
    size_t capacity;
    LoopRegion loops[INSTRBUF_MAX_LOOPS];
    size_t num_loops;
} InstrBuf;

/* Open counted-loop handle. Opaque to callers apart from being storage. */
typedef struct {
    size_t body_first;   /* index of the first body instruction */
    size_t region;       /* slot reserved in InstrBuf::loops */
    uint32_t trip;
    int counter_reg;       /* -1 when the counter lives in SRAM */
    uint16_t counter_addr; /* SRAM counter cell, meaningful only when counter_reg == -1 */
    char label[AVR_OPERAND_LEN];
} LoopCtx;

void instrbuf_init(InstrBuf *b);
void instrbuf_push(InstrBuf *b, const char *mnemonic, int num_operands,
                    const char *o1, const char *o2, const char *o3);
void ins1(InstrBuf *b, const char *m, const char *o1);
void ins2(InstrBuf *b, const char *m, const char *o1, const char *o2);

void ins0(InstrBuf *b, const char *m);

void fmt_reg(char *out, int r);
void fmt_addr(char *out, uint16_t addr);
void fmt_imm(char *out, uint8_t v);
/* Pointer operands: "X+", "Z+", "Z+6" and the like, plus lo8()/hi8() of a
 * 16-bit address for loading a pointer pair with ldi. */
void fmt_ptr(char *out, char reg, int post_increment);
void fmt_ptr_disp(char *out, char reg, int displacement);
void fmt_lo8(char *out, uint16_t addr);
void fmt_hi8(char *out, uint16_t addr);

/* --- Statically bounded counted loops ---
 *
 * The DSP path cannot be lowered as straight-line code: fully unrolling the
 * 64-point pipeline needs ~497 KB against the ATmega128's 128 KB of flash
 * (measured, not estimated -- see the milestone 6 pre-flight). These emit a
 * counted loop instead, so the body is emitted once and executed `trip`
 * times.
 *
 * `instrbuf_loop_begin` emits `ldi <counter>, trip` and a label;
 * `instrbuf_loop_end` emits `dec <counter>` and `brne <label>`, and records
 * the body as a loop region so pricing reflects what actually *executes*
 * rather than what is emitted. `Candidate::num_instructions` stays the
 * emitted count (that is what occupies flash) while `cycles`/`energy_nj`
 * become the executed totals.
 *
 * Deliberately not a general control-flow facility: trip counts are
 * compile-time constants, loops are counted and single-entry/single-exit,
 * and there is no branching of any other kind. */
void instrbuf_loop_begin(InstrBuf *b, LoopCtx *ctx, uint32_t trip, int counter_reg);
void instrbuf_loop_end(InstrBuf *b, LoopCtx *ctx);

/* The same counted loop with its counter held in one SRAM byte instead of a
 * register, for an outer loop whose body already owns every register -- the
 * FFT's per-block loop around the butterfly loop, which holds
 * REG_DSP_LOOP_COUNTER. Opens with `ldi r24, trip / sts cell, r24` and closes
 * with `lds r24, cell / dec r24 / sts cell, r24` before the usual branch, so
 * REG_SCRATCH0 must be dead at both ends (it is at every block boundary).
 * The trip count is still the ldi immediate, so the priced trip and the
 * executed one cannot disagree. */
void instrbuf_loop_begin_sram(InstrBuf *b, LoopCtx *ctx, uint32_t trip, uint16_t counter_addr);

/* True for the label pseudo-instruction, which emits `name:` rather than an
 * opcode and costs nothing. */
int avr_instr_is_label(const AvrInstr *instr);
#define AVR_LABEL_MNEMONIC ".L"

/* --- Program-memory constant data ---
 *
 * `.dw` emits one 16-bit word of constant data into the instruction stream.
 * It occupies two bytes of flash and costs no cycles, and exists so the DSP
 * path can bake its canonical twiddle table into program memory and read it
 * with `lpm` instead of materialising each constant with ldi/sts.
 *
 * Placement is the caller's responsibility and it matters: raw data sitting in
 * .text disassembles as instructions, so a table must go where control flow
 * cannot reach it -- after the program's terminating `break`. A dedicated
 * `.progmem` section is NOT an option here: under this project's
 * `-nostartfiles` link no `.progmem*` output section exists, so such a section
 * is silently dropped and its label resolves to 0x0000, which would make `lpm`
 * read instruction bytes as data. Verified empirically, not assumed. */
int avr_instr_is_data_word(const AvrInstr *instr);
/* Flash footprint of one emitted entry, matching the real AVR encoding. */
size_t avr_instr_flash_bytes(const AvrInstr *instr);
#define AVR_DATA_WORD_MNEMONIC ".dw"
void ins_data_word(InstrBuf *b, uint16_t value);

/* `lo8(sym+off)` / `hi8(sym+off)`: the low and high byte of a label's BYTE
 * address, which is what Z must hold for `lpm`. Deliberately not pm_lo8/
 * pm_hi8, which yield the WORD address and would read the wrong half of the
 * table -- confirmed against avr-as, where a label at byte 0x1E gives
 * lo8=0x1E but pm_lo8=0x0F. */
void fmt_lo8_sym(char *out, const char *symbol, int offset);
void fmt_hi8_sym(char *out, const char *symbol, int offset);

/* Converts a finished InstrBuf into a priced Candidate, taking ownership of
 * buf->items. Returns 0 on success, -1 (with an error printed) if any
 * instruction has no cost-category mapping or no cost_table.toml entry.
 * Every cost_table.toml entry is cycles x a single per-cycle constant (see
 * SOURCES.md's "Per-cycle energy constant" section, spec v2 section 13);
 * `cycles` is derived from energy rather than tracked separately, so the
 * 2.8375 constant here MUST be kept in sync with cost_table.toml's actual
 * per-cycle value. */
int instrbuf_price(InstrBuf *buf, const CostModel *cost_model, Candidate *out);

#endif /* OPTIFINE_CODEGEN_INSTR_BUF_H */
