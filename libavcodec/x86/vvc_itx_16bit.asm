; Copyright © 2023, Frank Plowman
; Copyright © 2021, VideoLAN and dav1d authors
; Copyright © 2021, Two Orioles, LLC
; Copyright © 2021, Matthias Dressel
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

SECTION_RODATA 32

idct4_shuf:   db  0,  1,  4,  5, 12, 13,  8,  9,  2,  3,  6,  7, 14, 15, 10, 11

%macro COEF_PAIR 2-3 0
vvc_pd_%1_%2: dd %1, %1, %2, %2
%define vvc_pd_%1 (vvc_pd_%1_%2 + 4*0)
%define vvc_pd_%2 (vvc_pd_%1_%2 + 4*2)
%endmacro

COEF_PAIR 64, 36
COEF_PAIR 64, 83

coeff_min_15: times 2 dw  -0x8000
coeff_max_15: times 2 dw   0x7fff
dconly_10:    times 2 dw 0x7c00

cextern vvc_pw_36_83
cextern vvc_pw_m36_m83
cextern vvc_pw_m83_36
cextern vvc_pw_64_64
cextern vvc_pw_m64_64
cextern vvc_pw_512

cextern vvc_pd_512

SECTION .text

%define m(x) mangle(private_prefix %+ _ %+ x %+ SUFFIX)

%macro WRAP_XMM 1+
    INIT_XMM cpuname
    %1
    INIT_YMM cpuname
%endmacro

INIT_YMM avx2

; dst1 = (src1 * coef1 - src2 * coef2 + rnd) >> 7
; dst2 = (src1 * coef2 + src2 * coef1 + rnd) >> 7
; flags: 1 = packed, 2 = inv_dst2
; skip round/shift if rnd is not a number
%macro ITX_MULSUB_2D 8-9 0 ; dst/src[1-2], tmp[1-3], rnd, coef[1-2], flags
%if %8 < 32
    pmulld              m%4, m%1, m%8
    pmulld              m%3, m%2, m%8
%else
%if %9 & 1
    vbroadcasti128      m%3, [vvc_pd_%8]
%else
    vpbroadcastd        m%3, [vvc_pd_%8]
%endif
    pmulld              m%4, m%1, m%3
    pmulld              m%3, m%2
%endif
%if %7 < 32
    pmulld              m%1, m%7
    pmulld              m%2, m%7
%else
%if %9 & 1
    vbroadcasti128      m%5, [vvc_pd_%7]
%else
    vpbroadcastd        m%5, [vvc_pd_%7]
%endif
    pmulld              m%1, m%5
    pmulld              m%2, m%5
%endif
%if %9 & 2
    psubd               m%4, m%6, m%4
    psubd               m%2, m%4, m%2
%else
%ifnum %6
    paddd               m%4, m%6
%endif
    paddd               m%2, m%4
%endif
%ifnum %6
    paddd               m%1, m%6
%endif
    psubd               m%1, m%3
%ifnum %6
    psrad               m%2, 7
    psrad               m%1, 7
%endif
%endmacro

%macro INV_TXFM_FN 4-5 10 ; type1, type2, eob_offset, size, bitdepth
cglobal vvc_inv_%1_%2_%4_%5, 4, 6, 0, dst, c, eob, l2tr, stride, tx2
    %define %%p1 m(i%1_%4_internal_%5)
    ; Jump to the 1st txfm function if we're not taking the fast path, which
    ; in turn performs an indirect jump to the 2nd txfm function.
    lea tx2q, [m(i%2_%4_internal_%5).pass2]
%ifidn %1_%2, dct2_dct2
    test               eobd, eobd
    jnz %%p1
%else
%if %3
    add                eobd, %3
%endif
    ; jump to the 1st txfm function unless it's located directly after this
    times ((%%end - %%p1) >> 31) & 1 jmp %%p1
ALIGN function_align
%%end:
%endif
%endmacro

%macro INV_TXFM_4X4_FN 2-3 10 ; type1, type2, bitdepth
    INV_TXFM_FN          %1, %2, 0, 4x4, %3
%ifidn %1_%2, dct2_dct2
    vpbroadcastd        xm2, [dconly_%3]
