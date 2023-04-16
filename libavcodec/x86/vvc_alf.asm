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
param_shuffe_ %+ %1:
%rep 2
    times 4 dw j
    times 4 dw (j + 0x0808)
%endrep
%endmacro

PARAM_SHUFFE 0
PARAM_SHUFFE 1
PARAM_SHUFFE 2
PARAM_SHUFFE 3

CLASSIFY_SHUFFE: times 2    db 2, 3, 0, 1, 6, 7, 4, 5, 10, 11, 8, 9, 14, 15, 12, 13
TRANSPOSE_PERMUTE:          dd 0, 1, 4, 5, 2, 3, 6, 7
ARG_VAR_SHUFFE: times 2     db 0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4

dw_64: times 8              dd 64
dw_2:  times 8              dd 2
dw_3:  times 8              dd 3
dw_5:  times 8              dd 5
dw_15: times 8              dd 15

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

    vshufpd                 m%5, m%1, m%2, 0011b        ;06 02 05 01
    vshufpd                 m%6, m%3, m%5, 1001b        ;06 10 01 09

    vshufpd                 m%1, m%1, m%6, 1100b        ;06 03 09 00
    vshufpd                 m%2, m%2, m%6, 0110b        ;10 07 01 04
    vshufpd                 m%3, m%3, m%5, 0110b        ;02 11 05 08

    vpermpd                 m%1, m%1, 01111000b         ;09 06 03 00
    vshufpd                 m%2, m%2, m%2, 1001b        ;10 07 04 01
    vpermpd                 m%3, m%3, 10000111b         ;11 08 05 02
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
    %define filters m %+ j
    %define clips m %+ k

    pshufb          m14, clips, [param_shuffe_ %+ i]          ;clip
    pxor            m13, m13
    psubw           m13, m14                                ;-clip

    vpsubw          m9, m2
    CLIPW           m9, m13, m14

    vpsubw          m10, m2
    CLIPW           m10, m13, m14

    vpunpckhwd      m15, m9, m10
    vpunpcklwd      m9, m9, m10

    pshufb          m14, filters, [param_shuffe_ %+ i]       ;filter
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
        vinserti128     m%1, m%1, xm%3, 1
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
    LOAD_PIXELS_ %+ BPC %1, %2, %3
%endmacro

%macro STORE_PIXELS_16 3
    %if WIDTH == 16
        movu            [%1], m%2
    %else
        pextrq          [%1], xm%2, 0
        pextrq          [%1 + dst_strideq], xm%2, 1
        vperm2i128      m%2, m%2, m%2, 1
        pextrq          [%1 + dst_strideq * 2], xm%2, 0
        pextrq          [%1 + dst_stride3q], xm%2, 1
    %endif
%endmacro

%macro STORE_PIXELS_8 3
    vperm2i128          m%3, m%2, m%3, 1
    packuswb            m%2, m%3
    %if WIDTH == 16
        movu            [%1], xm%2
    %else
        pextrd          [%1], xm%2, 0
        pextrd          [%1 + dst_strideq], xm%2, 1
        pextrd          [%1 + dst_strideq * 2], xm%2, 2
        pextrd          [%1 + dst_stride3q], xm%2, 3
    %endif
%endmacro

;STORE_PIXELS(dest, src, tmp)
%macro STORE_PIXELS 3
    STORE_PIXELS_ %+ BPC %1, %2, %3
%endmacro

;CLASSIFY_LOAD_PIXELS(dest, src)
%macro CLASSIFY_LOAD_PIXELS 2
%if ps == 2
    movu %1, %2
%else
    vpmovzxbw %1, %2
%endif
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

cglobal vvc_alf_filter_%2_w%3_%1bpc, 9, 14, 15, dst, dst_stride, src, src_stride, height, filter, clip, stride, pixel_max, \
    tmp, offset, src_stride3, src_stride0, dst_stride3
;pixel size
%define ps (%1 / 8)
    lea             src_stride3q, [src_strideq * 2 + src_strideq]
    lea             dst_stride3q, [dst_strideq * 2 + dst_strideq]

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
    movd          xm2, pixel_maxd
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

%define ALF_GRADIENT_BORDER 2
%define ALF_BORDER_LUMA 3

