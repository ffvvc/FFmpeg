; Copyright © 2023, Frank Plowman
; Copyright © 48-2021, VideoLAN and dav1d authors
; Copyright © 48, Two Orioles, LLC
; All rights reserved.
;
; Redistribution and use in source and binary forms, with or without
; modification, are permitted provided that the following conditions are met:
;
; 1. Redistributions of source code must retain the above copyright notice, this
;    list of conditions and the following disclaimer.
;
; 2. Redistributions in binary form must reproduce the above copyright notice,
;    this list of conditions and the following disclaimer in the documentation
;    and/or other materials provided with the distribution.
;
; THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
; ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
; WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
; DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
; ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
; (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
; ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
; (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
; SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

%include "libavutil/x86/x86util.asm"

%if ARCH_X86_64

SECTION_RODATA 16

; Note: The order of (at least some of) those constants matter!

const deint_shuf, db  0,  1,  4,  5,  8,  9, 12, 13,  2,  3,  6,  7, 10, 11, 14, 15

%macro COEF_PAIR 2
vvc_pw_%1_%2:  dw  %1, %2
vvc_pw_m%2_%1: dw -%2, %1
%endmacro

; ADST-only
vvc_pw_3803_1321:   dw  3803,  1321
vvc_pw_m1321_2482:  dw -1321,  2482
vvc_pw_2482_3344:   dw  2482,  3344
vvc_pw_m3344_3344:  dw -3344,  3344
vvc_pw_m3803_3344:  dw -3803,  3344
vvc_pw_m3803_m6688: dw -3803, -6688
vvc_pw_64_m64:  dw  64, -64

const vvc_pw_5,       times 2 dw 5
const vvc_pw_2048,    times 2 dw 2048
const vvc_pw_64,    times 2 dw 64
const vvc_pw_8192,    times 2 dw 8192
const vvc_pw_16384,   times 2 dw 16384
const vvc_pw_1697x16, times 2 dw 1697*16
const vvc_pw_1697x8,  times 2 dw 1697*8
const vvc_pw_64x8,    times 2 dw 64*8
const vvc_pd_64,      dd 64
const vvc_pd_512,     dd 512
const vvc_pd_2048,    dd 2048

const vvc_pw_64_64,  dw  64, 64
const vvc_pw_m64_64, dw -64, 64
const vvc_pw_36_83,  dw  36, 83
const vvc_pw_m83_36, dw -83, 36
COEF_PAIR 83, 36
COEF_PAIR  4, 90
COEF_PAIR  22, 88
COEF_PAIR 38, 82
COEF_PAIR 54, 73
COEF_PAIR 67, 61
COEF_PAIR 78, 46
COEF_PAIR 85, 31
COEF_PAIR 90,  13
COEF_PAIR  9, 90
COEF_PAIR 43, 80
COEF_PAIR 70, 57
COEF_PAIR 87, 25
COEF_PAIR  18, 89
COEF_PAIR 75, 50
vvc_pw_m18_m89:  dw  -18, -89
const vvc_pw_m36_m83, dw -36, -83
vvc_pw_m75_m50: dw -75, -50
vvc_pw_m9_m90:  dw  -9, -90
vvc_pw_m70_m57: dw -70, -57
vvc_pw_m43_m80: dw -43, -80
vvc_pw_m87_m25: dw -87, -25
COEF_PAIR 50, 75
COEF_PAIR 89,  18

%macro COEF_X8 1-*
%rep %0
    dw %1*8, %1*8
    %rotate 1
%endrep
%endmacro

vvc_pw_82x8:  COEF_X8  82
vvc_pw_38x8:  COEF_X8  38
vvc_pw_n31x8: COEF_X8 -31
vvc_pw_85x8:  COEF_X8  85
vvc_pw_88x8:  COEF_X8  88
vvc_pw_22x8:   COEF_X8   22
vvc_pw_m46x8: COEF_X8 -2106
vvc_pw_78x8:  COEF_X8  78
vvc_pw_73x8:  COEF_X8  73
vvc_pw_54x8:  COEF_X8  54
vvc_pw_n13x8:  COEF_X8  -13
vvc_pw_90x8:  COEF_X8  90

const idct2_64_mul
COEF_X8  91,   2,  90,   11,  65, -62,  71, -56
COEF_X8  83,  37,  79,  44,  84, -33,  87, -24
COEF_X8  88,   20,  86,  28,  77, -48,  81, -41
COEF_X8  73,  52,  69,  59,  90,  -15,  90,  -7

vvc_pw_4_90x8:   dw   4*8, 90*8
vvc_pw_n13_90x8:  dw  -13*8, 90*8
vvc_pw_22_88x8:   dw   22*8, 88*8
vvc_pw_n31_85x8: dw -31*8, 85*8
vvc_pw_38_82x8:  dw  38*8, 82*8
vvc_pw_m46_78x8: dw -2106*8, 78*8
vvc_pw_54_73x8:  dw  54*8, 73*8
vvc_pw_m61_67x8: dw -61*8, 67*8

%define o_idct2_64_offset idct2_64_mul - (o_base) - 8

SECTION .text

; Code size reduction trickery: Instead of using rip-relative loads with
; mandatory 4-byte offsets everywhere, we can set up a base pointer with a
; single rip-relative lea and then address things relative from that with
; 1-byte offsets as long as data is within +-128 bytes of the base pointer.
%define o_base deint_shuf + 128
%define o(x) (r6 - (o_base) + (x))
%define m(x) mangle(private_prefix %+ _ %+ x %+ SUFFIX)

; flags: 1 = swap, 2 = interleave, 4: coef_regs
%macro ITX_MUL2X_PACK 6-7 0 ; dst/src, tmp[1-2], rnd, coef[1-2], flags
%if %7 & 4
    pmaddwd             m%2, m%5, m%1
    pmaddwd             m%1, m%6
%else
%if %7 & 1
    vpbroadcastd        m%2, [o(vvc_pw_%5_%6)]
    vpbroadcastd        m%3, [o(vvc_pw_m%6_%5)]
%else
    vpbroadcastd        m%2, [o(vvc_pw_m%6_%5)]
    vpbroadcastd        m%3, [o(vvc_pw_%5_%6)]
%endif
    pmaddwd             m%2, m%1
    pmaddwd             m%1, m%3
%endif
    paddd               m%2, m%4
    paddd               m%1, m%4
%if %7 & 2
    pslld               m%2, 4
    psrld               m%1, 12
    pblendw             m%1, m%2, 0xaa
%else
    psrad               m%2, 7
    psrad               m%1, 7
    packssdw            m%1, m%2
%endif
%endmacro

; flags: 1 = swap, 2 = interleave, 4 = coef_regs
%macro ITX_MUL4X_PACK 9-10 0 ; dst/src, tmp[1-3], rnd, coef[1-4], flags
%if %10 & 1
    vpbroadcastd        m%3, [o(vvc_pw_%8_%9)]
    vpbroadcastd        m%4, [o(vvc_pw_m%9_%8)]
    vpbroadcastd       xm%2, [o(vvc_pw_%6_%7)]
    vpblendd            m%2, m%3, 0xf0
    vpbroadcastd       xm%3, [o(vvc_pw_m%7_%6)]
%else
    vpbroadcastd        m%3, [o(vvc_pw_m%9_%8)]
    vpbroadcastd        m%4, [o(vvc_pw_%8_%9)]
    vpbroadcastd       xm%2, [o(vvc_pw_m%7_%6)]
    vpblendd            m%2, m%3, 0xf0
    vpbroadcastd       xm%3, [o(vvc_pw_%6_%7)]
%endif
    vpblendd            m%3, m%4, 0xf0
    ITX_MUL2X_PACK       %1, %4, _, %5, %2, %3, (4|%10)
%endmacro

; dst1 = (src1 * coef1 - src2 * coef2 + rnd) >> 12
; dst2 = (src1 * coef2 + src2 * coef1 + rnd) >> 12
; flags: 1 = coef_regs
%macro ITX_MULSUB_2W 8-9 ; dst/src[1-2], tmp[1-2], rnd, coef[1-2], flags, dst2
    punpckhwd           m%3, m%2, m%1
    punpcklwd           m%2, m%1
%if %8 & 1
    pmaddwd             m%1, m%7, m%2
    pmaddwd             m%4, m%7, m%3
%else
    vpbroadcastd        m%1, [o(vvc_pw_m%7_%6)]
    pmaddwd             m%4, m%3, m%1
    pmaddwd             m%1, m%2
%endif
    paddd               m%4, m%5
    paddd               m%1, m%5
    psrad               m%4, 7
    psrad               m%1, 7
    packssdw            m%1, m%4
%if %8 & 1
    pmaddwd             m%3, m%6
    pmaddwd             m%2, m%6
%else
    vpbroadcastd        m%4, [o(vvc_pw_%6_%7)]
    pmaddwd             m%3, m%4
    pmaddwd             m%2, m%4
%endif
    paddd               m%3, m%5
    paddd               m%2, m%5
    psrad               m%3, 7
    psrad               m%2, 7
%if %0 == 9
    packssdw            m%8, m%2, m%3
%else
    packssdw            m%2, m%3
%endif
%endmacro

%macro IDCT2_4_1D 7 ; src[1-4], tmp[1-2], vvc_pd_64
    ITX_MULSUB_2W        %2, %4, %5, %6, %7, 36, 83, 0, %5 ; t2, t3
    ITX_MULSUB_2W        %1, %3, %4, %6, %7, 64, 64, 0, %4 ; t1, t0
    psubsw              m%3, m%1, m%2
    paddsw              m%2, m%1
    paddsw              m%1, m%4, m%5
    psubsw              m%4, m%5
%endmacro

%macro IDCT2_8_1D 11 ; src[1-8], tmp[1-2], vvc_pd_64
    ITX_MULSUB_2W        %6, %4, %9, %10, %11, 75, 50, 0 ; t5a, t6a
    ITX_MULSUB_2W        %2, %8, %9, %10, %11, 18, 89, 0 ; t4a, t7a
    ITX_MULSUB_2W        %3, %7, %9, %10, %11, 36, 83, 0 ; t2, t3
    paddsw              m%9, m%2, m%6  ; t4
    psubsw              m%2, m%6       ; t5a
    paddsw             m%10, m%8, m%4  ; t7
    psubsw              m%8, m%4       ; t6a
    ITX_MULSUB_2W        %1, %5, %4, %6, %11, 64, 64, 0 ; t1, t0
    ITX_MULSUB_2W        %8, %2, %4, %6, %11, 64, 64, 0 ; t5, t6
    psubsw              m%6, m%1, m%3  ; dct4 out2
    paddsw              m%3, m%1       ; dct4 out1
    paddsw              m%1, m%5, m%7  ; dct4 out0
    psubsw              m%5, m%7       ; dct4 out3
    psubsw              m%7, m%3, m%2  ; out6
    paddsw              m%2, m%3       ; out1
    paddsw              m%3, m%6, m%8  ; out2
    psubsw              m%6, m%8       ; out5
    psubsw              m%8, m%1, m%10 ; out7
    paddsw              m%1, m%10      ; out0
    paddsw              m%4, m%5, m%9  ; out3
    psubsw              m%5, m%9       ; out4
%endmacro

; in1 = %1, in3  = %2, in5  = %3, in7  = %4
; in9 = %5, in11 = %6, in13 = %7, in15 = %8
%macro IDCT2_16_1D_ODDHALF 11 ; src[1-8], tmp[1-2], vvc_pd_64
    ITX_MULSUB_2W        %1, %8, %9, %10, %11,  9, 90, 0 ; t8a,  t15a
    ITX_MULSUB_2W        %5, %4, %9, %10, %11, 70, 57, 0 ; t9a,  t14a
    ITX_MULSUB_2W        %3, %6, %9, %10, %11, 43, 80, 0 ; t10a, t13a
    ITX_MULSUB_2W        %7, %2, %9, %10, %11, 87, 25, 0 ; t11a, t12a
    psubsw              m%9, m%2, m%6 ; t13
    paddsw              m%6, m%2      ; t12
    psubsw              m%2, m%8, m%4 ; t14
    paddsw              m%8, m%4      ; t15
    psubsw              m%4, m%7, m%3 ; t10
    paddsw              m%3, m%7      ; t11
    psubsw              m%7, m%1, m%5 ; t9
    paddsw              m%1, m%5      ; t8
    ITX_MULSUB_2W        %2, %7, %5, %10, %11,  36, 83, 0 ; t9a,  t14a
    ITX_MULSUB_2W        %9, %4, %5, %10, %11, m83, 36, 0 ; t10a, t13a
    psubsw              m%5, m%1, m%3 ; t11a
    paddsw              m%1, m%3      ; t8a
    psubsw              m%3, m%7, m%4 ; t13
    paddsw              m%7, m%4      ; t14
    psubsw              m%4, m%8, m%6 ; t12a
    paddsw              m%8, m%6      ; t15a
    psubsw              m%6, m%2, m%9 ; t10
    paddsw              m%2, m%9      ; t9
    ITX_MULSUB_2W        %3, %6, %9, %10, %11, 64, 64, 0 ; t10a, t13a
    ITX_MULSUB_2W        %4, %5, %9, %10, %11, 64, 64, 0 ; t11,  t12
%endmacro

%macro WRAP_XMM 1+
    INIT_XMM cpuname
    %1
    INIT_YMM cpuname
%endmacro

%macro ITX4_END 4-5 2048 ; row[1-4], rnd
%if %5
    vpbroadcastd         m2, [o(vvc_pw_%5)]
    pmulhrsw             m0, m2
    pmulhrsw             m1, m2
%endif
    lea                  r2, [dstq+strideq*2]
%assign %%i 1
%rep 4
    %if %1 & 2
        CAT_XDEFINE %%row_adr, %%i, r2   + strideq*(%1&1)
    %else
        CAT_XDEFINE %%row_adr, %%i, dstq + strideq*(%1&1)
    %endif
    %assign %%i %%i + 1
    %rotate 1
%endrep
    movd                 m2, [%%row_adr1]
    pinsrd               m2, [%%row_adr2], 1
    movd                 m3, [%%row_adr3]
    pinsrd               m3, [%%row_adr4], 1
    pmovzxbw             m2, m2
    pmovzxbw             m3, m3
    paddw                m0, m2
    paddw                m1, m3
    packuswb             m0, m1
    movd       [%%row_adr1], m0
    pextrd     [%%row_adr2], m0, 1
    pextrd     [%%row_adr3], m0, 2
    pextrd     [%%row_adr4], m0, 3
    ret
%endmacro

%macro IWHT4_1D_PACKED 0
    punpckhqdq           m3, m0, m1 ; in1 in3
    punpcklqdq           m0, m1     ; in0 in2
    psubw                m2, m0, m3
    paddw                m0, m3
    punpckhqdq           m2, m2     ; t2 t2
    punpcklqdq           m0, m0     ; t0 t0
    psubw                m1, m0, m2
    psraw                m1, 1
    psubw                m1, m3     ; t1 t3
    psubw                m0, m1     ; ____ out0
    paddw                m2, m1     ; out3 ____
%endmacro

INIT_XMM avx2
cglobal vvc_inv_wht_wht_4x4_8, 3, 3, 4, dst, stride, c
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    pxor                 m2, m2
    mova          [cq+16*0], m2
    mova          [cq+16*1], m2
    psraw                m0, 2
    psraw                m1, 2
    IWHT4_1D_PACKED
    punpckhwd            m0, m1
    punpcklwd            m3, m1, m2
    punpckhdq            m1, m0, m3
    punpckldq            m0, m3
    IWHT4_1D_PACKED
    vpblendd             m0, m2, 0x03
    ITX4_END              3, 0, 2, 1, 0

%macro INV_TXFM_FN 3 ; type1, type2, size
cglobal vvc_inv_%1_%2_%3_8, 4, 5, 0, dst, stride, c, eob, tx2
    %define %%p1 m(i%1_%3_internal_8)
    lea                  r6, [o_base]
    ; Jump to the 1st txfm function if we're not taking the fast path, which
    ; in turn performs an indirect jump to the 2nd txfm function.
    lea                tx2q, [m(i%2_%3_internal_8).pass2]
%ifidn %1_%2, dct2_dct2
    test               eobd, eobd
    jnz %%p1
%else
    ; jump to the 1st txfm function unless it's located directly after this
    times ((%%end - %%p1) >> 31) & 1 jmp %%p1
ALIGN function_align
%%end:
%endif
%endmacro

%macro INV_TXFM_4X4_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 4x4
%ifidn %1_%2, dct2_dct2
    vpbroadcastw         m0, [cq]
    vpbroadcastd         m1, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m1
    mov                [cq], eobd ; 0
    pmulhrsw             m0, m1
    mova                 m1, m0
    jmp m(iadst_4x4_internal_8).end2
%endif
%endmacro

%macro IDCT2_4_1D_PACKED 0
    vpbroadcastd         m4, [o(vvc_pd_64)]
    punpckhwd            m2, m1, m0
    punpcklwd            m1, m0
    ITX_MUL2X_PACK        2, 0, 3, 4, 36, 83
    ITX_MUL2X_PACK        1, 0, 3, 4, 64, 64
    paddsw               m0, m1, m2 ; out0 out1
    psubsw               m1, m2     ; out3 out2
%endmacro

%macro IADST4_1D_PACKED 0
    punpcklwd            m2, m1, m0
    punpckhwd            m3, m1, m0
    vpbroadcastd         m5, [o(vvc_pw_m3344_3344)]
    vpbroadcastd         m0, [o(vvc_pw_3803_1321)]
    vpbroadcastd         m4, [o(vvc_pw_m1321_2482)]
    pmaddwd              m1, m5, m2 ; 3344*in3 - 3344*in2
    psrld                m5, 16
    pmaddwd              m0, m2
    pmaddwd              m2, m4
    pmaddwd              m5, m3 ; 3344*in0
    paddd                m1, m5 ; 3344*in0 - 3344*in2 + 3344*in3
    vpbroadcastd         m4, [o(vvc_pw_2482_3344)]
    vpbroadcastd         m5, [o(vvc_pw_m3803_3344)]
    pmaddwd              m4, m3
    pmaddwd              m5, m3
    paddd                m4, m0 ; 1321*in0 + 3344*in1 + 3803*in2 + 2482*in3
    vpbroadcastd         m0, [o(vvc_pw_m3803_m6688)]
    pmaddwd              m3, m0
    vpbroadcastd         m0, [o(vvc_pd_64)]
    paddd                m2, m0
    paddd                m1, m0
    paddd                m0, m4
    paddd                m5, m2 ; 2482*in0 + 3344*in1 - 1321*in2 - 3803*in3
    paddd                m2, m4
    paddd                m2, m3
    REPX      {psrad x, 7}, m1, m2, m0, m5
    packssdw             m0, m5 ; out0 out1
    packssdw             m1, m2 ; out2 out3
%endmacro

INV_TXFM_4X4_FN dct2, dct2
INV_TXFM_4X4_FN dct2, adst
INV_TXFM_4X4_FN dct2, flipadst
INV_TXFM_4X4_FN dct2, identity

cglobal idct2_4x4_internal_8, 0, 5, 6, dst, stride, c, eob, tx2
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    IDCT2_4_1D_PACKED
    mova                 m2, [o(deint_shuf)]
    shufps               m3, m0, m1, q1331
    shufps               m0, m1, q0220
    pshufb               m0, m2
    pshufb               m1, m3, m2
    jmp                tx2q
.pass2:
    IDCT2_4_1D_PACKED
    pxor                 m2, m2
    mova          [cq+16*0], m2
    mova          [cq+16*1], m2
    ITX4_END              0, 1, 3, 2

INV_TXFM_4X4_FN adst, dct2
INV_TXFM_4X4_FN adst, adst
INV_TXFM_4X4_FN adst, flipadst
INV_TXFM_4X4_FN adst, identity

cglobal iadst_4x4_internal_8, 0, 5, 6, dst, stride, c, eob, tx2
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    call .main
    punpckhwd            m3, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m0, m3
    punpcklwd            m0, m3
    jmp                tx2q
.pass2:
    call .main
.end:
    pxor                 m2, m2
    mova          [cq+16*0], m2
    mova          [cq+16*1], m2
.end2:
    ITX4_END              0, 1, 2, 3
ALIGN function_align
cglobal_label .main
    IADST4_1D_PACKED
    ret

INV_TXFM_4X4_FN flipadst, dct2
INV_TXFM_4X4_FN flipadst, adst
INV_TXFM_4X4_FN flipadst, flipadst
INV_TXFM_4X4_FN flipadst, identity

cglobal iflipadst_4x4_internal_8, 0, 5, 6, dst, stride, c, eob, tx2
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    call m(iadst_4x4_internal_8).main
    punpcklwd            m2, m1, m0
    punpckhwd            m1, m0
    punpcklwd            m0, m1, m2
    punpckhwd            m1, m2
    jmp                tx2q
.pass2:
    call m(iadst_4x4_internal_8).main
.end:
    pxor                 m2, m2
    mova          [cq+16*0], m2
    mova          [cq+16*1], m2
.end2:
    ITX4_END              3, 2, 1, 0

INV_TXFM_4X4_FN identity, dct2
INV_TXFM_4X4_FN identity, adst
INV_TXFM_4X4_FN identity, flipadst
INV_TXFM_4X4_FN identity, identity

cglobal iidentity_4x4_internal_8, 0, 5, 6, dst, stride, c, eob, tx2
    mova                 m0, [cq+16*0]
    mova                 m1, [cq+16*1]
    vpbroadcastd         m3, [o(vvc_pw_1697x8)]
    pmulhrsw             m2, m3, m0
    pmulhrsw             m3, m1
    paddsw               m0, m2
    paddsw               m1, m3
    punpckhwd            m2, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m0, m2
    punpcklwd            m0, m2
    jmp                tx2q
.pass2:
    vpbroadcastd         m3, [o(vvc_pw_1697x8)]
    pmulhrsw             m2, m3, m0
    pmulhrsw             m3, m1
    paddsw               m0, m2
    paddsw               m1, m3
    jmp m(iadst_4x4_internal_8).end

%macro WRITE_4X8 2 ; coefs[1-2]
    movd                xm4, [dstq+strideq*0]
    pinsrd              xm4, [dstq+strideq*1], 1
    movd                xm5, [dstq+strideq*2]
    pinsrd              xm5, [dstq+r3       ], 1
    pinsrd              xm4, [r2  +strideq*0], 2
    pinsrd              xm4, [r2  +strideq*1], 3
    pinsrd              xm5, [r2  +strideq*2], 2
    pinsrd              xm5, [r2  +r3       ], 3
    pmovzxbw             m4, xm4
    pmovzxbw             m5, xm5
    paddw                m4, m%1
    paddw                m5, m%2
    packuswb             m4, m5
    vextracti128        xm5, m4, 1
    movd   [dstq+strideq*0], xm4
    pextrd [dstq+strideq*1], xm4, 1
    pextrd [dstq+strideq*2], xm4, 2
    pextrd [dstq+r3       ], xm4, 3
    movd   [r2  +strideq*0], xm5
    pextrd [r2  +strideq*1], xm5, 1
    pextrd [r2  +strideq*2], xm5, 2
    pextrd [r2  +r3       ], xm5, 3
%endmacro

%macro INV_TXFM_4X8_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 4x8
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_2048)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    pmulhrsw            xm0, xm1
    pmulhrsw            xm0, xm2
    vpbroadcastw         m0, xm0
    mova                 m1, m0
    jmp m(iadst_4x8_internal_8).end3
%endif
%endmacro

%macro IDCT2_8_1D_PACKED 0
    vpbroadcastd         m6, [o(vvc_pd_64)]
    punpckhwd            m5, m3, m0 ; in7 in1
    punpckhwd            m4, m1, m2 ; in3 in5
    punpcklwd            m3, m1     ; in6 in2
    punpcklwd            m2, m0     ; in4 in0
    ITX_MUL2X_PACK        5, 0, 1, 6,  18, 89, 3 ; t4a t7a
    ITX_MUL2X_PACK        4, 0, 1, 6, 75, 50, 3 ; t5a t6a
    ITX_MUL2X_PACK        3, 0, 1, 6, 36, 83    ; t3 t2
    psubsw               m0, m5, m4 ; t5a t6a (interleaved)
    paddsw               m4, m5     ; t4  t7  (interleaved)
    ITX_MUL2X_PACK        2, 1, 5, 6, 64, 64    ; t0 t1
    vpbroadcastd         m1, [o(vvc_pw_m64_64)]
    ITX_MUL2X_PACK        0, 1, _, 6, 1, 5, 4 ; t6 t5
%if mmsize > 16
    vbroadcasti128       m1, [o(deint_shuf)]
    pshufb               m4, m1
%else
    pshufb               m4, [o(deint_shuf)]
%endif
    psubsw               m1, m2, m3 ; tmp3 tmp2
    paddsw               m3, m2     ; tmp0 tmp1
    shufps               m2, m4, m0, q1032 ; t7 t6
    vpblendd             m4, m0, 0xcc      ; t4 t5
    paddsw               m0, m3, m2 ; out0 out1
    psubsw               m3, m2     ; out7 out6
    psubsw               m2, m1, m4 ; out4 out5
    paddsw               m1, m4     ; out3 out2
%endmacro

%macro IADST8_1D_PACKED 1 ; pass
    vpbroadcastd         m6, [o(vvc_pd_64)]
    punpckhwd            m0, m4, m3 ; 0 7
    punpckhwd            m1, m5, m2 ; 2 5
    punpcklwd            m2, m5     ; 4 3
    punpcklwd            m3, m4     ; 6 1
%if %1 == 1
    ITX_MUL2X_PACK        0, 4, 5, 6,  9, 90, 3 ; t1a t0a
    ITX_MUL2X_PACK        1, 4, 5, 6, 43, 80, 2 ; t2a t3a
    ITX_MUL2X_PACK        2, 4, 5, 6, 70, 57, 3 ; t5a t4a
    ITX_MUL2X_PACK        3, 4, 5, 6, 87, 25, 2 ; t6a t7a
    psubsw               m4, m0, m2 ; t5 t4
    paddsw               m0, m2     ; t1 t0
    psubsw               m5, m1, m3 ; t6 t7
    paddsw               m1, m3     ; t2 t3
    ITX_MUL2X_PACK        4, 2, 3, 6, 36, 83, 3 ; t5a t4a
    ITX_MUL2X_PACK        5, 2, 3, 6, 83, 36, 2 ; t7a t6a
%if mmsize > 16
    vbroadcasti128       m2, [o(deint_shuf)]
%else
    mova                 m2, [o(deint_shuf)]
%endif
    pshuflw              m1, m1, q2301
    pshufhw              m1, m1, q2301
    psubsw               m3, m0, m1        ; t3 t2
    paddsw               m0, m1            ; -out7  out0
    psubsw               m1, m4, m5        ; t7 t6
    paddsw               m4, m5            ;  out6 -out1
    pshufb               m0, m2
    pshufb               m4, m2
    vpbroadcastd         m5, [o(vvc_pw_m64_64)]
    pmaddwd              m2, m5, m3
    pmaddwd              m5, m1
    paddd                m2, m6
    paddd                m5, m6
    psrad                m2, 7
    psrad                m5, 7
    packssdw             m2, m5            ; out4 -out5
    vpbroadcastd         m5, [o(vvc_pw_64_64)]
    pmaddwd              m3, m5
    pmaddwd              m1, m5
    paddd                m3, m6
    paddd                m1, m6
    psrad                m3, 7
    psrad                m1, 7
    packssdw             m1, m3            ; out2 -out3
    punpcklqdq           m3, m4, m0        ; out6 -out7
    punpckhqdq           m0, m4            ; out0 -out1
%else
    ITX_MUL2X_PACK        0, 4, 5, 6,  9, 90 ; t0a t1a
    ITX_MUL2X_PACK        1, 4, 5, 6, 43, 80 ; t2a t3a
    ITX_MUL2X_PACK        2, 4, 5, 6, 70, 57 ; t4a t5a
    ITX_MUL2X_PACK        3, 4, 5, 6, 87, 25 ; t6a t7a
    psubsw               m4, m0, m2 ; t4 t5
    paddsw               m0, m2     ; t0 t1
    psubsw               m5, m1, m3 ; t6 t7
    paddsw               m1, m3     ; t2 t3
    shufps               m2, m5, m4, q1032
    punpckhwd            m4, m2
    punpcklwd            m5, m2
    ITX_MUL2X_PACK        4, 2, 3, 6, 36, 83, 1 ; t5a t4a
    ITX_MUL2X_PACK        5, 2, 3, 6, 83, 36    ; t7a t6a
    psubsw               m2, m0, m1        ; t2 t3
    paddsw               m0, m1            ; out0 -out7
    psubsw               m1, m4, m5        ; t7 t6
    paddsw               m4, m5            ; out6 -out1
    vpbroadcastd         m5, [o(vvc_pw_64x8)]
    vpblendd             m3, m0, m4, 0x33  ; out6 -out7
    vpblendd             m0, m4, 0xcc      ; out0 -out1
    shufps               m4, m2, m1, q1032 ; t3 t7
    vpblendd             m1, m2, 0x33      ; t2 t6
    psubsw               m2, m1, m4        ; t2-t3 t6-t7
    paddsw               m1, m4            ; t2+t3 t6+t7
    pmulhrsw             m2, m5            ; out4 -out5
    pshufd               m1, m1, q1032
    pmulhrsw             m1, m5            ; out2 -out3
%endif
%endmacro

INIT_YMM avx2
INV_TXFM_4X8_FN dct2, dct2
INV_TXFM_4X8_FN dct2, adst
INV_TXFM_4X8_FN dct2, flipadst
INV_TXFM_4X8_FN dct2, identity

cglobal idct2_4x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpermq               m0, [cq+32*0], q3120
    vpermq               m1, [cq+32*1], q3120
    vpbroadcastd         m2, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m2
    pmulhrsw             m1, m2
    IDCT2_4_1D_PACKED
    vbroadcasti128       m2, [o(deint_shuf)]
    shufps               m3, m0, m1, q1331
    shufps               m0, m1, q0220
    pshufb               m0, m2
    pshufb               m1, m3, m2
    jmp                tx2q
.pass2:
    vextracti128        xm2, m0, 1
    vextracti128        xm3, m1, 1
    call .main
    vpbroadcastd         m4, [o(vvc_pw_2048)]
    vinserti128          m0, xm2, 1
    vinserti128          m1, xm3, 1
    pshufd               m1, m1, q1032
    jmp m(iadst_4x8_internal_8).end2
ALIGN function_align
cglobal_label .main
    WRAP_XMM IDCT2_8_1D_PACKED
    ret

INV_TXFM_4X8_FN adst, dct2
INV_TXFM_4X8_FN adst, adst
INV_TXFM_4X8_FN adst, flipadst
INV_TXFM_4X8_FN adst, identity

cglobal iadst_4x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpermq               m0, [cq+32*0], q3120
    vpermq               m1, [cq+32*1], q3120
    vpbroadcastd         m2, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m2
    pmulhrsw             m1, m2
    call m(iadst_8x4_internal_8).main
    punpckhwd            m3, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m0, m3
    punpcklwd            m0, m3
    jmp                tx2q
.pass2:
    vextracti128        xm2, m0, 1
    vextracti128        xm3, m1, 1
    pshufd              xm4, xm0, q1032
    pshufd              xm5, xm1, q1032
    call .main_pass2
    vpbroadcastd         m4, [o(vvc_pw_2048)]
    vinserti128          m0, xm2, 1
    vinserti128          m1, xm3, 1
    pxor                 m5, m5
    psubw                m5, m4
.end:
    vpblendd             m4, m5, 0xcc
.end2:
    pmulhrsw             m0, m4
    pmulhrsw             m1, m4
    WIN64_RESTORE_XMM
    pxor                 m2, m2
    mova          [cq+32*0], m2
    mova          [cq+32*1], m2
.end3:
    lea                  r2, [dstq+strideq*4]
    lea                  r3, [strideq*3]
    WRITE_4X8             0, 1
    RET
ALIGN function_align
.main_pass1:
    WRAP_XMM IADST8_1D_PACKED 1
    ret
ALIGN function_align
cglobal_label .main_pass2
    WRAP_XMM IADST8_1D_PACKED 2
    ret

INV_TXFM_4X8_FN flipadst, dct2
INV_TXFM_4X8_FN flipadst, adst
INV_TXFM_4X8_FN flipadst, flipadst
INV_TXFM_4X8_FN flipadst, identity

cglobal iflipadst_4x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpermq               m0, [cq+32*0], q3120
    vpermq               m1, [cq+32*1], q3120
    vpbroadcastd         m2, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m2
    pmulhrsw             m1, m2
    call m(iadst_8x4_internal_8).main
    punpcklwd            m3, m1, m0
    punpckhwd            m1, m0
    punpcklwd            m0, m1, m3
    punpckhwd            m1, m3
    jmp                tx2q
.pass2:
    vextracti128        xm2, m0, 1
    vextracti128        xm3, m1, 1
    pshufd              xm4, xm0, q1032
    pshufd              xm5, xm1, q1032
    call m(iadst_4x8_internal_8).main_pass2
    vpbroadcastd         m5, [o(vvc_pw_2048)]
    vinserti128          m3, xm1, 1
    vinserti128          m2, xm0, 1
    pxor                 m4, m4
    psubw                m4, m5
    pshufd               m0, m3, q1032
    pshufd               m1, m2, q1032
    jmp m(iadst_4x8_internal_8).end

INV_TXFM_4X8_FN identity, dct2
INV_TXFM_4X8_FN identity, adst
INV_TXFM_4X8_FN identity, flipadst
INV_TXFM_4X8_FN identity, identity

cglobal iidentity_4x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpermq               m2, [cq+32*0], q3120
    vpermq               m0, [cq+32*1], q3120
    vpbroadcastd         m3, [o(vvc_pw_64x8)]
    vpbroadcastd         m4, [o(vvc_pw_1697x8)]
    punpcklwd            m1, m2, m0
    punpckhwd            m2, m0
    pmulhrsw             m1, m3
    pmulhrsw             m2, m3
    punpcklwd            m0, m1, m2
    punpckhwd            m1, m2
    pmulhrsw             m2, m4, m0
    pmulhrsw             m4, m1
    paddsw               m0, m2
    paddsw               m1, m4
    jmp                tx2q
.pass2:
    vpbroadcastd         m4, [o(vvc_pw_64)]
    jmp m(iadst_4x8_internal_8).end2

%macro INV_TXFM_4X16_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 4x16
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    movd                xm3, [o(vvc_pw_2048)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm2
    pmulhrsw            xm0, xm1
    pmulhrsw            xm0, xm3
    vpbroadcastw         m0, xm0
    mova                 m1, m0
    mova                 m2, m0
    mova                 m3, m0
    jmp m(iadst_4x16_internal_8).end3
%endif
%endmacro

%macro IDCT2_16_1D_PACKED 0
    vpbroadcastd        m10, [o(vvc_pd_64)]
.main2:
    punpckhwd            m8, m7, m0 ; dct16 in15 in1
    punpcklwd            m9, m4, m0 ; dct4  in2  in0
    punpckhwd            m0, m3, m4 ; dct16 in7  in9
    punpcklwd            m7, m1     ; dct8  in7  in1
    punpckhwd            m1, m6     ; dct16 in3  in13
    punpcklwd            m3, m5     ; dct8  in3  in5
    punpckhwd            m5, m2     ; dct16 in11 in5
    punpcklwd            m6, m2     ; dct4  in3  in1
    ITX_MUL2X_PACK        8, 2, 4, 10,  9, 90, 3 ; t8a  t15a
    ITX_MUL2X_PACK        0, 2, 4, 10, 70, 57, 3 ; t9a  t14a
    ITX_MUL2X_PACK        1, 2, 4, 10, 87, 25, 3 ; t11a t12a
    ITX_MUL2X_PACK        5, 2, 4, 10, 43, 80, 3 ; t10a t13a
    ITX_MUL2X_PACK        7, 2, 4, 10,  18, 89, 3 ; t4a  t7a
    ITX_MUL2X_PACK        3, 2, 4, 10, 75, 50, 3 ; t5a  t6a
    ITX_MUL2X_PACK        6, 2, 4, 10, 36, 83    ; t3   t2
    psubsw               m2, m8, m0 ; t9  t14
    paddsw               m8, m0     ; t8  t15
    psubsw               m0, m1, m5 ; t10 t13
    paddsw               m1, m5     ; t11 t12
    vpbroadcastd         m5, [o(vvc_pw_m83_36)]  ; reuse vvc_pw_36_83
    ITX_MUL2X_PACK        2, 4, _, 10, 4, 5, 6   ; t9a  t14a
    vpbroadcastd         m4, [o(vvc_pw_m36_m83)] ; reuse vvc_pw_m83_36
    ITX_MUL2X_PACK        0, 5, _, 10, 5, 4, 6   ; t10a t13a
    psubsw               m4, m8, m1 ; t11a t12a
    paddsw               m8, m1     ; t8a  t15a
    psubsw               m1, m7, m3 ; t5a  t6a
    paddsw               m7, m3     ; t4   t7
    paddsw               m3, m2, m0 ; t9   t14
    psubsw               m2, m0     ; t10  t13
%if mmsize > 16
    vbroadcasti128       m0, [o(deint_shuf)]
%else
    mova                 m0, [o(deint_shuf)]
%endif
    pshufb               m8, m0
    pshufb               m7, m0
    pshufb               m3, m0
    ITX_MUL2X_PACK        9, 0, 5, 10, 64, 64 ; t0   t1
    vpbroadcastd         m0, [o(vvc_pw_m64_64)]
    ITX_MUL2X_PACK        4, 5, _, 10, 5, 0, 4    ; t11  t12
    vpbroadcastd         m5, [o(vvc_pw_64_64)]
    ITX_MUL2X_PACK        1, 0, _, 10, 0, 5, 4    ; t6   t5
    vpbroadcastd         m0, [o(vvc_pw_m64_64)]
    ITX_MUL2X_PACK        2, 0, _, 10, 0, 5, 4    ; t13a t10a
    punpckhqdq           m0, m8, m3        ; t15a t14
    punpcklqdq           m8, m3            ; t8a  t9
    shufps               m5, m4, m2, q1032 ; t12  t13a
    vpblendd             m4, m2, 0xcc      ; t11  t10a
    shufps               m2, m7, m1, q1032 ; t7 t6
    vpblendd             m7, m1, 0xcc      ; t4 t5
    psubsw               m1, m9, m6 ; dct4 out3 out2
    paddsw               m9, m6     ; dct4 out0 out1
    psubsw               m3, m9, m2 ; dct8 out7 out6
    paddsw               m9, m2     ; dct8 out0 out1
    psubsw               m2, m1, m7 ; dct8 out4 out5
    paddsw               m1, m7     ; dct8 out3 out2
    psubsw               m7, m9, m0 ; out15 out14
    paddsw               m0, m9     ; out0  out1
    psubsw               m6, m1, m5 ; out12 out13
    paddsw               m1, m5     ; out3  out2
    psubsw               m5, m2, m4 ; out11 out10
    paddsw               m2, m4     ; out4  out5
    psubsw               m4, m3, m8 ; out8  out9
    paddsw               m3, m8     ; out7  out6
%endmacro

INV_TXFM_4X16_FN dct2, dct2
INV_TXFM_4X16_FN dct2, adst
INV_TXFM_4X16_FN dct2, flipadst
INV_TXFM_4X16_FN dct2, identity

cglobal idct2_4x16_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    mova                 m0, [cq+32*0]
    mova                 m1, [cq+32*1]
    mova                 m2, [cq+32*2]
    mova                 m3, [cq+32*3]
    call m(idct2_16x4_internal_8).main
    vpbroadcastd         m5, [o(vvc_pw_16384)]
    punpckhwd            m4, m2, m3
    punpcklwd            m2, m3
    punpckhwd            m3, m0, m1
    punpcklwd            m0, m1
    REPX   {pmulhrsw x, m5}, m0, m4, m2, m3
    punpckhdq            m1, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m3, m4
    punpckhdq            m3, m4
    jmp                tx2q
.pass2:
    vextracti128        xm4, m0, 1
    vextracti128        xm5, m1, 1
    vextracti128        xm6, m2, 1
    vextracti128        xm7, m3, 1
    call .main
    vinserti128          m0, xm4, 1
    vinserti128          m1, xm5, 1
    vpbroadcastd         m5, [o(vvc_pw_2048)]
    vinserti128          m2, xm6, 1
    vinserti128          m3, xm7, 1
    pshufd               m1, m1, q1032
    pshufd               m3, m3, q1032
    jmp m(iadst_4x16_internal_8).end2
ALIGN function_align
cglobal_label .main
    WRAP_XMM IDCT2_16_1D_PACKED
    ret

INV_TXFM_4X16_FN adst, dct2
INV_TXFM_4X16_FN adst, adst
INV_TXFM_4X16_FN adst, flipadst
INV_TXFM_4X16_FN adst, identity

cglobal iadst_4x16_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    mova                 m0, [cq+32*0]
    mova                 m1, [cq+32*1]
    mova                 m2, [cq+32*2]
    mova                 m3, [cq+32*3]
    call m(iadst_16x4_internal_8).main
    vpbroadcastd         m5, [o(vvc_pw_16384)]
    punpckhwd            m4, m2, m3
    punpcklwd            m2, m3
    punpckhwd            m3, m0, m1
    punpcklwd            m0, m1
    REPX   {pmulhrsw x, m5}, m4, m2, m3, m0
    punpckhdq            m1, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m3, m4
    punpckhdq            m3, m4
    jmp                tx2q
.pass2:
    call .main
    vpbroadcastd         m5, [o(vvc_pw_64x8)]
    paddsw               m1, m2, m4
    psubsw               m2, m4
    pmulhrsw             m1, m5     ; -out7   out4   out6  -out5
    pmulhrsw             m2, m5     ;  out8  -out11 -out9   out10
    vpbroadcastd         m5, [o(vvc_pw_2048)]
    pshufd               m1, m1, q1032
    vpblendd             m4, m1, m0, 0x33
    vpblendd             m0, m2, 0x33
    vpblendd             m2, m3, 0x33
    vpblendd             m3, m1, 0x33
    vpermq               m0, m0, q2031
    vpermq               m1, m2, q1302
    vpermq               m2, m3, q3120
    vpermq               m3, m4, q0213
    psubw                m6, m7, m5
.end:
    vpblendd             m5, m6, 0xcc
.end2:
    REPX   {pmulhrsw x, m5}, m0, m1, m2, m3
    WIN64_RESTORE_XMM
    pxor                 m4, m4
    mova          [cq+32*0], m4
    mova          [cq+32*1], m4
    mova          [cq+32*2], m4
    mova          [cq+32*3], m4
.end3:
    lea                  r2, [dstq+strideq*8]
    lea                  r3, [strideq*3]
    WRITE_4X8             0, 1
    lea                dstq, [dstq+strideq*4]
    lea                  r2, [r2  +strideq*4]
    WRITE_4X8             2, 3
    RET
ALIGN function_align
.main:
    vpblendd             m4, m1, m0, 0xcc
    vpblendd             m1, m0, 0x33
    vpblendd             m5, m2, m3, 0xcc
    vpblendd             m2, m3, 0x33
    vperm2i128           m3, m5, m2, 0x31
    vinserti128          m0, m1, xm4, 1 ; in0  in3  in2  in1
    vperm2i128           m4, m1, m4, 0x31
    vinserti128          m1, m5, xm2, 1 ; in4  in7  in6  in5
    pshufd               m3, m3, q1032  ; in15 in12 in13 in14
    pshufd               m2, m4, q1032  ; in11 in8  in9  in10
cglobal_label .main2
    vpbroadcastd         m8, [o(vvc_pd_64)]
    pxor                 m7, m7
    punpckhwd            m4, m3, m0 ; in12 in3  in14 in1
    punpcklwd            m0, m3     ; in0  in15 in2  in13
    punpckhwd            m3, m2, m1 ; in8  in7  in10 in5
    punpcklwd            m1, m2     ; in4  in11 in6  in9
    ITX_MUL4X_PACK        0, 2, 5, 6, 8,  4, 90,  22, 88, 3
    ITX_MUL4X_PACK        1, 2, 5, 6, 8, 38, 82, 54, 73, 3
    ITX_MUL4X_PACK        3, 2, 5, 6, 8, 67, 61, 78, 46, 3
    ITX_MUL4X_PACK        4, 2, 5, 6, 8, 85, 31, 90,  13, 3
    psubsw               m2, m0, m3 ; t9a  t8a  t11a t10a
    paddsw               m0, m3     ; t1a  t0a  t3a  t2a
    psubsw               m3, m1, m4 ; t13a t12a t15a t14a
    paddsw               m1, m4     ; t5a  t4a  t7a  t6a
    ITX_MUL4X_PACK        2, 4, 5, 6, 8,  18, 89, 75, 50, 3
    psubw                m6, m7, m5
    ITX_MUL2X_PACK        3, 5, _, 8, 6, 4, 6
    vpbroadcastd         m6, [o(vvc_pw_m83_36)]
    vpbroadcastd         m5, [o(vvc_pw_36_83)]
    psubsw               m4, m0, m1 ; t5   t4   t7   t6
    paddsw               m0, m1     ; t1   t0   t3   t2
    psubsw               m1, m2, m3 ; t13a t12a t15a t14a
    paddsw               m2, m3     ; t9a  t8a  t11a t10a
    psubw                m3, m7, m6 ; vvc_pw_83_m36
    vpblendd             m6, m3, 0xf0
    ITX_MUL2X_PACK        4, 3, _, 8, 6, 5, 4 ; t4a t5a t7a t6a
    ITX_MUL2X_PACK        1, 3, _, 8, 6, 5, 4 ; t12 t13 t15 t14
    vbroadcasti128       m5, [o(deint_shuf)]
    pshufb               m0, m5
    pshufb               m2, m5
    vperm2i128           m3, m0, m2, 0x31  ; t3   t2   t11a t10a
    vinserti128          m0, xm2, 1        ; t1   t0   t9a  t8a
    vperm2i128           m2, m4, m1, 0x31  ; t7a  t6a  t15  t14
    vinserti128          m4, xm1, 1        ; t4a  t5a  t12  t13
    pshufd               m2, m2, q1032     ; t6a  t7a  t14  t15
    psubsw               m1, m0, m3        ; t3a t2a t11 t10
    paddsw               m0, m3     ; -out15  out0   out14 -out1
    paddsw               m3, m4, m2 ; -out3   out12  out2  -out13
    psubsw               m4, m2            ; t6 t7 t14a t15a
    shufps               m2, m1, m4, q1032 ; t2a t6  t10 t14a
    vpblendd             m4, m1, 0x33      ; t3a t7  t11 t15a
    ret
ALIGN function_align
.main_pass1_end:
    vpbroadcastd         m5, [o(vvc_pw_m64_64)]
    vpbroadcastd         m6, [o(vvc_pw_64_64)]
    punpcklwd            m1, m4, m2
    punpckhwd            m4, m2
    pmaddwd              m2, m5, m4
    pmaddwd              m4, m6
    pmaddwd              m5, m1
    pmaddwd              m1, m6
    REPX      {paddd x, m8}, m5, m1, m2, m4
    REPX      {psrad x, 7}, m5, m2, m1, m4
    packssdw             m2, m5     ; -out11  out8   out10 -out9
    packssdw             m1, m4     ; -out7   out4   out6  -out5
    ret

INV_TXFM_4X16_FN flipadst, dct2
INV_TXFM_4X16_FN flipadst, adst
INV_TXFM_4X16_FN flipadst, flipadst
INV_TXFM_4X16_FN flipadst, identity

cglobal iflipadst_4x16_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    mova                 m0, [cq+32*0]
    mova                 m1, [cq+32*1]
    mova                 m2, [cq+32*2]
    mova                 m3, [cq+32*3]
    call m(iadst_16x4_internal_8).main
    vpbroadcastd         m5, [o(vvc_pw_16384)]
    punpcklwd            m4, m1, m0
    punpckhwd            m1, m0
    punpcklwd            m0, m3, m2
    punpckhwd            m3, m2
    REPX   {pmulhrsw x, m5}, m4, m1, m0, m3
    punpckldq            m2, m3, m1
    punpckhdq            m3, m1
    punpckhdq            m1, m0, m4
    punpckldq            m0, m4
    jmp                tx2q
.pass2:
    call m(iadst_4x16_internal_8).main
    vpbroadcastd         m5, [o(vvc_pw_64x8)]
    paddsw               m1, m2, m4
    psubsw               m2, m4
    pmulhrsw             m1, m5     ; -out7   out4   out6  -out5
    pmulhrsw             m2, m5     ;  out8  -out11 -out9   out10
    vpbroadcastd         m6, [o(vvc_pw_2048)]
    pshufd               m1, m1, q1032
    vpblendd             m4, m0, m2, 0x33
    vpblendd             m0, m1, 0xcc
    vpblendd             m1, m3, 0xcc
    vpblendd             m2, m3, 0x33
    vpermq               m0, m0, q3120
    vpermq               m1, m1, q0213
    vpermq               m2, m2, q2031
    vpermq               m3, m4, q1302
    psubw                m5, m7, m6
    jmp m(iadst_4x16_internal_8).end

INV_TXFM_4X16_FN identity, dct2
INV_TXFM_4X16_FN identity, adst
INV_TXFM_4X16_FN identity, flipadst
INV_TXFM_4X16_FN identity, identity

cglobal iidentity_4x16_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    mova                 m3, [cq+32*0]
    mova                 m2, [cq+32*1]
    mova                 m4, [cq+32*2]
    mova                 m5, [cq+32*3]
    vpbroadcastd         m8, [o(vvc_pw_1697x8)]
    pcmpeqw              m0, m0 ; -1
    punpcklwd            m1, m3, m2
    punpckhwd            m3, m2
    punpcklwd            m2, m4, m5
    punpckhwd            m4, m5
    pmulhrsw             m5, m8, m1
    pmulhrsw             m6, m8, m2
    pmulhrsw             m7, m8, m3
    pmulhrsw             m8, m4
    pcmpeqw              m9, m0, m1 ; we want to do a signed avg, but pavgw is
    pxor                 m1, m9     ; unsigned. as long as both signs are equal
    pcmpeqw              m9, m0, m2 ; it still works, but if the input is -1 the
    pxor                 m2, m9     ; pmulhrsw result will become 0 which causes
    pcmpeqw              m9, m0, m3 ; pavgw to output -32768 instead of 0 unless
    pxor                 m3, m9     ; we explicitly deal with that case here.
    pcmpeqw              m0, m4
    pxor                 m4, m0
    pavgw                m1, m5
    pavgw                m2, m6
    pavgw                m3, m7
    pavgw                m4, m8
    punpckldq            m0, m1, m2
    punpckhdq            m1, m2
    punpckldq            m2, m3, m4
    punpckhdq            m3, m4
    jmp                tx2q
.pass2:
    vpbroadcastd         m8, [o(vvc_pw_1697x16)]
    vpbroadcastd         m5, [o(vvc_pw_2048)]
    pmulhrsw             m4, m8, m0
    pmulhrsw             m6, m8, m1
    pmulhrsw             m7, m8, m2
    pmulhrsw             m8, m3
    REPX      {paddsw x, x}, m0, m1, m2, m3
    paddsw               m0, m4
    paddsw               m1, m6
    paddsw               m2, m7
    paddsw               m3, m8
    jmp m(iadst_4x16_internal_8).end2

%macro WRITE_8X4 4-7 strideq*1, strideq*2, r3 ; coefs[1-2], tmp[1-2], off[1-3]
    movq               xm%3, [dstq   ]
    movhps             xm%3, [dstq+%5]
    movq               xm%4, [dstq+%6]
    movhps             xm%4, [dstq+%7]
    pmovzxbw            m%3, xm%3
    pmovzxbw            m%4, xm%4
%ifnum %1
    paddw               m%3, m%1
%else
    paddw               m%3, %1
%endif
%ifnum %2
    paddw               m%4, m%2
%else
    paddw               m%4, %2
%endif
    packuswb            m%3, m%4
    vextracti128       xm%4, m%3, 1
    movq          [dstq   ], xm%3
    movhps        [dstq+%6], xm%3
    movq          [dstq+%5], xm%4
    movhps        [dstq+%7], xm%4
%endmacro

%macro INV_TXFM_8X4_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 8x4
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    jmp m(vvc_inv_dct2_dct2_8x8_8).dconly2
%endif
%endmacro

INV_TXFM_8X4_FN dct2, dct2
INV_TXFM_8X4_FN dct2, adst
INV_TXFM_8X4_FN dct2, flipadst
INV_TXFM_8X4_FN dct2, identity

cglobal idct2_8x4_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpbroadcastd        xm3, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm3, [cq+16*0]
    pmulhrsw            xm1, xm3, [cq+16*1]
    pmulhrsw            xm2, xm3, [cq+16*2]
    pmulhrsw            xm3,      [cq+16*3]
    call m(idct2_4x8_internal_8).main
    vbroadcasti128       m4, [o(deint_shuf)]
    vinserti128          m3, m1, xm3, 1
    vinserti128          m1, m0, xm2, 1
    shufps               m0, m1, m3, q0220
    shufps               m1, m3, q1331
    pshufb               m0, m4
    pshufb               m1, m4
    jmp                tx2q
.pass2:
    IDCT2_4_1D_PACKED
    vpermq               m0, m0, q3120
    vpermq               m1, m1, q2031
    jmp m(iadst_8x4_internal_8).end2

INV_TXFM_8X4_FN adst, dct2
INV_TXFM_8X4_FN adst, adst
INV_TXFM_8X4_FN adst, flipadst
INV_TXFM_8X4_FN adst, identity

cglobal iadst_8x4_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpbroadcastd        xm0, [o(vvc_pw_64x8)]
    pshufd              xm4,      [cq+16*0], q1032
    pmulhrsw            xm3, xm0, [cq+16*3]
    pshufd              xm5,      [cq+16*1], q1032
    pmulhrsw            xm2, xm0, [cq+16*2]
    pmulhrsw            xm4, xm0
    pmulhrsw            xm5, xm0
    call m(iadst_4x8_internal_8).main_pass1
    vinserti128        m0, xm2, 1
    vinserti128        m1, xm3, 1
    punpckhwd          m2, m0, m1
    punpcklwd          m0, m1
    pxor               m3, m3
    psubsw             m3, m2
    punpckhwd          m1, m0, m3
    punpcklwd          m0, m3
    jmp              tx2q
.pass2:
    call .main
.end:
    vpermq               m0, m0, q3120
    vpermq               m1, m1, q3120
.end2:
    vpbroadcastd         m2, [o(vvc_pw_2048)]
    pmulhrsw             m0, m2
    pmulhrsw             m1, m2
    WIN64_RESTORE_XMM
.end3:
    pxor                 m2, m2
    mova          [cq+32*0], m2
    mova          [cq+32*1], m2
    lea                  r3, [strideq*3]
    WRITE_8X4             0, 1, 4, 5
    RET
ALIGN function_align
cglobal_label .main
    IADST4_1D_PACKED
    ret

INV_TXFM_8X4_FN flipadst, dct2
INV_TXFM_8X4_FN flipadst, adst
INV_TXFM_8X4_FN flipadst, flipadst
INV_TXFM_8X4_FN flipadst, identity

cglobal iflipadst_8x4_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpbroadcastd        xm0, [o(vvc_pw_64x8)]
    pshufd              xm4,      [cq+16*0], q1032
    pmulhrsw            xm3, xm0, [cq+16*3]
    pshufd              xm5,      [cq+16*1], q1032
    pmulhrsw            xm2, xm0, [cq+16*2]
    pmulhrsw            xm4, xm0
    pmulhrsw            xm5, xm0
    call m(iadst_4x8_internal_8).main_pass1
    vinserti128          m3, xm1, 1
    vinserti128          m2, xm0, 1
    punpckhwd            m1, m3, m2
    punpcklwd            m3, m2
    pxor                 m0, m0
    psubsw               m0, m1
    punpckhwd            m1, m0, m3
    punpcklwd            m0, m3
    jmp                tx2q
.pass2:
    call m(iadst_8x4_internal_8).main
    mova                 m2, m1
    vpermq               m1, m0, q2031
    vpermq               m0, m2, q2031
    jmp m(iadst_8x4_internal_8).end2

INV_TXFM_8X4_FN identity, dct2
INV_TXFM_8X4_FN identity, adst
INV_TXFM_8X4_FN identity, flipadst
INV_TXFM_8X4_FN identity, identity

cglobal iidentity_8x4_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    mova                xm2, [cq+16*0]
    mova                xm0, [cq+16*1]
    vinserti128          m2, [cq+16*2], 1
    vinserti128          m0, [cq+16*3], 1
    vpbroadcastd         m3, [o(vvc_pw_64x8)]
    punpcklwd            m1, m2, m0
    punpckhwd            m2, m0
    pmulhrsw             m1, m3
    pmulhrsw             m2, m3
    punpcklwd            m0, m1, m2
    punpckhwd            m1, m2
    paddsw               m0, m0
    paddsw               m1, m1
    jmp                tx2q
.pass2:
    vpbroadcastd         m3, [o(vvc_pw_1697x8)]
    pmulhrsw             m2, m3, m0
    pmulhrsw             m3, m1
    paddsw               m0, m2
    paddsw               m1, m3
    jmp m(iadst_8x4_internal_8).end

%macro INV_TXFM_8X8_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 8x8
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    or                  r3d, 8
.dconly:
    pmulhrsw            xm0, xm2
.dconly2:
    movd                xm2, [vvc_pw_2048]
    pmulhrsw            xm0, xm1
    lea                  r2, [strideq*3]
    pmulhrsw            xm0, xm2
    vpbroadcastw         m0, xm0
.dconly_loop:
    WRITE_8X4             0, 0, 1, 2, strideq*1, strideq*2, r2
    lea                dstq, [dstq+strideq*4]
    sub                 r3d, 4
    jg .dconly_loop
    RET
%endif
%endmacro

INV_TXFM_8X8_FN dct2, dct2
INV_TXFM_8X8_FN dct2, adst
INV_TXFM_8X8_FN dct2, flipadst
INV_TXFM_8X8_FN dct2, identity

cglobal idct2_8x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpermq               m0, [cq+32*0], q3120 ; 0 1
    vpermq               m3, [cq+32*3], q3120 ; 6 7
    vpermq               m2, [cq+32*2], q3120 ; 4 5
    vpermq               m1, [cq+32*1], q3120 ; 2 3
    call .main
    shufps               m4, m0, m1, q0220
    shufps               m5, m0, m1, q1331
    shufps               m1, m2, m3, q0220
    shufps               m3, m2, m3, q1331
    vbroadcasti128       m0, [o(deint_shuf)]
    vpbroadcastd         m2, [o(vvc_pw_16384)]
    REPX   {pshufb   x, m0}, m4, m5, m1, m3
    REPX   {pmulhrsw x, m2}, m4, m5, m1, m3
    vinserti128          m0, m4, xm1, 1
    vperm2i128           m2, m4, m1, 0x31
    vinserti128          m1, m5, xm3, 1
    vperm2i128           m3, m5, m3, 0x31
    jmp                tx2q
.pass2:
    call .main
    vpbroadcastd         m4, [o(vvc_pw_2048)]
    vpermq               m0, m0, q3120
    vpermq               m1, m1, q2031
    vpermq               m2, m2, q3120
    vpermq               m3, m3, q2031
    jmp m(iadst_8x8_internal_8).end2
ALIGN function_align
cglobal_label .main
    IDCT2_8_1D_PACKED
    ret

INV_TXFM_8X8_FN adst, dct2
INV_TXFM_8X8_FN adst, adst
INV_TXFM_8X8_FN adst, flipadst
INV_TXFM_8X8_FN adst, identity

cglobal iadst_8x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpermq               m4, [cq+32*0], q1302 ; 1 0
    vpermq               m3, [cq+32*3], q3120 ; 6 7
    vpermq               m5, [cq+32*1], q1302 ; 3 2
    vpermq               m2, [cq+32*2], q3120 ; 4 5
    call .main_pass1
    vpbroadcastd         m5, [o(vvc_pw_16384)]
    punpcklwd            m4, m0, m1
    punpckhwd            m0, m1
    punpcklwd            m1, m2, m3
    punpckhwd            m2, m3
    pxor                 m3, m3
    psubw                m3, m5 ; negate odd elements during rounding
    pmulhrsw             m4, m5
    pmulhrsw             m0, m3
    pmulhrsw             m1, m5
    pmulhrsw             m2, m3
    punpcklwd            m3, m4, m0
    punpckhwd            m4, m0
    punpcklwd            m0, m1, m2
    punpckhwd            m1, m2
    vperm2i128           m2, m3, m0, 0x31
    vinserti128          m0, m3, xm0, 1
    vperm2i128           m3, m4, m1, 0x31
    vinserti128          m1, m4, xm1, 1
    jmp                tx2q
.pass2:
    pshufd               m4, m0, q1032
    pshufd               m5, m1, q1032
    call .main_pass2
    vpbroadcastd         m5, [o(vvc_pw_2048)]
    vpbroadcastd        xm4, [o(vvc_pw_64)]
    psubw                m4, m5 ; lower half = 2048, upper half = -2048
.end:
    REPX {vpermq x, x, q3120}, m0, m1, m2, m3
.end2:
    pmulhrsw             m0, m4
    pmulhrsw             m1, m4
.end3:
    pmulhrsw             m2, m4
    pmulhrsw             m3, m4
    WIN64_RESTORE_XMM
.end4:
    pxor                 m4, m4
    mova          [cq+32*0], m4
    mova          [cq+32*1], m4
    mova          [cq+32*2], m4
    mova          [cq+32*3], m4
    lea                  r3, [strideq*3]
    WRITE_8X4             0, 1, 4, 5
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4             2, 3, 4, 5
    RET
ALIGN function_align
.main_pass1:
    IADST8_1D_PACKED 1
    ret
ALIGN function_align
cglobal_label .main_pass2
    IADST8_1D_PACKED 2
    ret

INV_TXFM_8X8_FN flipadst, dct2
INV_TXFM_8X8_FN flipadst, adst
INV_TXFM_8X8_FN flipadst, flipadst
INV_TXFM_8X8_FN flipadst, identity

cglobal iflipadst_8x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    vpermq               m4, [cq+32*0], q1302 ; 1 0
    vpermq               m3, [cq+32*3], q3120 ; 6 7
    vpermq               m5, [cq+32*1], q1302 ; 3 2
    vpermq               m2, [cq+32*2], q3120 ; 4 5
    call m(iadst_8x8_internal_8).main_pass1
    vpbroadcastd         m5, [o(vvc_pw_16384)]
    punpckhwd            m4, m3, m2
    punpcklwd            m3, m2
    punpckhwd            m2, m1, m0
    punpcklwd            m1, m0
    pxor                 m0, m0
    psubw                m0, m5
    pmulhrsw             m4, m0
    pmulhrsw             m3, m5
    pmulhrsw             m2, m0
    pmulhrsw             m1, m5
    punpckhwd            m0, m4, m3
    punpcklwd            m4, m3
    punpckhwd            m3, m2, m1
    punpcklwd            m2, m1
    vinserti128          m1, m0, xm3, 1
    vperm2i128           m3, m0, m3, 0x31
    vinserti128          m0, m4, xm2, 1
    vperm2i128           m2, m4, m2, 0x31
    jmp                tx2q
.pass2:
    pshufd               m4, m0, q1032
    pshufd               m5, m1, q1032
    call m(iadst_8x8_internal_8).main_pass2
    vpbroadcastd         m4, [o(vvc_pw_2048)]
    vpbroadcastd        xm5, [o(vvc_pw_64)]
    psubw                m4, m5 ; lower half = -2048, upper half = 2048
    vpermq               m5, m3, q2031
    vpermq               m3, m0, q2031
    vpermq               m0, m2, q2031
    vpermq               m2, m1, q2031
    pmulhrsw             m1, m0, m4
    pmulhrsw             m0, m5, m4
    jmp m(iadst_8x8_internal_8).end3

INV_TXFM_8X8_FN identity, dct2
INV_TXFM_8X8_FN identity, adst
INV_TXFM_8X8_FN identity, flipadst
INV_TXFM_8X8_FN identity, identity

cglobal iidentity_8x8_internal_8, 0, 5, 7, dst, stride, c, eob, tx2
    mova                xm3, [cq+16*0]
    mova                xm2, [cq+16*1]
    vinserti128          m3, [cq+16*4], 1
    vinserti128          m2, [cq+16*5], 1
    mova                xm4, [cq+16*2]
    mova                xm0, [cq+16*3]
    vinserti128          m4, [cq+16*6], 1
    vinserti128          m0, [cq+16*7], 1
    punpcklwd            m1, m3, m2
    punpckhwd            m3, m2
    punpcklwd            m2, m4, m0
    punpckhwd            m4, m0
    punpckldq            m0, m1, m2
    punpckhdq            m1, m2
    punpckldq            m2, m3, m4
    punpckhdq            m3, m4
    jmp                tx2q
.pass2:
    vpbroadcastd         m4, [o(vvc_pw_64)]
    jmp m(iadst_8x8_internal_8).end

%macro INV_TXFM_8X16_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 8x16
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    or                  r3d, 16
    jmp m(vvc_inv_dct2_dct2_8x8_8).dconly
%endif
%endmacro

%macro ITX_8X16_LOAD_COEFS 0
    vpbroadcastd         m4, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m4, [cq+32*0]
    add                  cq, 32*4
    pmulhrsw             m7, m4, [cq+32*3]
    pmulhrsw             m1, m4, [cq-32*3]
    pmulhrsw             m6, m4, [cq+32*2]
    pmulhrsw             m2, m4, [cq-32*2]
    pmulhrsw             m5, m4, [cq+32*1]
    pmulhrsw             m3, m4, [cq-32*1]
    pmulhrsw             m4,     [cq+32*0]
%endmacro

INV_TXFM_8X16_FN dct2, dct2
INV_TXFM_8X16_FN dct2, adst
INV_TXFM_8X16_FN dct2, flipadst
INV_TXFM_8X16_FN dct2, identity

cglobal idct2_8x16_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    ITX_8X16_LOAD_COEFS
    call m(idct2_16x8_internal_8).main
    vpbroadcastd        m10, [o(vvc_pw_16384)]
.pass1_end:
    vperm2i128           m9, m3, m7, 0x31
    vinserti128          m3, xm7, 1
    vperm2i128           m8, m2, m6, 0x31
    vinserti128          m2, xm6, 1
    vperm2i128           m6, m1, m5, 0x31
    vinserti128          m1, xm5, 1
    vperm2i128           m5, m0, m4, 0x31
    vinserti128          m0, xm4, 1
    punpckhwd            m4, m2, m3
    punpcklwd            m2, m3
    punpckhwd            m3, m0, m1
    punpcklwd            m0, m1
.pass1_end2:
    punpckhwd            m7, m5, m6
    punpcklwd            m5, m6
    punpcklwd            m6, m8, m9
    punpckhwd            m8, m9
    REPX  {pmulhrsw x, m10}, m2, m0, m4, m3, m5, m6, m7, m8
    punpckhdq            m1, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m3, m4
    punpckhdq            m3, m4
    punpckldq            m4, m5, m6
    punpckhdq            m5, m6
    punpckldq            m6, m7, m8
    punpckhdq            m7, m8
    jmp                tx2q
.pass2:
    call .main
    REPX {vpermq x, x, q3120}, m0, m2, m4, m6
    REPX {vpermq x, x, q2031}, m1, m3, m5, m7
.end:
    vpbroadcastd         m8, [o(vvc_pw_2048)]
.end2:
    REPX   {pmulhrsw x, m8}, m0, m1, m2, m3, m4, m5, m6, m7
.end3:
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8}, -4, -3, -2, -1, 0, 1, 2, 3
    lea                  r3, [strideq*3]
    WRITE_8X4             0, 1, 8, 9
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4             2, 3, 0, 1
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4             4, 5, 0, 1
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4             6, 7, 0, 1
    RET
ALIGN function_align
cglobal_label .main
    IDCT2_16_1D_PACKED
    ret

INV_TXFM_8X16_FN adst, dct2
INV_TXFM_8X16_FN adst, adst
INV_TXFM_8X16_FN adst, flipadst
INV_TXFM_8X16_FN adst, identity

cglobal iadst_8x16_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    ITX_8X16_LOAD_COEFS
    call m(iadst_16x8_internal_8).main
    call m(iadst_16x8_internal_8).main_pass1_end
    vpbroadcastd        m10, [o(vvc_pw_16384)]
    pslld                m9, m10, 17
    psubw               m10, m9 ; 16384, -16384
    jmp m(idct2_8x16_internal_8).pass1_end
ALIGN function_align
.pass2:
    call .main
    call .main_pass2_end
    vpbroadcastd         m9, [o(vvc_pw_2048)]
    vpbroadcastd        xm8, [o(vvc_pw_64)]
    psubw                m8, m9
    REPX {vpermq x, x, q2031}, m0, m1, m2, m3
    REPX {vpermq x, x, q3120}, m4, m5, m6, m7
    jmp m(idct2_8x16_internal_8).end2
ALIGN function_align
cglobal_label .main
    REPX {pshufd x, x, q1032}, m7, m1, m5, m3
.main2:
    vpbroadcastd        m10, [o(vvc_pd_64)]
    punpckhwd            m8, m7, m0 ; in14 in1
    punpcklwd            m0, m7     ; in0  in15
    punpcklwd            m7, m6, m1 ; in12 in3
    punpckhwd            m1, m6     ; in2  in13
    punpckhwd            m6, m5, m2 ; in10 in5
    punpcklwd            m2, m5     ; in4  in11
    punpcklwd            m5, m4, m3 ; in8  in7
    punpckhwd            m3, m4     ; in6  in9
    ITX_MUL2X_PACK        0, 4, 9, 10,  4, 90, 3 ; t0  t1
    ITX_MUL2X_PACK        1, 4, 9, 10,  22, 88, 3 ; t2  t3
    ITX_MUL2X_PACK        2, 4, 9, 10, 38, 82, 3 ; t4  t5
    ITX_MUL2X_PACK        3, 4, 9, 10, 54, 73, 3 ; t6  t7
    ITX_MUL2X_PACK        5, 4, 9, 10, 67, 61, 3 ; t8  t9
    ITX_MUL2X_PACK        6, 4, 9, 10, 78, 46, 3 ; t10 t11
    ITX_MUL2X_PACK        7, 4, 9, 10, 85, 31, 3 ; t12 t13
    ITX_MUL2X_PACK        8, 4, 9, 10, 90,  13, 3 ; t14 t15
    psubsw               m4, m0, m5 ; t9a  t8a
    paddsw               m0, m5     ; t1a  t0a
    psubsw               m5, m1, m6 ; t11a t10a
    paddsw               m1, m6     ; t3a  t2a
    psubsw               m6, m2, m7 ; t13a t12a
    paddsw               m2, m7     ; t5a  t4a
    psubsw               m7, m3, m8 ; t15a t14a
    paddsw               m3, m8     ; t7a  t6a
    vpbroadcastd        m11, [o(vvc_pw_m89_18)]
    vpbroadcastd        m12, [o(vvc_pw_18_89)]
    pxor                 m9, m9
    ITX_MUL2X_PACK        4, 8, _, 10, 11, 12, 6 ; t8  t9
    psubw                m8, m9, m11 ; vvc_pw_89_m18
    ITX_MUL2X_PACK        6, 12, _, 10, 12, 8, 6 ; t12 t13
    vpbroadcastd        m11, [o(vvc_pw_m50_75)]
    vpbroadcastd        m12, [o(vvc_pw_75_50)]
    ITX_MUL2X_PACK        5, 8, _, 10, 11, 12, 6 ; t10 t11
    psubw                m8, m9, m11 ; vvc_pw_50_m75
    ITX_MUL2X_PACK        7, 12, _, 10, 12, 8, 6 ; t14 t15
    psubsw               m8, m1, m3 ; t7   t6
    paddsw               m1, m3     ; t3   t2
    psubsw               m3, m0, m2 ; t5   t4
    paddsw               m0, m2     ; t1   t0
    psubsw               m2, m5, m7 ; t14a t15a
    paddsw               m7, m5     ; t10a t11a
    psubsw               m5, m4, m6 ; t12a t13a
    paddsw               m4, m6     ; t8a  t9a
    vpbroadcastd        m11, [o(vvc_pw_m83_36)]
    vpbroadcastd        m12, [o(vvc_pw_36_83)]
    ITX_MUL2X_PACK        3, 6, _, 10, 12, 11, 6 ; t5a t4a
    psubw                m6, m9, m11 ; vvc_pw_83_m36
    ITX_MUL2X_PACK        8, 6, _, 10, 6, 12, 6  ; t7a t6a
    vpbroadcastd        m11, [o(vvc_pw_m36_83)]
    vpbroadcastd        m12, [o(vvc_pw_83_36)]
    ITX_MUL2X_PACK        2, 6, _, 10, 11, 12, 6 ; t15 t14
    psubw                m6, m9, m11 ; vvc_pw_36_m83
    ITX_MUL2X_PACK        5, 12, _, 10, 12, 6, 6 ; t13 t12
    vbroadcasti128      m12, [o(deint_shuf)]
    paddsw               m6, m4, m7        ; -out1  out14
    psubsw               m4, m7            ;  t10    t11
    psubsw              m11, m3, m8        ;  t7     t6
    paddsw               m8, m3            ;  out12 -out3
    psubsw               m3, m0, m1        ;  t3a    t2a
    paddsw               m0, m1            ; -out15  out0
    paddsw               m1, m2, m5        ; -out13  out2
    psubsw               m5, m2            ;  t15a   t14a
    pshufb               m0, m12
    pshufb               m6, m12
    pshufb               m8, m12
    pshufb               m1, m12
    shufps               m7, m6, m0, q1032 ;  out14 -out15
    vpblendd             m0, m6, 0x33      ; -out1   out0
    punpcklqdq           m6, m8, m1        ;  out12 -out13
    punpckhqdq           m1, m8, m1        ; -out3   out2
    ret
ALIGN function_align
.main_pass1_end:
    vpbroadcastd         m8, [o(vvc_pw_m64_64)]
    vpbroadcastd        m12, [o(vvc_pw_64_64)]
    pmaddwd              m9, m8, m11       ; -out11
    pmaddwd              m2, m12, m5       ; -out5
    pmaddwd              m5, m8            ;  out10
    pmaddwd             m11, m12           ;  out4
    REPX     {paddd x, m10}, m9, m5, m2, m11
    REPX     {psrad x, 7 }, m9, m5, m2, m11
    packssdw             m5, m9            ;  out10 -out11
    packssdw             m2, m11           ; -out5   out4
    pmaddwd             m11, m8, m3        ;  out8
    vpbroadcastd         m8, [o(vvc_pw_64_m64)]
    pmaddwd              m3, m12           ; -out7
    pmaddwd              m8, m4            ; -out9
    pmaddwd              m4, m12           ;  out6
    REPX     {paddd x, m10}, m11, m3, m8, m4
    REPX     {psrad x,  7 }, m11, m3, m8, m4
    packssdw             m3, m4            ; -out7   out6
    packssdw             m4, m11, m8       ;  out8  -out9
    vpbroadcastd        m10, [o(vvc_pw_16384)]
    pxor                 m9, m9
    ret
ALIGN function_align
cglobal_label .main_pass2_end
    vpbroadcastd         m8, [o(vvc_pw_64x8)]
    pshufb               m2, m11, m12
    pshufb               m5, m12
    pshufb               m3, m12
    pshufb               m4, m12
    punpcklqdq          m11, m5, m2        ;  t15a   t7
    punpckhqdq           m5, m2            ;  t14a   t6
    shufps               m2, m3, m4, q1032 ;  t2a    t10
    vpblendd             m3, m4, 0xcc      ;  t3a    t11
    psubsw               m4, m2, m3        ;  out8  -out9
    paddsw               m3, m2            ; -out7   out6
    paddsw               m2, m5, m11       ; -out5   out4
    psubsw               m5, m11           ;  out10 -out11
    REPX   {pmulhrsw x, m8}, m2, m3, m4, m5
    ret

INV_TXFM_8X16_FN flipadst, dct2
INV_TXFM_8X16_FN flipadst, adst
INV_TXFM_8X16_FN flipadst, flipadst
INV_TXFM_8X16_FN flipadst, identity

cglobal iflipadst_8x16_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    ITX_8X16_LOAD_COEFS
    call m(iadst_16x8_internal_8).main
    call m(iadst_16x8_internal_8).main_pass1_end
    vpbroadcastd         m9, [o(vvc_pw_16384)]
    pslld               m10, m9, 17
    psubw               m10, m9 ; -16384, 16384
    vperm2i128           m9, m4, m0, 0x31
    vinserti128          m0, m4, xm0, 1
    vperm2i128           m8, m5, m1, 0x31
    vinserti128          m4, m5, xm1, 1
    vperm2i128           m5, m7, m3, 0x31
    vinserti128          m3, m7, xm3, 1
    vinserti128          m1, m6, xm2, 1
    vperm2i128           m6, m6, m2, 0x31
    punpcklwd            m2, m4, m0
    punpckhwd            m4, m0
    punpcklwd            m0, m3, m1
    punpckhwd            m3, m1
    jmp m(idct2_8x16_internal_8).pass1_end2
.pass2:
    call m(iadst_8x16_internal_8).main
    call m(iadst_8x16_internal_8).main_pass2_end
    vpbroadcastd         m8, [o(vvc_pw_2048)]
    vpbroadcastd        xm9, [o(vvc_pw_64)]
    psubw                m8, m9
    vpermq               m9, m0, q3120
    vpermq               m0, m7, q2031
    vpermq               m7, m1, q3120
    vpermq               m1, m6, q2031
    vpermq               m6, m2, q3120
    vpermq               m2, m5, q2031
    vpermq               m5, m3, q3120
    vpermq               m3, m4, q2031
    pmulhrsw             m0, m8
    pmulhrsw             m1, m8
    pmulhrsw             m2, m8
    pmulhrsw             m3, m8
    pmulhrsw             m4, m5, m8
    pmulhrsw             m5, m6, m8
    pmulhrsw             m6, m7, m8
    pmulhrsw             m7, m9, m8
    jmp m(idct2_8x16_internal_8).end3

INV_TXFM_8X16_FN identity, dct2
INV_TXFM_8X16_FN identity, adst
INV_TXFM_8X16_FN identity, flipadst
INV_TXFM_8X16_FN identity, identity

%macro IDTX16 3-4 ; src/dst, tmp, vvc_pw_1697x16, [vvc_pw_16394]
    pmulhrsw            m%2, m%3, m%1
%if %0 == 4 ; if downshifting by 1
    pmulhrsw            m%2, m%4
%else
    paddsw              m%1, m%1
%endif
    paddsw              m%1, m%2
%endmacro

cglobal iidentity_8x16_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    mova                xm3, [cq+16*0]
    mova                xm2, [cq+16*2]
    add                  cq, 16*8
    vinserti128          m3, [cq+16*0], 1
    vinserti128          m2, [cq+16*2], 1
    vpbroadcastd         m9, [o(vvc_pw_64x8)]
    mova                xm4, [cq-16*4]
    mova                xm5, [cq-16*2]
    vinserti128          m4, [cq+16*4], 1
    vinserti128          m5, [cq+16*6], 1
    mova                xm7, [cq-16*7]
    mova                xm6, [cq-16*5]
    vinserti128          m7, [cq+16*1], 1
    vinserti128          m6, [cq+16*3], 1
    mova                xm8, [cq-16*3]
    mova                xm0, [cq-16*1]
    vinserti128          m8, [cq+16*5], 1
    vinserti128          m0, [cq+16*7], 1
    punpcklwd            m1, m3, m2
    punpckhwd            m3, m2
    punpcklwd            m2, m4, m5
    punpckhwd            m4, m5
    punpcklwd            m5, m7, m6
    punpckhwd            m7, m6
    punpcklwd            m6, m8, m0
    punpckhwd            m8, m0
    REPX   {pmulhrsw x, m9}, m1, m2, m3, m4, m5, m6, m7, m8
    punpckldq            m0, m1, m2
    punpckhdq            m1, m2
    punpckldq            m2, m3, m4
    punpckhdq            m3, m4
    punpckldq            m4, m5, m6
    punpckhdq            m5, m6
    punpckldq            m6, m7, m8
    punpckhdq            m7, m8
    jmp                tx2q
.pass2:
    vpbroadcastd         m8, [o(vvc_pw_1697x16)]
    REPX {vpermq   x, x, q3120}, m0, m1, m2, m3, m4, m5, m6, m7
    REPX {IDTX16   x, 9, 8}, 0, 1, 2, 3, 4, 5, 6, 7
    jmp m(idct2_8x16_internal_8).end

%macro WRITE_16X2 6 ; coefs[1-2], tmp[1-2], offset[1-2]
    pmovzxbw            m%3, [dstq+%5]
%ifnum %1
    paddw               m%3, m%1
%else
    paddw               m%3, %1
%endif
    pmovzxbw            m%4, [dstq+%6]
%ifnum %2
    paddw               m%4, m%2
%else
    paddw               m%4, %2
%endif
    packuswb            m%3, m%4
    vpermq              m%3, m%3, q3120
    mova          [dstq+%5], xm%3
    vextracti128  [dstq+%6], m%3, 1
%endmacro

%macro INV_TXFM_16X4_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 16x4
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    or                  r3d, 4
.dconly:
    pmulhrsw            xm0, xm2
    movd                xm2, [vvc_pw_2048] ; intentionally rip-relative
    pmulhrsw            xm0, xm1
    pmulhrsw            xm0, xm2
    vpbroadcastw         m0, xm0
    pxor                 m3, m3
.dconly_loop:
    mova                xm1, [dstq+strideq*0]
    vinserti128          m1, [dstq+strideq*1], 1
    punpckhbw            m2, m1, m3
    punpcklbw            m1, m3
    paddw                m2, m0
    paddw                m1, m0
    packuswb             m1, m2
    mova         [dstq+strideq*0], xm1
    vextracti128 [dstq+strideq*1], m1, 1
    lea                dstq, [dstq+strideq*2]
    sub                 r3d, 2
    jg .dconly_loop
    RET
%endif
%endmacro

INV_TXFM_16X4_FN dct2, dct2
INV_TXFM_16X4_FN dct2, adst
INV_TXFM_16X4_FN dct2, flipadst
INV_TXFM_16X4_FN dct2, identity

cglobal idct2_16x4_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    mova                xm0, [cq+16*0]
    mova                xm1, [cq+16*1]
    mova                xm2, [cq+16*2]
    mova                xm3, [cq+16*3]
    mova                xm4, [cq+16*4]
    mova                xm5, [cq+16*5]
    mova                xm6, [cq+16*6]
    mova                xm7, [cq+16*7]
    call m(idct2_4x16_internal_8).main
    vinserti128          m6, m2, xm6, 1
    vinserti128          m2, m0, xm4, 1
    vinserti128          m0, m1, xm5, 1
    vinserti128          m1, m3, xm7, 1
    punpcklwd            m3, m2, m6
    punpckhwd            m2, m6
    vpbroadcastd         m6, [o(vvc_pw_16384)]
    punpckhwd            m4, m0, m1
    punpcklwd            m0, m1
    mova                 m1, m6
    jmp m(iadst_16x4_internal_8).pass1_end
.pass2:
    call .main
    jmp m(iadst_16x4_internal_8).end
ALIGN function_align
cglobal_label .main
    vpbroadcastd         m6, [o(vvc_pd_64)]
    IDCT2_4_1D              0, 1, 2, 3, 4, 5, 6
    ret

INV_TXFM_16X4_FN adst, dct2
INV_TXFM_16X4_FN adst, adst
INV_TXFM_16X4_FN adst, flipadst
INV_TXFM_16X4_FN adst, identity

cglobal iadst_16x4_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    vpermq               m0, [cq+32*0], q1230
    vpermq               m3, [cq+32*3], q2103
    vpermq               m1, [cq+32*1], q1230
    vpermq               m2, [cq+32*2], q2103
    call m(iadst_4x16_internal_8).main2
    call m(iadst_4x16_internal_8).main_pass1_end
    punpcklwd            m4, m3, m1
    punpcklwd            m5, m2, m0
    punpckhwd            m0, m1
    punpckhwd            m2, m3
    vpbroadcastd         m1, [o(vvc_pw_16384)]
    vinserti128          m3, m0, xm2, 1
    vperm2i128           m2, m0, m2, 0x31
    vinserti128          m0, m4, xm5, 1
    vperm2i128           m4, m4, m5, 0x31
    psubw                m6, m7, m1
.pass1_end:
    pmulhrsw             m3, m1
    pmulhrsw             m2, m6
    pmulhrsw             m4, m1
    pmulhrsw             m0, m6
    punpcklwd            m1, m3, m2
    punpckhwd            m3, m2
    punpcklwd            m2, m4, m0
    punpckhwd            m4, m0
    punpckldq            m0, m1, m2
    punpckhdq            m1, m2
    punpckldq            m2, m3, m4
    punpckhdq            m3, m4
    jmp                tx2q
.pass2:
    call .main
.end:
    vpbroadcastd         m4, [o(vvc_pw_2048)]
    REPX   {pmulhrsw x, m4}, m0, m1, m2, m3
    WIN64_RESTORE_XMM
.end2:
    pxor                 m4, m4
    mova          [cq+32*0], m4
    mova          [cq+32*1], m4
    mova          [cq+32*2], m4
    mova          [cq+32*3], m4
.end3:
    WRITE_16X2            0, 1, 4, 5, strideq*0, strideq*1
    lea                dstq, [dstq+strideq*2]
    WRITE_16X2            2, 3, 4, 5, strideq*0, strideq*1
    RET
ALIGN function_align
cglobal_label .main
    vpbroadcastd         m6, [o(vvc_pw_m3344_3344)]
    vpbroadcastd         m7, [o(vvc_pw_3803_1321)]
    vpbroadcastd         m8, [o(vvc_pw_m1321_2482)]
    vpbroadcastd         m9, [o(vvc_pw_2482_3344)]
    punpcklwd            m4, m2, m0 ; in2 in0 l
    punpckhwd            m2, m0     ; in2 in0 h
    psrld                m5, m6, 16
    pmaddwd             m10, m6, m4 ; t2:02 l
    pmaddwd              m6, m2     ; t2:02 h
    pmaddwd              m0, m7, m4 ; t0:02 l
    pmaddwd              m7, m2     ; t0:02 h
    pmaddwd              m4, m8     ; t1:02 l
    pmaddwd              m8, m2     ; t1:02 h
    punpckhwd            m2, m3, m1 ; in3 in1 h
    punpcklwd            m3, m1     ; in3 in1 l
    pmaddwd              m1, m5, m2 ; t2:3 h
    pmaddwd              m5, m3     ; t2:3 l
    paddd                m6, m1
    vpbroadcastd         m1, [o(vvc_pd_64)]
    paddd               m10, m5
    pmaddwd              m5, m9, m3
    pmaddwd              m9, m2
    paddd                m0, m1
    paddd                m7, m1
    paddd                m0, m5     ; t0 + t3 + 2048 l
    paddd                m7, m9     ; t0 + t3 + 2048 h
    vpbroadcastd         m9, [o(vvc_pw_m3803_3344)]
    pmaddwd              m5, m9, m2
    pmaddwd              m9, m3
    paddd               m10, m1     ; t2 + 2048 l
    paddd                m6, m1     ; t2 + 2048 h
    paddd                m5, m1     ; t1:13 + 2048 h
    paddd                m1, m9     ; t1:13 + 2048 l
    vpbroadcastd         m9, [o(vvc_pw_m3803_m6688)]
    pmaddwd              m2, m9
    pmaddwd              m3, m9
    paddd                m5, m8     ; t1 + t3 + 2048 h
    paddd                m1, m4     ; t1 + t3 + 2048 l
    paddd                m8, m7
    paddd                m4, m0
    paddd                m2, m8     ; t0 + t1 - t3 + 2048 h
    paddd                m3, m4     ; t0 + t1 - t3 + 2048 l
    REPX      {psrad x,  7}, m10, m6, m0, m7, m5, m1, m2, m3
    packssdw             m0, m7
    packssdw             m1, m5
    packssdw             m3, m2
    packssdw             m2, m10, m6
    ret

INV_TXFM_16X4_FN flipadst, dct2
INV_TXFM_16X4_FN flipadst, adst
INV_TXFM_16X4_FN flipadst, flipadst
INV_TXFM_16X4_FN flipadst, identity

cglobal iflipadst_16x4_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    vpermq               m0, [cq+32*0], q1230
    vpermq               m3, [cq+32*3], q2103
    vpermq               m1, [cq+32*1], q1230
    vpermq               m2, [cq+32*2], q2103
    call m(iadst_4x16_internal_8).main2
    call m(iadst_4x16_internal_8).main_pass1_end
    punpckhwd            m4, m3, m2
    punpckhwd            m5, m1, m0
    punpcklwd            m0, m2
    punpcklwd            m1, m3
    vpbroadcastd         m6, [o(vvc_pw_16384)]
    vinserti128          m3, m0, xm1, 1
    vperm2i128           m2, m0, m1, 0x31
    vinserti128          m0, m4, xm5, 1
    vperm2i128           m4, m4, m5, 0x31
    psubw                m1, m7, m6
    jmp m(iadst_16x4_internal_8).pass1_end
ALIGN function_align
.pass2:
    call m(iadst_16x4_internal_8).main
    vpbroadcastd         m4, [o(vvc_pw_2048)]
    REPX   {pmulhrsw x, m4}, m3, m2, m1, m0
    pxor                 m4, m4
    mova          [cq+32*0], m4
    mova          [cq+32*1], m4
    mova          [cq+32*2], m4
    mova          [cq+32*3], m4
    WRITE_16X2            3, 2, 4, 5, strideq*0, strideq*1
    lea                dstq, [dstq+strideq*2]
    WRITE_16X2            1, 0, 4, 5, strideq*0, strideq*1
    RET

INV_TXFM_16X4_FN identity, dct2
INV_TXFM_16X4_FN identity, adst
INV_TXFM_16X4_FN identity, flipadst
INV_TXFM_16X4_FN identity, identity

cglobal iidentity_16x4_internal_8, 0, 5, 11, dst, stride, c, eob, tx2
    mova                xm2, [cq+16*0]
    mova                xm4, [cq+16*1]
    vinserti128          m2, [cq+16*4], 1
    vinserti128          m4, [cq+16*5], 1
    mova                xm0, [cq+16*2]
    mova                xm1, [cq+16*3]
    vinserti128          m0, [cq+16*6], 1
    vinserti128          m1, [cq+16*7], 1
    vpbroadcastd         m7, [o(vvc_pw_1697x16)]
    vpbroadcastd         m8, [o(vvc_pw_16384)]
    punpcklwd            m3, m2, m4
    punpckhwd            m2, m4
    punpcklwd            m4, m0, m1
    punpckhwd            m0, m1
    punpcklwd            m1, m3, m2
    punpckhwd            m3, m2
    punpcklwd            m2, m4, m0
    punpckhwd            m4, m0
    pmulhrsw             m0, m7, m1
    pmulhrsw             m5, m7, m2
    pmulhrsw             m6, m7, m3
    pmulhrsw             m7, m4
    REPX   {pmulhrsw x, m8}, m0, m5, m6, m7
    paddsw               m1, m0
    paddsw               m2, m5
    paddsw               m3, m6
    paddsw               m4, m7
    punpcklqdq           m0, m1, m2
    punpckhqdq           m1, m2
    punpcklqdq           m2, m3, m4
    punpckhqdq           m3, m4
    jmp                tx2q
.pass2:
    vpbroadcastd         m7, [o(vvc_pw_1697x8)]
    pmulhrsw             m4, m7, m0
    pmulhrsw             m5, m7, m1
    pmulhrsw             m6, m7, m2
    pmulhrsw             m7, m3
    paddsw               m0, m4
    paddsw               m1, m5
    paddsw               m2, m6
    paddsw               m3, m7
    jmp m(iadst_16x4_internal_8).end

%macro INV_TXFM_16X8_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 16x8
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    or                  r3d, 8
    jmp m(vvc_inv_dct2_dct2_16x4_8).dconly
%endif
%endmacro

%macro ITX_16X8_LOAD_COEFS 1 ; shuf_odd
    vpbroadcastd         m8, [o(vvc_pw_64x8)]
    vpermq               m0, [cq+32*0], q3120
    add                  cq, 32*4
    vpermq               m7, [cq+32*3], q%1
    vpermq               m1, [cq-32*3], q%1
    vpermq               m6, [cq+32*2], q3120
    vpermq               m2, [cq-32*2], q3120
    vpermq               m5, [cq+32*1], q%1
    vpermq               m3, [cq-32*1], q%1
    vpermq               m4, [cq+32*0], q3120
    REPX   {pmulhrsw x, m8}, m0, m7, m1, m6, m2, m5, m3, m4
%endmacro

INV_TXFM_16X8_FN dct2, dct2
INV_TXFM_16X8_FN dct2, adst
INV_TXFM_16X8_FN dct2, flipadst
INV_TXFM_16X8_FN dct2, identity

cglobal idct2_16x8_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    ITX_16X8_LOAD_COEFS 3120
    call m(idct2_8x16_internal_8).main
    vpbroadcastd        m10, [o(vvc_pw_16384)]
    punpckhwd            m8, m0, m2
    punpcklwd            m0, m2
    punpckhwd            m2, m1, m3
    punpcklwd            m1, m3
    punpcklwd            m9, m4, m6
    punpckhwd            m4, m6
    punpcklwd            m6, m5, m7
    punpckhwd            m5, m7
    REPX  {pmulhrsw x, m10}, m8, m1, m4, m6
.pass1_end:
    REPX  {pmulhrsw x, m10}, m0, m2, m9, m5
    punpckhwd            m3, m0, m8
    punpcklwd            m0, m8
    punpckhwd            m8, m2, m1
    punpcklwd            m2, m1
    punpcklwd            m7, m9, m4
    punpckhwd            m9, m4
    punpcklwd            m4, m5, m6
    punpckhwd            m5, m6
    punpckhdq            m1, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m3, m8
    punpckhdq            m3, m8
    punpckldq            m6, m7, m4
    punpckhdq            m7, m4
    punpckldq            m8, m9, m5
    punpckhdq            m9, m5
    vperm2i128           m4, m0, m6, 0x31
    vinserti128          m0, xm6, 1
    vperm2i128           m5, m1, m7, 0x31
    vinserti128          m1, xm7, 1
    vperm2i128           m6, m2, m8, 0x31
    vinserti128          m2, xm8, 1
    vperm2i128           m7, m3, m9, 0x31
    vinserti128          m3, xm9, 1
    jmp                tx2q
.pass2:
    call .main
    vpbroadcastd         m8, [o(vvc_pw_2048)]
.end:
    REPX   {pmulhrsw x, m8}, m0, m2, m4, m6
.end2:
    REPX   {pmulhrsw x, m8}, m1, m3, m5, m7
    lea                  r3, [strideq*3]
    WRITE_16X2            0, 1, 8, 0, strideq*0, strideq*1
    WRITE_16X2            2, 3, 0, 1, strideq*2, r3
.end3:
    pxor                 m0, m0
    REPX {mova [cq+32*x], m0}, -4, -3, -2, -1, 0, 1, 2, 3
.end4:
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            4, 5, 0, 1, strideq*0, strideq*1
    WRITE_16X2            6, 7, 0, 1, strideq*2, r3
    RET
ALIGN function_align
cglobal_label .main
    vpbroadcastd        m10, [o(vvc_pd_64)]
.main2:
    IDCT2_8_1D              0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10
    ret

INV_TXFM_16X8_FN adst, dct2
INV_TXFM_16X8_FN adst, adst
INV_TXFM_16X8_FN adst, flipadst
INV_TXFM_16X8_FN adst, identity

cglobal iadst_16x8_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    ITX_16X8_LOAD_COEFS 1302
    call m(iadst_8x16_internal_8).main2
    call m(iadst_8x16_internal_8).main_pass1_end
    psubw               m11, m9, m10
    punpcklwd            m8, m0, m2
    punpckhwd            m0, m2
    punpckhwd            m2, m1, m3
    punpcklwd            m1, m3
    punpcklwd            m9, m4, m6
    punpckhwd            m4, m6
    punpckhwd            m6, m5, m7
    punpcklwd            m5, m7
    REPX  {pmulhrsw x, m11}, m8, m1, m4, m6
    jmp m(idct2_16x8_internal_8).pass1_end
ALIGN function_align
.pass2:
    call .main
    call .main_pass2_end
    pxor                 m8, m8
    psubw                m8, m9
    REPX   {pmulhrsw x, m9}, m0, m2, m4, m6
    jmp m(idct2_16x8_internal_8).end2
ALIGN function_align
cglobal_label .main
    vpbroadcastd        m10, [o(vvc_pd_64)]
    ITX_MULSUB_2W         7, 0, 8, 9, 10,  9, 90, 0 ; t1a, t0a
    ITX_MULSUB_2W         3, 4, 8, 9, 10, 70, 57, 0 ; t5a, t4a
    ITX_MULSUB_2W         1, 6, 8, 9, 10, 87, 25, 0 ; t7a, t6a
    ITX_MULSUB_2W         5, 2, 8, 9, 10, 43, 80, 0 ; t3a, t2a
    psubsw               m8, m2, m6 ; t6
    paddsw               m2, m6     ; t2
    psubsw               m6, m0, m4 ; t4
    paddsw               m0, m4     ; t0
    psubsw               m4, m5, m1 ; t7
    paddsw               m5, m1     ; t3
    psubsw               m1, m7, m3 ; t5
    paddsw               m7, m3     ; t1
    ITX_MULSUB_2W         6, 1, 3, 9, 10, 36, 83, 0 ; t5a, t4a
    ITX_MULSUB_2W         4, 8, 3, 9, 10, 83, 36, 0 ; t6a, t7a
    psubsw               m9, m6, m8 ;  t7
    paddsw               m6, m8     ;  out6
    psubsw               m3, m7, m5 ;  t3
    paddsw               m7, m5     ; -out7
    psubsw               m5, m0, m2 ;  t2
    paddsw               m0, m2     ;  out0
    psubsw               m2, m1, m4 ;  t6
    paddsw               m1, m4     ; -out1
    ret
ALIGN function_align
.main_pass1_end:
    vpbroadcastd        m11, [o(vvc_pw_m64_64)]
    vpbroadcastd        m12, [o(vvc_pw_64_64)]
    punpckhwd            m4, m3, m5
    punpcklwd            m3, m5
    pmaddwd              m5, m11, m4
    pmaddwd              m4, m12
    pmaddwd              m8, m11, m3
    pmaddwd              m3, m12
    REPX     {paddd x, m10}, m5, m4, m8, m3
    REPX     {psrad x,  7 }, m5, m8, m4, m3
    packssdw             m3, m4     ; -out3
    packssdw             m4, m8, m5 ;  out4
    punpcklwd            m5, m9, m2
    punpckhwd            m9, m2
    pmaddwd              m2, m12, m5
    pmaddwd              m5, m11
    pmaddwd             m12, m9
    pmaddwd             m11, m9
    REPX     {paddd x, m10}, m2, m5, m12, m11
    REPX     {psrad x,  7 }, m2, m7, m5, m11
    packssdw             m2, m12    ;  out2
    packssdw             m5, m11    ; -out5
    ret
ALIGN function_align
cglobal_label .main_pass2_end
    vpbroadcastd         m8, [o(vvc_pw_64x8)]
    psubsw               m4, m5, m3
    paddsw               m3, m5
    psubsw               m5, m2, m9
    paddsw               m2, m9
    pmulhrsw             m2, m8     ;  out2
    pmulhrsw             m3, m8     ; -out3
    pmulhrsw             m4, m8     ;  out4
    pmulhrsw             m5, m8     ; -out5
    vpbroadcastd         m9, [o(vvc_pw_2048)]
    ret

INV_TXFM_16X8_FN flipadst, dct2
INV_TXFM_16X8_FN flipadst, adst
INV_TXFM_16X8_FN flipadst, flipadst
INV_TXFM_16X8_FN flipadst, identity

cglobal iflipadst_16x8_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    ITX_16X8_LOAD_COEFS 1302
    call m(iadst_8x16_internal_8).main2
    call m(iadst_8x16_internal_8).main_pass1_end
    psubw                m9, m10
    punpcklwd            m8, m6, m4
    punpckhwd            m6, m4
    punpcklwd            m4, m7, m5
    punpckhwd            m7, m5
    punpckhwd            m5, m3, m1
    punpcklwd            m3, m1
    punpckhwd            m1, m2, m0
    punpcklwd            m2, m0
    REPX  {pmulhrsw x, m10}, m8, m4, m5, m1
    REPX  {pmulhrsw x, m9 }, m6, m7, m3, m2
    punpcklwd            m0, m7, m4
    punpckhwd            m7, m4
    punpckhwd            m4, m6, m8
    punpcklwd            m6, m8
    punpckhwd            m8, m3, m5
    punpcklwd            m3, m5
    punpcklwd            m5, m2, m1
    punpckhwd            m2, m1
    punpckhdq            m1, m0, m6
    punpckldq            m0, m6
    punpckldq            m6, m7, m4
    punpckhdq            m7, m4
    punpckhdq            m4, m3, m5
    punpckldq            m3, m5
    punpckldq            m5, m8, m2
    punpckhdq            m8, m2
    vinserti128          m2, m6, xm5, 1
    vperm2i128           m6, m5, 0x31
    vperm2i128           m5, m1, m4, 0x31
    vinserti128          m1, xm4, 1
    vperm2i128           m4, m0, m3, 0x31
    vinserti128          m0, xm3, 1
    vinserti128          m3, m7, xm8, 1
    vperm2i128           m7, m8, 0x31
    jmp                tx2q
.pass2:
    call m(iadst_16x8_internal_8).main
    call m(iadst_16x8_internal_8).main_pass2_end
    pxor                 m8, m8
    psubw                m8, m9
    pmulhrsw            m10, m7, m8
    pmulhrsw             m7, m0, m9
    pmulhrsw             m0, m6, m9
    pmulhrsw             m6, m1, m8
    pmulhrsw             m1, m5, m8
    pmulhrsw             m5, m2, m9
    pmulhrsw             m2, m4, m9
    pmulhrsw             m4, m3, m8
    lea                  r3, [strideq*3]
    WRITE_16X2           10, 0, 8, 9, strideq*0, strideq*1
    WRITE_16X2            1, 2, 0, 1, strideq*2, r3
    jmp m(idct2_16x8_internal_8).end3

INV_TXFM_16X8_FN identity, dct2
INV_TXFM_16X8_FN identity, adst
INV_TXFM_16X8_FN identity, flipadst
INV_TXFM_16X8_FN identity, identity

cglobal iidentity_16x8_internal_8, 0, 5, 13, dst, stride, c, eob, tx2
    mova                xm7, [cq+16*0]
    mova                xm2, [cq+16*1]
    add                  cq, 16*8
    vpbroadcastd         m3, [o(vvc_pw_64x8)]
    vinserti128          m7, [cq+16*0], 1
    vinserti128          m2, [cq+16*1], 1
    mova                xm6, [cq-16*6]
    mova                xm4, [cq-16*5]
    vinserti128          m6, [cq+16*2], 1
    vinserti128          m4, [cq+16*3], 1
    mova                xm8, [cq-16*4]
    mova                xm5, [cq-16*3]
    vinserti128          m8, [cq+16*4], 1
    vinserti128          m5, [cq+16*5], 1
    mova                xm0, [cq-16*2]
    mova                xm1, [cq-16*1]
    vinserti128          m0, [cq+16*6], 1
    vinserti128          m1, [cq+16*7], 1
    vpbroadcastd        m10, [o(vvc_pw_1697x16)]
    vpbroadcastd        m11, [o(vvc_pw_16384)]
    REPX   {pmulhrsw x, m3}, m7, m2, m6, m4, m8, m5, m0, m1
    punpcklwd            m3, m7, m2
    punpckhwd            m7, m2
    punpcklwd            m2, m6, m4
    punpckhwd            m6, m4
    punpcklwd            m4, m8, m5
    punpckhwd            m8, m5
    punpcklwd            m5, m0, m1
    punpckhwd            m0, m1
    punpckldq            m1, m3, m2
    punpckhdq            m3, m2
    punpckldq            m2, m4, m5
    punpckhdq            m4, m5
    punpckldq            m5, m7, m6
    punpckhdq            m7, m6
    punpckldq            m6, m8, m0
    punpckhdq            m8, m0
    REPX {IDTX16 x, 0, 10, 11}, 1, 3, 2, 4, 5, 7, 6, 8
    punpcklqdq           m0, m1, m2
    punpckhqdq           m1, m2
    punpcklqdq           m2, m3, m4
    punpckhqdq           m3, m4
    punpcklqdq           m4, m5, m6
    punpckhqdq           m5, m6
    punpcklqdq           m6, m7, m8
    punpckhqdq           m7, m8
    jmp                tx2q
.pass2:
    vpbroadcastd         m8, [o(vvc_pw_64)]
    jmp m(idct2_16x8_internal_8).end

%define o_base vvc_pw_5 + 128

%macro INV_TXFM_16X16_FN 2 ; type1, type2
    INV_TXFM_FN          %1, %2, 16x16
%ifidn %1_%2, dct2_dct2
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_8192)]
    mov                [cq], eobd
    or                  r3d, 16
    jmp m(vvc_inv_dct2_dct2_16x4_8).dconly
