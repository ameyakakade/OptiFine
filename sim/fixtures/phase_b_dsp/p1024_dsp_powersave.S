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
    sts 0x0BD5, r16
    sts 0x0BD6, r16
    sts 0x0BD7, r16
    sts 0x0BD8, r16

; INITIALIZATION BEGIN
    clr r2
    ldi r24, 0x00
    sts 0x0200, r24
    ldi r24, 0x00
    sts 0x0201, r24
    ldi r24, 0xC0
    sts 0x0202, r24
    ldi r24, 0x29
    sts 0x0203, r24
    ldi r24, 0x07
    sts 0x0204, r24
    ldi r24, 0x32
    sts 0x0205, r24
    ldi r24, 0x6B
    sts 0x0206, r24
    ldi r24, 0x1C
    sts 0x0207, r24
    ldi r24, 0xE0
    sts 0x0208, r24
    ldi r24, 0x09
    sts 0x0209, r24
    ldi r24, 0x90
    sts 0x020A, r24
    ldi r24, 0x0E
    sts 0x020B, r24
    ldi r24, 0x98
    sts 0x020C, r24
    ldi r24, 0x19
    sts 0x020D, r24
    ldi r24, 0x81
    sts 0x020E, r24
    ldi r24, 0x0C
    sts 0x020F, r24
    ldi r24, 0xD9
    sts 0x0210, r24
    ldi r24, 0xE4
    sts 0x0211, r24
    ldi r24, 0x9B
    sts 0x0212, r24
    ldi r24, 0xC3
    sts 0x0213, r24
    ldi r24, 0x3D
    sts 0x0214, r24
    ldi r24, 0xC8
    sts 0x0215, r24
    ldi r24, 0x1D
    sts 0x0216, r24
    ldi r24, 0xEC
    sts 0x0217, r24
    ldi r24, 0xE8
    sts 0x0218, r24
    ldi r24, 0x0A
    sts 0x0219, r24
    ldi r24, 0x8F
    sts 0x021A, r24
    ldi r24, 0x0D
    sts 0x021B, r24
    ldi r24, 0x3B
    sts 0x021C, r24
    ldi r24, 0x03
    sts 0x021D, r24
    ldi r24, 0x37
    sts 0x021E, r24
    ldi r24, 0x0A
    sts 0x021F, r24
    ldi r24, 0x66
    sts 0x0220, r24
    ldi r24, 0x26
    sts 0x0221, r24
    ldi r24, 0x84
    sts 0x0222, r24
    ldi r24, 0x39
    sts 0x0223, r24
    ldi r24, 0x6F
    sts 0x0224, r24
    ldi r24, 0x27
    sts 0x0225, r24
    ldi r24, 0xF8
    sts 0x0226, r24
    ldi r24, 0xF9
    sts 0x0227, r24
    ldi r24, 0xB5
    sts 0x0228, r24
    ldi r24, 0xD7
    sts 0x0229, r24
    ldi r24, 0x85
    sts 0x022A, r24
    ldi r24, 0xD8
    sts 0x022B, r24
    ldi r24, 0x71
    sts 0x022C, r24
    ldi r24, 0xEC
    sts 0x022D, r24
    ldi r24, 0xE8
    sts 0x022E, r24
    ldi r24, 0xF2
    sts 0x022F, r24
    ldi r24, 0xD9
    sts 0x0230, r24
    ldi r24, 0xE4
    sts 0x0231, r24
    ldi r24, 0x34
    sts 0x0232, r24
    ldi r24, 0xDD
    sts 0x0233, r24
    ldi r24, 0x64
    sts 0x0234, r24
    ldi r24, 0xF5
    sts 0x0235, r24
    ldi r24, 0x28
    sts 0x0236, r24
    ldi r24, 0x22
    sts 0x0237, r24
    ldi r24, 0x13
    sts 0x0238, r24
    ldi r24, 0x3D
    sts 0x0239, r24
    ldi r24, 0x03
    sts 0x023A, r24
    ldi r24, 0x30
    sts 0x023B, r24
    ldi r24, 0xD3
    sts 0x023C, r24
    ldi r24, 0x0D
    sts 0x023D, r24
    ldi r24, 0x73
    sts 0x023E, r24
    ldi r24, 0xFA
    sts 0x023F, r24
    ldi r24, 0x00
    sts 0x0240, r24
    ldi r24, 0x00
    sts 0x0241, r24
    ldi r24, 0x8D
    sts 0x0242, r24
    ldi r24, 0x05
    sts 0x0243, r24
    ldi r24, 0x2D
    sts 0x0244, r24
    ldi r24, 0xF2
    sts 0x0245, r24
    ldi r24, 0xFD
    sts 0x0246, r24
    ldi r24, 0xCF
    sts 0x0247, r24
    ldi r24, 0xED
    sts 0x0248, r24
    ldi r24, 0xC2
    sts 0x0249, r24
    ldi r24, 0xD8
    sts 0x024A, r24
    ldi r24, 0xDD
    sts 0x024B, r24
    ldi r24, 0x9C
    sts 0x024C, r24
    ldi r24, 0x0A
    sts 0x024D, r24
    ldi r24, 0xCC
    sts 0x024E, r24
    ldi r24, 0x22
    sts 0x024F, r24
    ldi r24, 0x27
    sts 0x0250, r24
    ldi r24, 0x1B
    sts 0x0251, r24
    ldi r24, 0x18
    sts 0x0252, r24
    ldi r24, 0x0D
    sts 0x0253, r24
    ldi r24, 0x8F
    sts 0x0254, r24
    ldi r24, 0x13
    sts 0x0255, r24
    ldi r24, 0x7B
    sts 0x0256, r24
    ldi r24, 0x27
    sts 0x0257, r24
    ldi r24, 0x4B
    sts 0x0258, r24
    ldi r24, 0x28
    sts 0x0259, r24
    ldi r24, 0x08
    sts 0x025A, r24
    ldi r24, 0x06
    sts 0x025B, r24
    ldi r24, 0x91
    sts 0x025C, r24
    ldi r24, 0xD8
    sts 0x025D, r24
    ldi r24, 0x7C
    sts 0x025E, r24
    ldi r24, 0xC6
    sts 0x025F, r24
    ldi r24, 0x9A
    sts 0x0260, r24
    ldi r24, 0xD9
    sts 0x0261, r24
    ldi r24, 0xC9
    sts 0x0262, r24
    ldi r24, 0xF5
    sts 0x0263, r24
    ldi r24, 0xC5
    sts 0x0264, r24
    ldi r24, 0xFC
    sts 0x0265, r24
    ldi r24, 0x71
    sts 0x0266, r24
    ldi r24, 0xF2
    sts 0x0267, r24
    ldi r24, 0x18
    sts 0x0268, r24
    ldi r24, 0xF5
    sts 0x0269, r24
    ldi r24, 0xE3
    sts 0x026A, r24
    ldi r24, 0x13
    sts 0x026B, r24
    ldi r24, 0xC3
    sts 0x026C, r24
    ldi r24, 0x37
    sts 0x026D, r24
    ldi r24, 0x65
    sts 0x026E, r24
    ldi r24, 0x3C
    sts 0x026F, r24
    ldi r24, 0x27
    sts 0x0270, r24
    ldi r24, 0x1B
    sts 0x0271, r24
    ldi r24, 0x7F
    sts 0x0272, r24
    ldi r24, 0xF3
    sts 0x0273, r24
    ldi r24, 0x68
    sts 0x0274, r24
    ldi r24, 0xE6
    sts 0x0275, r24
    ldi r24, 0x70
    sts 0x0276, r24
    ldi r24, 0xF1
    sts 0x0277, r24
    ldi r24, 0x20
    sts 0x0278, r24
    ldi r24, 0xF6
    sts 0x0279, r24
    ldi r24, 0x95
    sts 0x027A, r24
    ldi r24, 0xE3
    sts 0x027B, r24
    ldi r24, 0xF9
    sts 0x027C, r24
    ldi r24, 0xCD
    sts 0x027D, r24
    ldi r24, 0x40
    sts 0x027E, r24
    ldi r24, 0xD6
    sts 0x027F, r24
    ldi r24, 0x3D
    sts 0x0280, r24
    ldi r24, 0x0A
    sts 0x0281, r24
    ldi r24, 0x88
    sts 0x0282, r24
    ldi r24, 0x0A
    sts 0x0283, r24
    ldi r24, 0x68
    sts 0x0284, r24
    ldi r24, 0x0B
    sts 0x0285, r24
    ldi r24, 0xDB
    sts 0x0286, r24
    ldi r24, 0x0C
    sts 0x0287, r24
    ldi r24, 0xDD
    sts 0x0288, r24
    ldi r24, 0x0E
    sts 0x0289, r24
    ldi r24, 0x69
    sts 0x028A, r24
    ldi r24, 0x11
    sts 0x028B, r24
    ldi r24, 0x78
    sts 0x028C, r24
    ldi r24, 0x14
    sts 0x028D, r24
    ldi r24, 0x04
    sts 0x028E, r24
    ldi r24, 0x18
    sts 0x028F, r24
    ldi r24, 0x02
    sts 0x0290, r24
    ldi r24, 0x1C
    sts 0x0291, r24
    ldi r24, 0x68
    sts 0x0292, r24
    ldi r24, 0x20
    sts 0x0293, r24
    ldi r24, 0x2C
    sts 0x0294, r24
    ldi r24, 0x25
    sts 0x0295, r24
    ldi r24, 0x42
    sts 0x0296, r24
    ldi r24, 0x2A
    sts 0x0297, r24
    ldi r24, 0x9B
    sts 0x0298, r24
    ldi r24, 0x2F
    sts 0x0299, r24
    ldi r24, 0x2C
    sts 0x029A, r24
    ldi r24, 0x35
    sts 0x029B, r24
    ldi r24, 0xE5
    sts 0x029C, r24
    ldi r24, 0x3A
    sts 0x029D, r24
    ldi r24, 0xB8
    sts 0x029E, r24
    ldi r24, 0x40
    sts 0x029F, r24
    ldi r24, 0x96
    sts 0x02A0, r24
    ldi r24, 0x46
    sts 0x02A1, r24
    ldi r24, 0x70
    sts 0x02A2, r24
    ldi r24, 0x4C
    sts 0x02A3, r24
    ldi r24, 0x38
    sts 0x02A4, r24
    ldi r24, 0x52
    sts 0x02A5, r24
    ldi r24, 0xDF
    sts 0x02A6, r24
    ldi r24, 0x57
    sts 0x02A7, r24
    ldi r24, 0x55
    sts 0x02A8, r24
    ldi r24, 0x5D
    sts 0x02A9, r24
    ldi r24, 0x8F
    sts 0x02AA, r24
    ldi r24, 0x62
    sts 0x02AB, r24
    ldi r24, 0x7D
    sts 0x02AC, r24
    ldi r24, 0x67
    sts 0x02AD, r24
    ldi r24, 0x14
    sts 0x02AE, r24
    ldi r24, 0x6C
    sts 0x02AF, r24
    ldi r24, 0x47
    sts 0x02B0, r24
    ldi r24, 0x70
    sts 0x02B1, r24
    ldi r24, 0x0D
    sts 0x02B2, r24
    ldi r24, 0x74
    sts 0x02B3, r24
    ldi r24, 0x5C
    sts 0x02B4, r24
    ldi r24, 0x77
    sts 0x02B5, r24
    ldi r24, 0x2A
    sts 0x02B6, r24
    ldi r24, 0x7A
    sts 0x02B7, r24
    ldi r24, 0x72
    sts 0x02B8, r24
    ldi r24, 0x7C
    sts 0x02B9, r24
    ldi r24, 0x2D
    sts 0x02BA, r24
    ldi r24, 0x7E
    sts 0x02BB, r24
    ldi r24, 0x57
    sts 0x02BC, r24
    ldi r24, 0x7F
    sts 0x02BD, r24
    ldi r24, 0xEC
    sts 0x02BE, r24
    ldi r24, 0x7F
    sts 0x02BF, r24
    ldi r24, 0xEC
    sts 0x02C0, r24
    ldi r24, 0x7F
    sts 0x02C1, r24
    ldi r24, 0x57
    sts 0x02C2, r24
    ldi r24, 0x7F
    sts 0x02C3, r24
    ldi r24, 0x2D
    sts 0x02C4, r24
    ldi r24, 0x7E
    sts 0x02C5, r24
    ldi r24, 0x72
    sts 0x02C6, r24
    ldi r24, 0x7C
    sts 0x02C7, r24
    ldi r24, 0x2A
    sts 0x02C8, r24
    ldi r24, 0x7A
    sts 0x02C9, r24
    ldi r24, 0x5C
    sts 0x02CA, r24
    ldi r24, 0x77
    sts 0x02CB, r24
    ldi r24, 0x0D
    sts 0x02CC, r24
    ldi r24, 0x74
    sts 0x02CD, r24
    ldi r24, 0x47
    sts 0x02CE, r24
    ldi r24, 0x70
    sts 0x02CF, r24
    ldi r24, 0x14
    sts 0x02D0, r24
    ldi r24, 0x6C
    sts 0x02D1, r24
    ldi r24, 0x7D
    sts 0x02D2, r24
    ldi r24, 0x67
    sts 0x02D3, r24
    ldi r24, 0x8F
    sts 0x02D4, r24
    ldi r24, 0x62
    sts 0x02D5, r24
    ldi r24, 0x55
    sts 0x02D6, r24
    ldi r24, 0x5D
    sts 0x02D7, r24
    ldi r24, 0xDF
    sts 0x02D8, r24
    ldi r24, 0x57
    sts 0x02D9, r24
    ldi r24, 0x38
    sts 0x02DA, r24
    ldi r24, 0x52
    sts 0x02DB, r24
    ldi r24, 0x70
    sts 0x02DC, r24
    ldi r24, 0x4C
    sts 0x02DD, r24
    ldi r24, 0x96
    sts 0x02DE, r24
    ldi r24, 0x46
    sts 0x02DF, r24
    ldi r24, 0xB8
    sts 0x02E0, r24
    ldi r24, 0x40
    sts 0x02E1, r24
    ldi r24, 0xE5
    sts 0x02E2, r24
    ldi r24, 0x3A
    sts 0x02E3, r24
    ldi r24, 0x2C
    sts 0x02E4, r24
    ldi r24, 0x35
    sts 0x02E5, r24
    ldi r24, 0x9B
    sts 0x02E6, r24
    ldi r24, 0x2F
    sts 0x02E7, r24
    ldi r24, 0x42
    sts 0x02E8, r24
    ldi r24, 0x2A
    sts 0x02E9, r24
    ldi r24, 0x2C
    sts 0x02EA, r24
    ldi r24, 0x25
    sts 0x02EB, r24
    ldi r24, 0x68
    sts 0x02EC, r24
    ldi r24, 0x20
    sts 0x02ED, r24
    ldi r24, 0x02
    sts 0x02EE, r24
    ldi r24, 0x1C
    sts 0x02EF, r24
    ldi r24, 0x04
    sts 0x02F0, r24
    ldi r24, 0x18
    sts 0x02F1, r24
    ldi r24, 0x78
    sts 0x02F2, r24
    ldi r24, 0x14
    sts 0x02F3, r24
    ldi r24, 0x69
    sts 0x02F4, r24
    ldi r24, 0x11
    sts 0x02F5, r24
    ldi r24, 0xDD
    sts 0x02F6, r24
    ldi r24, 0x0E
    sts 0x02F7, r24
    ldi r24, 0xDB
    sts 0x02F8, r24
    ldi r24, 0x0C
    sts 0x02F9, r24
    ldi r24, 0x68
    sts 0x02FA, r24
    ldi r24, 0x0B
    sts 0x02FB, r24
    ldi r24, 0x88
    sts 0x02FC, r24
    ldi r24, 0x0A
    sts 0x02FD, r24
    ldi r24, 0x3D
    sts 0x02FE, r24
    ldi r24, 0x0A
    sts 0x02FF, r24
