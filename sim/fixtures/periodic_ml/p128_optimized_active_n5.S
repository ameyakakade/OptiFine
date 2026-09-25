#include <avr/io.h>

.arch atmega128
.section .text
.global _start

_start:
.org 0x0000
    jmp reset
.org (TIMER0_OVF_vect_num * 4)
    jmp timer0_ovf_isr

reset:
    cli
    ldi r16, lo8(RAMEND)
    out _SFR_IO_ADDR(SPL), r16
    ldi r16, hi8(RAMEND)
    out _SFR_IO_ADDR(SPH), r16
    clr r16
    sts 0x0425, r16
    sts 0x0426, r16
    sts 0x0427, r16
    sts 0x0428, r16

; INITIALIZATION BEGIN
    clr r2
    ldi r24, 0x24
    sts 0x0200, r24
    ldi r24, 0x0A
    sts 0x0201, r24
    ldi r24, 0x0F
    sts 0x0202, r24
    ldi r24, 0x20
    sts 0x0203, r24
    ldi r24, 0x06
    sts 0x0204, r24
    ldi r24, 0x16
    sts 0x0205, r24
    ldi r24, 0x1B
    sts 0x0206, r24
    ldi r24, 0xEA
    sts 0x0207, r24
    ldi r24, 0xDC
    sts 0x0208, r24
    ldi r24, 0xF0
    sts 0x0209, r24
    ldi r24, 0xEF
    sts 0x020A, r24
    ldi r24, 0x1E
    sts 0x020B, r24
    ldi r24, 0x21
    sts 0x020C, r24
    ldi r24, 0xD8
    sts 0x020D, r24
    ldi r24, 0x00
    sts 0x020E, r24
    ldi r24, 0x1A
    sts 0x020F, r24
    ldi r24, 0x61
    sts 0x0210, r24
    ldi r24, 0x69
    sts 0x0211, r24
    ldi r24, 0xE2
    sts 0x0212, r24
    ldi r24, 0x75
    sts 0x0213, r24
    ldi r24, 0xE4
    sts 0x0214, r24
    ldi r24, 0x1A
    sts 0x0215, r24
    ldi r24, 0xC2
    sts 0x0216, r24
    ldi r24, 0x4B
    sts 0x0217, r24
    ldi r24, 0x70
    sts 0x0218, r24
    ldi r24, 0xA3
    sts 0x0219, r24
    ldi r24, 0x6E
    sts 0x021A, r24
    ldi r24, 0x18
    sts 0x021B, r24
    ldi r24, 0x5E
    sts 0x021C, r24
    ldi r24, 0x11
    sts 0x021D, r24
    ldi r24, 0x3D
    sts 0x021E, r24
    ldi r24, 0xEE
    sts 0x021F, r24
    ldi r24, 0x62
    sts 0x0220, r24
    ldi r24, 0x13
    sts 0x0221, r24
    ldi r24, 0xC5
    sts 0x0222, r24
    ldi r24, 0x20
    sts 0x0223, r24
    ldi r24, 0xC5
    sts 0x0224, r24
    ldi r24, 0xF1
    sts 0x0225, r24
    ldi r24, 0xCC
    sts 0x0226, r24
    ldi r24, 0x54
    sts 0x0227, r24
    ldi r24, 0x9C
    sts 0x0228, r24
    ldi r24, 0xC5
    sts 0x0229, r24
    ldi r24, 0xDC
    sts 0x022A, r24
    ldi r24, 0xB4
    sts 0x022B, r24
    ldi r24, 0x0C
    sts 0x022C, r24
    ldi r24, 0x83
    sts 0x022D, r24
    ldi r24, 0x73
    sts 0x022E, r24
    ldi r24, 0x94
    sts 0x022F, r24
    ldi r24, 0x62
    sts 0x0230, r24
    ldi r24, 0x15
    sts 0x0231, r24
    ldi r24, 0xD7
    sts 0x0232, r24
    ldi r24, 0x4F
    sts 0x0233, r24
    ldi r24, 0x14
    sts 0x0234, r24
    ldi r24, 0x67
    sts 0x0235, r24
    ldi r24, 0x0E
    sts 0x0236, r24
    ldi r24, 0xD8
    sts 0x0237, r24
    ldi r24, 0x22
    sts 0x0238, r24
    ldi r24, 0xDE
    sts 0x0239, r24
    ldi r24, 0x35
    sts 0x023A, r24
    ldi r24, 0x71
    sts 0x023B, r24
    ldi r24, 0x49
    sts 0x023C, r24
    ldi r24, 0xC8
    sts 0x023D, r24
    ldi r24, 0x49
    sts 0x023E, r24
    ldi r24, 0x17
    sts 0x023F, r24
    ldi r24, 0x41
    sts 0x0240, r24
    ldi r24, 0xB3
    sts 0x0241, r24
    ldi r24, 0x82
    sts 0x0242, r24
    ldi r24, 0xCF
    sts 0x0243, r24
    ldi r24, 0x9F
    sts 0x0244, r24
    ldi r24, 0x68
    sts 0x0245, r24
    ldi r24, 0x25
    sts 0x0246, r24
    ldi r24, 0x35
    sts 0x0247, r24
    ldi r24, 0x28
    sts 0x0248, r24
    ldi r24, 0xFE
    sts 0x0249, r24
    ldi r24, 0x63
    sts 0x024A, r24
    ldi r24, 0xA6
    sts 0x024B, r24
    ldi r24, 0x08
    sts 0x024C, r24
    ldi r24, 0xA9
    sts 0x024D, r24
    ldi r24, 0x27
    sts 0x024E, r24
    ldi r24, 0xD4
    sts 0x024F, r24
    ldi r24, 0x27
    sts 0x0250, r24
    ldi r24, 0xE6
    sts 0x0251, r24
    ldi r24, 0x69
    sts 0x0252, r24
    ldi r24, 0xB5
    sts 0x0253, r24
    ldi r24, 0xB4
    sts 0x0254, r24
    ldi r24, 0xB4
    sts 0x0255, r24
    ldi r24, 0x72
    sts 0x0256, r24
    ldi r24, 0x2A
    sts 0x0257, r24
    ldi r24, 0x7A
    sts 0x0258, r24
    ldi r24, 0x97
    sts 0x0259, r24
    ldi r24, 0x82
    sts 0x025A, r24
    ldi r24, 0x9D
    sts 0x025B, r24
    ldi r24, 0xAB
    sts 0x025C, r24
    ldi r24, 0x33
    sts 0x025D, r24
    ldi r24, 0x2D
    sts 0x025E, r24
    ldi r24, 0x6A
    sts 0x025F, r24
    ldi r24, 0xBE
    sts 0x0260, r24
    ldi r24, 0xA9
    sts 0x0261, r24
    ldi r24, 0x43
    sts 0x0262, r24
    ldi r24, 0xCD
    sts 0x0263, r24
    ldi r24, 0x4D
    sts 0x0264, r24
    ldi r24, 0xE2
    sts 0x0265, r24
    ldi r24, 0x49
    sts 0x0266, r24
    ldi r24, 0x9D
    sts 0x0267, r24
    ldi r24, 0xC0
    sts 0x0268, r24
    ldi r24, 0x27
    sts 0x0269, r24
    ldi r24, 0x1B
    sts 0x026A, r24
    ldi r24, 0xE0
    sts 0x026B, r24
    ldi r24, 0x4C
    sts 0x026C, r24
    ldi r24, 0x56
    sts 0x026D, r24
    ldi r24, 0xA4
    sts 0x026E, r24
    ldi r24, 0xBC
    sts 0x026F, r24
    ldi r24, 0x74
    sts 0x0270, r24
    ldi r24, 0xD5
    sts 0x0271, r24
    ldi r24, 0xD3
    sts 0x0272, r24
    ldi r24, 0x85
    sts 0x0273, r24
    ldi r24, 0xB7
    sts 0x0274, r24
    ldi r24, 0x20
    sts 0x0275, r24
    ldi r24, 0xEF
    sts 0x0276, r24
    ldi r24, 0xA4
    sts 0x0277, r24
    ldi r24, 0x03
    sts 0x0278, r24
    ldi r24, 0xA9
    sts 0x0279, r24
    ldi r24, 0x94
    sts 0x027A, r24
    ldi r24, 0xBA
    sts 0x027B, r24
    ldi r24, 0x91
    sts 0x027C, r24
    ldi r24, 0xAF
    sts 0x027D, r24
    ldi r24, 0x7F
    sts 0x027E, r24
    ldi r24, 0x18
    sts 0x027F, r24
    ldi r24, 0x27
    sts 0x0280, r24
    ldi r24, 0x8A
    sts 0x0281, r24
    ldi r24, 0xAD
    sts 0x0282, r24
    ldi r24, 0xD6
    sts 0x0283, r24
    ldi r24, 0x14
    sts 0x0284, r24
    ldi r24, 0x90
    sts 0x0285, r24
    ldi r24, 0xC9
    sts 0x0286, r24
    ldi r24, 0xB4
    sts 0x0287, r24
    ldi r24, 0x00
    sts 0x0288, r24
    ldi r24, 0xD1
    sts 0x0289, r24
    ldi r24, 0xF7
    sts 0x028A, r24
    ldi r24, 0xAA
    sts 0x028B, r24
    ldi r24, 0xA9
    sts 0x028C, r24
    ldi r24, 0xB6
    sts 0x028D, r24
    ldi r24, 0xD5
    sts 0x028E, r24
    ldi r24, 0x9C
    sts 0x028F, r24
    ldi r24, 0x9E
    sts 0x0290, r24
    ldi r24, 0x0F
    sts 0x0291, r24
    ldi r24, 0x00
    sts 0x0292, r24
    ldi r24, 0x00
    sts 0x0293, r24
    ldi r24, 0x4E
    sts 0x0294, r24
    ldi r24, 0xFC
    sts 0x0295, r24
    ldi r24, 0xFF
    sts 0x0296, r24
    ldi r24, 0xFF
    sts 0x0297, r24
    ldi r24, 0x06
    sts 0x0298, r24
    ldi r24, 0x10
    sts 0x0299, r24
    ldi r24, 0x00
    sts 0x029A, r24
    ldi r24, 0x00
    sts 0x029B, r24
    ldi r24, 0xCE
    sts 0x029C, r24
    ldi r24, 0x05
    sts 0x029D, r24
    ldi r24, 0x00
    sts 0x029E, r24
    ldi r24, 0x00
    sts 0x029F, r24
    ldi r24, 0x3B
    sts 0x02A0, r24
    ldi r24, 0xF0
    sts 0x02A1, r24
    ldi r24, 0xFF
    sts 0x02A2, r24
    ldi r24, 0xFF
    sts 0x02A3, r24
    ldi r24, 0xE3
    sts 0x02A4, r24
    ldi r24, 0x0C
    sts 0x02A5, r24
    ldi r24, 0x00
    sts 0x02A6, r24
    ldi r24, 0x00
    sts 0x02A7, r24
    ldi r24, 0xE0
    sts 0x02A8, r24
    ldi r24, 0xFA
    sts 0x02A9, r24
    ldi r24, 0xFF
    sts 0x02AA, r24
    ldi r24, 0xFF
    sts 0x02AB, r24
    ldi r24, 0xDC
    sts 0x02AC, r24
    ldi r24, 0xF8
    sts 0x02AD, r24
    ldi r24, 0xFF
    sts 0x02AE, r24
    ldi r24, 0xFF
    sts 0x02AF, r24
    ldi r24, 0x96
    sts 0x0318, r24
    ldi r24, 0x81
    sts 0x0319, r24
    ldi r24, 0x25
    sts 0x031A, r24
    ldi r24, 0xE4
    sts 0x031B, r24
    ldi r24, 0x32
    sts 0x031C, r24
    ldi r24, 0x97
    sts 0x031D, r24
    ldi r24, 0x5F
    sts 0x031E, r24
    ldi r24, 0xA2
    sts 0x031F, r24
    ldi r24, 0xEA
    sts 0x0320, r24
    ldi r24, 0x1B
    sts 0x0321, r24
    ldi r24, 0x42
    sts 0x0322, r24
    ldi r24, 0x67
    sts 0x0323, r24
    ldi r24, 0x74
    sts 0x0324, r24
    ldi r24, 0x9B
    sts 0x0325, r24
    ldi r24, 0x20
    sts 0x0326, r24
    ldi r24, 0xC9
    sts 0x0327, r24
    ldi r24, 0xF2
    sts 0x0328, r24
    ldi r24, 0xA0
    sts 0x0329, r24
    ldi r24, 0x74
    sts 0x032A, r24
    ldi r24, 0xA2
    sts 0x032B, r24
    ldi r24, 0x44
    sts 0x032C, r24
    ldi r24, 0x2D
    sts 0x032D, r24
    ldi r24, 0x2A
    sts 0x032E, r24
    ldi r24, 0xBB
    sts 0x032F, r24
    ldi r24, 0x74
    sts 0x0330, r24
    ldi r24, 0x1C
    sts 0x0331, r24
    ldi r24, 0x10
    sts 0x0332, r24
    ldi r24, 0x8F
    sts 0x0333, r24
    ldi r24, 0x36
    sts 0x0334, r24
    ldi r24, 0xED
    sts 0x0335, r24
    ldi r24, 0xC5
    sts 0x0336, r24
    ldi r24, 0x6E
    sts 0x0337, r24
    ldi r24, 0xB9
    sts 0x0338, r24
    ldi r24, 0x08
    sts 0x0339, r24
    ldi r24, 0x00
    sts 0x033A, r24
    ldi r24, 0x00
    sts 0x033B, r24
    ldi r24, 0x5A
    sts 0x033C, r24
    ldi r24, 0xEA
    sts 0x033D, r24
    ldi r24, 0xFF
    sts 0x033E, r24
    ldi r24, 0xFF
    sts 0x033F, r24
    ldi r24, 0x32
    sts 0x0340, r24
    ldi r24, 0xEC
    sts 0x0341, r24
    ldi r24, 0xFF
    sts 0x0342, r24
    ldi r24, 0xFF
    sts 0x0343, r24
    ldi r24, 0x22
    sts 0x0344, r24
    ldi r24, 0xFE
    sts 0x0345, r24
    ldi r24, 0xFF
    sts 0x0346, r24
    ldi r24, 0xFF
    sts 0x0347, r24
