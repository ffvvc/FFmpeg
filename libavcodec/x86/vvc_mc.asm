; /*
; * Provide SIMD MC functions for VVC decoding
; *
; * Copyright (c) 2023 Wu Jianhua <toqsxw@outlook.com>
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

SECTION_RODATA 64

%macro VVC_LUMA_FILTERS 1
p%1_vvc_luma_filters:
%assign %%i 0
%rep 2
                    d%1  0, 0,   0, 64,  0,   0,  0,  0,
                    d%1  0, 1,  -3, 63,  4,  -2,  1,  0,
                    d%1 -1, 2,  -5, 62,  8,  -3,  1,  0,
                    d%1 -1, 3,  -8, 60, 13,  -4,  1,  0,
                    d%1 -1, 4, -10, 58, 17,  -5,  1,  0,
                    d%1 -1, 4, -11, 52, 26,  -8,  3, -1,
                    d%1 -1, 3,  -9, 47, 31, -10,  4, -1,
                    d%1 -1, 4, -11, 45, 34, -10,  4, -1,
%if %%i == 0
                    d%1 -1, 4, -11, 40, 40, -11,  4, -1,
%else
                    d%1  0, 3,   9, 20, 20,   9,  3,  0,
%endif
                    d%1 -1, 4, -10, 34, 45, -11,  4, -1,
                    d%1 -1, 4, -10, 31, 47,  -9,  3, -1,
                    d%1 -1, 3,  -8, 26, 52, -11,  4, -1,
                    d%1  0, 1,  -5, 17, 58, -10,  4, -1,
                    d%1  0, 1,  -4, 13, 60,  -8,  3, -1,
                    d%1  0, 1,  -3,  8, 62,  -5,  2, -1,
                    d%1  0, 1,  -2,  4, 63,  -3,  1,  0,
%assign %%i %%i+1
%endrep

                    ; affine, Table 30
                    d%1 0, 0,   0, 64,  0,   0,  0,  0,
                    d%1 0, 1,  -3, 63,  4,  -2,  1,  0,
                    d%1 0, 1,  -5, 62,  8,  -3,  1,  0,
                    d%1 0, 2,  -8, 60, 13,  -4,  1,  0,
                    d%1 0, 3, -10, 58, 17,  -5,  1,  0,
                    d%1 0, 3, -11, 52, 26,  -8,  2,  0,
                    d%1 0, 2,  -9, 47, 31, -10,  3,  0,
                    d%1 0, 3, -11, 45, 34, -10,  3,  0,
                    d%1 0, 3, -11, 40, 40, -11,  3,  0,
                    d%1 0, 3, -10, 34, 45, -11,  3,  0,
                    d%1 0, 3, -10, 31, 47,  -9,  2,  0,
                    d%1 0, 2,  -8, 26, 52, -11,  3,  0,
                    d%1 0, 1,  -5, 17, 58, -10,  3,  0,
                    d%1 0, 1,  -4, 13, 60,  -8,  2,  0,
                    d%1 0, 1,  -3,  8, 62,  -5,  1,  0,
                    d%1 0, 1,  -2,  4, 63,  -3,  1,  0
%endmacro

VVC_LUMA_FILTERS w
VVC_LUMA_FILTERS b

pw_vvc_iter_shuffle_index dw  0,  1,
                          dw  1,  2,
                          dw  2,  3,
                          dw  3,  4,
                          dw  4,  5,
                          dw  5,  6,
                          dw  6,  7,
                          dw  7,  8,
                          dw  8,  9,
                          dw  9, 10,
                          dw 10, 11,
                          dw 11, 12,
                          dw 12, 13,
                          dw 13, 14,
                          dw 14, 15,
                          dw 15, 16

                          dw  2,  3,
                          dw  3,  4,
                          dw  4,  5,
                          dw  5,  6,
                          dw  6,  7,
                          dw  7,  8,
                          dw  8,  9,
                          dw  9, 10,
                          dw 10, 11,
                          dw 11, 12,
                          dw 12, 13,
                          dw 13, 14,
                          dw 14, 15,
                          dw 15, 16,
                          dw 16, 17,
                          dw 17, 18

                          dw  4,  5,
                          dw  5,  6,
                          dw  6,  7,
                          dw  7,  8,
                          dw  8,  9,
                          dw  9, 10,
                          dw 10, 11,
                          dw 11, 12,
                          dw 12, 13,
                          dw 13, 14,
                          dw 14, 15,
                          dw 15, 16,
                          dw 16, 17,
                          dw 17, 18,
                          dw 18, 19,
                          dw 19, 20

                          dw  6,  7,
                          dw  7,  8,
                          dw  8,  9,
                          dw  9, 10,
                          dw 10, 11,
                          dw 11, 12,
                          dw 12, 13,
                          dw 13, 14,
                          dw 14, 15,
                          dw 15, 16,
                          dw 16, 17,
                          dw 17, 18,
                          dw 18, 19,
                          dw 19, 20,
                          dw 20, 21,
                          dw 21, 22

pw_vvc_iter_shuffle_index_half dw  0,  1,
                               dw  1,  2,
                               dw  2,  3,
                               dw  3,  4,
                               dw  4,  5,
                               dw  5,  6,
                               dw  6,  7,
                               dw  7,  8,
                               dw 16, 17,
                               dw 17, 18,
                               dw 18, 19,
                               dw 19, 20,
                               dw 20, 21,
                               dw 21, 22,
                               dw 22, 23,
                               dw 23, 24,

                               dw  2,  3,
                               dw  3,  4,
                               dw  4,  5,
                               dw  5,  6,
                               dw  6,  7,
                               dw  7,  8,
                               dw  8,  9,
                               dw  9, 10,
                               dw 18, 19,
                               dw 19, 20,
                               dw 20, 21,
                               dw 21, 22,
                               dw 22, 23,
                               dw 23, 24,
                               dw 24, 25,
                               dw 25, 26,

                               dw  4,  5,
                               dw  5,  6,
                               dw  6,  7,
                               dw  7,  8,
                               dw  8,  9,
                               dw  9, 10,
                               dw 10, 11,
                               dw 11, 12,
                               dw 20, 21,
                               dw 21, 22,
                               dw 22, 23,
                               dw 23, 24,
                               dw 24, 25,
                               dw 25, 26,
                               dw 26, 27,
                               dw 27, 28,

                               dw  6,  7,
                               dw  7,  8,
                               dw  8,  9,
                               dw  9, 10,
                               dw 10, 11,
                               dw 11, 12,
                               dw 12, 13,
                               dw 13, 14,
                               dw 22, 23,
                               dw 23, 24,
                               dw 24, 25,
                               dw 25, 26,
                               dw 26, 27,
                               dw 27, 28,
                               dw 28, 29,
                               dw 29, 30


pw_vvc_iter_shuffle_index_quarter times 2 dw  0,  1,  1,  2,  2,  3,  3,  4, 16, 17, 17, 18, 18, 19, 19, 20,
                                  times 2 dw  2,  3,  3,  4,  4,  5,  5,  6, 18, 19, 19, 20, 20, 21, 21, 22,
                                  times 2 dw  4,  5,  5,  6,  6,  7,  7,  8, 20, 21, 21, 22, 22, 23, 23, 24,
                                  times 2 dw  6,  7,  7,  8,  8,  9,  9, 10, 22, 23, 23, 24, 24, 25, 25, 26,

pq_vvc_iter_shuffle_index dq 0, 1, 4, 5, 2, 3, 6, 7

pb_vvc_iter_shuffle_index_w db 0,  1,  2,  3,  2,  3,  4,  5,  4,  5,  6,  7,  6,  7,  8,  9
                            db 4,  5,  6,  7,  6,  7,  8,  9,  8,  9, 10, 11, 10, 11, 12, 13

pb_vvc_iter_shuffle_index_b db  0,  1,  2,  3,  1,  2,  3,  4,  2,  3,  4,  5,  3,  4,  5,  6
                            db  4,  5,  6,  7,  5,  6,  7,  8,  6,  7,  8,  9,  7,  8,  9, 10
                            db  8,  9, 10, 11,  9, 10, 11, 12, 10, 11, 12, 13, 11, 12, 13, 14

SECTION .text

%define MAX_PB_SIZE 128*2

%macro PRE_CAL_INDEX 3
    sal              %1q, 4
    lea              %1q, [%1q, + %2q]
    sal              %1q, %3
%endmacro

%macro LOAD_FILTER 4-6
    lea           r3srcq, [p%1_vvc_luma_filters]
    vpbroadcastd     m%3, [r3srcq + %2q + 0 * 4]
    vpbroadcastd     m%4, [r3srcq + %2q + 1 * 4]
%if %0 == 6
    vpbroadcastd     m%5, [r3srcq + %2q + 2 * 4]
    vpbroadcastd     m%6, [r3srcq + %2q + 3 * 4]
%endif
%endmacro

%macro LOAD_FILTER_16 6
    PRE_CAL_INDEX %1, %2, 4
    LOAD_FILTER    w, %1, %3, %4, %5, %6
%endmacro

%macro LOAD_FILTER_8 4
    PRE_CAL_INDEX %1, %2, 3
    LOAD_FILTER    b, %1, %3, %4
%endmacro

%macro LOAD_SHUFFLE_16 5
    mova             m%2, [%1 + 0 * 64]
    mova             m%3, [%1 + 1 * 64]
    mova             m%4, [%1 + 2 * 64]
    mova             m%5, [%1 + 3 * 64]
%endmacro

%macro H_COMPUTE_8 6
    vpermw      m16,  m0, m%1
    vpermw      m17,  m1, m%1
    vpermw      m18,  m2, m%1
    vpermw      m19,  m3, m%1
    vpxor       m%1, m%1, m%1
    vpxor       m20, m20, m20
    vpdpwssd    m%1, m16, m%2
    vpdpwssd    m20, m17, m%3
    vpdpwssd    m%1, m18, m%4
    vpdpwssd    m20, m19, m%5
    vpaddd      m%1, m%1, m20
    vpsrad      m%1, %6
%endmacro

%macro H_COMPUTE_H8_16 1
H_COMPUTE_8 %1, 4, 5, 6, 7, 2
%endmacro

%macro H_COMPUTE_V8_10 1
H_COMPUTE_8 %1, 24, 25, 26, 27, 6
%endmacro

%macro H_COMPUTE_H4 7
    vpermw       m17,  m0, m%2
    vpermw       m16,  m0, m%1
    vinserti64x4 m16, m16, ym17, 1

    vpermw       m18,  m1, m%2
    vpermw       m17,  m1, m%1
    vinserti64x4 m17, m17, ym18, 1

    vpermw       m19,  m2, m%2
    vpermw       m18,  m2, m%1
    vinserti64x4 m18, m18, ym19, 1

    vpermw       m20,  m3, m%2
    vpermw       m19,  m3, m%1
    vinserti64x4 m19, m19, ym20, 1

    vpxor        m%1,  m%1, m%1
    vpxor        m%2,  m%2, m%2
    vpdpwssd     m%1,  m16, m%3
    vpdpwssd     m%2,  m17, m%4
    vpdpwssd     m%1,  m18, m%5
    vpdpwssd     m%2,  m19, m%6

    vpaddd       m%1, m%1, m%2
    vpsrad       m%1, %7
%endmacro

%macro H_COMPUTE_H4_16 2
H_COMPUTE_H4 %1, %2, 4, 5, 6, 7, 2
%endmacro

%macro H_COMPUTE_V4_16 2
H_COMPUTE_H4 %1, %2, 24, 25, 26, 27, 6
%endmacro

%macro LOOP_END 1
    lea         _srcq, [_srcq + 2 * %1]
    lea          dstq, [ dstq + 2 * %1]
    sub            xq, %1
    jnz  .loop_v%1

    lea        r3srcq, [widthq * 2]
    lea         _srcq, [_srcq + srcstrideq * %1]
    sub         _srcq, r3srcq
    lea          dstq, [dstq + MAX_PB_SIZE * %1]
    sub          dstq, r3srcq
    sub       heightq, %1
    jnz         .loop_h%1
%endmacro

;
; void ff_vvc_put_vvc_luma_hv(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
;                             const int height, const intptr_t mx, const intptr_t my, const int width,
;                             const int hf_idx, const int vf_idx);
;
%macro VVC_PUT_VVC_LUMA_HV_AVX512ICL 1
cglobal vvc_put_vvc_luma_hv_%1, 9, 12, 32, dst, src, srcstride, height, mx, my, width, hf_idx, vf_idx, r3src, _src, x
    LOAD_FILTER_16 hf_idx, mx,  4,  5,  6,  7
    LOAD_FILTER_16 vf_idx, my, 24, 25, 26, 27

    mov _srcq, srcq

    cmp          heightd, 4
    je              .hv4
    cmp           widthd, 4
    je              .hv4

.hv8:
    mova             m23, [pq_vvc_iter_shuffle_index]
    LOAD_SHUFFLE_16 pw_vvc_iter_shuffle_index_half, 0, 1, 2, 3

.loop_h8:
    mov               xq, widthq
.loop_v8:
    mov             srcq, _srcq
    lea           r3srcq, [srcstrideq * 3]
    sub             srcq, 6
    sub             srcq, r3srcq
    movu             ym8, [srcq                 ]
    movu             ym9, [srcq + srcstrideq * 1]
    movu            ym10, [srcq + srcstrideq * 2]
    movu            ym11, [srcq + r3srcq        ]

    movu            ym12, [srcq + srcstrideq * 4]

    lea             srcq, [srcq + r3srcq        ]
    movu            ym13, [srcq + srcstrideq * 2]

    lea             srcq, [srcq + r3srcq        ]
    movu            ym14, [srcq                 ]
    movu            ym15, [srcq + srcstrideq * 1]

    ; only    vinserti64x4: 20318
    ; mov and vinserti64x4: 17377
    vpxor            m22, m22, m22
    movu            ym16, [srcq + srcstrideq * 2]
    movu            ym17, [srcq + r3srcq        ]
    movu            ym18, [srcq + srcstrideq * 4]
    lea             srcq, [srcq + r3srcq        ]
    movu            ym19, [srcq + srcstrideq * 2]
    lea             srcq, [srcq + r3srcq        ]
    movu            ym20, [srcq                 ]
    movu            ym21, [srcq + srcstrideq * 1]
    movu            ym22, [srcq + srcstrideq * 2]
    vinserti64x4      m8,  m8, ym16, 1
    vinserti64x4      m9,  m9, ym17, 1
    vinserti64x4     m10, m10, ym18, 1
    vinserti64x4     m11, m11, ym19, 1
    vinserti64x4     m12, m12, ym20, 1
    vinserti64x4     m13, m13, ym21, 1
    vinserti64x4     m14, m14, ym22, 1

%assign %%i 8
%rep 8
    H_COMPUTE_H8_16 %%i
%assign %%i %%i+1
%endrep

    vpackusdw         m8,  m8, m12
    vpackusdw         m9,  m9, m13
    vpackusdw        m10, m10, m14
    vpackusdw        m11, m11, m15

    vpunpcklwd       m12,  m8, m9
    vpunpckhwd       m13,  m8, m9
    vpunpcklwd       m14, m10, m11
    vpunpckhwd       m15, m10, m11

    vpunpckldq        m8, m12, m14
    vpunpckhdq        m9, m12, m14
    vpunpckldq       m10, m13, m15
    vpunpckhdq       m11, m13, m15

    vpunpcklqdq      m12, m8, m10
    vpunpckhqdq      m13, m8, m10
    vpunpcklqdq      m14, m9, m11
    vpunpckhqdq      m15, m9, m11

    vpermq            m8, m23, m12
    vpermq            m9, m23, m13
    vpermq           m10, m23, m14
    vpermq           m11, m23, m15

%assign %%i 8
%rep 4
    H_COMPUTE_V8_10 %%i
%assign %%i %%i+1
%endrep

    vpackusdw        m8, m8, m10
    vpackusdw        m9, m9, m11

    vpunpcklwd      m10,  m8, m9
    vpunpckhwd      m11,  m8, m9
    vpunpckldq       m8, m10, m11
    vpunpckhdq       m9, m10, m11

    vextracti64x4  ym10, m8, 1
    vextracti64x4  ym11, m9, 1

    vpunpcklqdq    ym12, ym8, ym10
    vpunpckhqdq    ym13, ym8, ym10
    vpunpcklqdq    ym14, ym9, ym11
    vpunpckhqdq    ym15, ym9, ym11

    movu          [dstq + MAX_PB_SIZE * 0], xm12
    movu          [dstq + MAX_PB_SIZE * 1], xm13
    movu          [dstq + MAX_PB_SIZE * 2], xm14
    movu          [dstq + MAX_PB_SIZE * 3], xm15
    vextracti32x4 [dstq + MAX_PB_SIZE * 4], ym12, 1
    vextracti32x4 [dstq + MAX_PB_SIZE * 5], ym13, 1
    vextracti32x4 [dstq + MAX_PB_SIZE * 6], ym14, 1
    vextracti32x4 [dstq + MAX_PB_SIZE * 7], ym15, 1

    LOOP_END 8

    RET

.hv4:
    LOAD_SHUFFLE_16 pw_vvc_iter_shuffle_index_quarter, 0, 1, 2, 3

.loop_h4:
    mov               xq, widthq
.loop_v4:
    mov             srcq, _srcq
    lea           r3srcq, [srcstrideq * 3]
    sub             srcq, 6
    sub             srcq, r3srcq
    movu             ym8, [srcq                 ]
    movu             ym9, [srcq + srcstrideq * 1]
    movu            ym10, [srcq + srcstrideq * 2]
    movu            ym11, [srcq + r3srcq        ]

    movu            ym12, [srcq + srcstrideq * 4]

    lea             srcq, [srcq + r3srcq]
    movu            ym13, [srcq + srcstrideq * 2]

    lea             srcq, [srcq + r3srcq]
    movu            ym14, [srcq                 ]
    movu            ym15, [srcq + srcstrideq * 1]
    vinserti64x4      m8,  m8, ym12, 1
    vinserti64x4      m9,  m9, ym13, 1
    vinserti64x4     m10, m10, ym14, 1
    vinserti64x4     m11, m11, ym15, 1
    vpxor            m13, m13, m13
    movu            ym12, [srcq + srcstrideq * 2]
    movu            ym13, [srcq + r3srcq        ]
    movu            ym14, [srcq + srcstrideq * 4]
    vinserti64x4     m12, m12, ym14, 1

    H_COMPUTE_H4_16    8,  9
    H_COMPUTE_H4_16   10, 11
    H_COMPUTE_H4_16   12, 13

    vpackusdw         m8, m8, m10
    vpmovdw         ym12, m12

    vextracti64x4    ym9, m8, 1
    vpunpcklwd      ym10, ym8, ym9
    vpunpckhwd      ym11, ym8, ym9
    vpunpckldq       ym8, ym10, ym11
    vpunpckhdq       ym9, ym10, ym11

    vextracti32x4   xm13, ym12, 1
    vpunpcklwd      xm10, xm12, xm13
    vpunpckhwd      xm11, xm12, xm13
    vpunpckldq      xm12, xm10, xm11
    vpunpckhdq      xm13, xm10, xm11

    vextracti64x2   xm10, ym8, 1
    vextracti64x2   xm11, ym9, 1
    vinserti32x4     ym8, xm12, 1
    vinserti32x4     ym9, xm13, 1
    vpunpcklqdq     ym14, ym8, ym10
    vpunpckhqdq      ym8, ym8, ym10
    vpunpcklqdq     ym10, ym9, ym11
    vpunpckhqdq      ym9, ym9, ym11

    vinserti64x4      m9, m8, ym9, 1
    vinserti64x4      m8, m14, ym10, 1

    H_COMPUTE_V4_16    8, 9
    vpmovdw          ym8, m8
    vextracti64x2    xm9, ym8, 1
    vpunpcklwd      xm10, xm8, xm9
    vpunpckhwd      xm11, xm8, xm9
    vpunpckldq       xm8, xm10, xm11
    vpunpckhdq       xm9, xm10, xm11

    psrldq          xm10, xm8, 8
    psrldq          xm11, xm9, 8

    movq [dstq + MAX_PB_SIZE * 0], xm8
    movq [dstq + MAX_PB_SIZE * 1], xm10
    movq [dstq + MAX_PB_SIZE * 2], xm9
    movq [dstq + MAX_PB_SIZE * 3], xm11

    LOOP_END 4

    RET
%endmacro

%macro H_COMPUTE_H8_16_LOOP 2
%assign %%i %1
%rep %2
    H_COMPUTE_H8_16 %%i
    %assign %%i %%i+1
%endrep
%endmacro

%macro STORE_LOOP 5
%assign %%i 0
%rep %2
    %assign %%j 0
    %rep %3
        %assign %%k (%1 + %%i * %3 + %%j)
        %4 [dstq + %%i * MAX_PB_SIZE + %%j * mmsize], %5 %+ %%k
    %assign %%j %%j+1
    %endrep
    %assign %%i %%i+1
%endrep
%endmacro

%macro STORE4_LOOP 3
STORE_LOOP %1, %2, %3, movq, xm
%endmacro

%macro STORE8_LOOP 3
STORE_LOOP %1, %2, %3, movu, xm
%endmacro

%macro STORE16_LOOP 3
STORE_LOOP %1, %2, %3, vpmovdw, m
%endmacro

%macro H_LOOP_END 2
    lea             srcq, [srcq +  srcstrideq * %2]
    lea             dstq, [dstq + MAX_PB_SIZE * %2]
    sub          heightq, %2
    jnz .h%1_loop
%endmacro

%macro VVC_PUT_VVC_LUMA_H_AVX512ICL 1
cglobal vvc_put_vvc_luma_h_%1, 9, 12, 32, dst, src, srcstride, height, mx, my, width, hf_idx, vf_idx, r3src, _src, x
    LOAD_FILTER_16 hf_idx, mx, 4, 5, 6, 7

    lea           r3srcq, [srcstrideq * 3]
    sub             srcq, 6

    cmp           widthq, 4
    jne              .h8

.h4:
    LOAD_SHUFFLE_16 pw_vvc_iter_shuffle_index_quarter, 0, 1, 2, 3

.h4_loop:
    movu             ym8, [srcq                 ]
    movu             ym9, [srcq + srcstrideq * 1]
    vinserti64x4      m8, [srcq + srcstrideq * 2], 1
    vinserti64x4      m9, [srcq + r3srcq        ], 1

    H_COMPUTE_H4_16    8, 9
    vpmovdw          ym8, m8
    vextracti64x2    xm9, ym8, 1

    movq     [dstq + MAX_PB_SIZE * 0], xm8
    movq     [dstq + MAX_PB_SIZE * 1], xm9
    psrldq           xm8, xm8, 8
    psrldq           xm9, xm9, 8
    movq     [dstq + MAX_PB_SIZE * 2], xm8
    movq     [dstq + MAX_PB_SIZE * 3], xm9

    H_LOOP_END 4, 4

    RET

.h8:
    cmp           widthq, 8
    jne              .h16
    LOAD_SHUFFLE_16 pw_vvc_iter_shuffle_index_half, 0, 1, 2, 3

.h8_loop:
    movu             ym8, [srcq                 ]
    movu             ym9, [srcq + srcstrideq * 1]
    movu            ym10, [srcq + srcstrideq * 2]
    movu            ym11, [srcq + r3srcq        ]
    vinserti64x4      m8, ym10, 1
    vinserti64x4      m9, ym11, 1

    H_COMPUTE_H8_16_LOOP 8, 2

    vpmovdw          ym8, m8
    vpmovdw          ym9, m9

    movu          [dstq + MAX_PB_SIZE * 0], xm8
    movu          [dstq + MAX_PB_SIZE * 1], xm9
    vextracti64x2 [dstq + MAX_PB_SIZE * 2], ym8, 1
    vextracti64x2 [dstq + MAX_PB_SIZE * 3], ym9, 1

    H_LOOP_END 8, 4

    RET

.h16:
    LOAD_SHUFFLE_16 pw_vvc_iter_shuffle_index, 0, 1, 2, 3

    cmp           widthq, 32
    je            .h32_loop

    cmp           widthq, 64
    je            .h64_loop

    cmp           widthq, 128
    je            .h128_loop

.h16_loop:
    movu              m8, [srcq                 ]
    movu              m9, [srcq + srcstrideq * 1]
    movu             m10, [srcq + srcstrideq * 2]
    movu             m11, [srcq + r3srcq        ]

    H_COMPUTE_H8_16_LOOP 8, 4
    STORE16_LOOP 8, 4, 1
    H_LOOP_END 16, 4

    RET

.h32_loop:
    movu              m8, [srcq                      ]
    movu              m9, [srcq + 32                 ]
    movu             m10, [srcq + srcstrideq * 1     ]
    movu             m11, [srcq + srcstrideq * 1 + 32]

    movu             m12, [srcq + srcstrideq * 2     ]
    movu             m13, [srcq + srcstrideq * 2 + 32]
    movu             m14, [srcq + r3srcq             ]
    movu             m15, [srcq + r3srcq         + 32]

    H_COMPUTE_H8_16_LOOP 8, 8
    STORE16_LOOP 8, 4, 2

    H_LOOP_END 32, 4

    RET

.h64_loop:
    movu              m8, [srcq                       ]
    movu              m9, [srcq + 32                  ]
    movu             m10, [srcq + 64                  ]
    movu             m11, [srcq + 96                  ]
    movu             m12, [srcq + srcstrideq * 1      ]
    movu             m13, [srcq + srcstrideq * 1 + 32 ]
    movu             m14, [srcq + srcstrideq * 1 + 64 ]
    movu             m15, [srcq + srcstrideq * 1 + 96 ]

    H_COMPUTE_H8_16_LOOP 8, 8
    STORE16_LOOP         8, 2, 4

    H_LOOP_END 64, 2

    RET

.h128_loop:
    movu              m8, [srcq      ]
    movu              m9, [srcq +  32]
    movu             m10, [srcq +  64]
    movu             m11, [srcq +  96]
    movu             m12, [srcq + 128]
    movu             m13, [srcq + 160]
    movu             m14, [srcq + 192]
    movu             m15, [srcq + 224]

    H_COMPUTE_H8_16_LOOP 8, 8
    STORE16_LOOP 8, 1, 8

    H_LOOP_END 128, 1

    RET

%endmacro

%macro VVC_PUT_VVC_LUMA_V_AVX512ICL 1
cglobal vvc_put_vvc_luma_v_%1, 9, 12, 32, dst, src, srcstride, height, mx, my, width, hf_idx, vf_idx, r3src, _src, x
    LOAD_FILTER_16 vf_idx, my, 4, 5, 6, 7

    mov _srcq, srcq

    cmp          heightd, 4
    je              .hv4
    cmp           widthd, 4
    je              .hv4

.hv8:
    mova             m23, [pq_vvc_iter_shuffle_index]
    LOAD_SHUFFLE_16 pw_vvc_iter_shuffle_index_half, 0, 1, 2, 3

.loop_h8:
    mov               xq, widthq
.loop_v8:
    mov             srcq, _srcq
    lea           r3srcq, [srcstrideq * 3]
    sub             srcq, r3srcq
    movu             xm8, [srcq                 ]
    movu             xm9, [srcq + srcstrideq * 1]
    movu            xm10, [srcq + srcstrideq * 2]
    movu            xm11, [srcq + r3srcq        ]

    movu            xm12, [srcq + srcstrideq * 4]

    lea             srcq, [srcq + r3srcq        ]
    movu            xm13, [srcq + srcstrideq * 2]

    lea             srcq, [srcq + r3srcq        ]
    movu            xm14, [srcq                 ]
    movu            xm15, [srcq + srcstrideq * 1]

    vpxor            m22, m22, m22
    movu            xm16, [srcq + srcstrideq * 2]
    movu            xm17, [srcq + r3srcq        ]
    movu            xm18, [srcq + srcstrideq * 4]
    lea             srcq, [srcq + r3srcq        ]
    movu            xm19, [srcq + srcstrideq * 2]
    lea             srcq, [srcq + r3srcq        ]
    movu            xm20, [srcq                 ]
    movu            xm21, [srcq + srcstrideq * 1]
    movu            xm22, [srcq + srcstrideq * 2]

    vinserti64x2     ym8,  ym8, xm16, 1
    vinserti64x2     ym9,  ym9, xm17, 1
    vinserti64x2    ym10, ym10, xm18, 1
    vinserti64x2    ym11, ym11, xm19, 1
    vinserti64x2    ym12, ym12, xm20, 1
    vinserti64x2    ym13, ym13, xm21, 1
    vinserti64x2    ym14, ym14, xm22, 1

    vpunpcklwd      ym16,  ym8,  ym9
    vpunpckhwd      ym17,  ym8,  ym9
    vpunpcklwd      ym18, ym10, ym11
    vpunpckhwd      ym19, ym10, ym11
    vpunpcklwd       ym8, ym12, ym13
    vpunpckhwd       ym9, ym12, ym13
    vpunpcklwd      ym10, ym14, ym15
    vpunpckhwd      ym11, ym14, ym15

    vpunpckldq      ym12, ym16, ym18
    vpunpckhdq      ym13, ym16, ym18
    vpunpckldq      ym14, ym17, ym19
    vpunpckhdq      ym15, ym17, ym19
    vpunpckldq      ym16,  ym8, ym10
    vpunpckhdq      ym17,  ym8, ym10
    vpunpckldq      ym18,  ym9, ym11
    vpunpckhdq      ym19,  ym9, ym11

    vpunpcklqdq      ym8, ym12, ym16
    vpunpckhqdq      ym9, ym12, ym16
    vpunpcklqdq     ym10, ym13, ym17
    vpunpckhqdq     ym11, ym13, ym17
    vpunpcklqdq     ym12, ym14, ym18
    vpunpckhqdq     ym13, ym14, ym18
    vpunpcklqdq     ym14, ym15, ym19
    vpunpckhqdq     ym15, ym15, ym19

    vinserti64x4      m8,  m8, ym12, 1
    vinserti64x4      m9,  m9, ym13, 1
    vinserti64x4     m10, m10, ym14, 1
    vinserti64x4     m11, m11, ym15, 1

%assign %%i 8
%rep 4
    H_COMPUTE_H8_16 %%i
%assign %%i %%i+1
%endrep

    vpackusdw        m8, m8, m10
    vpackusdw        m9, m9, m11

    vpunpcklwd      m10,  m8, m9
    vpunpckhwd      m11,  m8, m9
    vpunpckldq       m8, m10, m11
    vpunpckhdq       m9, m10, m11

    vextracti64x4  ym10, m8, 1
    vextracti64x4  ym11, m9, 1

    vpunpcklqdq    ym12, ym8, ym10
    vpunpckhqdq    ym13, ym8, ym10
    vpunpcklqdq    ym14, ym9, ym11
    vpunpckhqdq    ym15, ym9, ym11

    movu          [dstq + MAX_PB_SIZE * 0], xm12
    movu          [dstq + MAX_PB_SIZE * 1], xm13
    movu          [dstq + MAX_PB_SIZE * 2], xm14
    movu          [dstq + MAX_PB_SIZE * 3], xm15
    vextracti32x4 [dstq + MAX_PB_SIZE * 4], ym12, 1
    vextracti32x4 [dstq + MAX_PB_SIZE * 5], ym13, 1
    vextracti32x4 [dstq + MAX_PB_SIZE * 6], ym14, 1
    vextracti32x4 [dstq + MAX_PB_SIZE * 7], ym15, 1

    LOOP_END 8

    RET

.hv4:
    LOAD_SHUFFLE_16 pw_vvc_iter_shuffle_index_quarter, 0, 1, 2, 3

.loop_h4:
    mov               xq, widthq
.loop_v4:
    mov             srcq, _srcq
    lea           r3srcq, [srcstrideq * 3]
    sub             srcq, r3srcq
    vpxor           ym11, ym11, ym11
    movu             xm8, [srcq                 ]
    movu             xm9, [srcq + srcstrideq * 1]
    movu            xm10, [srcq + srcstrideq * 2]
    movu            xm11, [srcq + r3srcq        ]

    movu            xm12, [srcq + srcstrideq * 4]

    lea             srcq, [srcq + r3srcq        ]
    movu            xm13, [srcq + srcstrideq * 2]

    lea             srcq, [srcq + r3srcq        ]
    movu            xm14, [srcq                 ]
    movu            xm15, [srcq + srcstrideq * 1]

    movu            xm16, [srcq + srcstrideq * 2]
    movu            xm17, [srcq + r3srcq        ]
    movu            xm18, [srcq + srcstrideq * 4]
    vinserti64x2     ym8,  ym8, xm16, 1
    vinserti64x2     ym9,  ym9, xm17, 1
    vinserti64x2    ym10, ym10, xm18, 1

    vpunpcklwd       ym8,  ym8,  ym9
    vpunpcklwd      ym10, ym10, ym11
    vpunpcklwd      ym12, ym12, ym13
    vpunpcklwd      ym14, ym14, ym15

    vpunpckldq       ym9,  ym8, ym10
    vpunpckhdq      ym11,  ym8, ym10
    vpunpckldq      ym10, ym12, ym14
    vpunpckhdq      ym12, ym12, ym14

    vpunpcklqdq      ym8,  ym9, ym10
    vpunpckhqdq      ym9,  ym9, ym10
    vpunpcklqdq     ym10, ym11, ym12
    vpunpckhqdq     ym11, ym11, ym12

    vinserti64x4      m8, m8, ym10, 1
    vinserti64x4      m9, m9, ym11, 1

    H_COMPUTE_H4_16    8, 9
    vpmovdw          ym8,   m8
    vextracti64x2    xm9,  ym8, 1
    vpunpcklwd      xm10,  xm8, xm9
    vpunpckhwd      xm11,  xm8, xm9
    vpunpckldq       xm8, xm10, xm11
    vpunpckhdq       xm9, xm10, xm11

    psrldq          xm10, xm8, 8
    psrldq          xm11, xm9, 8

    movq [dstq + MAX_PB_SIZE * 0], xm8
    movq [dstq + MAX_PB_SIZE * 1], xm10
    movq [dstq + MAX_PB_SIZE * 2], xm9
    movq [dstq + MAX_PB_SIZE * 3], xm11

    LOOP_END 4

    RET
%endmacro

%macro H_COMPUTE_16_AVX2 6
    vpshufb              m%4, m%1, m5
    vpshufb              m%1, m4
    vpmaddwd             m%5, m1, m%4
    vpmaddwd             m%1, m0
    vpshufb              m%2, m5
    vshufpd              m%4, m%2, 0x05
    vpaddd               m%1, m%5
    vpmaddwd             m%5, m3, m%2
    vpaddd               m%1, m%5
    vpmaddwd             m%5, m2, m%4
    vpshufb              m%3, m5
    vpmaddwd             m%4, m0
    vpaddd               m%1, m%5
    vpmaddwd             m%5, m1, m%2
    vshufpd              m%2, m%3, 0x05
    vpmaddwd             m%3, m3
    vpmaddwd             m%2, m2
    vpaddd               m%4, m%5
    vpaddd               m%3, m%4
    vpaddd               m%2, m%3
    vpsrad               m%1, %6
    vpsrad               m%2, %6
    vpminuw              m%1, m6
    vpminuw              m%2, m6
    vpackusdw            m%1, m%2
%endmacro

%macro H_COMPUTE_H8_16_AVX2 5
    H_COMPUTE_16_AVX2 %1, %2, %3, %4, %5, 2
%endmacro

%macro H_LOAD_COMPUTE_H8_16 6
    movu                 m%1, [srcq + srcstrideq * 0  +  0]
    vinserti128          m%1, [srcq + srcstrideq * %6 +  0], 1
    movu                xm%3, [srcq + srcstrideq * 0  + 16]
    vinserti128          m%3, [srcq + srcstrideq * %6 + 16], 1
    shufpd               m%2, m%1, m%3, 0x05
    H_COMPUTE_16_AVX2     %1, %2, %3, %4, %5, 2
    lea                 srcq, [srcq + srcstrideq]
%endmacro

%macro H_LOAD_COMPUTE_V8_16 5-6
    vperm2i128           m%3,  m%1,  m%2, 0x31
    vinserti128          m%1,  m%1, xm%2, 1
    shufpd               m%2,  m%1,  m%3, 0x05
%assign %%shift_count 6
%if %0 == 6
%assign %%shift_count %6
%endif
    H_COMPUTE_16_AVX2     %1, %2, %3, %4, %5, %%shift_count
%endmacro

%macro PUSH_MM 1
    mova [rsp + stack_offset], m%1
    %assign stack_offset stack_offset+mmsize
%endmacro

%macro POP_MM 1
    %assign stack_offset stack_offset-mmsize
    mova m%1, [rsp + stack_offset]
%endmacro

%macro VVC_PUT_VVC_LUMA_HV_AVX2 1
cglobal vvc_put_vvc_luma_hv_%1, 9, 12, 16, -0x60, dst, src, srcstride, height, mx, my, width, hf_idx, vf_idx, r3src, _src, x
%assign stack_offset 0

    PRE_CAL_INDEX hf_idx, mx, 4
    PRE_CAL_INDEX vf_idx, my, 4

    vbroadcasti128    m4, [pb_vvc_iter_shuffle_index_w + 0 * 16]
    vbroadcasti128    m5, [pb_vvc_iter_shuffle_index_w + 1 * 16]

    mov           r3srcq, 0x0000ffff
    movq             xm6, r3srcq
    vpbroadcastd      m6, xm6

    lea           r3srcq, [srcstrideq * 3   ]
    neg           r3srcq
    lea            _srcq, [srcq + r3srcq - 6]

    cmp          heightd, 4
    je              .hv4
    cmp           widthd, 4
    je              .hv4

.hv8:
.loop_h8:
    mov               xq, widthq
.loop_v8:
    mov             srcq, _srcq
    LOAD_FILTER        w, hf_idx, 0, 1, 2, 3
    H_LOAD_COMPUTE_H8_16  7,  8,  9, 10, 11, 8
    H_LOAD_COMPUTE_H8_16  8,  9, 10, 11, 12, 8
    H_LOAD_COMPUTE_H8_16  9, 10, 11, 12, 13, 8
    H_LOAD_COMPUTE_H8_16 10, 11, 12, 13, 14, 8
    H_LOAD_COMPUTE_H8_16 11, 12, 13, 14, 15, 8
    PUSH_MM  9
    PUSH_MM 10
    PUSH_MM 11
    H_LOAD_COMPUTE_H8_16 12, 9, 10, 11, 15, 8
    H_LOAD_COMPUTE_H8_16 13, 9, 10, 11, 15, 8

    movu            xm14, [srcq + srcstrideq * 0 +  0]
    movu            xm10, [srcq + srcstrideq * 0 + 16]
    shufpd            m9, m14, m10, 0x05
    H_COMPUTE_H8_16_AVX2 14, 9, 10, 11, 15
    POP_MM 11
    POP_MM 10
    POP_MM 9

    vpunpcklwd        m0,  m7, m8
    vpunpckhwd        m1,  m7, m8
    vpunpcklwd        m2,  m9, m10
    vpunpckhwd        m3,  m9, m10
    vpunpcklwd        m7, m11, m12
    vpunpckhwd        m8, m11, m12
    vpunpcklwd        m9, m13, m14
    vpunpckhwd       m10, m13, m14

    vpunpckldq       m11, m0, m2
    vpunpckhdq       m12, m0, m2
    vpunpckldq       m13, m1, m3
    vpunpckhdq       m14, m1, m3
    vpunpckldq        m0, m7, m9
    vpunpckhdq        m1, m7, m9
    vpunpckldq        m2, m8, m10
    vpunpckhdq        m3, m8, m10

    vpunpcklqdq       m7, m11, m0
    vpunpckhqdq       m8, m11, m0
    vpunpcklqdq       m9, m12, m1
    vpunpckhqdq      m10, m12, m1
    vpunpcklqdq      m11, m13, m2
    vpunpckhqdq      m12, m13, m2
    vpunpcklqdq      m13, m14, m3
    vpunpckhqdq      m14, m14, m3

    PUSH_MM 10
    PUSH_MM 14

    LOAD_FILTER          w, vf_idx, 0, 1, 2, 3
    H_LOAD_COMPUTE_V8_16 7, 11, 10, 14, 15
    H_LOAD_COMPUTE_V8_16 8, 12, 10, 14, 15
    H_LOAD_COMPUTE_V8_16 9, 13, 10, 14, 15
    POP_MM 14
    POP_MM 10
    H_LOAD_COMPUTE_V8_16 10, 14, 11, 12, 15

    vpunpcklwd         m0,  m7, m8
    vpunpckhwd         m1,  m7, m8
    vpunpcklwd         m2,  m9, m10
    vpunpckhwd         m3,  m9, m10

    vpunpckldq        m11,  m0, m2
    vpunpckhdq        m12,  m0, m2
    vpunpckldq        m13,  m1, m3
    vpunpckhdq        m14,  m1, m3

    vextracti128      xm0, m11, 1
    vextracti128      xm1, m12, 1
    vextracti128      xm2, m13, 1
    vextracti128      xm3, m14, 1

    vpunpcklqdq        m7, m11, m0
    vpunpckhqdq        m8, m11, m0
    vpunpcklqdq        m9, m12, m1
    vpunpckhqdq       m10, m12, m1
    vpunpcklqdq       m11, m13, m2
    vpunpckhqdq       m12, m13, m2
    vpunpcklqdq       m13, m14, m3
    vpunpckhqdq       m14, m14, m3

    movu [dstq + MAX_PB_SIZE * 0], xm7
    movu [dstq + MAX_PB_SIZE * 1], xm8
    movu [dstq + MAX_PB_SIZE * 2], xm9
    movu [dstq + MAX_PB_SIZE * 3], xm10
    movu [dstq + MAX_PB_SIZE * 4], xm11
    movu [dstq + MAX_PB_SIZE * 5], xm12
    movu [dstq + MAX_PB_SIZE * 6], xm13
    movu [dstq + MAX_PB_SIZE * 7], xm14

    LOOP_END 8

    RET

.hv4:
.loop_h4:
    mov               xq, widthq
.loop_v4:
    mov             srcq, _srcq
    LOAD_FILTER        w, hf_idx, 0, 1, 2, 3
    H_LOAD_COMPUTE_H8_16  7,  8,  9, 10, 11, 8
    H_LOAD_COMPUTE_H8_16  8,  9, 10, 11, 12, 8
    H_LOAD_COMPUTE_H8_16  9, 10, 11, 12, 13, 8

    vpunpcklwd m7, m7, m8

    movu            xm10, [srcq + srcstrideq * 0 +  0]
    movu            xm12, [srcq + srcstrideq * 0 + 16]
    shufpd           m11, m10, m12, 0x05
    H_COMPUTE_H8_16_AVX2 10, 11, 12, 13, 14

    lea             srcq, [srcq + srcstrideq         ]
    H_LOAD_COMPUTE_H8_16  8, 11, 12, 13, 14, 2
    H_LOAD_COMPUTE_H8_16 11, 12, 13, 14, 15, 2

    vpunpcklwd        m9, m9, m10
    vpunpcklwd        m8, m8, m11

    vpunpckldq       m10, m7, m9
    vpunpckhdq       m11, m7, m9

    vextracti128     xm7, m8, 1
    vpunpckldq      xm12, xm8, xm7
    vpunpckhdq      xm13, xm8, xm7

    vpunpcklqdq      m7, m10, m12
    vpunpckhqdq      m8, m10, m12
    vpunpcklqdq      m9, m11, m13
    vpunpckhqdq     m10, m11, m13

    LOAD_FILTER       w, vf_idx, 0, 1, 2, 3
    H_LOAD_COMPUTE_V8_16 7,  9, 11, 12, 13
    H_LOAD_COMPUTE_V8_16 8, 10, 11, 12, 13

    vpunpcklwd       m7,  m7, m8
    vextracti128    xm8,  m7, 1
    vpunpckldq      xm9, xm7, xm8
    vpunpckhdq     xm10, xm7, xm8

    psrldq          xm7,  xm9, 8
    psrldq          xm8, xm10, 8

    movq [dstq + MAX_PB_SIZE * 0], xm9
    movq [dstq + MAX_PB_SIZE * 1], xm7
    movq [dstq + MAX_PB_SIZE * 2], xm10
    movq [dstq + MAX_PB_SIZE * 3], xm8

    LOOP_END 4

    RET
%endmacro

%macro VVC_PUT_VVC_LUMA_H_AVX2 1
cglobal vvc_put_vvc_luma_h_%1, 9, 10, 12, dst, src, srcstride, height, mx, my, width, hf_idx, vf_idx, r3src
    LOAD_FILTER_16 hf_idx, mx, 0, 1, 2, 3

    vbroadcasti128       m4, [pb_vvc_iter_shuffle_index_w + 0 * 16]
    vbroadcasti128       m5, [pb_vvc_iter_shuffle_index_w + 1 * 16]

    mov              r3srcq, 0x0000ffff
    movq                xm6, r3srcq
    vpbroadcastd         m6, xm6

    sub             srcq, 6

    cmp           widthq, 4
    jne              .h8

.h4:
.h4_loop:
    movu                xm7, [srcq + srcstrideq * 0 +  0]
    vinserti128          m7, [srcq + srcstrideq * 1 +  0], 1
    movu                xm9, [srcq + srcstrideq * 0 + 16]
    vinserti128          m9, [srcq + srcstrideq * 1 + 16], 1
    shufpd               m8, m7, m9, 0x05

    H_COMPUTE_H8_16_AVX2 7, 8, 9, 10, 11

    movq [dstq + MAX_PB_SIZE * 0], xm7
    vextracti128      xm7, ym7, 1
    movq [dstq + MAX_PB_SIZE * 1], xm7

    H_LOOP_END 4, 2

    RET

.h8:
    cmp           widthq, 8
    jne              .h16

.h8_loop:
    movu                xm7, [srcq + srcstrideq * 0 +  0]
    vinserti128          m7, [srcq + srcstrideq * 1 +  0], 1
    movu                xm9, [srcq + srcstrideq * 0 + 16]
    vinserti128          m9, [srcq + srcstrideq * 1 + 16], 1
    shufpd               m8, m7, m9, 0x05

    H_COMPUTE_H8_16_AVX2 7, 8, 9, 10, 11

    movu              [dstq + MAX_PB_SIZE * 0], xm7
    vextracti128      [dstq + MAX_PB_SIZE * 1], ym7, 1

    H_LOOP_END 8, 2

    RET

.h16:
.h16_loop:
    mov             r3srcq, widthq
.h16_loop_w:
    movu                m7, [srcq + r3srcq * 2 - 32]
    movu                m8, [srcq + r3srcq * 2 - 24]
    movu                m9, [srcq + r3srcq * 2 - 16]

    H_COMPUTE_H8_16_AVX2 7, 8, 9, 10, 11
    movu [dstq + r3srcq * 2 - 32], m7
    sub             r3srcq, 16
    jg                  .h16_loop_w
    H_LOOP_END 16, 1

    RET
%endmacro

%macro VVC_PUT_VVC_LUMA_V_AVX2 1
cglobal vvc_put_vvc_luma_v_%1, 9, 12, 16, -0x40, dst, src, srcstride, height, mx, my, width, hf_idx, vf_idx, r3src, _src, x
%assign stack_offset 0

    PRE_CAL_INDEX hf_idx, mx, 4
    PRE_CAL_INDEX vf_idx, my, 4

    vbroadcasti128    m4, [pb_vvc_iter_shuffle_index_w + 0 * 16]
    vbroadcasti128    m5, [pb_vvc_iter_shuffle_index_w + 1 * 16]

    mov           r3srcq, 0x0000ffff
    movq             xm6, r3srcq
    vpbroadcastd      m6, xm6

    lea           r3srcq, [srcstrideq * 3]
    neg           r3srcq
    lea            _srcq, [srcq + r3srcq ]

    cmp          heightd, 4
    je              .hv4
    cmp           widthd, 4
    je              .hv4

.hv8:
.loop_h8:
    mov               xq, widthq
.loop_v8:
    mov             srcq, _srcq
    lea           r3srcq, [srcstrideq * 3       ]
    movu             xm7, [srcq                 ]
    movu             xm8, [srcq + srcstrideq * 1]
    movu             xm9, [srcq + srcstrideq * 2]
    movu            xm10, [srcq + r3srcq        ]

    movu            xm11, [srcq + srcstrideq * 4]

    lea             srcq, [srcq + r3srcq        ]
    movu            xm12, [srcq + srcstrideq * 2]

    lea             srcq, [srcq + r3srcq        ]
    movu            xm13, [srcq                 ]
    movu            xm14, [srcq + srcstrideq * 1]

    vinserti128       m7, [srcq + srcstrideq * 2], 1
    vinserti128       m8, [srcq + r3srcq        ], 1
    vinserti128       m9, [srcq + srcstrideq * 4], 1
    lea             srcq, [srcq + r3srcq        ]
    vinserti128      m10, [srcq + srcstrideq * 2], 1
    lea             srcq, [srcq + r3srcq        ]
    vinserti128      m11, [srcq                 ], 1
    vinserti128      m12, [srcq + srcstrideq * 1], 1
    vinserti128      m13, [srcq + srcstrideq * 2], 1

    vpunpcklwd        m0,  m7, m8
    vpunpckhwd        m1,  m7, m8
    vpunpcklwd        m2,  m9, m10
    vpunpckhwd        m3,  m9, m10
    vpunpcklwd        m7, m11, m12
    vpunpckhwd        m8, m11, m12
    vpunpcklwd        m9, m13, m14
    vpunpckhwd       m10, m13, m14

    vpunpckldq       m11, m0, m2
    vpunpckhdq       m12, m0, m2
    vpunpckldq       m13, m1, m3
    vpunpckhdq       m14, m1, m3
    vpunpckldq        m0, m7, m9
    vpunpckhdq        m1, m7, m9
    vpunpckldq        m2, m8, m10
    vpunpckhdq        m3, m8, m10

    vpunpcklqdq       m7, m11, m0
    vpunpckhqdq       m8, m11, m0
    vpunpcklqdq       m9, m12, m1
    vpunpckhqdq      m10, m12, m1
    vpunpcklqdq      m11, m13, m2
    vpunpckhqdq      m12, m13, m2
    vpunpcklqdq      m13, m14, m3
    vpunpckhqdq      m14, m14, m3

    PUSH_MM 10
    PUSH_MM 14

    LOAD_FILTER         w, vf_idx, 0, 1, 2, 3
    H_LOAD_COMPUTE_V8_16  7, 11, 10, 14, 15, 2
    H_LOAD_COMPUTE_V8_16  8, 12, 10, 14, 15, 2
    H_LOAD_COMPUTE_V8_16  9, 13, 10, 14, 15, 2
    POP_MM 14
    POP_MM 10
    H_LOAD_COMPUTE_V8_16 10, 14, 11, 12, 15, 2

    vpunpcklwd         m0,  m7, m8
    vpunpckhwd         m1,  m7, m8
    vpunpcklwd         m2,  m9, m10
    vpunpckhwd         m3,  m9, m10

    vpunpckldq        m11,  m0, m2
    vpunpckhdq        m12,  m0, m2
    vpunpckldq        m13,  m1, m3
    vpunpckhdq        m14,  m1, m3

    vextracti128      xm0, m11, 1
    vextracti128      xm1, m12, 1
    vextracti128      xm2, m13, 1
    vextracti128      xm3, m14, 1

    vpunpcklqdq        m7, m11, m0
    vpunpckhqdq        m8, m11, m0
    vpunpcklqdq        m9, m12, m1
    vpunpckhqdq       m10, m12, m1
    vpunpcklqdq       m11, m13, m2
    vpunpckhqdq       m12, m13, m2
    vpunpcklqdq       m13, m14, m3
    vpunpckhqdq       m14, m14, m3

    movu [dstq + MAX_PB_SIZE * 0], xm7
    movu [dstq + MAX_PB_SIZE * 1], xm8
    movu [dstq + MAX_PB_SIZE * 2], xm9
    movu [dstq + MAX_PB_SIZE * 3], xm10
    movu [dstq + MAX_PB_SIZE * 4], xm11
    movu [dstq + MAX_PB_SIZE * 5], xm12
    movu [dstq + MAX_PB_SIZE * 6], xm13
    movu [dstq + MAX_PB_SIZE * 7], xm14

    LOOP_END 8

    RET

.hv4:
.loop_h4:
    mov               xq, widthq
.loop_v4:
    mov             srcq, _srcq
    lea           r3srcq, [srcstrideq * 3       ]
    movu             xm7, [srcq                 ]
    movu             xm8, [srcq + srcstrideq * 1]
    movu             xm9, [srcq + srcstrideq * 2]
    movu            xm10, [srcq + r3srcq        ]

    movu            xm11, [srcq + srcstrideq * 4]

    lea             srcq, [srcq + r3srcq        ]
    movu            xm12, [srcq + srcstrideq * 2]

    lea             srcq, [srcq + r3srcq        ]
    vinserti128      m11, [srcq                 ], 1
    vinserti128      m12, [srcq + srcstrideq * 1], 1

    vinserti128       m7, [srcq + srcstrideq * 2], 1
    vinserti128       m8, [srcq + r3srcq        ], 1
    vinserti128       m9, [srcq + srcstrideq * 4], 1

    vpunpcklwd        m7,  m7, m8
    vpunpcklwd        m9,  m9, m10
    vpunpcklwd        m8, m11, m12

    vpunpckldq       m10, m7, m9
    vpunpckhdq       m11, m7, m9

    vextracti128     xm7, m8, 1
    vpunpckldq      xm12, xm8, xm7
    vpunpckhdq      xm13, xm8, xm7

    vpunpcklqdq      m7, m10, m12
    vpunpckhqdq      m8, m10, m12
    vpunpcklqdq      m9, m11, m13
    vpunpckhqdq     m10, m11, m13

    LOAD_FILTER       w, vf_idx, 0, 1, 2, 3
    H_LOAD_COMPUTE_V8_16 7,  9, 11, 12, 13, 2
    H_LOAD_COMPUTE_V8_16 8, 10, 11, 12, 13, 2

    vpunpcklwd       m7,  m7, m8
    vextracti128    xm8,  m7, 1
    vpunpckldq      xm9, xm7, xm8
    vpunpckhdq     xm10, xm7, xm8

    psrldq          xm7,  xm9, 8
    psrldq          xm8, xm10, 8

    movq [dstq + MAX_PB_SIZE * 0], xm9
    movq [dstq + MAX_PB_SIZE * 1], xm7
    movq [dstq + MAX_PB_SIZE * 2], xm10
    movq [dstq + MAX_PB_SIZE * 3], xm8

    LOOP_END 4

    RET
%endmacro

%macro H_COMPUTE_H4_8_AVX2 2
    pshufb               m%2, m%1, m3
    pshufb               m%1, m%1, m2
    pmaddubsw            m%1, m0
    pmaddubsw            m%2, m1
    paddw                m%1, m%2
    phaddw               m%1, m%1
    vextracti128        xm%2, m%1, 1
%endmacro

%macro H_COMPUTE_H8_8_AVX2 4
    pshufb               m%2, m%1, m3
    pshufb               m%3, m%1, m4
    pshufb               m%1, m%1, m2
    pmaddubsw            m%1, m0
    pmaddubsw            m%4, m%2, m1
    pmaddubsw            m%2, m0
    pmaddubsw            m%3, m1
    paddw                m%1, m%4
    paddw                m%2, m%3
    phaddw               m%1, m%2
%endmacro

%macro VVC_PUT_VVC_LUMA_H_8_AVX2 1
cglobal vvc_put_vvc_luma_h_%1, 9, 10, 10, dst, src, srcstride, height, mx, my, width, hf_idx, vf_idx, r3src
    LOAD_FILTER_%1 hf_idx, mx, 0, 1

    vbroadcasti128       m2, [pb_vvc_iter_shuffle_index_b + 0 * 16]
    vbroadcasti128       m3, [pb_vvc_iter_shuffle_index_b + 1 * 16]

    lea                srcq, [srcq - 3      ]
    lea              r3srcq, [srcstrideq * 3]

    cmp              widthq, 4
    jne                 .h8

.h4:
.h4_loop:
    movu                 xm5, [srcq + srcstrideq * 0]
    vinserti128           m5, [srcq + srcstrideq * 1], 1
    movu                 xm7, [srcq + srcstrideq * 2]
    vinserti128           m7, [srcq + r3srcq        ], 1

    H_COMPUTE_H4_8_AVX2    5, 6
    H_COMPUTE_H4_8_AVX2    7, 8

    STORE4_LOOP  5, 4, 1
    H_LOOP_END   4, 4

    RET

.h8:
    vbroadcasti128       m4, [pb_vvc_iter_shuffle_index_b + 2 * 16]
    cmp              widthq, 8
    jne                 .h16

.h8_loop:
    movu                xm5, [srcq + srcstrideq * 0]
    vinserti128          m5, [srcq + srcstrideq * 1], 1
    movu                xm7, [srcq + srcstrideq * 2]
    vinserti128          m7, [srcq + r3srcq        ], 1

    H_COMPUTE_H8_8_AVX2   5, 6, 8, 9
    H_COMPUTE_H8_8_AVX2   7, 6, 8, 9

    vextracti128        xm6, m5, 1
    vextracti128        xm8, m7, 1

    STORE8_LOOP  5, 4, 1
    H_LOOP_END   8, 4

    RET

.h16:
.h16_loop:
    mov                 mxq, widthq
.h16_loop_w:
    lea              r3srcq, [srcq + mxq - 16            ]
    movu                xm5, [r3srcq + srcstrideq * 0 + 0]
    vinserti128          m5, [r3srcq + srcstrideq * 0 + 8], 1
    movu                xm6, [r3srcq + srcstrideq * 1 + 0]
    vinserti128          m6, [r3srcq + srcstrideq * 1 + 8], 1

    H_COMPUTE_H8_8_AVX2   5, 7, 8, 9
    H_COMPUTE_H8_8_AVX2   6, 7, 8, 9

    movu [dstq + mxq * 2 - 32 + MAX_PB_SIZE * 0], m5
    movu [dstq + mxq * 2 - 32 + MAX_PB_SIZE * 1], m6

    sub                 mxq, 16
    jg                  .h16_loop_w

    H_LOOP_END  16, 2

    RET
%endmacro

%if ARCH_X86_64
%if HAVE_AVX512ICL_EXTERNAL

INIT_ZMM avx512icl
VVC_PUT_VVC_LUMA_HV_AVX512ICL 16

VVC_PUT_VVC_LUMA_H_AVX512ICL 16

VVC_PUT_VVC_LUMA_V_AVX512ICL 16

%endif

%if HAVE_AVX2_EXTERNAL
INIT_YMM avx2
VVC_PUT_VVC_LUMA_HV_AVX2 16

VVC_PUT_VVC_LUMA_H_AVX2 16

VVC_PUT_VVC_LUMA_V_AVX2 16

VVC_PUT_VVC_LUMA_H_8_AVX2 8
%endif

%endif
