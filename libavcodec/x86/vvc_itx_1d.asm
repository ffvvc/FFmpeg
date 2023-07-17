;******************************************************************************
;*
;* SIMD-optimized inverse transform functions for VVC decoding
;*
;* Copyright (c) 2023 Frank Plowman <post@frankplowman.com>
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
;*
;******************************************************************************

%include "libavutil/x86/x86util.asm"

SECTION_RODATA

const vvc_dct2_4_even_mat, dw 64, 64, 64, -64, 64, -64, 64, 64

const vvc_dct2_4_odd_mat, dw 83, 36, 36, -83, -36, 83, -83, -36

%define matvec_mul_4_permute(m11, m12, m13, m14, \
                             m21, m22, m23, m24, \
                             m31, m32, m33, m34, \
                             m41, m42, m43, m44) \
    m11, m12, m21, m22, m31, m32, m41, m42, \
    m13, m14, m23, m24, m33, m34, m43, m44

%define dct2_8_odd_mat_permute(m11, m12, m13, m14, \
                               m21, m22, m23, m24, \
                               m31, m32, m33, m34, \
                               m41, m42, m43, m44) \
    m41, m42, m43, m44, \
    m31, m32, m33, m34, \
    m21, m22, m23, m24, \
    m11, m12, m13, m14
    
const vvc_dct2_8_odd_mat, dw matvec_mul_4_permute(dct2_8_odd_mat_permute( \
    -18,  50, -75,  89, \
    -50,  89, -18, -75, \
    -75,  18,  89,  50, \
    -89, -75, -50, -18))

%define matvec_mul_8_permute(m11, m12, m13, m14, m15, m16, m17, m18, \
                             m21, m22, m23, m24, m25, m26, m27, m28, \
                             m31, m32, m33, m34, m35, m36, m37, m38, \
                             m41, m42, m43, m44, m45, m46, m47, m48, \
                             m51, m52, m53, m54, m55, m56, m57, m58, \
                             m61, m62, m63, m64, m65, m66, m67, m68, \
                             m71, m72, m73, m74, m75, m76, m77, m78, \
                             m81, m82, m83, m84, m85, m86, m87, m88) \
    m11, m12, m21, m22, m31, m32, m41, m42, \
    m51, m52, m61, m62, m71, m72, m81, m82, \
    m13, m14, m23, m24, m33, m34, m43, m44, \
    m53, m54, m63, m64, m73, m74, m83, m84, \
    m15, m16, m25, m26, m35, m36, m45, m46, \
    m55, m56, m65, m66, m75, m76, m85, m86, \
    m17, m18, m27, m28, m37, m38, m47, m48, \
    m57, m58, m67, m68, m77, m78, m87, m88

%define dct2_16_odd_mat_permute(m11, m12, m13, m14, m15, m16, m17, m18, \
                                m21, m22, m23, m24, m25, m26, m27, m28, \
                                m31, m32, m33, m34, m35, m36, m37, m38, \
                                m41, m42, m43, m44, m45, m46, m47, m48, \
                                m51, m52, m53, m54, m55, m56, m57, m58, \
                                m61, m62, m63, m64, m65, m66, m67, m68, \
                                m71, m72, m73, m74, m75, m76, m77, m78, \
                                m81, m82, m83, m84, m85, m86, m87, m88) \
    m11, m12, m13, m14, m15, m16, m17, m18, \
    m21, m22, m23, m24, m25, m26, m27, m28, \
    m31, m32, m33, m34, m35, m36, m37, m38, \
    m41, m42, m43, m44, m45, m46, m47, m48, \
    m81, m82, m83, m84, m85, m86, m87, m88, \
    m71, m72, m73, m74, m75, m76, m77, m78, \
    m61, m62, m63, m64, m65, m66, m67, m68, \
    m51, m52, m53, m54, m55, m56, m57, m58

const vvc_dct2_16_odd_mat, dw matvec_mul_8_permute(dct2_16_odd_mat_permute( \
     -9,  25, -43,  57, -70,  80, -87,  90, \
    -25,  70, -90,  80, -43,  -9,  57, -87, \
    -43,  90, -57, -25,  87, -70,  -9,  80, \
    -57,  80,  25, -90,   9,  87, -43, -70, \
    -70,  43,  87,  -9, -90, -25,  80,  57, \
    -80,  -9,  70,  87,  25, -57, -90, -43, \
    -87, -57,  -9,  43,  80,  90,  70,  25, \
    -90, -87, -80, -70, -57, -43, -25,  -9))

