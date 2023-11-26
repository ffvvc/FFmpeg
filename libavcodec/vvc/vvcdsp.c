/*
 * VVC DSP
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

#include "vvcdsp.h"
#include "vvc_ctu.h"
#include "vvc_itx_1d.h"

#define VVC_SIGN(v) (v < 0 ? -1 : !!v)

static void av_always_inline pad_int16(int16_t *_dst, const ptrdiff_t dst_stride, const int width, const int height)
{
    const int padded_width = width + 2;
    int16_t *dst;
    for (int y = 0; y < height; y++) {
        dst = _dst + y * dst_stride;
        for (int x = 0; x < width; x++) {
            dst[-1] = dst[0];
            dst[width] = dst[width - 1];
        }
    }

    _dst--;
    //top
    memcpy(_dst - dst_stride, _dst, padded_width * sizeof(int16_t));
    //bottom
    _dst += dst_stride * height;
    memcpy(_dst, _dst - dst_stride, padded_width * sizeof(int16_t));
}

static int vvc_sad(const int16_t *src0, const int16_t *src1, int dx, int dy,
    const int block_w, const int block_h)
{
    int sad = 0;
    dx -= 2;
    dy -= 2;
    src0 += (2 + dy) * MAX_PB_SIZE + 2 + dx;
    src1 += (2 - dy) * MAX_PB_SIZE + 2 - dx;
    for (int y = 0; y < block_h; y += 2) {
        for (int x = 0; x < block_w; x++) {
            sad += FFABS(src0[x] - src1[x]);
        }
        src0 += 2 * MAX_PB_SIZE;
        src1 += 2 * MAX_PB_SIZE;
    }
    return sad;
}

static void scale_clip(int *coeff, const int nzw, const int w, const int h,
    const int shift, const int log2_transform_range)
{
    const int add = 1 << (shift - 1);
    for (int y = 0; y < h; y++) {
        int *p = coeff + y * w;
        for (int x = 0; x < nzw; x++) {
            *p = av_clip_intp2((*p + add) >> shift, log2_transform_range);
            p++;
        }
        memset(p, 0, sizeof(*p) * (w - nzw));
    }
}

static void scale(int *c, const int w, const int h, const int shift)
{
    const int add = 1 << (shift - 1);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            *c = (*c + add) >> shift;
            c++;
        }
    }
}

//transmatrix[0][0]
#define DCT_A 64
static void itx_2d(int *coeffs,
    const int w, const int h, const size_t nzw, const size_t nzh,
    const intptr_t log2_transform_range, const intptr_t bd,
    const vvc_itx_1d_fn first_1d_fn, const vvc_itx_1d_fn second_1d_fn, const int has_dconly)
{
    const int shift[]   = { 7, 5 + log2_transform_range - bd };

    if (w == h && nzw == 1 && nzh == 1 && has_dconly) {
        const int add[] = { 1 << (shift[0] - 1), 1 << (shift[1] - 1) };
        const int t     = (coeffs[0] * DCT_A + add[0]) >> shift[0];
        const int dc    = (t * DCT_A + add[1]) >> shift[1];
        for (int i = 0; i < w * h; i++)
            coeffs[i] = dc;
        return;
    }

    for (int x = 0; x < nzw; x++)
        first_1d_fn(coeffs + x, w, nzh);
    scale_clip(coeffs, nzw, w, h, shift[0], log2_transform_range);

    for (int y = 0; y < h; y++)
        second_1d_fn(coeffs + y * w, 1, nzw);
    scale(coeffs, w, h, shift[1]);
}

static void itx_1d(int *coeffs,
    const int w, const int h, const size_t nzw, const size_t nzh,
    const intptr_t log2_transform_range, const intptr_t bd,
    const vvc_itx_1d_fn first_1d_fn, const vvc_itx_1d_fn second_1d_fn, const int has_dconly)
{
    const int shift = 6 + log2_transform_range - bd;
    if (nzw == 1 && nzh == 1 && has_dconly) {
        const int add   = 1 << (shift - 1);
        const int dc    = (coeffs[0] * DCT_A + add) >> shift;
        for (int i = 0; i < w * h; i++)
            coeffs[i] = dc;
        return;
    }

    if (w > 1)
        second_1d_fn(coeffs, 1, nzw);
    else
        first_1d_fn(coeffs, 1, nzh);
    scale(coeffs, w, h, shift);
}

#define itx_fn(type1, type2, w, h, has_dconly)                                                  \
static void itx_##type1##_##type2##_##w##x##h(int *coeffs,                                      \
    const size_t nzw, const size_t nzh, const intptr_t log2_transform_range, const intptr_t bd) \
{                                                                                               \
    if (w > 1 && h > 1)                                                                         \
        itx_2d(coeffs, w, h, nzw, nzh, log2_transform_range, bd,                                \
            ff_vvc_inv_##type2##_##h, ff_vvc_inv_##type1##_##w, has_dconly);                    \
    else                                                                                        \
        itx_1d(coeffs, w, h, nzw, nzh, log2_transform_range, bd,                                \
            ff_vvc_inv_##type2##_##h, ff_vvc_inv_##type1##_##w, has_dconly);                    \
}

#define itx_fn_1d_type(type, s, has_dconly)                                     \
    itx_fn(type, dct2, s, 1, has_dconly)                                        \
    itx_fn(dct2, type, 1, s, has_dconly)                                        \

#define itx_fn_1d(s)                                                            \
    itx_fn_1d_type(dct2, s, 1)                                                  \
    itx_fn_1d_type(dct8, s, 0)                                                  \
    itx_fn_1d_type(dst7, s, 0)                                                  \

#define itx_fn_height_4_to_32(type1, type2, w, has_dconly)                      \
    itx_fn(type1, type2, w,  4, has_dconly)                                     \
    itx_fn(type1, type2, w,  8, has_dconly)                                     \
    itx_fn(type1, type2, w, 16, has_dconly)                                     \
    itx_fn(type1, type2, w, 32, has_dconly)                                     \

#define itx_fn_dxtn_height(type, w, has_dconly)                                 \
    itx_fn(type, dct2, w,  2, has_dconly)                                       \
    itx_fn(type, dct2, w, 64, has_dconly)                                       \
    itx_fn_height_4_to_32(type, dct2, w, has_dconly)                            \
    itx_fn_height_4_to_32(type, dct8, w, 0)                                     \
    itx_fn_height_4_to_32(type, dst7, w, 0)                                     \

// for width 2 and 64, we can only do dct2
#define itx_fn_dct2(w)                                                          \
    itx_fn_dxtn_height(dct2, w, 1)                                              \

// for width 4, 8, 16, 32, we can do dct2, dct8 and dst7
#define itx_fn_dxtn(w)                                                          \
    itx_fn_dct2(w)                                                              \
    itx_fn_dxtn_height(dct8, w, 0)                                              \
    itx_fn_dxtn_height(dst7, w, 0)                                              \

// 1d
itx_fn_1d(16)
itx_fn_1d(32)
itx_fn_1d_type(dct2, 64, 1)

// 2d
itx_fn_dct2(2)
itx_fn_dxtn(4)
itx_fn_dxtn(8)
itx_fn_dxtn(16)
itx_fn_dxtn(32)
itx_fn_dct2(64)

typedef struct IntraEdgeParams {
    uint8_t* top;
    uint8_t* left;
    int filter_flag;

    uint16_t left_array[3 * MAX_TB_SIZE + 3];
    uint16_t filtered_left_array[3 * MAX_TB_SIZE + 3];
    uint16_t top_array[3 * MAX_TB_SIZE + 3];
    uint16_t filtered_top_array[3 * MAX_TB_SIZE + 3];
} IntraEdgeParams;

#define PROF_BORDER_EXT         1
#define PROF_BLOCK_SIZE         (AFFINE_MIN_BLOCK_SIZE + PROF_BORDER_EXT * 2)
#define BDOF_BORDER_EXT         1

#define BDOF_PADDED_SIZE        (16 + BDOF_BORDER_EXT * 2)
#define BDOF_BLOCK_SIZE         4
#define BDOF_GRADIENT_SIZE      (BDOF_BLOCK_SIZE + BDOF_BORDER_EXT * 2)

#define BIT_DEPTH 8
#include "vvcdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "vvcdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 12
#include "vvcdsp_template.c"
#undef BIT_DEPTH

void ff_vvc_dsp_init(VVCDSPContext *vvcdsp, int bit_depth)
{
#undef FUNC
#define FUNC(a, depth) a ## _ ## depth

#define VVC_DSP(depth)                                                          \
    FUNC(ff_vvc_inter_dsp_init, depth)(&vvcdsp->inter);                         \
    FUNC(ff_vvc_intra_dsp_init, depth)(&vvcdsp->intra);                         \
    FUNC(ff_vvc_itx_dsp_init, depth)(&vvcdsp->itx);                             \
    FUNC(ff_vvc_lmcs_dsp_init, depth)(&vvcdsp->lmcs);                           \
    FUNC(ff_vvc_lf_dsp_init, depth)(&vvcdsp->lf);                               \
    FUNC(ff_vvc_sao_dsp_init, depth)(&vvcdsp->sao);                             \
    FUNC(ff_vvc_alf_dsp_init, depth)(&vvcdsp->alf);                             \

    switch (bit_depth) {
    case 12:
        VVC_DSP(12);
        break;
    case 10:
        VVC_DSP(10);
        break;
    default:
        VVC_DSP(8);
        break;
    }
#if ARCH_X86
    ff_vvc_dsp_init_x86(vvcdsp, bit_depth);
#elif ARCH_AARCH64
    ff_vvc_dsp_init_aarch64(vvcdsp, bit_depth);
#endif
}
