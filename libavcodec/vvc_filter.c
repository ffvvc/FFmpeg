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

#include "libavutil/common.h"
#include "libavutil/internal.h"

#include "cabac_functions.h"
#include "vvc_ctu.h"
#include "vvc_data.h"

//Table 43 Derivation of threshold variables beta' and tc' from input Q
static const uint16_t tctable[66] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      0,   0,   3,   4,   4,   4,   4,   5,   5,   5,   5,   7,   7,   8,   9,  10,
     10,  11,  13,  14,  15,  17,  19,  21,  24,  25,  29,  33,  36,  41,  45,  51,
     57,  64,  71,  80,  89, 100, 112, 125, 141, 157, 177, 198, 222, 250, 280, 314,
    352, 395,

};

//Table 43 Derivation of threshold variables beta' and tc' from input Q
static const uint8_t betatable[64] = {
      0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,
      6,   7,   8,   9,  10,  11,  12,  13,  14,  15,  16,  17,  18,  20,  22,  24,
     26,  28,  30,  32,  34,  36,  38,  40,  42,  44,  46,  48,  50,  52,  54,  56,
     58,  60,  62,  64,  66,  68,  70,  72,  74,  76,  78,  80,  82,  84,  86,  88,
};

int ff_vvc_get_qPy(const VVCFrameContext *fc, int xc, int yc)
{
    int min_cb_log2_size_y  = fc->ps.sps->min_cb_log2_size_y;
    int x                 = xc >> min_cb_log2_size_y;
    int y                 = yc >> min_cb_log2_size_y;
    return fc->tab.qp[LUMA][x + y * fc->ps.pps->min_cb_width];
}

static int get_qPc(const VVCFrameContext *fc, const int x0, const int y0, const int chroma)
{
    const int x             = x0 >> MIN_TB_LOG2;
    const int y             = y0 >> MIN_TB_LOG2;
    const int min_tb_width  = fc->ps.pps->min_tb_width;
    return fc->tab.qp[chroma][x + y * min_tb_width];
}

static void copy_CTB(uint8_t *dst, const uint8_t *src, const int width, const int height,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    int i, j;

    if (((intptr_t)dst | (intptr_t)src | dst_stride | src_stride) & 15) {
        for (i = 0; i < height; i++) {
            for (j = 0; j < width; j+=8)
                AV_COPY64U(dst+j, src+j);
            dst += dst_stride;
            src += src_stride;
        }
    } else {
        for (i = 0; i < height; i++) {
            for (j = 0; j < width; j+=16)
                AV_COPY128(dst+j, src+j);
            dst += dst_stride;
            src += src_stride;
        }
    }
}

static void copy_pixel(uint8_t *dst, const uint8_t *src, const int pixel_shift)
{
    if (pixel_shift)
        *(uint16_t *)dst = *(uint16_t *)src;
    else
        *dst = *src;
}

static void copy_vert(uint8_t *dst, const uint8_t *src, const int pixel_shift, const int height,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    int i;
    if (pixel_shift == 0) {
        for (i = 0; i < height; i++) {
            *dst = *src;
            dst += dst_stride;
            src += src_stride;
        }
    } else {
        for (i = 0; i < height; i++) {
            *(uint16_t *)dst = *(uint16_t *)src;
            dst += dst_stride;
            src += src_stride;
        }
    }
}

static void copy_CTB_to_hv(VVCFrameContext *fc, const uint8_t *src,
    const ptrdiff_t src_stride, const int x, const int y, const int width, const int height,
    const int c_idx, const int x_ctb, const int y_ctb)
{
    int ps = fc->ps.sps->pixel_shift;
    int w  = fc->ps.sps->width >> fc->ps.sps->hshift[c_idx];
    int h  = fc->ps.sps->height >> fc->ps.sps->vshift[c_idx];

    /* copy horizontal edges */
    memcpy(fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb) * w + x) << ps),
        src, width << ps);
    memcpy(fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb + 1) * w + x) << ps),
        src + src_stride * (height - 1), width << ps);

    /* copy vertical edges */
    copy_vert(fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb) * h + y) << ps), src, ps, height, 1 << ps, src_stride);

    copy_vert(fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb + 1) * h + y) << ps), src + ((width - 1) << ps), ps, height, 1 << ps, src_stride);
}