%macro ALF_CLASSIFY_GRAD 1
;void ff_vvc_alf_classify_grad(int *gradient_sum,
;       const uint8_t *src, ptrdiff_t src_stride, intptr_t width, intptr_t height,
;       intptr_t vb_pos);
cglobal vvc_alf_classify_grad_%1bpc, 6, 14, 15, gradient_sum, src, src_stride, width, height, vb_pos, \
    x, y, s0, s1, s2, s3, vb_pos_below, src_stride3

    lea src_stride3q, [src_strideq * 2 + src_strideq]

    lea vb_pos_belowd, [vb_posd + ALF_GRADIENT_BORDER]

    ;src = src - ALF_BORDER_LUMA * src_stride - ALF_BORDER_LUMA
    sub srcq, src_stride3q
    sub srcq, ALF_BORDER_LUMA * ps

    add widthd, ALF_GRADIENT_BORDER * 2
    add heightd, ALF_GRADIENT_BORDER * 2

    xor yd, yd

.loop_h:
    xor  xd,  xd
    pxor m15, m15                               ;prev
    .loop_w:
        lea     s0q, [srcq + xq * ps]
        lea     s1q, [s0q + src_strideq]
        lea     s2q, [s0q + 2 * src_strideq]
        lea     s3q, [s0q + src_stride3q]

        cmp     yd, vb_pos_belowd
        cmove  s0q, s1q

        cmp     yd, vb_posd
        cmove  s3q, s2q

        CLASSIFY_LOAD_PIXELS m0, [s0q]
        CLASSIFY_LOAD_PIXELS m1, [s1q]
        CLASSIFY_LOAD_PIXELS m2, [s2q]
        CLASSIFY_LOAD_PIXELS m3, [s3q]

        CLASSIFY_LOAD_PIXELS m4, [s0q + 2 * ps]
        CLASSIFY_LOAD_PIXELS m5, [s1q + 2 * ps]
        CLASSIFY_LOAD_PIXELS m6, [s2q + 2 * ps]
        CLASSIFY_LOAD_PIXELS m7, [s3q + 2 * ps]

        vpblendw m8,  m0,  m1, 0xaa             ;nw
        vpblendw m9,  m0,  m5, 0x55             ;n
        vpblendw m10, m4,  m5, 0xaa             ;ne
        vpblendw m11, m1,  m2, 0xaa             ;w
        vpblendw m12, m5,  m6, 0xaa             ;e
        vpblendw m13, m2,  m3, 0xaa             ;sw
        vpblendw m14, m2,  m7, 0x55             ;s


        vpblendw m0,  m1,  m6, 0x55
        vpaddw   m0,  m0                        ;c

        movu     m1,  [CLASSIFY_SHUFFE]
        pshufb   m1,  m0,  m1                   ;d

        vpaddw   m9,  m14                       ;n + s
        vpsubw   m9,  m0                        ;(n + s) - c
        vpabsw   m9,  m9                        ;ver

        vpaddw   m11, m12                       ;w + e
        vpsubw   m11, m1                        ;(w + e) - d
        vpabsw   m11, m11                       ;hor

        vpblendw m14, m6, m7, 0xaa              ;se
        vpaddw   m8,  m14                       ;nw + se
        vpsubw   m8,  m1                        ;(nw + se) - d
        vpabsw   m8,  m8                        ;di0

        vpaddw   m10, m13                       ;ne + sw
        vpsubw   m10, m1                        ;(nw + se) - d
        vpabsw   m10, m10                       ;di1

        phaddw   m9,  m11                       ;vh,  each word represent 2x2 pixels
        phaddw   m8,  m10                       ;di,  each word represent 2x2 pixels
        phaddw   m0,  m9, m8                    ;all = each word represent 4x2 pixels, order is v_h_d0_d1 x 4

        vinserti128  m15, m15, xm0, 1
        vpblendw     m1,  m0, m15, 0xaa         ;t

        phaddw       m1,  m0                    ;each word represent 8x2 pixels, adjacent word share 4x2 pixels

        vextracti128 xm15, m0, 1                ;prev

        movu         [gradient_sumq], m1

        add gradient_sumq, 32
        add xd, 16
        cmp xd, widthd
        jl .loop_w

    lea srcq, [srcq + 2 * src_strideq]
    add yd, 2
    cmp yd, heightd
    jl .loop_h
    RET
%endmacro

;SAVE_CLASSIFY_PARAM_W16(dest, src)
%macro SAVE_CLASSIFY_PARAM_W16 2
    lea tempq, [%1q + xq]
    movu [tempq], xm%2
    vperm2i128 m%2, m%2, m%2, 1
    movu [tempq + widthq], xm%2
%endmacro

;SAVE_CLASSIFY_PARAM_W8
%macro SAVE_CLASSIFY_PARAM_W8 2
    movq [%1], xm%2
    vperm2i128 m%2, m%2, m%2, 1
    movq [%1 + widthq], xm%2
%endmacro