%define matvec_mul_16_permute(m1_1,  m1_2,  m1_3,  m1_4,  m1_5,  m1_6,  m1_7,  m1_8,  m1_9,  m1_10,  m1_11,  m1_12,  m1_13,  m1_14,  m1_15,  m1_16, \
                              m2_1,  m2_2,  m2_3,  m2_4,  m2_5,  m2_6,  m2_7,  m2_8,  m2_9,  m2_10,  m2_11,  m2_12,  m2_13,  m2_14,  m2_15,  m2_16, \
                              m3_1,  m3_2,  m3_3,  m3_4,  m3_5,  m3_6,  m3_7,  m3_8,  m3_9,  m3_10,  m3_11,  m3_12,  m3_13,  m3_14,  m3_15,  m3_16, \
                              m4_1,  m4_2,  m4_3,  m4_4,  m4_5,  m4_6,  m4_7,  m4_8,  m4_9,  m4_10,  m4_11,  m4_12,  m4_13,  m4_14,  m4_15,  m4_16, \
                              m5_1,  m5_2,  m5_3,  m5_4,  m5_5,  m5_6,  m5_7,  m5_8,  m5_9,  m5_10,  m5_11,  m5_12,  m5_13,  m5_14,  m5_15,  m5_16, \
                              m6_1,  m6_2,  m6_3,  m6_4,  m6_5,  m6_6,  m6_7,  m6_8,  m6_9,  m6_10,  m6_11,  m6_12,  m6_13,  m6_14,  m6_15,  m6_16, \
                              m7_1,  m7_2,  m7_3,  m7_4,  m7_5,  m7_6,  m7_7,  m7_8,  m7_9,  m7_10,  m7_11,  m7_12,  m7_13,  m7_14,  m7_15,  m7_16, \
                              m8_1,  m8_2,  m8_3,  m8_4,  m8_5,  m8_6,  m8_7,  m8_8,  m8_9,  m8_10,  m8_11,  m8_12,  m8_13,  m8_14,  m8_15,  m8_16, \
                              m9_1,  m9_2,  m9_3,  m9_4,  m9_5,  m9_6,  m9_7,  m9_8,  m9_9,  m9_10,  m9_11,  m9_12,  m9_13,  m9_14,  m9_15,  m9_16, \
                             m10_1, m10_2, m10_3, m10_4, m10_5, m10_6, m10_7, m10_8, m10_9, m10_10, m10_11, m10_12, m10_13, m10_14, m10_15, m10_16, \
                             m11_1, m11_2, m11_3, m11_4, m11_5, m11_6, m11_7, m11_8, m11_9, m11_10, m11_11, m11_12, m11_13, m11_14, m11_15, m11_16, \
                             m12_1, m12_2, m12_3, m12_4, m12_5, m12_6, m12_7, m12_8, m12_9, m12_10, m12_11, m12_12, m12_13, m12_14, m12_15, m12_16, \
                             m13_1, m13_2, m13_3, m13_4, m13_5, m13_6, m13_7, m13_8, m13_9, m13_10, m13_11, m13_12, m13_13, m13_14, m13_15, m13_16, \
                             m14_1, m14_2, m14_3, m14_4, m14_5, m14_6, m14_7, m14_8, m14_9, m14_10, m14_11, m14_12, m14_13, m14_14, m14_15, m14_16, \
                             m15_1, m15_2, m15_3, m15_4, m15_5, m15_6, m15_7, m15_8, m15_9, m15_10, m15_11, m15_12, m15_13, m15_14, m15_15, m15_16, \
                             m16_1, m16_2, m16_3, m16_4, m16_5, m16_6, m16_7, m16_8, m16_9, m16_10, m16_11, m16_12, m16_13, m16_14, m16_15, m16_16) \
      m1_1,   m1_2,   m2_1,   m2_2,   m3_1,   m3_2,   m4_1,   m4_2, \
      m5_1,   m5_2,   m6_1,   m6_2,   m7_1,   m7_2,   m8_1,   m8_2, \
      m9_1,   m9_2,  m10_1,  m10_2,  m11_1,  m11_2,  m12_1,  m12_2, \
     m13_1,  m13_2,  m14_1,  m14_2,  m15_1,  m15_2,  m16_1,  m16_2, \
      m1_3,   m1_4,   m2_3,   m2_4,   m3_3,   m3_4,   m4_3,   m4_4, \
      m5_3,   m5_4,   m6_3,   m6_4,   m7_3,   m7_4,   m8_3,   m8_4, \
      m9_3,   m9_4,  m10_3,  m10_4,  m11_3,  m11_4,  m12_3,  m12_4, \
     m13_3,  m13_4,  m14_3,  m14_4,  m15_3,  m15_4,  m16_3,  m16_4, \
      m1_5,   m1_6,   m2_5,   m2_6,   m3_5,   m3_6,   m4_5,   m4_6, \
      m5_5,   m5_6,   m6_5,   m6_6,   m7_5,   m7_6,   m8_5,   m8_6, \
      m9_5,   m9_6,  m10_5,  m10_6,  m11_5,  m11_6,  m12_5,  m12_6, \
     m13_5,  m13_6,  m14_5,  m14_6,  m15_5,  m15_6,  m16_5,  m16_6, \
      m1_7,   m1_8,   m2_7,   m2_8,   m3_7,   m3_8,   m4_7,   m4_8, \
      m5_7,   m5_8,   m6_7,   m6_8,   m7_7,   m7_8,   m8_7,   m8_8, \
      m9_7,   m9_8,  m10_7,  m10_8,  m11_7,  m11_8,  m12_7,  m12_8, \
     m13_7,  m13_8,  m14_7,  m14_8,  m15_7,  m15_8,  m16_7,  m16_8, \
      m1_9,  m1_10,   m2_9,  m2_10,   m3_9,  m3_10,   m4_9,  m4_10, \
      m5_9,  m5_10,   m6_9,  m6_10,   m7_9,  m7_10,   m8_9,  m8_10, \
      m9_9,  m9_10,  m10_9, m10_10,  m11_9, m11_10,  m12_9, m12_10, \
     m13_9, m13_10,  m14_9, m14_10,  m15_9, m15_10,  m16_9, m16_10, \
     m1_11,  m1_12,  m2_11,  m2_12,  m3_11,  m3_12,  m4_11,  m4_12, \
     m5_11,  m5_12,  m6_11,  m6_12,  m7_11,  m7_12,  m8_11,  m8_12, \
     m9_11,  m9_12, m10_11, m10_12, m11_11, m11_12, m12_11, m12_12, \
    m13_11, m13_12, m14_11, m14_12, m15_11, m15_12, m16_11, m16_12, \
     m1_13,  m1_14,  m2_13,  m2_14,  m3_13,  m3_14,  m4_13,  m4_14, \
     m5_13,  m5_14,  m6_13,  m6_14,  m7_13,  m7_14,  m8_13,  m8_14, \
     m9_13,  m9_14, m10_13, m10_14, m11_13, m11_14, m12_13, m12_14, \
    m13_13, m13_14, m14_13, m14_14, m15_13, m15_14, m16_13, m16_14, \
     m1_15,  m1_16,  m2_15,  m2_16,  m3_15,  m3_16,  m4_15,  m4_16, \
     m5_15,  m5_16,  m6_15,  m6_16,  m7_15,  m7_16,  m8_15,  m8_16, \
     m9_15,  m9_16, m10_15, m10_16, m11_15, m11_16, m12_15, m12_16, \
    m13_15, m13_16, m14_15, m14_16, m15_15, m15_16, m16_15, m16_16

