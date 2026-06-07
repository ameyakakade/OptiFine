#include "optifine/codegen/periodic.h"

#include <stdarg.h>
#include <string.h>

enum {
    PERIODIC_SCHEDULER_BYTES = 4,
    PERIODIC_TERMINAL_OUTPUT_BYTES = 4,
};

static int emitf(FILE *out, const char *format, ...) {
    va_list args;
    va_start(args, format);
    int written = vfprintf(out, format, args);
    va_end(args);
    return written < 0 ? -1 : 0;
}

/* Returns the exact ATmega128 Timer0 CS02:0 encoding for the supported
 * asynchronous-clock divisors. This helper is kept here so the Task 3 AVR
 * wrapper emits the same encoding that validation accepts. */
static int timer0_clock_select_bits(uint16_t prescaler, uint8_t *out_bits) {
    switch (prescaler) {
        case 8:
            *out_bits = 2;
            return 0;
        case 32:
            *out_bits = 3;
            return 0;
        case 128:
            *out_bits = 5;
            return 0;
        case 1024:
            *out_bits = 7;
            return 0;
        default:
            return -1;
    }
}

int periodic_options_validate(const PeriodicOptions *options) {
    if (options == NULL ||
        (options->policy != WAIT_ACTIVE && options->policy != WAIT_POWER_SAVE) ||
        options->inference_count == 0) {
        return -1;
    }

    uint8_t timer_bits;
    return timer0_clock_select_bits(options->timer_prescaler, &timer_bits);
}

static int periodic_output_addr(const IrGraph *graph, const SramLayout *layout,
                                uint16_t *out_addr) {
    if (graph == NULL || graph->ops == NULL || graph->count == 0 ||
        layout->op_addr == NULL || layout->count < graph->count) {
        return -1;
    }

    size_t output_op_id = graph->count - 1;
    const IrOp *output_op = &graph->ops[output_op_id];
    if (output_op->kind != OP_OUTPUT || output_op->dtype != DT_INT8) {
        return -1;
    }

    size_t output_elements = 1;
    for (size_t i = 0; i < output_op->output_shape_len; i++) {
        if (output_op->output_shape == NULL || output_op->output_shape[i] == 0 ||
            output_elements > SIZE_MAX / output_op->output_shape[i]) {
            return -1;
        }
        output_elements *= output_op->output_shape[i];
    }
    if (output_elements != PERIODIC_TERMINAL_OUTPUT_BYTES) {
        return -1;
    }

    uint32_t address = layout->op_addr[output_op_id];
    uint32_t scheduler_start = (uint32_t)SRAM_LAYOUT_BASE + layout->bytes_used;
    if (address < SRAM_LAYOUT_BASE ||
        address + PERIODIC_TERMINAL_OUTPUT_BYTES > scheduler_start) {
        return -1;
    }

    *out_addr = (uint16_t)address;
    return 0;
}

