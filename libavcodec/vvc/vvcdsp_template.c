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
#include "libavutil/frame.h"
#include "libavcodec/bit_depth_template.c"

#include "vvcdec.h"
#include "vvc_data.h"

#include "vvc_inter_template.c"
#include "vvc_intra_template.c"
#include "vvc_filter_template.c"

static void FUNC(add_residual)(uint8_t *_dst, const int *res,
    const int w, const int h, const ptrdiff_t _stride)
{
    pixel *dst          = (pixel *)_dst;

    const int stride    = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            dst[x] = av_clip_pixel(dst[x] + *res);
            res++;
        }
        dst += stride;
    }
}

static void FUNC(add_residual_joint)(uint8_t *_dst, const int *res,
    const int w, const int h, const ptrdiff_t _stride, const int c_sign, const int shift)
{
    pixel *dst = (pixel *)_dst;

    const int stride = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int r = ((*res) * c_sign) >> shift;
            dst[x] = av_clip_pixel(dst[x] + r);
            res++;
        }
        dst += stride;
    }
}

static void FUNC(pred_residual_joint)(int *buf, const int w, const int h,
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

#define ITX_1D_TYPE(TYPE, type, s)                                              \
    do {                                                                        \
        const int log2 = av_log2(s);                                            \
        itx->itx[TYPE][DCT2][log2][0] = itx_##type##_##dct2##_##s##x##1;        \
        itx->itx[DCT2][TYPE][0][log2] = itx_##dct2##_##type##_##1##x##s;        \
    } while (0)

#define ITX_1D(s)                                                               \
    do {                                                                        \
        ITX_1D_TYPE(DCT2, dct2, s);                                             \
        ITX_1D_TYPE(DCT8, dct8, s);                                             \
        ITX_1D_TYPE(DST7, dst7, s);                                             \
    } while (0)

#define ITX_HEIGHT_4_TO_32(TYPE1, TYPE2, type1, type2, w)                       \
    do {                                                                        \
        const int log2 = av_log2(w);                                            \
        itx->itx[TYPE1][TYPE2][log2][2] = itx_##type1##_##type2##_##w##x##4;    \
        itx->itx[TYPE1][TYPE2][log2][3] = itx_##type1##_##type2##_##w##x##8;    \
        itx->itx[TYPE1][TYPE2][log2][4] = itx_##type1##_##type2##_##w##x##16;   \
        itx->itx[TYPE1][TYPE2][log2][5] = itx_##type1##_##type2##_##w##x##32;   \
    } while (0)

#define ITX_DXTN_HEIGHT(TYPE, type, w)                                          \
    do {                                                                        \
        const int log2 = av_log2(w);                                            \
        itx->itx[TYPE][DCT2][log2][1] = itx_##type##_##dct2##_##w##x##2;        \
        itx->itx[TYPE][DCT2][log2][6] = itx_##type##_##dct2##_##w##x##64;       \
        ITX_HEIGHT_4_TO_32(TYPE, DCT2, type, dct2, w);                          \
        ITX_HEIGHT_4_TO_32(TYPE, DCT8, type, dct8, w);                          \
        ITX_HEIGHT_4_TO_32(TYPE, DST7, type, dst7, w);                          \
    } while (0)

#define ITX_DCT2(w)                                                             \
    do {                                                                        \
        ITX_DXTN_HEIGHT(DCT2, dct2, w);                                         \
    } while (0)

#define ITX_DXTN(w)                                                             \
    do {                                                                        \
        ITX_DCT2(w);                                                            \
        ITX_DXTN_HEIGHT(DCT8, dct8, w);                                         \
        ITX_DXTN_HEIGHT(DST7, dst7, w);                                         \
    } while (0)

static void FUNC(ff_vvc_itx_dsp_init)(VVCItxDSPContext *const itx)
{
    itx->add_residual                = FUNC(add_residual);
    itx->add_residual_joint          = FUNC(add_residual_joint);
    itx->pred_residual_joint         = FUNC(pred_residual_joint);
    itx->transform_bdpcm             = FUNC(transform_bdpcm);

    ITX_1D(16);
    ITX_1D(32);
    ITX_1D_TYPE(DCT2, dct2, 64);

    ITX_DCT2(2);
    ITX_DXTN(4);
    ITX_DXTN(8);
    ITX_DXTN(16);
    ITX_DXTN(32);
    ITX_DCT2(64);
}

#undef ITX_1D_TYPE
#undef ITX_1D
#undef ITX_HEIGHT_4_TO_32
#undef ITX_DXTN_HEIGHT
#undef ITX_DCT2
#undef ITX_DXTN
