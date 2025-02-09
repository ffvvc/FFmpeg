; /*
; * Provide SIMD deblock functions for VVC
; * Copyright (c) 2025 Stone Chen
; * Copyright (c) 2025 Nuo Mi
; * All rights reserved.
; *
; * This file is part of FFmpeg.
; *
; * FFmpeg is free software; you can redistribute it and/or
; * modify it under the terms of the GNU Lesser General Public
; * License as published by the Free Software Foundation; either
; * version 2.1 of the License, or (at your option) any later version.
; *
; * FFmpeg is distributed in the hope that it will be useful,
; * but WITHOUT ANY WARRANTY; without even the implied warranty of
; * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
; * Lesser General Public License for more details.
; *
; * You should have received a copy of the GNU Lesser General Public
; * License along with FFmpeg; if not, write to the Free Software
; * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
; */

%include "libavutil/x86/x86util.asm"
%include "libavcodec/x86/h26x/h2656_deblock.asm"

%macro LUMA_DEBLOCK_BODY 2
    H2656_LUMA_DEBLOCK_BODY vvc, %1, %2
%endmacro

SECTION_RODATA

cextern pw_1
cextern pw_2
cextern pw_3
cextern pw_4
cextern pw_5
cextern pw_8
cextern pw_1023
cextern pw_m1
cextern pd_1

%define pw_pixel_max_10 pw_1023
pw_pixel_max_12: times 8 dw ((1 << 12)-1)
pw_m2:           times 8 dw -2

SECTION .text

%macro CHROMA_LOAD_PARAM 3 ; dst, src, is_i32
    cmp         shiftd, 1
    jne             %%no_shift
%if %3
    movdqu          %1, [%2]
%else
    pmovzxbd        %1, [%2]
%endif
    pshufhw         %1, %1, q2200
    pshuflw         %1, %1, q2200
    jmp             %%end_shift

%%no_shift:
%if %3
    movq            %1, [%2]
%else
    vpbroadcastw    %1, [%2]
    pmovzxbd        %1, xmm%1
%endif
    punpcklwd       %1, %1
    vpermilps       %1, %1, q2200

%%end_shift:
%endmacro

%macro CHROMA_LOAD_I32 2 ; dst, src
    CHROMA_LOAD_PARAM %1, %2, 1
%endmacro

%macro CHROMA_LOAD_U8 2 ; dst, src
    CHROMA_LOAD_PARAM %1, %2, 0
%endmacro

%macro CHROMA_LOAD_BETA 2 ; dst, bit_depth
    CHROMA_LOAD_I32  %1, betaq
%if %2 > 8
    psllw            %1, %2 - 8
%endif
%endmacro

; input: shift, betaq
; output: m8(-tc), m9(tc)
; CHROMA_LOAD_TC(bit_depth)
%macro CHROMA_LOAD_TC 1
    CHROMA_LOAD_I32   m9, tcq

%if %1 > 10
    psllw             m9, %1 - 10
%elif %1 < 10
    paddw             m9, [pw_2]
    psrlw             m9, 2
%endif

    pxor              m8, m8
    psubw             m8, m9  ; -tc
%endmacro

; m14: output, d0+d1 or dsam0&&dsam1
%macro CHROMA_COMBINE 1 ; op
    cmp                shiftd, 1
    je                 %%shift
    psrlq              m15, m13, 48
    %1                 m14, m13, m15
    pshufhw            m14, m14, 0
    pshuflw            m14, m14, 0
    jmp                %%end_shift
%%shift:
    psrld              m15, m13, 16
    %1                 m14, m13, m15
    pshufhw            m14, m14, q2200
    pshuflw            m14, m14, q2200
%%end_shift:
%endmacro