; INITIALIZATION END

    ldi r16, (1 << AS0)
    out _SFR_IO_ADDR(ASSR), r16
    ldi r16, 0x07
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
    lds r16, 0x0BD5
    tst r16
    brne tick_ready
    ldi r16, (1 << SE) | (1 << SM1) | (1 << SM0)
    out _SFR_IO_ADDR(MCUCR), r16
    sei
    sleep
    rjmp wait_for_tick

tick_ready:
    clr r16
    sts 0x0BD5, r16
    out _SFR_IO_ADDR(MCUCR), r16
    ldi r16, 1
    sts 0x0BD6, r16
    sei
; BODY BEGIN

    ; ---- inference begins here ----
    lds r16, 0x0200
    lds r17, 0x0201
    lds r18, 0x0280
    lds r19, 0x0281
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0300, r21
    sts 0x0301, r22
    lds r16, 0x0202
    lds r17, 0x0203
    lds r18, 0x0282
    lds r19, 0x0283
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0302, r21
    sts 0x0303, r22
    lds r16, 0x0204
    lds r17, 0x0205
    lds r18, 0x0284
    lds r19, 0x0285
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0304, r21
    sts 0x0305, r22
    lds r16, 0x0206
    lds r17, 0x0207
    lds r18, 0x0286
    lds r19, 0x0287
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0306, r21
    sts 0x0307, r22
    lds r16, 0x0208
    lds r17, 0x0209
    lds r18, 0x0288
    lds r19, 0x0289
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0308, r21
    sts 0x0309, r22
    lds r16, 0x020A
    lds r17, 0x020B
    lds r18, 0x028A
    lds r19, 0x028B
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x030A, r21
    sts 0x030B, r22
    lds r16, 0x020C
    lds r17, 0x020D
    lds r18, 0x028C
    lds r19, 0x028D
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x030C, r21
    sts 0x030D, r22
    lds r16, 0x020E
    lds r17, 0x020F
    lds r18, 0x028E
    lds r19, 0x028F
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x030E, r21
    sts 0x030F, r22
    lds r16, 0x0210
    lds r17, 0x0211
    lds r18, 0x0290
    lds r19, 0x0291
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0310, r21
    sts 0x0311, r22
    lds r16, 0x0212
    lds r17, 0x0213
    lds r18, 0x0292
    lds r19, 0x0293
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0312, r21
    sts 0x0313, r22
    lds r16, 0x0214
    lds r17, 0x0215
    lds r18, 0x0294
    lds r19, 0x0295
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0314, r21
    sts 0x0315, r22
    lds r16, 0x0216
    lds r17, 0x0217
    lds r18, 0x0296
    lds r19, 0x0297
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0316, r21
    sts 0x0317, r22
    lds r16, 0x0218
    lds r17, 0x0219
    lds r18, 0x0298
    lds r19, 0x0299
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0318, r21
    sts 0x0319, r22
    lds r16, 0x021A
    lds r17, 0x021B
    lds r18, 0x029A
    lds r19, 0x029B
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x031A, r21
    sts 0x031B, r22
    lds r16, 0x021C
    lds r17, 0x021D
    lds r18, 0x029C
    lds r19, 0x029D
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x031C, r21
    sts 0x031D, r22
    lds r16, 0x021E
    lds r17, 0x021F
    lds r18, 0x029E
    lds r19, 0x029F
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x031E, r21
    sts 0x031F, r22
    lds r16, 0x0220
    lds r17, 0x0221
    lds r18, 0x02A0
    lds r19, 0x02A1
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0320, r21
    sts 0x0321, r22
    lds r16, 0x0222
    lds r17, 0x0223
    lds r18, 0x02A2
    lds r19, 0x02A3
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0322, r21
    sts 0x0323, r22
    lds r16, 0x0224
    lds r17, 0x0225
    lds r18, 0x02A4
    lds r19, 0x02A5
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0324, r21
    sts 0x0325, r22
    lds r16, 0x0226
    lds r17, 0x0227
    lds r18, 0x02A6
    lds r19, 0x02A7
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0326, r21
    sts 0x0327, r22
    lds r16, 0x0228
    lds r17, 0x0229
    lds r18, 0x02A8
    lds r19, 0x02A9
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0328, r21
    sts 0x0329, r22
    lds r16, 0x022A
    lds r17, 0x022B
    lds r18, 0x02AA
    lds r19, 0x02AB
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x032A, r21
    sts 0x032B, r22
    lds r16, 0x022C
    lds r17, 0x022D
    lds r18, 0x02AC
    lds r19, 0x02AD
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x032C, r21
    sts 0x032D, r22
    lds r16, 0x022E
    lds r17, 0x022F
    lds r18, 0x02AE
    lds r19, 0x02AF
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x032E, r21
    sts 0x032F, r22
    lds r16, 0x0230
    lds r17, 0x0231
    lds r18, 0x02B0
    lds r19, 0x02B1
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0330, r21
    sts 0x0331, r22
    lds r16, 0x0232
    lds r17, 0x0233
    lds r18, 0x02B2
    lds r19, 0x02B3
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0332, r21
    sts 0x0333, r22
    lds r16, 0x0234
    lds r17, 0x0235
    lds r18, 0x02B4
    lds r19, 0x02B5
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0334, r21
    sts 0x0335, r22
    lds r16, 0x0236
    lds r17, 0x0237
    lds r18, 0x02B6
    lds r19, 0x02B7
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0336, r21
    sts 0x0337, r22
    lds r16, 0x0238
    lds r17, 0x0239
    lds r18, 0x02B8
    lds r19, 0x02B9
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0338, r21
    sts 0x0339, r22
    lds r16, 0x023A
    lds r17, 0x023B
    lds r18, 0x02BA
    lds r19, 0x02BB
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x033A, r21
    sts 0x033B, r22
    lds r16, 0x023C
    lds r17, 0x023D
    lds r18, 0x02BC
    lds r19, 0x02BD
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x033C, r21
    sts 0x033D, r22
    lds r16, 0x023E
    lds r17, 0x023F
    lds r18, 0x02BE
    lds r19, 0x02BF
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x033E, r21
    sts 0x033F, r22
    lds r16, 0x0240
    lds r17, 0x0241
    lds r18, 0x02C0
    lds r19, 0x02C1
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0340, r21
    sts 0x0341, r22
    lds r16, 0x0242
    lds r17, 0x0243
    lds r18, 0x02C2
    lds r19, 0x02C3
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0342, r21
    sts 0x0343, r22
    lds r16, 0x0244
    lds r17, 0x0245
    lds r18, 0x02C4
    lds r19, 0x02C5
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0344, r21
    sts 0x0345, r22
    lds r16, 0x0246
    lds r17, 0x0247
    lds r18, 0x02C6
    lds r19, 0x02C7
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0346, r21
    sts 0x0347, r22
    lds r16, 0x0248
    lds r17, 0x0249
    lds r18, 0x02C8
    lds r19, 0x02C9
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0348, r21
    sts 0x0349, r22
    lds r16, 0x024A
    lds r17, 0x024B
    lds r18, 0x02CA
    lds r19, 0x02CB
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x034A, r21
    sts 0x034B, r22
    lds r16, 0x024C
    lds r17, 0x024D
    lds r18, 0x02CC
    lds r19, 0x02CD
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x034C, r21
    sts 0x034D, r22
    lds r16, 0x024E
    lds r17, 0x024F
    lds r18, 0x02CE
    lds r19, 0x02CF
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x034E, r21
    sts 0x034F, r22
    lds r16, 0x0250
    lds r17, 0x0251
    lds r18, 0x02D0
    lds r19, 0x02D1
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0350, r21
    sts 0x0351, r22
    lds r16, 0x0252
    lds r17, 0x0253
    lds r18, 0x02D2
    lds r19, 0x02D3
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0352, r21
    sts 0x0353, r22
    lds r16, 0x0254
    lds r17, 0x0255
    lds r18, 0x02D4
    lds r19, 0x02D5
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0354, r21
    sts 0x0355, r22
    lds r16, 0x0256
    lds r17, 0x0257
    lds r18, 0x02D6
    lds r19, 0x02D7
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0356, r21
    sts 0x0357, r22
    lds r16, 0x0258
    lds r17, 0x0259
    lds r18, 0x02D8
    lds r19, 0x02D9
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0358, r21
    sts 0x0359, r22
    lds r16, 0x025A
    lds r17, 0x025B
    lds r18, 0x02DA
    lds r19, 0x02DB
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x035A, r21
    sts 0x035B, r22
    lds r16, 0x025C
    lds r17, 0x025D
    lds r18, 0x02DC
    lds r19, 0x02DD
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x035C, r21
    sts 0x035D, r22
    lds r16, 0x025E
    lds r17, 0x025F
    lds r18, 0x02DE
    lds r19, 0x02DF
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x035E, r21
    sts 0x035F, r22
    lds r16, 0x0260
    lds r17, 0x0261
    lds r18, 0x02E0
    lds r19, 0x02E1
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0360, r21
    sts 0x0361, r22
    lds r16, 0x0262
    lds r17, 0x0263
    lds r18, 0x02E2
    lds r19, 0x02E3
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0362, r21
    sts 0x0363, r22
    lds r16, 0x0264
    lds r17, 0x0265
    lds r18, 0x02E4
    lds r19, 0x02E5
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0364, r21
    sts 0x0365, r22
    lds r16, 0x0266
    lds r17, 0x0267
    lds r18, 0x02E6
    lds r19, 0x02E7
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0366, r21
    sts 0x0367, r22
    lds r16, 0x0268
    lds r17, 0x0269
    lds r18, 0x02E8
    lds r19, 0x02E9
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0368, r21
    sts 0x0369, r22
    lds r16, 0x026A
    lds r17, 0x026B
    lds r18, 0x02EA
    lds r19, 0x02EB
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x036A, r21
    sts 0x036B, r22
    lds r16, 0x026C
    lds r17, 0x026D
    lds r18, 0x02EC
    lds r19, 0x02ED
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x036C, r21
    sts 0x036D, r22
    lds r16, 0x026E
    lds r17, 0x026F
    lds r18, 0x02EE
    lds r19, 0x02EF
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x036E, r21
    sts 0x036F, r22
    lds r16, 0x0270
    lds r17, 0x0271
    lds r18, 0x02F0
    lds r19, 0x02F1
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0370, r21
    sts 0x0371, r22
    lds r16, 0x0272
    lds r17, 0x0273
    lds r18, 0x02F2
    lds r19, 0x02F3
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0372, r21
    sts 0x0373, r22
    lds r16, 0x0274
    lds r17, 0x0275
    lds r18, 0x02F4
    lds r19, 0x02F5
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0374, r21
    sts 0x0375, r22
    lds r16, 0x0276
    lds r17, 0x0277
    lds r18, 0x02F6
    lds r19, 0x02F7
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0376, r21
    sts 0x0377, r22
    lds r16, 0x0278
    lds r17, 0x0279
    lds r18, 0x02F8
    lds r19, 0x02F9
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0378, r21
    sts 0x0379, r22
    lds r16, 0x027A
    lds r17, 0x027B
    lds r18, 0x02FA
    lds r19, 0x02FB
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x037A, r21
    sts 0x037B, r22
    lds r16, 0x027C
    lds r17, 0x027D
    lds r18, 0x02FC
    lds r19, 0x02FD
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x037C, r21
    sts 0x037D, r22
    lds r16, 0x027E
    lds r17, 0x027F
    lds r18, 0x02FE
    lds r19, 0x02FF
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x037E, r21
    sts 0x037F, r22
    lds r24, 0x0300
    sts 0x0380, r24
    lds r24, 0x0301
    sts 0x0381, r24
    ldi r24, 0x00
    sts 0x0382, r24
    ldi r24, 0x00
    sts 0x0383, r24
    lds r24, 0x0340
    sts 0x0384, r24
    lds r24, 0x0341
    sts 0x0385, r24
    ldi r24, 0x00
    sts 0x0386, r24
    ldi r24, 0x00
    sts 0x0387, r24
    lds r24, 0x0320
    sts 0x0388, r24
    lds r24, 0x0321
    sts 0x0389, r24
    ldi r24, 0x00
    sts 0x038A, r24
    ldi r24, 0x00
    sts 0x038B, r24
    lds r24, 0x0360
    sts 0x038C, r24
    lds r24, 0x0361
    sts 0x038D, r24
    ldi r24, 0x00
    sts 0x038E, r24
    ldi r24, 0x00
    sts 0x038F, r24
    lds r24, 0x0310
    sts 0x0390, r24
    lds r24, 0x0311
    sts 0x0391, r24
    ldi r24, 0x00
    sts 0x0392, r24
    ldi r24, 0x00
    sts 0x0393, r24
    lds r24, 0x0350
    sts 0x0394, r24
    lds r24, 0x0351
    sts 0x0395, r24
    ldi r24, 0x00
    sts 0x0396, r24
    ldi r24, 0x00
    sts 0x0397, r24
    lds r24, 0x0330
    sts 0x0398, r24
    lds r24, 0x0331
    sts 0x0399, r24
    ldi r24, 0x00
    sts 0x039A, r24
    ldi r24, 0x00
    sts 0x039B, r24
    lds r24, 0x0370
    sts 0x039C, r24
    lds r24, 0x0371
    sts 0x039D, r24
    ldi r24, 0x00
    sts 0x039E, r24
    ldi r24, 0x00
    sts 0x039F, r24
    lds r24, 0x0308
    sts 0x03A0, r24
    lds r24, 0x0309
    sts 0x03A1, r24
    ldi r24, 0x00
    sts 0x03A2, r24
    ldi r24, 0x00
    sts 0x03A3, r24
    lds r24, 0x0348
    sts 0x03A4, r24
    lds r24, 0x0349
    sts 0x03A5, r24
    ldi r24, 0x00
    sts 0x03A6, r24
    ldi r24, 0x00
    sts 0x03A7, r24
    lds r24, 0x0328
    sts 0x03A8, r24
    lds r24, 0x0329
    sts 0x03A9, r24
    ldi r24, 0x00
    sts 0x03AA, r24
    ldi r24, 0x00
    sts 0x03AB, r24
    lds r24, 0x0368
    sts 0x03AC, r24
    lds r24, 0x0369
    sts 0x03AD, r24
    ldi r24, 0x00
    sts 0x03AE, r24
    ldi r24, 0x00
    sts 0x03AF, r24
    lds r24, 0x0318
    sts 0x03B0, r24
    lds r24, 0x0319
    sts 0x03B1, r24
    ldi r24, 0x00
    sts 0x03B2, r24
    ldi r24, 0x00
    sts 0x03B3, r24
    lds r24, 0x0358
    sts 0x03B4, r24
    lds r24, 0x0359
    sts 0x03B5, r24
    ldi r24, 0x00
    sts 0x03B6, r24
    ldi r24, 0x00
    sts 0x03B7, r24
    lds r24, 0x0338
    sts 0x03B8, r24
    lds r24, 0x0339
    sts 0x03B9, r24
    ldi r24, 0x00
    sts 0x03BA, r24
    ldi r24, 0x00
    sts 0x03BB, r24
    lds r24, 0x0378
    sts 0x03BC, r24
    lds r24, 0x0379
    sts 0x03BD, r24
    ldi r24, 0x00
    sts 0x03BE, r24
    ldi r24, 0x00
    sts 0x03BF, r24
    lds r24, 0x0304
    sts 0x03C0, r24
    lds r24, 0x0305
    sts 0x03C1, r24
    ldi r24, 0x00
    sts 0x03C2, r24
    ldi r24, 0x00
    sts 0x03C3, r24
    lds r24, 0x0344
    sts 0x03C4, r24
    lds r24, 0x0345
    sts 0x03C5, r24
    ldi r24, 0x00
    sts 0x03C6, r24
    ldi r24, 0x00
    sts 0x03C7, r24
    lds r24, 0x0324
    sts 0x03C8, r24
    lds r24, 0x0325
    sts 0x03C9, r24
    ldi r24, 0x00
    sts 0x03CA, r24
    ldi r24, 0x00
    sts 0x03CB, r24
    lds r24, 0x0364
    sts 0x03CC, r24
    lds r24, 0x0365
    sts 0x03CD, r24
    ldi r24, 0x00
    sts 0x03CE, r24
    ldi r24, 0x00
    sts 0x03CF, r24
    lds r24, 0x0314
    sts 0x03D0, r24
    lds r24, 0x0315
    sts 0x03D1, r24
    ldi r24, 0x00
    sts 0x03D2, r24
    ldi r24, 0x00
    sts 0x03D3, r24
    lds r24, 0x0354
    sts 0x03D4, r24
    lds r24, 0x0355
    sts 0x03D5, r24
    ldi r24, 0x00
    sts 0x03D6, r24
    ldi r24, 0x00
    sts 0x03D7, r24
    lds r24, 0x0334
    sts 0x03D8, r24
    lds r24, 0x0335
    sts 0x03D9, r24
    ldi r24, 0x00
    sts 0x03DA, r24
    ldi r24, 0x00
    sts 0x03DB, r24
    lds r24, 0x0374
    sts 0x03DC, r24
    lds r24, 0x0375
    sts 0x03DD, r24
    ldi r24, 0x00
    sts 0x03DE, r24
    ldi r24, 0x00
    sts 0x03DF, r24
    lds r24, 0x030C
    sts 0x03E0, r24
    lds r24, 0x030D
    sts 0x03E1, r24
    ldi r24, 0x00
    sts 0x03E2, r24
    ldi r24, 0x00
    sts 0x03E3, r24
    lds r24, 0x034C
    sts 0x03E4, r24
    lds r24, 0x034D
    sts 0x03E5, r24
    ldi r24, 0x00
    sts 0x03E6, r24
    ldi r24, 0x00
    sts 0x03E7, r24
    lds r24, 0x032C
    sts 0x03E8, r24
    lds r24, 0x032D
    sts 0x03E9, r24
    ldi r24, 0x00
    sts 0x03EA, r24
    ldi r24, 0x00
    sts 0x03EB, r24
    lds r24, 0x036C
    sts 0x03EC, r24
    lds r24, 0x036D
    sts 0x03ED, r24
    ldi r24, 0x00
    sts 0x03EE, r24
    ldi r24, 0x00
    sts 0x03EF, r24
    lds r24, 0x031C
    sts 0x03F0, r24
    lds r24, 0x031D
    sts 0x03F1, r24
    ldi r24, 0x00
    sts 0x03F2, r24
    ldi r24, 0x00
    sts 0x03F3, r24
    lds r24, 0x035C
    sts 0x03F4, r24
    lds r24, 0x035D
    sts 0x03F5, r24
    ldi r24, 0x00
    sts 0x03F6, r24
    ldi r24, 0x00
    sts 0x03F7, r24
    lds r24, 0x033C
    sts 0x03F8, r24
    lds r24, 0x033D
    sts 0x03F9, r24
    ldi r24, 0x00
    sts 0x03FA, r24
    ldi r24, 0x00
    sts 0x03FB, r24
    lds r24, 0x037C
    sts 0x03FC, r24
    lds r24, 0x037D
    sts 0x03FD, r24
    ldi r24, 0x00
    sts 0x03FE, r24
    ldi r24, 0x00
    sts 0x03FF, r24
    lds r24, 0x0302
    sts 0x0400, r24
    lds r24, 0x0303
    sts 0x0401, r24
    ldi r24, 0x00
    sts 0x0402, r24
    ldi r24, 0x00
    sts 0x0403, r24
    lds r24, 0x0342
    sts 0x0404, r24
    lds r24, 0x0343
    sts 0x0405, r24
    ldi r24, 0x00
    sts 0x0406, r24
    ldi r24, 0x00
    sts 0x0407, r24
    lds r24, 0x0322
    sts 0x0408, r24
    lds r24, 0x0323
    sts 0x0409, r24
    ldi r24, 0x00
    sts 0x040A, r24
    ldi r24, 0x00
    sts 0x040B, r24
    lds r24, 0x0362
    sts 0x040C, r24
    lds r24, 0x0363
    sts 0x040D, r24
    ldi r24, 0x00
    sts 0x040E, r24
    ldi r24, 0x00
    sts 0x040F, r24
    lds r24, 0x0312
    sts 0x0410, r24
    lds r24, 0x0313
    sts 0x0411, r24
    ldi r24, 0x00
    sts 0x0412, r24
    ldi r24, 0x00
    sts 0x0413, r24
    lds r24, 0x0352
    sts 0x0414, r24
    lds r24, 0x0353
    sts 0x0415, r24
    ldi r24, 0x00
    sts 0x0416, r24
    ldi r24, 0x00
    sts 0x0417, r24
    lds r24, 0x0332
    sts 0x0418, r24
    lds r24, 0x0333
    sts 0x0419, r24
    ldi r24, 0x00
    sts 0x041A, r24
    ldi r24, 0x00
    sts 0x041B, r24
    lds r24, 0x0372
    sts 0x041C, r24
    lds r24, 0x0373
    sts 0x041D, r24
    ldi r24, 0x00
    sts 0x041E, r24
    ldi r24, 0x00
    sts 0x041F, r24
    lds r24, 0x030A
    sts 0x0420, r24
    lds r24, 0x030B
    sts 0x0421, r24
    ldi r24, 0x00
    sts 0x0422, r24
    ldi r24, 0x00
    sts 0x0423, r24
    lds r24, 0x034A
    sts 0x0424, r24
    lds r24, 0x034B
    sts 0x0425, r24
    ldi r24, 0x00
    sts 0x0426, r24
    ldi r24, 0x00
    sts 0x0427, r24
    lds r24, 0x032A
    sts 0x0428, r24
    lds r24, 0x032B
    sts 0x0429, r24
    ldi r24, 0x00
    sts 0x042A, r24
    ldi r24, 0x00
    sts 0x042B, r24
    lds r24, 0x036A
    sts 0x042C, r24
    lds r24, 0x036B
    sts 0x042D, r24
    ldi r24, 0x00
    sts 0x042E, r24
    ldi r24, 0x00
    sts 0x042F, r24
    lds r24, 0x031A
    sts 0x0430, r24
    lds r24, 0x031B
    sts 0x0431, r24
    ldi r24, 0x00
    sts 0x0432, r24
    ldi r24, 0x00
    sts 0x0433, r24
    lds r24, 0x035A
    sts 0x0434, r24
    lds r24, 0x035B
    sts 0x0435, r24
    ldi r24, 0x00
    sts 0x0436, r24
    ldi r24, 0x00
    sts 0x0437, r24
    lds r24, 0x033A
    sts 0x0438, r24
    lds r24, 0x033B
    sts 0x0439, r24
    ldi r24, 0x00
    sts 0x043A, r24
    ldi r24, 0x00
    sts 0x043B, r24
    lds r24, 0x037A
    sts 0x043C, r24
    lds r24, 0x037B
    sts 0x043D, r24
    ldi r24, 0x00
    sts 0x043E, r24
    ldi r24, 0x00
    sts 0x043F, r24
    lds r24, 0x0306
    sts 0x0440, r24
    lds r24, 0x0307
    sts 0x0441, r24
    ldi r24, 0x00
    sts 0x0442, r24
    ldi r24, 0x00
    sts 0x0443, r24
    lds r24, 0x0346
    sts 0x0444, r24
    lds r24, 0x0347
    sts 0x0445, r24
    ldi r24, 0x00
    sts 0x0446, r24
    ldi r24, 0x00
    sts 0x0447, r24
    lds r24, 0x0326
    sts 0x0448, r24
    lds r24, 0x0327
    sts 0x0449, r24
    ldi r24, 0x00
    sts 0x044A, r24
    ldi r24, 0x00
    sts 0x044B, r24
    lds r24, 0x0366
    sts 0x044C, r24
    lds r24, 0x0367
    sts 0x044D, r24
    ldi r24, 0x00
    sts 0x044E, r24
    ldi r24, 0x00
    sts 0x044F, r24
    lds r24, 0x0316
    sts 0x0450, r24
    lds r24, 0x0317
    sts 0x0451, r24
    ldi r24, 0x00
    sts 0x0452, r24
    ldi r24, 0x00
    sts 0x0453, r24
    lds r24, 0x0356
    sts 0x0454, r24
    lds r24, 0x0357
    sts 0x0455, r24
    ldi r24, 0x00
    sts 0x0456, r24
    ldi r24, 0x00
    sts 0x0457, r24
    lds r24, 0x0336
    sts 0x0458, r24
    lds r24, 0x0337
    sts 0x0459, r24
    ldi r24, 0x00
    sts 0x045A, r24
    ldi r24, 0x00
    sts 0x045B, r24
    lds r24, 0x0376
    sts 0x045C, r24
    lds r24, 0x0377
    sts 0x045D, r24
    ldi r24, 0x00
    sts 0x045E, r24
    ldi r24, 0x00
    sts 0x045F, r24
    lds r24, 0x030E
    sts 0x0460, r24
    lds r24, 0x030F
    sts 0x0461, r24
    ldi r24, 0x00
    sts 0x0462, r24
    ldi r24, 0x00
    sts 0x0463, r24
    lds r24, 0x034E
    sts 0x0464, r24
    lds r24, 0x034F
    sts 0x0465, r24
    ldi r24, 0x00
    sts 0x0466, r24
    ldi r24, 0x00
    sts 0x0467, r24
    lds r24, 0x032E
    sts 0x0468, r24
    lds r24, 0x032F
    sts 0x0469, r24
    ldi r24, 0x00
    sts 0x046A, r24
    ldi r24, 0x00
    sts 0x046B, r24
    lds r24, 0x036E
    sts 0x046C, r24
    lds r24, 0x036F
    sts 0x046D, r24
    ldi r24, 0x00
    sts 0x046E, r24
    ldi r24, 0x00
    sts 0x046F, r24
    lds r24, 0x031E
    sts 0x0470, r24
    lds r24, 0x031F
    sts 0x0471, r24
    ldi r24, 0x00
    sts 0x0472, r24
    ldi r24, 0x00
    sts 0x0473, r24
    lds r24, 0x035E
    sts 0x0474, r24
    lds r24, 0x035F
    sts 0x0475, r24
    ldi r24, 0x00
    sts 0x0476, r24
    ldi r24, 0x00
    sts 0x0477, r24
    lds r24, 0x033E
    sts 0x0478, r24
    lds r24, 0x033F
    sts 0x0479, r24
    ldi r24, 0x00
    sts 0x047A, r24
    ldi r24, 0x00
    sts 0x047B, r24
    lds r24, 0x037E
    sts 0x047C, r24
    lds r24, 0x037F
    sts 0x047D, r24
    ldi r24, 0x00
    sts 0x047E, r24
    ldi r24, 0x00
    sts 0x047F, r24
    ldi r26, 0x80
    ldi r27, 0x03
    ldi r28, 0x80
    ldi r29, 0x04
    ldi r30, lo8(.Ltw)
    ldi r31, hi8(.Ltw)
    ldi r24, 0x20
    mov r3, r24
