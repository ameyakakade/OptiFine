; Phase B spike 2: empirically verify Avrora simulates Power-down mode +
; watchdog-timer-triggered reset correctly. ATmega128's watchdog can only
; reset the chip on timeout (iom128.h has no WDIE/WDIF -- interrupt-mode
; watchdogs came later in the AVR line), so "waking" from Power-down here
; means the whole chip resets and re-enters `reset:`, not a clean ISR
; resume (see spike_sleep_isr.S for the Idle+Timer0-ISR alternative, which
; only gives ~2.26x active/idle current savings vs. Power-down's ~65x).
;
; SRAM content survives a watchdog reset (only registers/PC reset, per the
; datasheet), so a wake counter still works across resets. Behavior: each
; boot (real power-on OR watchdog-triggered) increments mem[0x0200], and
; after 3 total boots, halts instead of re-arming the watchdog.
;
; Success criteria (checked by hand against Avrora's output): the energy
; report should show significant Power Down time (not Active), and the
; simulation should actually complete (reach `break`) rather than looping
; forever or crashing -- proving Avrora both fires the watchdog reset and
; correctly re-executes from the reset vector afterward.

#include <avr/io.h>

.section .text
.global _start

.org 0x0000
    jmp reset

_start:
reset:
    ldi r16, lo8(RAMEND)
    out _SFR_IO_ADDR(SPL), r16
    ldi r16, hi8(RAMEND)
    out _SFR_IO_ADDR(SPH), r16

    lds r17, 0x0200             ; wake counter -- persists across watchdog resets (SRAM survives)
    inc r17
    sts 0x0200, r17
    cpi r17, 3
    breq halt

    ; Arm the watchdog: documented two-write safety sequence (WDCE+WDE
    ; together, then the real config within 4 cycles) -- required by the
    ; datasheet for any change to WDE/prescaler, not optional.
    ldi r16, (1 << WDCE) | (1 << WDE)
    out _SFR_IO_ADDR(WDTCR), r16
    ldi r16, (1 << WDE) | (0 << WDP2) | (0 << WDP1) | (0 << WDP0)  ; shortest timeout
    out _SFR_IO_ADDR(WDTCR), r16

    ldi r16, (1 << SE) | (1 << SM1)   ; Power-down (SM2:0=010) + Sleep Enable
    out _SFR_IO_ADDR(MCUCR), r16
    sleep
    rjmp .                       ; should never execute -- watchdog reset happens first

halt:
    break