%define dct2_32_odd_mat_permute(m1_1,  m1_2,  m1_3,  m1_4,  m1_5,  m1_6,  m1_7,  m1_8,  m1_9,  m1_10,  m1_11,  m1_12,  m1_13,  m1_14,  m1_15,  m1_16, \
                                m2_1,  m2_2,  m2_3,  m2_4,  m2_5,  m2_6,  m2_7,  m2_8,  m2_9,  m2_10,  m2_11,  m2_12,  m2_13,  m2_14,  m2_15,  m2_16, \
                                m3_1,  m3_2,  m3_3,  m3_4,  m3_5,  m3_6,  m3_7,  m3_8,  m3_9,  m3_10,  m3_11,  m3_12,  m3_13,  m3_14,  m3_15,  m3_16, \
                                m4_1,  m4_2,  m4_3,  m4_4,  m4_5,  m4_6,  m4_7,  m4_8,  m4_9,  m4_10,  m4_11,  m4_12,  m4_13,  m4_14,  m4_15,  m4_16, \
                                m5_1,  m5_2,  m5_3,  m5_4,  m5_5,  m5_6,  m5_7,  m5_8,  m5_9,  m5_10,  m5_11,  m5_12,  m5_13,  m5_14,  m5_15,  m5_16, \
                                m6_1,  m6_2,  m6_3,  m6_4,  m6_5,  m6_6,  m6_7,  m6_8,  m6_9,  m6_10,  m6_11,  m6_12,  m6_13,  m6_14,  m6_15,  m6_16, \
                                m7_1,  m7_2,  m7_3,  m7_4,  m7_5,  m7_6,  m7_7,  m7_8,  m7_9,  m7_10,  m7_11,  m7_12,  m7_13,  m7_14,  m7_15,  m7_16, \
                                m8_1,  m8_2,  m8_3,  m8_4,  m8_5,  m8_6,  m8_7,  m8_8,  m8_9,  m8_10,  m8_11,  m8_12,  m8_13,  m8_14,  m8_15,  m8_16, \
                                m9_1,  m9_2,  m9_3,  m9_4,  m9_5,  m9_6,  m9_7,  m9_8,  m9_9,  m9_10,  m9_11,  m9_12,  m9_13,  m9_14,  m9_15,  m9_16, \
                               m10_1, m10_2, m10_3, m10_4, m10_5, m10_6, m10_7, m10_8, m10_9, m10_10, m10_11, m10_12, m10_13, m10_14, m10_15, m10_16, \
                               m11_1, m11_2, m11_3, m11_4, m11_5, m11_6, m11_7, m11_8, m11_9, m11_10, m11_11, m11_12, m11_13, m11_14, m11_15, m11_16, \
                               m12_1, m12_2, m12_3, m12_4, m12_5, m12_6, m12_7, m12_8, m12_9, m12_10, m12_11, m12_12, m12_13, m12_14, m12_15, m12_16, \
                               m13_1, m13_2, m13_3, m13_4, m13_5, m13_6, m13_7, m13_8, m13_9, m13_10, m13_11, m13_12, m13_13, m13_14, m13_15, m13_16, \
                               m14_1, m14_2, m14_3, m14_4, m14_5, m14_6, m14_7, m14_8, m14_9, m14_10, m14_11, m14_12, m14_13, m14_14, m14_15, m14_16, \
                               m15_1, m15_2, m15_3, m15_4, m15_5, m15_6, m15_7, m15_8, m15_9, m15_10, m15_11, m15_12, m15_13, m15_14, m15_15, m15_16, \
                               m16_1, m16_2, m16_3, m16_4, m16_5, m16_6, m16_7, m16_8, m16_9, m16_10, m16_11, m16_12, m16_13, m16_14, m16_15, m16_16) \
     m1_1,  m1_2,  m1_3,  m1_4,  m1_5,  m1_6,  m1_7,  m1_8,  m1_9,  m1_10,  m1_11,  m1_12,  m1_13,  m1_14,  m1_15,  m1_16, \
     m2_1,  m2_2,  m2_3,  m2_4,  m2_5,  m2_6,  m2_7,  m2_8,  m2_9,  m2_10,  m2_11,  m2_12,  m2_13,  m2_14,  m2_15,  m2_16, \
     m3_1,  m3_2,  m3_3,  m3_4,  m3_5,  m3_6,  m3_7,  m3_8,  m3_9,  m3_10,  m3_11,  m3_12,  m3_13,  m3_14,  m3_15,  m3_16, \
     m4_1,  m4_2,  m4_3,  m4_4,  m4_5,  m4_6,  m4_7,  m4_8,  m4_9,  m4_10,  m4_11,  m4_12,  m4_13,  m4_14,  m4_15,  m4_16, \
     m8_1,  m8_2,  m8_3,  m8_4,  m8_5,  m8_6,  m8_7,  m8_8,  m8_9,  m8_10,  m8_11,  m8_12,  m8_13,  m8_14,  m8_15,  m8_16, \
     m7_1,  m7_2,  m7_3,  m7_4,  m7_5,  m7_6,  m7_7,  m7_8,  m7_9,  m7_10,  m7_11,  m7_12,  m7_13,  m7_14,  m7_15,  m7_16, \
     m6_1,  m6_2,  m6_3,  m6_4,  m6_5,  m6_6,  m6_7,  m6_8,  m6_9,  m6_10,  m6_11,  m6_12,  m6_13,  m6_14,  m6_15,  m6_16, \
     m5_1,  m5_2,  m5_3,  m5_4,  m5_5,  m5_6,  m5_7,  m5_8,  m5_9,  m5_10,  m5_11,  m5_12,  m5_13,  m5_14,  m5_15,  m5_16, \
     m9_1,  m9_2,  m9_3,  m9_4,  m9_5,  m9_6,  m9_7,  m9_8,  m9_9,  m9_10,  m9_11,  m9_12,  m9_13,  m9_14,  m9_15,  m9_16, \
    m10_1, m10_2, m10_3, m10_4, m10_5, m10_6, m10_7, m10_8, m10_9, m10_10, m10_11, m10_12, m10_13, m10_14, m10_15, m10_16, \
    m11_1, m11_2, m11_3, m11_4, m11_5, m11_6, m11_7, m11_8, m11_9, m11_10, m11_11, m11_12, m11_13, m11_14, m11_15, m11_16, \
    m12_1, m12_2, m12_3, m12_4, m12_5, m12_6, m12_7, m12_8, m12_9, m12_10, m12_11, m12_12, m12_13, m12_14, m12_15, m12_16, \
    m16_1, m16_2, m16_3, m16_4, m16_5, m16_6, m16_7, m16_8, m16_9, m16_10, m16_11, m16_12, m16_13, m16_14, m16_15, m16_16, \
    m15_1, m15_2, m15_3, m15_4, m15_5, m15_6, m15_7, m15_8, m15_9, m15_10, m15_11, m15_12, m15_13, m15_14, m15_15, m15_16, \
    m14_1, m14_2, m14_3, m14_4, m14_5, m14_6, m14_7, m14_8, m14_9, m14_10, m14_11, m14_12, m14_13, m14_14, m14_15, m14_16, \
    m13_1, m13_2, m13_3, m13_4, m13_5, m13_6, m13_7, m13_8, m13_9, m13_10, m13_11, m13_12, m13_13, m13_14, m13_15, m13_16