.Ldsp0:
    lpm r24, Z+
    sts 0x0B2C, r24
    lpm r24, Z+
    sts 0x0B2D, r24
    lpm r24, Z+
    sts 0x0B2E, r24
    lpm r24, Z+
    sts 0x0B2F, r24
    sbiw r30, 0x04
    ld r24, X+
    sts 0x0B30, r24
    ld r24, X+
    sts 0x0B31, r24
    ld r24, X+
    sts 0x0B32, r24
    ld r24, X+
    sts 0x0B33, r24
    ld r24, X+
    sts 0x0B34, r24
    ld r24, X+
    sts 0x0B35, r24
    ld r24, X+
    sts 0x0B36, r24
    ld r24, X+
    sts 0x0B37, r24
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B20, r21
    sts 0x0B21, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B22, r21
    sts 0x0B23, r22
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B24, r21
    sts 0x0B25, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B26, r21
    sts 0x0B27, r22
    lds r24, 0x0B20
    lds r25, 0x0B22
    sub r24, r25
    sts 0x0B28, r24
    lds r24, 0x0B21
    lds r25, 0x0B23
    sbc r24, r25
    sts 0x0B29, r24
    lds r24, 0x0B24
    lds r25, 0x0B26
    add r24, r25
    sts 0x0B2A, r24
    lds r24, 0x0B25
    lds r25, 0x0B27
    adc r24, r25
    sts 0x0B2B, r24
    lds r24, 0x0B31
    lds r25, 0x0B30
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B29
    lds r25, 0x0B28
    asr r24
    ror r25
    sts 0x0B29, r24
    sts 0x0B28, r25
    lds r24, 0x0B38
    lds r25, 0x0B28
    add r24, r25
    sts 0x0B20, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    adc r24, r25
    sts 0x0B21, r24
    lds r24, 0x0B38
    lds r25, 0x0B28
    sub r24, r25
    sts 0x0B22, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    sbc r24, r25
    sts 0x0B23, r24
    lds r24, 0x0B33
    lds r25, 0x0B32
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B2B
    lds r25, 0x0B2A
    asr r24
    ror r25
    sts 0x0B2B, r24
    sts 0x0B2A, r25
    lds r24, 0x0B38
    lds r25, 0x0B2A
    add r24, r25
    sts 0x0B24, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    adc r24, r25
    sts 0x0B25, r24
    lds r24, 0x0B38
    lds r25, 0x0B2A
    sub r24, r25
    sts 0x0B26, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    sbc r24, r25
    sts 0x0B27, r24
    lds r24, 0x0B20
    st Y+, r24
    lds r24, 0x0B21
    st Y+, r24
    lds r24, 0x0B24
    st Y+, r24
    lds r24, 0x0B25
    st Y+, r24
    lds r24, 0x0B22
    st Y+, r24
    lds r24, 0x0B23
    st Y+, r24
    lds r24, 0x0B26
    st Y+, r24
    lds r24, 0x0B27
    st Y+, r24
    dec r3
    breq .Lex0
    rjmp .Ldsp0