; INITIALIZATION END

    ldi r16, (1 << AS0)
    out _SFR_IO_ADDR(ASSR), r16
    ldi r16, 0x05
    out _SFR_IO_ADDR(TCCR0), r16
wait_timer0_busy:
    in r16, _SFR_IO_ADDR(ASSR)
    andi r16, (1 << TCR0UB) | (1 << OCR0UB) | (1 << TCN0UB)
    brne wait_timer0_busy
    ldi r16, (1 << TOIE0)
    out _SFR_IO_ADDR(TIMSK), r16
    sei

wait_for_tick:
    cli
    lds r16, 0x0425
    tst r16
    brne tick_ready
    sei
    rjmp wait_for_tick

tick_ready:
    clr r16
    sts 0x0425, r16
    out _SFR_IO_ADDR(MCUCR), r16
    ldi r16, 1
    sts 0x0426, r16
    sei
; BODY BEGIN

    ; ---- inference begins here ----
    lds r9, 0x0200
    lds r10, 0x0201
    lds r11, 0x0202
    lds r12, 0x0203
    lds r13, 0x0204
    lds r14, 0x0205
    lds r15, 0x0206
    lds r24, 0x0207
    lds r25, 0x0208
    lds r26, 0x0209
    lds r27, 0x020A
    lds r28, 0x020B
    lds r29, 0x020C
    lds r30, 0x020D
    lds r31, 0x020E
    lds r3, 0x020F
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0210
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0211
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0212
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0213
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0214
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0215
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0216
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0217
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0218
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0219
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x021A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x021B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x021C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x021D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x021E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x021F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02B0, r18
    sts 0x02B1, r19
    sts 0x02B2, r20
    sts 0x02B3, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0220
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0221
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0222
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0223
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0224
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0225
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0226
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0227
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0228
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0229
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x022A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x022B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x022C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x022D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x022E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x022F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02B4, r18
    sts 0x02B5, r19
    sts 0x02B6, r20
    sts 0x02B7, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0230
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0231
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0232
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0233
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0234
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0235
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0236
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0237
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0238
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0239
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x023A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x023B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x023C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x023D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x023E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x023F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02B8, r18
    sts 0x02B9, r19
    sts 0x02BA, r20
    sts 0x02BB, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0240
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0241
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0242
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0243
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0244
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0245
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0246
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0247
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0248
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0249
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x024A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x024B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x024C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x024D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x024E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x024F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02BC, r18
    sts 0x02BD, r19
    sts 0x02BE, r20
    sts 0x02BF, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0250
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0251
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0252
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0253
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0254
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0255
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0256
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0257
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0258
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0259
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x025A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x025B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x025C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x025D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x025E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x025F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02C0, r18
    sts 0x02C1, r19
    sts 0x02C2, r20
    sts 0x02C3, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0260
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0261
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0262
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0263
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0264
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0265
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0266
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0267
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0268
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0269
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x026A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x026B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x026C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x026D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x026E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x026F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02C4, r18
    sts 0x02C5, r19
    sts 0x02C6, r20
    sts 0x02C7, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0270
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0271
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0272
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0273
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0274
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0275
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0276
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0277
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0278
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0279
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x027A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x027B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x027C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x027D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x027E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x027F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02C8, r18
    sts 0x02C9, r19
    sts 0x02CA, r20
    sts 0x02CB, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0280
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0281
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0282
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0283
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0284
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0285
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0286
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0287
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r25
    lds r17, 0x0288
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r26
    lds r17, 0x0289
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r27
    lds r17, 0x028A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r28
    lds r17, 0x028B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r29
    lds r17, 0x028C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r30
    lds r17, 0x028D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r31
    lds r17, 0x028E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r3
    lds r17, 0x028F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x02CC, r18
    sts 0x02CD, r19
    sts 0x02CE, r20
    sts 0x02CF, r21
    lds r18, 0x02B0
    lds r19, 0x0290
    add r18, r19
    sts 0x02D0, r18
    lds r18, 0x02B1
    lds r19, 0x0291
    adc r18, r19
    sts 0x02D1, r18
    lds r18, 0x02B2
    lds r19, 0x0292
    adc r18, r19
    sts 0x02D2, r18
    lds r18, 0x02B3
    lds r19, 0x0293
    adc r18, r19
    sts 0x02D3, r18
    lds r18, 0x02B4
    lds r19, 0x0294
    add r18, r19
    sts 0x02D4, r18
    lds r18, 0x02B5
    lds r19, 0x0295
    adc r18, r19
    sts 0x02D5, r18
    lds r18, 0x02B6
    lds r19, 0x0296
    adc r18, r19
    sts 0x02D6, r18
    lds r18, 0x02B7
    lds r19, 0x0297
    adc r18, r19
    sts 0x02D7, r18
    lds r18, 0x02B8
    lds r19, 0x0298
    add r18, r19
    sts 0x02D8, r18
    lds r18, 0x02B9
    lds r19, 0x0299
    adc r18, r19
    sts 0x02D9, r18
    lds r18, 0x02BA
    lds r19, 0x029A
    adc r18, r19
    sts 0x02DA, r18
    lds r18, 0x02BB
    lds r19, 0x029B
    adc r18, r19
    sts 0x02DB, r18
    lds r18, 0x02BC
    lds r19, 0x029C
    add r18, r19
    sts 0x02DC, r18
    lds r18, 0x02BD
    lds r19, 0x029D
    adc r18, r19
    sts 0x02DD, r18
    lds r18, 0x02BE
    lds r19, 0x029E
    adc r18, r19
    sts 0x02DE, r18
    lds r18, 0x02BF
    lds r19, 0x029F
    adc r18, r19
    sts 0x02DF, r18
    lds r18, 0x02C0
    lds r19, 0x02A0
    add r18, r19
    sts 0x02E0, r18
    lds r18, 0x02C1
    lds r19, 0x02A1
    adc r18, r19
    sts 0x02E1, r18
    lds r18, 0x02C2
    lds r19, 0x02A2
    adc r18, r19
    sts 0x02E2, r18
    lds r18, 0x02C3
    lds r19, 0x02A3
    adc r18, r19
    sts 0x02E3, r18
    lds r18, 0x02C4
    lds r19, 0x02A4
    add r18, r19
    sts 0x02E4, r18
    lds r18, 0x02C5
    lds r19, 0x02A5
    adc r18, r19
    sts 0x02E5, r18
    lds r18, 0x02C6
    lds r19, 0x02A6
    adc r18, r19
    sts 0x02E6, r18
    lds r18, 0x02C7
    lds r19, 0x02A7
    adc r18, r19
    sts 0x02E7, r18
    lds r18, 0x02C8
    lds r19, 0x02A8
    add r18, r19
    sts 0x02E8, r18
    lds r18, 0x02C9
    lds r19, 0x02A9
    adc r18, r19
    sts 0x02E9, r18
    lds r18, 0x02CA
    lds r19, 0x02AA
    adc r18, r19
    sts 0x02EA, r18
    lds r18, 0x02CB
    lds r19, 0x02AB
    adc r18, r19
    sts 0x02EB, r18
    lds r18, 0x02CC
    lds r19, 0x02AC
    add r18, r19
    sts 0x02EC, r18
    lds r18, 0x02CD
    lds r19, 0x02AD
    adc r18, r19
    sts 0x02ED, r18
    lds r18, 0x02CE
    lds r19, 0x02AE
    adc r18, r19
    sts 0x02EE, r18
    lds r18, 0x02CF
    lds r19, 0x02AF
    adc r18, r19
    sts 0x02EF, r18
    lds r22, 0x02D3
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02D0
    and r18, r23
    sts 0x02F0, r18
    lds r18, 0x02D1
    and r18, r23
    sts 0x02F1, r18
    lds r18, 0x02D2
    and r18, r23
    sts 0x02F2, r18
    lds r18, 0x02D3
    and r18, r23
    sts 0x02F3, r18
    lds r22, 0x02D7
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02D4
    and r18, r23
    sts 0x02F4, r18
    lds r18, 0x02D5
    and r18, r23
    sts 0x02F5, r18
    lds r18, 0x02D6
    and r18, r23
    sts 0x02F6, r18
    lds r18, 0x02D7
    and r18, r23
    sts 0x02F7, r18
    lds r22, 0x02DB
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02D8
    and r18, r23
    sts 0x02F8, r18
    lds r18, 0x02D9
    and r18, r23
    sts 0x02F9, r18
    lds r18, 0x02DA
    and r18, r23
    sts 0x02FA, r18
    lds r18, 0x02DB
    and r18, r23
    sts 0x02FB, r18
    lds r22, 0x02DF
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02DC
    and r18, r23
    sts 0x02FC, r18
    lds r18, 0x02DD
    and r18, r23
    sts 0x02FD, r18
    lds r18, 0x02DE
    and r18, r23
    sts 0x02FE, r18
    lds r18, 0x02DF
    and r18, r23
    sts 0x02FF, r18
    lds r22, 0x02E3
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02E0
    and r18, r23
    sts 0x0300, r18
    lds r18, 0x02E1
    and r18, r23
    sts 0x0301, r18
    lds r18, 0x02E2
    and r18, r23
    sts 0x0302, r18
    lds r18, 0x02E3
    and r18, r23
    sts 0x0303, r18
    lds r22, 0x02E7
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02E4
    and r18, r23
    sts 0x0304, r18
    lds r18, 0x02E5
    and r18, r23
    sts 0x0305, r18
    lds r18, 0x02E6
    and r18, r23
    sts 0x0306, r18
    lds r18, 0x02E7
    and r18, r23
    sts 0x0307, r18
    lds r22, 0x02EB
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02E8
    and r18, r23
    sts 0x0308, r18
    lds r18, 0x02E9
    and r18, r23
    sts 0x0309, r18
    lds r18, 0x02EA
    and r18, r23
    sts 0x030A, r18
    lds r18, 0x02EB
    and r18, r23
    sts 0x030B, r18
    lds r22, 0x02EF
    lsl r22
    sbc r23, r23
    com r23
    lds r18, 0x02EC
    and r18, r23
    sts 0x030C, r18
    lds r18, 0x02ED
    and r18, r23
    sts 0x030D, r18
    lds r18, 0x02EE
    and r18, r23
    sts 0x030E, r18
    lds r18, 0x02EF
    and r18, r23
    sts 0x030F, r18
    lds r18, 0x02F0
    lds r19, 0x02F1
    lds r20, 0x02F2
    lds r21, 0x02F3
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0310, r3
    lds r18, 0x02F4
    lds r19, 0x02F5
    lds r20, 0x02F6
    lds r21, 0x02F7
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0311, r3
    lds r18, 0x02F8
    lds r19, 0x02F9
    lds r20, 0x02FA
    lds r21, 0x02FB
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0312, r3
    lds r18, 0x02FC
    lds r19, 0x02FD
    lds r20, 0x02FE
    lds r21, 0x02FF
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0313, r3
    lds r18, 0x0300
    lds r19, 0x0301
    lds r20, 0x0302
    lds r21, 0x0303
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0314, r3
    lds r18, 0x0304
    lds r19, 0x0305
    lds r20, 0x0306
    lds r21, 0x0307
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0315, r3
    lds r18, 0x0308
    lds r19, 0x0309
    lds r20, 0x030A
    lds r21, 0x030B
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0316, r3
    lds r18, 0x030C
    lds r19, 0x030D
    lds r20, 0x030E
    lds r21, 0x030F
    ldi r16, 0x5C
    ldi r17, 0x43
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0317, r3
    lds r9, 0x0310
    lds r10, 0x0311
    lds r11, 0x0312
    lds r12, 0x0313
    lds r13, 0x0314
    lds r14, 0x0315
    lds r15, 0x0316
    lds r24, 0x0317
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0318
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0319
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x031A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x031B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x031C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x031D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x031E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x031F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x0348, r18
    sts 0x0349, r19
    sts 0x034A, r20
    sts 0x034B, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0320
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0321
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0322
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0323
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0324
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0325
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0326
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0327
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x034C, r18
    sts 0x034D, r19
    sts 0x034E, r20
    sts 0x034F, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0328
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0329
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x032A
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x032B
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x032C
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x032D
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x032E
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x032F
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x0350, r18
    sts 0x0351, r19
    sts 0x0352, r20
    sts 0x0353, r21
    clr r18
    clr r19
    clr r20
    clr r21
    mov r16, r9
    lds r17, 0x0330
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r10
    lds r17, 0x0331
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r11
    lds r17, 0x0332
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r12
    lds r17, 0x0333
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r13
    lds r17, 0x0334
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r14
    lds r17, 0x0335
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r15
    lds r17, 0x0336
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    mov r16, r24
    lds r17, 0x0337
    muls r16, r17
    mov r22, r1
    lsl r22
    sbc r22, r22
    mov r23, r22
    add r18, r0
    adc r19, r1
    adc r20, r22
    adc r21, r23
    sts 0x0354, r18
    sts 0x0355, r19
    sts 0x0356, r20
    sts 0x0357, r21
    lds r18, 0x0348
    lds r19, 0x0338
    add r18, r19
    sts 0x0358, r18
    lds r18, 0x0349
    lds r19, 0x0339
    adc r18, r19
    sts 0x0359, r18
    lds r18, 0x034A
    lds r19, 0x033A
    adc r18, r19
    sts 0x035A, r18
    lds r18, 0x034B
    lds r19, 0x033B
    adc r18, r19
    sts 0x035B, r18
    lds r18, 0x034C
    lds r19, 0x033C
    add r18, r19
    sts 0x035C, r18
    lds r18, 0x034D
    lds r19, 0x033D
    adc r18, r19
    sts 0x035D, r18
    lds r18, 0x034E
    lds r19, 0x033E
    adc r18, r19
    sts 0x035E, r18
    lds r18, 0x034F
    lds r19, 0x033F
    adc r18, r19
    sts 0x035F, r18
    lds r18, 0x0350
    lds r19, 0x0340
    add r18, r19
    sts 0x0360, r18
    lds r18, 0x0351
    lds r19, 0x0341
    adc r18, r19
    sts 0x0361, r18
    lds r18, 0x0352
    lds r19, 0x0342
    adc r18, r19
    sts 0x0362, r18
    lds r18, 0x0353
    lds r19, 0x0343
    adc r18, r19
    sts 0x0363, r18
    lds r18, 0x0354
    lds r19, 0x0344
    add r18, r19
    sts 0x0364, r18
    lds r18, 0x0355
    lds r19, 0x0345
    adc r18, r19
    sts 0x0365, r18
    lds r18, 0x0356
    lds r19, 0x0346
    adc r18, r19
    sts 0x0366, r18
    lds r18, 0x0357
    lds r19, 0x0347
    adc r18, r19
    sts 0x0367, r18
    lds r18, 0x0358
    lds r19, 0x0359
    lds r20, 0x035A
    lds r21, 0x035B
    ldi r16, 0xB0
    ldi r17, 0x64
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0368, r3
    lds r18, 0x035C
    lds r19, 0x035D
    lds r20, 0x035E
    lds r21, 0x035F
    ldi r16, 0xB0
    ldi r17, 0x64
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x0369, r3
    lds r18, 0x0360
    lds r19, 0x0361
    lds r20, 0x0362
    lds r21, 0x0363
    ldi r16, 0xB0
    ldi r17, 0x64
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x036A, r3
    lds r18, 0x0364
    lds r19, 0x0365
    lds r20, 0x0366
    lds r21, 0x0367
    ldi r16, 0xB0
    ldi r17, 0x64
    clr r3
    clr r4
    clr r5
    clr r6
    clr r7
    clr r8
    mul r18, r16
    add r3, r0
    adc r4, r1
    adc r5, r2
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r18, r17
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r16
    add r4, r0
    adc r5, r1
    adc r6, r2
    adc r7, r2
    adc r8, r2
    mul r19, r17
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r16
    add r5, r0
    adc r6, r1
    adc r7, r2
    adc r8, r2
    mul r20, r17
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r16
    add r6, r0
    adc r7, r1
    adc r8, r2
    mulsu r21, r17
    add r7, r0
    adc r8, r1
    ldi r16, 0x00
    add r3, r16
    ldi r16, 0x00
    adc r4, r16
    ldi r16, 0x20
    adc r5, r16
    ldi r16, 0x00
    adc r6, r16
    ldi r16, 0x00
    adc r7, r16
    ldi r16, 0x00
    adc r8, r16
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    asr r8
    ror r7
    ror r6
    ror r5
    ror r4
    ror r3
    sts 0x036B, r3
    lds r24, 0x0368
    sts 0x036C, r24
    lds r24, 0x0369
    sts 0x036D, r24
    lds r24, 0x036A
    sts 0x036E, r24
    lds r24, 0x036B
    sts 0x036F, r24