const vvc_dct2_32_odd_mat, dw matvec_mul_16_permute(dct2_32_odd_mat_permute( \
     -4,  13, -22,  31, -38,  46, -54,  61, -67,  73, -78,  82, -85,  88, -90,  90, \
    -13,  38, -61,  78, -88,  90, -85,  73, -54,  31,  -4, -22,  46, -67,  82, -90, \
    -22,  61, -85,  90, -73,  38,   4, -46,  78, -90,  82, -54,  13,  31, -67,  88, \
    -31,  78, -90,  61,  -4, -54,  88, -82,  38,  22, -73,  90, -67,  13,  46, -85, \
    -38,  88, -73,   4,  67, -90,  46,  31, -85,  78, -13, -61,  90, -54, -22,  82, \
    -46,  90, -38, -54,  90, -31, -61,  88, -22, -67,  85, -13, -73,  82,  -4, -78, \
    -54,  85,   4, -88,  46,  61, -82, -13,  90, -38, -67,  78,  22, -90,  31,  73, \
    -61,  73,  46, -82, -31,  88,  13, -90,   4,  90, -22, -85,  38,  78, -54, -67, \
    -67,  54,  78, -38, -85,  22,  90,  -4, -90, -13,  88,  31, -82, -46,  73,  61, \
    -73,  31,  90,  22, -78, -67,  38,  90,  13, -82, -61,  46,  88,   4, -85, -54, \
    -78,   4,  82,  73, -13, -85, -67,  22,  88,  61, -31, -90, -54,  38,  90,  46, \
    -82, -22,  54,  90,  61, -13, -78, -85, -31,  46,  90,  67,  -4, -73, -88, -38, \
    -85, -46,  13,  67,  90,  73,  22, -38, -82, -88, -54,   4,  61,  90,  78,  31, \
    -88, -67, -31,  13,  54,  82,  90,  78,  46,   4, -38, -73, -90, -85, -61, -22, \
    -90, -82, -67, -46, -22,   4,  31,  54,  73,  85,  90,  88,  78,  61,  38,  13, \
    -90, -90, -88, -85, -82, -78, -73, -67, -61, -54, -46, -38, -31, -22, -13,  -4))