%macro CHROMA_DECIDE_TYPE 1
    pcmpeqw            m15, m13, [pw_3]
    pand               m11, m15
    ptest              m11, m11
    jz                 .weak

    pcmpeqw            m12, [pw_1]
    pblendvb           m13, m1, m2, m12 ; p2 and p2n
    psubw              m13, m2
    psubw              m13, m2
    paddw              m13, m3
    pabsw              m13, m13         ; abs(p2 - 2 * p1 + p0)  and abs(p2n - 2 * p1n + p0n)

    psubw              m14, m6, m5      ; Q2 and q2n
    psubw              m14, m5
    paddw              m14, m4
    pabsw              m14, m14

    paddw              m13, m14         ; d0 and d1

    ; compare d0 + d1 with beta
    CHROMA_COMBINE     paddw
    CHROMA_LOAD_BETA   m15, %1
    pcmpgtw            m14, m15, m14    ; d0 + d1 < beta
    pand               m11, m14

    ; compare d0 << 1 and d1 << 1 with beta2
    psrlw              m15, 2           ; beta_2
    psllw              m13, 1
    pcmpgtw            m13, m15, m13    ; (d0 << 1) and (d1 << 1) < beta_2

    ; compare d0 and d1 with beta2
    psrlw              m15, 1           ; beta_3
    pblendvb           m12, m0, m2, m12 ; p3 and p3n
    psubw              m12, m3
    pabsw              m12, m12         ; abs(p3 - p0) and abs(p3n - p0n)
    psubw              m14, m4, m7
    pabsw              m14, m14         ; abs(Q0 - Q3) and abs(q0n - q3n)
    paddw              m12, m14
    pcmpgtw            m12, m15, m12    ; abs(p3 - p0) + abs(p3 - p0) and abs(p3n - p0n) + abs(q0n - q3n) < beta_3
    pand               m13, m12

    ; compare abs(p0 - Q0) and abs(p0n - q0n) with tc25
    psllw              m15, m9, 2       ; tc * 4
    pavgw              m15, m9          ; tc25
    psubw              m14, m3, m4      ; (p0 - Q0) and (p3n - p0n)
    pabsw              m14, m14
    pcmpgtw            m14, m15, m14    ; (p0 - Q0) and (p3n - p0n) < tc25
    pand               m13, m14

    CHROMA_COMBINE     pand             ; dsam0 && dsam1
    pand               m11, m14
%endmacro

%macro WEAK_CHROMA 0
    H2656_CHROMA_DEBLOCK m14, m15, m2, m3, m4, m5, m8, m9, m12, m13 ; (dst0, dst1, p1, p0, q0, q1, -tc, tc, tmp1, tmp2)
    MASKED_COPY       m3, m14
    MASKED_COPY       m4, m15
%endmacro

%macro CHROMA_FILTER 1 ;(dst)
    paddw               m15, m14
    paddw               m15, m13
    H2656_CHROMA_ROUND  m15

    psubw               m15, %1
    CLIPW               m15, m8, m9 ; clip to [-tc, tc]
    paddw               m15, %1

    MASKED_COPY          %1, m15
%endmacro

%macro ONE_SIDE_CHROMA 0
    paddw          m12, m3, m4      ; p0 + q0
    paddw          m13, m5, m6      ; q1 + q2
    paddw          m13, m12         ; p0 + q0 + q1 + q2

    ; P0
    paddw          m14, m2, m2      ; 2 * p1
    paddw          m15, m2, m3      ; p1 + p0
    CHROMA_FILTER  m3

    ; Q0
    paddw          m15, m4, m7      ; q0 + q3
    CHROMA_FILTER  m4

    ; Q1
    paddw          m14, m7, m7      ; 2 * q3
    paddw          m15, m2, m5      ; p1 + q1
    CHROMA_FILTER  m5

    ; Q2
    paddw          m15, m6, m7      ; q2 + q3
    CHROMA_FILTER  m6
%endmacro

%macro STRONG_CHROMA 0
    mova            m10, m1          ; save p2
    mova            m12, m2          ; save p1

    paddw           m14, m1, m2      ; p2 + p1
    paddw           m13, m3, m4      ; p0 + q0
    paddw           m13, m14         ; p2 + p1 + p0 + q0

    ; P2
    paddw          m14, m0, m0       ; 2 * p3
    paddw          m15, m0, m1       ; p3 + p2
    CHROMA_FILTER  m1

    ; P1
    paddw          m15, m2, m5       ; p1 + q1
    CHROMA_FILTER  m2

    ; P0
    paddw          m14, m0, m3       ; p3 + p0
    paddw          m15, m5, m6       ; q1 + q2
    CHROMA_FILTER  m3

    ; Q0
    paddw          m14, m4, m5       ; q0 + q1
    paddw          m15, m6, m7       ; q2 + q3
    CHROMA_FILTER  m4

    ; Q1
    psubw          m10, m5           ; p2 - q1
    psubw          m13, m10          ; p1 + p0 + q0 + q1
    paddw          m14, m7, m7       ; 2 * q3
    paddw          m15, m5, m6       ; q1 + q2
    CHROMA_FILTER  m5

    ; Q2
    psubw          m12, m6           ; p1 - q2
    psubw          m13, m12          ; p0 + q0 + q1 + q2
    paddw          m15, m6, m7       ; q2 + q3
    CHROMA_FILTER  m6