%endif
%endmacro

%macro ITX_16X16_LOAD_COEFS 0
    mova                 m0, [cq+32*0]
    mova                 m1, [cq+32*1]
    mova                 m2, [cq+32*2]
    mova                 m3, [cq+32*3]
    add                  cq, 32*8
    mova                 m4, [cq-32*4]
    mova                 m5, [cq-32*3]
    mova                 m6, [cq-32*2]
    mova                 m7, [cq-32*1]
    mova                 m8, [cq+32*0]
    mova                 m9, [cq+32*1]
    mova                m10, [cq+32*2]
    mova                m11, [cq+32*3]
    mova                m12, [cq+32*4]
    mova                m13, [cq+32*5]
    mova                m14, [cq+32*6]
    mova                m15, [cq+32*7]
    mova              [rsp], m15
%endmacro

INV_TXFM_16X16_FN dct2, dct2
INV_TXFM_16X16_FN dct2, adst
INV_TXFM_16X16_FN dct2, flipadst
INV_TXFM_16X16_FN dct2, identity

cglobal idct2_16x16_internal_8, 0, 5, 16, 32*3, dst, stride, c, eob, tx2
    ITX_16X16_LOAD_COEFS
    call .main
.pass1_end:
    vpbroadcastd         m1, [o(vvc_pw_8192)]
    REPX   {pmulhrsw x, m1}, m0, m2, m4, m6, m8, m10, m12, m14
    vextracti128 [rsp+16*5], m8, 1
    mova         [rsp+16*1], xm8