SECTION .text

INIT_XMM avx2

; Multiply a 2D vector by a 2x2 matrix.
%macro MATVEC_MUL_2 4 ; out, in, stride, mat
    movd            xm%1, [%2 + 0*%3*4]
    punpckldq       m%1, [%2 + 1*%3*4]
    packssdw        m%1, m%1
    pmaddwd         m%1, [%4]
%endmacro

; Multiply a 4D vector by a 4x4 matrix.
%macro MATVEC_MUL_4 5 ; out, in, stride, mat, temp
    %push
    %define %$out %1
    %define %$in %2
    %define %$stride %3
    %define %$mat %4
    %define %$temp %5

    lea             stride3q, [3*%$stride]

    movd            xm%$out, [%$in + 0*%$stride*4]
    punpckldq       m%$out, [%$in + 1*%$stride*4]
    movd            xm%$temp, [%$in + 2*%$stride*4]
    punpckldq       m%$temp, [%$in + stride3q*4]

    punpcklqdq      m%$out, m%$out
    packssdw        m%$out, m%$out

    punpcklqdq      m%$temp, m%$temp
    packssdw        m%$temp, m%$temp

    pmaddwd         m%$out, [%$mat]
    pmaddwd         m%$temp, [%$mat + 16]

    paddd           m%$out, m%$temp

    %pop
%endmacro

; Multiply an 8D vector by an 8x8 matrix.
%macro MATVEC_MUL_8 7 ; out[2], in, stride, mat, temp[2]
    %push
    %define %$out0 %1
    %define %$out1 %2
    %define %$in %3
    %define %$stride %4
    %define %$mat %5
    %define %$temp0 %6
    %define %$temp1 %7

    movd            xm%$out0, [%$in]
    punpckldq       m%$out0, [%$in + %$stride*4]
    punpcklqdq      m%$out0, m%$out0
    packssdw        m%$out0, m%$out0
    mova            m%$out1, m%$out0
    pmaddwd         m%$out0, [%$mat]
    pmaddwd         m%$out1, [%$mat + 16]

    %assign mat_offset 0
    %rep 3
        %assign mat_offset mat_offset + 32
        lea             %$in, [%$in + 2*%$stride*4]

        movd            xm%$temp0, [%$in]
        punpckldq       m%$temp0, [%$in + %$stride*4]
        punpcklqdq      m%$temp0, m%$temp0
        packssdw        m%$temp0, m%$temp0
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp0, [%$mat + mat_offset]
        pmaddwd         m%$temp1, [%$mat + mat_offset+16]
        paddd           m%$out0, m%$temp0
        paddd           m%$out1, m%$temp1
    %endrep

    %pop
%endmacro

; Multiply a 16D vector by an 16x16 matrix.
%macro MATVEC_MUL_16 9 ; out[4], in, stride, mat, temp[2]
    %push
    %define %$out0 %1
    %define %$out1 %2
    %define %$out2 %3
    %define %$out3 %4
    %define %$in %5
    %define %$stride %6
    %define %$mat %7
    %define %$temp0 %8
    %define %$temp1 %9

    movd            xm%$out0, [%$in]
    punpckldq       m%$out0, [%$in + %$stride*4]
    punpcklqdq      m%$out0, m%$out0
    packssdw        m%$out0, m%$out0
    mova            m%$out1, m%$out0
    mova            m%$out2, m%$out0
    mova            m%$out3, m%$out0
    pmaddwd         m%$out0, [%$mat]
    pmaddwd         m%$out1, [%$mat + 16]
    pmaddwd         m%$out2, [%$mat + 32]
    pmaddwd         m%$out3, [%$mat + 48]

    %assign mat_offset 0
    %rep 7
        %assign mat_offset mat_offset + 64
        lea             %$in, [%$in + 2*%$stride*4]

        movd            xm%$temp0, [%$in]
        punpckldq       m%$temp0, [%$in + %$stride*4]
        punpcklqdq      m%$temp0, m%$temp0
        packssdw        m%$temp0, m%$temp0
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*0]
        paddd           m%$out0, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*1]
        paddd           m%$out1, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*2]
        paddd           m%$out2, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*3]
        paddd           m%$out3, m%$temp1
    %endrep

    %pop
