;*****************************************************************************
;* AVX2-optimized VVC deblocking code
;*****************************************************************************
;* Copyright (C) 2013 VTT
;* Copyright (C) 2023 Shaun Loo
;*
;* Authors: Seppo Tomperi <seppo.tomperi@vtt.fi>
;*          Shaun Loo <shaunloo10@gmail.com>
;*
;* This file is part of FFmpeg.
;*
;* FFmpeg is free software; you can redistribute it and/or
;* modify it under the terms of the GNU Lesser General Public
;* License as published by the Free Software Foundation; either
;* version 2.1 of the License, or (at your option) any later version.
;*
;* FFmpeg is distributed in the hope that it will be useful,
;* but WITHOUT ANY WARRANTY; without even the implied warranty of
;* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
;* Lesser General Public License for more details.
;*
;* You should have received a copy of the GNU Lesser General Public
;* License along with FFmpeg; if not, write to the Free Software
;* Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

cextern pw_1023
%define pw_pixel_max_10 pw_1023
pw_pixel_max_12: times 8 dw ((1 << 12)-1)
pw_m2:           times 8 dw -2
pd_1 :           times 4 dd  1
pw_2 :           times 8 dw  2
pd_2 :           times 4 dd  2

cextern pw_4
cextern pw_8
cextern pw_m1

SECTION .text
INIT_XMM avx2

; in: 8 rows of 4 bytes in %0..%8
; out: 4 rows of 8 words in m0..m3
%macro TRANSPOSE4x8B_LOAD 8
    movd             m0, %1
    movd             m2, %2
    movd             m1, %3
    movd             m3, %4

    punpcklbw        m0, m2
    punpcklbw        m1, m3
    punpcklwd        m0, m1

    movd             m4, %5
    movd             m6, %6
    movd             m5, %7
    movd             m3, %8

    punpcklbw        m4, m6
    punpcklbw        m5, m3
    punpcklwd        m4, m5

    punpckhdq        m2, m0, m4
    punpckldq        m0, m4

    pxor             m5, m5
    punpckhbw        m1, m0, m5
    punpcklbw        m0, m5
    punpckhbw        m3, m2, m5
    punpcklbw        m2, m5
%endmacro

; in: 4 rows of 8 words in m0..m3
; out: 8 rows of 4 bytes in %1..%8
%macro TRANSPOSE8x4B_STORE 8
    packuswb         m0, m2
    packuswb         m1, m3
    SBUTTERFLY bw, 0, 1, 2
    SBUTTERFLY wd, 0, 1, 2

    movd             %1, m0
    pshufd           m0, m0, 0x39
    movd             %2, m0
    pshufd           m0, m0, 0x39
    movd             %3, m0
    pshufd           m0, m0, 0x39
    movd             %4, m0

    movd             %5, m1
    pshufd           m1, m1, 0x39
    movd             %6, m1
    pshufd           m1, m1, 0x39
    movd             %7, m1
    pshufd           m1, m1, 0x39
    movd             %8, m1
%endmacro

; in: 8 rows of 4 words in %4..%11
; out: 4 rows of 8 words in m0..m3
%macro TRANSPOSE4x8W_LOAD 8
    movq             m0, %1
    movq             m2, %2
    movq             m1, %3
    movq             m3, %4

    punpcklwd        m0, m2
    punpcklwd        m1, m3
    punpckhdq        m2, m0, m1
    punpckldq        m0, m1

    movq             m4, %5
    movq             m6, %6
    movq             m5, %7
    movq             m3, %8

    punpcklwd        m4, m6
    punpcklwd        m5, m3
    punpckhdq        m6, m4, m5
    punpckldq        m4, m5

    punpckhqdq       m1, m0, m4
    punpcklqdq       m0, m4
    punpckhqdq       m3, m2, m6
    punpcklqdq       m2, m6

%endmacro

; in: 4 rows of 8 words in m0..m3
; out: 8 rows of 4 words in %1..%8
%macro TRANSPOSE8x4W_STORE 9
    TRANSPOSE4x4W     0, 1, 2, 3, 4

    pxor             m5, m5; zeros reg
    CLIPW            m0, m5, %9
    CLIPW            m1, m5, %9
    CLIPW            m2, m5, %9
    CLIPW            m3, m5, %9

    movq             %1, m0
    movhps           %2, m0
    movq             %3, m1
    movhps           %4, m1
    movq             %5, m2
    movhps           %6, m2
    movq             %7, m3
    movhps           %8, m3
