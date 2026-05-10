; Milestone 2: minimal end-to-end path -- DSP `Window` candidate 1
; (register-resident). One tap of an elementwise Q15 windowing multiply,
; sample * coefficient, using AVR's signed fractional multiply. The
; coefficient is already register-resident (as if loaded once and reused
; across a full window pass); no SRAM traffic for the multiply itself.
; Expected final state: r1:r0 == fmuls(0x40, 0x60) (signed fractional product).

.arch atmega128

.section .text
.global _start

_start:
    ldi r16, 0x40       ; sample (Q15-style operand, register-resident)
    ldi r17, 0x60       ; window coefficient, register-resident
    fmuls r16, r17      ; r1:r0 = sample * coeff (signed fractional multiply)
    break               ; halt simulation
