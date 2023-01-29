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
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * FFmpeg is distributed in the hope that it will be useful,
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "vvcpred.h"
#include "vvc_ctu.h"

int ff_vvc_get_mip_size_id(const int w, const int h)
{
    if (w == 4 && h == 4)
        return 0;
    if ((w == 4 || h == 4) || (w == 8 && h == 8))
        return 1;
    return 2;
}

int ff_vvc_nscale_derive(const int w, const int h, const int mode)
{
    int side_size, nscale;
    av_assert0(mode < INTRA_LT_CCLM && !(mode > INTRA_HORZ && mode < INTRA_VERT));
    if (mode == INTRA_PLANAR || mode == INTRA_DC ||
        mode == INTRA_HORZ || mode == INTRA_VERT) {
        nscale = (av_log2(w) + av_log2(h) - 2) >> 2;
    } else {
        const int intra_pred_angle = ff_vvc_intra_pred_angle_derive(mode);
        const int inv_angle        = ff_vvc_intra_inv_angle_derive(intra_pred_angle);
        if (mode >= INTRA_VERT)
            side_size = h;
        if (mode <= INTRA_HORZ)
            side_size = w;
        nscale = FFMIN(2, av_log2(side_size) - av_log2(3 * inv_angle - 2) + 8);
    }
    return nscale;
}

int ff_vvc_need_pdpc(const int w, const int h, const uint8_t bdpcm_flag, const int mode, const int ref_idx)
{
    av_assert0(mode < INTRA_LT_CCLM);
    if ((w >= 4 && h >= 4) && !ref_idx && !bdpcm_flag) {
        int nscale;
        if (mode == INTRA_PLANAR || mode == INTRA_DC ||
            mode == INTRA_HORZ || mode == INTRA_VERT)
            return 1;
        if (mode > INTRA_HORZ && mode < INTRA_VERT)
            return 0;
        nscale = ff_vvc_nscale_derive(w, h, mode);
        return nscale >= 0;

    }
    return 0;
}

static const ReconstructedArea* get_reconstructed_area(const VVCLocalContext *lc, const int x, const int y, const int c_idx)
{
    const int ch_type = c_idx > 0;
    for (int i = lc->num_ras[ch_type] - 1; i >= 0; i--) {
        const ReconstructedArea* a = &lc->ras[ch_type][i];
        const int r = (a->x + a->w);
        const int b = (a->y + a->h);
        if (a->x <= x && x < r && a->y <= y && y < b)
            return a;

        //it's too far away, no need check it;
        if (x >= r && y >= b)
            break;
    }
    return NULL;
}

int ff_vvc_get_top_available(const VVCLocalContext *lc, const int x, const int y, int target_size, const int c_idx)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps = fc->ps.sps;
    const int hs = sps->hshift[c_idx];
    const int vs = sps->vshift[c_idx];
    const int log2_ctb_size_v   = sps->ctb_log2_size_y - vs;
    const int end_of_ctb_x      = ((lc->cu->x0 >> sps->ctb_log2_size_y) + 1) << sps->ctb_log2_size_y;
    const int y0b               = av_mod_uintp2(y, log2_ctb_size_v);
    const int max_x             = FFMIN(fc->ps.pps->width, end_of_ctb_x) >> hs;
    const ReconstructedArea *a;
    int px = x;

    if (!y0b) {
        if (!lc->ctb_up_flag)
            return 0;
        target_size = FFMIN(target_size, (lc->end_of_tiles_x >> hs) - x);
        if (sps->entropy_coding_sync_enabled_flag)
            target_size = FFMIN(target_size, (end_of_ctb_x >> hs) - x);
        return target_size;
    }

    target_size = FFMAX(0, FFMIN(target_size, max_x - x));
    while (target_size > 0 && (a = get_reconstructed_area(lc, px, y - 1, c_idx))) {
        const int sz = FFMIN(target_size, a->x + a->w - px);
        px += sz;
        target_size -= sz;
    }
    return px - x;
}