.pass1_end2:
    vextracti128 [rsp+16*4], m0, 1
    mova         [rsp+16*0], xm0
    REPX   {pmulhrsw x, m1}, m3, m5, m7, m9, m11, m13, m15
    pmulhrsw             m1, [rsp+32*1]
    vperm2i128           m8, m1, m9, 0x31
    vinserti128          m1, xm9, 1
    vperm2i128           m9, m2, m10, 0x31
    vinserti128          m2, xm10, 1
    vperm2i128          m10, m3, m11, 0x31
    vinserti128          m3, xm11, 1
    vperm2i128          m11, m4, m12, 0x31
    vinserti128          m4, xm12, 1
    vperm2i128          m12, m5, m13, 0x31
    vinserti128          m5, xm13, 1
    vperm2i128          m13, m6, m14, 0x31
    vinserti128          m6, xm14, 1
    vperm2i128          m14, m7, m15, 0x31
    vinserti128          m7, xm15, 1
    mova                m15, [rsp+32*2]
.pass1_end3:
    punpcklwd            m0, m9, m10
    punpckhwd            m9, m10
    punpcklwd           m10, m15, m8
    punpckhwd           m15, m8
    punpckhwd            m8, m11, m12
    punpcklwd           m11, m12
    punpckhwd           m12, m13, m14
    punpcklwd           m13, m14
    punpckhdq           m14, m11, m13
    punpckldq           m11, m13
    punpckldq           m13, m15, m9
    punpckhdq           m15, m9
    punpckldq            m9, m10, m0
    punpckhdq           m10, m0
    punpckhdq            m0, m8, m12
    punpckldq            m8, m12
    punpcklqdq          m12, m13, m8
    punpckhqdq          m13, m8
    punpcklqdq           m8, m9, m11
    punpckhqdq           m9, m11
    punpckhqdq          m11, m10, m14
    punpcklqdq          m10, m14
    punpcklqdq          m14, m15, m0
    punpckhqdq          m15, m0
    mova                 m0, [rsp]
    mova              [rsp], m15
    punpckhwd           m15, m4, m5
    punpcklwd            m4, m5
    punpckhwd            m5, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m6, m7
    punpcklwd            m6, m7
    punpckhwd            m7, m2, m3
    punpcklwd            m2, m3
    punpckhdq            m3, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m4, m6
    punpckhdq            m4, m6
    punpckhdq            m6, m5, m7
    punpckldq            m5, m7
    punpckldq            m7, m15, m1
    punpckhdq           m15, m1
    punpckhqdq           m1, m0, m2
    punpcklqdq           m0, m2
    punpcklqdq           m2, m3, m4
    punpckhqdq           m3, m4
    punpcklqdq           m4, m5, m7
    punpckhqdq           m5, m7
    punpckhqdq           m7, m6, m15
    punpcklqdq           m6, m15
    jmp                tx2q