%endmacro

; in: 8 rows of 8 bytes in %1..%8
; out: 8 rows of 8 words in m0..m7
%macro TRANSPOSE8x8B_LOAD 8
    movq             m7, %1
    movq             m2, %2
    movq             m1, %3
    movq             m3, %4

    punpcklbw        m7, m2
    punpcklbw        m1, m3
    punpcklwd        m3, m7, m1
    punpckhwd        m7, m1

    movq             m4, %5
    movq             m6, %6
    movq             m5, %7
    movq            m15, %8

    punpcklbw        m4, m6
    punpcklbw        m5, m15
    punpcklwd        m9, m4, m5
    punpckhwd        m4, m5

    punpckldq        m1, m3, m9;  0, 1
    punpckhdq        m3, m9;  2, 3

    punpckldq        m5, m7, m4;  4, 5
    punpckhdq        m7, m4;  6, 7

    pxor            m13, m13

    punpcklbw        m0, m1, m13; 0 in 16 bit
    punpckhbw        m1, m13; 1 in 16 bit

    punpcklbw        m2, m3, m13; 2
    punpckhbw        m3, m13; 3

    punpcklbw        m4, m5, m13; 4
    punpckhbw        m5, m13; 5

    punpcklbw        m6, m7, m13; 6
    punpckhbw        m7, m13; 7
%endmacro

; in: 8 rows of 8 words in m0..m8
; out: 8 rows of 8 bytes in %1..%8
%macro TRANSPOSE8x8B_STORE 8
    packuswb         m0, m4
    packuswb         m1, m5
    packuswb         m2, m6
    packuswb         m3, m7
    TRANSPOSE2x4x4B   0, 1, 2, 3, 4

    movq             %1, m0
    movhps           %2, m0
    movq             %3, m1
    movhps           %4, m1
    movq             %5, m2
    movhps           %6, m2
    movq             %7, m3
    movhps           %8, m3
%endmacro

; in: 8 rows of 8 words in %1..%8
; out: 8 rows of 8 words in m0..m7
%macro TRANSPOSE8x8W_LOAD 8
    movdqu           m0, %1
    movdqu           m1, %2
    movdqu           m2, %3
    movdqu           m3, %4
    movdqu           m4, %5
    movdqu           m5, %6
    movdqu           m6, %7
    movdqu           m7, %8
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8
%endmacro

; in: 8 rows of 8 words in m0..m8
; out: 8 rows of 8 words in %1..%8
%macro TRANSPOSE8x8W_STORE 9
    TRANSPOSE8x8W     0, 1, 2, 3, 4, 5, 6, 7, 8

    pxor             m8, m8
    CLIPW            m0, m8, %9
    CLIPW            m1, m8, %9
    CLIPW            m2, m8, %9
    CLIPW            m3, m8, %9
    CLIPW            m4, m8, %9
    CLIPW            m5, m8, %9
    CLIPW            m6, m8, %9
    CLIPW            m7, m8, %9

    movdqu           %1, m0
    movdqu           %2, m1
    movdqu           %3, m2
    movdqu           %4, m3
    movdqu           %5, m4
    movdqu           %6, m5
    movdqu           %7, m6
    movdqu           %8, m7
%endmacro

; in: %2 clobbered
; out: %1
; mask in m11
; clobbers m10
%macro MASKED_COPY 2
    pand             %2, m11 ; and mask
    pandn           m10, m11, %1; and -mask
    por              %2, m10
    mova             %1, %2
%endmacro

; in: %2 clobbered
; out: %1
; mask in %3, will be clobbered
%macro MASKED_COPY2 3
    pand             %2, %3 ; and mask
    pandn            %3, %1; and -mask
    por              %2, %3
    mova             %1, %2
%endmacro

