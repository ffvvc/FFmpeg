/*
 * VVC filter DSP
 *
 * Copyright (C) 2022 Nuo Mi
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

static void FUNC(lmcs_filter_luma)(uint8_t *dst, ptrdiff_t dst_stride,
    const int width, const int height, const uint8_t *lut)
{
    return bitfn(lmcs_filter_luma)(dst, dst_stride, width, height, lut);
}

static int FUNC(vvc_h_loop_ladf_level)(const uint8_t *pix, ptrdiff_t stride)
{
    return bitfn(vvc_loop_ladf_level)(pix, stride, sizeof(pixel));
}

static int FUNC(vvc_v_loop_ladf_level)(const uint8_t *pix, ptrdiff_t stride)
{
    return bitfn(vvc_loop_ladf_level)(pix, sizeof(pixel), stride);
}

static void FUNC(vvc_h_loop_filter_chroma)(uint8_t *pix, const ptrdiff_t stride,
    const int beta, const int32_t tc, const uint8_t no_p, const uint8_t no_q,
    const int shift, const int max_len_p, const int max_len_q)
{
    bitfn(vvc_loop_filter_chroma)(pix, stride, sizeof(pixel), beta, tc, no_p, no_q,
        shift, max_len_p, max_len_q HIGHBD_TAIL_SUFFIX);
}

static void FUNC(vvc_v_loop_filter_chroma)(uint8_t *pix, const ptrdiff_t stride,
    const int beta, const int32_t tc, const uint8_t no_p, const uint8_t no_q,
    const int shift, const int max_len_p, const int max_len_q)
{
    bitfn(vvc_loop_filter_chroma)(pix, sizeof(pixel), stride, beta, tc, no_p, no_q,
        shift, max_len_p, max_len_q HIGHBD_TAIL_SUFFIX);
}

static void FUNC(vvc_h_loop_filter_luma)(uint8_t *pix, const ptrdiff_t stride,
    const int beta, const int32_t tc, const uint8_t no_p, const uint8_t no_q,
    const uint8_t max_len_p, const uint8_t max_len_q, const int hor_ctu_edge)
{
    bitfn(vvc_loop_filter_luma)(pix, stride, sizeof(pixel), beta, tc, no_p, no_q,
        max_len_p, max_len_q, hor_ctu_edge HIGHBD_TAIL_SUFFIX);
}

static void FUNC(vvc_v_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
    const int beta, const int32_t tc, const uint8_t no_p, const uint8_t no_q,
    const uint8_t max_len_p, const uint8_t max_len_q, const int hor_ctu_edge)
{
    bitfn(vvc_loop_filter_luma)(pix, sizeof(pixel), stride, beta, tc, no_p, no_q,
        max_len_p, max_len_q, hor_ctu_edge HIGHBD_TAIL_SUFFIX);
}

av_cold static void FUNC(ff_vvc_lmcs_dsp_init)(VVCLMCSDSPContext *const lmcs)
{
    lmcs->filter = FUNC(lmcs_filter_luma);
}

av_cold static void FUNC(ff_vvc_lf_dsp_init)(VVCLFDSPContext *const lf)
{
    lf->ladf_level[0]      = FUNC(vvc_h_loop_ladf_level);
    lf->ladf_level[1]      = FUNC(vvc_v_loop_ladf_level);
    lf->filter_luma[0]     = FUNC(vvc_h_loop_filter_luma);
    lf->filter_luma[1]     = FUNC(vvc_v_loop_filter_luma);
    lf->filter_chroma[0]   = FUNC(vvc_h_loop_filter_chroma);
    lf->filter_chroma[1]   = FUNC(vvc_v_loop_filter_chroma);
}