%endmacro

; CHROMA_DEBLOCK_BODY(bit_depth)
%macro CHROMA_DEBLOCK_BODY 1
    CHROMA_LOAD_TC    %1

    pxor               m10, m10   ; 0

    ; if (tc)
    pcmpgtw            m11, m9, m10
    ptest              m11, m11
    jz              .bypass

    ; if (!max_len_q || !max_len_p)
    CHROMA_LOAD_U8     m12, max_len_pq
    CHROMA_LOAD_U8     m13, max_len_qq
    pcmpgtw            m14, m12, m10
    pcmpgtw            m15, m13, m10
    pand               m14, m15
    ptest              m14, m14
    jz               .bypass

    ; save filter mask
    pand               m11, m14
    mova               m10, m11

    CHROMA_DECIDE_TYPE %1

.weak:
    pandn              m11, m10
    ptest              m11, m11
    jz                 .one_side
    WEAK_CHROMA
    pandn              m10, m11, m10
    ptest              m10, m10
    jz                 .bypass

.one_side:
    CHROMA_LOAD_U8     m11, max_len_pq
    pcmpeqw            m11, [pw_1]
    pand               m11, m10
    ptest              m11, m11
    jz                 .strong
    ONE_SIDE_CHROMA
    pandn              m10, m11, m10
    ptest              m10, m10
    jz                 .bypass

.strong:
    mova               m11, m10
    STRONG_CHROMA

.bypass:
%endmacro

%macro LOOP_FILTER_CHROMA 0
cglobal vvc_v_loop_filter_chroma_8, 9, 11, 16, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE8x8B_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 8
    TRANSPOSE8x8B_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    RET

cglobal vvc_v_loop_filter_chroma_10, 9, 11, 16, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift, pix0, r3stride
    sub            pixq, 8
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE8x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 10
    TRANSPOSE8x8W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_10]
    RET

cglobal vvc_v_loop_filter_chroma_12, 9, 11, 16, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift, pix0, r3stride
    sub            pixq, 8
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE8x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 12
    TRANSPOSE8x8W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_12]
    RET

cglobal vvc_h_loop_filter_chroma_8, 9, 11, 16, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift, pix0, src3stride
    lea     src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq

    movq             m0, [pix0q             ]  ;  p3
    movq             m1, [pix0q +   strideq]   ;  p2
    movq             m2, [pix0q + 2 * strideq] ;  p1
    movq             m3, [pix0q + src3strideq] ;  p0
    movq             m4, [pixq]                ;  q0
    movq             m5, [pixq +     strideq]  ;  q1
    movq             m6, [pixq + 2 * strideq]  ;  q2
    movq             m7, [pixq + src3strideq]  ;  q3

    pxor            m12, m12 ; zeros reg
    punpcklbw        m0, m12
    punpcklbw        m1, m12
    punpcklbw        m2, m12
    punpcklbw        m3, m12
    punpcklbw        m4, m12
    punpcklbw        m5, m12
    punpcklbw        m6, m12
    punpcklbw        m7, m12

    CHROMA_DEBLOCK_BODY 8

    packuswb          m0, m1
    packuswb          m2, m3
    packuswb          m4, m5
    packuswb          m6, m7

    movh     [pix0q              ], m0
    movhps   [pix0q +   strideq  ], m0
    movh     [pix0q + 2 * strideq], m2
    movhps   [pix0q + src3strideq], m2
    movh     [pixq]               , m4
    movhps   [pixq +     strideq] , m4
    movh     [pixq + 2 * strideq] , m6
    movhps   [pixq + src3strideq] , m6

RET

cglobal vvc_h_loop_filter_chroma_10, 9, 11, 16, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift, pix0, src3stride
    lea    src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq

    movu             m0, [pix0q]               ;  p3
    movu             m1, [pix0q + strideq]     ;  p2
    movu             m2, [pix0q + 2 * strideq] ;  p1
    movu             m3, [pix0q + src3strideq] ;  p0
    movu             m4, [pixq]                ;  q0
    movu             m5, [pixq +     strideq]  ;  q1
    movu             m6, [pixq + 2 * strideq]  ;  q2
    movu             m7, [pixq + src3strideq]  ;  q3

    CHROMA_DEBLOCK_BODY 10

    pxor           m12, m12
    CLIPW           m1, m12, [pw_pixel_max_10] ; p2
    CLIPW           m2, m12, [pw_pixel_max_10] ; p1
    CLIPW           m3, m12, [pw_pixel_max_10] ; p0
    CLIPW           m4, m12, [pw_pixel_max_10] ; q0
    CLIPW           m5, m12, [pw_pixel_max_10] ; q1
    CLIPW           m6, m12, [pw_pixel_max_10] ; q2

    movu   [pix0q +     strideq], m1
    movu   [pix0q +   2*strideq], m2
    movu   [pix0q + src3strideq], m3

    movu                  [pixq], m4
    movu    [pixq +     strideq], m5
    movu    [pixq + 2 * strideq], m6

    RET