.pass2:
    call .main
.end:
    vpbroadcastd         m1, [o(vvc_pw_2048)]
    REPX   {pmulhrsw x, m1}, m0, m2, m4, m6, m8, m10, m12, m14
    mova              [rsp], m6
.end2:
    REPX   {pmulhrsw x, m1}, m3, m5, m7, m9, m11, m13, m15
    pmulhrsw             m1, [rsp+32*1]
    lea                  r3, [strideq*3]
    WRITE_16X2            0,  1,  6,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2        [rsp],  7,  0,  1, strideq*2, r3
.end3:
    pxor                 m2, m2
    REPX {mova [cq+32*x], m2}, -8, -7, -6, -5, -4, -3, -2, -1
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            8,  9,  0,  1, strideq*0, strideq*1
    WRITE_16X2           10, 11,  0,  1, strideq*2, r3
    REPX {mova [cq+32*x], m2},  0,  1,  2,  3,  4,  5,  6,  7
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2           12, 13,  0,  1, strideq*0, strideq*1
    WRITE_16X2           14, 15,  0,  1, strideq*2, r3
    RET
ALIGN function_align
cglobal_label .main
    vpbroadcastd        m15, [o(vvc_pd_64)]
    mova [rsp+gprsize+32*1], m1
    mova [rsp+gprsize+32*2], m9
    IDCT2_8_1D              0,  2,  4,  6,  8, 10, 12, 14,  1,  9, 15
    mova                 m1, [rsp+gprsize+32*2] ; in9
    mova [rsp+gprsize+32*2], m14 ; tmp7
    mova                 m9, [rsp+gprsize+32*1] ; in1
    mova [rsp+gprsize+32*1], m10 ; tmp5
    mova                m14, [rsp+gprsize+32*0] ; in15
    mova [rsp+gprsize+32*0], m6  ; tmp3
    IDCT2_16_1D_ODDHALF     9,  3,  5,  7,  1, 11, 13, 14,  6, 10, 15
    mova                 m6, [rsp+gprsize+32*1] ; tmp5
    psubsw              m15, m0, m14  ; out15
    paddsw               m0, m14      ; out0
    psubsw              m14, m2, m13  ; out14
    paddsw               m2, m13      ; out1
    mova [rsp+gprsize+32*1], m2
    psubsw              m13, m4, m11  ; out13
    paddsw               m2, m4, m11  ; out2
    psubsw              m11, m8, m7   ; out11
    paddsw               m4, m8, m7   ; out4
    mova                 m7, [rsp+gprsize+32*2] ; tmp7
    psubsw              m10, m6, m5   ; out10
    paddsw               m5, m6       ; out5
    psubsw               m8, m7, m9   ; out8
    paddsw               m7, m9       ; out7
    psubsw               m9, m12, m3  ; out9
    paddsw               m6, m12, m3  ; out6
    mova                 m3, [rsp+gprsize+32*0] ; tmp3
    psubsw              m12, m3, m1   ; out12
    paddsw               m3, m1       ; out3
    ret

INV_TXFM_16X16_FN adst, dct2
INV_TXFM_16X16_FN adst, adst
INV_TXFM_16X16_FN adst, flipadst

cglobal iadst_16x16_internal_8, 0, 5, 16, 32*3, dst, stride, c, eob, tx2
    ITX_16X16_LOAD_COEFS
    call .main
    call .main_pass1_end
    pmulhrsw             m0, m1, [cq+32*0]
    pmulhrsw             m2, m1, [cq+32*1]
    REPX   {pmulhrsw x, m1}, m4, m6, m8, m10
    pmulhrsw            m12, m1, [cq+32*2]
    pmulhrsw            m14, m1, [cq+32*3]
    vextracti128 [rsp+16*5], m8, 1
    mova         [rsp+16*1], xm8
    pxor                 m8, m8
    psubw                m1, m8, m1
    jmp m(idct2_16x16_internal_8).pass1_end2
ALIGN function_align
.pass2:
    call .main
    call .main_pass2_end
    REPX   {pmulhrsw x, m1}, m0, m2, m4, m6, m8, m10, m12, m14
    mova         [rsp+32*0], m6
    pxor                 m6, m6
    psubw                m1, m6, m1
    jmp m(idct2_16x16_internal_8).end2
ALIGN function_align
cglobal_label .main
    vpbroadcastd        m15, [o(vvc_pd_64)]
    mova [rsp+gprsize+32*1], m0
    mova [rsp+gprsize+32*2], m4
    ITX_MULSUB_2W        13,  2,  0,  4, 15,  22, 88, 0 ; t3,  t2
    ITX_MULSUB_2W         9,  6,  0,  4, 15, 54, 73, 0 ; t7,  t6
    ITX_MULSUB_2W         5, 10,  0,  4, 15, 78, 46, 0 ; t11, t10
    ITX_MULSUB_2W         1, 14,  0,  4, 15, 90,  13, 0 ; t15, t14
    psubsw               m0, m2, m10  ; t10a
    paddsw               m2, m10      ; t2a
    psubsw              m10, m13, m5  ; t11a
    paddsw              m13, m5       ; t3a
    psubsw               m5, m6, m14  ; t14a
    paddsw               m6, m14      ; t6a
    psubsw              m14, m9, m1   ; t15a
    paddsw               m9, m1       ; t7a
    ITX_MULSUB_2W         0, 10,  1,  4, 15, 75, 50, 0 ; t11, t10
    ITX_MULSUB_2W        14,  5,  1,  4, 15, 50, 75, 0 ; t14, t15
    psubsw               m1, m10, m14 ; t14a
    paddsw              m10, m14      ; t10a
    psubsw              m14, m0, m5   ; t15a
    paddsw               m0, m5       ; t11a
    psubsw               m5, m2, m6   ; t6
    paddsw               m2, m6       ; t2
    psubsw               m6, m13, m9  ; t7
    paddsw              m13, m9       ; t3
    ITX_MULSUB_2W         6,  5,  4,  9, 15, 83, 36, 0 ; t6a, t7a
    ITX_MULSUB_2W        14,  1,  4,  9, 15, 83, 36, 0 ; t14, t15
    mova                 m9, [rsp+gprsize+32*0] ; in15
    mova [rsp+gprsize+32*0], m10 ; t10a
    mova                 m4, [rsp+gprsize+32*1] ; in0
    mova [rsp+gprsize+32*1], m6  ; t6a
    mova                 m6, [rsp+gprsize+32*2] ; in4
    mova [rsp+gprsize+32*2], m2  ; t2
    ITX_MULSUB_2W         9,  4,  2, 10, 15,  4, 90, 0 ; t1,  t0
    ITX_MULSUB_2W        11,  6,  2, 10, 15, 38, 82, 0 ; t5,  t4
    ITX_MULSUB_2W         7,  8,  2, 10, 15, 67, 61, 0 ; t9,  t8
    ITX_MULSUB_2W         3, 12,  2, 10, 15, 85, 31, 0 ; t13, t12
    psubsw              m10, m4, m8  ; t8a
    paddsw               m8, m4      ; t0a
    psubsw               m4, m9, m7  ; t9a
    paddsw               m9, m7      ; t1a
    psubsw               m7, m6, m12 ; t12a
    paddsw               m6, m12     ; t4a
    psubsw              m12, m11, m3 ; t13a
    paddsw              m11, m3      ; t5a
    ITX_MULSUB_2W        10,  4,  2,  3, 15,  18, 89, 0 ; t9,  t8
    ITX_MULSUB_2W        12,  7,  2,  3, 15, 89,  18, 0 ; t12, t13
    psubsw               m3, m9, m11 ; t5
    paddsw               m9, m11     ; t1
    psubsw              m11, m4, m12 ; t12a
    paddsw               m4, m12     ; t8a
    paddsw              m12, m8, m6  ; t0
    psubsw               m8, m6      ; t4
    paddsw               m6, m10, m7 ; t9a
    psubsw              m10, m7      ; t13a
    ITX_MULSUB_2W         8,  3,  2,  7, 15, 36, 83, 0 ; t5a, t4a
    ITX_MULSUB_2W        11, 10,  2,  7, 15, 36, 83, 0 ; t13, t12
    mova                 m7, [rsp+gprsize+32*0] ; t10a
    mova                 m2, [rsp+gprsize+32*1] ; t6a
    paddsw              m15, m9, m13  ; -out15
    psubsw               m9, m13      ;  t3a
    paddsw              m13, m11, m1  ; -out13
    psubsw              m11, m1       ;  t15a
    psubsw               m1, m4, m7   ;  t10
    paddsw               m7, m4       ; -out1
    psubsw               m4, m3, m2   ;  t6
    paddsw               m3, m2       ; -out3
    paddsw               m2, m10, m14 ;  out2
    psubsw              m10, m14      ;  t14a
    paddsw              m14, m6, m0   ;  out14
    psubsw               m6, m0       ;  t11
    mova                 m0, [rsp+gprsize+32*2] ; t2
    mova [rsp+gprsize+32*1], m7
    psubsw               m7, m12, m0  ;  t2a
    paddsw               m0, m12      ;  out0
    paddsw              m12, m8, m5   ;  out12
    psubsw               m8, m5       ;  t7
    ret
ALIGN function_align
.main_pass1_end:
    mova          [cq+32*0], m0
    mova          [cq+32*1], m2
    mova          [cq+32*2], m12
    mova          [cq+32*3], m14
    vpbroadcastd        m14, [vvc_pw_m64_64]
    vpbroadcastd        m12, [vvc_pw_64_64]
    vpbroadcastd         m2, [vvc_pd_64]
    punpcklwd            m5, m11, m10
    punpckhwd           m11, m10
    pmaddwd             m10, m14, m5
    pmaddwd              m0, m14, m11
    pmaddwd              m5, m12
    pmaddwd             m11, m12
    REPX      {paddd x, m2}, m10, m0, m5, m11
    REPX      {psrad x,  7}, m10, m0, m5, m11
    packssdw            m10, m0  ;  out10
    packssdw             m5, m11 ; -out5
    punpcklwd           m11, m8, m4
    punpckhwd            m8, m4
    pmaddwd              m4, m12, m11
    pmaddwd              m0, m12, m8
    pmaddwd             m11, m14
    pmaddwd              m8, m14
    REPX      {paddd x, m2}, m4, m0, m11, m8
    REPX      {psrad x,  7}, m4, m0, m11, m8
    packssdw             m4, m0  ;  out4
    packssdw            m11, m8  ; -out11
    punpcklwd            m8, m9, m7
    punpckhwd            m9, m7
    pmaddwd              m7, m12, m8
    pmaddwd              m0, m12, m9
    pmaddwd              m8, m14
    pmaddwd              m9, m14
    REPX      {paddd x, m2}, m7, m0, m8, m9
    REPX      {psrad x,  7}, m7, m0, m8, m9
    packssdw             m7, m0  ; -out7
    packssdw             m8, m9  ;  out8
    punpckhwd            m0, m6, m1
    punpcklwd            m6, m1
    pmaddwd              m1, m14, m0
    pmaddwd              m9, m14, m6
    pmaddwd              m0, m12
    pmaddwd              m6, m12
    REPX      {paddd x, m2}, m1, m9, m0, m6
    REPX      {psrad x,  7}, m1, m9, m0, m6
    packssdw             m9, m1  ; -out7
    packssdw             m6, m0  ;  out8
    vpbroadcastd         m1, [o(vvc_pw_8192)]
    ret
ALIGN function_align
cglobal_label .main_pass2_end
    ; In pass 2 we're going to clip to pixels afterwards anyway, so clipping to
    ; 16-bit here will produce the same result as using 32-bit intermediates.
    paddsw               m5, m10, m11 ; -out5
    psubsw              m10, m11      ;  out10
    psubsw              m11, m4, m8   ; -out11
    paddsw               m4, m8       ;  out4
    psubsw               m8, m7, m9   ;  out8
    paddsw               m7, m9       ; -out7
    psubsw               m9, m1, m6   ; -out9
    paddsw               m6, m1       ;  out6
    vpbroadcastd         m1, [o(vvc_pw_64x8)]
    REPX   {pmulhrsw x, m1}, m4, m5, m6, m7, m8, m9, m10, m11
    vpbroadcastd         m1, [o(vvc_pw_2048)]
    ret

INV_TXFM_16X16_FN flipadst, dct2
INV_TXFM_16X16_FN flipadst, adst
INV_TXFM_16X16_FN flipadst, flipadst

