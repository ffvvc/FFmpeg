/*
 * VVC video Decoder
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

#ifndef AVCODEC_VVCPRED_H
#define AVCODEC_VVCPRED_H

#include <stddef.h>
#include <stdint.h>

#include "vvcdsp.h"

struct VVCLocalContext;

typedef struct VVCPredContext {
    void (*intra_cclm_pred)(const struct VVCLocalContext *lc, int x0, int y0, int w, int h);
    void (*lmcs_scale_chroma)(struct VVCLocalContext *lc, int *dst, const int *coeff, int w, int h, int x0_cu, int y0_cu);
    void (*intra_pred)(const struct VVCLocalContext *lc, int x0, int y0, int w, int h, int c_idx);

    void (*pred_planar)(uint8_t *src, const uint8_t *top, const uint8_t *left,
                        int w, int h, ptrdiff_t stride);
    void (*pred_mip)(uint8_t *src, const uint8_t *top, const uint8_t *left,
                        int w, int h, ptrdiff_t stride, int mode_id, int is_transpose);
    void (*pred_dc)(uint8_t *src, const uint8_t *top, const uint8_t *left,
                        int w, int h, ptrdiff_t stride);
    void (*pred_v)(uint8_t *_src, const uint8_t *_top, int w, int h, ptrdiff_t stride);
    void (*pred_h)(uint8_t *_src, const uint8_t *_left, int w, int h, ptrdiff_t stride);
    void (*pred_angular_v)(uint8_t *_src, const uint8_t *_top, const uint8_t *_left,
                           int w, int h, ptrdiff_t stride, int c_idx, int mode,
                           int ref_idx, int filter_flag, int need_pdpc);
    void (*pred_angular_h)(uint8_t *_src, const uint8_t *_top, const uint8_t *_left,
                           int w, int h, ptrdiff_t stride, int c_idx, int mode,
                           int ref_idx, int filter_flag, int need_pdpc);
} VVCPredContext;

void ff_vvc_pred_init(VVCPredContext *hpc, int bit_depth);
int ff_vvc_get_mip_size_id(int w, int h);
int ff_vvc_need_pdpc(int w, int h, uint8_t bdpcm_flag, int mode, int ref_idx);
int ff_vvc_nscale_derive(int w, int h, int mode);
int ff_vvc_ref_filter_flag_derive(int mode);
int ff_vvc_intra_pred_angle_derive(int pred_mode);
int ff_vvc_intra_inv_angle_derive(int pred_mode);

#define NUM_INTRA_LUMA_TAPS 4
#define NUM_INTRA_LUMA_FACTS 32
extern const int8_t ff_vvc_filter_c[NUM_INTRA_LUMA_FACTS][NUM_INTRA_LUMA_TAPS];
extern const int8_t ff_vvc_filter_g[NUM_INTRA_LUMA_FACTS][NUM_INTRA_LUMA_TAPS];

#endif /* AVCODEC_VVCPRED_H */
