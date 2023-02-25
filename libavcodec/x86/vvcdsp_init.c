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
#include "libavutil/x86/asm.h"
#include "libavutil/x86/cpu.h"
#include "libavcodec/vvcdec.h"
#include "libavcodec/vvcdsp.h"
#include "libavcodec/x86/vvcdsp.h"

#define PIXEL_MAX_10 ((1 << 10) - 1)
#define PIXEL_MAX_12 ((1 << 12) - 1)

static void alf_filter_luma_16bpc_avx2(uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *src, const ptrdiff_t src_stride, const int width, const int height,
    const int8_t *filter, const int16_t *clip, const int pixel_max)
{
    const int ps            = 1;                                    //pixel shift
    const int param_stride  = (width >> 2) * ALF_NUM_COEFF_LUMA;
    int w;

    for (w = 0; w + 16 <= width; w += 16) {
        const int param_offset = w * ALF_NUM_COEFF_LUMA / ALF_BLOCK_SIZE;
        ff_vvc_alf_filter_luma_w16_16bpc_avx2(dst + (w << ps), dst_stride, src + (w << ps), src_stride,
            height, filter + param_offset, clip + param_offset, param_stride, pixel_max);
    }
    for ( /* nothing */; w < width; w += 4) {
        const int param_offset = w * ALF_NUM_COEFF_LUMA / ALF_BLOCK_SIZE;
        ff_vvc_alf_filter_luma_w4_16bpc_avx2(dst + (w << ps), dst_stride, src + (w << ps), src_stride,
            height, filter + param_offset, clip + param_offset, param_stride, pixel_max);
    }
}

static void alf_filter_luma_10_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int8_t *filter, const int16_t *clip)
{
    alf_filter_luma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height, filter, clip, PIXEL_MAX_10);
}

static void alf_filter_chroma_16bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int8_t *filter, const int16_t *clip, const int pixel_max)
{
    const int ps = 1;                                    //pixel shift
    int w;

    for (w = 0; w + 16 <= width; w += 16) {
        ff_vvc_alf_filter_chroma_w16_16bpc_avx2(dst + (w << ps), dst_stride, src + (w << ps), src_stride,
            height, filter, clip, 0, pixel_max);
    }
    for ( /* nothing */; w < width; w += 4) {
        ff_vvc_alf_filter_chroma_w4_16bpc_avx2(dst + (w << ps), dst_stride, src + (w << ps), src_stride,
            height, filter, clip, 0, pixel_max);
    }
}

static void alf_filter_chroma_10_avx2(uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
    int width, int height, const int8_t *filter, const int16_t *clip)
{
    alf_filter_chroma_16bpc_avx2(dst, dst_stride, src, src_stride, width, height, filter, clip, PIXEL_MAX_10);
}

void ff_vvc_dsp_init_x86(VVCDSPContext *const c, const int bit_depth)
{
    const int cpu_flags = av_get_cpu_flags();

    if (EXTERNAL_AVX2(cpu_flags)) {
        if (bit_depth == 10) {
            c->alf.filter[LUMA] = alf_filter_luma_10_avx2;
            c->alf.filter[CHROMA] = alf_filter_chroma_10_avx2;
        }
    }
}

