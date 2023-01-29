/*
 * VVC video decoder
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

DECLARE_ALIGNED(16, const int8_t, ff_vvc_chroma_filters)[3][32][4] = {
    {
        //1x, Table 33
        {  0, 64,  0,  0 },
        { -1, 63,  2,  0 },
        { -2, 62,  4,  0 },
        { -2, 60,  7, -1 },
        { -2, 58, 10, -2 },
        { -3, 57, 12, -2 },
        { -4, 56, 14, -2 },
        { -4, 55, 15, -2 },
        { -4, 54, 16, -2 },
        { -5, 53, 18, -2 },
        { -6, 52, 20, -2 },
        { -6, 49, 24, -3 },
        { -6, 46, 28, -4 },
        { -5, 44, 29, -4 },
        { -4, 42, 30, -4 },
        { -4, 39, 33, -4 },
        { -4, 36, 36, -4 },
        { -4, 33, 39, -4 },
        { -4, 30, 42, -4 },
        { -4, 29, 44, -5 },
        { -4, 28, 46, -6 },
        { -3, 24, 49, -6 },
        { -2, 20, 52, -6 },
        { -2, 18, 53, -5 },
        { -2, 16, 54, -4 },
        { -2, 15, 55, -4 },
        { -2, 14, 56, -4 },
        { -2, 12, 57, -3 },
        { -2, 10, 58, -2 },
        { -1,  7, 60, -2 },
        {  0,  4, 62, -2 },
        {  0,  2, 63, -1 },
    },
    {
        //1.5x, Table 34
        { 12, 40, 12,  0 },
        { 11, 40, 13,  0 },
        { 10, 40, 15, -1 },
        {  9, 40, 16, -1 },
        {  8, 40, 17, -1 },
        {  8, 39, 18, -1 },
        {  7, 39, 19, -1 },
        {  6, 38, 21, -1 },
        {  5, 38, 22, -1 },
        {  4, 38, 23, -1 },
        {  4, 37, 24, -1 },
        {  3, 36, 25,  0 },
        {  3, 35, 26,  0 },
        {  2, 34, 28,  0 },
        {  2, 33, 29,  0 },
        {  1, 33, 30,  0 },
        {  1, 31, 31,  1 },
        {  0, 30, 33,  1 },
        {  0, 29, 33,  2 },
        {  0, 28, 34,  2 },
        {  0, 26, 35,  3 },
        {  0, 25, 36,  3 },
        { -1, 24, 37,  4 },
        { -1, 23, 38,  4 },
        { -1, 22, 38,  5 },
        { -1, 21, 38,  6 },
        { -1, 19, 39,  7 },
        { -1, 18, 39,  8 },
        { -1, 17, 40,  8 },
        { -1, 16, 40,  9 },
        { -1, 15, 40, 10 },
        {  0, 13, 40, 11 },
    },
    {
        //2x, Table 35
        { 17, 30, 17,  0 },
        { 17, 30, 18, -1 },
        { 16, 30, 18,  0 },
        { 16, 30, 18,  0 },
        { 15, 30, 18,  1 },
        { 14, 30, 18,  2 },
        { 13, 29, 19,  3 },
        { 13, 29, 19,  3 },
        { 12, 29, 20,  3 },
        { 11, 28, 21,  4 },
        { 10, 28, 22,  4 },
        { 10, 27, 22,  5 },
        {  9, 27, 23,  5 },
        {  9, 26, 24,  5 },
        {  8, 26, 24,  6 },
        {  7, 26, 25,  6 },
        {  7, 25, 25,  7 },
        {  6, 25, 26,  7 },
        {  6, 24, 26,  8 },
        {  5, 24, 26,  9 },
        {  5, 23, 27,  9 },
        {  5, 22, 27, 10 },
        {  4, 22, 28, 10 },
        {  4, 21, 28, 11 },
        {  3, 20, 29, 12 },
        {  3, 19, 29, 13 },
        {  3, 19, 29, 13 },
        {  2, 18, 30, 14 },
        {  1, 18, 30, 15 },
        {  0, 18, 30, 16 },
        {  0, 18, 30, 16 },
        { -1, 18, 30, 17 },
    },
};

DECLARE_ALIGNED(16, const int8_t, ff_vvc_luma_filters)[3][16][8] = {
    {
        //1x, hpelIfIdx == 0, Table 27
        {  0, 0,   0, 64,  0,   0,  0,  0 },
        {  0, 1,  -3, 63,  4,  -2,  1,  0 },
        { -1, 2,  -5, 62,  8,  -3,  1,  0 },
        { -1, 3,  -8, 60, 13,  -4,  1,  0 },
        { -1, 4, -10, 58, 17,  -5,  1,  0 },
        { -1, 4, -11, 52, 26,  -8,  3, -1 },
        { -1, 3,  -9, 47, 31, -10,  4, -1 },
        { -1, 4, -11, 45, 34, -10,  4, -1 },
        { -1, 4, -11, 40, 40, -11,  4, -1 },
        { -1, 4, -10, 34, 45, -11,  4, -1 },
        { -1, 4, -10, 31, 47,  -9,  3, -1 },
        { -1, 3,  -8, 26, 52, -11,  4, -1 },
        {  0, 1,  -5, 17, 58, -10,  4, -1 },
        {  0, 1,  -4, 13, 60,  -8,  3, -1 },
        {  0, 1,  -3,  8, 62,  -5,  2, -1 },
        {  0, 1,  -2,  4, 63,  -3,  1,  0 },
    },

    {
        //1x, hpelIfIdx == 1, Table 27
        {  0, 0,   0, 64,  0,   0,  0,  0 },
        {  0, 1,  -3, 63,  4,  -2,  1,  0 },
        { -1, 2,  -5, 62,  8,  -3,  1,  0 },
        { -1, 3,  -8, 60, 13,  -4,  1,  0 },
        { -1, 4, -10, 58, 17,  -5,  1,  0 },
        { -1, 4, -11, 52, 26,  -8,  3, -1 },
        { -1, 3,  -9, 47, 31, -10,  4, -1 },
        { -1, 4, -11, 45, 34, -10,  4, -1 },
        {  0, 3,   9, 20, 20,   9,  3,  0 },
        { -1, 4, -10, 34, 45, -11,  4, -1 },
        { -1, 4, -10, 31, 47,  -9,  3, -1 },
        { -1, 3,  -8, 26, 52, -11,  4, -1 },
        {  0, 1,  -5, 17, 58, -10,  4, -1 },
        {  0, 1,  -4, 13, 60,  -8,  3, -1 },
        {  0, 1,  -3,  8, 62,  -5,  2, -1 },
        {  0, 1,  -2,  4, 63,  -3,  1,  0 },
    },

    {
        //1x, affine, Table 30
        {  0, 0,   0, 64,  0,   0,  0,  0 },
        {  0, 1,  -3, 63,  4,  -2,  1,  0 },
        {  0, 1,  -5, 62,  8,  -3,  1,  0 },
        {  0, 2,  -8, 60, 13,  -4,  1,  0 },
        {  0, 3, -10, 58, 17,  -5,  1,  0 },
        {  0, 3, -11, 52, 26,  -8,  2,  0 },
        {  0, 2,  -9, 47, 31, -10,  3,  0 },
        {  0, 3, -11, 45, 34, -10,  3,  0 },
        {  0, 3, -11, 40, 40, -11,  3,  0 },
        {  0, 3, -10, 34, 45, -11,  3,  0 },
        {  0, 3, -10, 31, 47,  -9,  2,  0 },
        {  0, 2,  -8, 26, 52, -11,  3,  0 },
        {  0, 1,  -5, 17, 58, -10,  3,  0 },
        {  0, 1,  -4, 13, 60,  -8,  2,  0 },
        {  0, 1,  -3,  8, 62,  -5,  1,  0 },
        {  0, 1,  -2,  4, 63,  -3,  1,  0 },
    },

};

DECLARE_ALIGNED(16, const int8_t, ff_vvc_dmvr_filters)[16][2] = {
    { 16,  0},
    { 15,  1},
    { 14,  2},
    { 13,  3},
    { 12,  4},
    { 11,  5},
    { 10,  6},
    {  9,  7},
    {  8,  8},
    {  7,  9},
    {  6, 10},
    {  5, 11},
    {  4, 12},
    {  3, 13},
    {  2, 14},
    {  1, 15},
};

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

#define itx_fn(type, s)                                                                         \
static void itx_##type##_##s(int *out, ptrdiff_t out_step, const int *in, ptrdiff_t in_step)    \
{                                                                                               \
  ff_vvc_inv_##type##_##s(out, out_step, in, in_step);                                          \
}

#define itx_fn_common(type) \
    itx_fn(type, 4);        \
    itx_fn(type, 8);        \
    itx_fn(type, 16);       \
    itx_fn(type, 32);       \

itx_fn_common(dct2);
itx_fn_common(dst7);
itx_fn_common(dct8);
itx_fn(dct2, 2);
itx_fn(dct2, 64);

typedef struct IntraEdgeParams {
    uint8_t* top;
    uint8_t* left;
    int filter_flag;

    uint16_t left_array[3 * MAX_TB_SIZE + 3];
    uint16_t filtered_left_array[3 * MAX_TB_SIZE + 3];
    uint16_t top_array[3 * MAX_TB_SIZE + 3];
    uint16_t filtered_top_array[3 * MAX_TB_SIZE + 3];
} IntraEdgeParams;

#define BIT_DEPTH 8
#include "vvcdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
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
    case 10:
        VVC_DSP(10);
        break;
    default:
        VVC_DSP(8);
        break;
    }
}