;SAVE_CLASSIFY_PARAM_W4
%macro SAVE_CLASSIFY_PARAM_W4 2
    movd [%1], xm%2
    vperm2i128 m%2, m%2, m%2, 1
    movd [%1 + widthq], xm%2
%endmacro

;SAVE_CLASSIFY_PARAM_W(dest, src)
%macro SAVE_CLASSIFY_PARAM_W 2
    lea tempq, [%1q + xq]

    cmp wd, 8
    jl %%w4
        SAVE_CLASSIFY_PARAM_W8 tempq, %2
        vpermq m%2, m%2, 00010011b
        add tempq, 8
        cmp wd, 8
        je %%end
    %%w4:
    SAVE_CLASSIFY_PARAM_W4 tempq, %2
%%end:
%endmacro

%macro ALF_CLASSIFY_H8 0
    ;first line, sum of 16x4 pixels (includes borders)
    lea gradq, [gradient_sumq + 2 * xq]
    movu m0, [gradq]
    movu m1, [gradq + sum_strideq]
    movu m2, [gradq + 2 * sum_strideq]

    pcmpeqb      m11, m11
    movd xm13, yd
    vpbroadcastd m13, xm13
    movd xm12, vb_posd
    vpbroadcastd m12, xm12
    vpcmpeqd     m13, m12       ; y == vb_pos
    pandn        m13, m11       ; y != vb_pos

    vpbroadcastd m14, [dw_3]
    pblendvb m14, m14, [dw_2], m13    ;ac

    pblendvb m3, m15, [gradq + sum_stride3q], m13

    ;extent to dword to avoid overflow
    vpunpcklwd m4,  m0, m15
    vpunpckhwd m5,  m0, m15
    vpunpcklwd m6,  m1, m15
    vpunpckhwd m7,  m1, m15
    vpunpcklwd m8,  m2, m15
    vpunpckhwd m9,  m2, m15
    vpunpcklwd m10, m3, m15
    vpunpckhwd m11, m3, m15

    vpaddd m0, m4, m6
    vpaddd m1, m5, m7
    vpaddd m2, m8, m10
    vpaddd m3, m9, m11

    ;sum of the first row
    vpaddd m0, m2           ;low
    vpaddd m1, m3           ;high

    lea gradq, [gradq + 2 * sum_strideq]

    pblendvb m10, m15, [gradq], m13

    movu m11, [gradq + sum_strideq]
    movu m12, [gradq + 2 * sum_strideq]
    movu m13, [gradq + sum_stride3q]

    vpunpcklwd m4,  m10, m15
    vpunpckhwd m5,  m10, m15
    vpunpcklwd m6,  m11, m15
    vpunpckhwd m7,  m11, m15
    vpunpcklwd m8,  m12, m15
    vpunpckhwd m9,  m12, m15
    vpunpcklwd m10,  m13, m15
    vpunpckhwd m11,  m13, m15

    vpaddd m2, m4, m6
    vpaddd m3, m5, m7
    vpaddd m4, m8, m10
    vpaddd m5, m9, m11

    ;sum of the second row
    vpaddd m2, m4           ;low
    vpaddd m3, m5           ;high

    vpunpckldq m4, m0, m2
    vpunpckhdq m5, m0, m2
    vpunpckldq m6, m1, m3
    vpunpckhdq m7, m1, m3

    ;each dword represent 4x2 alf blocks
    ;the order is 01452367
    vpunpckldq m0, m4, m6           ;sum_v
    vpunpckhdq m1, m4, m6           ;sum_h
    vpunpckldq m2, m5, m7           ;sum_d0
    vpunpckhdq m3, m5, m7           ;sum_d1

    vpcmpgtd   m4, m0, m1           ;dir_hv - 1
    vpmaxsd    m5, m0, m1           ;hv1
    vpminsd    m6, m0, m1           ;hv0

    vpaddd     m0, m1;              ;sum_hv

    vpcmpgtd   m7, m2, m3           ;dir_d - 1
    vpmaxsd    m8, m2, m3           ;d1
    vpminsd    m9, m2, m3           ;d0

    ;*transpose_idx = dir_d * 2 + dir_hv;
    vpbroadcastd m10, [dw_3]
    vpaddd m11, m7, m7
    vpaddd m11, m4
    vpaddd m10, m11
    vpermq m10, m10, 11011000b
    SAVE_CLASSIFY_PARAM transpose_idx, 10

    vpsrlq    m10, m8, 32
    vpsrlq    m11, m6, 32
    pmuldq    m12, m10, m11         ;d1 * hv0 high
    vpsrlq    m1,  m9, 32
    vpsrlq    m2,  m5, 32
    pmuldq    m3,  m1, m2           ;d0 * hv1 high
    vpcmpgtq  m10, m12, m3          ;dir1 - 1 high

    pmuldq    m1, m8, m6            ;d1 * hv0 low
    pmuldq    m2, m9, m5            ;d0 * hv1 low
    vpcmpgtq  m1, m2                ;dir1 - 1 low

    vpblendd  m1, m1, m10, 0xaa     ;dir1 - 1

    pblendvb  m2, m5, m8, m1        ;hvd1
    pblendvb  m3, m6, m9, m1        ;hvd0

    movd            xm5, bit_depthd
    vpbroadcastd    m5, xm5

    ;*class_idx = arg_var[av_clip_uintp2(sum_hv * ac >> (BIT_DEPTH - 1), 4)];
    vpmulld   m0, m14               ;sum_hv * ac
    vpsrlvd   m0, m0, m5
    vpminsd   m0, [dw_15]
    movu      m6, [ARG_VAR_SHUFFE]
    pshufb    m6, m0                ;class_idx

    vpbroadcastd m10, [dw_5]

    ;if (hvd1 * 2 > 9 * hvd0)
    ;   *class_idx += ((dir1 << 1) + 2) * 5;
    ;else if (hvd1 > 2 * hvd0)
    ;   *class_idx += ((dir1 << 1) + 1) * 5;
    paddd     m7,  m3, m3
    pcmpgtd   m7,  m2, m7           ;hvd1 > 2 * hvd0
    pand      m7, m10
    paddd     m6,  m7               ;class_idx

    paddd     m8, m2, m2
    vpslld    m9, m3, 3
    paddd     m9, m3
    pcmpgtd   m8, m9                ;hvd1 * 2 > 9 * hvd0
    pand      m8, m10
    paddd     m6, m8                ;class_idx

    pandn     m1, m7
    paddd     m1, m1                ;dir1 << 1
    paddd     m6, m1                ;class_idx
    vpermq    m6, m6, 11011000b

    SAVE_CLASSIFY_PARAM class_idx, 6
