; Phase B spike: empirically verify Avrora actually simulates AVR interrupts
; and the sleep instruction before any of this gets automated by codegen.
; Uppercase .S so avr-gcc preprocesses it, letting us use avr-libc's real
; iom128.h register/vector definitions instead of hand-derived hex (which
; would have to independently match both real hardware AND Avrora's model
; of it -- avr-gcc's own header is the authoritative source for the
; former, and this spike is exactly how we check the latter).
;
; Behavior: initializes the stack (required -- -nostartfiles means nothing
; else does this, and interrupt entry/reti needs a valid stack), configures
; Timer0 for a fast overflow (clk/1, ~256 cycles/overflow), enables its
; overflow interrupt, then repeatedly executes `sleep` in Idle mode. The
; ISR increments a counter in SRAM and, once it has fired 3 times, halts
; the simulation with `break`.
;
; Success criteria (checked by hand against the Avrora output, not
; automated yet): the energy report should show non-zero Idle-mode time
; (not everything counted as Active), and mem[0x0200] should read 3 if we
; could inspect final memory state (Avrora's energy monitor doesn't show
; that, so this is a bring-up sanity check on the energy split only).

#include <avr/io.h>

.section .text
.global _start

.org 0x0000
    jmp reset

; ATmega128 uses 2-word (4-byte) JMP vectors, not 1-word RJMP, because its
; flash exceeds 8K (confirmed via iom128.h's _VECTORS_SIZE=140 bytes / 35
; vectors = 4 bytes/vector -- not assumed).
.org (TIMER0_OVF_vect_num * 4)
    jmp timer0_ovf_isr

; iom128.h's register macros (e.g. SPL = _SFR_IO8(0x3D)) expand to the
; memory-mapped address (io_addr + 0x20), correct for `lds`/`sts` but not
; for `in`/`out`, which need the raw 6-bit I/O address -- _SFR_IO_ADDR()
; converts back. Omitting this was the first assembler error here
; ("operand out of range: 93" -- 93 = 0x5D = SPL's memory address, not a
; valid `out` operand).
_start:
reset:
    ldi r16, lo8(RAMEND)
    out _SFR_IO_ADDR(SPL), r16
    ldi r16, hi8(RAMEND)
    out _SFR_IO_ADDR(SPH), r16

    clr r17                    ; wake counter, mem[0x0200]
    sts 0x0200, r17

    ldi r16, (1 << CS00)       ; Timer0: no prescaling (clk/1) -- fast overflow for a short spike
    out _SFR_IO_ADDR(TCCR0), r16
    ldi r16, (1 << TOIE0)
    out _SFR_IO_ADDR(TIMSK), r16
    sei

sleep_loop:
    ldi r16, (1 << SE)         ; Idle mode (SM2:0 = 000) + Sleep Enable
    out _SFR_IO_ADDR(MCUCR), r16
    sleep
    rjmp sleep_loop

timer0_ovf_isr:
    push r16
    lds r16, 0x0200
    inc r16
    sts 0x0200, r16
    cpi r16, 3
    breq halt
    pop r16
    reti

halt:
    break