%endmacro

; Multiply a 32D vector by a 32x32 matrix.
%macro MATVEC_MUL_32 9 ; out[4], in, stride, mat, temp[2]
    %push
    %define %$out0 %1
    %define %$out1 %2
    %define %$out2 %3
    %define %$out3 %4
    %define %$in %5
    %define %$stride %6
    %define %$mat %7
    %define %$temp0 %8
    %define %$temp1 %9

    movd            xm%$out0, [%$in]
    punpckldq       m%$out0, [%$in + %$stride*4]
    punpcklqdq      m%$out0, m%$out0
    packssdw        m%$out0, m%$out0
    mova            m%$out1, m%$out0
    mova            m%$out2, m%$out0
    mova            m%$out3, m%$out0
    mova            m%$out4, m%$out0
    mova            m%$out5, m%$out0
    mova            m%$out6, m%$out0
    mova            m%$out7, m%$out0
    pmaddwd         m%$out0, [%$mat]
    pmaddwd         m%$out1, [%$mat + 16]
    pmaddwd         m%$out2, [%$mat + 32]
    pmaddwd         m%$out3, [%$mat + 48]
    pmaddwd         m%$out4, [%$mat + 64]
    pmaddwd         m%$out5, [%$mat + 80]
    pmaddwd         m%$out6, [%$mat + 96]
    pmaddwd         m%$out7, [%$mat + 112]

    %assign %$mat_offset 0
    %rep 15
        %assign %$mat_offset %$mat_offset + 16*8
        lea             %$in, [%$in + 2*%$stride*4]
        movd            xm%$temp0, [%$in]
        punpckldq       m%$temp0, [%$in + %$stride*4]
        punpcklqdq      m%$temp0, m%$temp0
        packssdw        m%$temp0, m%$temp0
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*0]
        paddd           m%$out0, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*1]
        paddd           m%$out1, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*2]
        paddd           m%$out2, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*3]
        paddd           m%$out3, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*4]
        paddd           m%$out4, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*5]
        paddd           m%$out5, m%$temp1
        mova            m%$temp1, m%$temp0
        pmaddwd         m%$temp1, [%$mat + mat_offset+16*6]
        paddd           m%$out6, m%$temp1
        pmaddwd         m%$temp0, [%$mat + mat_offset+16*7]
        paddd           m%$out7, m%$temp0
    %endrep

    %pop
%endmacro

%macro IDCT2_1D_2 3 ; out, in, stride
    MATVEC_MUL_2    %1, %2, %3, vvc_dct2_4_even_mat
%endmacro

; Performs a single type-II DCT with length 4
; 
; %1 Index of a SIMD register in which to store the result.
;    Result is stored as 4 packed doublewords.
;
; %2 Memory address of input data.
;
; %3 Difference in memory address between input elements in bytes.
;
; %4 Index of scratch SIMD register.
%macro IDCT2_1D_4 4 ; out, in, stride, temp
    %push
    %define %$out %1
    %define %$in %2
    %define %$stride %3
    %define %$temp %4

    lea             stride3q, [%$stride*3]

    ; even part
    movd            xm%$out, [%$in + 0*%$stride*4]
    punpckldq       m%$out, [%2 + 2*%$stride*4]
    punpcklqdq      m%$out, m%$out
    packssdw        m%$out, m%$out
    pmaddwd         m%$out, [vvc_dct2_4_even_mat]

    ; odd part
    movd            xm%$temp, [%$in + 1*%$stride*4]
    punpckldq       m%$temp, [%$in + stride3q*4]
    punpcklqdq      m%$temp, m%$temp
    packssdw        m%$temp, m%$temp
    pmaddwd         m%$temp, [vvc_dct2_4_odd_mat]

    paddd           m%$out, m%$temp

    %pop
%endmacro

; Performs a single type-II DCT with length 8
; 
; %1 Index of SIMD register. Stores first half of output in order as
;    packed doublewords:
;    | y0 | y1 | y2 | y3 |
;
; %2 Index of SIMD register. Stores second half of output in reverse order as
;    packed doublewords:
;    | y7 | y6 | y5 | y4 |
;
; %3 Memory address of input data. Modified by macro.
;
; %4 Difference in memory address between input elements in bytes.
;    Modified by macro.
;
; %5 Index of scratch SIMD register.
%macro IDCT2_1D_8 5 ; out[2], in, stride, temp
    %push
    %define %$out0 %1
    %define %$out1 %2
    %define %$in %3
    %define %$stride %4
    %define %$temp %5

    lea             %$stride, [%$stride*2]

    ; even part
    IDCT2_1D_4      %$out0, %$in, %$stride, %$temp

    ; odd part
    lea             %$in, [%$in + %$stride*2]
    MATVEC_MUL_4    %$out1, %$in, %$stride, vvc_dct2_8_odd_mat, %$temp

    SUMSUB_BA       d, %$out1, %$out0, %$temp

    %pop