%endmacro

%macro ALF_CLASSIFY_16x8 0
%define SAVE_CLASSIFY_PARAM SAVE_CLASSIFY_PARAM_W16
    ALF_CLASSIFY_H8
%undef SAVE_CLASSIFY_PARAM
%endmacro

%macro ALF_CLASSIFY_Wx8 0
%define SAVE_CLASSIFY_PARAM SAVE_CLASSIFY_PARAM_W
    ALF_CLASSIFY_H8
%undef SAVE_CLASSIFY_PARAM
%endmacro

%macro ALF_CLASSIFY 1
;void ff_vvc_alf_classify(int *class_idx, int *transpose_idx,
;       const int *gradient_sum, intptr_t width, intptr_t height,
;       intptr_t vb_pos, int *gradient_tmp, intptr_t bit_depth);

;pixel size
%define ps (%1 / 8)
ALF_CLASSIFY_GRAD %1
cglobal vvc_alf_classify_%1bpc, 7, 15, 15, class_idx, transpose_idx, gradient_sum, width, height, vb_pos, bit_depth, \
    x, y, grad, sum_stride, sum_stride3, temp, w

    sub bit_depthq, 1

    ;now we can use gradient to get class idx and transpose idx
    lea sum_strideq, [widthd + ALF_GRADIENT_BORDER * 2]
    add sum_strideq, 15
    and sum_strideq, ~15                        ;align to 16
    add sum_strideq, sum_strideq                ;two rows a time

    add     gradient_sumq, 8                    ;first 4 words are garbage

    lea     sum_stride3q, [3 * sum_strideq]

    xor yd, yd
    and vb_posd, ~7    ;floor align to 8
    pxor m15, m15

.loop_sum_h:
    xor  xd,  xd
    .loop_sum_w16:
        lea wd, [widthd]
        sub wd, xd
        cmp wd, 16
        jl .loop_sum_w16_end
            ALF_CLASSIFY_16x8
            add xd, 16
        jmp .loop_sum_w16
    .loop_sum_w16_end:

    cmp wd, 0
    je .loop_sum_w_end
        ALF_CLASSIFY_Wx8
    .loop_sum_w_end:

    lea gradient_sumq, [gradient_sumq + 4 * sum_strideq]
    lea transpose_idxq, [transpose_idxq + 2 * widthq]
    lea class_idxq, [class_idxq + 2 * widthq]

    add yd, 8
    cmp yd, heightd
    jl .loop_sum_h

    RET
%endmacro

INIT_YMM avx2
ALF_FILTER  16
ALF_FILTER  8
ALF_CLASSIFY 16
ALF_CLASSIFY 8
%endif