.Lex0:
    ldi r26, 0x80
    ldi r27, 0x04
    ldi r28, 0x80
    ldi r29, 0x05
    ldi r24, 0x10
    sts 0x0BD4, r24
.Ldsp1:
    ldi r30, lo8(.Ltw)
    ldi r31, hi8(.Ltw)
    ldi r24, 0x02
    mov r3, r24
.Ldsp2:
    lpm r24, Z+
    sts 0x0B2C, r24
    lpm r24, Z+
    sts 0x0B2D, r24
    lpm r24, Z+
    sts 0x0B2E, r24
    lpm r24, Z+
    sts 0x0B2F, r24
    adiw r30, 0x3C
    ld r24, X+
    sts 0x0B30, r24
    ld r24, X+
    sts 0x0B31, r24
    ld r24, X+
    sts 0x0B32, r24
    ld r24, X+
    sts 0x0B33, r24
    adiw r26, 0x04
    ld r24, X+
    sts 0x0B34, r24
    ld r24, X+
    sts 0x0B35, r24
    ld r24, X+
    sts 0x0B36, r24
    ld r24, X+
    sts 0x0B37, r24
    sbiw r26, 0x08
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B20, r21
    sts 0x0B21, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B22, r21
    sts 0x0B23, r22
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B24, r21
    sts 0x0B25, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B26, r21
    sts 0x0B27, r22
    lds r24, 0x0B20
    lds r25, 0x0B22
    sub r24, r25
    sts 0x0B28, r24
    lds r24, 0x0B21
    lds r25, 0x0B23
    sbc r24, r25
    sts 0x0B29, r24
    lds r24, 0x0B24
    lds r25, 0x0B26
    add r24, r25
    sts 0x0B2A, r24
    lds r24, 0x0B25
    lds r25, 0x0B27
    adc r24, r25
    sts 0x0B2B, r24
    lds r24, 0x0B31
    lds r25, 0x0B30
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B29
    lds r25, 0x0B28
    asr r24
    ror r25
    sts 0x0B29, r24
    sts 0x0B28, r25
    lds r24, 0x0B38
    lds r25, 0x0B28
    add r24, r25
    sts 0x0B20, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    adc r24, r25
    sts 0x0B21, r24
    lds r24, 0x0B38
    lds r25, 0x0B28
    sub r24, r25
    sts 0x0B22, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    sbc r24, r25
    sts 0x0B23, r24
    lds r24, 0x0B33
    lds r25, 0x0B32
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B2B
    lds r25, 0x0B2A
    asr r24
    ror r25
    sts 0x0B2B, r24
    sts 0x0B2A, r25
    lds r24, 0x0B38
    lds r25, 0x0B2A
    add r24, r25
    sts 0x0B24, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    adc r24, r25
    sts 0x0B25, r24
    lds r24, 0x0B38
    lds r25, 0x0B2A
    sub r24, r25
    sts 0x0B26, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    sbc r24, r25
    sts 0x0B27, r24
    lds r24, 0x0B20
    st Y+, r24
    lds r24, 0x0B21
    st Y+, r24
    lds r24, 0x0B24
    st Y+, r24
    lds r24, 0x0B25
    st Y+, r24
    adiw r28, 0x04
    lds r24, 0x0B22
    st Y+, r24
    lds r24, 0x0B23
    st Y+, r24
    lds r24, 0x0B26
    st Y+, r24
    lds r24, 0x0B27
    st Y+, r24
    sbiw r28, 0x08
    dec r3
    breq .Lex2
    rjmp .Ldsp2
