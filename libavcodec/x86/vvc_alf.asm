;******************************************************************************
;* VVC Adaptive Loop Filter SIMD optimizations
;*
;* Copyright (c) 2023 Nuo Mi <nuomi2021@gmail.com>
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

%macro PARAM_SHUFFE 1
%assign i (%1  * 2)
%assign j ((i + 1) << 8) + (i)
param_shuffe_%+%1:
%rep 2
    times 4 dw j
    times 4 dw (j + 0x0808)
%endrep
%endmacro

PARAM_SHUFFE 0
PARAM_SHUFFE 1
PARAM_SHUFFE 2
PARAM_SHUFFE 3

dw_64:                  dd 64

SECTION .text

%if HAVE_AVX2_EXTERNAL

;%1-%3 out
;%4 clip or filter
%macro LOAD_LUMA_PARAMS_W16 4
    %ifidn clip, %4
        movu            m%1, [%4q + 0 * 32]
        movu            m%2, [%4q + 1 * 32]
        movu            m%3, [%4q + 2 * 32]
    %elifidn filter, %4
        pmovsxbw        m%1, [%4q + 0 * 16]
        pmovsxbw        m%2, [%4q + 1 * 16]
        pmovsxbw        m%3, [%4q + 2 * 16]
    %else
        %error "need filter or clip for the fourth param"
    %endif
%endmacro

%macro LOAD_LUMA_PARAMS_W16 6
    LOAD_LUMA_PARAMS_W16    %1, %2, %3, %4
    ;m%1 = 03 02 01 00
    ;m%2 = 07 06 05 04
    ;m%3 = 11 10 09 08

    vshufpd                 m%5, m%1, m%2, 0b0011       ;06 02 05 01
    vshufpd                 m%6, m%3, m%5, 0b1001       ;06 10 01 09

    vshufpd                 m%1, m%1, m%6, 0b1100       ;06 03 09 00
    vshufpd                 m%2, m%2, m%6, 0b0110       ;10 07 01 04
    vshufpd                 m%3, m%3, m%5, 0b0110       ;02 11 05 08

    vpermpd                 m%1, m%1, 0b01_11_10_00     ;09 06 03 00
    vshufpd                 m%2, m%2, m%2, 0b1001       ;10 07 04 01
    vpermpd                 m%3, m%3, 0b10_00_01_11     ;11 08 05 02
%endmacro

%macro LOAD_LUMA_PARAMS_W4 6
    %ifidn clip, %4
        movq                xm%1, [%4q + 0 * 8]
        movq                xm%2, [%4q + 1 * 8]
        movq                xm%3, [%4q + 2 * 8]
    %elifidn filter, %4
        movd                xm%1, [%4q + 0 * 4]
        movd                xm%2, [%4q + 1 * 4]
        movd                xm%3, [%4q + 2 * 4]
        pmovsxbw            xm%1, xm%1
        pmovsxbw            xm%2, xm%2
        pmovsxbw            xm%3, xm%3
    %else
        %error "need filter or clip for the fourth param"
    %endif
    vpbroadcastq            m%1, xm%1
    vpbroadcastq            m%2, xm%2
    vpbroadcastq            m%3, xm%3
%endmacro

;%1-%3 out
;%4 clip or filter
;%5, %6 tmp
%macro LOAD_LUMA_PARAMS 6
    LOAD_LUMA_PARAMS_W %+ WIDTH %1, %2, %3, %4, %5, %6
%endmacro

%macro LOAD_CHROMA_PARAMS 4
    ;LOAD_CHROMA_PARAMS_W %+ WIDTH %1, %2, %3, %4
    %ifidn clip, %3
        movq            xm%1, [%3q]
        movd            xm%2, [%3q + 8]
    %elifidn filter, %3
        movd            xm%1, [%3q + 0]
        pinsrw          xm%2, [%3q + 4], 0
        vpmovsxbw       m%1, xm%1
        vpmovsxbw       m%2, xm%2
    %else
        %error "need filter or clip for the third param"
    %endif
    vpbroadcastq    m%1, xm%1
    vpbroadcastq    m%2, xm%2
%endmacro

%macro LOAD_PARAMS 0
    %if LUMA
        LOAD_LUMA_PARAMS     3, 4, 5, filter, 6, 7
        LOAD_LUMA_PARAMS     6, 7, 8, clip,   9, 10
    %else
        LOAD_CHROMA_PARAMS   3, 4, filter, 5
        LOAD_CHROMA_PARAMS   6, 7, clip, 8
    %endif
%endmacro

;FILTER(param_idx)
;input: m2, m9, m10
;output: m0, m1
;m13 ~ m15: tmp
%macro FILTER 1
    %assign i (%1 % 4)
    %assign j (%1 / 4 + 3)
    %assign k (%1 / 4 + 6)
    %define filters m%+j
    %define clips m%+k

    pshufb          m14, clips, [param_shuffe_%+i]          ;clip
    pxor            m13, m13
    psubw           m13, m14                                ;-clip

    vpsubw          m9, m2
    CLIPW           m9, m13, m14

    vpsubw          m10, m2
    CLIPW           m10, m13, m14

    vpunpckhwd      m15, m9, m10
    vpunpcklwd      m9, m9, m10

    pshufb          m14, filters, [param_shuffe_%+i]       ;filter
    vpunpcklwd      m10, m14, m14
    vpunpckhwd      m14, m14, m14

    vpmaddwd        m9, m10
    vpmaddwd        m14, m15

    paddd           m0, m9
    paddd           m1, m14
%endmacro

