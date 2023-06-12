/*
 * VVC DSP init for x86
 *
 * Copyright (C) 2022 Nuo Mi
 *
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

#define PIXEL_MAX_8  ((1 << 8)  - 1)
#define PIXEL_MAX_10 ((1 << 10) - 1)
#define PIXEL_MAX_12 ((1 << 12) - 1)

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

static void alf_filter_luma_8_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    const int param_stride  = (width >> 2) * ALF_NUM_COEFF_LUMA;
    ff_vvc_alf_filter_luma_8bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, param_stride, vb_pos, PIXEL_MAX_8);
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

static void alf_filter_chroma_8_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int16_t *filter, const int16_t *clip, const int vb_pos)
{
    ff_vvc_alf_filter_chroma_8bpc_avx2(dst, dst_stride, src, src_stride, width, height,
        filter, clip, 0, vb_pos, PIXEL_MAX_8);
}

static void alf_classify_8_avx2(int *class_idx, int *transpose_idx,
    const uint8_t *src, ptrdiff_t src_stride, int width, int height,
    int vb_pos, int *gradient_tmp)
{
    ff_vvc_alf_classify_grad_8bpc_avx2(gradient_tmp, src, src_stride, width, height, vb_pos);
    ff_vvc_alf_classify_8bpc_avx2(class_idx, transpose_idx, gradient_tmp, width, height, vb_pos, 8);
}

static void alf_classify_10_avx2(int *class_idx, int *transpose_idx,
    const uint8_t *src, ptrdiff_t src_stride, int width, int height,
    int vb_pos, int *gradient_tmp)
{
    ff_vvc_alf_classify_grad_16bpc_avx2(gradient_tmp, src, src_stride, width, height, vb_pos);
    ff_vvc_alf_classify_16bpc_avx2(class_idx, transpose_idx, gradient_tmp, width, height, vb_pos, 10);
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

void ff_vvc_dsp_init_x86(VVCDSPContext *const c, const int bit_depth)
{
    const int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AVX2(cpu_flags)) {
        switch (bit_depth) {
            case 8:
                ALF_DSP(8);
                c->sao.band_filter[0] = ff_vvc_sao_band_filter_8_8_avx2;
                c->sao.band_filter[1] = ff_vvc_sao_band_filter_16_8_avx2;
                break;
            case 10:
                ALF_DSP(10);
                c->sao.band_filter[0] = ff_vvc_sao_band_filter_8_10_avx2;
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
}