cglobal iflipadst_16x16_internal_8, 0, 5, 16, 32*3, dst, stride, c, eob, tx2
    ITX_16X16_LOAD_COEFS
    call m(iadst_16x16_internal_8).main
    call m(iadst_16x16_internal_8).main_pass1_end
    pmulhrsw             m6, m1
    pmulhrsw             m2, m1, m8
    mova         [rsp+32*2], m6
    pmulhrsw             m6, m1, m4
    pmulhrsw             m4, m1, m10
    pmulhrsw             m8, m1, [cq+32*3]
    pmulhrsw            m10, m1, [cq+32*2]
    pmulhrsw            m12, m1, [cq+32*1]
    pmulhrsw            m14, m1, [cq+32*0]
    pxor                 m0, m0
    psubw                m0, m1
    REPX   {pmulhrsw x, m0}, m3, m5, m7, m11, m15
    pmulhrsw             m1, m0, m9
    pmulhrsw             m9, m0, m13
    pmulhrsw             m0, [rsp+32*1]
    mova         [rsp+16*0], xm15
    mova         [rsp+16*1], xm7
    vperm2i128          m15, m15, m7, 0x31
    vinserti128          m7, m2, xm14, 1
    vperm2i128          m14, m2, m14, 0x31
    vinserti128          m2, m9, xm5, 1
    vperm2i128           m9, m9, m5, 0x31
    vinserti128          m5, m4, xm12, 1
    vperm2i128          m12, m4, m12, 0x31
    vinserti128          m4, m11, xm3, 1
    vperm2i128          m11, m11, m3, 0x31
    vinserti128          m3, m10, xm6, 1
    vperm2i128          m10, m10, m6, 0x31
    vinserti128          m6, m1, xm0, 1
    vperm2i128          m13, m1, m0, 0x31
    vinserti128          m1, m8, [rsp+32*2], 1
    vperm2i128           m8, m8, [rsp+32*2], 0x31
    jmp m(idct2_16x16_internal_8).pass1_end3
.pass2:
    call m(iadst_16x16_internal_8).main
    call m(iadst_16x16_internal_8).main_pass2_end
    pmulhrsw             m0, m1
    pmulhrsw             m8, m1
    mova         [rsp+32*0], m0
    mova         [rsp+32*2], m8
    pxor                 m0, m0
    psubw                m0, m1
    pmulhrsw             m8, m0, m7
    pmulhrsw             m7, m0, m9
    pmulhrsw             m9, m1, m6
    pmulhrsw             m6, m1, m10
    pmulhrsw            m10, m0, m5
    pmulhrsw             m5, m0, m11
    pmulhrsw            m11, m1, m4
    pmulhrsw             m4, m1, m12
    pmulhrsw            m12, m0, m3
    pmulhrsw             m3, m0, m13
    pmulhrsw            m13, m1, m2
    pmulhrsw             m1, m14
    pmulhrsw            m14, m0, [rsp+32*1]
    pmulhrsw             m0, m15
    lea                  r3, [strideq*3]
    WRITE_16X2            0,  1,  2,  0, strideq*0, strideq*1
    mova                m15, [rsp+32*0]
    WRITE_16X2            3,  4,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            5,  6,  0,  1, strideq*0, strideq*1
    WRITE_16X2            7, [rsp+32*2],  0,  1, strideq*2, r3
    jmp m(idct2_16x16_internal_8).end3

%macro IDTX16B 3 ; src/dst, tmp, vvc_pw_1697x16
    pmulhrsw            m%2, m%3, m%1
    psraw               m%2, 1
    pavgw               m%1, m%2 ; signs are guaranteed to be equal
%endmacro

INV_TXFM_16X16_FN identity, dct2
INV_TXFM_16X16_FN identity, identity

cglobal iidentity_16x16_internal_8, 0, 5, 16, 32*3, dst, stride, c, eob, tx2
    vpbroadcastd         m7, [o(vvc_pw_1697x16)]
    mova                xm0, [cq+16* 0]
    vinserti128          m0, [cq+16*16], 1
    mova               xm15, [cq+16* 1]
    vinserti128         m15, [cq+16*17], 1
    mova                xm1, [cq+16* 2]
    vinserti128          m1, [cq+16*18], 1
    mova                xm8, [cq+16* 3]
    vinserti128          m8, [cq+16*19], 1
    mova                xm2, [cq+16* 4]
    vinserti128          m2, [cq+16*20], 1
    mova                xm9, [cq+16* 5]
    vinserti128          m9, [cq+16*21], 1
    mova                xm3, [cq+16* 6]
    vinserti128          m3, [cq+16*22], 1
    mova               xm10, [cq+16* 7]
    add                  cq, 16*16
    vinserti128         m10, [cq+16* 7], 1
    mova                xm4, [cq-16* 8]
    vinserti128          m4, [cq+16* 8], 1
    mova               xm11, [cq-16* 7]
    vinserti128         m11, [cq+16* 9], 1
    mova                xm5, [cq-16* 6]
    vinserti128          m5, [cq+16*10], 1
    mova               xm12, [cq-16* 5]
    vinserti128         m12, [cq+16*11], 1
    mova               xm13, [cq-16* 3]
    vinserti128         m13, [cq+16*13], 1
    mova               xm14, [cq-16* 1]
    vinserti128         m14, [cq+16*15], 1
    REPX  {IDTX16B x, 6, 7},  0, 15,  1,  8,  2,  9,  3, \
                             10,  4, 11,  5, 12, 13, 14
    mova                xm6, [cq-16* 4]
    vinserti128          m6, [cq+16*12], 1
    mova              [rsp], m0
    IDTX16B               6, 0, 7
    mova                xm0, [cq-16* 2]
    vinserti128          m0, [cq+16*14], 1
    pmulhrsw             m7, m0
    psraw                m7, 1
    pavgw                m7, m0
    jmp m(idct2_16x16_internal_8).pass1_end3
ALIGN function_align
.pass2:
    vpbroadcastd        m15, [o(vvc_pw_1697x16)]
    mova         [rsp+32*1], m0
    REPX  {IDTX16 x, 0, 15},  1,  2,  3,  4,  5,  6,  7, \
                              8,  9, 10, 11, 12, 13, 14
    mova                 m0, [rsp+32*1]
    mova         [rsp+32*1], m1
    IDTX16                0, 1, 15
    mova                 m1, [rsp+32*0]
    pmulhrsw            m15, m1
    paddsw               m1, m1
    paddsw              m15, m1
    jmp m(idct2_16x16_internal_8).end

%define o_base deint_shuf + 128

%macro LOAD_8ROWS 2-3 0 ; src, stride, is_rect2
%if %3
    vpbroadcastd        m15, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m15, [%1+%2*0]
    pmulhrsw             m1, m15, [%1+%2*1]
    pmulhrsw             m2, m15, [%1+%2*2]
    pmulhrsw             m3, m15, [%1+%2*3]
    pmulhrsw             m4, m15, [%1+%2*4]
    pmulhrsw             m5, m15, [%1+%2*5]
    pmulhrsw             m6, m15, [%1+%2*6]
    pmulhrsw             m7, m15, [%1+%2*7]
%else
    mova                 m0, [%1+%2*0]
    mova                 m1, [%1+%2*1]
    mova                 m2, [%1+%2*2]
    mova                 m3, [%1+%2*3]
    mova                 m4, [%1+%2*4]
    mova                 m5, [%1+%2*5]
    mova                 m6, [%1+%2*6]
    mova                 m7, [%1+%2*7]
%endif
%endmacro

%macro LOAD_8ROWS_H 2-3 0 ; src, stride, is_rect2
%if %3
%if %3 == 1
    vpbroadcastd        m15, [o(vvc_pw_64x8)]
%endif
    pmulhrsw             m8, m15, [%1+%2*0]
    pmulhrsw             m9, m15, [%1+%2*1]
    pmulhrsw            m10, m15, [%1+%2*2]
    pmulhrsw            m11, m15, [%1+%2*3]
    pmulhrsw            m12, m15, [%1+%2*4]
    pmulhrsw            m13, m15, [%1+%2*5]
    pmulhrsw            m14, m15, [%1+%2*6]
    pmulhrsw            m15,      [%1+%2*7]
%else
    mova                 m8, [%1+%2*0]
    mova                 m9, [%1+%2*1]
    mova                m10, [%1+%2*2]
    mova                m11, [%1+%2*3]
    mova                m12, [%1+%2*4]
    mova                m13, [%1+%2*5]
    mova                m14, [%1+%2*6]
    mova                m15, [%1+%2*7]
%endif
%endmacro

%macro ITX_UNPACK_MULHRSW 7 ; dst1, dst2/src, tmp, coef[1-4]
    vpbroadcastd        m%3, [r5-vvc_pw_4_90x8+vvc_pw_%4_%5x8]
    punpcklwd           m%1, m%2, m%2
    pmulhrsw            m%1, m%3
    vpbroadcastd        m%3, [r5-vvc_pw_4_90x8+vvc_pw_%6_%7x8]
    punpckhwd           m%2, m%2
    pmulhrsw            m%2, m%3
%endmacro

cglobal vvc_inv_dct2_dct2_8x32_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jz .dconly
    PROLOGUE              0, 4, 16, 32*3, dst, stride, c, eob
    %undef cmp
    cmp                eobd, 106
    jle .fast
    LOAD_8ROWS      cq+32*1, 32*2
    call m(idct2_16x8_internal_8).main
    vperm2i128          m11, m0, m4, 0x31
    vinserti128          m0, xm4, 1
    vperm2i128           m4, m1, m5, 0x31
    vinserti128          m1, xm5, 1
    vperm2i128           m5, m2, m6, 0x31
    vinserti128          m2, xm6, 1
    vperm2i128           m6, m3, m7, 0x31
    vinserti128          m3, xm7, 1
    pxor                 m7, m7
    REPX {mova [cq+32*x], m7}, 1, 3, 5, 7, 9, 11, 13, 15
    punpckhwd            m7, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m2, m3
    punpcklwd            m2, m3
    punpcklwd            m3, m11, m4
    punpckhwd           m11, m4
    punpckhwd            m4, m5, m6
    punpcklwd            m5, m6
    punpckhdq            m6, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m3, m5
    punpckhdq            m3, m5
    punpckhdq            m5, m11, m4
    punpckldq           m11, m4
    punpckldq            m4, m7, m1
    punpckhdq            m7, m1
    punpckhqdq          m12, m6, m0
    punpcklqdq           m0, m6     ; out4
    punpckhqdq          m13, m7, m4
    punpcklqdq           m4, m7     ; out5
    punpckhqdq          m14, m3, m2
    punpcklqdq           m2, m3     ; out6
    punpckhqdq          m15, m5, m11
    punpcklqdq          m11, m5     ; out7
    mova         [rsp+32*0], m0
    mova         [rsp+32*1], m4
    mova         [rsp+32*2], m2
.fast:
    LOAD_8ROWS      cq+32*0, 32*2
    call m(idct2_16x8_internal_8).main
    vperm2i128           m8, m0, m4, 0x31
    vinserti128          m0, xm4, 1
    vperm2i128           m4, m1, m5, 0x31
    vinserti128          m1, xm5, 1
    vperm2i128           m5, m2, m6, 0x31
    vinserti128          m2, xm6, 1
    vperm2i128           m6, m3, m7, 0x31
    vinserti128          m3, xm7, 1
    vpbroadcastd         m9, [o(vvc_pw_8192)]
    pxor                 m7, m7
    REPX {mova [cq+32*x], m7}, 0, 2, 4, 6, 8, 10, 12, 14
    punpckhwd            m7, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m2, m3
    punpcklwd            m2, m3
    punpckhwd            m3, m8, m4
    punpcklwd            m8, m4
    punpckhwd            m4, m5, m6
    punpcklwd            m5, m6
    punpckhdq            m6, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m8, m5
    punpckhdq            m8, m5
    punpckhdq            m5, m3, m4
    punpckldq            m3, m4
    punpckhdq            m4, m7, m1
    punpckldq            m7, m1
    punpcklqdq           m1, m7, m4
    punpckhqdq           m7, m4     ; out9
    punpckhqdq           m4, m2, m8 ; out10
    punpcklqdq           m2, m8
    punpckhqdq           m8, m3, m5
    punpcklqdq           m3, m5
    punpckhqdq           m5, m0, m6 ; out8
    punpcklqdq           m0, m6
    REPX   {pmulhrsw x, m9}, m0, m1, m2, m3, m4, m5, m7
    cmp                eobd, 106
    jg .full
    mova         [rsp+32*0], m5
    mova         [rsp+32*1], m7
    mova         [rsp+32*2], m4
    pmulhrsw            m11, m9, m8
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7
    call .main_fast
    jmp .pass2
.dconly:
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_8192)]
    mov                [cq], eobd
    or                  r3d, 32
    jmp m(vvc_inv_dct2_dct2_8x8_8).dconly
.full:
    REPX   {pmulhrsw x, m9}, m12, m13, m14, m15
    pmulhrsw             m6, m9, [rsp+32*2]
    mova         [rsp+32*2], m4
    pmulhrsw             m4, m9, [rsp+32*0]
    mova         [rsp+32*0], m5
    pmulhrsw             m5, m9, [rsp+32*1]
    mova         [rsp+32*1], m7
    pmulhrsw             m7, m9, m11
    pmulhrsw            m11, m9, m8
    call .main
.pass2:
    vpbroadcastd        m12, [o(vvc_pw_2048)]
    REPX  {pmulhrsw x, m12}, m0,  m1,  m2,  m3,  m4,  m5,  m6,  m7, \
                             m8,  m9,  m10, m11,      m13, m14, m15
    pmulhrsw            m12, [rsp]
    REPX {vpermq x, x, q3120}, m0, m2, m4, m6, m8, m10, m12, m14
    REPX {vpermq x, x, q2031}, m1, m3, m5, m7, m9, m11, m13, m15
    mova         [rsp+32*0], m4
    mova         [rsp+32*1], m6
    lea                  r3, [strideq*3]
    WRITE_8X4             0,  1,  4,  6
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4             2,  3,  4,  6
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4    [rsp+32*0],  5,  4,  6
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4    [rsp+32*1],  7,  4,  6
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4             8,  9,  4,  6
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4            10, 11,  4,  6
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4            12, 13,  4,  6
    lea                dstq, [dstq+strideq*4]
    WRITE_8X4            14, 15,  4,  6
    RET
ALIGN function_align
cglobal_label .main_fast ; bottom half is zero
    call m(idct2_8x16_internal_8).main
    mova                 m8, [rsp+gprsize+0*32]
    mova [rsp+gprsize+0*32], m0
    mova                 m9, [rsp+gprsize+1*32]
    mova [rsp+gprsize+1*32], m1
    mova                 m0, [rsp+gprsize+2*32]
    mova [rsp+gprsize+2*32], m6
    lea                  r5, [r6-(o_base)+vvc_pw_4_90x8]
    ITX_UNPACK_MULHRSW    1,  8,  6,  4, 90,  n13, 90 ; t16a, t31a, t23a, t24a
    ITX_UNPACK_MULHRSW   15,  9,  6,  22, 88, n31, 85 ; t20a, t27a, t19a, t28a
    ITX_UNPACK_MULHRSW   14,  0,  6, 38, 82, m46, 78 ; t18a, t29a, t21a, t26a
    ITX_UNPACK_MULHRSW   13, 11,  6, 54, 73, m61, 67 ; t22a, t25a, t17a, t30a
    jmp .main2
ALIGN function_align
cglobal_label .main
    call m(idct2_8x16_internal_8).main
    mova                 m8, [rsp+gprsize+0*32]
    mova [rsp+gprsize+0*32], m0
    mova                 m9, [rsp+gprsize+1*32]
    mova [rsp+gprsize+1*32], m1
    mova                 m0, [rsp+gprsize+2*32]
    mova [rsp+gprsize+2*32], m6
    punpcklwd            m1, m15, m8  ; in31 in1
    punpckhwd            m8, m15      ; in3  in29
    punpcklwd           m15, m14, m9  ; in27 in5
    punpckhwd            m9, m14      ; in7  in25
    punpcklwd           m14, m13, m0  ; in23 in9
    punpckhwd            m0, m13      ; in11 in21
    punpcklwd           m13, m12, m11 ; in19 in13
    punpckhwd           m11, m12      ; in15 in17
    ITX_MUL2X_PACK        1,  6, 12, 10,  4, 90, 3 ; t16a, t31a
    ITX_MUL2X_PACK        8,  6, 12, 10, 90,  13, 3 ; t23a, t24a
    ITX_MUL2X_PACK       15,  6, 12, 10,  22, 88, 3 ; t20a, t27a
    ITX_MUL2X_PACK        9,  6, 12, 10, 85, 31, 3 ; t19a, t28a
    ITX_MUL2X_PACK       14,  6, 12, 10, 38, 82, 3 ; t18a, t29a
    ITX_MUL2X_PACK        0,  6, 12, 10, 78, 46, 3 ; t21a, t26a
    ITX_MUL2X_PACK       13,  6, 12, 10, 54, 73, 3 ; t22a, t25a
    ITX_MUL2X_PACK       11,  6, 12, 10, 67, 61, 3 ; t17a, t30a
.main2:
    psubsw               m6, m1, m11  ; t17 t30
    paddsw               m1, m11      ; t16 t31
    psubsw              m11, m9, m14  ; t18 t29
    paddsw               m9, m14      ; t19 t28
    psubsw              m14, m15, m0  ; t21 t26
    paddsw              m15, m0       ; t20 t27
    psubsw               m0, m8, m13  ; t22 t25
    paddsw               m8, m13      ; t23 t24
    ITX_MUL2X_PACK        6, 12, 13, 10,   18, 89, 3 ; t17a t30a
    ITX_MUL2X_PACK       11, 12, 13, 10, m89,  18, 3 ; t18a t29a
    ITX_MUL2X_PACK       14, 12, 13, 10,  75, 50, 3 ; t21a t26a
    ITX_MUL2X_PACK        0, 12, 13, 10, m50, 75, 3 ; t22a t25a
    psubsw              m13, m1, m9   ; t19a t28a
    paddsw               m1, m9       ; t16a t31a
    psubsw               m9, m8, m15  ; t20a t27a
    paddsw               m8, m15      ; t23a t24a
    psubsw              m15, m6, m11  ; t18  t29
    paddsw               m6, m11      ; t17  t30
    psubsw              m11, m0, m14  ; t21  t26
    paddsw               m0, m14      ; t22  t25
    ITX_MUL2X_PACK       15, 12, 14, 10,  36, 83, 3 ; t18a t29a
    ITX_MUL2X_PACK       13, 12, 14, 10,  36, 83, 3 ; t19  t28
    ITX_MUL2X_PACK        9, 12, 14, 10, m83, 36, 3 ; t20  t27
    ITX_MUL2X_PACK       11, 12, 14, 10, m83, 36, 3 ; t21a t26a
    vbroadcasti128      m12, [o(deint_shuf)]
    psubsw              m14, m1, m8   ; t23  t24
    paddsw               m1, m8       ; t16  t31
    psubsw               m8, m6, m0   ; t22a t25a
    paddsw               m6, m0       ; t17a t30a
    psubsw               m0, m15, m11 ; t21  t26
    paddsw              m15, m11      ; t18  t29
    psubsw              m11, m13, m9  ; t20a t27a
    paddsw              m13, m9       ; t19a t28a
    REPX    {pshufb x, m12}, m1, m6, m15, m13
    ITX_MUL2X_PACK       14,  9, 12, 10, 64, 64 ; t24a t23a
    vpbroadcastd         m9, [o(vvc_pw_m64_64)]
    ITX_MUL2X_PACK        8, 12,  _, 10, 12,  9, 4  ; t22  t25
    vpbroadcastd        m12, [o(vvc_pw_64_64)]
    ITX_MUL2X_PACK        0, 12,  _, 10, 12,  9, 4  ; t21a t26a
    vpbroadcastd        m12, [o(vvc_pw_64_64)]
    ITX_MUL2X_PACK       11,  9,  _, 10,  9, 12, 4  ; t27  t20
    shufps               m9, m14, m8, q1032 ; t23a t22
    vpblendd            m14, m8, 0xcc       ; t24a t25
    shufps               m8, m11, m0, q1032 ; t20  t21a
    vpblendd            m11, m0, 0xcc       ; t27  t26a
    punpcklqdq           m0, m1, m6   ; t16  t17a
    punpckhqdq           m1, m6       ; t31  t30a
    psubsw              m10, m5, m8   ; out20 out21
    paddsw               m5, m8       ; out11 out10
    psubsw               m6, m3, m14  ; out24 out25
    paddsw               m3, m14      ; out7  out6
    psubsw               m8, m7, m0   ; out16 out17
    paddsw               m7, m0       ; out15 out14
    mova                 m0, [rsp+gprsize+0*32]
    punpcklqdq          m12, m13, m15 ; t19a t18
    punpckhqdq          m13, m15      ; t28a t29
    psubsw              m15, m0, m1   ; out31 out30
    paddsw               m0, m1       ; out0  out1
    mova                 m1, [rsp+gprsize+1*32]
    mova [rsp+gprsize+0*32], m6
    mova                 m6, [rsp+gprsize+2*32]
    psubsw              m14, m1, m13  ; out28 out29
    paddsw               m1, m13      ; out3  out2
    psubsw              m13, m2, m11  ; out27 out26
    paddsw               m2, m11      ; out4  out5
    psubsw              m11, m4, m9   ; out23 out22
    paddsw               m4, m9       ; out8  out9
    psubsw               m9, m6, m12  ; out19 out18
    paddsw               m6, m12      ; out12 out13
    ret

%macro LOAD_PACKED_16X2 4 ; dst, tmp, row[1-2]
    vbroadcasti128      m%1, [cq+16*%3]
    vbroadcasti128      m%2, [cq+16*%4]
    shufpd              m%1, m%2, 0x0c
%endmacro

cglobal vvc_inv_dct2_dct2_32x8_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_8192)]
    mov                [cq], eobd
    or                  r3d, 8
.dconly:
    pmulhrsw            xm0, xm2
    movd                xm2, [vvc_pw_2048] ; intentionally rip-relative
    pmulhrsw            xm0, xm1
    pmulhrsw            xm0, xm2
    vpbroadcastw         m0, xm0
    pxor                 m3, m3
.dconly_loop:
    mova                 m1, [dstq]
    punpckhbw            m2, m1, m3
    punpcklbw            m1, m3
    paddw                m2, m0
    paddw                m1, m0
    packuswb             m1, m2
    mova             [dstq], m1
    add                dstq, strideq
    dec                 r3d
    jg .dconly_loop
    RET
.normal:
    PROLOGUE              0, 4, 16, 32*3, dst, stride, c, eob
    %undef cmp
    LOAD_PACKED_16X2      0,  7,  0,  2 ; in0  in2
    LOAD_PACKED_16X2      4,  7,  1,  3 ; in1  in3
    LOAD_PACKED_16X2      1,  7,  4,  6 ; in4  in6
    LOAD_PACKED_16X2      5,  7,  5,  7 ; in5  in7
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8},  0,  1,  2,  3
    add                  cq, 16*16
    LOAD_PACKED_16X2      2,  7, -8, -6 ; in8  in10
    LOAD_PACKED_16X2      6,  7, -7, -5 ; in9  in11
    LOAD_PACKED_16X2      3,  7, -4, -2 ; in12 in14
    LOAD_PACKED_16X2     11,  7, -3, -1 ; in13 in15
    REPX {mova [cq+32*x], m8}, -4, -3, -2, -1
    mova         [rsp+32*0], m4
    mova         [rsp+32*1], m5
    mova         [rsp+32*2], m6
    cmp                eobd, 106
    jg .full
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7
    call m(vvc_inv_dct2_dct2_8x32_8).main_fast
    jmp .pass2
.full:
    LOAD_PACKED_16X2      4,  7,  0,  2 ; in16 in18
    LOAD_PACKED_16X2     12,  7,  3,  1 ; in19 in17
    LOAD_PACKED_16X2      5,  7,  4,  6 ; in20 in22
    LOAD_PACKED_16X2     13,  7,  7,  5 ; in23 in21
    REPX {mova [cq+32*x], m8},  0,  1,  2,  3
    add                  cq, 16*8
    LOAD_PACKED_16X2      6,  7,  0,  2 ; in24 in26
    LOAD_PACKED_16X2     14,  7,  3,  1 ; in27 in25
    LOAD_PACKED_16X2      7,  8,  4,  6 ; in28 in30
    LOAD_PACKED_16X2     15,  8,  7,  5 ; in31 in29
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8},  0,  1,  2,  3
    call m(vvc_inv_dct2_dct2_8x32_8).main
.pass2:
    vpbroadcastd        m12, [o(vvc_pw_8192)]
    REPX  {pmulhrsw x, m12}, m8, m9, m10, m11, m13, m14, m15
    mova         [rsp+32*1], m9
    mova         [rsp+32*2], m10
    punpckhwd            m9, m0, m2
    punpcklwd            m0, m2
    punpckhwd            m2, m1, m3
    punpcklwd            m1, m3
    punpcklwd           m10, m4, m6
    punpckhwd            m4, m6
    punpcklwd            m6, m5, m7
    punpckhwd            m5, m7
    punpckhwd            m3, m0, m9
    punpcklwd            m0, m9
    punpckhwd            m9, m2, m1
    punpcklwd            m2, m1
    punpcklwd            m7, m10, m4
    punpckhwd           m10, m4
    punpcklwd            m4, m5, m6
    punpckhwd            m5, m6
    punpckhdq            m1, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m3, m9
    punpckhdq            m3, m9
    punpckldq            m6, m7, m4
    punpckhdq            m7, m4
    punpckldq            m9, m10, m5
    punpckhdq           m10, m5
    REPX  {pmulhrsw x, m12}, m0, m1, m2, m3, m6, m7, m9, m10
    pmulhrsw            m12, [rsp+32*0]
    mova         [rsp+32*0], m8
    vperm2i128           m4, m0, m6, 0x31
    vinserti128          m0, xm6, 1
    vperm2i128           m5, m1, m7, 0x31
    vinserti128          m1, xm7, 1
    vperm2i128           m6, m2, m9, 0x31
    vinserti128          m2, xm9, 1
    vperm2i128           m7, m3, m10, 0x31
    vinserti128          m3, xm10, 1
    call m(idct2_16x8_internal_8).main
    vpbroadcastd         m8, [o(vvc_pw_2048)]
    REPX   {pmulhrsw x, m8}, m0, m1, m2, m3, m4, m5, m6, m7
    lea                  r2, [strideq*3]
    WRITE_16X2            0,  1,  8,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r2
    lea                  r3, [dstq+strideq*4]
    %define dstq r3
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2            6,  7,  0,  1, strideq*2, r2
    mova                 m0, [rsp+32*0]
    mova                 m1, [rsp+32*1]
    mova                 m2, [rsp+32*2]
    punpckhwd            m7, m0, m2
    punpcklwd            m0, m2
    punpckhwd            m2, m1, m11
    punpcklwd            m1, m11
    punpckhwd            m4, m12, m14
    punpcklwd           m12, m14
    punpckhwd            m5, m13, m15
    punpcklwd           m13, m15
    punpckhwd            m3, m0, m7
    punpcklwd            m0, m7
    punpckhwd            m9, m2, m1
    punpcklwd            m2, m1
    punpcklwd            m7, m12, m4
    punpckhwd           m12, m4
    punpcklwd            m4, m5, m13
    punpckhwd            m5, m13
    punpckhdq            m1, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m3, m9
    punpckhdq            m3, m9
    punpckldq            m6, m7, m4
    punpckhdq            m7, m4
    punpckldq            m9, m12, m5
    punpckhdq           m12, m5
    vperm2i128           m4, m0, m6, 0x31
    vinserti128          m0, xm6, 1
    vperm2i128           m5, m1, m7, 0x31
    vinserti128          m1, xm7, 1
    vperm2i128           m6, m2, m9, 0x31
    vinserti128          m2, xm9, 1
    vperm2i128           m7, m3, m12, 0x31
    vinserti128          m3, xm12, 1
    call m(idct2_16x8_internal_8).main2
    vpbroadcastd         m8, [o(vvc_pw_2048)]
    REPX   {pmulhrsw x, m8}, m0, m1, m2, m3, m4, m5, m6, m7
    add                  r0, 16
    add                  r3, 16
    %define dstq r0
    WRITE_16X2            0,  1,  8,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r2
    %define dstq r3
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2            6,  7,  0,  1, strideq*2, r2
    RET

cglobal vvc_inv_identity_identity_8x32_8, 4, 5, 11, dst, stride, c, eob
    vpbroadcastd         m9, [vvc_pw_5]
    lea                  r4, [strideq*3]
    sub                eobd, 107 ; loop_iterations = 1 + (eobd >= 107)
.loop:
    mova                xm0,[cq+16* 0]
    mova                xm1, [cq+16* 4]
    vinserti128          m0, [cq+16* 1], 1
    vinserti128          m1, [cq+16* 5], 1
    pxor                 m8, m8
    mova          [cq+32*0], m8
    mova          [cq+32*2], m8
    add                  cq, 16*16
    mova                xm2, [cq-16* 8]
    mova                xm3, [cq-16* 4]
    vinserti128          m2, [cq-16* 7], 1
    vinserti128          m3, [cq-16* 3], 1
    mova                xm4, [cq+16* 0]
    mova                xm5, [cq+16* 4]
    vinserti128          m4, [cq+16* 1], 1
    vinserti128          m5, [cq+16* 5], 1
    mova                xm6, [cq+16* 8]
    mova                xm7, [cq+16*12]
    vinserti128          m6, [cq+16* 9], 1
    vinserti128          m7, [cq+16*13], 1
    REPX {mova [cq+32*x], m8}, -4, -2,  0,  2,  4,  6
    REPX  {paddsw    x, m9}, m0, m1, m2, m3, m4, m5, m6, m7
    call .transpose8x8
    REPX  {psraw     x, 3 }, m0, m1, m2, m3, m4, m5, m6, m7
    WRITE_8X4             0,  4,  8, 10, strideq*8, strideq*4, r4*4
    add                dstq, strideq
    WRITE_8X4             1,  5,  0,  4, strideq*8, strideq*4, r4*4
    add                dstq, strideq
    WRITE_8X4             2,  6,  0,  4, strideq*8, strideq*4, r4*4
    add                dstq, strideq
    WRITE_8X4             3,  7,  0,  4, strideq*8, strideq*4, r4*4
    add                dstq, strideq
    sub                  cq, 16*16-32
    lea                dstq, [dstq+r4*4]
    add                eobd, 0x80000000
    jnc .loop
    RET