%endmacro

; Performs a single type-II DCT with length 16
; 
; %1 Index of SIMD register. Stores first quarter of output in order as
;    packed doublewords:
;    | y0 | y1 | y2 | y3 |
;
; %2 Index of SIMD register. Stores second quarter of output in reverse order as
;    packed doublewords:
;    | y7 | y6 | y5 | y4 |
;
; %3 Index of SIMD register. Stores third quarter of output in order as
;    packed doublewords:
;    | y8 | y9 | y10 | y11 |
;
; %4 Index of SIMD register. Stores fourth quarter of output in reverse order as
;    packed doublewords:
;    | y15 | y14 | y13 | y12 |
;
; %5 Memory address of input data. Modified by macro.
;
; %6 Difference in memory address between input elements in bytes.
;    Modified by macro.
;
; %7 Index of scratch SIMD register.
;
; %8 Index of scratch SIMD register.
%macro IDCT2_1D_16 8 ; out[4], in, stride, temp[2]
    %push
    %define %$out0 %1
    %define %$out1 %2
    %define %$out2 %3
    %define %$out3 %4
    %define %$in %5
    %define %$stride %6
    %define %$temp0 %7
    %define %$temp1 %8

    lea             %$stride, [%$stride*2]

    ; even part
    push            %$in
    push            %$stride
    IDCT2_1D_8      %$out0, %$out1, %$in, %$stride, %$temp0
    pop             %$stride
    pop             %$in

    ; odd part
    lea             %$in, [%$in + %$stride*2]
    MATVEC_MUL_8    %$out2, %$out3, %$in, %$stride, vvc_dct2_16_odd_mat, %$temp0, %$temp1

    SUMSUB_BADC     d, %$out3, %$out0, %$out2, %$out1, %$temp0

    %pop
%endmacro

; Performs a single type-II DCT with length 32
; 
; %1  Index of SIMD register. Stores first eighth of output in order as
;     packed doublewords:
;     | y0 | y1 | y2 | y3 |
;
; %2  Index of SIMD register. Stores second eighth of output in reverse order as
;     packed doublewords:
;     | y7 | y6 | y5 | y4 |
;
; %3  Index of SIMD register. Stores third eighth of output in order as
;     packed doublewords:
;     | y8 | y9 | y10 | y11 |
;
; %4  Index of SIMD register. Stores fourth eighth of output in reverse order as
;     packed doublewords:
;     | y15 | y14 | y13 | y12 |
;
; %5  Index of SIMD register. Stores fifth eighth of output in order as
;     packed doublewords:
;     | y16 | y17 | y18 | y19 |
;
; %6  Index of SIMD register. Stores sixth eighth of output in reverse order as
;     packed doublewords:
;     | y23 | y22 | y21 | y20 |
;
; %7  Index of SIMD register. Stores seventh eighth of output in order as
;     packed doublewords:
;     | y24 | y25 | y26 | y27 |
;
; %8  Index of SIMD register. Stores eigth eighth of output in order as
;     packed doublewords:
;     | y28 | y29 | y30 | y31 |
;
; %9  Memory address of input data. Modified by macro.
;
; %10 Difference in memory address between input elements in bytes.
;    Modified by macro.
;
; %11 Index of scratch SIMD register.

; %12 Index of scratch SIMD register.
%macro IDCT2_1D_32 12 ; out[8], in, stride, temp[2]
    %push
    %define %$out0 %1
    %define %$out1 %2
    %define %$out2 %3
    %define %$out3 %4
    %define %$out4 %5
    %define %$out5 %6
    %define %$out6 %7
    %define %$out7 %8
    %define %$in %9
    %define %$stride %10
    %define %$temp0 %11
    %define %$temp1 %12
    %define %$temp2 %13
    %define %$temp3 %14

    lea             %$stride, [%$stride*2]

    ; even part
    push            %$in
    push            %$stride
    IDCT2_1D_16     %$out0, %$out1, %$out2, %$out3, %$in, %$stride, %$temp0, %$temp1
    pop             %$stride
    pop             %$in

    ; odd part
    lea             %$in, [%$in + %$stride*2]
    MATVEC_MUL_16   %$out4, %$out5, %$out6, %$out7, %$in, %$stride, vvc_dct2_32_odd_mat, %$temp0, %$temp1

    SUMSUB_BADC     d, %$out7, %$out0, %$out6, %$out1, %$temp0
    SUMSUB_BADC     d, %$out5, %$out2, %$out4, %$out3, %$temp0

    %pop
%endmacro

