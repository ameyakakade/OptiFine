; Milestone 1 toolchain bring-up smoke test.
; Loads two constants, adds them, stores to SRAM, loads back,
; then executes `break` which stops Avrora's simulation.
; Expected final state: r16 == r18 == 0x30, mem[0x0100] == 0x30.

.arch atmega128

.section .text
.global _start

_start:
    ldi r16, 0x0F       ; 15
    ldi r17, 0x21       ; 33
    add r16, r17        ; r16 = 48 (0x30)
    sts 0x0100, r16     ; store result to SRAM
    lds r18, 0x0100     ; load it back
    break               ; halt simulation