;FILTER(param_start, off0~off2)
%macro FILTER 4
    %assign %%i (%1)
    %rep 3
        mov             tmpq, srcq
        lea             offsetq, [%2]
        sub             tmpq, offsetq
        LOAD_PIXELS     9, tmpq, 11
        lea             tmpq, [srcq + offsetq]
        LOAD_PIXELS     10, tmpq, 12
        FILTER  %%i
        %assign %%i %%i+1
        %rotate 1
    %endrep
%endmacro

;filter pixels for luma and chroma
%macro FILTER 0
    %if LUMA
        FILTER          0, src_stride3q ,           src_strideq  * 2 + ps,  src_strideq  * 2
        FILTER          3, src_strideq  * 2 - ps,   src_strideq  + 2 * ps,  src_strideq  + ps
        FILTER          6, src_strideq,             src_strideq  - ps,      src_strideq  + -2 * ps
        FILTER          9, src_stride0q + 3 * ps,   src_stride0q + 2 * ps,  src_stride0q + ps
    %else
        FILTER          0, src_strideq * 2,         src_strideq  + ps,      src_strideq
        FILTER          3, src_strideq - ps,        src_stride0q + 2 * ps,  src_stride0q + ps
    %endif
%endmacro

%define SHIFT 7

%macro LOAD_PIXELS_16 3
    %if WIDTH == 16
        movu            m%1, [%2]
    %else
        pinsrq          xm%1, [%2], 0
        pinsrq          xm%1, [%2 + src_strideq], 1
        pinsrq          xm%3, [%2 + src_strideq * 2], 0
        pinsrq          xm%3, [%2 + src_stride3q], 1
        vinsertf128     m%1, xm%3, 1
    %endif
%endmacro

%macro LOAD_PIXELS_8 3
    %if WIDTH == 16
        vpmovzxbw       m%1,  [%2]
    %else
        pinsrd          xm%1, [%2], 0
        pinsrd          xm%1, [%2 + src_strideq], 1
        pinsrd          xm%1, [%2 + src_strideq * 2], 2
        pinsrd          xm%1, [%2 + src_stride3q], 3
        vpmovzxbw       m%1,  xm%1
    %endif
%endmacro

;LOAD_PIXELS(dest, src, tmp)
%macro LOAD_PIXELS 3
    LOAD_PIXELS_%+BPC %1, %2, %3
%endmacro

%macro STORE_PIXELS_16 3
    %if WIDTH == 16
        movu            [%1], m%2
    %else
        pextrq          [%1], xm%2, 0
        pextrq          [%1 + src_strideq], xm%2, 1
        vperm2f128      m%2, m%2, 1
        pextrq          [%1 + src_strideq * 2], xm%2, 0
        pextrq          [%1 + src_stride3q], xm%2, 1
    %endif
%endmacro

%macro STORE_PIXELS_8 3
    vperm2i128          m%3, m%2, m%3, 1
    packuswb            m%2, m%3
    %if WIDTH == 16
        movu            [%1], xm%2
    %else
        pextrd          [%1], xm%2, 0
        pextrd          [%1 + src_strideq], xm%2, 1
        pextrd          [%1 + src_strideq * 2], xm%2, 2
        pextrd          [%1 + src_stride3q], xm%2, 3
    %endif
%endmacro

;STORE_PIXELS(dest, src, tmp)
%macro STORE_PIXELS 3
    STORE_PIXELS_%+BPC %1, %2, %3
%endmacro

;FILTER(bpc, luma/chroma, width)
%macro ALF_FILTER 3
%xdefine BPC   %1
%ifidn %2, luma
    %xdefine LUMA 1
%else
    %xdefine LUMA 0
%endif
%xdefine WIDTH %3
; void vvc_alf_filter_%2_w%3_%1bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride,
;    const uint8_t *src, ptrdiff_t src_stride, int height,
;    const int8_t *filter, const int16_t *clip, ptrdiff_t stride, uint16_t pixel_max);

; see c code for p0 to p6

cglobal vvc_alf_filter_%2_w%3_%1bpc, 9, 13, 15, dst, dst_stride, src, src_stride, height, filter, clip, stride, pixel_max, \
    tmp, offset, src_stride3, src_stride0
;pixel size
%define ps (%1 / 8)
    lea             src_stride3q, [src_strideq * 2 + src_strideq]

    ;avoid "warning : absolute address can not be RIP-relative""
    mov             src_stride0q, 0

    shr             heightq, 2

.loop:
    LOAD_PARAMS

;we need loop 4 times for a 16x4 block, 1 time for a 4x4 block
%define rep_num (WIDTH / 4)
%define lines  (4 / rep_num)
%rep rep_num
    VPBROADCASTD    m0, [dw_64]
    VPBROADCASTD    m1, [dw_64]

    LOAD_PIXELS     2, srcq, 9   ;p0

    FILTER

    vpsrad          m0, SHIFT
    vpsrad          m1, SHIFT

    vpackssdw       m0, m0, m1
    paddw           m0, m2

    ;clip to pixel
    pinsrw          xm2, pixel_maxw, 0
    vpbroadcastw    m2, xm2
    pxor            m1, m1
    CLIPW           m0, m1, m2

    STORE_PIXELS    dstq, 0, 1

    lea             srcq, [srcq + lines * src_strideq]
    lea             dstq, [dstq + lines * dst_strideq]
%endrep

    lea             filterq, [filterq + strideq]
    lea             clipq, [clipq + 2 * strideq]

    dec             heightq
    jg              .loop
    RET
%endmacro

;FILTER(bpc, luma/chroma)
%macro ALF_FILTER 2
    ALF_FILTER  %1, %2, 16
    ALF_FILTER  %1, %2, 4
%endmacro

;FILTER(bpc)
%macro ALF_FILTER 1
    ALF_FILTER  %1, luma
    ALF_FILTER  %1, chroma
%endmacro

INIT_YMM avx2
ALF_FILTER  16
ALF_FILTER  8
%endif