void ff_vvc_sao_filter(VVCLocalContext *lc, int x, int y)
{
    VVCFrameContext *fc  = lc->fc;
    const int ctb_size_y = fc->ps.sps->ctb_size_y;
    static const uint8_t sao_tab[16] = { 0, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8 };
    int c_idx;
    int edges[4];  // 0 left 1 top 2 right 3 bottom
    int x_ctb                = x >> fc->ps.sps->ctb_log2_size_y;
    int y_ctb                = y >> fc->ps.sps->ctb_log2_size_y;
    SAOParams *sao           = &CTB(fc->tab.sao, x_ctb, y_ctb);
    // flags indicating unfilterable edges
    uint8_t vert_edge[]      = { 0, 0 };
    uint8_t horiz_edge[]     = { 0, 0 };
    uint8_t diag_edge[]      = { 0, 0, 0, 0 };
    uint8_t lfase            = fc->ps.pps->loop_filter_across_slices_enabled_flag;
    uint8_t no_tile_filter   = fc->ps.pps->num_tiles_in_pic > 1 &&
                               !fc->ps.pps->loop_filter_across_tiles_enabled_flag;
    uint8_t restore          = no_tile_filter || !lfase;
    uint8_t left_tile_edge   = 0;
    uint8_t right_tile_edge  = 0;
    uint8_t up_tile_edge     = 0;
    uint8_t bottom_tile_edge = 0;

    edges[0]   = x_ctb == 0;
    edges[1]   = y_ctb == 0;
    edges[2]   = x_ctb == fc->ps.pps->ctb_width  - 1;
    edges[3]   = y_ctb == fc->ps.pps->ctb_height - 1;

    if (restore) {
        if (!edges[0]) {
            left_tile_edge  = no_tile_filter && fc->ps.pps->ctb_to_col_bd[x_ctb] == x_ctb;
            vert_edge[0]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb - 1, y_ctb)) || left_tile_edge;
        }
        if (!edges[2]) {
            right_tile_edge = no_tile_filter && fc->ps.pps->ctb_to_col_bd[x_ctb] != fc->ps.pps->ctb_to_col_bd[x_ctb + 1];
            vert_edge[1]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb + 1, y_ctb)) || right_tile_edge;
        }
        if (!edges[1]) {
            up_tile_edge     = no_tile_filter && fc->ps.pps->ctb_to_row_bd[y_ctb] == y_ctb;
            horiz_edge[0]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb, y_ctb - 1)) || up_tile_edge;
        }
        if (!edges[3]) {
            bottom_tile_edge = no_tile_filter && fc->ps.pps->ctb_to_row_bd[y_ctb] != fc->ps.pps->ctb_to_row_bd[y_ctb + 1];
            horiz_edge[1]    = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb, y_ctb + 1)) || bottom_tile_edge;
        }
        if (!edges[0] && !edges[1]) {
            diag_edge[0] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb - 1, y_ctb - 1)) || left_tile_edge || up_tile_edge;
        }
        if (!edges[1] && !edges[2]) {
            diag_edge[1] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb + 1, y_ctb - 1)) || right_tile_edge || up_tile_edge;
        }
        if (!edges[2] && !edges[3]) {
            diag_edge[2] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb + 1, y_ctb + 1)) || right_tile_edge || bottom_tile_edge;
        }
        if (!edges[0] && !edges[3]) {
            diag_edge[3] = (!lfase && CTB(fc->tab.slice_idx, x_ctb, y_ctb) != CTB(fc->tab.slice_idx, x_ctb - 1, y_ctb + 1)) || left_tile_edge || bottom_tile_edge;
        }
    }

    for (c_idx = 0; c_idx < (fc->ps.sps->chroma_format_idc ? 3 : 1); c_idx++) {
        int x0       = x >> fc->ps.sps->hshift[c_idx];
        int y0       = y >> fc->ps.sps->vshift[c_idx];
        ptrdiff_t src_stride = fc->frame->linesize[c_idx];
        int ctb_size_h = ctb_size_y >> fc->ps.sps->hshift[c_idx];
        int ctb_size_v = ctb_size_y >> fc->ps.sps->vshift[c_idx];
        int width    = FFMIN(ctb_size_h, (fc->ps.sps->width  >> fc->ps.sps->hshift[c_idx]) - x0);
        int height   = FFMIN(ctb_size_v, (fc->ps.sps->height >> fc->ps.sps->vshift[c_idx]) - y0);
        int tab      = sao_tab[(FFALIGN(width, 8) >> 3) - 1];
        uint8_t *src = &fc->frame->data[c_idx][y0 * src_stride + (x0 << fc->ps.sps->pixel_shift)];
        ptrdiff_t dst_stride;
        uint8_t *dst;

        switch (sao->type_idx[c_idx]) {
        case SAO_BAND:
            copy_CTB_to_hv(fc, src, src_stride, x0, y0, width, height, c_idx, x_ctb, y_ctb);
            fc->vvcdsp.sao_band_filter[tab](src, src, src_stride, src_stride,
                sao->offset_val[c_idx], sao->band_position[c_idx], width, height);

            sao->type_idx[c_idx] = SAO_APPLIED;
            break;
        case SAO_EDGE:
        {
            int w = fc->ps.sps->width >> fc->ps.sps->hshift[c_idx];
            int h = fc->ps.sps->height >> fc->ps.sps->vshift[c_idx];
            int left_edge = edges[0];
            int top_edge = edges[1];
            int right_edge = edges[2];
            int bottom_edge = edges[3];
            int sh = fc->ps.sps->pixel_shift;
            int left_pixels, right_pixels;

            dst_stride = 2*MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE;
            dst = lc->sao_buffer + dst_stride + AV_INPUT_BUFFER_PADDING_SIZE;

            if (!top_edge) {
                int left = 1 - left_edge;
                int right = 1 - right_edge;
                const uint8_t *src1[2];
                uint8_t *dst1;
                int src_idx, pos;

                dst1 = dst - dst_stride - (left << sh);
                src1[0] = src - src_stride - (left << sh);
                src1[1] = fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb - 1) * w + x0 - left) << sh);
                pos = 0;
                if (left) {
                    src_idx = (CTB(fc->tab.sao, x_ctb-1, y_ctb-1).type_idx[c_idx] ==
                               SAO_APPLIED);
                    copy_pixel(dst1, src1[src_idx], sh);
                    pos += (1 << sh);
                }
                src_idx = (CTB(fc->tab.sao, x_ctb, y_ctb-1).type_idx[c_idx] ==
                           SAO_APPLIED);
                memcpy(dst1 + pos, src1[src_idx] + pos, width << sh);
                if (right) {
                    pos += width << sh;
                    src_idx = (CTB(fc->tab.sao, x_ctb+1, y_ctb-1).type_idx[c_idx] ==
                               SAO_APPLIED);
                    copy_pixel(dst1 + pos, src1[src_idx] + pos, sh);
                }
            }
            if (!bottom_edge) {
                int left = 1 - left_edge;
                int right = 1 - right_edge;
                const uint8_t *src1[2];
                uint8_t *dst1;
                int src_idx, pos;

                dst1 = dst + height * dst_stride - (left << sh);
                src1[0] = src + height * src_stride - (left << sh);
                src1[1] = fc->tab.sao_pixel_buffer_h[c_idx] + (((2 * y_ctb + 2) * w + x0 - left) << sh);
                pos = 0;
                if (left) {
                    src_idx = (CTB(fc->tab.sao, x_ctb-1, y_ctb+1).type_idx[c_idx] ==
                               SAO_APPLIED);
                    copy_pixel(dst1, src1[src_idx], sh);
                    pos += (1 << sh);
                }
                src_idx = (CTB(fc->tab.sao, x_ctb, y_ctb+1).type_idx[c_idx] ==
                           SAO_APPLIED);
                memcpy(dst1 + pos, src1[src_idx] + pos, width << sh);
                if (right) {
                    pos += width << sh;
                    src_idx = (CTB(fc->tab.sao, x_ctb+1, y_ctb+1).type_idx[c_idx] ==
                               SAO_APPLIED);
                    copy_pixel(dst1 + pos, src1[src_idx] + pos, sh);
                }
            }
            left_pixels = 0;
            if (!left_edge) {
                if (CTB(fc->tab.sao, x_ctb-1, y_ctb).type_idx[c_idx] == SAO_APPLIED) {
                    copy_vert(dst - (1 << sh),
                              fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb - 1) * h + y0) << sh),
                              sh, height, dst_stride, 1 << sh);
                } else {
                    left_pixels = 1;
                }
            }
            right_pixels = 0;
            if (!right_edge) {
                if (CTB(fc->tab.sao, x_ctb+1, y_ctb).type_idx[c_idx] == SAO_APPLIED) {
                    copy_vert(dst + (width << sh),
                              fc->tab.sao_pixel_buffer_v[c_idx] + (((2 * x_ctb + 2) * h + y0) << sh),
                              sh, height, dst_stride, 1 << sh);
                } else {
                    right_pixels = 1;
                }
            }

            copy_CTB(dst - (left_pixels << sh),
                     src - (left_pixels << sh),
                     (width + left_pixels + right_pixels) << sh,
                     height, dst_stride, src_stride);

            copy_CTB_to_hv(fc, src, src_stride, x0, y0, width, height, c_idx,
                           x_ctb, y_ctb);
            fc->vvcdsp.sao_edge_filter[tab](src, dst, src_stride, sao->offset_val[c_idx],
                                            sao->eo_class[c_idx], width, height);
            fc->vvcdsp.sao_edge_restore[restore](src, dst,
                                                src_stride, dst_stride,
                                                sao,
                                                edges, width,
                                                height, c_idx,
                                                vert_edge,
                                                horiz_edge,
                                                diag_edge);
            sao->type_idx[c_idx] = SAO_APPLIED;
            break;
        }
        }
    }
}

#define TAB_BS(t, x, y)     (t)[((y) >> 2) * (fc->tab.bs_width) + ((x) >> 2)]
#define TAB_MAX_LEN(t, x, y)  (t)[((y) >> 2) * (fc->tab.bs_width) + ((x) >> 2)]
#define DEBLOCK_STEP            4
#define MAX_FILTER_LEN          8
#define LUMA_GRID               4
#define CHROMA_GRID             8

