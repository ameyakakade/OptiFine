; Milestone 2: minimal end-to-end path -- DSP `Window` candidate 2
; (SRAM-resident coefficient). Same tap as ms2_dsp_window_regs.s, but the
; window coefficient is reloaded from SRAM immediately before each use (as
; if the window table lived in SRAM rather than staying resident across the
; pass) -- same result, deliberately SRAM-heavy.
; Expected final state: r1:r0 == fmuls(0x40, 0x60), same product as the
; register-resident candidate; mem[0x0102] == 0x60 (the coefficient).

.arch atmega128

.section .text
.global _start

_start:
    ldi r16, 0x40       ; sample
    ldi r18, 0x60       ; coefficient value to seed the SRAM window table
    sts 0x0102, r18     ; window table entry lives in SRAM
    lds r17, 0x0102     ; reload coefficient from SRAM before use
    fmuls r16, r17      ; r1:r0 = sample * coeff
    break               ; halt simulation