cglobal vvc_h_loop_filter_chroma_12, 9, 11, 16, pix, stride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift, pix0, src3stride
    lea    src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq

    movu             m0, [pix0q]               ;  p3
    movu             m1, [pix0q + strideq]     ;  p2
    movu             m2, [pix0q + 2 * strideq] ;  p1
    movu             m3, [pix0q + src3strideq] ;  p0
    movu             m4, [pixq]                ;  q0
    movu             m5, [pixq +     strideq]  ;  q1
    movu             m6, [pixq + 2 * strideq]  ;  q2
    movu             m7, [pixq + src3strideq]  ;  q3

    CHROMA_DEBLOCK_BODY 12

    pxor           m12, m12
    CLIPW           m1, m12, [pw_pixel_max_12] ; p2
    CLIPW           m2, m12, [pw_pixel_max_12] ; p1
    CLIPW           m3, m12, [pw_pixel_max_12] ; p0
    CLIPW           m4, m12, [pw_pixel_max_12] ; q0
    CLIPW           m5, m12, [pw_pixel_max_12] ; p0
    CLIPW           m6, m12, [pw_pixel_max_12] ; p0

    movu   [pix0q +     strideq], m1
    movu   [pix0q +   2*strideq], m2
    movu   [pix0q + src3strideq], m3
    movu                  [pixq], m4
    movu    [pixq +     strideq], m5
    movu    [pixq + 2 * strideq], m6

    RET

%endmacro

INIT_XMM avx
LOOP_FILTER_CHROMA

INIT_XMM avx2
LOOP_FILTER_CHROMA


%if ARCH_X86_64
%macro LOOP_FILTER_LUMA 0
;-----------------------------------------------------------------------------
; void ff_vvc_v_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int32_t *beta, int32_t *tc,
;     uint8_t *_no_p, uint8_t *_no_q, uint8_t *max_len_p, uint8_t *max_len_q, int hor_ctu_edge);
;-----------------------------------------------------------------------------
cglobal vvc_v_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 4
    lea           pix0q, [3 * r1]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8B_LOAD  PASS8ROWS(src3strideq, pixq, r1, pix0q)
    LUMA_DEBLOCK_BODY 8, v
.store:
    TRANSPOSE8x8B_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q)
.bypassluma:
    RET

cglobal vvc_v_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 8
    lea           pix0q, [3 * strideq]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8W_LOAD  PASS8ROWS(src3strideq, pixq, strideq, pix0q)
    LUMA_DEBLOCK_BODY 10, v
.store:
    TRANSPOSE8x8W_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q), [pw_pixel_max_10]
.bypassluma:
    RET

cglobal vvc_v_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    sub            pixq, 8
    lea           pix0q, [3 * strideq]
    mov     src3strideq, pixq
    add            pixq, pix0q
    TRANSPOSE8x8W_LOAD  PASS8ROWS(src3strideq, pixq, strideq, pix0q)
    LUMA_DEBLOCK_BODY 12, v
.store:
    TRANSPOSE8x8W_STORE PASS8ROWS(src3strideq, pixq, r1, pix0q), [pw_pixel_max_12]
.bypassluma:
    RET

;-----------------------------------------------------------------------------
; void ff_vvc_h_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int32_t *beta, int32_t *tc,
;     uint8_t *_no_p, uint8_t *_no_q, uint8_t *max_len_p, uint8_t *max_len_q, int hor_ctu_edge);
;-----------------------------------------------------------------------------
cglobal vvc_h_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    lea     src3strideq, [3 * strideq]
    mov           pix0q, pixq
    sub           pix0q, src3strideq
    sub           pix0q, strideq
    movq             m0, [pix0q];               p3
    movq             m1, [pix0q +     strideq]; p2
    movq             m2, [pix0q + 2 * strideq]; p1
    movq             m3, [pix0q + src3strideq]; p0
    movq             m4, [pixq];                q0
    movq             m5, [pixq +     strideq];  q1
    movq             m6, [pixq + 2 * strideq];  q2
    movq             m7, [pixq + src3strideq];  q3
    pxor             m8, m8
    punpcklbw        m0, m8
    punpcklbw        m1, m8
    punpcklbw        m2, m8
    punpcklbw        m3, m8
    punpcklbw        m4, m8
    punpcklbw        m5, m8
    punpcklbw        m6, m8
    punpcklbw        m7, m8
    LUMA_DEBLOCK_BODY 8, h
