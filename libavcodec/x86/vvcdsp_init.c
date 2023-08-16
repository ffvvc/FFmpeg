/*
 * VVC DSP init for x86
 *
 * Copyright (C) 2022-2023 Nuo Mi
 * Copyright (c) 2023 Wu Jianhua <toqsxw@outlook.com>
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

#include "config.h"

#include "libavutil/cpu.h"
#include "libavcodec/vvc/vvcdec.h"
#include "libavcodec/vvc/vvcdsp.h"
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/x86/vvcdsp.h"
#include <stdlib.h>
#include <time.h>

#define PIXEL_MAX_8  ((1 << 8)  - 1)
#define PIXEL_MAX_10 ((1 << 10) - 1)
#define PIXEL_MAX_12 ((1 << 12) - 1)

static void alf_filter_luma_8_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    const int param_stride  = (width >> 2) * ALF_NUM_COEFF_LUMA;
    ff_vvc_alf_filter_luma_8bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, param_stride, vb_pos, PIXEL_MAX_8);
}

static void alf_filter_luma_16bpc_avx2(uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *src, const ptrdiff_t src_stride, const int width, const int height,
    const int16_t *filter, const int16_t *clip, const int vb_pos, const int pixel_max)
{
    const int param_stride  = (width >> 2) * ALF_NUM_COEFF_LUMA;
    ff_vvc_alf_filter_luma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, param_stride, vb_pos, pixel_max);
}

static void alf_filter_luma_10_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    alf_filter_luma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, vb_pos, PIXEL_MAX_10);
}

static void alf_filter_luma_12_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    alf_filter_luma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, vb_pos, PIXEL_MAX_12);
}

static void alf_filter_chroma_8_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    ff_vvc_alf_filter_chroma_8bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, 0, vb_pos, PIXEL_MAX_8);
}

static void alf_filter_chroma_16bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos, const int pixel_max)
{
    ff_vvc_alf_filter_chroma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, 0, vb_pos, pixel_max);
}

static void alf_filter_chroma_10_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    alf_filter_chroma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, vb_pos, PIXEL_MAX_10);
}

static void alf_filter_chroma_12_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    alf_filter_chroma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, vb_pos, PIXEL_MAX_12);
}

static void alf_classify_8_avx2(int *class_idx, int *transpose_idx,
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, int *gradient_tmp)
{
    ff_vvc_alf_classify_grad_8bpc_avx2(gradient_tmp, src, src_stride, width, height, vb_pos);
    ff_vvc_alf_classify_8bpc_avx2(class_idx, transpose_idx, gradient_tmp, width, height, vb_pos, 8);
}

static void alf_classify_10_avx2(int *class_idx, int *transpose_idx,
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, int *gradient_tmp)
{
    ff_vvc_alf_classify_grad_16bpc_avx2(gradient_tmp, src, src_stride, width, height, vb_pos);
    ff_vvc_alf_classify_16bpc_avx2(class_idx, transpose_idx, gradient_tmp, width, height, vb_pos, 10);
}

static void alf_classify_12_avx2(int *class_idx, int *transpose_idx,
    const uint8_t *src, ptrdiff_t src_stride, int width, int height, int vb_pos, int *gradient_tmp)
{
    ff_vvc_alf_classify_grad_16bpc_avx2(gradient_tmp, src, src_stride, width, height, vb_pos);
    ff_vvc_alf_classify_16bpc_avx2(class_idx, transpose_idx, gradient_tmp, width, height, vb_pos, 12);
}

#define ALF_DSP(depth) do {                                                     \
        c->alf.filter[LUMA] = alf_filter_luma_##depth##_avx2;                   \
        c->alf.filter[CHROMA] = alf_filter_chroma_##depth##_avx2;               \
        c->alf.classify = alf_classify_##depth##_avx2;                          \
    } while (0)


#define SAO_BAND_FILTER_FUNCS(bitd, opt)                                                                                        \
void ff_vvc_sao_band_filter_8_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,  \
                                              const int16_t *sao_offset_val, int sao_left_class, int width, int height);        \
void ff_vvc_sao_band_filter_16_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src, \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);       \
void ff_vvc_sao_band_filter_32_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src, \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);       \
void ff_vvc_sao_band_filter_48_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src, \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);       \
void ff_vvc_sao_band_filter_64_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src, \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);       \
void ff_vvc_sao_band_filter_80_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src, \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);       \
void ff_vvc_sao_band_filter_96_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src, \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);       \
void ff_vvc_sao_band_filter_112_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,    \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);           \
void ff_vvc_sao_band_filter_128_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t _stride_dst, ptrdiff_t _stride_src,    \
                                               const int16_t *sao_offset_val, int sao_left_class, int width, int height);           \

SAO_BAND_FILTER_FUNCS(8,  avx2)
SAO_BAND_FILTER_FUNCS(10, avx2)
SAO_BAND_FILTER_FUNCS(12, avx2)

#define SAO_BAND_INIT(bitd, opt) do {                                       \
    c->sao.band_filter[0]   = ff_vvc_sao_band_filter_8_##bitd##_##opt;      \
    c->sao.band_filter[1]   = ff_vvc_sao_band_filter_16_##bitd##_##opt;     \
    c->sao.band_filter[2]   = ff_vvc_sao_band_filter_32_##bitd##_##opt;     \
    c->sao.band_filter[3]   = ff_vvc_sao_band_filter_48_##bitd##_##opt;     \
    c->sao.band_filter[4]   = ff_vvc_sao_band_filter_64_##bitd##_##opt;     \
    c->sao.band_filter[5]   = ff_vvc_sao_band_filter_80_##bitd##_##opt;     \
    c->sao.band_filter[6]   = ff_vvc_sao_band_filter_96_##bitd##_##opt;     \
    c->sao.band_filter[7]   = ff_vvc_sao_band_filter_112_##bitd##_##opt;    \
    c->sao.band_filter[8]   = ff_vvc_sao_band_filter_128_##bitd##_##opt;    \
} while (0)

#define SAO_EDGE_FILTER_FUNCS(bitd, opt)                                                                      \
void ff_vvc_sao_edge_filter_8_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,        \
                                              const int16_t *sao_offset_val, int eo, int width, int height);  \
void ff_vvc_sao_edge_filter_16_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,       \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \
void ff_vvc_sao_edge_filter_32_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,       \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \
void ff_vvc_sao_edge_filter_48_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,       \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \
void ff_vvc_sao_edge_filter_64_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,       \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \
void ff_vvc_sao_edge_filter_80_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,       \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \
void ff_vvc_sao_edge_filter_96_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,       \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \
void ff_vvc_sao_edge_filter_112_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,      \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \
void ff_vvc_sao_edge_filter_128_##bitd##_##opt(uint8_t *_dst, const uint8_t *_src, ptrdiff_t stride_dst,      \
                                               const int16_t *sao_offset_val, int eo, int width, int height); \


SAO_EDGE_FILTER_FUNCS(8, avx2)
SAO_EDGE_FILTER_FUNCS(10, avx2)
SAO_EDGE_FILTER_FUNCS(12, avx2)

#define SAO_EDGE_INIT(bitd, opt) do {                                           \
    c->sao.edge_filter[0]       = ff_vvc_sao_edge_filter_8_##bitd##_##opt;      \
    c->sao.edge_filter[1]       = ff_vvc_sao_edge_filter_16_##bitd##_##opt;     \
    c->sao.edge_filter[2]       = ff_vvc_sao_edge_filter_32_##bitd##_##opt;     \
    c->sao.edge_filter[3]       = ff_vvc_sao_edge_filter_48_##bitd##_##opt;     \
    c->sao.edge_filter[4]       = ff_vvc_sao_edge_filter_64_##bitd##_##opt;     \
    c->sao.edge_filter[5]       = ff_vvc_sao_edge_filter_80_##bitd##_##opt;     \
    c->sao.edge_filter[6]       = ff_vvc_sao_edge_filter_96_##bitd##_##opt;     \
    c->sao.edge_filter[7]       = ff_vvc_sao_edge_filter_112_##bitd##_##opt;    \
    c->sao.edge_filter[8]       = ff_vvc_sao_edge_filter_128_##bitd##_##opt;    \
} while (0)

#define PUT_VVC_LUMA_8_FUNC(dir, opt)                                                                         \
    void ff_vvc_put_vvc_luma_##dir##_8_##opt(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,  \
    const int height, const intptr_t mx, const intptr_t my, const int width,                                  \
    const int hf_idx, const int vf_idx);                                                                      \

#define PUT_VVC_LUMA_16_FUNC(dir, opt)                                                                        \
    void ff_vvc_put_vvc_luma_##dir##_16_##opt(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride, \
    const int height, const intptr_t mx, const intptr_t my, const int width,                                  \
    const int hf_idx, const int vf_idx, const int bitdepth);

#define PUT_VVC_LUMA_FUNCS(bitd, opt)    \
    PUT_VVC_LUMA_##bitd##_FUNC(h,  opt)  \
    PUT_VVC_LUMA_##bitd##_FUNC(v,  opt)  \
    PUT_VVC_LUMA_##bitd##_FUNC(hv, opt)

#define PUT_VVC_LUMA_FORWARD_FUNC(dir, bitd, opt)                                                                      \
static void ff_vvc_put_vvc_luma_##dir##_##bitd##_##opt(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride, \
    const int height, const intptr_t mx, const intptr_t my, const int width,                                           \
    const int hf_idx, const int vf_idx)                                                                                \
{                                                                                                                      \
    ff_vvc_put_vvc_luma_##dir##_16_##opt(dst, _src, _src_stride, height, mx, my, width, hf_idx, vf_idx, bitd);         \
}

#define PUT_VVC_LUMA_FORWARD_FUNCS(bitd, opt) \
    PUT_VVC_LUMA_FORWARD_FUNC(h,  bitd, opt)  \
    PUT_VVC_LUMA_FORWARD_FUNC(v,  bitd, opt)  \
    PUT_VVC_LUMA_FORWARD_FUNC(hv, bitd, opt)

PUT_VVC_LUMA_FUNCS(8,  avx2)
PUT_VVC_LUMA_FUNCS(16, avx2)
PUT_VVC_LUMA_FORWARD_FUNCS(10, avx2)
PUT_VVC_LUMA_FORWARD_FUNCS(12, avx2)

#if HAVE_AVX512ICL_EXTERNAL
PUT_VVC_LUMA_FUNCS(16, avx512icl)
PUT_VVC_LUMA_FORWARD_FUNCS(10, avx512icl)
PUT_VVC_LUMA_FORWARD_FUNCS(12, avx512icl)
#endif

#define PUT_VVC_LUMA_INIT(bitd, opt) do {                             \
    c->inter.put[LUMA][0][1] = ff_vvc_put_vvc_luma_h_##bitd##_##opt;  \
    c->inter.put[LUMA][1][0] = ff_vvc_put_vvc_luma_v_##bitd##_##opt;  \
    c->inter.put[LUMA][1][1] = ff_vvc_put_vvc_luma_hv_##bitd##_##opt; \
} while (0)

#define ITX_COMMON_SIZES(TYPE_H, type_h, TYPE_V, type_v, bitd, opt)             \
    ITX(TYPE_H, type_h, TYPE_V, type_v, 4, 4, bitd, opt);                       \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 4, 8, bitd, opt); */                 \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 4, 16, bitd, opt); */                \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 8, 4, bitd, opt); */                 \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 8, 8, bitd, opt); */                 \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 8, 16, bitd, opt); */                \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 8, 32, bitd, opt); */                \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 16, 4, bitd, opt); */                \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 16, 8, bitd, opt); */                \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 16, 16, bitd, opt); */               \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 16, 32, bitd, opt); */               \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 32, 8, bitd, opt); */                \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 32, 16, bitd, opt); */               \
    /* ITX(TYPE_H, type_h, TYPE_V, type_v, 32, 32, bitd, opt); */