ALIGN function_align
.transpose8x8:
    punpckhwd            m8, m4, m5
    punpcklwd            m4, m5
    punpckhwd            m5, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m6, m7
    punpcklwd            m6, m7
    punpckhwd            m7, m2, m3
    punpcklwd            m2, m3
    punpckhdq            m3, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m4, m6
    punpckhdq            m4, m6
    punpckhdq            m6, m5, m7
    punpckldq            m5, m7
    punpckldq            m7, m8, m1
    punpckhdq            m8, m1
    punpckhqdq           m1, m0, m2
    punpcklqdq           m0, m2
    punpcklqdq           m2, m3, m4
    punpckhqdq           m3, m4
    punpcklqdq           m4, m5, m7
    punpckhqdq           m5, m7
    punpckhqdq           m7, m6, m8
    punpcklqdq           m6, m8
    ret

cglobal vvc_inv_identity_identity_32x8_8, 4, 6, 10, dst, stride, c, eob
    add                  cq, 16*8
    vpbroadcastd         m9, [vvc_pw_64]
    lea                  r4, [strideq*3]
    lea                  r5, [dstq+strideq*4]
    sub                eobd, 107
.loop:
    mova                xm0, [cq-16*8]
    mova                xm1, [cq-16*7]
    vinserti128          m0, [cq+16*0], 1
    vinserti128          m1, [cq+16*1], 1
    mova                xm2, [cq-16*6]
    mova                xm3, [cq-16*5]
    vinserti128          m2, [cq+16*2], 1
    vinserti128          m3, [cq+16*3], 1
    mova                xm4, [cq-16*4]
    mova                xm5, [cq-16*3]
    vinserti128          m4, [cq+16*4], 1
    vinserti128          m5, [cq+16*5], 1
    mova                xm6, [cq-16*2]
    mova                xm7, [cq-16*1]
    vinserti128          m6, [cq+16*6], 1
    vinserti128          m7, [cq+16*7], 1
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8}, -4, -3, -2, -1,  0,  1,  2,  3
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    REPX   {pmulhrsw x, m9}, m0, m1, m2, m3, m4, m5, m6, m7
    WRITE_16X2            0,  1,  8,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r4
    %define dstq r5
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2            6,  7,  0,  1, strideq*2, r4
    add                  cq, 16*16
    add                  r0, 16
    add                  r5, 16
    add                eobd, 0x80000000
    jnc .loop
    RET

%define o_base vvc_pw_5 + 128

%macro LOAD_16ROWS 2-4 0, 1 ; src, stride, is_rect2, zero_coefs
%if %3
    vpbroadcastd        m15, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m15, [%1+%2* 0]
    pmulhrsw             m1, m15, [%1+%2* 1]
    pmulhrsw             m2, m15, [%1+%2* 2]
    pmulhrsw             m3, m15, [%1+%2* 3]
    pmulhrsw             m4, m15, [%1+%2* 4]
    pmulhrsw             m5, m15, [%1+%2* 5]
    pmulhrsw             m6, m15, [%1+%2* 6]
    pmulhrsw             m7, m15, [%1+%2* 7]
    pmulhrsw             m8, m15, [%1+%2* 8]
    pmulhrsw             m9, m15, [%1+%2* 9]
    pmulhrsw            m10, m15, [%1+%2*10]
    pmulhrsw            m11, m15, [%1+%2*11]
    pmulhrsw            m12, m15, [%1+%2*12]
    pmulhrsw            m13, m15, [%1+%2*13]
    pmulhrsw            m14, m15, [%1+%2*14]
    pmulhrsw            m15,      [%1+%2*15]
%else
    mova                 m0, [%1+%2* 0]
    mova                 m1, [%1+%2* 1]
    mova                 m2, [%1+%2* 2]
    mova                 m3, [%1+%2* 3]
    mova                 m4, [%1+%2* 4]
    mova                 m5, [%1+%2* 5]
    mova                 m6, [%1+%2* 6]
    mova                 m7, [%1+%2* 7]
    mova                 m8, [%1+%2* 8]
    mova                 m9, [%1+%2* 9]
    mova                m10, [%1+%2*10]
    mova                m11, [%1+%2*11]
    mova                m12, [%1+%2*12]
    mova                m13, [%1+%2*13]
    mova                m14, [%1+%2*14]
    mova                m15, [%1+%2*15]
%endif
    mova              [rsp], m15
%if %4
    pxor                m15, m15
    REPX {mova [%1+%2*x], m15}, 0,  1,  2,  3,  4,  5,  6,  7, \
                                8,  9, 10, 11, 12, 13, 14, 15
%endif
%endmacro

%macro IDCT2_32_PASS2_END 7 ; coefs[1-2], tmp[1-2], rnd, offset[1-2]
    mova                m%4, [%2]
    paddsw              m%3, m%1, m%4
    psubsw              m%1, m%4
    pmovzxbw            m%4, [dstq+%6]
    pmulhrsw            m%3, m%5
    pmulhrsw            m%1, m%5
    paddw               m%3, m%4
    pmovzxbw            m%4, [r2+%7]
    paddw               m%1, m%4
    packuswb            m%3, m%1
    vpermq              m%3, m%3, q3120
    mova          [dstq+%6], xm%3
    vextracti128    [r2+%7], m%3, 1
%endmacro

cglobal vvc_inv_dct2_dct2_16x32_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jz .dconly
    PROLOGUE              0, 8, 16, 32*35, dst, stride, c, eob, tmp1, tmp2, \
                                           base, tmp3
    %undef cmp
    LOAD_16ROWS          cq, 64, 1
    call m(idct2_16x16_internal_8).main
    lea               tmp1q, [rsp+32*7]
    lea               tmp2q, [tmp1q+32*8]
    lea               tmp3q, [tmp1q+32*16]
    mova                 m1, [rsp+32*1]
    mova         [rsp+32*0], m6
    mova         [rsp+32*1], m7
    vpbroadcastd         m7, [o(vvc_pw_16384)]
    call .transpose_2x8x8_round
    mova                m15, [rsp+32*0]
    mova         [tmp3q-32*4+ 0], xm0
    vextracti128 [tmp3q+32*0+ 0], m0, 1
    mova         [tmp3q-32*3+ 0], xm2
    vextracti128 [tmp3q+32*1+ 0], m2, 1
    mova         [tmp3q-32*2+ 0], xm4
    vextracti128 [tmp3q+32*2+ 0], m4, 1
    mova         [tmp3q-32*1+ 0], xm6
    vextracti128 [tmp3q+32*3+ 0], m6, 1
    mova         [tmp3q-32*4+16], xm8
    vextracti128 [tmp3q+32*0+16], m8, 1
    mova         [tmp3q-32*3+16], xm10
    vextracti128 [tmp3q+32*1+16], m10, 1
    mova         [tmp3q-32*2+16], xm12
    vextracti128 [tmp3q+32*2+16], m12, 1
    mova         [tmp3q-32*1+16], xm14
    vextracti128 [tmp3q+32*3+16], m14, 1
    cmp                eobd, 150
    jg .full
    vinserti128          m0, m1, xm9, 1
    vperm2i128           m4, m1, m9, 0x31
    vinserti128          m2, m5, xm13, 1
    vperm2i128           m6, m5, m13, 0x31
    vinserti128          m1, m3, xm11, 1
    vperm2i128           m5, m3, m11, 0x31
    vinserti128          m3, m7, xm15, 1
    vperm2i128           m7, m7, m15, 0x31
    call .main_oddhalf_fast
    pxor                 m8, m8
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14, m15
    jmp .idct2_16
.dconly:
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    or                  r3d, 32
    jmp m(vvc_inv_dct2_dct2_16x4_8).dconly
.full:
    mova       [tmp1q-32*4], m1
    mova       [tmp1q-32*3], m3
    mova       [tmp1q-32*2], m5
    mova       [tmp1q-32*1], m7
    mova       [tmp1q+32*0], m9
    mova       [tmp1q+32*1], m11
    mova       [tmp1q+32*2], m13
    mova       [tmp1q+32*3], m15
    LOAD_16ROWS       cq+32, 64, 1
    call m(idct2_16x16_internal_8).main
    lea                  r2, [tmp3q+32*8]
    mova                 m1, [rsp+32*1]
    mova         [rsp+32*0], m6
    mova         [rsp+32*1], m7
    vpbroadcastd         m7, [o(vvc_pw_16384)]
    call .transpose_2x8x8_round
    mova                m15, [rsp+32*0]
    mova         [r2-32*4+ 0], xm0
    vextracti128 [r2+32*0+ 0], m0, 1
    mova         [r2-32*3+ 0], xm2
    vextracti128 [r2+32*1+ 0], m2, 1
    mova         [r2-32*2+ 0], xm4
    vextracti128 [r2+32*2+ 0], m4, 1
    mova         [r2-32*1+ 0], xm6
    vextracti128 [r2+32*3+ 0], m6, 1
    mova         [r2-32*4+16], xm8
    vextracti128 [r2+32*0+16], m8, 1
    mova         [r2-32*3+16], xm10
    vextracti128 [r2+32*1+16], m10, 1
    mova         [r2-32*2+16], xm12
    vextracti128 [r2+32*2+16], m12, 1
    mova         [r2-32*1+16], xm14
    vextracti128 [r2+32*3+16], m14, 1
    vinserti128          m8, m1, xm9, 1
    vperm2i128          m12, m1, m9, 0x31
    mova                xm0, [tmp1q-32*4]
    mova                xm1, [tmp1q-32*3]
    vinserti128          m0, [tmp1q+32*0], 1
    vinserti128          m1, [tmp1q+32*1], 1
    vinserti128         m10, m5, xm13, 1
    vperm2i128          m14, m5, m13, 0x31
    mova                xm4, [tmp1q-32*4+16]
    mova                xm5, [tmp1q-32*3+16]
    vinserti128          m4, [tmp1q+32*0+16], 1
    vinserti128          m5, [tmp1q+32*1+16], 1
    vinserti128          m9, m3, xm11, 1
    vperm2i128          m13, m3, m11, 0x31
    mova                xm2, [tmp1q-32*2]
    mova                xm3, [tmp1q-32*1]
    vinserti128          m2, [tmp1q+32*2], 1
    vinserti128          m3, [tmp1q+32*3], 1
    vinserti128         m11, m7, xm15, 1
    vperm2i128          m15, m7, m15, 0x31
    mova                xm6, [tmp1q-32*2+16]
    mova                xm7, [tmp1q-32*1+16]
    vinserti128          m6, [tmp1q+32*2+16], 1
    vinserti128          m7, [tmp1q+32*3+16], 1
    call .main_oddhalf
    LOAD_8ROWS_H    r2-32*4, 32
.idct2_16:
    LOAD_8ROWS   tmp3q-32*4, 32
    mova              [rsp], m15
    call m(idct2_16x16_internal_8).main
    imul                 r2, strideq, 19
    lea                  r3, [strideq*3]
    add                  r2, dstq
    call .pass2_end
    RET
ALIGN function_align
cglobal_label .main_oddhalf_fast ; lower half is zero
    mova [rsp+gprsize+32*1], m7
    pxor                 m7, m7
    mova [rsp+gprsize+32*0], m7
    mova [rsp+gprsize+32*2], m7
    vpbroadcastd        m11, [o(vvc_pw_82x8)]
    vpbroadcastd         m7, [o(vvc_pw_38x8)]
    vpbroadcastd        m12, [o(vvc_pw_n31x8)]
    vpbroadcastd         m8, [o(vvc_pw_85x8)]
    vpbroadcastd        m13, [o(vvc_pw_88x8)]
    vpbroadcastd        m15, [o(vvc_pw_22x8)]
    pmulhrsw            m11, m4  ; t29a
    pmulhrsw             m4, m7  ; t18a
    pmulhrsw            m12, m3  ; t19a
    pmulhrsw             m3, m8  ; t28a
    pmulhrsw            m13, m2  ; t27a
    pmulhrsw             m2, m15 ; t20a
    vpbroadcastd        m10, [o(vvc_pw_m46x8)]
    vpbroadcastd         m7, [o(vvc_pw_78x8)]
    vpbroadcastd         m9, [o(vvc_pw_73x8)]
    vpbroadcastd         m8, [o(vvc_pw_54x8)]
    vpbroadcastd        m14, [o(vvc_pw_n13x8)]
    vpbroadcastd        m15, [o(vvc_pw_90x8)]
    pmulhrsw            m10, m5  ; t21a
    pmulhrsw             m5, m7  ; t26a
    pmulhrsw             m9, m6  ; t25a
    pmulhrsw             m6, m8  ; t22a
    pmulhrsw            m14, m1  ; t23a
    pmulhrsw             m1, m15 ; t24a
    vpbroadcastd        m15, [o(vvc_pd_64)]
    jmp .main2
ALIGN function_align
cglobal_label .main_oddhalf
    mova [rsp+gprsize+32*0], m15
    mova [rsp+gprsize+32*1], m7
    mova [rsp+gprsize+32*2], m8
    vpbroadcastd        m15, [o(vvc_pd_64)]
    ITX_MULSUB_2W         4, 11,  7,  8, 15, 38, 82, 0 ; t18a, t29a
    ITX_MULSUB_2W        12,  3,  7,  8, 15, 85, 31, 0 ; t19a, t28a
    ITX_MULSUB_2W         2, 13,  7,  8, 15,  22, 88, 0 ; t20a, t27a
    ITX_MULSUB_2W        10,  5,  7,  8, 15, 78, 46, 0 ; t21a, t26a
    ITX_MULSUB_2W         6,  9,  7,  8, 15, 54, 73, 0 ; t22a, t25a
    ITX_MULSUB_2W        14,  1,  7,  8, 15, 90,  13, 0 ; t23a, t24a
.main2:
    psubsw               m7, m12, m4  ; t18
    paddsw              m12, m4       ; t19
    psubsw               m4, m2, m10  ; t21
    paddsw               m2, m10      ; t20
    psubsw              m10, m14, m6  ; t22
    paddsw              m14, m6       ; t23
    psubsw               m6, m1, m9   ; t25
    paddsw               m1, m9       ; t24
    psubsw               m9, m13, m5  ; t26
    paddsw              m13, m5       ; t27
    psubsw               m5, m3, m11  ; t29
    paddsw               m3, m11      ; t28
    ITX_MULSUB_2W         5,  7,  8, 11, 15, m89, 18, 0 ; t18a, t29a
    ITX_MULSUB_2W         9,  4,  8, 11, 15,  75, 50, 0 ; t21a, t26a
    ITX_MULSUB_2W         6, 10,  8, 11, 15, m50, 75, 0 ; t22a, t25a
    psubsw               m8, m14, m2  ; t20a
    paddsw              m14, m2       ; t23a
    psubsw               m2, m1, m13  ; t27a
    paddsw               m1, m13      ; t24a
    psubsw              m13, m6, m9   ; t21
    paddsw               m6, m9       ; t22
    psubsw               m9, m10, m4  ; t26
    paddsw              m10, m4       ; t25
    ITX_MULSUB_2W         2,  8,  4, 11, 15, m83, 36, 0 ; t20,  t27
    ITX_MULSUB_2W         9, 13,  4, 11, 15, m83, 36, 0 ; t21a, t26a
    mova                 m4, [rsp+gprsize+32*0] ; in31
    mova [rsp+gprsize+32*0], m6  ; t22
    mova                 m6, [rsp+gprsize+32*1] ; in15
    mova [rsp+gprsize+32*1], m14 ; t23a
    mova                m14, [rsp+gprsize+32*2] ; in17
    mova [rsp+gprsize+32*2], m1  ; t24a
    ITX_MULSUB_2W         0,  4,  1, 11, 15,  4, 90, 0 ; t16a, t31a
    ITX_MULSUB_2W        14,  6,  1, 11, 15, 67, 61, 0 ; t17a, t30a
    psubsw               m1, m0, m14  ; t17
    paddsw               m0, m14      ; t16
    psubsw              m14, m4, m6   ; t30
    paddsw               m4, m6       ; t31
    ITX_MULSUB_2W        14,  1,  6, 11, 15,  18, 89, 0 ; t17a, t30a
    psubsw               m6, m0, m12  ; t19a
    paddsw               m0, m12      ; t16a
    psubsw              m12, m4, m3   ; t28a
    paddsw               m4, m3       ; t31a
    psubsw               m3, m14, m5  ; t18
    paddsw              m14, m5       ; t17
    psubsw               m5, m1, m7   ; t29
    paddsw               m1, m7       ; t30
    ITX_MULSUB_2W         5,  3,  7, 11, 15, 36, 83, 0 ; t18a, t29a
    ITX_MULSUB_2W        12,  6,  7, 11, 15, 36, 83, 0 ; t19,  t28
    psubsw               m7, m1, m10  ; t25a
    paddsw               m1, m10      ; t30a
    psubsw              m10, m5, m9   ; t21
    paddsw               m5, m9       ; t18
    psubsw               m9, m12, m2  ; t20a
    paddsw              m12, m2       ; t19a
    psubsw               m2, m3, m13  ; t26
    paddsw               m3, m13      ; t29
    psubsw              m13, m6, m8   ; t27a
    paddsw               m6, m8       ; t28a
    mova       [tmp1q-32*2], m5
    mova       [tmp1q-32*1], m12
    mova       [tmp2q+32*0], m6
    mova       [tmp2q+32*1], m3
    mova       [tmp2q+32*2], m1
    mova                 m5, [rsp+gprsize+32*0] ; t22
    mova                 m6, [rsp+gprsize+32*1] ; t23
    mova                 m3, [rsp+gprsize+32*2] ; t24a
    psubsw               m1, m14, m5  ; t22a
    paddsw              m14, m5       ; t17a
    psubsw               m5, m0, m6   ; t23
    paddsw               m0, m6       ; t16
    psubsw               m6, m4, m3   ; t24
    paddsw               m4, m3       ; t31
    vpbroadcastd         m8, [o(vvc_pw_m64_64)]
    vpbroadcastd         m3, [o(vvc_pw_64_64)]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m14
    mova       [tmp2q+32*3], m4
    ITX_MULSUB_2W        13,  9,  0,  4, 15,  3,  8, 1 ; t20,  t27
    ITX_MULSUB_2W         2, 10,  0,  4, 15,  3,  8, 1 ; t21a, t26a
    ITX_MULSUB_2W         7,  1,  0,  4, 15,  3,  8, 1 ; t22,  t25
    ITX_MULSUB_2W         6,  5,  0,  4, 15,  3,  8, 1 ; t23a, t24a
    mova       [tmp1q+32*0], m13
    mova       [tmp1q+32*1], m2
    mova       [tmp1q+32*2], m7
    mova       [tmp1q+32*3], m6
    mova       [tmp2q-32*4], m5
    mova       [tmp2q-32*3], m1
    mova       [tmp2q-32*2], m10
    mova       [tmp2q-32*1], m9
    ret
ALIGN function_align
.transpose_2x8x8_round:
    punpckhwd            m6, m12, m13
    punpcklwd           m12, m13
    punpckhwd           m13, m8, m9
    punpcklwd            m8, m9
    punpckhwd            m9, m14, m15
    punpcklwd           m14, m15
    punpckhwd           m15, m10, m11
    punpcklwd           m10, m11
    REPX   {pmulhrsw x, m7}, m0, m1, m2, m3, m4, m5
    punpckhdq           m11, m8, m10
    punpckldq            m8, m10
    punpckldq           m10, m12, m14
    punpckhdq           m12, m14
    punpckhdq           m14, m13, m15
    punpckldq           m13, m15
    punpckldq           m15, m6, m9
    punpckhdq            m6, m9
    punpckhqdq           m9, m8, m10
    punpcklqdq           m8, m10
    punpcklqdq          m10, m11, m12
    punpckhqdq          m11, m12
    punpcklqdq          m12, m13, m15
    punpckhqdq          m13, m15
    punpckhqdq          m15, m14, m6
    punpcklqdq          m14, m6
    pmulhrsw             m6, m7, [rsp+gprsize+32*0]
    REPX   {pmulhrsw x, m7}, m8, m9, m10, m11, m12, m13, m14, m15
    pmulhrsw             m7, [rsp+gprsize+32*1]
    mova [rsp+gprsize+32*0], m15
    punpckhwd           m15, m4, m5
    punpcklwd            m4, m5
    punpckhwd            m5, m0, m1
    punpcklwd            m0, m1
    punpckhwd            m1, m6, m7
    punpcklwd            m6, m7
    punpckhwd            m7, m2, m3
    punpcklwd            m2, m3
    punpckhdq            m3, m0, m2
    punpckldq            m0, m2
    punpckldq            m2, m4, m6
    punpckhdq            m4, m6
    punpckhdq            m6, m5, m7
    punpckldq            m5, m7
    punpckldq            m7, m15, m1
    punpckhdq           m15, m1
    punpckhqdq           m1, m0, m2
    punpcklqdq           m0, m2
    punpcklqdq           m2, m3, m4
    punpckhqdq           m3, m4
    punpcklqdq           m4, m5, m7
    punpckhqdq           m5, m7
    punpckhqdq           m7, m6, m15
    punpcklqdq           m6, m15
    ret
ALIGN function_align
.pass2_end:
    mova [rsp+gprsize+32*0], m7
    mova [rsp+gprsize+32*2], m15
    vpbroadcastd        m15, [o(vvc_pw_2048)]
    IDCT2_32_PASS2_END      0, tmp2q+32*3, 1, 7, 15, strideq*0, r3*4
    IDCT2_32_PASS2_END      4, tmp2q-32*1, 0, 7, 15, strideq*4, strideq*8
    IDCT2_32_PASS2_END      8, tmp1q+32*3, 0, 4, 15, strideq*8, strideq*4
    IDCT2_32_PASS2_END     12, tmp1q-32*1, 0, 4, 15, r3*4,      strideq*0
    add                dstq, strideq
    sub                  r2, strideq
    mova                 m1, [rsp+gprsize+32*1]
    IDCT2_32_PASS2_END      1, tmp2q+32*2, 0, 4, 15, strideq*0, r3*4
    IDCT2_32_PASS2_END      5, tmp2q-32*2, 0, 4, 15, strideq*4, strideq*8
    IDCT2_32_PASS2_END      9, tmp1q+32*2, 0, 4, 15, strideq*8, strideq*4
    IDCT2_32_PASS2_END     13, tmp1q-32*2, 0, 4, 15, r3*4,      strideq*0
    add                dstq, strideq
    sub                  r2, strideq
    IDCT2_32_PASS2_END      2, tmp2q+32*1, 0, 4, 15, strideq*0, r3*4
    IDCT2_32_PASS2_END      6, tmp2q-32*3, 0, 4, 15, strideq*4, strideq*8
    IDCT2_32_PASS2_END     10, tmp1q+32*1, 0, 4, 15, strideq*8, strideq*4
    IDCT2_32_PASS2_END     14, tmp1q-32*3, 0, 4, 15, r3*4,      strideq*0
    add                dstq, strideq
    sub                  r2, strideq
    mova                 m7, [rsp+gprsize+32*0]
    mova                 m1, [rsp+gprsize+32*2]
    IDCT2_32_PASS2_END      3, tmp2q+32*0, 0, 4, 15, strideq*0, r3*4
    IDCT2_32_PASS2_END      7, tmp2q-32*4, 0, 4, 15, strideq*4, strideq*8
    IDCT2_32_PASS2_END     11, tmp1q+32*0, 0, 4, 15, strideq*8, strideq*4
    IDCT2_32_PASS2_END      1, tmp1q-32*4, 0, 4, 15, r3*4,      strideq*0
    ret

; Perform the final sumsub step and YMM lane shuffling
%macro IDCT2_32_PASS1_END 4 ; row[1-2], tmp[1-2]
    mova                m%3, [tmp2q+32*( 3-%1)]
    psubsw              m%4, m%1, m%3
    paddsw              m%1, m%3
    mova                m%3, [tmp1q+32*(11-%2)]
    mova         [tmp1q+32*(11-%2)+16], xm%4
    vextracti128 [tmp2q+32*( 3-%1)+16], m%4, 1
    paddsw              m%4, m%2, m%3
    psubsw              m%2, m%3
    mova         [tmp1q+32*(11-%2)], xm%2
    vextracti128 [tmp2q+32*( 3-%1)], m%2, 1
    vperm2i128          m%2, m%1, m%4, 0x31
    vinserti128         m%1, xm%4, 1
%endmacro

cglobal vvc_inv_dct2_dct2_32x16_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    or                  r3d, 16
    jmp m(vvc_inv_dct2_dct2_32x8_8).dconly
.normal:
    PROLOGUE              0, 6, 16, 32*19, dst, stride, c, eob, tmp1, tmp2
    vpbroadcastd        m15, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m15, [cq+32* 1]
    pmulhrsw             m1, m15, [cq+32* 3]
    pmulhrsw             m2, m15, [cq+32* 5]
    pmulhrsw             m3, m15, [cq+32* 7]
    pmulhrsw             m4, m15, [cq+32* 9]
    pmulhrsw             m5, m15, [cq+32*11]
    pmulhrsw             m6, m15, [cq+32*13]
    pmulhrsw             m7, m15, [cq+32*15]
    pmulhrsw             m8, m15, [cq+32*17]
    pmulhrsw             m9, m15, [cq+32*19]
    pmulhrsw            m10, m15, [cq+32*21]
    pmulhrsw            m11, m15, [cq+32*23]
    pmulhrsw            m12, m15, [cq+32*25]
    pmulhrsw            m13, m15, [cq+32*27]
    pmulhrsw            m14, m15, [cq+32*29]
    pmulhrsw            m15,      [cq+32*31]
    lea               tmp1q, [rsp+32*7]
    lea               tmp2q, [tmp1q+32*8]
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf
    LOAD_16ROWS     cq+32*0, 32*2, 1, 0
    pxor                m15, m15
    mov                 r3d, 8
.zero_loop:
    mova          [cq+32*0], m15
    mova          [cq+32*1], m15
    mova          [cq+32*2], m15
    mova          [cq+32*3], m15
    add                  cq, 32*4
    dec                 r3d
    jg .zero_loop
    call m(idct2_16x16_internal_8).main
    call .pass1_end
    lea                  r2, [strideq*3]
    mov                  r3, dstq
.pass2:
    vpbroadcastd         m7, [o(vvc_pw_16384)]
    call m(vvc_inv_dct2_dct2_16x32_8).transpose_2x8x8_round
    call m(idct2_16x16_internal_8).main
    mova         [rsp+32*2], m15
    vpbroadcastd        m15, [o(vvc_pw_2048)]
    REPX  {pmulhrsw x, m15}, m2, m3, m0
    WRITE_16X2            2,  3,  1,  2, strideq*2, r2
    pmulhrsw             m1, m15, [rsp+32*1]
    WRITE_16X2            0,  1,  2,  3, strideq*0, strideq*1
    lea                dstq, [dstq+strideq*4]
    REPX  {pmulhrsw x, m15}, m4, m5, m6, m7
    WRITE_16X2            4,  5,  2,  3, strideq*0, strideq*1
    WRITE_16X2            6,  7,  2,  3, strideq*2, r2
    lea                dstq, [dstq+strideq*4]
    REPX  {pmulhrsw x, m15}, m8, m9, m10, m11
    WRITE_16X2            8,  9,  2,  3, strideq*0, strideq*1
    WRITE_16X2           10, 11,  2,  3, strideq*2, r2
    lea                dstq, [dstq+strideq*4]
    REPX  {pmulhrsw x, m15}, m11, m12, m13, m14
    pmulhrsw            m15, [rsp+32*2]
    WRITE_16X2           12, 13,  2,  3, strideq*0, strideq*1
    WRITE_16X2           14, 15,  2,  3, strideq*2, r2
    test                 r3, r3
    jnz .right_half
    RET
.right_half:
    LOAD_8ROWS   tmp1q-32*4, 32
    LOAD_8ROWS_H tmp2q-32*4, 32
    lea                dstq, [r3+16]
    xor                 r3d, r3d
    mova         [rsp+32*0], m6
    mova         [rsp+32*1], m7
    jmp .pass2
ALIGN function_align
.pass1_end:
    mova [rsp+gprsize+32*0], m9
    IDCT2_32_PASS1_END      0,  8,  1,  9
    IDCT2_32_PASS1_END      2, 10,  1,  9
    IDCT2_32_PASS1_END      3, 11,  1,  9
    IDCT2_32_PASS1_END      4, 12,  1,  9
    IDCT2_32_PASS1_END      5, 13,  1,  9
    IDCT2_32_PASS1_END      6, 14,  1,  9
    IDCT2_32_PASS1_END      7, 15,  1,  9
    mova                 m1, [rsp+gprsize+32*1]
    mova                 m9, [rsp+gprsize+32*0]
    mova [rsp+gprsize+32*0], m6
    mova [rsp+gprsize+32*1], m7
    IDCT2_32_PASS1_END      1,  9,  6,  7
    ret

cglobal vvc_inv_identity_identity_16x32_8, 4, 5, 13, dst, stride, c, eob
%undef cmp
    lea                  r6, [o_base]
    vpbroadcastd         m9, [o(vvc_pw_64x8)]
    vpbroadcastd        m10, [o(vvc_pw_1697x16)]
    vpbroadcastd        m12, [o(vvc_pw_8192)]
    cmp                eobd, 43   ; if (eob > 43)
    setg                r4b       ;   iteration_count++
    cmp                eobd, 150  ; if (eob > 150)
    setg                 al       ;   iteration_count++
    add                eobd, -279 ; if (eob > 278)
    adc                 r4b, al   ;   iteration_count++
    lea                  r3, [strideq*3]
    mov                  r6, cq
    paddw               m11, m12, m12 ; vvc_pw_16384
.loop:
    mova                xm0, [cq+64* 0]
    mova                xm1, [cq+64* 1]
    vinserti128          m0, [cq+64* 8], 1
    vinserti128          m1, [cq+64* 9], 1
    mova                xm2, [cq+64* 2]
    mova                xm3, [cq+64* 3]
    vinserti128          m2, [cq+64*10], 1
    vinserti128          m3, [cq+64*11], 1
    mova                xm4, [cq+64* 4]
    mova                xm5, [cq+64* 5]
    vinserti128          m4, [cq+64*12], 1
    vinserti128          m5, [cq+64*13], 1
    mova                xm6, [cq+64* 6]
    mova                xm7, [cq+64* 7]
    vinserti128          m6, [cq+64*14], 1
    vinserti128          m7, [cq+64*15], 1
    REPX  {pmulhrsw x, m9 }, m0, m1, m2, m3, m4, m5, m6, m7
    REPX  {IDTX16 x, 8, 10, 11}, 0, 1, 2, 3, 4, 5, 6, 7
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    REPX  {pmulhrsw x, m12}, m0, m1, m2, m3, m4, m5, m6, m7
    WRITE_16X2            0,  1,  8,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2            6,  7,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    add                  cq, 16
    dec                 r4b
    jge .loop
    sub                  cq, 32
    pxor                 m0, m0
    mov                 r0d, 8
    cmp                  cq, r6
    ja .zero_loop
