; /*
; * Provide SSE luma and chroma mc functions for HEVC/VVC decoding
; * Copyright (c) 2013 Pierre-Edouard LEPERE
; * Copyright (c) 2023 Nuo Mi
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

%macro SIMPLE_LOAD 4    ;width, bitd, tab, r1
%if %1 == 2 || (%2 == 8 && %1 <= 4)
    movd              %4, [%3]                                               ; load data from source
%elif %1 == 4 || (%2 == 8 && %1 <= 8)
    movq              %4, [%3]                                               ; load data from source
%elif notcpuflag(avx)
    movu              %4, [%3]                                               ; load data from source
%elif %1 <= 8 || (%2 == 8 && %1 <= 16)
    movdqu           %4, [%3]
%else
    movu              %4, [%3]
%endif
%endmacro

%macro MC_8TAP_FILTER 2 ;bitdepth, filter
    vpbroadcastw      m12, [%2q + 0 * 2]  ; coeff 0, 1
    vpbroadcastw      m13, [%2q + 1 * 2]  ; coeff 2, 3
    vpbroadcastw      m14, [%2q + 2 * 2]  ; coeff 4, 5
    vpbroadcastw      m15, [%2q + 3 * 2]  ; coeff 6, 7
%if %1 != 8
    pmovsxbw          m12, xm12
    pmovsxbw          m13, xm13
    pmovsxbw          m14, xm14
    pmovsxbw          m15, xm15
%endif
%endmacro

%macro MC_8TAP_H_LOAD 4
%assign %%stride (%1+7)/8
%if %1 == 8
%if %3 <= 4
%define %%load movd
%elif %3 == 8
%define %%load movq
%else
%define %%load movu
%endif
%else
%if %3 == 2
%define %%load movd
%elif %3 == 4
%define %%load movq
%else
%define %%load movu
%endif
%endif
    %%load            m0, [%2-3*%%stride]        ;load data from source
    %%load            m1, [%2-2*%%stride]
    %%load            m2, [%2-%%stride  ]
    %%load            m3, [%2           ]
    %%load            m4, [%2+%%stride  ]
    %%load            m5, [%2+2*%%stride]
    %%load            m6, [%2+3*%%stride]
    %%load            m7, [%2+4*%%stride]

%if %1 == 8
%if %3 > 8
    SBUTTERFLY        wd, 0, 1, %4
    SBUTTERFLY        wd, 2, 3, %4
    SBUTTERFLY        wd, 4, 5, %4
    SBUTTERFLY        wd, 6, 7, %4
%else
    punpcklbw         m0, m1
    punpcklbw         m2, m3
    punpcklbw         m4, m5
    punpcklbw         m6, m7
%endif
%else
%if %3 > 4
    SBUTTERFLY        dq, 0, 1, %4
    SBUTTERFLY        dq, 2, 3, %4
    SBUTTERFLY        dq, 4, 5, %4
    SBUTTERFLY        dq, 6, 7, %4
%else
    punpcklwd         m0, m1
    punpcklwd         m2, m3
    punpcklwd         m4, m5
    punpcklwd         m6, m7
%endif
%endif
%endmacro

%macro MC_8TAP_V_LOAD 5
    lea              %5q, [%2]
    sub              %5q, r3srcq
    movu              m0, [%5q            ]      ;load x- 3*srcstride
    movu              m1, [%5q+   %3q     ]      ;load x- 2*srcstride
    movu              m2, [%5q+ 2*%3q     ]      ;load x-srcstride
    movu              m3, [%2       ]      ;load x
    movu              m4, [%2+   %3q]      ;load x+stride
    movu              m5, [%2+ 2*%3q]      ;load x+2*stride
    movu              m6, [%2+r3srcq]      ;load x+3*stride
    movu              m7, [%2+ 4*%3q]      ;load x+4*stride
%if %1 == 8
%if %4 > 8
    SBUTTERFLY        bw, 0, 1, 8
    SBUTTERFLY        bw, 2, 3, 8
    SBUTTERFLY        bw, 4, 5, 8
    SBUTTERFLY        bw, 6, 7, 8
%else
    punpcklbw         m0, m1
    punpcklbw         m2, m3
    punpcklbw         m4, m5
    punpcklbw         m6, m7
%endif
%else
%if %4 > 4
    SBUTTERFLY        wd, 0, 1, 8
    SBUTTERFLY        wd, 2, 3, 8
    SBUTTERFLY        wd, 4, 5, 8
    SBUTTERFLY        wd, 6, 7, 8
%else
    punpcklwd         m0, m1
    punpcklwd         m2, m3
    punpcklwd         m4, m5
    punpcklwd         m6, m7
%endif
%endif
%endmacro

%macro PEL_10STORE2 3
    movd           [%1], %2
%endmacro
%macro PEL_10STORE4 3
    movq           [%1], %2
%endmacro
%macro PEL_10STORE8 3
    movdqa         [%1], %2
%endmacro
%macro PEL_10STORE16 3
%if cpuflag(avx2)
    movu            [%1], %2
%else
    PEL_10STORE8      %1, %2, %3
    movdqa       [%1+16], %3
%endif
%endmacro
%macro PEL_10STORE32 3
    PEL_10STORE16     %1, %2, %3
    movu         [%1+32], %3
%endmacro

%macro LOOP_END 3
    add              %1q, 2*MAX_PB_SIZE          ; dst += dststride
    add              %2q, %3q                    ; src += srcstride
    dec          heightd                         ; cmp height
    jnz               .loop                      ; height loop
%endmacro


%macro MC_PIXEL_COMPUTE 2-3 ;width, bitdepth
%if %2 == 8
%if cpuflag(avx2) && %0 ==3
%if %1 > 16
    vextracti128 xm1, m0, 1
    pmovzxbw      m1, xm1
    psllw         m1, 14-%2
%endif
    pmovzxbw      m0, xm0
%else ; not avx
%if %1 > 8
    punpckhbw     m1, m0, m2
    psllw         m1, 14-%2
%endif
    punpcklbw     m0, m2
%endif
%endif ;avx
    psllw         m0, 14-%2
%endmacro

%macro MC_8TAP_HV_COMPUTE 4     ; width, bitdepth, filter

%if %2 == 8
    vpbroadcastw     m15, [%3q + 0 * 2]
    pmaddubsw         m0, m15               ;x1*c1+x2*c2
    vpbroadcastw     m15, [%3q + 1 * 2]
    pmaddubsw         m2, m15               ;x3*c3+x4*c4
    vpbroadcastw     m15, [%3q + 2 * 2]
    pmaddubsw         m4, m15               ;x5*c5+x6*c6
    vpbroadcastw     m15, [%3q + 3 * 2]
    pmaddubsw         m6, m15               ;x7*c7+x8*c8
    paddw             m0, m2
    paddw             m4, m6
    paddw             m0, m4
%else
    vpbroadcastw     m15, [%3q + 0 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m0, m15
    vpbroadcastw     m15, [%3q + 1 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m2, m15
    vpbroadcastw     m15, [%3q + 2 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m4, m15
    vpbroadcastw     m15, [%3q + 3 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m6, m15
    paddd             m0, m2
    paddd             m4, m6
    paddd             m0, m4
%if %2 != 8
    psrad             m0, %2-8
%endif
%if %1 > 4
    vpbroadcastw     m15, [%3q + 0 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m1, m15
    vpbroadcastw     m15, [%3q + 1 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m3, m15
    vpbroadcastw     m15, [%3q + 2 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m5, m15
    vpbroadcastw     m15, [%3q + 3 * 2]
    pmovsxbw         m15, xm15
    pmaddwd           m7, m15
    paddd             m1, m3
    paddd             m5, m7
    paddd             m1, m5
%if %2 != 8
    psrad             m1, %2-8
%endif
%endif
    p%4               m0, m1
%endif
%endmacro


%macro MC_8TAP_COMPUTE 2-3     ; width, bitdepth
%if %2 == 8
%if cpuflag(avx2) && (%0 == 3)

    vperm2i128 m10, m0,  m1, q0301
    vinserti128 m0, m0, xm1, 1
    SWAP 1, 10

    vperm2i128 m10, m2,  m3, q0301
    vinserti128 m2, m2, xm3, 1
    SWAP 3, 10


    vperm2i128 m10, m4,  m5, q0301
    vinserti128 m4, m4, xm5, 1
    SWAP 5, 10

    vperm2i128 m10, m6,  m7, q0301
    vinserti128 m6, m6, xm7, 1
    SWAP 7, 10
%endif

    pmaddubsw         m0, m12   ;x1*c1+x2*c2
    pmaddubsw         m2, m13   ;x3*c3+x4*c4
    pmaddubsw         m4, m14   ;x5*c5+x6*c6
    pmaddubsw         m6, m15   ;x7*c7+x8*c8
    paddw             m0, m2
    paddw             m4, m6
    paddw             m0, m4
%if %1 > 8
    pmaddubsw         m1, m12
    pmaddubsw         m3, m13
    pmaddubsw         m5, m14
    pmaddubsw         m7, m15
    paddw             m1, m3
    paddw             m5, m7
    paddw             m1, m5
%endif
%else
    pmaddwd           m0, m12
    pmaddwd           m2, m13
    pmaddwd           m4, m14
    pmaddwd           m6, m15
    paddd             m0, m2
    paddd             m4, m6
    paddd             m0, m4
%if %2 != 8
    psrad             m0, %2-8
%endif
%if %1 > 4
    pmaddwd           m1, m12
    pmaddwd           m3, m13
    pmaddwd           m5, m14
    pmaddwd           m7, m15
    paddd             m1, m3
    paddd             m5, m7
    paddd             m1, m5
%if %2 != 8
    psrad             m1, %2-8
%endif
%endif
%endif
%endmacro

; ******************************
; void %1_put_pixels(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride,
;                         int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************

%macro PUT_PIXELS 3
    MC_PIXELS     %1, %2, %3
%endmacro

%macro MC_PIXELS 3
cglobal %1_put_pixels%2_%3, 4, 4, 3, dst, src, srcstride,height
    pxor              m2, m2
.loop:
    SIMPLE_LOAD       %2, %3, srcq, m0
    MC_PIXEL_COMPUTE  %2, %3, 1
    PEL_10STORE%2     dstq, m0, m1
    LOOP_END         dst, src, srcstride
    RET
 %endmacro

; ******************************
; void put_8tap_hX_X_X(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride,
;                       int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************

%macro PUT_8TAP 3
cglobal %1_put_8tap_h%2_%3, 5, 5, 16, dst, src, srcstride, height, rfilter

    MC_8TAP_FILTER          %3, rfilterq
.loop:
    MC_8TAP_H_LOAD          %3, srcq, %2, 10
    MC_8TAP_COMPUTE         %2, %3, 1
%if %3 > 8
    packssdw                m0, m1
%endif
    PEL_10STORE%2         dstq, m0, m1
    LOOP_END               dst, src, srcstride
    RET


; ******************************
; void put_8tap_vX_X_X(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride,
;                      int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************

cglobal %1_put_8tap_v%2_%3, 6, 8, 16, dst, src, srcstride, height, r3src, rfilter
    mov             rfilterq, r5mp
    MC_8TAP_FILTER        %3, rfilter
    lea               r3srcq, [srcstrideq*3]
.loop:
    MC_8TAP_V_LOAD        %3, srcq, srcstride, %2, r7
    MC_8TAP_COMPUTE       %2, %3, 1
%if %3 > 8
    packssdw              m0, m1
%endif
    PEL_10STORE%2       dstq, m0, m1
    LOOP_END             dst, src, srcstride
    RET
%endmacro


; ******************************
; void put_8tap_hvX_X(int16_t *dst, const uint8_t *_src, ptrdiff_t _srcstride,
;                     int height, const int8_t *hf, const int8_t *vf, int width)
; ******************************
%macro PUT_8TAP_HV 3
cglobal %1_put_8tap_hv%2_%3, 6, 6, 16, dst, src, srcstride, height, hf, vf, r3src
    lea                  r3srcq, [srcstrideq*3]
    sub                    srcq, r3srcq

    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                     m8, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                     m9, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m10, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m11, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m12, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m13, m0
    add                    srcq, srcstrideq
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m14, m0
    add                    srcq, srcstrideq
.loop:
    MC_8TAP_H_LOAD           %3, srcq, %2, 15
    MC_8TAP_HV_COMPUTE       %2, %3, hf, ackssdw
    SWAP                    m15, m0
    punpcklwd                m0, m8, m9
    punpcklwd                m2, m10, m11
    punpcklwd                m4, m12, m13
    punpcklwd                m6, m14, m15
%if %2 > 4
    punpckhwd                m1, m8, m9
    punpckhwd                m3, m10, m11
    punpckhwd                m5, m12, m13
    punpckhwd                m7, m14, m15
%endif
%if %2 <= 4
    movq                     m8, m9
    movq                     m9, m10
    movq                    m10, m11
    movq                    m11, m12
    movq                    m12, m13
    movq                    m13, m14
    movq                    m14, m15
%else
    movdqa                   m8, m9
    movdqa                   m9, m10
    movdqa                  m10, m11
    movdqa                  m11, m12
    movdqa                  m12, m13
    movdqa                  m13, m14
    movdqa                  m14, m15
%endif
    MC_8TAP_HV_COMPUTE       %2, 14, vf, ackssdw
    PEL_10STORE%2          dstq, m0, m1

    LOOP_END                dst, src, srcstride
    RET
%endmacro
