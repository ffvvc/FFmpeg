;*****************************************************************************
;* SSE2-optimized HEVC deblocking code
;*****************************************************************************
;* Copyright (C) 2013 VTT
;*
;* Authors: Seppo Tomperi <seppo.tomperi@vtt.fi>
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
%include "libavcodec/x86/h26x/h2656_deblock.asm"

%macro LUMA_DEBLOCK_BODY 2
    H2656_LUMA_DEBLOCK_BODY hevc, %1, %2
%endmacro

SECTION_RODATA

cextern pw_1023
%define pw_pixel_max_10 pw_1023
pw_pixel_max_12: times 8 dw ((1 << 12)-1)
pw_m2:           times 8 dw -2
pd_1 :           times 4 dd  1

cextern pw_4
cextern pw_8
cextern pw_4096
cextern pw_m1

SECTION .text
INIT_XMM sse2

ALIGN 16
; input in m0 ... m3 and tcs in r2. Output in m1 and m2
%macro CHROMA_DEBLOCK_BODY 1
    ;tc calculations
    movq             m6, [tcq]; tc0
    punpcklwd        m6, m6
    pshufd           m6, m6, 0xA0; tc0, tc1
%if cpuflag(ssse3)
    psignw           m4, m6, [pw_m1]; -tc0, -tc1
%else
    pmullw           m4, m6, [pw_m1]; -tc0, -tc1
%endif
    ;end tc calculations
%if %1 > 8
    psllw            m4, %1-8; << (BIT_DEPTH - 8)
    psllw            m6, %1-8; << (BIT_DEPTH - 8)
%endif
    H2656_CHROMA_DEBLOCK m1, m2, m0, m1, m2, m3, m4, m6, m5, m7
%endmacro

;-----------------------------------------------------------------------------
; void ff_hevc_v_loop_filter_chroma(uint8_t *_pix, ptrdiff_t _stride, int32_t *tc,
;                                   uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
%macro LOOP_FILTER_CHROMA 0
cglobal hevc_v_loop_filter_chroma_8, 3, 5, 8, pix, stride, tc, pix0, r3stride
    sub            pixq, 2
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8B_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 8
    TRANSPOSE8x4B_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    RET

cglobal hevc_v_loop_filter_chroma_10, 3, 5, 8, pix, stride, tc, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 10
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_10]
    RET

cglobal hevc_v_loop_filter_chroma_12, 3, 5, 8, pix, stride, tc, pix0, r3stride
    sub            pixq, 4
    lea       r3strideq, [3*strideq]
    mov           pix0q, pixq
    add            pixq, r3strideq
    TRANSPOSE4x8W_LOAD  PASS8ROWS(pix0q, pixq, strideq, r3strideq)
    CHROMA_DEBLOCK_BODY 12
    TRANSPOSE8x4W_STORE PASS8ROWS(pix0q, pixq, strideq, r3strideq), [pw_pixel_max_12]
    RET

;-----------------------------------------------------------------------------
; void ff_hevc_h_loop_filter_chroma(uint8_t *_pix, ptrdiff_t _stride, int32_t *tc,
;                                   uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_h_loop_filter_chroma_8, 3, 4, 8, pix, stride, tc, pix0
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

cglobal hevc_h_loop_filter_chroma_10, 3, 4, 8, pix, stride, tc, pix0
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

cglobal hevc_h_loop_filter_chroma_12, 3, 4, 8, pix, stride, tc, pix0
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
%endmacro

INIT_XMM sse2
LOOP_FILTER_CHROMA
INIT_XMM avx
LOOP_FILTER_CHROMA

%if ARCH_X86_64
%macro LOOP_FILTER_LUMA 0
;-----------------------------------------------------------------------------
; void ff_hevc_v_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int beta,
;                                 int32_t *tc, uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_v_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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

cglobal hevc_v_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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

cglobal hevc_v_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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
; void ff_hevc_h_loop_filter_luma(uint8_t *_pix, ptrdiff_t _stride, int beta,
;                                 int32_t *tc, uint8_t *_no_p, uint8_t *_no_q);
;-----------------------------------------------------------------------------
cglobal hevc_h_loop_filter_luma_8, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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

cglobal hevc_h_loop_filter_luma_10, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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

cglobal hevc_h_loop_filter_luma_12, 4, 14, 16, pix, stride, beta, tc, pix0, src3stride
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
