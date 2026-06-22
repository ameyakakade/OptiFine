; Phase B spike 3: empirically verify Avrora simulates Power-save mode +
; asynchronous Timer0 (driven by an external 32.768kHz crystal via ASSR,
; decoupled from the system clock) -- a real, standard AVR low-power
; pattern for periodic wake from deep sleep, and the only viable one for
; this project: spike 2 proved Avrora has no Watchdog Timer at all, and
; there's no general external-interrupt-injection mechanism in this
; simulator either (see the plan file for the full research trail).
;
; Correction vs. general AVR knowledge: on ATmega128 specifically (unlike
; e.g. ATmega328P), it's TIMER0 that has async capability via ASSR's AS0
; bit (TCN0UB/OCR0UB/TCR0UB busy flags) -- confirmed via avr-libc's
; iom128.h, not assumed from memory. This reuses spike_sleep_isr.S's
; Timer0/TOIE0/vector-16 setup almost entirely; only the ASSR async
; enable (+ the real hardware requirement to wait for the busy flags to
; clear before relying on the new clock source) and the MCUCR sleep-mode
; value (Power-save's SM2:0=011 instead of Idle's 000) are new.
;
; Success criteria: the energy report should show real Power-save time
; (not Active, not Idle), and the simulation should actually complete
; (reach `break`) rather than hang like spike 2 did -- proving Avrora
; models the asynchronous clock domain, not just the ASSR register's
; bit storage.

#include <avr/io.h>

.section .text
.global _start

.org 0x0000
    jmp reset

.org (TIMER0_OVF_vect_num * 4)
    jmp timer0_ovf_isr

_start:
reset:
    ldi r16, lo8(RAMEND)
    out _SFR_IO_ADDR(SPL), r16
    ldi r16, hi8(RAMEND)
    out _SFR_IO_ADDR(SPH), r16

    clr r17                       ; wake counter, mem[0x0200]
    sts 0x0200, r17

    ldi r16, (1 << AS0)           ; Timer0 clocked from the external async crystal, not the system clock
    out _SFR_IO_ADDR(ASSR), r16

    ldi r16, (1 << CS00)          ; Timer0: no further prescaling of the 32kHz async clock
    out _SFR_IO_ADDR(TCCR0), r16

wait_busy:                        ; real hardware requirement: TCCR0/TCNT0/OCR0 writes in async mode
    in r16, _SFR_IO_ADDR(ASSR)    ; take up to ~2 external clock cycles to actually propagate --
    andi r16, (1 << TCR0UB)       ; the busy flags say when it's safe to rely on the new config.
    brne wait_busy

    ldi r16, (1 << TOIE0)
    out _SFR_IO_ADDR(TIMSK), r16
    sei

sleep_loop:
    ldi r16, (1 << SE) | (1 << SM1) | (1 << SM0)   ; Power-save (SM2:0=011) + Sleep Enable
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