int codegen_emit_periodic_program(const IrGraph *graph, const SramLayout *layout,
                                  const RegAllocResult *regalloc, const CostModel *cost_model,
                                  const int8_t *demo_input, size_t demo_input_len,
                                  const PeriodicOptions *options,
                                  FILE *out, PeriodicProgramCost *out_cost) {
    if (out_cost == NULL) {
        return -1;
    }
    memset(out_cost, 0, sizeof(*out_cost));

    if (layout == NULL || periodic_options_validate(options) != 0) {
        return -1;
    }

    uint32_t scheduler_start = (uint32_t)SRAM_LAYOUT_BASE + layout->bytes_used;
    if (scheduler_start + PERIODIC_SCHEDULER_BYTES > SRAM_LAYOUT_LIMIT) {
        return -1;
    }

    out_cost->tick_addr = (uint16_t)scheduler_start;
    out_cost->running_addr = (uint16_t)(scheduler_start + 1);
    out_cost->overrun_addr = (uint16_t)(scheduler_start + 2);
    out_cost->completed_addr = (uint16_t)(scheduler_start + 3);

    /* Preserve Task 2's allocation-query behavior for callers that only need
     * the scheduler addresses and intentionally provide no output stream. */
    if (out == NULL) {
        return 0;
    }

    if (regalloc == NULL || cost_model == NULL) {
        return -1;
    }

    uint16_t output_addr;
    uint8_t timer_bits;
    if (periodic_output_addr(graph, layout, &output_addr) != 0 ||
        timer0_clock_select_bits(options->timer_prescaler, &timer_bits) != 0) {
        return -1;
    }

    if (emitf(out,
              "#include <avr/io.h>\n\n"
              ".arch atmega128\n"
              ".section .text\n"
              ".global _start\n\n"
              "_start:\n"
              ".org 0x0000\n"
              "    jmp reset\n"
              ".org (TIMER0_OVF_vect_num * 4)\n"
              "    jmp timer0_ovf_isr\n\n"
              "reset:\n"
              "    cli\n"
              "    ldi r16, lo8(RAMEND)\n"
              "    out _SFR_IO_ADDR(SPL), r16\n"
              "    ldi r16, hi8(RAMEND)\n"
              "    out _SFR_IO_ADDR(SPH), r16\n"
              "    clr r16\n"
              "    sts 0x%04X, r16\n"
              "    sts 0x%04X, r16\n"
              "    sts 0x%04X, r16\n"
              "    sts 0x%04X, r16\n\n"
              "; INITIALIZATION BEGIN\n",
              (unsigned)out_cost->tick_addr,
              (unsigned)out_cost->running_addr,
              (unsigned)out_cost->overrun_addr,
              (unsigned)out_cost->completed_addr) != 0) {
        return -1;
    }

    if (codegen_emit_initialization(graph, layout, regalloc, cost_model,
                                    demo_input, demo_input_len,
                                    options->use_real_candidates,
                                    out, &out_cost->initialization) != 0) {
        return -1;
    }

    if (emitf(out,
              "; INITIALIZATION END\n\n"
              "    ldi r16, (1 << AS0)\n"
              "    out _SFR_IO_ADDR(ASSR), r16\n"
              "    ldi r16, 0x%02X\n"
              "    out _SFR_IO_ADDR(TCCR0), r16\n"
              "wait_timer0_busy:\n"
              "    in r16, _SFR_IO_ADDR(ASSR)\n"
              "    andi r16, (1 << TCR0UB) | (1 << OCR0UB) | (1 << TCN0UB)\n"
              "    brne wait_timer0_busy\n"
              "    ldi r16, (1 << TOIE0)\n"
              "    out _SFR_IO_ADDR(TIMSK), r16\n"
              "    sei\n\n"
              "wait_for_tick:\n"
              "    cli\n"
              "    lds r16, 0x%04X\n"
              "    tst r16\n"
              "    brne tick_ready\n",
              (unsigned)timer_bits,
              (unsigned)out_cost->tick_addr) != 0) {
        return -1;
    }

    if (options->policy == WAIT_ACTIVE) {
        if (emitf(out,
                  "    sei\n"
                  "    rjmp wait_for_tick\n") != 0) {
            return -1;
        }
    } else {
        /* AVR services an interrupt only after the instruction following
         * sei, so the adjacent sleep closes the flag-test/wait race. */
        if (emitf(out,
                  "    ldi r16, (1 << SE) | (1 << SM1) | (1 << SM0)\n"
                  "    out _SFR_IO_ADDR(MCUCR), r16\n"
                  "    sei\n"
                  "    sleep\n"
                  "    rjmp wait_for_tick\n") != 0) {
            return -1;
        }
    }

    if (emitf(out,
              "\n"
              "tick_ready:\n"
              "    clr r16\n"
              "    sts 0x%04X, r16\n"
              "    out _SFR_IO_ADDR(MCUCR), r16\n"
              "    ldi r16, 1\n"
              "    sts 0x%04X, r16\n"
              "    sei\n"
              "; BODY BEGIN\n",
              (unsigned)out_cost->tick_addr,
              (unsigned)out_cost->running_addr) != 0) {
        return -1;
    }

    if (codegen_emit_inference_body(graph, layout, regalloc, cost_model,
                                    demo_input, demo_input_len,
                                    options->use_real_candidates,
                                    out, &out_cost->inference) != 0) {
        return -1;
    }

    if (emitf(out,
              "; BODY END\n"
              "    cli\n"
              "    clr r16\n"
              "    sts 0x%04X, r16\n"
              "    lds r16, 0x%04X\n"
              "    inc r16\n"
              "    sts 0x%04X, r16\n"
              "    lds r16, 0x%04X\n"
              "    tst r16\n"
              "    brne periodic_done\n"
              "    lds r16, 0x%04X\n"
              "    cpi r16, %u\n"
              "    breq periodic_done\n"
              "    sei\n"
              "    jmp wait_for_tick\n\n"
              "periodic_done:\n"
              "    cli\n"
              "; Terminal metadata is compiler-side structural evidence;\n"
              "; no simulator SRAM readback is claimed.\n"
              "; META completed_count_addr=0x%04X\n"
              "; META overrun_addr=0x%04X\n"
              "; META output_addr=0x%04X output_len=4\n"
              "    lds r16, 0x%04X\n"
              "    sts 0x%04X, r16\n"
              "    lds r16, 0x%04X\n"
              "    sts 0x%04X, r16\n",
              (unsigned)out_cost->running_addr,
              (unsigned)out_cost->completed_addr,
              (unsigned)out_cost->completed_addr,
              (unsigned)out_cost->overrun_addr,
              (unsigned)out_cost->completed_addr,
              (unsigned)options->inference_count,
              (unsigned)out_cost->completed_addr,
              (unsigned)out_cost->overrun_addr,
              (unsigned)output_addr,
              (unsigned)out_cost->completed_addr,
              (unsigned)out_cost->completed_addr,
              (unsigned)out_cost->overrun_addr,
              (unsigned)out_cost->overrun_addr) != 0) {
        return -1;
    }

    for (unsigned i = 0; i < PERIODIC_TERMINAL_OUTPUT_BYTES; i++) {
        unsigned address = (unsigned)output_addr + i;
        if (emitf(out,
                  "    lds r16, 0x%04X\n"
                  "    sts 0x%04X, r16\n",
                  address, address) != 0) {
            return -1;
        }
    }

    if (emitf(out,
              "    break\n\n"
              "timer0_ovf_isr:\n"
              "    push r16\n"
              "    in r16, _SFR_IO_ADDR(SREG)\n"
              "    push r16\n"
              "    lds r16, 0x%04X\n"
              "    tst r16\n"
              "    breq timer0_no_overrun\n"
              "    ldi r16, 1\n"
              "    sts 0x%04X, r16\n"
              "timer0_no_overrun:\n"
              "    ldi r16, 1\n"
              "    sts 0x%04X, r16\n"
              "    pop r16\n"
              "    out _SFR_IO_ADDR(SREG), r16\n"
              "    pop r16\n"
              "    reti\n",
              (unsigned)out_cost->running_addr,
              (unsigned)out_cost->overrun_addr,
              (unsigned)out_cost->tick_addr) != 0) {
        return -1;
    }

    return 0;
}