; BODY END
    cli
    clr r16
    sts 0x0426, r16
    lds r16, 0x0428
    inc r16
    sts 0x0428, r16
    lds r16, 0x0427
    tst r16
    brne periodic_done
    lds r16, 0x0428
    cpi r16, 5
    breq periodic_done
    sei
    jmp wait_for_tick

periodic_done:
    cli
; Terminal metadata is compiler-side structural evidence;
; no simulator SRAM readback is claimed.
; META completed_count_addr=0x0428
; META overrun_addr=0x0427
; META output_addr=0x036C output_len=4
    lds r16, 0x0428
    sts 0x0428, r16
    lds r16, 0x0427
    sts 0x0427, r16
    lds r16, 0x036C
    sts 0x036C, r16
    lds r16, 0x036D
    sts 0x036D, r16
    lds r16, 0x036E
    sts 0x036E, r16
    lds r16, 0x036F
    sts 0x036F, r16
    break

timer0_ovf_isr:
    push r16
    in r16, _SFR_IO_ADDR(SREG)
    push r16
    lds r16, 0x0426
    tst r16
    breq timer0_no_overrun
    ldi r16, 1
    sts 0x0427, r16
timer0_no_overrun:
    ldi r16, 1
    sts 0x0425, r16
    pop r16
    out _SFR_IO_ADDR(SREG), r16
    pop r16
    reti