#define ITX_SIZES(bitd, opt)                                                    \
    ITX_COMMON_SIZES(DCT2, dct2, DCT2, dct2, bitd, opt);                        \
    /* ITX(DCT2, dct2, DCT2, dct2, 16, 64, bitd, opt); */                       \
    /* ITX(DCT2, dct2, DCT2, dct2, 32, 64, bitd, opt); */                       \
    /* ITX(DCT2, dct2, DCT2, dct2, 64, 16, bitd, opt); */                       \
    /* ITX(DCT2, dct2, DCT2, dct2, 64, 32, bitd, opt); */                       \
    /* ITX(DCT2, dct2, DCT2, dct2, 64, 64, bitd, opt); */

#define ITX(TYPE_H, type_h, TYPE_V, type_v, width, height, bitd, opt) \
void ff_vvc_inv_##type_h##_##type_v##_##width##x##height##_##bitd##_##opt( \
    int16_t *dst, const int *coeff, int nzw, int log2_transform_range);
/* ITX_SIZES(8, avx2) */
ITX_SIZES(10, avx2)

#undef ITX
#define ITX(TYPE_H, type_h, TYPE_V, type_v, width, height, bitd, opt) \
    c->itx.itx[TYPE_H][TYPE_V][TX_SIZE_##width][TX_SIZE_##height] = ff_vvc_inv_##type_h##_##type_v##_##width##x##height##_##bitd##_##opt;

#define ITX_INIT(bitd, opt) do { \
    ITX_SIZES(bitd, opt)         \
} while (0)