static int boundary_strength(const VVCFrameContext *fc, MvField *curr, MvField *neigh,
                             const RefPicList *neigh_refPicList)
{
    if (curr->pred_flag == PF_BI &&  neigh->pred_flag == PF_BI) {
        // same L0 and L1
        if (fc->ref->refPicList[0].list[curr->ref_idx[0]] == neigh_refPicList[0].list[neigh->ref_idx[0]]  &&
            fc->ref->refPicList[0].list[curr->ref_idx[0]] == fc->ref->refPicList[1].list[curr->ref_idx[1]] &&
            neigh_refPicList[0].list[neigh->ref_idx[0]] == neigh_refPicList[1].list[neigh->ref_idx[1]]) {
            if ((FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8 ||
                 FFABS(neigh->mv[1].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 8) &&
                (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 8 ||
                 FFABS(neigh->mv[0].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 8))
                return 1;
            else
                return 0;
        } else if (neigh_refPicList[0].list[neigh->ref_idx[0]] == fc->ref->refPicList[0].list[curr->ref_idx[0]] &&
                   neigh_refPicList[1].list[neigh->ref_idx[1]] == fc->ref->refPicList[1].list[curr->ref_idx[1]]) {
            if (FFABS(neigh->mv[0].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[0].y) >= 8 ||
                FFABS(neigh->mv[1].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[1].y) >= 8)
                return 1;
            else
                return 0;
        } else if (neigh_refPicList[1].list[neigh->ref_idx[1]] == fc->ref->refPicList[0].list[curr->ref_idx[0]] &&
                   neigh_refPicList[0].list[neigh->ref_idx[0]] == fc->ref->refPicList[1].list[curr->ref_idx[1]]) {
            if (FFABS(neigh->mv[1].x - curr->mv[0].x) >= 8 || FFABS(neigh->mv[1].y - curr->mv[0].y) >= 8 ||
                FFABS(neigh->mv[0].x - curr->mv[1].x) >= 8 || FFABS(neigh->mv[0].y - curr->mv[1].y) >= 8)
                return 1;
            else
                return 0;
        } else {
            return 1;
        }
    } else if ((curr->pred_flag != PF_BI) && (neigh->pred_flag != PF_BI)){ // 1 MV
        Mv A, B;
        int ref_A, ref_B;

        if (curr->pred_flag & 1) {
            A     = curr->mv[0];
            ref_A = fc->ref->refPicList[0].list[curr->ref_idx[0]];
        } else {
            A     = curr->mv[1];
            ref_A = fc->ref->refPicList[1].list[curr->ref_idx[1]];
        }

        if (neigh->pred_flag & 1) {
            B     = neigh->mv[0];
            ref_B = neigh_refPicList[0].list[neigh->ref_idx[0]];
        } else {
            B     = neigh->mv[1];
            ref_B = neigh_refPicList[1].list[neigh->ref_idx[1]];
        }

        if (ref_A == ref_B) {
            if (FFABS(A.x - B.x) >= 8 || FFABS(A.y - B.y) >= 8)
                return 1;
            else
                return 0;
        } else
            return 1;
    }

    return 1;
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void derive_max_filter_length_luma(const VVCFrameContext *fc, const int qx, const int qy,
                                          const int is_intra, const int has_subblock, const int vertical, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const int px =  vertical ? qx - 1 : qx;
    const int py = !vertical ? qy - 1 : qy;
    const uint8_t *tb_size = vertical ? fc->tab.tb_width[LUMA] : fc->tab.tb_height[LUMA];
    const int size_p = tb_size[(py >> MIN_TB_LOG2) * fc->ps.pps->min_tb_width + (px >> MIN_TB_LOG2)];
    const int size_q = tb_size[(qy >> MIN_TB_LOG2) * fc->ps.pps->min_tb_width + (qx >> MIN_TB_LOG2)];
    const int min_cb_log2 = fc->ps.sps->min_cb_log2_size_y;
    const int off_p = (py >> min_cb_log2) * fc->ps.pps->min_cb_width + (px >> min_cb_log2);
    if (size_p <= 4 || size_q <= 4) {
        *max_len_p = *max_len_q = 1;
    } else {
        *max_len_p = *max_len_q = 3;
        if (size_p >= 32)
            *max_len_p = 7;
        if (size_q >= 32)
            *max_len_q = 7;
    }
    if (has_subblock)
        *max_len_q = FFMIN(5, *max_len_q);
    if (fc->tab.msf[off_p] || fc->tab.iaf[off_p])
        *max_len_p = FFMIN(5, *max_len_p);
}

static void vvc_deblock_subblock_bs_vertical(const VVCFrameContext  *fc,
    const int cb_x, const int cb_y, const int x0, const int y0, const int width, const int height)
{
    MvField* tab_mvf            = fc->ref->tab_mvf;
    RefPicList* rpl             = fc->ref->refPicList;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    const int log2_min_pu_size  = MIN_PU_LOG2;
    uint8_t max_len_p, max_len_q;
    int bs, i, j;

    // bs for TU internal vertical PU boundaries
    for (j = 0; j < height; j += 4) {
        int y_pu = (y0 + j) >> log2_min_pu_size;

        for (i = 8 - ((x0 - cb_x) % 8); i < width; i += 8) {
            int xp_pu = (x0 + i - 1) >> log2_min_pu_size;
            int xq_pu = (x0 + i)     >> log2_min_pu_size;
            MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
            MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];
            const int x = x0 + i;
            const int y = y0 + j;

            bs = boundary_strength(fc, curr, left, rpl);
            TAB_BS(fc->tab.vertical_bs[LUMA], x, y) = bs;


            max_len_p = max_len_q = 0;
            if (i == 4 || i == width - 4)
                max_len_p = max_len_q = 1;
            else if (i == 8 || i == width - 8)
                max_len_p = max_len_q = 2;
            else
                max_len_p = max_len_q = 3;

            TAB_MAX_LEN(fc->tab.vertical_p, x, y) = max_len_p;
            TAB_MAX_LEN(fc->tab.vertical_q, x, y) = max_len_q;
        }
    }
}

static void vvc_deblock_subblock_bs_horizontal(const VVCFrameContext  *fc,
    const int cb_x, const int cb_y, const int x0, const int y0, const int width, const int height)
{
    MvField* tab_mvf            = fc->ref->tab_mvf;
    RefPicList* rpl             = fc->ref->refPicList;
    const int min_pu_width      = fc->ps.pps->min_pu_width;
    const int log2_min_pu_size  = MIN_PU_LOG2;
    uint8_t max_len_p, max_len_q;
    int bs, i, j;

    // bs for TU internal horizontal PU boundaries
    for (j = 8 - ((y0 - cb_y) % 8); j < height; j += 8) {
        int yp_pu = (y0 + j - 1) >> log2_min_pu_size;
        int yq_pu = (y0 + j)     >> log2_min_pu_size;

        for (i = 0; i < width; i += 4) {
            int x_pu = (x0 + i) >> log2_min_pu_size;
            MvField *top  = &tab_mvf[yp_pu * min_pu_width + x_pu];
            MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];
            const int x = x0 + i;
            const int y = y0 + j;

            bs = boundary_strength(fc, curr, top, rpl);
            TAB_BS(fc->tab.horizontal_bs[LUMA], x, y) = bs;

            //fixme:
            //edgeTbFlags[ x − sbW ][ y ] is equal to 1
            //edgeTbFlags[ x + sbW ][ y ] is equal to 1
            max_len_p = max_len_q = 0;
            if (j == 4 || j == height - 4)
                max_len_p = max_len_q = 1;
            else if (j == 8 || j == height - 8)
                max_len_p = max_len_q = 2;
            else
                max_len_p = max_len_q = 3;
            TAB_MAX_LEN(fc->tab.horizontal_p, x, y) = max_len_p;
            TAB_MAX_LEN(fc->tab.horizontal_q, x, y) = max_len_q;
        }
    }

}

static void vvc_deblock_bs_luma_vertical(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc        = lc->fc;
    MvField *tab_mvf           = fc->ref->tab_mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int log2_min_tu_size = MIN_TB_LOG2;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int min_tu_width     = fc->ps.pps->min_tb_width;
    const int min_cb_log2      = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width     = fc->ps.pps->min_cb_width;
    int is_intra = tab_mvf[(y0 >> log2_min_pu_size) * min_pu_width +
                           (x0 >> log2_min_pu_size)].pred_flag == PF_INTRA;
    int boundary_left;
    int i, bs, has_vertical_sb = 0;
    uint8_t max_len_p, max_len_q;

    const int off_q            = (y0 >> min_cb_log2) * min_cb_width + (x0 >> min_cb_log2);
    const int cb_x             = fc->tab.cb_pos_x[LUMA][off_q];
    const int cb_y             = fc->tab.cb_pos_y[LUMA][off_q];
    const int cb_width         = fc->tab.cb_width[LUMA][off_q];

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            has_vertical_sb   = cb_width  > 8;
    }

    // bs for vertical TU boundaries
    boundary_left = x0 > 0 && !(x0 & 3);
    if (boundary_left &&
        ((!fc->ps.pps->loop_filter_across_slices_enabled_flag &&
            lc->boundary_flags & BOUNDARY_LEFT_SLICE &&
            (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
            (!fc->ps.pps->loop_filter_across_tiles_enabled_flag &&
            lc->boundary_flags & BOUNDARY_LEFT_TILE &&
            (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_left = 0;

    if (boundary_left) {
        const RefPicList *rpl_left = (lc->boundary_flags & BOUNDARY_LEFT_SLICE) ?
                                ff_vvc_get_ref_list(fc, fc->ref, x0 - 1, y0) :
                                fc->ref->refPicList;
        int xp_pu = (x0 - 1) >> log2_min_pu_size;
        int xq_pu =  x0      >> log2_min_pu_size;
        int xp_tu = (x0 - 1) >> log2_min_tu_size;
        int xq_tu =  x0      >> log2_min_tu_size;

        for (i = 0; i < height; i += 4) {
            const int off_x = cb_x - x0;
            int y_pu      = (y0 + i) >> log2_min_pu_size;
            int y_tu      = (y0 + i) >> log2_min_tu_size;
            MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
            MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];
            uint8_t left_cbf_luma = fc->tab.tu_coded_flag[LUMA][y_tu * min_tu_width + xp_tu];
            uint8_t curr_cbf_luma = fc->tab.tu_coded_flag[LUMA][y_tu * min_tu_width + xq_tu];
            uint8_t pcmf          = fc->tab.pcmf[LUMA][y_tu * min_tu_width + xp_tu] &&
                fc->tab.pcmf[LUMA][y_tu * min_tu_width + xq_tu];

            if (pcmf)
                bs = 0;
            else if (curr->pred_flag == PF_INTRA || left->pred_flag == PF_INTRA || curr->ciip_flag || left->ciip_flag)
                bs = 2;
            else if (curr_cbf_luma || left_cbf_luma)
                bs = 1;
            else if (off_x && ((off_x % 8) || !has_vertical_sb))
                bs = 0;                                     ////inside a cu, not aligned to 8 or with no subblocks
            else
                bs = boundary_strength(fc, curr, left, rpl_left);

            TAB_BS(fc->tab.vertical_bs[LUMA], x0, (y0 + i)) = bs;

            derive_max_filter_length_luma(fc, x0, y0 + i, is_intra, has_vertical_sb, 1, &max_len_p, &max_len_q);
            TAB_MAX_LEN(fc->tab.vertical_p, x0, y0 + i) = max_len_p;
            TAB_MAX_LEN(fc->tab.vertical_q, x0, y0 + i) = max_len_q;
        }
    }

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            vvc_deblock_subblock_bs_vertical(fc, cb_x, cb_y, x0, y0, width, height);
    }

}
static void vvc_deblock_bs_luma_horizontal(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc  = lc->fc;
    MvField *tab_mvf           = fc->ref->tab_mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int log2_min_tu_size = MIN_TB_LOG2;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int min_tu_width     = fc->ps.pps->min_tb_width;
    const int min_cb_log2      = fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width     = fc->ps.pps->min_cb_width;
    int is_intra = tab_mvf[(y0 >> log2_min_pu_size) * min_pu_width +
                           (x0 >> log2_min_pu_size)].pred_flag == PF_INTRA;
    int boundary_upper;
    int i, bs, has_horizontal_sb = 0;
    uint8_t max_len_p, max_len_q;

    const int off_q            = (y0 >> min_cb_log2) * min_cb_width + (x0 >> min_cb_log2);
    const int cb_x             = fc->tab.cb_pos_x[LUMA][off_q];
    const int cb_y             = fc->tab.cb_pos_y[LUMA][off_q];
    const int cb_height        = fc->tab.cb_height[LUMA][off_q];

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            has_horizontal_sb = cb_height > 8;
    }

    boundary_upper = y0 > 0 && !(y0 & 3);
    if (boundary_upper &&
        ((!fc->ps.pps->loop_filter_across_slices_enabled_flag &&
            lc->boundary_flags & BOUNDARY_UPPER_SLICE &&
            (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
            (!fc->ps.pps->loop_filter_across_tiles_enabled_flag &&
            lc->boundary_flags & BOUNDARY_UPPER_TILE &&
            (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_upper = 0;

    if (boundary_upper) {
        const RefPicList *rpl_top = (lc->boundary_flags & BOUNDARY_UPPER_SLICE) ?
                                ff_vvc_get_ref_list(fc, fc->ref, x0, y0 - 1) :
                                fc->ref->refPicList;
        int yp_pu = (y0 - 1) >> log2_min_pu_size;
        int yq_pu =  y0      >> log2_min_pu_size;
        int yp_tu = (y0 - 1) >> log2_min_tu_size;
        int yq_tu =  y0      >> log2_min_tu_size;

        for (i = 0; i < width; i += 4) {
            const int off_y = y0 - cb_y;
            int x_pu = (x0 + i) >> log2_min_pu_size;
            int x_tu = (x0 + i) >> log2_min_tu_size;
            MvField *top  = &tab_mvf[yp_pu * min_pu_width + x_pu];
            MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];
            uint8_t top_cbf_luma  = fc->tab.tu_coded_flag[LUMA][yp_tu * min_tu_width + x_tu];
            uint8_t curr_cbf_luma = fc->tab.tu_coded_flag[LUMA][yq_tu * min_tu_width + x_tu];
            const uint8_t pcmf    = fc->tab.pcmf[LUMA][yp_tu * min_tu_width + x_tu] &&
                fc->tab.pcmf[LUMA][yq_tu * min_tu_width + x_tu];

            if (pcmf)
                bs = 0;
            else if (curr->pred_flag == PF_INTRA || top->pred_flag == PF_INTRA || curr->ciip_flag || top->ciip_flag)
                bs = 2;
            else if (curr_cbf_luma || top_cbf_luma)
                bs = 1;
            else if (off_y && ((off_y % 8) || !has_horizontal_sb))
                bs = 0;                                     //inside a cu, not aligned to 8 or with no subblocks
            else
                bs = boundary_strength(fc, curr, top, rpl_top);

            TAB_BS(fc->tab.horizontal_bs[LUMA], x0 + i, y0) = bs;

            derive_max_filter_length_luma(fc, x0 + i, y0, is_intra, has_horizontal_sb, 0, &max_len_p, &max_len_q);
            TAB_MAX_LEN(fc->tab.horizontal_p, x0 + i, y0) = max_len_p;
            TAB_MAX_LEN(fc->tab.horizontal_q, x0 + i, y0) = max_len_q;
        }
    }

    if (!is_intra) {
        if (fc->tab.msf[off_q] || fc->tab.iaf[off_q])
            vvc_deblock_subblock_bs_horizontal(fc, cb_x, cb_y, x0, y0, width, height);
    }
}

static void vvc_deblock_bs_chroma_vertical(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc  = lc->fc;
    MvField *tab_mvf           = fc->ref->tab_mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int log2_min_tu_size = MIN_PU_LOG2;
    const int min_pu_width     = fc->ps.pps->min_pu_width;
    const int min_tu_width     = fc->ps.pps->min_tb_width;
    int boundary_left, i;

    // bs for vertical TU boundaries
    boundary_left = x0 > 0 && !(x0 & ((CHROMA_GRID << fc->ps.sps->hshift[1]) - 1));
    if (boundary_left &&
        ((!fc->ps.pps->loop_filter_across_slices_enabled_flag &&
          lc->boundary_flags & BOUNDARY_LEFT_SLICE &&
          (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
         (!fc->ps.pps->loop_filter_across_tiles_enabled_flag &&
          lc->boundary_flags & BOUNDARY_LEFT_TILE &&
          (x0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_left = 0;

    if (boundary_left) {
        int xp_pu = (x0 - 1) >> log2_min_pu_size;
        int xq_pu =  x0      >> log2_min_pu_size;
        int xp_tu = (x0 - 1) >> log2_min_tu_size;
        int xq_tu =  x0      >> log2_min_tu_size;

        for (i = 0; i < height; i += 2) {
            int y_pu      = (y0 + i) >> log2_min_pu_size;
            int y_tu      = (y0 + i) >> log2_min_tu_size;
            MvField *left = &tab_mvf[y_pu * min_pu_width + xp_pu];
            MvField *curr = &tab_mvf[y_pu * min_pu_width + xq_pu];
            const int left_tu = y_tu * min_tu_width + xp_tu;
            const int curr_tu = y_tu * min_tu_width + xq_tu;
            const uint8_t pcmf = fc->tab.pcmf[CHROMA][left_tu] && fc->tab.pcmf[CHROMA][curr_tu];

            for (int c = CB; c <= CR; c++) {
                uint8_t cbf  = fc->tab.tu_coded_flag[c][left_tu] |
                    fc->tab.tu_coded_flag[c][curr_tu] |
                    fc->tab.tu_joint_cbcr_residual_flag[left_tu] |
                    fc->tab.tu_joint_cbcr_residual_flag[curr_tu];
                int bs = 0;

                if (pcmf)
                    bs = 0;
                else if (curr->pred_flag == PF_INTRA || left->pred_flag == PF_INTRA || curr->ciip_flag || left->ciip_flag)
                    bs = 2;
                else if (cbf)
                    bs = 1;
                TAB_BS(fc->tab.vertical_bs[c], x0, (y0 + i)) = bs;
            }
        }
    }
}

static void vvc_deblock_bs_chroma_horizontal(const VVCLocalContext *lc,
    const int x0, const int y0, const int width, const int height)
{
    const VVCFrameContext *fc = lc->fc;
    MvField *tab_mvf = fc->ref->tab_mvf;
    const int log2_min_pu_size = MIN_PU_LOG2;
    const int log2_min_tu_size = MIN_PU_LOG2;
    const int min_pu_width = fc->ps.pps->min_pu_width;
    const int min_tu_width = fc->ps.pps->min_tb_width;
    int boundary_upper;
    int i;

    boundary_upper = y0 > 0 && !(y0 & ((CHROMA_GRID << fc->ps.sps->vshift[1]) - 1));
    if (boundary_upper &&
        ((!fc->ps.pps->loop_filter_across_slices_enabled_flag &&
            lc->boundary_flags & BOUNDARY_UPPER_SLICE &&
            (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0) ||
            (!fc->ps.pps->loop_filter_across_tiles_enabled_flag &&
                lc->boundary_flags & BOUNDARY_UPPER_TILE &&
                (y0 % (1 << fc->ps.sps->ctb_log2_size_y)) == 0)))
        boundary_upper = 0;

    if (boundary_upper) {
        int yp_pu = (y0 - 1) >> log2_min_pu_size;
        int yq_pu = y0 >> log2_min_pu_size;
        int yp_tu = (y0 - 1) >> log2_min_tu_size;
        int yq_tu = y0 >> log2_min_tu_size;

        for (i = 0; i < width; i += 2) {
            int x_pu = (x0 + i) >> log2_min_pu_size;
            int x_tu = (x0 + i) >> log2_min_tu_size;
            MvField *top = &tab_mvf[yp_pu * min_pu_width + x_pu];
            MvField *curr = &tab_mvf[yq_pu * min_pu_width + x_pu];
            const int top_tu = yp_tu * min_tu_width + x_tu;
            const int curr_tu = yq_tu * min_tu_width + x_tu;
            const uint8_t pcmf = fc->tab.pcmf[CHROMA][top_tu] && fc->tab.pcmf[CHROMA][curr_tu];

            for (int c = CB; c <= CR; c++) {
                uint8_t cbf = fc->tab.tu_coded_flag[c][top_tu] |
                    fc->tab.tu_coded_flag[c][curr_tu] |
                    fc->tab.tu_joint_cbcr_residual_flag[top_tu] |
                    fc->tab.tu_joint_cbcr_residual_flag[curr_tu];
                int bs = 0;

                if (pcmf)
                    bs = 0;
                else if (curr->pred_flag == PF_INTRA || top->pred_flag == PF_INTRA || curr->ciip_flag || top->ciip_flag)
                    bs = 2;
                else if (cbf)
                    bs = 1;
                TAB_BS(fc->tab.horizontal_bs[c], x0 + i, y0) = bs;
            }
        }
    }
}

typedef void (*deblock_bs_fn)(const VVCLocalContext *lc, const int x0, const int y0,
    const int width, const int height);

static void vvc_deblock_bs(const VVCLocalContext *lc, const int x0, const int y0, const int vertical)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const VVCPPS *pps   = fc->ps.pps;
    const int ctb_size  = sps->ctb_size_y;
    const int x_end     = FFMIN(x0 + ctb_size, pps->width) >> MIN_TB_LOG2;
    const int y_end     = FFMIN(y0 + ctb_size, pps->height) >> MIN_TB_LOG2;
    deblock_bs_fn deblock_bs[2][2] = {
        { vvc_deblock_bs_luma_horizontal, vvc_deblock_bs_chroma_horizontal },
        { vvc_deblock_bs_luma_vertical,   vvc_deblock_bs_chroma_vertical   }
    };

    for (int is_chroma = 0; is_chroma <= 1; is_chroma++) {
        const int hs = sps->hshift[is_chroma];
        const int vs = sps->vshift[is_chroma];
        for (int y = y0 >> MIN_TB_LOG2; y < y_end; y++) {
            for (int x = x0 >> MIN_TB_LOG2; x < x_end; x++) {
                const int off = y * fc->ps.pps->min_tb_width + x;
                if ((fc->tab.tb_pos_x0[is_chroma][off] >> MIN_TB_LOG2) == x && (fc->tab.tb_pos_y0[is_chroma][off] >> MIN_TB_LOG2) == y) {
                    deblock_bs[vertical][is_chroma](lc, x << MIN_TB_LOG2, y << MIN_TB_LOG2,
                        fc->tab.tb_width[is_chroma][off] << hs, fc->tab.tb_height[is_chroma][off] << vs);
                }
            }
        }
    }
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void max_filter_length_luma(const VVCFrameContext *fc, const int qx, const int qy,
                                   const int vertical, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const uint8_t *tab_len_p = vertical ? fc->tab.vertical_p : fc->tab.horizontal_p;
    const uint8_t *tab_len_q = vertical ? fc->tab.vertical_q : fc->tab.horizontal_q;
    *max_len_p = TAB_MAX_LEN(tab_len_p, qx, qy);
    *max_len_q = TAB_MAX_LEN(tab_len_q, qx, qy);
}

//part of 8.8.3.3 Derivation process of transform block boundary
static void max_filter_length_chroma(const VVCFrameContext *fc, const int qx, const int qy,
                                     const int vertical, const int horizontal_ctu_edge, const int bs, uint8_t *max_len_p, uint8_t *max_len_q)
{
    const int px =  vertical ? qx - 1 : qx;
    const int py = !vertical ? qy - 1 : qy;
    const uint8_t *tb_size = vertical ? fc->tab.tb_width[CHROMA] : fc->tab.tb_height[CHROMA];

    const int size_p = tb_size[(py >> MIN_TB_LOG2) * fc->ps.pps->min_tb_width + (px >> MIN_TB_LOG2)];
    const int size_q = tb_size[(qy >> MIN_TB_LOG2) * fc->ps.pps->min_tb_width + (qx >> MIN_TB_LOG2)];
    if (size_p >= 8 && size_q >= 8) {
        *max_len_p = *max_len_q = 3;
        if (horizontal_ctu_edge)
            *max_len_p = 1;
    } else {
        //part of 8.8.3.6.4 Decision process for chroma block edges
        *max_len_p = *max_len_q = (bs == 2);
    }
}

static void max_filter_length(const VVCFrameContext *fc, const int qx, const int qy,
    const int c_idx, const int vertical, const int horizontal_ctu_edge, const int bs, uint8_t *max_len_p, uint8_t *max_len_q)
{
    if (!c_idx)
        max_filter_length_luma(fc, qx, qy, vertical, max_len_p, max_len_q);
    else
        max_filter_length_chroma(fc, qx, qy, vertical, horizontal_ctu_edge, bs, max_len_p, max_len_q);
}

#define TC_CALC(qp, bs)                                                 \
    tctable[av_clip((qp) + DEFAULT_INTRA_TC_OFFSET * ((bs) - 1) +       \
                    (tc_offset & -2),                                   \
                    0, MAX_QP + DEFAULT_INTRA_TC_OFFSET)]

// part of 8.8.3.6.2 Decision process for luma block edges
static int get_qp_y(const VVCFrameContext *fc, const uint8_t *src, const int x, const int y, const int vertical)
{
    const VVCSPS *sps   = fc->ps.sps;
    const int qp        = (ff_vvc_get_qPy(fc, x - vertical, y - !vertical) + ff_vvc_get_qPy(fc, x, y) + 1) >> 1;
    int qp_offset       = 0;
    int level;

    if (!sps->ladf_enabled_flag)
        return qp;

    level = (vertical ? fc->vvcdsp.vvc_v_loop_ladf_level : fc->vvcdsp.vvc_h_loop_ladf_level)(src, fc->frame->linesize[LUMA]);
    qp_offset = sps->ladf_lowest_interval_qp_offset;
    for (int i = 0; i < sps->num_ladf_intervals - 1 && level > sps->ladf_interval_lower_bound[i + 1]; i++)
        qp_offset = sps->ladf_qp_offset[i];

    return qp + qp_offset;
}

// part of 8.8.3.6.2 Decision process for luma block edges
static int get_qp_c(const VVCFrameContext *fc, const int x, const int y, const int c_idx, const int vertical)
{
    const VVCSPS *sps   = fc->ps.sps;
    return (get_qPc(fc, x - vertical, y - !vertical, c_idx) + get_qPc(fc, x, y, c_idx) - 2 * sps->qp_bd_offset + 1) >> 1;
}

static int get_qp(const VVCFrameContext *fc, const uint8_t *src, const int x, const int y, const int c_idx, const int vertical)
{
    if (!c_idx)
        return get_qp_y(fc, src, x, y, vertical);
    return get_qp_c(fc, x, y, c_idx, vertical);
}

void ff_vvc_deblock_vertical(const VVCLocalContext *lc, int x0, int y0)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const int c_end     = fc->ps.sps->chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    uint8_t *src;
    int x, y, beta, tc, qp;
    uint8_t no_p = 0;
    uint8_t no_q = 0;

    int ctb_log2_size_y = fc->ps.sps->ctb_log2_size_y;
    int x_end, y_end;
    int ctb_size = 1 << ctb_log2_size_y;
    int ctb = (x0 >> ctb_log2_size_y) +
        (y0 >> ctb_log2_size_y) * fc->ps.pps->ctb_width;
    DBParams  *params = fc->tab.deblock + ctb;

    vvc_deblock_bs(lc, x0, y0, 1);

    x_end = x0 + ctb_size;
    if (x_end > fc->ps.sps->width)
        x_end = fc->ps.sps->width;
    y_end = y0 + ctb_size;
    if (y_end > fc->ps.sps->height)
        y_end = fc->ps.sps->height;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs            = sps->hshift[c_idx];
        const int vs            = sps->vshift[c_idx];
        const int grid          = c_idx ? (CHROMA_GRID << hs) : LUMA_GRID;
        const int tc_offset     = params->tc_offset[c_idx];
        const int beta_offset   = params->beta_offset[c_idx];

        for (y = y0; y < y_end; y += DEBLOCK_STEP) {
            for (x = x0 ? x0 : grid; x < x_end; x += grid) {
                const int bs = TAB_BS(fc->tab.vertical_bs[c_idx], x, y);
                if (bs) {
                    uint8_t max_len_p, max_len_q;

                    src = &fc->frame->data[c_idx][(y >> vs) * fc->frame->linesize[c_idx] + ((x >> hs) << fc->ps.sps->pixel_shift)];
                    qp = get_qp(fc, src, x, y, c_idx, 1);

                    beta = betatable[av_clip(qp + beta_offset, 0, MAX_QP)];

                    tc = TC_CALC(qp, bs);

                    max_filter_length(fc, x, y, c_idx, 1, 0, bs, &max_len_p, &max_len_q);

                    if (!c_idx) {
                        fc->vvcdsp.vvc_v_loop_filter_luma(src,
                            fc->frame->linesize[c_idx], beta, tc, no_p, no_q, max_len_p, max_len_q);
                    } else {
                        fc->vvcdsp.vvc_v_loop_filter_chroma(src,
                            fc->frame->linesize[c_idx], beta, tc, no_p, no_q, vs, max_len_p, max_len_q);
                    }
                }
            }
        }
    }
}

void ff_vvc_deblock_horizontal(const VVCLocalContext *lc, int x0, int y0)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const int c_end     = fc->ps.sps->chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    uint8_t* src;
    int x, y, beta, tc, qp;
    uint8_t no_p = 0;
    uint8_t no_q = 0;

    int ctb_log2_size_y = fc->ps.sps->ctb_log2_size_y;
    int x_end, x_end2, y_end;
    int ctb_size = 1 << ctb_log2_size_y;
    int ctb = (x0 >> ctb_log2_size_y) +
        (y0 >> ctb_log2_size_y) * fc->ps.pps->ctb_width;
    int tc_offset, beta_offset;

    vvc_deblock_bs(lc, x0, y0, 0);

    x_end = x0 + ctb_size;
    if (x_end > fc->ps.sps->width)
        x_end = fc->ps.sps->width;
    y_end = y0 + ctb_size;
    if (y_end > fc->ps.sps->height)
        y_end = fc->ps.sps->height;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs            = sps->hshift[c_idx];
        const int vs            = sps->vshift[c_idx];
        const int grid          = c_idx ? (CHROMA_GRID << vs) : LUMA_GRID;

        x_end2 = x_end == fc->ps.sps->width ? x_end : x_end - (MAX_FILTER_LEN << hs);

        for (y = y0; y < y_end; y += grid) {
            if (!y)
                continue;

            // horizontal filtering luma
            for (x = x0 ? x0 - (MAX_FILTER_LEN << hs) : 0; x < x_end2; x += DEBLOCK_STEP) {
                const int bs = TAB_BS(fc->tab.horizontal_bs[c_idx], x, y);
                if (bs) {
                    const DBParams  *params = fc->tab.deblock + ctb - (x < x0);
                    const int horizontal_ctu_edge = !(y % fc->ps.sps->ctb_size_y);
                    uint8_t max_len_p, max_len_q;

                    src = &fc->frame->data[c_idx][(y >> vs) * fc->frame->linesize[c_idx] + ((x >> hs) << fc->ps.sps->pixel_shift)];
                    qp = get_qp(fc, src, x, y, c_idx, 0);

                    tc_offset = params->tc_offset[c_idx];
                    beta_offset = params->beta_offset[c_idx];

                    beta = betatable[av_clip(qp + beta_offset, 0, MAX_QP)];
                    tc = TC_CALC(qp, bs);

                    max_filter_length(fc, x, y, c_idx, 0, horizontal_ctu_edge, bs, &max_len_p, &max_len_q);

                    if (!c_idx) {
                        fc->vvcdsp.vvc_h_loop_filter_luma(src, fc->frame->linesize[c_idx],
                            beta, tc, no_p, no_q, max_len_p, max_len_q, horizontal_ctu_edge);
                    } else {
                        fc->vvcdsp.vvc_h_loop_filter_chroma(src, fc->frame->linesize[c_idx], beta,
                            tc, no_p, no_q, hs, max_len_p, max_len_q);
                    }
                }
            }
        }
    }
}

static void alf_copy_border(uint8_t *dst, const uint8_t *src,
    const int pixel_shift, int width, const int height, const ptrdiff_t dst_stride, const ptrdiff_t src_stride)
{
    width <<= pixel_shift;
    for (int i = 0; i < height; i++) {
        memcpy(dst, src, width);
        dst += dst_stride;
        src += src_stride;
    }
}

static void alf_extend_vert(uint8_t *_dst, const uint8_t *_src,
    const int pixel_shift, const int width, const int height, ptrdiff_t stride)
{
    if (pixel_shift == 0) {
        for (int i = 0; i < height; i++) {
            memset(_dst, *_src, width);
            _src += stride;
            _dst += stride;
        }
    } else {
        const uint16_t *src = (const uint16_t *)_src;
        uint16_t *dst = (uint16_t *)_dst;
        stride >>= pixel_shift;

        for (int i = 0; i < height; i++) {
            for (int j = 0; j < width; j++)
                dst[j] = *src;
            src += stride;
            dst += stride;
        }
    }
}

static void alf_extend_horz(uint8_t *dst, const uint8_t *src,
    const int pixel_shift, int width, const int height, const ptrdiff_t stride)
{
    width <<= pixel_shift;
    for (int i = 0; i < height; i++) {
        memcpy(dst, src, width);
        dst += stride;
    }
}

static void alf_copy_ctb_to_hv(VVCFrameContext *fc, const uint8_t *src, const ptrdiff_t src_stride,
    const int x, const int y, const int width, const int height, const int x_ctb, const int y_ctb, const int c_idx)
{
    const int ps            = fc->ps.sps->pixel_shift;
    const int w             = fc->ps.sps->width >> fc->ps.sps->hshift[c_idx];
    const int h             = fc->ps.sps->height >> fc->ps.sps->vshift[c_idx];
    const int border_pixels = (c_idx == 0) ? ALF_BORDER_LUMA : ALF_BORDER_CHROMA;
    const int offset_h[]    = { 0, height - border_pixels };
    const int offset_v[]    = { 0, width  - border_pixels };

    /* copy horizontal edges */
    for (int i = 0; i < FF_ARRAY_ELEMS(offset_h); i++) {
        alf_copy_border(fc->tab.alf_pixel_buffer_h[c_idx][i] + ((border_pixels * y_ctb * w + x)<< ps),
            src + offset_h[i] * src_stride, ps, width, border_pixels, w << ps, src_stride);
    }
    /* copy vertical edges */
    for (int i = 0; i < FF_ARRAY_ELEMS(offset_v); i++) {
        alf_copy_border(fc->tab.alf_pixel_buffer_v[c_idx][i] + ((h * x_ctb + y) * (border_pixels << ps)),
            src + (offset_v[i] << ps), ps, border_pixels, height, border_pixels << ps, src_stride);
    }
}

#define LEFT        0
#define TOP         1
#define RIGHT       2
#define BOTTOM      3
#define MAX_EDGES   4

static void alf_fill_border_h(uint8_t *dst, const ptrdiff_t dst_stride, const uint8_t *src, const ptrdiff_t src_stride,
    const uint8_t *border, const int width, const int border_pixels, const int ps, const int edge)
{
    if (edge)
        alf_extend_horz(dst, border, ps, width, border_pixels, dst_stride);
    else
        alf_copy_border(dst, src, ps, width, border_pixels, dst_stride, src_stride);
}

static void alf_fill_border_v(uint8_t *dst, const ptrdiff_t dst_stride, const uint8_t *src,
    const uint8_t *border, const int border_pixels, const int height, const int pixel_shift, const int *edges, const int edge)
{
    const ptrdiff_t src_stride = (border_pixels << pixel_shift);

    if (edge) {
        alf_extend_vert(dst, border, pixel_shift, border_pixels, height + 2 * border_pixels, dst_stride);
        return;
    }

    //left/right
    alf_copy_border(dst + dst_stride * border_pixels * edges[TOP], src + src_stride * border_pixels * edges[TOP],
        pixel_shift, border_pixels, height + (!edges[TOP] + !edges[BOTTOM]) * border_pixels, dst_stride, src_stride);

    //top left/right
    if (edges[TOP])
        alf_extend_horz(dst, dst + dst_stride * border_pixels, pixel_shift, border_pixels, border_pixels, dst_stride);

    //bottom left/right
    if (edges[BOTTOM]) {
        dst += dst_stride * (border_pixels + height);
        alf_extend_horz(dst, dst - dst_stride, pixel_shift, border_pixels, border_pixels, dst_stride);
    }
}

static void alf_prepare_buffer(VVCFrameContext *fc, uint8_t *_dst, const uint8_t *_src, const int x, const int y,
    const int x_ctb, const int y_ctb, const int width, const int height, const ptrdiff_t dst_stride, const ptrdiff_t src_stride,
    const int c_idx, const int *edges)
{
    const int ps = fc->ps.sps->pixel_shift;
    const int w = fc->ps.sps->width >> fc->ps.sps->hshift[c_idx];
    const int h = fc->ps.sps->height >> fc->ps.sps->vshift[c_idx];
    const int border_pixels = c_idx == 0 ? ALF_BORDER_LUMA : ALF_BORDER_CHROMA;
    uint8_t *dst, *src;

    copy_CTB(_dst, _src, width << ps, height, dst_stride, src_stride);

    //top
    src = fc->tab.alf_pixel_buffer_h[c_idx][1] + (((border_pixels * (y_ctb - 1)) * w + x) << ps);
    dst = _dst - border_pixels * dst_stride;
    alf_fill_border_h(dst, dst_stride, src, w  << ps, _dst, width, border_pixels, ps, edges[TOP]);

    //bottom
    src = fc->tab.alf_pixel_buffer_h[c_idx][0] + ((border_pixels * (y_ctb + 1) * w + x) << ps);
    dst = _dst + height * dst_stride;
    alf_fill_border_h(dst, dst_stride, src, w  << ps, _dst + (height - 1) * dst_stride, width, border_pixels, ps, edges[BOTTOM]);


    //left
    src = fc->tab.alf_pixel_buffer_v[c_idx][1] + (h * (x_ctb - 1) + y - border_pixels) * (border_pixels << ps);
    dst = _dst - (border_pixels << ps) - border_pixels * dst_stride;
    alf_fill_border_v(dst, dst_stride, src,  dst + (border_pixels << ps), border_pixels, height, ps, edges, edges[LEFT]);

    //right
    src = fc->tab.alf_pixel_buffer_v[c_idx][0] + (h * (x_ctb + 1) + y - border_pixels) * (border_pixels << ps);
    dst = _dst + (width << ps) - border_pixels * dst_stride;
    alf_fill_border_v(dst, dst_stride, src,  dst - (1 << ps), border_pixels, height, ps, edges, edges[RIGHT]);
}

#define ALF_BLOCKS_IN_SUBBLOCK      (ALF_SUBBLOCK_SIZE / ALF_BLOCK_SIZE)
#define ALF_SUBBLOCK_FILTER_SIZE    (ALF_BLOCKS_IN_SUBBLOCK * ALF_BLOCKS_IN_SUBBLOCK * ALF_NUM_COEFF_LUMA)

static void alf_filter_luma(VVCLocalContext *lc, uint8_t *_dst, const uint8_t *_src,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride, const int x0, const int y0,
    const int width, const int height, const int vb_pos, ALFParams *alf)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSH *sh             = &lc->sc->sh;
    const int ps                = fc->ps.sps->pixel_shift;
    const int no_vb_height      = y0 + height > vb_pos ? vb_pos - y0 - ALF_VB_POS_ABOVE_LUMA : height;
    int8_t coeff[ALF_SUBBLOCK_FILTER_SIZE];
    int16_t clip[ALF_SUBBLOCK_FILTER_SIZE];
    uint8_t fixed_clip_set[ALF_NUM_FILTERS_LUMA * ALF_NUM_COEFF_LUMA] = { 0 };
    const int8_t  *coeff_set;
    const uint8_t *clip_idx_set;
    const uint8_t *class_to_filt;

    if (alf->ctb_filt_set_idx_y < 16) {
        coeff_set = &ff_vvc_alf_fix_filt_coeff[0][0];
        clip_idx_set = fixed_clip_set;
        class_to_filt = ff_vvc_alf_class_to_filt_map[alf->ctb_filt_set_idx_y];
        //av_assert0(0 && "fixme");
    } else {
        const int id = sh->alf.aps_id_luma[alf->ctb_filt_set_idx_y - 16];
        const VVCALF *aps = (VVCALF *)fc->ps.alf_list[id]->data;
        coeff_set    = aps->luma_coeff;
        clip_idx_set = aps->luma_clip_idx;
        class_to_filt = ff_vvc_alf_aps_class_to_filt_map;
    }

    for (int y = 0; y < no_vb_height; y += ALF_SUBBLOCK_SIZE) {
        for (int x = 0; x < width; x += ALF_SUBBLOCK_SIZE) {
            const uint8_t *src = _src + y * src_stride + (x << ps);
            uint8_t *dst = _dst + y * dst_stride + (x << ps);
            const int w = FFMIN(width  - x, ALF_SUBBLOCK_SIZE);
            const int h = FFMIN(no_vb_height - y, ALF_SUBBLOCK_SIZE);

            fc->vvcdsp.alf_get_coeff_and_clip(src, src_stride, x0 + x, y0 + y, w, h,
                vb_pos, coeff_set, clip_idx_set, class_to_filt, coeff, clip);

            fc->vvcdsp.alf_filter_luma(dst, src, dst_stride, src_stride, w, h, coeff, clip);
        }
    }

    if (height > no_vb_height) {
        const int y = no_vb_height;
        for (int x = 0; x < width; x += ALF_SUBBLOCK_SIZE) {
            const uint8_t *src = _src + y * src_stride + (x << ps);
            uint8_t *dst = _dst + y * dst_stride + (x << ps);
            const int w = FFMIN(width  - x, ALF_SUBBLOCK_SIZE);
            const int h = height - y;

            fc->vvcdsp.alf_get_coeff_and_clip(src, src_stride, x0 + x, y0 + y, w, h, vb_pos, coeff_set, clip_idx_set, class_to_filt, coeff, clip);

            fc->vvcdsp.alf_filter_luma_vb(dst, src, dst_stride, src_stride, w, h, coeff, clip, vb_pos - y0 - y);
        }
    }
}

static int alf_clip_from_idx(const VVCFrameContext *fc, const int idx)
{
    const VVCSPS *sps   = fc->ps.sps;
    const int offset[] = {0, 3, 5, 7};

    return 1 << (sps->bit_depth - offset[idx]);
}

static void alf_filter_chroma(VVCLocalContext *lc, uint8_t *dst, const uint8_t *src,
    const ptrdiff_t dst_stride, const ptrdiff_t src_stride, const int c_idx,
    const int width, const int height, const int vb_pos, ALFParams *alf)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCSH *sh         = &lc->sc->sh;
    const VVCALF *aps       = (VVCALF *)fc->ps.alf_list[sh->alf.aps_id_chroma]->data;
    const int off           = alf->alf_ctb_filter_alt_idx[c_idx - 1] * ALF_NUM_COEFF_CHROMA;
    const int8_t *coeff     = aps->chroma_coeff + off;
    const int no_vb_height  = height > vb_pos ? vb_pos - ALF_VB_POS_ABOVE_CHROMA : height;
    int16_t clip[ALF_NUM_COEFF_CHROMA];

    for (int i = 0; i < ALF_NUM_COEFF_CHROMA; i++)
        clip[i] = alf_clip_from_idx(fc, aps->chroma_clip_idx[off + i]);

    fc->vvcdsp.alf_filter_chroma(dst, src, dst_stride, src_stride, width, no_vb_height, coeff, clip);
    if (no_vb_height < height) {
        dst += dst_stride * no_vb_height;
        src += src_stride * no_vb_height;
        fc->vvcdsp.alf_filter_chroma_vb(dst, src, dst_stride, src_stride, width, height - no_vb_height, coeff, clip, vb_pos - no_vb_height);
    }
}

static void alf_filter_cc(VVCLocalContext *lc, uint8_t *dst, const uint8_t *luma,
    const ptrdiff_t dst_stride, const ptrdiff_t luma_stride, const int c_idx,
    const int width, const int height, const int hs, const int vs, const int vb_pos, ALFParams *alf)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCSH *sh         = &lc->sc->sh;
    const int idx           = c_idx - 1;
    AVBufferRef *aps_buf    = fc->ps.alf_list[sh->alf.cc_aps_id[idx]];

    if (aps_buf) {
        const VVCALF *aps   = (VVCALF *)aps_buf->data;
        const int off       = (alf->ctb_cc_idc[idx] - 1)* ALF_NUM_COEFF_CC;
        const int8_t *coeff = aps->cc_coeff[idx] + off;

        fc->vvcdsp.alf_filter_cc(dst, luma, dst_stride, luma_stride, width, height, hs, vs, coeff, vb_pos);
    }
}

void ff_vvc_alf_copy_ctu_to_hv(VVCLocalContext* lc, const int x0, const int y0)
{
    VVCFrameContext *fc     = lc->fc;
    const int x_ctb         = x0 >> fc->ps.sps->ctb_log2_size_y;
    const int y_ctb         = y0 >> fc->ps.sps->ctb_log2_size_y;
    const int ctb_size_y    = fc->ps.sps->ctb_size_y;
    const int ps            = fc->ps.sps->pixel_shift;
    const int c_end         = fc->ps.sps->chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs        = fc->ps.sps->hshift[c_idx];
        const int vs        = fc->ps.sps->vshift[c_idx];
        const int x         = x0 >> hs;
        const int y         = y0 >> vs;
        const int width     = FFMIN(fc->ps.sps->width - x0, ctb_size_y) >> hs;
        const int height    = FFMIN(fc->ps.sps->height - y0, ctb_size_y) >> vs;

        const int src_stride = fc->frame->linesize[c_idx];
        uint8_t* src = &fc->frame->data[c_idx][y * src_stride + (x << ps)];

        alf_copy_ctb_to_hv(fc, src, src_stride, x, y, width, height, x_ctb, y_ctb, c_idx);
    }
}

void ff_vvc_alf_filter(VVCLocalContext *lc, const int x0, const int y0)
{
    VVCFrameContext *fc     = lc->fc;
    const int x_ctb         = x0 >> fc->ps.sps->ctb_log2_size_y;
    const int y_ctb         = y0 >> fc->ps.sps->ctb_log2_size_y;
    const int ctb_size_y    = fc->ps.sps->ctb_size_y;
    const int ps            = fc->ps.sps->pixel_shift;
    const int padded_stride = EDGE_EMU_BUFFER_STRIDE << ps;
    const int padded_offset = padded_stride * ALF_PADDING_SIZE + (ALF_PADDING_SIZE << ps);
    const int c_end         = fc->ps.sps->chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;
    ALFParams *alf          = &CTB(fc->tab.alf, x_ctb, y_ctb);
    int edges[4];  // 0 left 1 top 2 right 3 bottom

    edges[0]   = x_ctb == 0;
    edges[1]   = y_ctb == 0;
    edges[2]   = x_ctb == fc->ps.pps->ctb_width  - 1;
    edges[3]   = y_ctb == fc->ps.pps->ctb_height - 1;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs = fc->ps.sps->hshift[c_idx];
        const int vs = fc->ps.sps->vshift[c_idx];
        const int ctb_size_h = ctb_size_y >> hs;
        const int ctb_size_v = ctb_size_y >> vs;
        const int x = x0 >> hs;
        const int y = y0 >> vs;
        const int pic_width = fc->ps.sps->width >> hs;
        const int pic_height = fc->ps.sps->height >> vs;
        const int width  = FFMIN(pic_width  - x, ctb_size_h);
        const int height = FFMIN(pic_height - y, ctb_size_v);
        const int src_stride = fc->frame->linesize[c_idx];
        uint8_t *src = &fc->frame->data[c_idx][y * src_stride + (x << ps)];
        uint8_t *padded;

        if (alf->ctb_flag[c_idx] || (!c_idx && (alf->ctb_cc_idc[0] || alf->ctb_cc_idc[1]))) {
            padded = (c_idx ? lc->alf_buffer_chroma : lc->alf_buffer_luma) + padded_offset;
            alf_prepare_buffer(fc, padded, src, x, y, x_ctb, y_ctb, width, height,
                padded_stride, src_stride, c_idx, edges);
        }
        if (alf->ctb_flag[c_idx]) {
            if (!c_idx)  {
                alf_filter_luma(lc, src, padded, src_stride, padded_stride, x, y,
                    width, height, y + ctb_size_v - ALF_VB_POS_ABOVE_LUMA, alf);
            } else {
                alf_filter_chroma(lc, src, padded, src_stride, padded_stride, c_idx,
                    width, height, ctb_size_v - ALF_VB_POS_ABOVE_CHROMA, alf);
            }
        }
        if (c_idx && alf->ctb_cc_idc[c_idx - 1]) {
            padded = lc->alf_buffer_luma + padded_offset;
            alf_filter_cc(lc, src, padded, src_stride, padded_stride, c_idx,
                width, height, hs, vs, (ctb_size_v << vs) - ALF_VB_POS_ABOVE_LUMA, alf);
        }

        alf->applied[c_idx] = 1;
    }
}


void ff_vvc_lmcs_filter(const VVCLocalContext *lc, const int x, const int y)
{
    const SliceContext *sc = lc->sc;
    const VVCFrameContext *fc = lc->fc;
    const int ctb_size  = fc->ps.sps->ctb_size_y;
    const int width     = FFMIN(fc->ps.pps->width  - x, ctb_size);
    const int height    = FFMIN(fc->ps.pps->height - y, ctb_size);
    uint8_t *data       = fc->frame->data[LUMA] + y * fc->frame->linesize[LUMA] + (x << fc->ps.sps->pixel_shift);
    if (sc->sh.lmcs_used_flag)
        fc->vvcdsp.lmcs_filter_luma(data, fc->frame->linesize[LUMA], width, height, fc->ps.ph->lmcs_inv_lut);
}
