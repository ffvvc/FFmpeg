/*
 * VVC transform and residual DSP
 *
 * Copyright (C) 2021 Nuo Mi
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "libavcodec/bit_depth_template.c"

#include "vvcdec.h"
#include "vvc_itx_1d.h"

#include "vvc_inter_template.c"
#include "vvc_intra_template.c"
#include "vvc_filter_template.c"

static void FUNC(add_residual)(uint8_t *_dst, const int16_t *res,
    const int w, const int h, const ptrdiff_t _stride)
{
    pixel *dst          = (pixel *)_dst;

    const int stride    = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            dst[x] = av_clip_pixel(dst[x] + res[x * h + y]);
        }
        dst += stride;
    }
}

static void FUNC(add_residual_joint)(uint8_t *_dst, const int16_t *res,
    const int w, const int h, const ptrdiff_t _stride, const int c_sign, const int shift)
{
    pixel *dst = (pixel *)_dst;

    const int stride = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int r = (res[x * h + y] * c_sign) >> shift;
            dst[x] = av_clip_pixel(dst[x] + r);
        }
        dst += stride;
    }
}

static void FUNC(pred_residual_joint)(int16_t *buf, const int w, const int h,
    const int c_sign, const int shift)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            *buf = ((*buf) * c_sign) >> shift;
            buf++;
        }
    }
}

static void FUNC(transform_bdpcm)(int *coeffs, const int width, const int height,
    const int vertical, const int log2_transform_range)
{
    int x, y;

    if (vertical) {
        coeffs += width;
        for (y = 0; y < height - 1; y++) {
            for (x = 0; x < width; x++)
                coeffs[x] = av_clip_intp2(coeffs[x] + coeffs[x - width], log2_transform_range);
            coeffs += width;
        }
    } else {
        for (y = 0; y < height; y++) {
            for (x = 1; x < width; x++)
                coeffs[x] = av_clip_intp2(coeffs[x] + coeffs[x - 1], log2_transform_range);
            coeffs += width;
        }
    }
}

#define ITX_COMMON_SIZES(TYPE_H, type_h, TYPE_V, type_v)                        \
    ITX_1D_V(TYPE_H, type_h, TYPE_V, type_v, 1, 4);                             \
    ITX_1D_V(TYPE_H, type_h, TYPE_V, type_v, 1, 8);                             \
    ITX_1D_V(TYPE_H, type_h, TYPE_V, type_v, 1, 16);                            \
    ITX_1D_V(TYPE_H, type_h, TYPE_V, type_v, 1, 32);                            \
    ITX_1D_H(TYPE_H, type_h, TYPE_V, type_v, 4, 1);                             \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 4, 4);                               \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 4, 8);                               \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 4, 16);                              \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 4, 32);                              \
    ITX_1D_H(TYPE_H, type_h, TYPE_V, type_v, 8, 1);                             \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 8, 4);                               \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 8, 8);                               \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 8, 16);                              \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 8, 32);                              \
    ITX_1D_H(TYPE_H, type_h, TYPE_V, type_v, 16, 1);                            \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 16, 4);                              \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 16, 8);                              \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 16, 16);                             \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 16, 32);                             \
    ITX_1D_H(TYPE_H, type_h, TYPE_V, type_v, 32, 1);                            \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 32, 4);                              \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 32, 8);                              \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 32, 16);                             \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, 32, 32);

#define ITX \
    ITX_COMMON_SIZES(DCT2, dct2, DCT2, dct2);                                   \
    ITX_COMMON_SIZES(DCT2, dct2, DST7, dst7);                                   \
    ITX_COMMON_SIZES(DCT2, dct2, DCT8, dct8);                                   \
    ITX_COMMON_SIZES(DST7, dst7, DCT2, dct2);                                   \
    ITX_COMMON_SIZES(DST7, dst7, DST7, dst7);                                   \
    ITX_COMMON_SIZES(DST7, dst7, DCT8, dct8);                                   \
    ITX_COMMON_SIZES(DCT8, dct8, DCT2, dct2);                                   \
    ITX_COMMON_SIZES(DCT8, dct8, DST7, dst7);                                   \
    ITX_COMMON_SIZES(DCT8, dct8, DCT8, dct8);                                   \
    ITX_1D_V(DCT2, dct2, DCT2, dct2, 1, 2);                                     \
    ITX_1D_V(DCT2, dct2, DCT2, dct2, 1, 64);                                    \
    ITX_1D_H(DCT2, dct2, DCT2, dct2, 2, 1);                                     \
    ITX_2D(DCT2, dct2, DCT2, dct2, 2, 2);                                       \
    ITX_2D(DCT2, dct2, DCT2, dct2, 2, 4);                                       \
    ITX_2D(DCT2, dct2, DCT2, dct2, 2, 8);                                       \
    ITX_2D(DCT2, dct2, DCT2, dct2, 2, 16);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 2, 32);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 2, 64);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 4, 2);                                       \
    ITX_2D(DCT2, dct2, DCT2, dct2, 4, 64);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 8, 2);                                       \
    ITX_2D(DCT2, dct2, DCT2, dct2, 8, 64);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 16, 2);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 16, 64);                                     \
    ITX_2D(DCT2, dct2, DCT2, dct2, 32, 2);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 32, 64);                                     \
    ITX_1D_H(DCT2, dct2, DCT2, dct2, 64, 1);                                    \
    ITX_2D(DCT2, dct2, DCT2, dct2, 64, 2);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 64, 4);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 64, 8);                                      \
    ITX_2D(DCT2, dct2, DCT2, dct2, 64, 16);                                     \
    ITX_2D(DCT2, dct2, DCT2, dct2, 64, 32);                                     \
    ITX_2D(DCT2, dct2, DCT2, dct2, 64, 64);                                     \
    ITX_1D_H(DCT2, dct2, DST7, dst7, 2, 1);                                     \
    ITX_2D(DCT2, dct2, DST7, dst7, 2, 4);                                       \
    ITX_2D(DCT2, dct2, DST7, dst7, 2, 8);                                       \
    ITX_2D(DCT2, dct2, DST7, dst7, 2, 16);                                      \
    ITX_2D(DCT2, dct2, DST7, dst7, 2, 32);                                      \
    ITX_1D_H(DCT2, dct2, DST7, dst7, 64, 1);                                    \
    ITX_2D(DCT2, dct2, DST7, dst7, 64, 4);                                      \
    ITX_2D(DCT2, dct2, DST7, dst7, 64, 8);                                      \
    ITX_2D(DCT2, dct2, DST7, dst7, 64, 16);                                     \
    ITX_2D(DCT2, dct2, DST7, dst7, 64, 32);                                     \
    ITX_1D_H(DCT2, dct2, DCT8, dct8, 2, 1);                                     \
    ITX_2D(DCT2, dct2, DCT8, dct8, 2, 4);                                       \
    ITX_2D(DCT2, dct2, DCT8, dct8, 2, 8);                                       \
    ITX_2D(DCT2, dct2, DCT8, dct8, 2, 16);                                      \
    ITX_2D(DCT2, dct2, DCT8, dct8, 2, 32);                                      \
    ITX_1D_H(DCT2, dct2, DCT8, dct8, 64, 1);                                    \
    ITX_2D(DCT2, dct2, DCT8, dct8, 64, 4);                                      \
    ITX_2D(DCT2, dct2, DCT8, dct8, 64, 8);                                      \
    ITX_2D(DCT2, dct2, DCT8, dct8, 64, 16);                                     \
    ITX_2D(DCT2, dct2, DCT8, dct8, 64, 32);                                     \
    ITX_1D_V(DST7, dst7, DCT2, dct2, 1, 2);                                     \
    ITX_2D(DST7, dst7, DCT2, dct2, 4, 2);                                       \
    ITX_2D(DST7, dst7, DCT2, dct2, 8, 2);                                       \
    ITX_2D(DST7, dst7, DCT2, dct2, 16, 2);                                      \
    ITX_2D(DST7, dst7, DCT2, dct2, 32, 2);                                      \
    ITX_1D_V(DST7, dst7, DCT2, dct2, 1, 64);                                    \
    ITX_2D(DST7, dst7, DCT2, dct2, 4, 64);                                      \
    ITX_2D(DST7, dst7, DCT2, dct2, 8, 64);                                      \
    ITX_2D(DST7, dst7, DCT2, dct2, 16, 64);                                     \
    ITX_2D(DST7, dst7, DCT2, dct2, 32, 64);                                     \
    ITX_1D_V(DCT8, dct8, DCT2, dct2, 1, 2);                                     \
    ITX_2D(DCT8, dct8, DCT2, dct2, 4, 2);                                       \
    ITX_2D(DCT8, dct8, DCT2, dct2, 8, 2);                                       \
    ITX_2D(DCT8, dct8, DCT2, dct2, 16, 2);                                      \
    ITX_2D(DCT8, dct8, DCT2, dct2, 32, 2);                                      \
    ITX_1D_V(DCT8, dct8, DCT2, dct2, 1, 64);                                    \
    ITX_2D(DCT8, dct8, DCT2, dct2, 4, 64);                                      \
    ITX_2D(DCT8, dct8, DCT2, dct2, 8, 64);                                      \
    ITX_2D(DCT8, dct8, DCT2, dct2, 16, 64);                                     \
    ITX_2D(DCT8, dct8, DCT2, dct2, 32, 64);

// ITX function prototypes
#undef ITX_2D
#define ITX_2D(TYPE_H, type_h, TYPE_V, type_v, width, height)                   \
static void FUNC(inv_##type_h##_##type_v##_##width##x##height)(int16_t *dst,    \
    const int *coeff, int nzw, int log2_transform_range)                        \
{                                                                               \
    DECLARE_ALIGNED(32, int, temp)[width * height];                             \
    DECLARE_ALIGNED(32, int, temp2)[width * height];                            \
                                                                                \
    for (int x = 0; x < nzw; x++)                                               \
        ff_vvc_inv_##type_v##_##height(temp + x, width, coeff + x, width);      \
                                                                                \
    scale_clip(temp, nzw, width, height, 7, log2_transform_range);              \
                                                                                \
    for (int y = 0; y < height; y++)                                            \
        ff_vvc_inv_##type_h##_##width(temp2 + y, height, temp + y * width, 1);  \
                                                                                \
    scale(dst, temp2, width, height, 5 + log2_transform_range - BIT_DEPTH);     \
}
#undef ITX_1D_H
#define ITX_1D_H(TYPE_H, type_h, TYPE_V, type_v, width, height)                 \
static void FUNC(inv_##type_h##_##type_v##_##width##x##height)(int16_t *dst,    \
    const int *coeff, int nzw, int log2_transform_range)                        \
{                                                                               \
    DECLARE_ALIGNED(32, int, temp)[width * height];                             \
                                                                                \
    ff_vvc_inv_##type_h##_##width(temp, 1, coeff, 1);                           \
    scale(dst, temp, width, height, 6 + log2_transform_range - BIT_DEPTH);      \
}
#undef ITX_1D_V
#define ITX_1D_V(TYPE_H, type_h, TYPE_V, type_v, width, height)                 \
static void FUNC(inv_##type_h##_##type_v##_##width##x##height)(int16_t *dst,    \
    const int *coeff, int nzw, int log2_transform_range)                        \
{                                                                               \
    DECLARE_ALIGNED(32, int, temp)[width * height];                             \
                                                                                \
    ff_vvc_inv_##type_v##_##height(temp, 1, coeff, 1);                          \
    scale(dst, temp, width, height, 6 + log2_transform_range - BIT_DEPTH);      \
}
ITX

static void FUNC(ff_vvc_itx_dsp_init)(VVCItxDSPContext *const itx)
{
    itx->add_residual                = FUNC(add_residual);
    itx->add_residual_joint          = FUNC(add_residual_joint);
    itx->pred_residual_joint         = FUNC(pred_residual_joint);
    itx->transform_bdpcm             = FUNC(transform_bdpcm);
#undef ITX_2D
#define ITX_2D(TYPE_H, type_h, TYPE_V, type_v, width, height)                   \
    itx->itx[TYPE_H][TYPE_V][TX_SIZE_##width][TX_SIZE_##height] = FUNC(inv_##type_h##_##type_v##_##width##x##height);
#undef ITX_1D_H
#define ITX_1D_H(TYPE_H, type_h, TYPE_V, type_v, width, height)                 \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, width, height)
#undef ITX_1D_V
#define ITX_1D_V(TYPE_H, type_h, TYPE_V, type_v, width, height)                 \
    ITX_2D(TYPE_H, type_h, TYPE_V, type_v, width, height)
    ITX

#undef VVC_ITX
#undef VVC_ITX_COMMON
}
