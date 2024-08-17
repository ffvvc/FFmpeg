; /*
; * Provide AVX2 luma optical flow functions for VVC decoding
; * Copyright (c) 2024 Nuo Mi
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

%define MAX_PB_SIZE             128
%define SRC_STRIDE              (MAX_PB_SIZE * 2)
%define SRC_PS                  2                                                       ; source pixel size, sizeof(int16_t)
%define BDOF_STACK_SIZE         10                                                      ; (4 + 1) * 2, 4 lines + the first line, *2 for h and v
%define bdof_stack_offset(line) ((((line) * 2) % BDOF_STACK_SIZE)  * mmsize)
%define SHIFT                   6
%define SHIFT2                  4

SECTION_RODATA 32
pd_15                   times 8 dd 15
pd_m15                  times 8 dd -15
cextern pb_0

pb_shuffle_w8  times 2 db   0, 1, 0xff, 0xff, 8, 9, 0xff, 0xff, 6, 7, 0xff, 0xff, 14, 15, 0xff, 0xff
pb_shuffle_w16 times 2 db   0, 1, 0xff, 0xff, 6, 7, 0xff, 0xff, 8, 9, 0xff, 0xff, 14, 15, 0xff, 0xff
pd_perm_w16            dd   0, 2, 1, 4, 3, 6, 5, 7
%if ARCH_X86_64

%if HAVE_AVX2_EXTERNAL

SECTION .text

INIT_YMM avx2

; dst = (src0 >> shift) - (src1 >> shift)
%macro DIFF 5 ;(dst, src0, src1, shift, tmp)
    psraw   %1, %2, %4
    psraw   %5, %3, %4
    psubw   %1, %5
%endmacro

%macro LOAD_GRAD_H 4 ;(dst, src, off, tmp)
    movu    %1, [%2 + %3 + 2 * SRC_PS]
    movu    %4, [%2 + %3]

    DIFF    %1, %1, %4, SHIFT, %4
%endmacro

%macro SUM_GRAD 2 ;(dst/grad0, grad1)
    paddw  %1, %2
    psraw  %1, 1    ; shift3
%endmacro

%macro APPLY_BDOF_MIN_BLOCK_LINE 5; (dst, vx, vy, tmp, line_num)
%define off bdof_stack_offset(%5)
    pmullw                        %1, %2, [rsp + off + 0 * mmsize]   ; vx * (gradient_h[0] - gradient_h[1])
    pmullw                        %4, %3, [rsp + off + 1 * mmsize]   ; vy * (gradient_v[0] - gradient_v[1])
    paddw                         %1, [src0q + (%5 + 1) * SRC_STRIDE + SRC_PS]
    paddw                         %4, [src1q + (%5 + 1) * SRC_STRIDE + SRC_PS]
    paddsw                        %1, %4                                         ; src0[x] + src1[x] + bdof_offset
    pmulhrsw                      %1, m11
    CLIPW                         %1, m9, m10
%endmacro

%macro SAVE_8BPC 2
    packuswb                   m%2, m%2
    vpermq                     m%2, m%2, q0020

    cmp                         wd, 16
    je                       %%w16
    movq                        %1, xm%2
    jmp                     %%wend
%%w16:
    movu                        %1, xm%2
%%wend:
%endmacro

%macro SAVE_16BPC 2
    cmp                         wd, 16
    je                       %%w16
    movu                        %1, xm%2
    jmp                     %%wend
%%w16:
    movu                        %1, m%2
%%wend:
%endmacro

%macro SAVE 2
    cmp                 pixel_maxd, (1 << 8) - 1
    jne               %%save_16bpc
    SAVE_8BPC                   %1, %2
    jmp                      %%end
%%save_16bpc:
    SAVE_16BPC                   %1, %2
%%end:
%endmacro

; [rsp + even * mmsize] are gradient_h[0] - gradient_h[1]
; [rsp +  odd * mmsize] are gradient_v[0] - gradient_v[1]
%macro APPLY_BDOF_MIN_BLOCK 4; (block_num, vx, vy, bd)
    vpbroadcastd                  m9, [pb_0]

    movd                        xm10, pixel_maxd
    vpbroadcastw                 m10, xm10

    lea                        tmp0d, [pixel_maxd + 1]
    movd                        xm11, tmp0d
    VPBROADCASTW                 m11, xm11                 ;shift_4 for pmulhrsw

    APPLY_BDOF_MIN_BLOCK_LINE    m6, %2, %3, m7, (%1) * 4 + 0
    SAVE                        [dstq + 0 * dsq], 6

    APPLY_BDOF_MIN_BLOCK_LINE    m6, %2, %3, m7, (%1) * 4 + 1
    SAVE                        [dstq + 1 * dsq], 6

    APPLY_BDOF_MIN_BLOCK_LINE    m6, %2, %3, m7, (%1) * 4 + 2
    SAVE                        [dstq + 2 * dsq], 6

    APPLY_BDOF_MIN_BLOCK_LINE    m6, %2, %3, m7, (%1) * 4 + 3
    SAVE                        [dstq + ds3q], 6
%endmacro

%macro SUM_MIN_BLOCK_W16 4; (src/dst, shuffle, perm, tmp)
    pshufb  %4, %1, %2
    vpermd  %4, %3, %4
    paddw   %1, %4
%endmacro

%macro SUM_MIN_BLOCK_W8 3; (src/dst, shuffle, tmp)
    pshufb  %3, %1, %2
    paddw   %1, %3
%endmacro

%macro BDOF_PROF_GRAD 2; (line_no, last_line)
%assign i0 (%1 + 0) % 3
%assign j0 (%1 + 1) % 3
%assign k0 (%1 + 2) % 3
%assign i1 3 + (%1 + 0) % 3
%assign j1 3 + (%1 + 1) % 3
%assign k1 3 + (%1 + 2) % 3

; we cached src0 in m0 to m2
%define t0 m %+ i0
%define c0 m %+ j0
%define b0 m %+ k0

; we cached src1 in m3 to m5
%define t1 m %+ i1
%define c1 m %+ j1
%define b1 m %+ k1
%define ndiff t1
%define off bdof_stack_offset(%1)

    movu                        b0, [src0q + (%1 + 2) * SRC_STRIDE + SRC_PS]
    movu                        b1, [src1q + (%1 + 2) * SRC_STRIDE + SRC_PS]

    ; gradient_v[0], gradient_v[1]
    DIFF                        m6,  b0,  t0, SHIFT, t0
    DIFF                        m7,  b1,  t1, SHIFT, t1

    ; save gradient_v[0] - gradient_v[1]
    psubw                      m10, m6, m7
    mova      [rsp + off + mmsize], m10

    ; gradient_h[0], gradient_h[1]
    LOAD_GRAD_H                 m8, src0q, (%1 + 1) * SRC_STRIDE, t0
    LOAD_GRAD_H                 m9, src1q, (%1 + 1) * SRC_STRIDE, t1

    ; save gradient_h[0] - gradient_h[1]
    psubw                      m11, m8, m9
    mova               [rsp + off], m11

    SUM_GRAD                    m8, m9                  ; temph
    SUM_GRAD                    m6, m7                  ; tempv

    DIFF                     ndiff, c1, c0, SHIFT2, t0  ; -diff

    psignw                      m7, ndiff, m8           ; sgxdi
    psignw                      m9, ndiff, m6           ; sgydi
    psignw                     m10, m8, m6              ; sgxgy

    pabsw                       m6, m6                  ; sgy2
    pabsw                       m8, m8                  ; sgx2

    ; use t0, t1 as temporary buffers
    cmp                         wd, 16

    je                       %%w16
    mova                        t0, [pb_shuffle_w8]
    SUM_MIN_BLOCK_W8            m6, t0, m11
    SUM_MIN_BLOCK_W8            m7, t0, m11
    SUM_MIN_BLOCK_W8            m8, t0, m11
    SUM_MIN_BLOCK_W8            m9, t0, m11
    SUM_MIN_BLOCK_W8           m10, t0, m11
    jmp                     %%wend

%%w16:
    mova                        t0, [pb_shuffle_w16]
    mova                        t1, [pd_perm_w16]
    SUM_MIN_BLOCK_W16           m6, t0, t1, m11
    SUM_MIN_BLOCK_W16           m7, t0, t1, m11
    SUM_MIN_BLOCK_W16           m8, t0, t1, m11
    SUM_MIN_BLOCK_W16           m9, t0, t1, m11
    SUM_MIN_BLOCK_W16          m10, t0, t1, m11

%%wend:
    phaddw                      m8, m7
    phaddw                      m6, m9
    phaddw                      m8, m6                   ; 2 sgx2, 2 sgxdi, sgy2, 2 sgydi, 2 sgx2, 2 sgxdi, 2 sgy2, 2 sgydi

%if (%1) == 0 || (%2)
    ; pad for top and bottom
    paddw                       m8, m8
    paddw                      m10, m10
%endif

    paddw                      m12, m8
    paddw                      m13, m10
%endmacro


%macro LOG2 5; log_sum, src, cmp, shift, tmp
    pcmpgtw               %5, %2, %3
    pandd                 %5, %4
    paddw                 %1, %5

    psrlw                 %2, %5
    psrlw                 %4, 1
    psrlw                 %3, %4
%endmacro

%macro LOG2 3; dst0/src0, dst1/src, offset
    pextrd              tmp0d, xm%1, %3
    pextrd              tmp1d, xm%2, %3
    bsr                 tmp0d, tmp0d
    bsr                 tmp1d, tmp1d
    pinsrd              xm%1, tmp0d, %3
    pinsrd              xm%2, tmp1d, %3
%endmacro

%macro LOG2 2; dst/src, tmp
    vperm2i128           m%2, m%1, m%1, 1
    LOG2                 %1,  %2, 0
    LOG2                 %1,  %2, 1
    LOG2                 %1,  %2, 2
    LOG2                 %1,  %2, 3
    vperm2i128           m%1, m%1, m%2, q0200
%endmacro

; %1: 2 sgx2, 2 sgxdi, sgy2, 2 sgydi, 2 sgx2, 2 sgxdi, 2 sgy2, 2 sgydi
; %2: 4 4sgxgy
%macro BDOF_VX_VY 2       ;
    pmovsxwd                m6, xm%1
    vextracti128           xm7, m%1, 1
    pmovsxwd                m7, xm7
    punpcklqdq              m8, m6, m7               ; 4 sgx2,  4 sgy2
    punpckhqdq              m9, m6, m7               ; 4 sgxdi, 4 sgydi
    mova                   m10, m8

    LOG2                    10, 11
    psignd                 m11, m9, m8
    pslld                  m11, 2
    vpsravd                m11, m11, m10
    CLIPD                  m11, [pd_m15], [pd_15]   ; 4 vx

    pxor                    m6, m6
    phaddw                 m%2, m6
    phaddw                 m%2, m6
    vpermq                 m%2, m%2, q0020
    pshufd                 m%2, m%2, q1120
    pmovsxwd               m%2, xmm%2               ; 4 sgxgy

    pmulld                 m%2, m11                 ; 4 vx * sgxgy
    psrad                  m%2, 1

    pslld                   m9, 2
    vextracti128          xmm9, m9, 1               ; 4 (sgydi << 2)
    psubd                   m9, m%2                 ; 4 ((sgydi << 2) - (vx * sgxgy >> 1))

    vextracti128          xmm8, m8, 1               ; 4 sgy2
    psignd                  m9, m8

    vextracti128         xmm10, m10, 1              ; 4 log2(sgy2)
    vpsravd                 m9, m9, m10
    CLIPD                   m9, [pd_m15], [pd_15]   ; 4 vy

    vpermq                 m%1, m11, q1100
    pshuflw                m%1, m%1, q2200
    vpunpckldq             m%1, m%1                 ; 4 x 4vx

    vpermq                 m%2, m9, q1100
    pshuflw                m%2, m%2, q2200
    vpunpckldq             m%2, m%2                 ; 4 x 4vy
%endmacro


%macro BDOF_MINI_BLOCKS 2 ; (block_num, last_block)

%if (%1) == 0
    movu                    m0, [src0q + 0 * SRC_STRIDE + SRC_PS]
    movu                    m1, [src0q + 1 * SRC_STRIDE + SRC_PS]
    movu                    m3, [src1q + 0 * SRC_STRIDE + SRC_PS]
    movu                    m4, [src1q + 1 * SRC_STRIDE + SRC_PS]

    pxor                   m12, m12
    pxor                   m13, m13

    BDOF_PROF_GRAD           0, 0
%endif

    mova                   m14, m12
    mova                   m15, m13

    pxor                   m12, m12
    pxor                   m13, m13
    BDOF_PROF_GRAD  %1 * 4 + 1, 0
    BDOF_PROF_GRAD  %1 * 4 + 2, 0
    paddw                  m14, m12
    paddw                  m15, m13

    pxor                   m12, m12
    pxor                   m13, m13
    BDOF_PROF_GRAD  %1 * 4 + 3, %2
%if (%2) == 0
    BDOF_PROF_GRAD  %1 * 4 + 4, 0
%endif
    paddw                  m14, m12
    paddw                  m15, m13

    BDOF_VX_VY              14, 15
    APPLY_BDOF_MIN_BLOCK    %1, m14, m15, bd
    lea                   dstq, [dstq + 4 * dsq]
%endmacro

;void ff_vvc_apply_bdof_%1(uint8_t *dst, const ptrdiff_t dst_stride, int16_t *src0, int16_t *src1,
;    const int w, const int h, const int int pixel_max)
%macro BDOF_AVX2 0
cglobal vvc_apply_bdof, 7, 10, 16, BDOF_STACK_SIZE*32, dst, ds, src0, src1, w, h, pixel_max, ds3, tmp0, tmp1

    lea                   ds3q, [dsq * 3]
    sub                  src0q, SRC_STRIDE + SRC_PS
    sub                  src1q, SRC_STRIDE + SRC_PS

    BDOF_MINI_BLOCKS         0, 0

    cmp                     hd, 16
    je                    .h16
    BDOF_MINI_BLOCKS         1, 1
    jmp                   .end

.h16:
    BDOF_MINI_BLOCKS         1, 0
    BDOF_MINI_BLOCKS         2, 0
    BDOF_MINI_BLOCKS         3, 1

.end:
    RET
%endmacro

%macro VVC_OF_AVX2 0
    BDOF_AVX2
%endmacro

VVC_OF_AVX2

%endif ; HAVE_AVX2_EXTERNAL

%endif ; ARCH_X86_64
