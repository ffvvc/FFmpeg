/*
 * VVC DSP for x86
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

#ifndef AVCODEC_X86_VVCDSP_H
#define AVCODEC_X86_VVCDSP_H

void ff_vvc_alf_filter_luma_16bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride,
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);

void ff_vvc_alf_filter_chroma_16bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride,
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);

void ff_vvc_alf_filter_luma_8bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride,
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);

void ff_vvc_alf_filter_chroma_8bpc_avx2(uint8_t *dst, ptrdiff_t dst_stride,
    const uint8_t *src, ptrdiff_t src_stride, ptrdiff_t width, ptrdiff_t height,
    const int16_t *filter, const int16_t *clip, ptrdiff_t stride, ptrdiff_t vb_pos, ptrdiff_t pixel_max);

/* each word in gradient_sum is sum gradient of 8x2 pixels, adjacent word share 4x2 pixels  */
/* gradient_sum's stride aligned to 16 words                                                */
void ff_vvc_alf_classify_grad_8bpc_avx2(int *gradient_sum,
    const uint8_t *src, ptrdiff_t src_stride, intptr_t width, intptr_t height, intptr_t vb_pos);
void ff_vvc_alf_classify_grad_16bpc_avx2(int *gradient_sum,
    const uint8_t *src, ptrdiff_t src_stride, intptr_t width, intptr_t height, intptr_t vb_pos);

void ff_vvc_alf_classify_8bpc_avx2(int *class_idx, int *transpose_idx, const int *gradient_sum,
    intptr_t width, intptr_t height, intptr_t vb_pos, intptr_t bit_depth);
void ff_vvc_alf_classify_16bpc_avx2(int *class_idx, int *transpose_idx, const int *gradient_sum,
    intptr_t width, intptr_t height, intptr_t vb_pos, intptr_t bit_depth);

#endif //AVCODEC_X86_VVCDSP_H