void ff_vvc_dsp_init_x86(VVCDSPContext *const c, const int bit_depth)
{
    const int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AVX2(cpu_flags)) {
        switch (bit_depth) {
            case 8:
                ALF_DSP(8);
                PUT_VVC_LUMA_INIT(8, avx2);
                /* ITX_INIT(8, avx2); */
                c->sao.band_filter[0] = ff_vvc_sao_band_filter_8_8_avx2;
                c->sao.band_filter[1] = ff_vvc_sao_band_filter_16_8_avx2;
                break;
            case 10:
                ALF_DSP(10);
                PUT_VVC_LUMA_INIT(10, avx2);
                ITX_INIT(10, avx2);
                c->sao.band_filter[0] = ff_vvc_sao_band_filter_8_10_avx2;
                break;
            case 12:
                ALF_DSP(12);
                PUT_VVC_LUMA_INIT(12, avx2);
                break;
            default:
                break;
        }
    }
    if (EXTERNAL_AVX2_FAST(cpu_flags)) {
        switch (bit_depth) {
            case 8:
                SAO_BAND_INIT(8, avx2);
                SAO_EDGE_INIT(8, avx2);
                break;
            case 10:
                SAO_BAND_INIT(10, avx2);
                SAO_EDGE_INIT(10, avx2);
                break;
            case 12:
                SAO_BAND_INIT(12, avx2);
                SAO_EDGE_INIT(12, avx2);
            default:
                break;
        }
    }
#if HAVE_AVX512ICL_EXTERNAL
    if (EXTERNAL_AVX512ICL(cpu_flags)) {
        switch (bit_depth) {
            case 10:
                PUT_VVC_LUMA_INIT(10, avx512icl);
                break;
            case 12:
                PUT_VVC_LUMA_INIT(12, avx512icl);
                break;
            default:
            break;
        }
    }
#endif
}
