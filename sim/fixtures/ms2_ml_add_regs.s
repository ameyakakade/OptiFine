; Milestone 2: minimal end-to-end path -- ML `Add` candidate 1 (register-resident).
; Both operands already live in registers (as if produced by an immediately
; prior op in the same basic block); compute c = a + b with a single direct
; register-register add. No SRAM traffic for the add itself.
; Expected final state: r16 == 0x30 (48).

.arch atmega128

.section .text
.global _start

_start:
    ldi r16, 0x0F       ; a = 15 (operand setup, register-resident)
    ldi r17, 0x21       ; b = 33 (operand setup, register-resident)
    add r16, r17        ; c = a + b = 48 (0x30), direct register add
    break               ; halt simulation
