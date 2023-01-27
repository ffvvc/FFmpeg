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

#define BIT_DEPTH 8
#include "vvcdsp_template.c"
#undef BIT_DEPTH

#define BIT_DEPTH 10
#include "vvcdsp_template.c"
#undef BIT_DEPTH

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


void ff_vvc_dsp_init(VVCDSPContext *vvcdsp, int bit_depth)
{
#undef FUNC
#define FUNC(a, depth) a ## _ ## depth


#undef PEL_FUNC
#define PEL_FUNC(dst, idx1, idx2, a, depth)                                     \
    vvcdsp->dst[idx1][idx2] = a ## _ ## depth;                                  \

#undef CHROMA_FUNCS
#define CHROMA_FUNCS(depth)                                                     \
    PEL_FUNC(put_vvc_chroma, 0, 0, put_vvc_pel_pixels, depth);                  \
    PEL_FUNC(put_vvc_chroma, 0, 1, put_vvc_chroma_h, depth);                    \
    PEL_FUNC(put_vvc_chroma, 1, 0, put_vvc_chroma_v, depth);                    \
    PEL_FUNC(put_vvc_chroma, 1, 1, put_vvc_chroma_hv, depth)

#undef CHROMA_UNI_FUNCS
#define CHROMA_UNI_FUNCS(depth)                                                 \
    PEL_FUNC(put_vvc_chroma_uni, 0, 0, put_vvc_pel_uni_pixels, depth);          \
    PEL_FUNC(put_vvc_chroma_uni, 0, 1, put_vvc_chroma_uni_h, depth);            \
    PEL_FUNC(put_vvc_chroma_uni, 1, 0, put_vvc_chroma_uni_v, depth);            \
    PEL_FUNC(put_vvc_chroma_uni, 1, 1, put_vvc_chroma_uni_hv, depth);           \
    PEL_FUNC(put_vvc_chroma_uni_w, 0, 0, put_vvc_pel_uni_w_pixels, depth);      \
    PEL_FUNC(put_vvc_chroma_uni_w, 0, 1, put_vvc_chroma_uni_w_h, depth);        \
    PEL_FUNC(put_vvc_chroma_uni_w, 1, 0, put_vvc_chroma_uni_w_v, depth);        \
    PEL_FUNC(put_vvc_chroma_uni_w, 1, 1, put_vvc_chroma_uni_w_hv, depth)

#undef CHROMA_BI_FUNCS
#define CHROMA_BI_FUNCS(depth)                                                  \
    PEL_FUNC(put_vvc_chroma_bi, 0, 0, put_vvc_pel_bi_pixels, depth);            \
    PEL_FUNC(put_vvc_chroma_bi, 0, 1, put_vvc_chroma_bi_h, depth);              \
    PEL_FUNC(put_vvc_chroma_bi, 1, 0, put_vvc_chroma_bi_v, depth);              \
    PEL_FUNC(put_vvc_chroma_bi, 1, 1, put_vvc_chroma_bi_hv, depth);             \
    PEL_FUNC(put_vvc_chroma_bi_w, 0, 0, put_vvc_pel_bi_w_pixels, depth);        \
    PEL_FUNC(put_vvc_chroma_bi_w, 0, 1, put_vvc_chroma_bi_w_h, depth);          \
    PEL_FUNC(put_vvc_chroma_bi_w, 1, 0, put_vvc_chroma_bi_w_v, depth);          \
    PEL_FUNC(put_vvc_chroma_bi_w, 1, 1, put_vvc_chroma_bi_w_hv, depth)

#undef LUMA_FUNCS
#define LUMA_FUNCS(depth)                                                       \
    PEL_FUNC(put_vvc_luma, 0, 0, put_vvc_pel_pixels, depth);                    \
    PEL_FUNC(put_vvc_luma, 0, 1, put_vvc_luma_h, depth);                        \
    PEL_FUNC(put_vvc_luma, 1, 0, put_vvc_luma_v, depth);                        \
    PEL_FUNC(put_vvc_luma, 1, 1, put_vvc_luma_hv, depth)


#undef LUMA_UNI_FUNCS
#define LUMA_UNI_FUNCS(depth)                                                   \
    PEL_FUNC(put_vvc_luma_uni, 0, 0, put_vvc_pel_uni_pixels, depth);            \
    PEL_FUNC(put_vvc_luma_uni, 0, 1, put_vvc_luma_uni_h, depth);                \
    PEL_FUNC(put_vvc_luma_uni, 1, 0, put_vvc_luma_uni_v, depth);                \
    PEL_FUNC(put_vvc_luma_uni, 1, 1, put_vvc_luma_uni_hv, depth);               \
    PEL_FUNC(put_vvc_luma_uni_w, 0, 0, put_vvc_pel_uni_w_pixels, depth);        \
    PEL_FUNC(put_vvc_luma_uni_w, 0, 1, put_vvc_luma_uni_w_h, depth);            \
    PEL_FUNC(put_vvc_luma_uni_w, 1, 0, put_vvc_luma_uni_w_v, depth);            \
    PEL_FUNC(put_vvc_luma_uni_w, 1, 1, put_vvc_luma_uni_w_hv, depth)

#undef LUMA_BI_FUNCS
#define LUMA_BI_FUNCS(depth)                                                    \
    PEL_FUNC(put_vvc_luma_bi, 0, 0, put_vvc_pel_bi_pixels, depth);              \
    PEL_FUNC(put_vvc_luma_bi, 0, 1, put_vvc_luma_bi_h, depth);                  \
    PEL_FUNC(put_vvc_luma_bi, 1, 0, put_vvc_luma_bi_v, depth);                  \
    PEL_FUNC(put_vvc_luma_bi, 1, 1, put_vvc_luma_bi_hv, depth);                 \
    PEL_FUNC(put_vvc_luma_bi_w, 0, 0, put_vvc_pel_bi_w_pixels, depth);          \
    PEL_FUNC(put_vvc_luma_bi_w, 0, 1, put_vvc_luma_bi_w_h, depth);              \
    PEL_FUNC(put_vvc_luma_bi_w, 1, 0, put_vvc_luma_bi_w_v, depth);              \
    PEL_FUNC(put_vvc_luma_bi_w, 1, 1, put_vvc_luma_bi_w_hv, depth)

#undef DMVR_FUNCS
#define DMVR_FUNCS(depth)                                                       \
        PEL_FUNC(dmvr_vvc_luma, 0, 0, dmvr_vvc_luma, depth);                    \
        PEL_FUNC(dmvr_vvc_luma, 0, 1, dmvr_vvc_luma_h, depth);                  \
        PEL_FUNC(dmvr_vvc_luma, 1, 0, dmvr_vvc_luma_v, depth);                  \
        PEL_FUNC(dmvr_vvc_luma, 1, 1, dmvr_vvc_luma_hv, depth)

#define VVC_ITX(TYPE, type, s)                                                  \
    vvcdsp->itx[TYPE][TX_SIZE_##s]      = itx_##type##_##s;                     \

#define VVC_ITX_COMMON(TYPE, type)                                              \
    VVC_ITX(TYPE, type, 4);                                                     \
    VVC_ITX(TYPE, type, 8);                                                     \
    VVC_ITX(TYPE, type, 16);                                                    \
    VVC_ITX(TYPE, type, 32);                                                    \

#define VVC_ITX_DCT2()

#define VVC_DSP(depth)                                                          \
    vvcdsp->add_residual                = FUNC(add_residual, depth);            \
    vvcdsp->add_residual_joint          = FUNC(add_residual_joint, depth);      \
    vvcdsp->pred_residual_joint         = FUNC(pred_residual_joint, depth);     \
    vvcdsp->transform_bdpcm             = FUNC(transform_bdpcm, depth);         \
    vvcdsp->fetch_samples               = FUNC(fetch_samples, depth);           \
    vvcdsp->bdof_fetch_samples          = FUNC(bdof_fetch_samples, depth);      \
    vvcdsp->apply_prof                  = FUNC(apply_prof, depth);              \
    vvcdsp->apply_prof_uni              = FUNC(apply_prof_uni, depth);          \
    vvcdsp->apply_prof_uni_w            = FUNC(apply_prof_uni_w, depth);        \
    vvcdsp->apply_prof_bi               = FUNC(apply_prof_bi, depth);           \
    vvcdsp->apply_prof_bi_w             = FUNC(apply_prof_bi_w, depth);         \
    vvcdsp->apply_bdof                  = FUNC(apply_bdof, depth);              \
    vvcdsp->prof_grad_filter            = FUNC(prof_grad_filter, depth);        \
    vvcdsp->vvc_sad                     = vvc_sad;                              \
    vvcdsp->sao_band_filter[0] =                                                \
    vvcdsp->sao_band_filter[1] =                                                \
    vvcdsp->sao_band_filter[2] =                                                \
    vvcdsp->sao_band_filter[3] =                                                \
    vvcdsp->sao_band_filter[4] =                                                \
    vvcdsp->sao_band_filter[5] =                                                \
    vvcdsp->sao_band_filter[6] =                                                \
    vvcdsp->sao_band_filter[7] =                                                \
    vvcdsp->sao_band_filter[8] = FUNC(sao_band_filter, depth);                  \
    vvcdsp->sao_edge_filter[0] =                                                \
    vvcdsp->sao_edge_filter[1] =                                                \
    vvcdsp->sao_edge_filter[2] =                                                \
    vvcdsp->sao_edge_filter[3] =                                                \
    vvcdsp->sao_edge_filter[4] =                                                \
    vvcdsp->sao_edge_filter[5] =                                                \
    vvcdsp->sao_edge_filter[6] =                                                \
    vvcdsp->sao_edge_filter[7] =                                                \
    vvcdsp->sao_edge_filter[8] = FUNC(sao_edge_filter, depth);                  \
    vvcdsp->sao_edge_restore[0] = FUNC(sao_edge_restore_0, depth);              \
    vvcdsp->sao_edge_restore[1] = FUNC(sao_edge_restore_1, depth);              \
                                                                                \
    vvcdsp->alf_filter_luma         = FUNC(alf_filter_luma, depth);             \
    vvcdsp->alf_filter_luma_vb      = FUNC(alf_filter_luma_vb, depth);          \
    vvcdsp->alf_filter_chroma       = FUNC(alf_filter_chroma, depth);           \
    vvcdsp->alf_filter_chroma_vb    = FUNC(alf_filter_chroma_vb, depth);        \
    vvcdsp->alf_filter_cc           = FUNC(alf_filter_cc, depth);               \
    vvcdsp->alf_get_coeff_and_clip  = FUNC(alf_get_coeff_and_clip, depth);      \
                                                                                \
    vvcdsp->lmcs_filter_luma    = FUNC(lmcs_filter_luma, depth);                \
                                                                                \
    VVC_ITX(DCT2, dct2, 2)                                                      \
    VVC_ITX(DCT2, dct2, 64)                                                     \
    VVC_ITX_COMMON(DCT2, dct2)                                                  \
    VVC_ITX_COMMON(DCT8, dct8)                                                  \
    VVC_ITX_COMMON(DST7, dst7)                                                  \
    LUMA_FUNCS(depth)                                                           \
    LUMA_UNI_FUNCS(depth)                                                       \
    CHROMA_FUNCS(depth)                                                         \
    CHROMA_UNI_FUNCS(depth)                                                     \
    LUMA_BI_FUNCS(depth)                                                        \
    CHROMA_BI_FUNCS(depth)                                                      \
    DMVR_FUNCS(depth)                                                           \
                                                                                \
    vvcdsp->vvc_h_loop_ladf_level      = FUNC(vvc_h_loop_ladf_level, depth);    \
    vvcdsp->vvc_v_loop_ladf_level      = FUNC(vvc_v_loop_ladf_level, depth);    \
    vvcdsp->vvc_h_loop_filter_luma     = FUNC(vvc_h_loop_filter_luma, depth);   \
    vvcdsp->vvc_v_loop_filter_luma     = FUNC(vvc_v_loop_filter_luma, depth);   \
    vvcdsp->vvc_h_loop_filter_chroma   = FUNC(vvc_h_loop_filter_chroma, depth); \
    vvcdsp->vvc_v_loop_filter_chroma   = FUNC(vvc_v_loop_filter_chroma, depth); \
    vvcdsp->vvc_h_loop_filter_luma_c   = FUNC(vvc_h_loop_filter_luma, depth);   \
    vvcdsp->vvc_v_loop_filter_luma_c   = FUNC(vvc_v_loop_filter_luma, depth);   \
    vvcdsp->vvc_h_loop_filter_chroma_c = FUNC(vvc_h_loop_filter_chroma, depth); \
    vvcdsp->vvc_v_loop_filter_chroma_c = FUNC(vvc_v_loop_filter_chroma, depth); \
    vvcdsp->put_vvc_ciip               = FUNC(put_vvc_ciip, depth);             \
    vvcdsp->put_vvc_gpm                = FUNC(put_vvc_gpm, depth);              \

    switch (bit_depth) {
    case 10:
        VVC_DSP(10);
        break;
    default:
        VVC_DSP(8);
        break;
    }
}