; void ff_vvc_inv_dct2_2_avx2(int *out,       ptrdiff_t out_stride,
;                             const int *in,  ptrdiff_t in_stride);
cglobal vvc_inv_dct2_2, 4, 5, 3, out, out_stride, in, in_stride, \
                                 stride3
    IDCT2_1D_2      0, inq, in_strideq

    pextrd          [outq + 0*out_strideq*4], xm0, 0
    pextrd          [outq + 1*out_strideq*4], xm0, 2
    RET

; void ff_vvc_inv_dct2_4_avx2(int *out,       ptrdiff_t out_stride,
;                             const int *in,  ptrdiff_t in_stride);
cglobal vvc_inv_dct2_4, 4, 5, 3, out, out_stride, in, in_stride, \
                                 stride3
    IDCT2_1D_4      0, inq, in_strideq, 1

    lea             stride3q, [in_strideq*3]
    pextrd          [outq + 0*out_strideq*4], xm0, 0
    pextrd          [outq + 1*out_strideq*4], xm0, 1
    pextrd          [outq + 2*out_strideq*4], xm0, 2
    pextrd          [outq + stride3q*4], xm0, 3
    RET

; void ff_vvc_inv_dct2_8_avx2(int *out,       ptrdiff_t out_stride,
;                             const int *in,  ptrdiff_t in_stride);
cglobal vvc_inv_dct2_8, 4, 5, 3, out, out_stride, in, in_stride, \
                                 stride3
    IDCT2_1D_8      0, 1, inq, in_strideq, 2

    pextrd          [outq + 0*out_strideq*4], xm0, 0
    pextrd          [outq + 1*out_strideq*4], xm0, 1
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm0, 2
    pextrd          [outq + 1*out_strideq*4], xm0, 3
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm1, 3
    pextrd          [outq + 1*out_strideq*4], xm1, 2
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm1, 1
    pextrd          [outq + 1*out_strideq*4], xm1, 0
    RET

; void ff_vvc_inv_dct2_16_avx2(int *out,       ptrdiff_t out_stride,
;                              const int *in,  ptrdiff_t in_stride);
cglobal vvc_inv_dct2_16, 4, 5, 6, out, out_stride, in, in_stride, \
                                  stride3
    IDCT2_1D_16      0, 1, 2, 3, inq, in_strideq, 4, 5

    pextrd          [outq + 0*out_strideq*4], xm0, 0
    pextrd          [outq + 1*out_strideq*4], xm0, 1
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm0, 2
    pextrd          [outq + 1*out_strideq*4], xm0, 3
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm1, 3
    pextrd          [outq + 1*out_strideq*4], xm1, 2
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm1, 1
    pextrd          [outq + 1*out_strideq*4], xm1, 0
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm2, 0
    pextrd          [outq + 1*out_strideq*4], xm2, 1
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm2, 2
    pextrd          [outq + 1*out_strideq*4], xm2, 3
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm3, 3
    pextrd          [outq + 1*out_strideq*4], xm3, 2
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm3, 1
    pextrd          [outq + 1*out_strideq*4], xm3, 0
    RET

; void ff_vvc_inv_dct2_32_avx2(int *out,       ptrdiff_t out_stride,
;                              const int *in,  ptrdiff_t in_stride);
cglobal vvc_inv_dct2_32, 4, 5, 12, out, out_stride, in, in_stride, \
                                  stride3
    IDCT2_1D_32     0, 1, 2, 3, 4, 5, 6, 7, inq, in_strideq, 8, 9

    pextrd          [outq + 0*out_strideq*4], xm0, 0
    pextrd          [outq + 1*out_strideq*4], xm0, 1
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm0, 2
    pextrd          [outq + 1*out_strideq*4], xm0, 3
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm1, 3
    pextrd          [outq + 1*out_strideq*4], xm1, 2
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm1, 1
    pextrd          [outq + 1*out_strideq*4], xm1, 0
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm2, 0
    pextrd          [outq + 1*out_strideq*4], xm2, 1
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm2, 2
    pextrd          [outq + 1*out_strideq*4], xm2, 3
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm3, 3
    pextrd          [outq + 1*out_strideq*4], xm3, 2
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm3, 1
    pextrd          [outq + 1*out_strideq*4], xm3, 0
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm4, 0
    pextrd          [outq + 1*out_strideq*4], xm4, 1
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm4, 2
    pextrd          [outq + 1*out_strideq*4], xm4, 3
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm5, 3
    pextrd          [outq + 1*out_strideq*4], xm5, 2
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm5, 1
    pextrd          [outq + 1*out_strideq*4], xm5, 0
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm6, 0
    pextrd          [outq + 1*out_strideq*4], xm6, 1
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm6, 2
    pextrd          [outq + 1*out_strideq*4], xm6, 3
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm7, 3
    pextrd          [outq + 1*out_strideq*4], xm7, 2
    lea             outq, [outq + 2*out_strideq*4]
    pextrd          [outq + 0*out_strideq*4], xm7, 1
    pextrd          [outq + 1*out_strideq*4], xm7, 0
    RET