.Lex2:
    adiw r26, 0x08
    adiw r28, 0x08
    lds r24, 0x0BD4
    dec r24
    sts 0x0BD4, r24
    breq .Lex1
    rjmp .Ldsp1
.Lex1:
    ldi r26, 0x80
    ldi r27, 0x05
    ldi r28, 0x80
    ldi r29, 0x06
    ldi r24, 0x08
    sts 0x0BD4, r24
.Ldsp3:
    ldi r30, lo8(.Ltw)
    ldi r31, hi8(.Ltw)
    ldi r24, 0x04
    mov r3, r24
.Ldsp4:
    lpm r24, Z+
    sts 0x0B2C, r24
    lpm r24, Z+
    sts 0x0B2D, r24
    lpm r24, Z+
    sts 0x0B2E, r24
    lpm r24, Z+
    sts 0x0B2F, r24
    adiw r30, 0x1C
    ld r24, X+
    sts 0x0B30, r24
    ld r24, X+
    sts 0x0B31, r24
    ld r24, X+
    sts 0x0B32, r24
    ld r24, X+
    sts 0x0B33, r24
    adiw r26, 0x0C
    ld r24, X+
    sts 0x0B34, r24
    ld r24, X+
    sts 0x0B35, r24
    ld r24, X+
    sts 0x0B36, r24
    ld r24, X+
    sts 0x0B37, r24
    sbiw r26, 0x10
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B20, r21
    sts 0x0B21, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B22, r21
    sts 0x0B23, r22
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B24, r21
    sts 0x0B25, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B26, r21
    sts 0x0B27, r22
    lds r24, 0x0B20
    lds r25, 0x0B22
    sub r24, r25
    sts 0x0B28, r24
    lds r24, 0x0B21
    lds r25, 0x0B23
    sbc r24, r25
    sts 0x0B29, r24
    lds r24, 0x0B24
    lds r25, 0x0B26
    add r24, r25
    sts 0x0B2A, r24
    lds r24, 0x0B25
    lds r25, 0x0B27
    adc r24, r25
    sts 0x0B2B, r24
    lds r24, 0x0B31
    lds r25, 0x0B30
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B29
    lds r25, 0x0B28
    asr r24
    ror r25
    sts 0x0B29, r24
    sts 0x0B28, r25
    lds r24, 0x0B38
    lds r25, 0x0B28
    add r24, r25
    sts 0x0B20, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    adc r24, r25
    sts 0x0B21, r24
    lds r24, 0x0B38
    lds r25, 0x0B28
    sub r24, r25
    sts 0x0B22, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    sbc r24, r25
    sts 0x0B23, r24
    lds r24, 0x0B33
    lds r25, 0x0B32
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B2B
    lds r25, 0x0B2A
    asr r24
    ror r25
    sts 0x0B2B, r24
    sts 0x0B2A, r25
    lds r24, 0x0B38
    lds r25, 0x0B2A
    add r24, r25
    sts 0x0B24, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    adc r24, r25
    sts 0x0B25, r24
    lds r24, 0x0B38
    lds r25, 0x0B2A
    sub r24, r25
    sts 0x0B26, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    sbc r24, r25
    sts 0x0B27, r24
    lds r24, 0x0B20
    st Y+, r24
    lds r24, 0x0B21
    st Y+, r24
    lds r24, 0x0B24
    st Y+, r24
    lds r24, 0x0B25
    st Y+, r24
    adiw r28, 0x0C
    lds r24, 0x0B22
    st Y+, r24
    lds r24, 0x0B23
    st Y+, r24
    lds r24, 0x0B26
    st Y+, r24
    lds r24, 0x0B27
    st Y+, r24
    sbiw r28, 0x10
    dec r3
    breq .Lex4
    rjmp .Ldsp4