.store:
    packuswb          m1, m2
    packuswb          m3, m4
    packuswb          m5, m6
    movh   [pix0q +     strideq], m1
    movhps [pix0q + 2 * strideq], m1
    movh   [pix0q + src3strideq], m3
    movhps [pixq               ], m3
    movh   [pixq  +     strideq], m5
    movhps [pixq  + 2 * strideq], m5
.bypassluma:
    RET

cglobal vvc_h_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    lea                  src3strideq, [3 * strideq]
    mov                        pix0q, pixq
    sub                        pix0q, src3strideq
    sub                        pix0q, strideq
    movdqu                        m0, [pix0q];               p3
    movdqu                        m1, [pix0q +     strideq]; p2
    movdqu                        m2, [pix0q + 2 * strideq]; p1
    movdqu                        m3, [pix0q + src3strideq]; p0
    movdqu                        m4, [pixq];                q0
    movdqu                        m5, [pixq  +     strideq]; q1
    movdqu                        m6, [pixq  + 2 * strideq]; q2
    movdqu                        m7, [pixq  + src3strideq]; q3
    LUMA_DEBLOCK_BODY             10, h
.store:
    pxor                          m8, m8; zeros reg
    CLIPW                         m1, m8, [pw_pixel_max_10]
    CLIPW                         m2, m8, [pw_pixel_max_10]
    CLIPW                         m3, m8, [pw_pixel_max_10]
    CLIPW                         m4, m8, [pw_pixel_max_10]
    CLIPW                         m5, m8, [pw_pixel_max_10]
    CLIPW                         m6, m8, [pw_pixel_max_10]
    movdqu     [pix0q +     strideq], m1;  p2
    movdqu     [pix0q + 2 * strideq], m2;  p1
    movdqu     [pix0q + src3strideq], m3;  p0
    movdqu     [pixq               ], m4;  q0
    movdqu     [pixq  +     strideq], m5;  q1
    movdqu     [pixq  + 2 * strideq], m6;  q2
.bypassluma:
    RET

cglobal vvc_h_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
    lea                  src3strideq, [3 * strideq]
    mov                        pix0q, pixq
    sub                        pix0q, src3strideq
    sub                        pix0q, strideq
    movdqu                        m0, [pix0q];               p3
    movdqu                        m1, [pix0q +     strideq]; p2
    movdqu                        m2, [pix0q + 2 * strideq]; p1
    movdqu                        m3, [pix0q + src3strideq]; p0
    movdqu                        m4, [pixq];                q0
    movdqu                        m5, [pixq  +     strideq]; q1
    movdqu                        m6, [pixq  + 2 * strideq]; q2
    movdqu                        m7, [pixq  + src3strideq]; q3
    LUMA_DEBLOCK_BODY             12, h
.store:
    pxor                          m8, m8; zeros reg
    CLIPW                         m1, m8, [pw_pixel_max_12]
    CLIPW                         m2, m8, [pw_pixel_max_12]
    CLIPW                         m3, m8, [pw_pixel_max_12]
    CLIPW                         m4, m8, [pw_pixel_max_12]
    CLIPW                         m5, m8, [pw_pixel_max_12]
    CLIPW                         m6, m8, [pw_pixel_max_12]
    movdqu     [pix0q +     strideq], m1;  p2
    movdqu     [pix0q + 2 * strideq], m2;  p1
    movdqu     [pix0q + src3strideq], m3;  p0
    movdqu     [pixq               ], m4;  q0
    movdqu     [pixq  +     strideq], m5;  q1
    movdqu     [pixq  + 2 * strideq], m6;  q2
.bypassluma:
    RET

%endmacro

INIT_XMM sse2
LOOP_FILTER_LUMA
INIT_XMM ssse3
LOOP_FILTER_LUMA
INIT_XMM avx
LOOP_FILTER_LUMA
%endif
