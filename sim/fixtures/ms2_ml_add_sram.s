; Milestone 2: minimal end-to-end path -- ML `Add` candidate 2 (SRAM-resident).
; Same logical op (c = a + b) as ms2_ml_add_regs.s, but both operands are
; spilled to SRAM first (as if a register allocator had to spill them) and
; reloaded before the add -- same result, deliberately SRAM-heavy, to
; isolate the register-vs-SRAM cost delta for a single Add op.
; Expected final state: r16 == 0x30 (48), mem[0x0100] == 0x0F, mem[0x0101] == 0x21.

.arch atmega128

.section .text
.global _start

_start:
    ldi r16, 0x0F       ; a = 15
    ldi r17, 0x21       ; b = 33
    sts 0x0100, r16     ; spill a to SRAM
    sts 0x0101, r17     ; spill b to SRAM
    lds r16, 0x0100     ; reload a
    lds r17, 0x0101     ; reload b
    add r16, r17        ; c = a + b = 48 (0x30)
    break               ; halt simulation