.zero_loop_half:
    mova          [r6+64*0], m0
    mova          [r6+64*1], m0
    add                  r6, 64*4
    mova          [r6-64*2], m0
    mova          [r6-64*1], m0
    sub                 r0d, 2
    jg .zero_loop_half
    RET
.zero_loop:
    mova          [r6+32*0], m0
    mova          [r6+32*1], m0
    mova          [r6+32*2], m0
    mova          [r6+32*3], m0
    add                  r6, 32*4
    dec                 r0d
    jg .zero_loop
    RET

cglobal vvc_inv_identity_identity_32x16_8, 4, 6, 12, dst, stride, c, eob
%undef cmp
    lea                  r6, [o_base]
    vpbroadcastd         m9, [o(vvc_pw_64x8)]
    vpbroadcastd        m10, [o(vvc_pw_1697x16)]
    vpbroadcastd        m11, [o(vvc_pw_2048)]
    cmp                eobd, 35  ; if (eob > 35)
    setg                r4b      ;   iteration_count++
    cmp                eobd, 150 ; if (eob > 150)
    setg                r3b      ;   iteration_count += 2
    lea                 r4d, [r4+r3*2]
    lea                  r3, [strideq*3]
    mov                  r5, dstq
    mov                  r6, cq
.loop:
    mova                xm0, [cq+32* 0]
    mova                xm1, [cq+32* 1]
    vinserti128          m0, [cq+32* 8], 1
    vinserti128          m1, [cq+32* 9], 1
    mova                xm2, [cq+32* 2]
    mova                xm3, [cq+32* 3]
    vinserti128          m2, [cq+32*10], 1
    vinserti128          m3, [cq+32*11], 1
    mova                xm4, [cq+32* 4]
    mova                xm5, [cq+32* 5]
    vinserti128          m4, [cq+32*12], 1
    vinserti128          m5, [cq+32*13], 1
    mova                xm6, [cq+32* 6]
    mova                xm7, [cq+32* 7]
    vinserti128          m6, [cq+32*14], 1
    vinserti128          m7, [cq+32*15], 1
    REPX  {pmulhrsw x, m9 }, m0, m1, m2, m3, m4, m5, m6, m7
    REPX  {paddsw   x, x  }, m0, m1, m2, m3, m4, m5, m6, m7
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    REPX  {IDTX16 x, 8, 10}, 0, 1, 2, 3, 4, 5, 6, 7
    REPX  {pmulhrsw x, m11}, m0, m1, m2, m3, m4, m5, m6, m7
    WRITE_16X2            0,  1,  8,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2            6,  7,  0,  1, strideq*2, r3
    lea                dstq, [dstq+strideq*4]
    add                  cq, 16
    dec                 r4b
    jl .ret
    test                r4b, 1
    jz .loop
    add                  cq, 32*15
    lea                dstq, [r5+16]
    jmp .loop
.ret:
    sub                  cd, eax
    pxor                 m0, m0
    add                  cd, 384
.zero_loop:
    mova          [r6+32*0], m0
    mova          [r6+32*1], m0
    mova          [r6+32*2], m0
    mova          [r6+32*3], m0
    add                  r6, 32*4
    sub                  cd, 128
    jge .zero_loop
    RET

cglobal vvc_inv_dct2_dct2_32x32_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_8192)]
    mov                [cq], eobd
    or                  r3d, 32
    jmp m(vvc_inv_dct2_dct2_32x8_8).dconly
.normal:
    PROLOGUE              0, 9, 16, 32*67, dst, stride, c, eob, tmp1, tmp2, \
                                           base, tmp3, tmp4
    %undef cmp
    lea               tmp1q, [rsp+32*7]
    lea               tmp2q, [tmp1q+32*8]
    sub                eobd, 136
    mov               tmp4d, eobd
.pass1_loop:
    LOAD_8ROWS      cq+64*1, 64*2
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 1, 3, 5, 7, 9, 11, 13, 15
    test              tmp4d, tmp4d
    jl .fast
    LOAD_8ROWS_H   cq+64*17, 64*2
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf
    LOAD_8ROWS_H   cq+64*16, 64*2
    pxor                 m0, m0
    REPX {mova [cq+64*x], m0}, 16, 17, 18, 19, 20, 21, 22, 23, \
                               24, 25, 26, 27, 28, 29, 30, 31
    mova              [rsp], m15
    jmp .idct2_16
.fast:
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    pxor                 m8, m8
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14
    mova              [rsp], m8
.idct2_16:
    LOAD_8ROWS      cq+64*0, 64*2
    pxor                m15, m15
    REPX {mova [cq+64*x], m15}, 0, 2, 4, 6, 8, 10, 12, 14
    call m(idct2_16x16_internal_8).main
    call m(vvc_inv_dct2_dct2_32x16_8).pass1_end
    vpbroadcastd         m7, [o(vvc_pw_8192)]
    call m(vvc_inv_dct2_dct2_16x32_8).transpose_2x8x8_round
    lea               tmp3q, [tmp1q+32*32]
    mova                m15, [rsp]
    mova       [tmp3q-32*4], m0
    mova       [tmp3q-32*3], m2
    mova       [tmp3q-32*2], m4
    mova       [tmp3q-32*1], m6
    mova       [tmp3q+32*0], m8
    mova       [tmp3q+32*1], m10
    mova       [tmp3q+32*2], m12
    mova       [tmp3q+32*3], m14
    add               tmp3q, 32*8
    mova       [tmp3q-32*4], m1
    mova       [tmp3q-32*3], m3
    mova       [tmp3q-32*2], m5
    mova       [tmp3q-32*1], m7
    mova       [tmp3q+32*0], m9
    mova       [tmp3q+32*1], m11
    mova       [tmp3q+32*2], m13
    mova       [tmp3q+32*3], m15
    vpbroadcastd         m9, [o(vvc_pw_8192)]
    pmulhrsw             m0, m9, [tmp1q-32*4]
    pmulhrsw             m1, m9, [tmp1q-32*3]
    pmulhrsw             m2, m9, [tmp1q-32*2]
    pmulhrsw             m3, m9, [tmp1q-32*1]
    pmulhrsw             m4, m9, [tmp1q+32*0]
    pmulhrsw             m5, m9, [tmp1q+32*1]
    pmulhrsw             m6, m9, [tmp1q+32*2]
    pmulhrsw             m7, m9, [tmp1q+32*3]
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    mova       [tmp1q-32*4], m0
    pmulhrsw             m0, m9, [tmp2q-32*4]
    mova       [tmp2q-32*4], m1
    pmulhrsw             m1, m9, [tmp2q-32*3]
    mova       [tmp1q-32*3], m2
    pmulhrsw             m2, m9, [tmp2q-32*2]
    mova       [tmp2q-32*3], m3
    pmulhrsw             m3, m9, [tmp2q-32*1]
    mova       [tmp1q-32*2], m4
    pmulhrsw             m4, m9, [tmp2q+32*0]
    mova       [tmp2q-32*2], m5
    pmulhrsw             m5, m9, [tmp2q+32*1]
    mova       [tmp1q-32*1], m6
    pmulhrsw             m6, m9, [tmp2q+32*2]
    mova       [tmp2q-32*1], m7
    pmulhrsw             m7, m9, [tmp2q+32*3]
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    mova       [tmp1q+32*0], m0
    mova       [tmp2q+32*0], m1
    mova       [tmp1q+32*1], m2
    mova       [tmp2q+32*1], m3
    mova       [tmp1q+32*2], m4
    mova       [tmp2q+32*2], m5
    mova       [tmp1q+32*3], m6
    mova       [tmp2q+32*3], m7
    add                  cq, 32
    add               tmp1q, 32*16
    add               tmp2q, 32*16
    add                eobd, 0x80000000
    jnc .pass1_loop
    add               tmp1q, 32*24
    imul                 r2, strideq, 19
    lea                  r3, [strideq*3]
    add                  r2, dstq
    test              tmp4d, tmp4d
    jge .pass2_loop
    add               tmp1q, 32*16
    add               tmp2q, 32*16
    add               tmp3q, 32*16
.pass2_loop:
    LOAD_8ROWS   tmp2q-32*4, 32
    test              tmp4d, tmp4d
    jl .fast2
    LOAD_8ROWS_H tmp3q-32*4, 32
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf
    sub               tmp3q, 32*8
    LOAD_8ROWS_H tmp3q-32*4, 32
    sub               tmp3q, 32*16
    jmp .pass2_loop_end
.fast2:
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    sub               tmp3q, 32*24
    pxor                 m8, m8
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14, m15
.pass2_loop_end:
    LOAD_8ROWS   tmp3q-32*4, 32
    mova              [rsp], m15
    call m(idct2_16x16_internal_8).main
    call m(vvc_inv_dct2_dct2_16x32_8).pass2_end
    lea               tmp3q, [tmp1q-32*32]
    cmp               tmp2q, tmp3q
    jb .ret
    sub               tmp2q, 32*32
    sub                dstq, r3
    lea                  r2, [r2+r3+16]
    add                dstq, 16
    jmp .pass2_loop
.ret:
    RET

cglobal vvc_inv_identity_identity_32x32_8, 4, 6, 10, dst, stride, c, eob
    %undef cmp
    vpbroadcastd         m9, [vvc_pw_8192]
    sub                eobd, 136 ; if (eob < 136)
    shr                eobd, 30  ;     topleft 16x16 only
    lea                eobd, [eobq*2-8]
    lea                  r4, [strideq*3]
    mov                  r5, dstq
    lea                  r6, [cq+32]
.loop:
    mova                xm0, [cq+64* 0]
    mova                xm1, [cq+64* 1]
    vinserti128          m0, [cq+64* 8], 1
    vinserti128          m1, [cq+64* 9], 1
    mova                xm2, [cq+64* 2]
    mova                xm3, [cq+64* 3]
    vinserti128          m2, [cq+64*10], 1
    vinserti128          m3, [cq+64*11], 1
    mova                xm4, [cq+64* 4]
    mova                xm5, [cq+64* 5]
    vinserti128          m4, [cq+64*12], 1
    vinserti128          m5, [cq+64*13], 1
    mova                xm6, [cq+64* 6]
    mova                xm7, [cq+64* 7]
    vinserti128          m6, [cq+64*14], 1
    vinserti128          m7, [cq+64*15], 1
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    REPX   {pmulhrsw x, m9}, m0, m1, m2, m3, m4, m5, m6, m7
    WRITE_16X2            0,  1,  8,  0, strideq*0, strideq*1
    WRITE_16X2            2,  3,  0,  1, strideq*2, r4
    lea                dstq, [dstq+strideq*4]
    WRITE_16X2            4,  5,  0,  1, strideq*0, strideq*1
    WRITE_16X2            6,  7,  0,  1, strideq*2, r4
    lea                dstq, [dstq+strideq*4]
    add                  cq, 16
    inc                eobd
    jz .ret
    test               eobd, 3
    jnz .loop
    add                  cq, 64*15
    lea                dstq, [r5+16]
    jmp .loop
.ret:
    pxor                 m0, m0
    mov                 r0d, 16
    cmp                  cq, r6
    jne .zero_loop
.zero_loop_topleft:
    mova          [r6-32*1], m0
    mova          [r6+32*1], m0
    mova          [r6+32*3], m0
    mova          [r6+32*5], m0
    add                  r6, 64*4
    sub                 r0d, 4
    jg .zero_loop_topleft
    RET
.zero_loop:
    mova          [r6-32*1], m0
    mova          [r6+32*0], m0
    mova          [r6+32*1], m0
    mova          [r6+32*2], m0
    add                  r6, 32*4
    dec                 r0d
    jg .zero_loop
    RET

%macro IDCT2_64_PART2_END 6-10 ; out, src[1-2], tmp[1-3], (offset[1-4])
%if %1 & 1
    mova                m%5, [tmp2q-32*(51-%1)] ; idct2_16 out 0+n
    mova                m%4, [tmp1q-32*(14+%1)] ; idct2_32 out31-n
%else
    mova                m%5, [tmp1q-32*(45-%1)]
    mova                m%4, [tmp2q-32*(20+%1)]
%endif
    psubsw              m%6, m%5, m%4 ; idct2_32 out31-n
    paddsw              m%5, m%4      ; idct2_32 out 0+n
    psubsw              m%4, m%6, m%3 ; out32+n
    paddsw              m%6, m%3      ; out31-n
    psubsw              m%3, m%5, m%2 ; out63-n
    paddsw              m%5, m%2      ; out 0+n
%if %0 == 6 ; pass 1
%if %1 & 1
    mova [tmp2q-32*(19-%1)], m%4
    mova [tmp1q-32*(14+%1)], m%6
    mova [tmp1q+32*(18-%1)], m%3
    mova [tmp2q-32*(51-%1)], m%5
%else
    mova [tmp1q-32*(13-%1)], m%4
    mova [tmp2q-32*(20+%1)], m%6
    mova [tmp2q+32*(12-%1)], m%3
    mova [tmp1q-32*(45-%1)], m%5
%endif
%else ; pass 2
    REPX  {pmulhrsw x, m14}, m%4, m%6, m%3, m%5
%if %1 & 1
    %define %%d0 r2
    %define %%d1 dstq
%else
    %define %%d0 dstq
    %define %%d1 r2
%endif
    pmovzxbw            m%2, [%%d0+%9 ]
    paddw               m%2, m%4
    pmovzxbw            m%4, [%%d1+%8 ]
    paddw               m%4, m%6
    pmovzxbw            m%6, [%%d1+%10]
    paddw               m%3, m%6
    pmovzxbw            m%6, [%%d0+%7 ]
    paddw               m%5, m%6
    packuswb            m%2, m%4
    packuswb            m%3, m%5
    vpermq              m%2, m%2, q3120
    vpermq              m%3, m%3, q3120
    mova         [%%d0+%9 ], xm%2
    vextracti128 [%%d1+%8 ], m%2, 1
    mova         [%%d1+%10], xm%3
    vextracti128 [%%d0+%7 ], m%3, 1
%endif
%endmacro

cglobal vvc_inv_dct2_dct2_16x64_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_8192)]
    mov                [cq], eobd
    or                  r3d, 64
    jmp m(vvc_inv_dct2_dct2_16x4_8).dconly
.normal:
    PROLOGUE              0, 10, 16, 32*67, dst, stride, c, eob, tmp1, tmp2
    %undef cmp
    lea               tmp1q, [rsp+32*23]
    lea               tmp2q, [tmp1q+32*24]
    sub                eobd, 151
    mov                 r7d, eobd
.pass1_loop:
    LOAD_16ROWS          cq, 64
    call m(idct2_16x16_internal_8).main
    mova                 m1, [rsp+32*1]
    mova         [rsp+32*0], m6
    mova         [rsp+32*1], m7
    vpbroadcastd         m7, [o(vvc_pw_8192)]
    call m(vvc_inv_dct2_dct2_16x32_8).transpose_2x8x8_round
    mova                m15, [rsp+32*0]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m2
    mova       [tmp1q-32*2], m4
    mova       [tmp1q-32*1], m6
    mova       [tmp1q+32*0], m8
    mova       [tmp1q+32*1], m10
    mova       [tmp1q+32*2], m12
    mova       [tmp1q+32*3], m14
    mova       [tmp2q-32*4], m1
    mova       [tmp2q-32*3], m3
    mova       [tmp2q-32*2], m5
    mova       [tmp2q-32*1], m7
    mova       [tmp2q+32*0], m9
    mova       [tmp2q+32*1], m11
    mova       [tmp2q+32*2], m13
    mova       [tmp2q+32*3], m15
    add                  cq, 32
    add               tmp1q, 32*8
    add               tmp2q, 32*8
    add                eobd, 0x80000000
    jnc .pass1_loop
    lea                  r2, [rsp+32*23]
    mova                xm0, [r2-32*4+ 0]
    mova                xm1, [r2-32*2+ 0]
    vinserti128          m0, [r2+32*0+ 0], 1
    vinserti128          m1, [r2+32*2+ 0], 1
    mova                xm2, [r2-32*4+16]
    mova                xm3, [r2-32*2+16]
    vinserti128          m2, [r2+32*0+16], 1
    vinserti128          m3, [r2+32*2+16], 1
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14
    test                r7d, r7d
    jl .fast
    lea                  r3, [r2+32*8]
    mova                xm4, [r3-32*4+ 0]
    mova                xm5, [r3-32*2+ 0]
    vinserti128          m4, [r3+32*0+ 0], 1
    vinserti128          m5, [r3+32*2+ 0], 1
    mova                xm6, [r3-32*4+16]
    mova                xm7, [r3-32*2+16]
    vinserti128          m6, [r3+32*0+16], 1
    vinserti128          m7, [r3+32*2+16], 1
.fast:
    mova              [rsp], m8
    lea               tmp1q, [rsp+32*7]
    call m(idct2_16x16_internal_8).main
    mova                 m1, [rsp+32*1]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m1
    mova       [tmp1q-32*2], m2
    mova       [tmp1q-32*1], m3
    mova       [tmp1q+32*0], m4
    mova       [tmp1q+32*1], m5
    mova       [tmp1q+32*2], m6
    mova       [tmp1q+32*3], m7
    add               tmp1q, 32*8
    mova       [tmp1q-32*4], m8
    mova       [tmp1q-32*3], m9
    mova       [tmp1q-32*2], m10
    mova       [tmp1q-32*1], m11
    mova       [tmp1q+32*0], m12
    mova       [tmp1q+32*1], m13
    mova       [tmp1q+32*2], m14
    mova       [tmp1q+32*3], m15
    mova                xm0, [r2-32*3+ 0]
    mova                xm1, [r2-32*1+ 0]
    vinserti128          m0, [r2+32*1+ 0], 1
    vinserti128          m1, [r2+32*3+ 0], 1
    mova                xm2, [r2-32*3+16]
    mova                xm3, [r2-32*1+16]
    vinserti128          m2, [r2+32*1+16], 1
    vinserti128          m3, [r2+32*3+16], 1
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7
    test                r7d, r7d
    jl .fast2
    mova                xm4, [r3-32*3+ 0]
    mova                xm5, [r3-32*1+ 0]
    vinserti128          m4, [r3+32*1+ 0], 1
    vinserti128          m5, [r3+32*3+ 0], 1
    mova                xm6, [r3-32*3+16]
    mova                xm7, [r3-32*1+16]
    vinserti128          m6, [r3+32*1+16], 1
    vinserti128          m7, [r3+32*3+16], 1
.fast2:
    add               tmp1q, 32*8
    lea               tmp2q, [tmp1q+32*8]
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    add                  r2, 32*24
    vpbroadcastd        m15, [o(vvc_pd_64)]
    add               tmp1q, 32*16
    add               tmp2q, 32*32
    mova                xm0, [r2-32*4+ 0]
    mova                xm3, [r2-32*1+16]
    vinserti128          m0, [r2+32*0+ 0], 1
    vinserti128          m3, [r2+32*3+16], 1
    mova                xm4, [r2-32*4+16]
    mova                xm7, [r2-32*1+ 0]
    vinserti128          m4, [r2+32*0+16], 1
    vinserti128          m7, [r2+32*3+ 0], 1
    pxor                 m1, m1
    REPX       {mova x, m1}, m2, m5, m6
    test                r7d, r7d
    jl .fast3
    add                  r3, 32*24
    mova                xm1, [r3-32*1+16]
    mova                xm2, [r3-32*4+ 0]
    vinserti128          m1, [r3+32*3+16], 1
    vinserti128          m2, [r3+32*0+ 0], 1
    mova                xm5, [r3-32*1+ 0]
    mova                xm6, [r3-32*4+16]
    vinserti128          m5, [r3+32*3+ 0], 1
    vinserti128          m6, [r3+32*0+16], 1
.fast3:
    add                  r6, o_idct2_64_offset
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    add                  r6, 8
    add               tmp1q, 32*8
    sub               tmp2q, 32*8
    mova                xm0, [r2-32*2+ 0]
    mova                xm3, [r2-32*3+16]
    vinserti128          m0, [r2+32*2+ 0], 1
    vinserti128          m3, [r2+32*1+16], 1
    mova                xm4, [r2-32*2+16]
    mova                xm7, [r2-32*3+ 0]
    vinserti128          m4, [r2+32*2+16], 1
    vinserti128          m7, [r2+32*1+ 0], 1
    pxor                 m1, m1
    REPX       {mova x, m1}, m2, m5, m6
    test                r7d, r7d
    jl .fast4
    mova                xm1, [r3-32*3+16]
    mova                xm2, [r3-32*2+ 0]
    vinserti128          m1, [r3+32*1+16], 1
    vinserti128          m2, [r3+32*2+ 0], 1
    mova                xm5, [r3-32*3+ 0]
    mova                xm6, [r3-32*2+16]
    vinserti128          m5, [r3+32*1+ 0], 1
    vinserti128          m6, [r3+32*2+16], 1
.fast4:
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    call m(vvc_inv_dct2_dct2_16x64_8).main_part2_pass2
    RET
ALIGN function_align
%define o_base idct2_64_mul - 8
cglobal_label .main_part1
    ; idct2_64 steps 1-5:
    ; in1/31/17/15/ 9/23/25/ 7 ->
    ;     t32a/33/34a/35/36/37a/38/39a/56a/57/58a/59/60/61a/62/63a
    ; in5/27/21/11/13/19/29/ 3 ->
    ;     t40a/41/42a/43/44/45a/46/47a/48a/49/50a/51/52/53a/54/55a
    vpbroadcastd        m11, [o(idct2_64_mul+4* 0)]
    vpbroadcastd        m13, [o(idct2_64_mul+4* 1)]
    vpbroadcastd        m10, [o(idct2_64_mul+4* 4)]
    vpbroadcastd        m12, [o(idct2_64_mul+4* 5)]
    pmulhrsw            m11, m0  ; t63a
    pmulhrsw             m0, m13 ; t32a
    pmulhrsw            m10, m1  ; t62a
    pmulhrsw             m1, m12 ; t33a
    vpbroadcastd         m9, [o(idct2_64_mul+4* 8)]
    vpbroadcastd        m13, [o(idct2_64_mul+4* 9)]
    vpbroadcastd         m8, [o(idct2_64_mul+4*12)]
    vpbroadcastd        m12, [o(idct2_64_mul+4*13)]
    pmulhrsw             m9, m2  ; t61a
    pmulhrsw             m2, m13 ; t34a
    pmulhrsw             m8, m3  ; t60a
    pmulhrsw             m3, m12 ; t35a
    psubsw              m12, m0, m1   ; t33
    paddsw               m0, m1       ; t32
    psubsw               m1, m3, m2   ; t34
    paddsw               m3, m2       ; t35
    psubsw               m2, m8, m9   ; t61
    paddsw               m8, m9       ; t60
    psubsw               m9, m11, m10 ; t62
    paddsw              m11, m10      ; t63
    ITX_MULSUB_2W         2,  1, 10, 13, 15, m90, 9, 0 ; t34a, t61a
    vpbroadcastd        m14, [o(vvc_pw_9_90)]
    ITX_MULSUB_2W         9, 12, 10, 13, 15, 14, 13, 1 ; t33a, t62a
    psubsw              m10, m0, m3  ; t35a
    paddsw               m0, m3      ; t32a
    psubsw               m3, m11, m8 ; t60a
    paddsw              m11, m8      ; t63a
    psubsw               m8, m9, m2  ; t34
    paddsw               m9, m2      ; t33
    psubsw               m2, m12, m1 ; t61
    paddsw              m12, m1      ; t62
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m9
    mova       [tmp2q+32*2], m12
    mova       [tmp2q+32*3], m11
    vpbroadcastd        m13, [o(vvc_pw_m89_18)]
    vpbroadcastd        m14, [o(vvc_pw_18_89)]
    ITX_MULSUB_2W         2,  8,  0,  1, 15, 14, 13, 1 ; t34a, t61a
    ITX_MULSUB_2W         3, 10,  0,  1, 15, 14, 13, 1 ; t35,  t60
    mova       [tmp1q-32*2], m2
    mova       [tmp1q-32*1], m3
    mova       [tmp2q+32*0], m10
    mova       [tmp2q+32*1], m8
    vpbroadcastd         m3, [o(idct2_64_mul+4*16)]
    vpbroadcastd        m11, [o(idct2_64_mul+4*17)]
    vpbroadcastd         m2, [o(idct2_64_mul+4*20)]
    vpbroadcastd        m10, [o(idct2_64_mul+4*21)]
    vpbroadcastd         m1, [o(idct2_64_mul+4*24)]
    vpbroadcastd         m9, [o(idct2_64_mul+4*25)]
    vpbroadcastd         m0, [o(idct2_64_mul+4*28)]
    vpbroadcastd         m8, [o(idct2_64_mul+4*29)]
    pmulhrsw             m3, m4  ; t59a
    pmulhrsw             m4, m11 ; t36a
    pmulhrsw             m2, m5  ; t58a
    pmulhrsw             m5, m10 ; t37a
    pmulhrsw             m1, m6  ; t57a
    pmulhrsw             m6, m9  ; t38a
    pmulhrsw             m0, m7  ; t56a
    pmulhrsw             m7, m8  ; t39a
    psubsw               m8, m4, m5 ; t37
    paddsw               m4, m5     ; t36
    psubsw               m5, m7, m6 ; t38
    paddsw               m7, m6     ; t39
    psubsw               m6, m0, m1 ; t57
    paddsw               m0, m1     ; t56
    psubsw               m1, m3, m2 ; t58
    paddsw               m3, m2     ; t59
    ITX_MULSUB_2W         6,  5,  2,  9, 15, m57, 70, 0 ; t38a, t57a
    vpbroadcastd        m10, [o(vvc_pw_70_57)]
    ITX_MULSUB_2W         1,  8,  2,  9, 15, 10,  9, 1 ; t37a, t58a
    psubsw               m2, m7, m4 ; t36a
    paddsw               m7, m4     ; t39a
    psubsw               m4, m0, m3 ; t59a
    paddsw               m0, m3     ; t56a
    psubsw               m3, m6, m1 ; t37
    paddsw               m6, m1     ; t38
    psubsw               m1, m5, m8 ; t58
    paddsw               m5, m8     ; t57
    mova       [tmp1q+32*2], m6
    mova       [tmp1q+32*3], m7
    mova       [tmp2q-32*4], m0
    mova       [tmp2q-32*3], m5
    vpbroadcastd         m6, [o(vvc_pw_m18_m89)]
    vpbroadcastd         m7, [o(vvc_pw_m89_18)]
    ITX_MULSUB_2W         4,  2,  0,  5, 15,  7,  6, 1 ; t36,  t59
    ITX_MULSUB_2W         1,  3,  0,  5, 15,  7,  6, 1 ; t37a, t58a
    mova       [tmp1q+32*0], m4
    mova       [tmp1q+32*1], m1
    mova       [tmp2q-32*2], m3
    mova       [tmp2q-32*1], m2
    ret
%define o_base vvc_pw_5 + 128
.main_part2_pass1: ; idct2_64 steps 6-9 + idct2_16/32/64 sumsub
    sub                  r6, o_idct2_64_offset + 8
    vpbroadcastd        m11, [o(vvc_pw_36_83)]
    vpbroadcastd        m12, [o(vvc_pw_m83_36)]
    vpbroadcastd        m13, [o(vvc_pw_64_64)]
    vpbroadcastd        m14, [o(vvc_pw_m64_64)]
.main_part2_pass1_loop:
    call .main_part2_internal
    IDCT2_64_PART2_END      0,  7,  0,  6,  9, 10
    IDCT2_64_PART2_END      7,  8,  5,  0,  6,  7
    IDCT2_64_PART2_END      8,  2,  1,  0,  6,  7
    IDCT2_64_PART2_END     15,  3,  4,  0,  6,  7
    cmp               tmp1q, tmp2q
    jne .main_part2_pass1_loop
    ret
cglobal_label .main_part2_internal
    mova                 m0, [tmp1q-32*12] ; t32a
    mova                 m6, [tmp2q-32*13] ; t39a
    mova                 m1, [tmp1q-32* 4] ; t40a
    mova                 m5, [tmp2q+32* 3] ; t55a
    add               tmp1q, 32
    sub               tmp2q, 32
    mova                 m2, [tmp1q+32* 3] ; t48a
    mova                 m4, [tmp2q-32* 4] ; t47a
    mova                 m3, [tmp1q+32*11] ; t56a
    mova                 m7, [tmp2q+32*12] ; t63a
    psubsw               m8, m0, m6 ; t39
    paddsw               m0, m6     ; t32
    psubsw               m6, m4, m1 ; t40
    paddsw               m4, m1     ; t47
    psubsw               m1, m2, m5 ; t55
    paddsw               m2, m5     ; t48
    psubsw               m5, m7, m3 ; t56
    paddsw               m7, m3     ; t63
    ITX_MULSUB_2W         5,  8,  3,  9, 15, 11, 12, 1 ; t39a, t56a
    vpbroadcastd         m9, [o(vvc_pw_m36_m83)]
    ITX_MULSUB_2W         1,  6,  3,  9, 15, 12,  9, 1 ; t40a, t55a
    psubsw               m3, m0, m4 ; t47a
    paddsw               m0, m4     ; t32a
    psubsw               m4, m7, m2 ; t48a
    paddsw               m7, m2     ; t63a
    psubsw               m2, m5, m1 ; t40
    paddsw               m5, m1     ; t39
    psubsw               m1, m8, m6 ; t55
    paddsw               m8, m6     ; t56
    ITX_MULSUB_2W         4,  3,  6,  9, 15, 13, 14, 1 ; t47,  t48
    ITX_MULSUB_2W         1,  2,  6,  9, 15, 13, 14, 1 ; t40a, t55a
    ret
.main_part2_pass2:
    sub                  r6, o_idct2_64_offset + 8
    vpbroadcastd        m11, [o(vvc_pw_36_83)]
    vpbroadcastd        m12, [o(vvc_pw_m83_36)]
    vpbroadcastd        m13, [o(vvc_pw_64_64)]
    lea                  r9, [strideq*5]    ; stride*5
    lea                  r3, [r9+strideq*1] ; stride*6
    lea                  r7, [r9+strideq*2] ; stride*7
    lea                  r8, [r3+strideq*2] ; stride*8
    lea                  r2, [dstq+r7]