.Lex4:
    adiw r26, 0x10
    adiw r28, 0x10
    lds r24, 0x0BD4
    dec r24
    sts 0x0BD4, r24
    breq .Lex3
    rjmp .Ldsp3
.Lex3:
    ldi r26, 0x80
    ldi r27, 0x06
    ldi r28, 0x80
    ldi r29, 0x07
    ldi r24, 0x04
    sts 0x0BD4, r24
.Ldsp5:
    ldi r30, lo8(.Ltw)
    ldi r31, hi8(.Ltw)
    ldi r24, 0x08
    mov r3, r24
.Ldsp6:
    lpm r24, Z+
    sts 0x0B2C, r24
    lpm r24, Z+
    sts 0x0B2D, r24
    lpm r24, Z+
    sts 0x0B2E, r24
    lpm r24, Z+
    sts 0x0B2F, r24
    adiw r30, 0x0C
    ld r24, X+
    sts 0x0B30, r24
    ld r24, X+
    sts 0x0B31, r24
    ld r24, X+
    sts 0x0B32, r24
    ld r24, X+
    sts 0x0B33, r24
    adiw r26, 0x1C
    ld r24, X+
    sts 0x0B34, r24
    ld r24, X+
    sts 0x0B35, r24
    ld r24, X+
    sts 0x0B36, r24
    ld r24, X+
    sts 0x0B37, r24
    sbiw r26, 0x20
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B20, r21
    sts 0x0B21, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B22, r21
    sts 0x0B23, r22
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B24, r21
    sts 0x0B25, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B26, r21
    sts 0x0B27, r22
    lds r24, 0x0B20
    lds r25, 0x0B22
    sub r24, r25
    sts 0x0B28, r24
    lds r24, 0x0B21
    lds r25, 0x0B23
    sbc r24, r25
    sts 0x0B29, r24
    lds r24, 0x0B24
    lds r25, 0x0B26
    add r24, r25
    sts 0x0B2A, r24
    lds r24, 0x0B25
    lds r25, 0x0B27
    adc r24, r25
    sts 0x0B2B, r24
    lds r24, 0x0B31
    lds r25, 0x0B30
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B29
    lds r25, 0x0B28
    asr r24
    ror r25
    sts 0x0B29, r24
    sts 0x0B28, r25
    lds r24, 0x0B38
    lds r25, 0x0B28
    add r24, r25
    sts 0x0B20, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    adc r24, r25
    sts 0x0B21, r24
    lds r24, 0x0B38
    lds r25, 0x0B28
    sub r24, r25
    sts 0x0B22, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    sbc r24, r25
    sts 0x0B23, r24
    lds r24, 0x0B33
    lds r25, 0x0B32
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B2B
    lds r25, 0x0B2A
    asr r24
    ror r25
    sts 0x0B2B, r24
    sts 0x0B2A, r25
    lds r24, 0x0B38
    lds r25, 0x0B2A
    add r24, r25
    sts 0x0B24, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    adc r24, r25
    sts 0x0B25, r24
    lds r24, 0x0B38
    lds r25, 0x0B2A
    sub r24, r25
    sts 0x0B26, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    sbc r24, r25
    sts 0x0B27, r24
    lds r24, 0x0B20
    st Y+, r24
    lds r24, 0x0B21
    st Y+, r24
    lds r24, 0x0B24
    st Y+, r24
    lds r24, 0x0B25
    st Y+, r24
    adiw r28, 0x1C
    lds r24, 0x0B22
    st Y+, r24
    lds r24, 0x0B23
    st Y+, r24
    lds r24, 0x0B26
    st Y+, r24
    lds r24, 0x0B27
    st Y+, r24
    sbiw r28, 0x20
    dec r3
    breq .Lex6
    rjmp .Ldsp6