ALIGN 16
; input in m0 ... m3 and tcs in r2. Output in m1 and m2
%macro CHROMA_DEBLOCK_BODY 1
    psubw            m4, m2, m1; q0 - p0
    psubw            m5, m0, m3; p1 - q1
    psllw            m4, 2; << 2
    paddw            m5, m4;

    ;tc calculations
    movq             m6, tcq; tc
    punpcklwd        m6, m6
    pshufd           m6, m6, 0xA0
%if %1 == 8
    paddw            m6, [pw_2]
    psraw            m6, 2
%elif %1 == 12
    psllw            m6, 2
%endif
    psignw           m4, m6, [pw_m1]
    ;end tc calculations

    paddw            m5, [pw_4]; +4
    psraw            m5, 3; >> 3

    pmaxsw           m5, m4
    pminsw           m5, m6
    paddw            m1, m5; p0 + delta0
    psubw            m2, m5; q0 - delta0
%endmacro



%macro LOOP_FILTER_CHROMA 0

; void ff_vvc_h_loop_filter_chroma_<depth>(uint8_t *pix, ptrdiff_t stride, 
; int beta, int32_t tc, uint8_t no_p, uint8_t no_q, int shift, 
; int max_len_p, int max_len_q)

cglobal vvc_h_loop_filter_chroma_8, 5, 6, 7, pix, stride, beta, tc, shift, pix0
    mov           pix0q, pixq
    sub           pix0q, strideq
    sub           pix0q, strideq
    movq             m0, [pix0q];    p1
    movq             m1, [pix0q+strideq]; p0
    movq             m2, [pixq];    q0
    movq             m3, [pixq+strideq]; q1
    pxor             m5, m5; zeros reg
    punpcklbw        m0, m5
    punpcklbw        m1, m5
    punpcklbw        m2, m5
    punpcklbw        m3, m5
    CHROMA_DEBLOCK_BODY  8
    packuswb         m1, m2
    movh[pix0q+strideq], m1
    movhps       [pixq], m1
    RET

cglobal vvc_h_loop_filter_chroma_10, 5, 6, 7, pix, stride, beta, tc, shift, pix0
    mov          pix0q, pixq
    sub          pix0q, strideq
    sub          pix0q, strideq
    movu            m0, [pix0q];    p1
    movu            m1, [pix0q+strideq]; p0
    movu            m2, [pixq];    q0
    movu            m3, [pixq+strideq]; q1
    CHROMA_DEBLOCK_BODY 10
    pxor            m5, m5; zeros reg
    CLIPW           m1, m5, [pw_pixel_max_10]
    CLIPW           m2, m5, [pw_pixel_max_10]
    movu [pix0q+strideq], m1
    movu        [pixq], m2
    RET

cglobal vvc_h_loop_filter_chroma_12, 4, 5, 7, pix, stride, beta, tc, pix0
    mov          pix0q, pixq
    sub          pix0q, strideq
    sub          pix0q, strideq
    movu            m0, [pix0q];    p1
    movu            m1, [pix0q+strideq]; p0
    movu            m2, [pixq];    q0
    movu            m3, [pixq+strideq]; q1
    CHROMA_DEBLOCK_BODY 12
    pxor            m5, m5; zeros reg
    CLIPW           m1, m5, [pw_pixel_max_12]
    CLIPW           m2, m5, [pw_pixel_max_12]
    movu [pix0q+strideq], m1
    movu        [pixq], m2
    RET

; void ff_vvc_v_loop_filter_chroma_<depth>(uint8_t *pix, ptrdiff_t stride, 
; int beta, int32_t tc, uint8_t no_p, uint8_t no_q, int shift, 
; int max_len_p, int max_len_q)

cglobal vvc_v_loop_filter_chroma_8, 5, 7, 7, pix, stride, beta, tc, shift, pix0, r3stride
    sub            pixq, 2
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq   
    TRANSPOSE4x8B_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 8
    TRANSPOSE8x4B_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    RET

cglobal vvc_v_loop_filter_chroma_10, 5, 7, 7, pix, stride, beta, tc, shift, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 10
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_10]
    RET
    
cglobal vvc_v_loop_filter_chroma_12, 4, 6, 7, pix, stride, beta, tc, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 12
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_12]
    RET

%endmacro

INIT_XMM avx2
LOOP_FILTER_CHROMA