.main_part2_pass2_loop:
    vpbroadcastd        m14, [o(vvc_pw_m64_64)]
    call .main_part2_internal
    vpbroadcastd        m14, [o(vvc_pw_2048)]
    IDCT2_64_PART2_END      0,  7,  0,  6,  9, 10, strideq*0, r3*4, r8*4, r7*8
    IDCT2_64_PART2_END      7,  8,  5,  0,  6,  7, strideq*0, r3*4, r8*4, r7*8
    IDCT2_64_PART2_END      8,  2,  1,  0,  6,  7, strideq*8, r8*2, r9*8, r3*8
    IDCT2_64_PART2_END     15,  3,  4,  0,  6,  7, strideq*8, r8*2, r9*8, r3*8
    add                dstq, strideq
    sub                  r2, strideq
    cmp               tmp1q, tmp2q
    jne .main_part2_pass2_loop
    ret

cglobal vvc_inv_dct2_dct2_64x16_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_8192)]
    mov                [cq], eobd
    or                  r3d, 16
.dconly:
    pmulhrsw            xm0, xm2
    movd                xm2, [o(vvc_pw_2048)]
    pmulhrsw            xm0, xm1
    pmulhrsw            xm0, xm2
    vpbroadcastw         m0, xm0
    pxor                 m1, m1
.dconly_loop:
    mova                 m2, [dstq+32*0]
    mova                 m3, [dstq+32*1]
    punpckhbw            m4, m2, m1
    punpcklbw            m2, m1
    punpckhbw            m5, m3, m1
    punpcklbw            m3, m1
    paddw                m4, m0
    paddw                m2, m0
    paddw                m5, m0
    paddw                m3, m0
    packuswb             m2, m4
    packuswb             m3, m5
    mova        [dstq+32*0], m2
    mova        [dstq+32*1], m3
    add                dstq, strideq
    dec                 r3d
    jg .dconly_loop
    RET
.normal:
    PROLOGUE              0, 7, 16, 32*67, dst, stride, c, eob, tmp1, tmp2
    LOAD_8ROWS      cq+32*0, 32*4
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8}, 0, 4, 8, 12, 16, 20, 24, 28
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14
    mova              [rsp], m8
    lea               tmp1q, [rsp+32*7]
    call m(idct2_16x16_internal_8).main
    mova                 m1, [rsp+32*1]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m1
    mova       [tmp1q-32*2], m2
    mova       [tmp1q-32*1], m3
    mova       [tmp1q+32*0], m4
    mova       [tmp1q+32*1], m5
    mova       [tmp1q+32*2], m6
    mova       [tmp1q+32*3], m7
    add               tmp1q, 32*8
    mova       [tmp1q-32*4], m8
    mova       [tmp1q-32*3], m9
    mova       [tmp1q-32*2], m10
    mova       [tmp1q-32*1], m11
    mova       [tmp1q+32*0], m12
    mova       [tmp1q+32*1], m13
    mova       [tmp1q+32*2], m14
    mova       [tmp1q+32*3], m15
    LOAD_8ROWS      cq+32*2, 32*4
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8}, 2, 6, 10, 14, 18, 22, 26, 30
    add               tmp1q, 32*8
    lea               tmp2q, [tmp1q+32*8]
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    vpbroadcastd        m15, [o(vvc_pd_64)]
    add               tmp1q, 32*16
    add               tmp2q, 32*32
    mova                 m0, [cq+32* 1]
    mova                 m1, [cq+32*31]
    mova                 m2, [cq+32*17]
    mova                 m3, [cq+32*15]
    mova                 m4, [cq+32* 9]
    mova                 m5, [cq+32*23]
    mova                 m6, [cq+32*25]
    mova                 m7, [cq+32* 7]
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8}, 1, 31, 17, 15, 9, 23, 25, 7
    add                  r6, o_idct2_64_offset
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    add                  r6, 8
    add               tmp1q, 32*8
    sub               tmp2q, 32*8
    mova                 m0, [cq+32* 5]
    mova                 m1, [cq+32*27]
    mova                 m2, [cq+32*21]
    mova                 m3, [cq+32*11]
    mova                 m4, [cq+32*13]
    mova                 m5, [cq+32*19]
    mova                 m6, [cq+32*29]
    mova                 m7, [cq+32* 3]
    pxor                 m8, m8
    REPX {mova [cq+32*x], m8}, 5, 27, 21, 11, 13, 19, 29, 3
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    call m(vvc_inv_dct2_dct2_16x64_8).main_part2_pass1
    sub               tmp1q, 32*36
    lea                  r2, [strideq*3]
    mov               tmp2d, 4
.pass2_loop:
    lea                  r3, [tmp1q-32*8]
    mova                xm0, [r3   -32*4]
    mova                xm1, [r3   -32*3]
    vinserti128          m0, [tmp1q-32*4], 1
    vinserti128          m1, [tmp1q-32*3], 1
    mova                xm2, [r3   -32*2]
    mova                xm3, [r3   -32*1]
    vinserti128          m2, [tmp1q-32*2], 1
    vinserti128          m3, [tmp1q-32*1], 1
    mova                xm4, [r3   +32*0]
    mova                xm5, [r3   +32*1]
    vinserti128          m4, [tmp1q+32*0], 1
    vinserti128          m5, [tmp1q+32*1], 1
    mova                xm6, [r3   +32*2]
    mova                xm7, [r3   +32*3]
    vinserti128          m6, [tmp1q+32*2], 1
    vinserti128          m7, [tmp1q+32*3], 1
    mova                xm8, [r3   -32*4+16]
    mova                xm9, [r3   -32*3+16]
    vinserti128          m8, [tmp1q-32*4+16], 1
    vinserti128          m9, [tmp1q-32*3+16], 1
    mova               xm10, [r3   -32*2+16]
    mova               xm11, [r3   -32*1+16]
    vinserti128         m10, [tmp1q-32*2+16], 1
    vinserti128         m11, [tmp1q-32*1+16], 1
    mova               xm12, [r3   +32*0+16]
    mova               xm13, [r3   +32*1+16]
    vinserti128         m12, [tmp1q+32*0+16], 1
    vinserti128         m13, [tmp1q+32*1+16], 1
    mova               xm14, [r3   +32*2+16]
    mova               xm15, [r3   +32*3+16]
    vinserti128         m14, [tmp1q+32*2+16], 1
    vinserti128         m15, [tmp1q+32*3+16], 1
    mova         [rsp+32*0], m6
    mova         [rsp+32*1], m7
    vpbroadcastd         m7, [o(vvc_pw_8192)]
    call m(vvc_inv_dct2_dct2_16x32_8).transpose_2x8x8_round
    call m(idct2_16x16_internal_8).main
    mova         [rsp+32*0], m15
    vpbroadcastd        m15, [o(vvc_pw_2048)]
    REPX  {pmulhrsw x, m15}, m0, m2, m3, m4, m5, m6, m7
    WRITE_16X2            2,  3,  1,  2, strideq*2, r2
    pmulhrsw             m1, m15, [rsp+32*1]
    WRITE_16X2            0,  1,  2,  3, strideq*0, strideq*1
    lea                  r3, [dstq+strideq*4]
    %define dstq r3
    WRITE_16X2            4,  5,  2,  3, strideq*0, strideq*1
    WRITE_16X2            6,  7,  2,  3, strideq*2, r2
    REPX  {pmulhrsw x, m15}, m8, m9, m10, m11, m12, m13, m14
    lea                  r3, [r3+strideq*4]
    WRITE_16X2            8,  9,  2,  3, strideq*0, strideq*1
    WRITE_16X2           10, 11,  2,  3, strideq*2, r2
    pmulhrsw            m15, [rsp+32*0]
    lea                  r3, [r3+strideq*4]
    WRITE_16X2           12, 13,  2,  3, strideq*0, strideq*1
    WRITE_16X2           14, 15,  2,  3, strideq*2, r2
    add               tmp1q, 32*16
    add                  r0, 16
    dec               tmp2d
    jg .pass2_loop
    RET

cglobal vvc_inv_dct2_dct2_32x64_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    or                  r3d, 64
    jmp m(vvc_inv_dct2_dct2_32x8_8).dconly
.normal:
    PROLOGUE              0, 11, 16, 32*99, dst, stride, c, eob, tmp1, tmp2
    lea               tmp1q, [rsp+32*7]
    lea                r10d, [eobq-136]
    sar                r10d, 31
.pass1_loop:
    lea               tmp2q, [tmp1q+32*16]
    LOAD_8ROWS      cq+64*1, 64*2, 1
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 1, 3, 5, 7, 9, 11, 13, 15
    test               r10b, r10b
    jnz .fast
    LOAD_8ROWS_H   cq+64*17, 64*2, 2
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf
    LOAD_8ROWS_H   cq+64*16, 64*2, 1
    mova              [rsp], m15
    pxor                m15, m15
    REPX {mova [cq+64*x], m15}, 16, 17, 18, 19, 20, 21, 22, 23, \
                                24, 25, 26, 27, 28, 29, 30, 31
    jmp .idct2_16
.fast:
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    pxor                 m8, m8
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14
    mova              [rsp], m8
.idct2_16:
    LOAD_8ROWS      cq+64*0, 64*2, 1
    pxor                m15, m15
    REPX {mova [cq+64*x], m15}, 0, 2, 4, 6, 8, 10, 12, 14
    call m(idct2_16x16_internal_8).main
    call m(vvc_inv_dct2_dct2_32x16_8).pass1_end
    vpbroadcastd         m7, [o(vvc_pw_16384)]
    call m(vvc_inv_dct2_dct2_16x32_8).transpose_2x8x8_round
    lea                  r3, [tmp1q+32*48]
    mova                m15, [rsp]
    mova          [r3-32*4], m0
    mova          [r3-32*3], m2
    mova          [r3-32*2], m4
    mova          [r3-32*1], m6
    mova          [r3+32*0], m8
    mova          [r3+32*1], m10
    mova          [r3+32*2], m12
    mova          [r3+32*3], m14
    add                  r3, 32*24
    mova          [r3-32*4], m1
    mova          [r3-32*3], m3
    mova          [r3-32*2], m5
    mova          [r3-32*1], m7
    mova          [r3+32*0], m9
    mova          [r3+32*1], m11
    mova          [r3+32*2], m13
    mova          [r3+32*3], m15
    vpbroadcastd         m9, [o(vvc_pw_16384)]
    pmulhrsw             m0, m9, [tmp1q-32*4]
    pmulhrsw             m1, m9, [tmp1q-32*3]
    pmulhrsw             m2, m9, [tmp1q-32*2]
    pmulhrsw             m3, m9, [tmp1q-32*1]
    pmulhrsw             m4, m9, [tmp1q+32*0]
    pmulhrsw             m5, m9, [tmp1q+32*1]
    pmulhrsw             m6, m9, [tmp1q+32*2]
    pmulhrsw             m7, m9, [tmp1q+32*3]
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    mova       [tmp1q-32*4], m0
    pmulhrsw             m0, m9, [tmp2q-32*4]
    mova       [tmp2q-32*4], m1
    pmulhrsw             m1, m9, [tmp2q-32*3]
    mova       [tmp1q-32*3], m2
    pmulhrsw             m2, m9, [tmp2q-32*2]
    mova       [tmp2q-32*3], m3
    pmulhrsw             m3, m9, [tmp2q-32*1]
    mova       [tmp1q-32*2], m4
    pmulhrsw             m4, m9, [tmp2q+32*0]
    mova       [tmp2q-32*2], m5
    pmulhrsw             m5, m9, [tmp2q+32*1]
    mova       [tmp1q-32*1], m6
    pmulhrsw             m6, m9, [tmp2q+32*2]
    mova       [tmp2q-32*1], m7
    pmulhrsw             m7, m9, [tmp2q+32*3]
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    mova       [tmp1q+32*0], m0
    mova       [tmp2q+32*0], m1
    mova       [tmp1q+32*1], m2
    mova       [tmp2q+32*1], m3
    mova       [tmp1q+32*2], m4
    mova       [tmp2q+32*2], m5
    mova       [tmp1q+32*3], m6
    mova       [tmp2q+32*3], m7
    add                  cq, 32
    add               tmp1q, 32*8
    add                r10d, 0x80000000
    jnc .pass1_loop
    lea                  r2, [rsp+32*55]
    lea                  r7, [r2+32*24]
.pass2_loop:
    lea                  r3, [r2+32*8]
    lea                  r8, [r7+32*8]
    mova                 m0, [r2-32*4]
    mova                 m1, [r2-32*2]
    mova                 m2, [r2+32*0]
    mova                 m3, [r2+32*2]
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14
    test               r10b, r10b
    jnz .fast2
    mova                 m4, [r3-32*4]
    mova                 m5, [r3-32*2]
    mova                 m6, [r3+32*0]
    mova                 m7, [r3+32*2]
.fast2:
    mova              [rsp], m8
    lea               tmp1q, [rsp+32*39]
    call m(idct2_16x16_internal_8).main
    mova                 m1, [rsp+32*1]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m1
    mova       [tmp1q-32*2], m2
    mova       [tmp1q-32*1], m3
    mova       [tmp1q+32*0], m4
    mova       [tmp1q+32*1], m5
    mova       [tmp1q+32*2], m6
    mova       [tmp1q+32*3], m7
    add               tmp1q, 32*8
    mova       [tmp1q-32*4], m8
    mova       [tmp1q-32*3], m9
    mova       [tmp1q-32*2], m10
    mova       [tmp1q-32*1], m11
    mova       [tmp1q+32*0], m12
    mova       [tmp1q+32*1], m13
    mova       [tmp1q+32*2], m14
    mova       [tmp1q+32*3], m15
    mova                 m0, [r2-32*3]
    mova                 m1, [r2-32*1]
    mova                 m2, [r2+32*1]
    mova                 m3, [r2+32*3]
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7
    test               r10b, r10b
    jnz .fast3
    mova                 m4, [r3-32*3]
    mova                 m5, [r3-32*1]
    mova                 m6, [r3+32*1]
    mova                 m7, [r3+32*3]
.fast3:
    add               tmp1q, 32*8
    lea               tmp2q, [tmp1q+32*8]
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    vpbroadcastd        m15, [o(vvc_pd_64)]
    add               tmp1q, 32*16
    add               tmp2q, 32*32
    mova                 m0, [r7-32*4]
    mova                 m3, [r7+32*3]
    mova                 m4, [r7+32*0]
    mova                 m7, [r7-32*1]
    pxor                 m1, m1
    REPX       {mova x, m1}, m2, m5, m6
    test               r10b, r10b
    jnz .fast4
    mova                 m1, [r8+32*3]
    mova                 m2, [r8-32*4]
    mova                 m5, [r8-32*1]
    mova                 m6, [r8+32*0]
.fast4:
    add                  r6, o_idct2_64_offset
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    add                  r6, 8
    add               tmp1q, 32*8
    sub               tmp2q, 32*8
    mova                 m0, [r7-32*2]
    mova                 m3, [r7+32*1]
    mova                 m4, [r7+32*2]
    mova                 m7, [r7-32*3]
    pxor                 m1, m1
    REPX       {mova x, m1}, m2, m5, m6
    test               r10b, r10b
    jnz .fast5
    mova                 m1, [r8+32*1]
    mova                 m2, [r8-32*2]
    mova                 m5, [r8-32*3]
    mova                 m6, [r8+32*2]
.fast5:
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    call m(vvc_inv_dct2_dct2_16x64_8).main_part2_pass2
    add                r10d, 0x80000000
    jc .ret
    lea                  r2, [rsp+32*7]
    lea                  r7, [r2+32*16]
    sub                dstq, r8
    lea                dstq, [dstq+strideq*4+16]
    jmp .pass2_loop
.ret:
    RET

cglobal vvc_inv_dct2_dct2_64x32_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_16384)]
    mov                [cq], eobd
    pmulhrsw            xm0, xm1
    or                  r3d, 32
    jmp m(vvc_inv_dct2_dct2_64x16_8).dconly
.normal:
    PROLOGUE              0, 9, 16, 32*131, dst, stride, c, eob, tmp1, tmp2, \
                                            base, tmp3, tmp4
    lea               tmp1q, [rsp+32*7]
    lea               tmp4d, [eobq-136]
.pass1_loop:
    LOAD_8ROWS      cq+64*0, 64*4, 1
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 0, 4, 8, 12, 16, 20, 24, 28
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14
    mova              [rsp], m8
    call m(idct2_16x16_internal_8).main
    mova                 m1, [rsp+32*1]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m1
    mova       [tmp1q-32*2], m2
    mova       [tmp1q-32*1], m3
    mova       [tmp1q+32*0], m4
    mova       [tmp1q+32*1], m5
    mova       [tmp1q+32*2], m6
    mova       [tmp1q+32*3], m7
    add               tmp1q, 32*8
    mova       [tmp1q-32*4], m8
    mova       [tmp1q-32*3], m9
    mova       [tmp1q-32*2], m10
    mova       [tmp1q-32*1], m11
    mova       [tmp1q+32*0], m12
    mova       [tmp1q+32*1], m13
    mova       [tmp1q+32*2], m14
    mova       [tmp1q+32*3], m15
    LOAD_8ROWS      cq+64*2, 64*4, 1
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 2, 6, 10, 14, 18, 22, 26, 30
    add               tmp1q, 32*8
    lea               tmp2q, [tmp1q+32*8]
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    vpbroadcastd        m15, [o(vvc_pd_64)]
    add               tmp1q, 32*16
    add               tmp2q, 32*32
    vpbroadcastd         m7, [o(vvc_pw_64x8)]
    pmulhrsw             m0, m7, [cq+64* 1]
    pmulhrsw             m1, m7, [cq+64*31]
    pmulhrsw             m2, m7, [cq+64*17]
    pmulhrsw             m3, m7, [cq+64*15]
    pmulhrsw             m4, m7, [cq+64* 9]
    pmulhrsw             m5, m7, [cq+64*23]
    pmulhrsw             m6, m7, [cq+64*25]
    pmulhrsw             m7,     [cq+64* 7]
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 1, 31, 17, 15, 9, 23, 25, 7
    add                  r6, o_idct2_64_offset
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    vpbroadcastd         m7, [o(vvc_pw_64x8-(o_idct2_64_offset))]
    add                  r6, 8
    add               tmp1q, 32*8
    sub               tmp2q, 32*8
    pmulhrsw             m0, m7, [cq+64* 5]
    pmulhrsw             m1, m7, [cq+64*27]
    pmulhrsw             m2, m7, [cq+64*21]
    pmulhrsw             m3, m7, [cq+64*11]
    pmulhrsw             m4, m7, [cq+64*13]
    pmulhrsw             m5, m7, [cq+64*19]
    pmulhrsw             m6, m7, [cq+64*29]
    pmulhrsw             m7,     [cq+64* 3]
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 5, 27, 21, 11, 13, 19, 29, 3
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    call m(vvc_inv_dct2_dct2_16x64_8).main_part2_pass1
    sub               tmp1q, 32*44
    vpbroadcastd        m10, [o(vvc_pw_16384)]
    call m(vvc_inv_dct2_dct2_64x32_8).transpose_round_interleave
    add                  cq, 32
    add               tmp4d, 0x80000000
    jnc .pass1_loop
    lea               tmp1q, [rsp+32*15]
    imul                 r2, strideq, 19
    lea                  r3, [strideq*3]
    add                  r2, dstq
    mov               tmp4b, 4
.pass2_loop:
    lea               tmp2q, [tmp1q+32*64]
    LOAD_8ROWS   tmp1q-32*4, 32
    test              tmp4d, 0x40000000
    jnz .fast
    LOAD_8ROWS_H tmp2q-32*4, 32
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf
    lea               tmp3q, [tmp2q-32*8]
    LOAD_8ROWS_H tmp3q-32*4, 32
    mova              [rsp], m15
    jmp .idct2_16
.fast:
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    pxor                 m8, m8
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14
    mova              [rsp], m8
.idct2_16:
    lea               tmp3q, [tmp1q-32*8]
    LOAD_8ROWS   tmp3q-32*4, 32
    call m(idct2_16x16_internal_8).main
    call m(vvc_inv_dct2_dct2_16x32_8).pass2_end
    add               tmp1q, 32*16
    sub                dstq, r3
    lea                  r2, [r2+r3+16]
    add                dstq, 16
    dec               tmp4b
    jg .pass2_loop
    RET
ALIGN function_align
.transpose_round_interleave:
    mov               tmp3d, 4
.loop:
    lea               tmp2q, [tmp1q+32*8]
    mova                xm0, [tmp1q-32*4]
    mova                xm1, [tmp1q-32*3]
    vinserti128          m0, [tmp2q-32*4], 1
    vinserti128          m1, [tmp2q-32*3], 1
    mova                xm2, [tmp1q-32*2]
    mova                xm3, [tmp1q-32*1]
    vinserti128          m2, [tmp2q-32*2], 1
    vinserti128          m3, [tmp2q-32*1], 1
    mova                xm4, [tmp1q+32*0]
    mova                xm5, [tmp1q+32*1]
    vinserti128          m4, [tmp2q+32*0], 1
    vinserti128          m5, [tmp2q+32*1], 1
    mova                xm6, [tmp1q+32*2]
    mova                xm7, [tmp1q+32*3]
    vinserti128          m6, [tmp2q+32*2], 1
    vinserti128          m7, [tmp2q+32*3], 1
    REPX  {pmulhrsw x, m10}, m0, m1, m2, m3, m4, m5, m6, m7
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    mova                xm8, [tmp1q-32*4+16]
    mova                xm9, [tmp1q-32*3+16]
    vinserti128          m8, [tmp2q-32*4+16], 1
    vinserti128          m9, [tmp2q-32*3+16], 1
    mova       [tmp1q-32*4], m0
    mova       [tmp2q-32*4], m1
    mova       [tmp1q-32*3], m2
    mova       [tmp2q-32*3], m3
    mova                xm2, [tmp1q-32*2+16]
    mova                xm3, [tmp1q-32*1+16]
    vinserti128          m2, [tmp2q-32*2+16], 1
    vinserti128          m3, [tmp2q-32*1+16], 1
    mova       [tmp1q-32*2], m4
    mova       [tmp2q-32*2], m5
    mova       [tmp1q-32*1], m6
    mova       [tmp2q-32*1], m7
    mova                xm4, [tmp1q+32*0+16]
    mova                xm5, [tmp1q+32*1+16]
    vinserti128          m4, [tmp2q+32*0+16], 1
    vinserti128          m5, [tmp2q+32*1+16], 1
    mova                xm6, [tmp1q+32*2+16]
    mova                xm7, [tmp1q+32*3+16]
    vinserti128          m6, [tmp2q+32*2+16], 1
    vinserti128          m7, [tmp2q+32*3+16], 1
    pmulhrsw             m0, m8, m10
    pmulhrsw             m1, m9, m10
    REPX  {pmulhrsw x, m10}, m2, m3, m4, m5, m6, m7
    call m(vvc_inv_identity_identity_8x32_8).transpose8x8
    mova       [tmp1q+32*0], m0
    mova       [tmp2q+32*0], m1
    mova       [tmp1q+32*1], m2
    mova       [tmp2q+32*1], m3
    mova       [tmp1q+32*2], m4
    mova       [tmp2q+32*2], m5
    mova       [tmp1q+32*3], m6
    mova       [tmp2q+32*3], m7
    add               tmp1q, 32*16
    dec               tmp3d
    jg .loop
    ret

cglobal vvc_inv_dct2_dct2_64x64_8, 4, 4, 0, dst, stride, c, eob
    lea                  r6, [o_base]
    test               eobd, eobd
    jnz .normal
    movd                xm1, [o(vvc_pw_64x8)]
    pmulhrsw            xm0, xm1, [cq]
    movd                xm2, [o(vvc_pw_8192)]
    mov                [cq], eobd
    or                  r3d, 64
    jmp m(vvc_inv_dct2_dct2_64x16_8).dconly
.normal:
    PROLOGUE              0, 11, 16, 32*199, dst, stride, c, eob, tmp1, tmp2
    lea               tmp1q, [rsp+32*71]
    lea                r10d, [eobq-136]
.pass1_loop:
    LOAD_8ROWS      cq+64*0, 64*4
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 0, 4, 8, 12, 16, 20, 24, 28
    REPX       {mova x, m8}, m9, m10, m11, m12, m13, m14
    mova              [rsp], m8
    call m(idct2_16x16_internal_8).main
    mova                 m1, [rsp+32*1]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m1
    mova       [tmp1q-32*2], m2
    mova       [tmp1q-32*1], m3
    mova       [tmp1q+32*0], m4
    mova       [tmp1q+32*1], m5
    mova       [tmp1q+32*2], m6
    mova       [tmp1q+32*3], m7
    add               tmp1q, 32*8
    mova       [tmp1q-32*4], m8
    mova       [tmp1q-32*3], m9
    mova       [tmp1q-32*2], m10
    mova       [tmp1q-32*1], m11
    mova       [tmp1q+32*0], m12
    mova       [tmp1q+32*1], m13
    mova       [tmp1q+32*2], m14
    mova       [tmp1q+32*3], m15
    LOAD_8ROWS      cq+64*2, 64*4
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 2, 6, 10, 14, 18, 22, 26, 30
    add               tmp1q, 32*8
    lea               tmp2q, [tmp1q+32*8]
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    vpbroadcastd        m15, [o(vvc_pd_64)]
    add               tmp1q, 32*16
    add               tmp2q, 32*32
    mova                 m0, [cq+64* 1]
    mova                 m1, [cq+64*31]
    mova                 m2, [cq+64*17]
    mova                 m3, [cq+64*15]
    mova                 m4, [cq+64* 9]
    mova                 m5, [cq+64*23]
    mova                 m6, [cq+64*25]
    mova                 m7, [cq+64* 7]
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 1, 31, 17, 15, 9, 23, 25, 7
    add                  r6, o_idct2_64_offset
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    add                  r6, 8
    add               tmp1q, 32*8
    sub               tmp2q, 32*8
    mova                 m0, [cq+64* 5]
    mova                 m1, [cq+64*27]
    mova                 m2, [cq+64*21]
    mova                 m3, [cq+64*11]
    mova                 m4, [cq+64*13]
    mova                 m5, [cq+64*19]
    mova                 m6, [cq+64*29]
    mova                 m7, [cq+64* 3]
    pxor                 m8, m8
    REPX {mova [cq+64*x], m8}, 5, 27, 21, 11, 13, 19, 29, 3
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    call m(vvc_inv_dct2_dct2_16x64_8).main_part2_pass1
    sub               tmp1q, 32*44
    vpbroadcastd        m10, [o(vvc_pw_8192)]
    call m(vvc_inv_dct2_dct2_64x32_8).transpose_round_interleave
    add                  cq, 32
    add                r10d, 0x80000000
    jnc .pass1_loop
    lea               tmp1q, [rsp+32*7]
    mov                r10b, 4
.pass2_loop:
    lea                  r2, [tmp1q+32*64]
    mova                 m0, [r2-32*4]
    mova                 m1, [r2-32*2]
    mova                 m2, [r2+32*0]
    mova                 m3, [r2+32*2]
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7, m8, m9, m10, m11, m12, m13, m14
    mova              [rsp], m4
    test               r10d, 0x40000000
    jnz .fast
    lea                  r3, [r2+32*64]
    mova                 m4, [r3-32*4]
    mova                 m5, [r3-32*2]
    mova                 m6, [r3+32*0]
    mova                 m7, [r3+32*2]
.fast:
    call m(idct2_16x16_internal_8).main
    mova                 m1, [rsp+32*1]
    mova       [tmp1q-32*4], m0
    mova       [tmp1q-32*3], m1
    mova       [tmp1q-32*2], m2
    mova       [tmp1q-32*1], m3
    mova       [tmp1q+32*0], m4
    mova       [tmp1q+32*1], m5
    mova       [tmp1q+32*2], m6
    mova       [tmp1q+32*3], m7
    add               tmp1q, 32*8
    mova       [tmp1q-32*4], m8
    mova       [tmp1q-32*3], m9
    mova       [tmp1q-32*2], m10
    mova       [tmp1q-32*1], m11
    mova       [tmp1q+32*0], m12
    mova       [tmp1q+32*1], m13
    mova       [tmp1q+32*2], m14
    mova       [tmp1q+32*3], m15
    mova                 m0, [r2-32*3]
    mova                 m1, [r2-32*1]
    mova                 m2, [r2+32*1]
    mova                 m3, [r2+32*3]
    pxor                 m4, m4
    REPX       {mova x, m4}, m5, m6, m7
    test               r10d, 0x40000000
    jnz .fast2
    mova                 m4, [r3-32*3]
    mova                 m5, [r3-32*1]
    mova                 m6, [r3+32*1]
    mova                 m7, [r3+32*3]
.fast2:
    add               tmp1q, 32*8
    lea               tmp2q, [tmp1q+32*8]
    call m(vvc_inv_dct2_dct2_16x32_8).main_oddhalf_fast
    vpbroadcastd        m15, [o(vvc_pd_64)]
    add                  r2, 32*8
    add                  r3, 32*8
    add               tmp1q, 32*16
    add               tmp2q, 32*32
    mova                 m0, [r2-32*4] ;  1
    mova                 m3, [r2+32*3] ; 15
    mova                 m4, [r2+32*0] ;  9
    mova                 m7, [r2-32*1] ;  7
    pxor                 m1, m1
    REPX       {mova x, m1}, m2, m5, m6
    test               r10d, 0x40000000
    jnz .fast3
    mova                 m1, [r3+32*3] ; 31
    mova                 m2, [r3-32*4] ; 17
    mova                 m5, [r3-32*1] ; 23
    mova                 m6, [r3+32*0] ; 25
.fast3:
    add                  r6, o_idct2_64_offset
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    add                  r6, 8
    add               tmp1q, 32*8
    sub               tmp2q, 32*8
    mova                 m0, [r2-32*2] ;  5
    mova                 m3, [r2+32*1] ; 11
    mova                 m4, [r2+32*2] ; 13
    mova                 m7, [r2-32*3] ;  3
    pxor                 m1, m1
    REPX       {mova x, m1}, m2, m5, m6
    test               r10d, 0x40000000
    jnz .fast4
    mova                 m1, [r3+32*1] ; 27
    mova                 m2, [r3-32*2] ; 21
    mova                 m5, [r3-32*3] ; 19
    mova                 m6, [r3+32*2] ; 29
.fast4:
    call m(vvc_inv_dct2_dct2_16x64_8).main_part1
    call m(vvc_inv_dct2_dct2_16x64_8).main_part2_pass2
    sub               tmp1q, 32*28
    sub                dstq, r8
    lea                dstq, [dstq+strideq*4+16]
    dec                r10b
    jg .pass2_loop
    RET

%endif ; ARCH_X86_64