.Lex6:
    adiw r26, 0x20
    adiw r28, 0x20
    lds r24, 0x0BD4
    dec r24
    sts 0x0BD4, r24
    breq .Lex5
    rjmp .Ldsp5
.Lex5:
    ldi r26, 0x80
    ldi r27, 0x07
    ldi r28, 0x80
    ldi r29, 0x08
    ldi r24, 0x02
    sts 0x0BD4, r24
.Ldsp7:
    ldi r30, lo8(.Ltw)
    ldi r31, hi8(.Ltw)
    ldi r24, 0x10
    mov r3, r24
.Ldsp8:
    lpm r24, Z+
    sts 0x0B2C, r24
    lpm r24, Z+
    sts 0x0B2D, r24
    lpm r24, Z+
    sts 0x0B2E, r24
    lpm r24, Z+
    sts 0x0B2F, r24
    adiw r30, 0x04
    ld r24, X+
    sts 0x0B30, r24
    ld r24, X+
    sts 0x0B31, r24
    ld r24, X+
    sts 0x0B32, r24
    ld r24, X+
    sts 0x0B33, r24
    adiw r26, 0x3C
    ld r24, X+
    sts 0x0B34, r24
    ld r24, X+
    sts 0x0B35, r24
    ld r24, X+
    sts 0x0B36, r24
    ld r24, X+
    sts 0x0B37, r24
    subi r26, 0x40
    sbci r27, 0x00
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B20, r21
    sts 0x0B21, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B22, r21
    sts 0x0B23, r22
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B24, r21
    sts 0x0B25, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B26, r21
    sts 0x0B27, r22
    lds r24, 0x0B20
    lds r25, 0x0B22
    sub r24, r25
    sts 0x0B28, r24
    lds r24, 0x0B21
    lds r25, 0x0B23
    sbc r24, r25
    sts 0x0B29, r24
    lds r24, 0x0B24
    lds r25, 0x0B26
    add r24, r25
    sts 0x0B2A, r24
    lds r24, 0x0B25
    lds r25, 0x0B27
    adc r24, r25
    sts 0x0B2B, r24
    lds r24, 0x0B31
    lds r25, 0x0B30
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B29
    lds r25, 0x0B28
    asr r24
    ror r25
    sts 0x0B29, r24
    sts 0x0B28, r25
    lds r24, 0x0B38
    lds r25, 0x0B28
    add r24, r25
    sts 0x0B20, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    adc r24, r25
    sts 0x0B21, r24
    lds r24, 0x0B38
    lds r25, 0x0B28
    sub r24, r25
    sts 0x0B22, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    sbc r24, r25
    sts 0x0B23, r24
    lds r24, 0x0B33
    lds r25, 0x0B32
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B2B
    lds r25, 0x0B2A
    asr r24
    ror r25
    sts 0x0B2B, r24
    sts 0x0B2A, r25
    lds r24, 0x0B38
    lds r25, 0x0B2A
    add r24, r25
    sts 0x0B24, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    adc r24, r25
    sts 0x0B25, r24
    lds r24, 0x0B38
    lds r25, 0x0B2A
    sub r24, r25
    sts 0x0B26, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    sbc r24, r25
    sts 0x0B27, r24
    lds r24, 0x0B20
    st Y+, r24
    lds r24, 0x0B21
    st Y+, r24
    lds r24, 0x0B24
    st Y+, r24
    lds r24, 0x0B25
    st Y+, r24
    adiw r28, 0x3C
    lds r24, 0x0B22
    st Y+, r24
    lds r24, 0x0B23
    st Y+, r24
    lds r24, 0x0B26
    st Y+, r24
    lds r24, 0x0B27
    st Y+, r24
    subi r28, 0x40
    sbci r29, 0x00
    dec r3
    breq .Lex8
    rjmp .Ldsp8
.Lex8:
    subi r26, 0xC0
    sbci r27, 0xFF
    subi r28, 0xC0
    sbci r29, 0xFF
    lds r24, 0x0BD4
    dec r24
    sts 0x0BD4, r24
    breq .Lex7
    rjmp .Ldsp7
.Lex7:
    ldi r26, 0x80
    ldi r27, 0x08
    ldi r28, 0x80
    ldi r29, 0x09
    ldi r30, lo8(.Ltw)
    ldi r31, hi8(.Ltw)
    ldi r24, 0x20
    mov r3, r24
.Ldsp9:
    lpm r24, Z+
    sts 0x0B2C, r24
    lpm r24, Z+
    sts 0x0B2D, r24
    lpm r24, Z+
    sts 0x0B2E, r24
    lpm r24, Z+
    sts 0x0B2F, r24
    ld r24, X+
    sts 0x0B30, r24
    ld r24, X+
    sts 0x0B31, r24
    ld r24, X+
    sts 0x0B32, r24
    ld r24, X+
    sts 0x0B33, r24
    subi r26, 0x84
    sbci r27, 0xFF
    ld r24, X+
    sts 0x0B34, r24
    ld r24, X+
    sts 0x0B35, r24
    ld r24, X+
    sts 0x0B36, r24
    ld r24, X+
    sts 0x0B37, r24
    subi r26, 0x80
    sbci r27, 0x00
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B20, r21
    sts 0x0B21, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B22, r21
    sts 0x0B23, r22
    lds r16, 0x0B2C
    lds r17, 0x0B2D
    lds r18, 0x0B36
    lds r19, 0x0B37
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B24, r21
    sts 0x0B25, r22
    lds r16, 0x0B2E
    lds r17, 0x0B2F
    lds r18, 0x0B34
    lds r19, 0x0B35
    mul r16, r18
    mov r20, r1
    clr r21
    clr r22
    mulsu r17, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    mulsu r19, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r20, r0
    adc r21, r1
    adc r22, r23
    muls r17, r19
    add r21, r0
    adc r22, r1
    lsl r20
    rol r21
    rol r22
    sts 0x0B26, r21
    sts 0x0B27, r22
    lds r24, 0x0B20
    lds r25, 0x0B22
    sub r24, r25
    sts 0x0B28, r24
    lds r24, 0x0B21
    lds r25, 0x0B23
    sbc r24, r25
    sts 0x0B29, r24
    lds r24, 0x0B24
    lds r25, 0x0B26
    add r24, r25
    sts 0x0B2A, r24
    lds r24, 0x0B25
    lds r25, 0x0B27
    adc r24, r25
    sts 0x0B2B, r24
    lds r24, 0x0B31
    lds r25, 0x0B30
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B29
    lds r25, 0x0B28
    asr r24
    ror r25
    sts 0x0B29, r24
    sts 0x0B28, r25
    lds r24, 0x0B38
    lds r25, 0x0B28
    add r24, r25
    sts 0x0B20, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    adc r24, r25
    sts 0x0B21, r24
    lds r24, 0x0B38
    lds r25, 0x0B28
    sub r24, r25
    sts 0x0B22, r24
    lds r24, 0x0B39
    lds r25, 0x0B29
    sbc r24, r25
    sts 0x0B23, r24
    lds r24, 0x0B33
    lds r25, 0x0B32
    asr r24
    ror r25
    sts 0x0B39, r24
    sts 0x0B38, r25
    lds r24, 0x0B2B
    lds r25, 0x0B2A
    asr r24
    ror r25
    sts 0x0B2B, r24
    sts 0x0B2A, r25
    lds r24, 0x0B38
    lds r25, 0x0B2A
    add r24, r25
    sts 0x0B24, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    adc r24, r25
    sts 0x0B25, r24
    lds r24, 0x0B38
    lds r25, 0x0B2A
    sub r24, r25
    sts 0x0B26, r24
    lds r24, 0x0B39
    lds r25, 0x0B2B
    sbc r24, r25
    sts 0x0B27, r24
    lds r24, 0x0B20
    st Y+, r24
    lds r24, 0x0B21
    st Y+, r24
    lds r24, 0x0B24
    st Y+, r24
    lds r24, 0x0B25
    st Y+, r24
    subi r28, 0x84
    sbci r29, 0xFF
    lds r24, 0x0B22
    st Y+, r24
    lds r24, 0x0B23
    st Y+, r24
    lds r24, 0x0B26
    st Y+, r24
    lds r24, 0x0B27
    st Y+, r24
    subi r28, 0x80
    sbci r29, 0x00
    dec r3
    breq .Lex9
    rjmp .Ldsp9