%if %3 = 10
.dconly:
    imul                r7d, [cq], 181
    mov                [cq], eobd ; 0
    or                  r2d, 4
.dconly2:
    add                 r7d, 128
    sar                 r7d, 8
.dconly3:
    imul                r7d, 181
    add                 r7d, 2176
    sar                 r7d, 12
    movd                xm0, r7d
    paddsw              xm0, xm2
    vpbroadcastw        xm0, xm0
.dconly_loop:
    movq                xm1, [dstq+strideq*0]
    movhps              xm1, [dstq+strideq*1]
    paddsw              xm1, xm0
    psubusw             xm1, xm2
    movq   [dstq+strideq*0], xm1
    movhps [dstq+strideq*1], xm1
    lea                dstq, [dstq+strideq*2]
    sub                 r2d, 2
    jg .dconly_loop
    WRAP_XMM RET
%else
    jmp m(vvc_inv_dct2_dct2_4x4_10).dconly
%endif
%endif
%endmacro

%macro IDCT2_4_1D_PACKED 6 ; dst/src[1-2], tmp[1-3], rnd
    ITX_MULSUB_2D        %1, %2, %3, %4, %5, nornd, 64_36, 64_83, 1
    punpckhqdq          m%3, m%2, m%1 ; t3 t2
    punpcklqdq          m%2, m%1      ; t0 t1
    paddd               m%1, m%2, m%3 ; out0 out1
    psubd               m%2, m%3      ; out3 out2
    paddd               m%1, m%6
    paddd               m%2, m%6
    psrad               m%1, 7
    psrad               m%2, 7
    vpbroadcastd        m%3, [coeff_min_15]
    vpbroadcastd        m%4, [coeff_max_15]
    pmaxsd              m%1, m%3
    pmaxsd              m%2, m%3
    pminsd              m%1, m%4
    pminsd              m%2, m%4
%endmacro

%macro IDCT2_4_1D_PACKED_WORD 6 ; dst/src[1-2], tmp[1-3], rnd
    vpbroadcastd        m%5, [vvc_pw_m83_36]
    punpckhwd           m%3, m%2, m%1
    vpbroadcastd        m%4, [vvc_pw_36_83]
    punpcklwd           m%2, m%1
    vpbroadcastd        m%1, [vvc_pw_m64_64]
    pmaddwd             m%5, m%3
    pmaddwd             m%3, m%4
    vpbroadcastd        m%4, [vvc_pw_64_64]
    pmaddwd             m%1, m%2
    pmaddwd             m%2, m%4
    paddd               m%4, m%1, m%5
    psubd               m%5, m%1, m%5
    paddd               m%1, m%2, m%3
    psubd               m%2, m%3
    REPX     {paddd x, m%6}, m%4, m%1, m%5, m%2
    REPX     {psrad x, 10 }, m%4, m%1, m%5, m%2
    packssdw            m%1, m%4
    packssdw            m%2, m%5
%endmacro

INV_TXFM_4X4_FN dct2, dct2

cglobal idct2_4x4_internal_10, 0, 8, 6, dst, c, eob, l2tr, stride, tx2
    mov             strideq, 8
    call .main
    vbroadcasti128       m2, [idct4_shuf]
    packssdw             m0, m1
    pshufb               m0, m2
    jmp                tx2q
.pass2:
    vextracti128        xm1, m0, 1
    vpbroadcastd        xm5, [vvc_pd_512]
    WRAP_XMM IDCT2_4_1D_PACKED_WORD 0, 1, 2, 3, 4, 5
    lea                  r7, [dstq+strideq*2]


    movq   [dstq+strideq*0], xm0
    movhps [dstq+strideq*1], xm0
    movhps [r7  +strideq*0], xm1
    movq   [r7  +strideq*1], xm1
    RET
ALIGN function_align
.main:
    vpermq               m0, [cq+32*0], q3120
    vpermq               m1, [cq+32*1], q3120
    vpbroadcastd         m5, [vvc_pd_64]
.main2:
    IDCT2_4_1D_PACKED     0, 1, 2, 3, 4, 5
    ret

%endif ; ARCH_X86_64