int ff_vvc_get_left_available(const VVCLocalContext *lc, const int x, const int y, int target_size, const int c_idx)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps = fc->ps.sps;
    const int hs = sps->hshift[c_idx];
    const int vs = sps->vshift[c_idx];
    const int log2_ctb_size_h   =  sps->ctb_log2_size_y - hs;
    const int x0b               = av_mod_uintp2(x, log2_ctb_size_h);
    const int end_of_ctb_y      = ((lc->cu->y0 >> sps->ctb_log2_size_y) + 1) << sps->ctb_log2_size_y;
    const int max_y             = FFMIN(fc->ps.pps->height, end_of_ctb_y) >> vs;
    const ReconstructedArea *a;
    int  py = y;

    if (!x0b && !lc->ctb_left_flag)
        return 0;

    target_size = FFMAX(0, FFMIN(target_size, max_y - y));
    if (!x0b)
        return target_size;

    while (target_size > 0 && (a = get_reconstructed_area(lc, x - 1, py, c_idx))) {
        const int sz = FFMIN(target_size, a->y + a->h - py);
        py += sz;
        target_size -= sz;
    }
    return py - y;
}

static int less(const void *a, const void *b)
{
    return *(const int*)a - *(const int*)b;
}

int ff_vvc_ref_filter_flag_derive(const int mode)
{
    static const int modes[] = { -14, -12, -10, -6, INTRA_PLANAR, 2, 34, 66, 72, 76, 78, 80};
    return bsearch(&mode, modes, FF_ARRAY_ELEMS(modes), sizeof(int), less) != NULL;
}

int ff_vvc_intra_pred_angle_derive(const int pred_mode)
{
    static const int angles[] = {
          0,   1,   2,   3,   4,   6,   8,  10,  12,  14,  16,  18,  20,  23,  26, 29,
         32,  35,  39,  45,  51,  57,  64,  73,  86, 102, 128, 171, 256, 341, 512
    };
    int sign = 1, idx, intra_pred_angle;
    if (pred_mode > INTRA_DIAG) {
        idx = pred_mode - INTRA_VERT;
    } else if (pred_mode > 0) {
        idx = INTRA_HORZ - pred_mode;
    } else {
        idx = INTRA_HORZ - 2 - pred_mode;
    }
    if (idx < 0) {
        idx = -idx;
        sign = -1;
    }
    intra_pred_angle = sign * angles[idx];
    return intra_pred_angle;
}

#define ROUND(f) (int)(f < 0 ? -(-f + 0.5) : (f + 0.5))
int ff_vvc_intra_inv_angle_derive(const int intra_pred_angle)
{
    float inv_angle;
    av_assert0(intra_pred_angle);
    inv_angle = 32 * 512.0 / intra_pred_angle;
    return ROUND(inv_angle);
}

const int8_t ff_vvc_filter_c[NUM_INTRA_LUMA_FACTS][NUM_INTRA_LUMA_TAPS] =
{
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
};

#define FILTER_G(fact)  { 16 - (fact >> 1), 32 - (fact >> 1), 16 + (fact >> 1), fact >> 1}
const int8_t ff_vvc_filter_g[NUM_INTRA_LUMA_FACTS][NUM_INTRA_LUMA_TAPS] = {
    FILTER_G(0),
    FILTER_G(1),
    FILTER_G(2),
    FILTER_G(3),
    FILTER_G(4),
    FILTER_G(5),
    FILTER_G(6),
    FILTER_G(7),
    FILTER_G(8),
    FILTER_G(9),
    FILTER_G(10),
    FILTER_G(11),
    FILTER_G(12),
    FILTER_G(13),
    FILTER_G(14),
    FILTER_G(15),
    FILTER_G(16),
    FILTER_G(17),
    FILTER_G(18),
    FILTER_G(19),
    FILTER_G(20),
    FILTER_G(21),
    FILTER_G(22),
    FILTER_G(23),
    FILTER_G(24),
    FILTER_G(25),
    FILTER_G(26),
    FILTER_G(27),
    FILTER_G(28),
    FILTER_G(29),
    FILTER_G(30),
    FILTER_G(31),

};