.Lex9:
    ldi r26, 0x80
    ldi r27, 0x09
    ldi r28, 0x80
    ldi r29, 0x0A
    ldi r24, 0x40
    sts 0x0B20, r24
.Ldsp10:
    ld r16, X+
    ld r17, X+
    ld r18, X+
    ld r19, X+
    mul r16, r16
    mov r4, r0
    mov r5, r1
    clr r6
    clr r7
    muls r17, r17
    add r6, r0
    adc r7, r1
    mulsu r17, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r5, r0
    adc r6, r1
    adc r7, r23
    mulsu r17, r16
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r5, r0
    adc r6, r1
    adc r7, r23
    mul r18, r18
    mov r8, r0
    mov r9, r1
    clr r10
    clr r11
    muls r19, r19
    add r10, r0
    adc r11, r1
    mulsu r19, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r9, r0
    adc r10, r1
    adc r11, r23
    mulsu r19, r18
    mov r23, r1
    lsl r23
    sbc r23, r23
    add r9, r0
    adc r10, r1
    adc r11, r23
    add r4, r8
    adc r5, r9
    adc r6, r10
    adc r7, r11
    clr r8
    clr r9
    clr r10
    clr r11
    clr r12
    clr r13
    clr r14
    ldi r25, 0x40
    mov r15, r25
    ldi r24, 0x10
    mov r3, r24
.Ldsp11:
    movw r16, r8
    movw r18, r10
    add r16, r12
    adc r17, r13
    adc r18, r14
    adc r19, r15
    cp r4, r16
    cpc r5, r17
    cpc r6, r18
    cpc r7, r19
    sbc r25, r25
    com r25
    and r16, r25
    and r17, r25
    and r18, r25
    and r19, r25
    sub r4, r16
    sbc r5, r17
    sbc r6, r18
    sbc r7, r19
    lsr r11
    ror r10
    ror r9
    ror r8
    movw r16, r12
    movw r18, r14
    and r16, r25
    and r17, r25
    and r18, r25
    and r19, r25
    add r8, r16
    adc r9, r17
    adc r10, r18
    adc r11, r19
    lsr r15
    ror r14
    ror r13
    ror r12
    lsr r15
    ror r14
    ror r13
    ror r12
    dec r3
    brne .Ldsp11
    st Y+, r8
    st Y+, r9
    lds r24, 0x0B20
    dec r24
    sts 0x0B20, r24
    breq .Lex10
    rjmp .Ldsp10
.Lex10:
    ldi r26, 0x20
    ldi r27, 0x0B
    ldi r24, 0x40
    mov r3, r24
.Ldsp12:
    st X+, r2
    dec r3
    brne .Ldsp12
    ldi r23, 0xFF
    ldi r28, 0x00
    ldi r29, 0x0B
    ldi r24, 0x08
    mov r8, r24
.Ldsp13:
    ldi r30, 0x80
    ldi r31, 0x0A
    ldi r26, 0x20
    ldi r27, 0x0B
    clr r4
    clr r5
    clr r6
    clr r7
    clr r20
    ldi r24, 0x40
    mov r3, r24
.Ldsp14:
    ld r16, Z+
    ld r17, Z+
    ld r18, X+
    cp r2, r18
    sbc r19, r19
    com r19
    cp r4, r16
    cpc r5, r17
    sbc r21, r21
    com r21
    and r21, r7
    com r21
    and r21, r19
    mov r22, r4
    eor r22, r16
    and r22, r21
    eor r4, r22
    mov r22, r5
    eor r22, r17
    and r22, r21
    eor r5, r22
    mov r22, r6
    eor r22, r20
    and r22, r21
    eor r6, r22
    mov r22, r7
    eor r22, r23
    and r22, r21
    eor r7, r22
    subi r20, 0xFF
    dec r3
    brne .Ldsp14
    ldi r26, 0x20
    ldi r27, 0x0B
    add r26, r6
    adc r27, r2
    st X, r8
    st Y+, r4
    st Y+, r5
    dec r8
    brne .Ldsp13
    lds r24, 0x0B00
    sts 0x0B10, r24
    lds r24, 0x0B01
    sts 0x0B11, r24
    lds r24, 0x0B02
    sts 0x0B12, r24
    lds r24, 0x0B03
    sts 0x0B13, r24
    lds r24, 0x0B04
    sts 0x0B14, r24
    lds r24, 0x0B05
    sts 0x0B15, r24
    lds r24, 0x0B06
    sts 0x0B16, r24
    lds r24, 0x0B07
    sts 0x0B17, r24
    lds r24, 0x0B08
    sts 0x0B18, r24
    lds r24, 0x0B09
    sts 0x0B19, r24
    lds r24, 0x0B0A
    sts 0x0B1A, r24
    lds r24, 0x0B0B
    sts 0x0B1B, r24
    lds r24, 0x0B0C
    sts 0x0B1C, r24
    lds r24, 0x0B0D
    sts 0x0B1D, r24
    lds r24, 0x0B0E
    sts 0x0B1E, r24
    lds r24, 0x0B0F
    sts 0x0B1F, r24
; BODY END
    cli
    clr r16
    sts 0x0BD6, r16
    lds r16, 0x0BD8
    inc r16
    sts 0x0BD8, r16
    lds r16, 0x0BD7
    tst r16
    brne periodic_done
    lds r16, 0x0BD8
    cpi r16, 4
    breq periodic_done
    sei
    jmp wait_for_tick

periodic_done:
    cli
; Terminal metadata is compiler-side structural evidence;
; no simulator SRAM readback is claimed.
; META completed_count_addr=0x0BD8
; META overrun_addr=0x0BD7
; META output_addr=0x0B10 output_len=16
    lds r16, 0x0BD8
    sts 0x0BD8, r16
    lds r16, 0x0BD7
    sts 0x0BD7, r16
    lds r16, 0x0B10
    sts 0x0B10, r16
    lds r16, 0x0B11
    sts 0x0B11, r16
    lds r16, 0x0B12
    sts 0x0B12, r16
    lds r16, 0x0B13
    sts 0x0B13, r16
    lds r16, 0x0B14
    sts 0x0B14, r16
    lds r16, 0x0B15
    sts 0x0B15, r16
    lds r16, 0x0B16
    sts 0x0B16, r16
    lds r16, 0x0B17
    sts 0x0B17, r16
    lds r16, 0x0B18
    sts 0x0B18, r16
    lds r16, 0x0B19
    sts 0x0B19, r16
    lds r16, 0x0B1A
    sts 0x0B1A, r16
    lds r16, 0x0B1B
    sts 0x0B1B, r16
    lds r16, 0x0B1C
    sts 0x0B1C, r16
    lds r16, 0x0B1D
    sts 0x0B1D, r16
    lds r16, 0x0B1E
    sts 0x0B1E, r16
    lds r16, 0x0B1F
    sts 0x0B1F, r16
    break

timer0_ovf_isr:
    push r16
    in r16, _SFR_IO_ADDR(SREG)
    push r16
    lds r16, 0x0BD6
    tst r16
    breq timer0_no_overrun
    ldi r16, 1
    sts 0x0BD7, r16
timer0_no_overrun:
    ldi r16, 1
    sts 0x0BD5, r16
    pop r16
    out _SFR_IO_ADDR(SREG), r16
    pop r16
    reti

; ---- constant data (program memory, never executed) ----
.Ltw:
    .word 0x7FFF
    .word 0x0000
    .word 0x7F61
    .word 0xF374
    .word 0x7D89
    .word 0xE707
    .word 0x7A7C
    .word 0xDAD8
    .word 0x7641
    .word 0xCF05
    .word 0x70E2
    .word 0xC3AA
    .word 0x6A6D
    .word 0xB8E4
    .word 0x62F1
    .word 0xAECD
    .word 0x5A82
    .word 0xA57E
    .word 0x5133
    .word 0x9D0F
    .word 0x471C
    .word 0x9593
    .word 0x3C56
    .word 0x8F1E
    .word 0x30FB
    .word 0x89BF
    .word 0x2528
    .word 0x8584
    .word 0x18F9
    .word 0x8277
    .word 0x0C8C
    .word 0x809F
    .word 0x0000
    .word 0x8001
    .word 0xF374
    .word 0x809F
    .word 0xE707
    .word 0x8277
    .word 0xDAD8
    .word 0x8584
    .word 0xCF05
    .word 0x89BF
    .word 0xC3AA
    .word 0x8F1E
    .word 0xB8E4
    .word 0x9593
    .word 0xAECD
    .word 0x9D0F
    .word 0xA57E
    .word 0xA57E
    .word 0x9D0F
    .word 0xAECD
    .word 0x9593
    .word 0xB8E4
    .word 0x8F1E
    .word 0xC3AA
    .word 0x89BF
    .word 0xCF05
    .word 0x8584
    .word 0xDAD8
    .word 0x8277
    .word 0xE707
    .word 0x809F
    .word 0xF374
