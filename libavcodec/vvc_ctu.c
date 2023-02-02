/*
 * VVC CTU decoder
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

#include "vvc_cabac.h"
#include "vvc_ctu.h"
#include "vvc_data.h"
#include "vvc_itx_1d.h"
#include "vvc_mvs.h"

#define PROF_TEMP_SIZE (PROF_BLOCK_SIZE) * sizeof(int16_t)

#define TAB_MSM(fc, depth, x, y) fc->tab.msm[(depth)][((y) >> 5) * fc->ps.pps->width32 + ((x) >> 5)]
#define TAB_ISPMF(fc, x, y) fc->tab.ispmf[((y) >> 6) * fc->ps.pps->width64 + ((x) >> 6)]

typedef enum VVCModeType {
    MODE_TYPE_ALL,
    MODE_TYPE_INTER,
    MODE_TYPE_INTRA,
} VVCModeType;

static void set_tb_pos(const VVCFrameContext *fc, const TransformBlock *tb)
{
    const int x_tb      = tb->x0 >> MIN_TB_LOG2;
    const int y_tb      = tb->y0 >> MIN_TB_LOG2;
    const int hs        = fc->ps.sps->hshift[tb->c_idx];
    const int vs        = fc->ps.sps->vshift[tb->c_idx];
    const int is_chroma = tb->c_idx != 0;
    const int width     = FFMAX(1, tb->tb_width >> (MIN_TB_LOG2 - hs));
    const int end       = y_tb + FFMAX(1, tb->tb_height >> (MIN_TB_LOG2 - vs));

    for (int y = y_tb; y < end; y++) {
        const int off = y * fc->ps.pps->min_tb_width + x_tb;
        for (int i = 0; i < width; i++) {
            fc->tab.tb_pos_x0[is_chroma][off + i] = tb->x0;
            fc->tab.tb_pos_y0[is_chroma][off + i] = tb->y0;
        }
        memset(fc->tab.tb_width [is_chroma] + off, tb->tb_width,  width);
        memset(fc->tab.tb_height[is_chroma] + off, tb->tb_height, width);
    }
}


static void set_tb_tab(uint8_t *tab, uint8_t v, const VVCFrameContext *fc,
    const TransformBlock *tb)
{
    const int width  = tb->tb_width  << fc->ps.sps->hshift[tb->c_idx];
    const int height = tb->tb_height << fc->ps.sps->vshift[tb->c_idx];

    for (int h = 0; h < height; h += MIN_TB_SIZE) {
        const int y = (tb->y0 + h) >> MIN_TB_LOG2;
        const int off = y * fc->ps.pps->min_tb_width + (tb->x0 >> MIN_TB_LOG2);
        const int w = FFMAX(1, width >> MIN_TB_LOG2);
        memset(tab + off, v, w);
    }
}

// 8.7.1 Derivation process for quantization parameters
static int get_qp_y_pred(const VVCLocalContext *lc)
{
    const VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps       = fc->ps.sps;
    const VVCPPS *pps       = fc->ps.pps;
    const CodingUnit *cu    = lc->cu;
    const int ctb_log2_size = sps->ctb_log2_size_y;
    const int ctb_size_mask = (1 << ctb_log2_size) - 1;
    const int xQg           = lc->parse.cu_qg_top_left_x;
    const int yQg           = lc->parse.cu_qg_top_left_y;
    const int min_cb_width  = fc->ps.pps->min_cb_width;
    const int x_cb          = cu->x0 >> sps->min_cb_log2_size_y;
    const int y_cb          = cu->y0 >> sps->min_cb_log2_size_y;
    const int x_ctb         = cu->x0 >> ctb_log2_size;
    const int y_ctb         = cu->y0 >> ctb_log2_size;
    const int in_same_ctb_a = ((xQg - 1) >> ctb_log2_size) == x_ctb && (yQg >> ctb_log2_size) == y_ctb;
    const int in_same_ctb_b = (xQg >> ctb_log2_size) == x_ctb && ((yQg - 1) >> ctb_log2_size) == y_ctb;
    int qPy_pred, qPy_a, qPy_b;

    if (lc->na.cand_up) {
        const int first_qg_in_ctu = !(xQg & ctb_size_mask) &&  !(yQg & ctb_size_mask);
        const int qPy_up          = fc->tab.qp[LUMA][x_cb + (y_cb - 1) * min_cb_width];
        if (first_qg_in_ctu && pps->ctb_to_col_bd[xQg >> ctb_log2_size] == xQg)
            return qPy_up;
    }

    // qPy_pred
    qPy_pred = lc->ep->is_first_qg ? lc->sc->sh.slice_qp_y : lc->ep->qp_y;

    // qPy_b
    if (!lc->na.cand_up || !in_same_ctb_b)
        qPy_b = qPy_pred;
    else
        qPy_b = fc->tab.qp[LUMA][x_cb + (y_cb - 1) * min_cb_width];

    // qPy_a
    if (!lc->na.cand_left || !in_same_ctb_a)
        qPy_a = qPy_pred;
    else
        qPy_a = fc->tab.qp[LUMA][(x_cb - 1) + y_cb * min_cb_width];

    av_assert2(qPy_a >= -fc->ps.sps->qp_bd_offset && qPy_a < 63);
    av_assert2(qPy_b >= -fc->ps.sps->qp_bd_offset && qPy_b < 63);

    return (qPy_a + qPy_b + 1) >> 1;
}

static int set_qp_y(VVCLocalContext *lc, const int x0, const int y0, const int has_qp_delta)
{
    const VVCSPS *sps   = lc->fc->ps.sps;
    EntryPoint *ep      = lc->ep;
    CodingUnit *cu      = lc->cu;
    int cu_qp_delta     = 0;

    if (!lc->fc->ps.pps->cu_qp_delta_enabled_flag) {
        ep->qp_y = lc->sc->sh.slice_qp_y;
    } else if (ep->is_first_qg || (lc->parse.cu_qg_top_left_x == x0 && lc->parse.cu_qg_top_left_y == y0)) {
        ep->qp_y = get_qp_y_pred(lc);
        ep->is_first_qg = 0;
    }

    if (has_qp_delta) {
        const int cu_qp_delta_abs = ff_vvc_cu_qp_delta_abs(lc);

        if (cu_qp_delta_abs)
            cu_qp_delta = ff_vvc_cu_qp_delta_sign_flag(lc) ? -cu_qp_delta_abs : cu_qp_delta_abs;
        if (cu_qp_delta > (31 + sps->qp_bd_offset / 2) || cu_qp_delta < -(32 + sps->qp_bd_offset / 2))
            return AVERROR_INVALIDDATA;
        lc->parse.is_cu_qp_delta_coded = 1;

        if (cu_qp_delta) {
            int off = sps->qp_bd_offset;
            ep->qp_y = FFUMOD(ep->qp_y + cu_qp_delta + 64 + 2 * off, 64 + off) - off;
        }
    }

    ff_vvc_set_cb_tab(lc, lc->fc->tab.qp[LUMA], ep->qp_y);
    cu->qp[LUMA] = ep->qp_y;

    return 0;
}

static void set_qp_c_tab(const VVCLocalContext *lc, const TransformUnit *tu, const TransformBlock *tb)
{
    const int is_jcbcr = tu->joint_cbcr_residual_flag && tu->coded_flag[CB] && tu->coded_flag[CR];
    const int idx = is_jcbcr ? JCBCR : tb->c_idx;

    set_tb_tab(lc->fc->tab.qp[tb->c_idx], lc->cu->qp[idx], lc->fc, tb);
}

static void set_qp_c(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCSH *sh             = &lc->sc->sh;
    CodingUnit *cu              = lc->cu;
    const int x_center          = cu->x0 + cu->cb_width  / 2;
    const int y_center          = cu->y0 + cu->cb_height / 2;
    const int single_tree       = cu->tree_type == SINGLE_TREE;
    const int qp_luma           = (single_tree ? lc->ep->qp_y : ff_vvc_get_qPy(fc, x_center, y_center)) + sps->qp_bd_offset;
    const int qp_chroma         = av_clip(qp_luma, 0, MAX_QP + sps->qp_bd_offset);
    int qp;

    for (int i = CB - 1; i < CR + sps->joint_cbcr_enabled_flag; i++) {
        qp = sps->chroma_qp_table[i][qp_chroma];
        qp = qp + pps->chroma_qp_offset[i] + sh->chroma_qp_offset[i] + lc->parse.chroma_qp_offset[i];
        qp = av_clip(qp, -sps->qp_bd_offset, MAX_QP) + sps->qp_bd_offset;
        cu->qp[i + 1] = qp;
    }
}

static TransformUnit* add_tu(CodingUnit *cu, const int x0, const int y0, const int tu_width, const int tu_height)
{
    TransformUnit *tu;

    if (cu->num_tus >= FF_ARRAY_ELEMS(cu->tus))
        return NULL;

    tu = &cu->tus[cu->num_tus];
    tu->x0 = x0;
    tu->y0 = y0;
    tu->width = tu_width;
    tu->height = tu_height;
    tu->joint_cbcr_residual_flag = 0;
    memset(tu->coded_flag, 0, sizeof(tu->coded_flag));
    tu->nb_tbs = 0;
    cu->num_tus++;

    return tu;
}

static TransformBlock* add_tb(TransformUnit *tu, VVCLocalContext *lc,
    const int x0, const int y0, const int tb_width, const int tb_height, const int c_idx)
{
    TransformBlock *tb;

    tb = &tu->tbs[tu->nb_tbs++];
    tb->has_coeffs = 0;
    tb->x0 = x0;
    tb->y0 = y0;
    tb->tb_width  = tb_width;
    tb->tb_height = tb_height;
    tb->log2_tb_width  = log2(tb_width);
    tb->log2_tb_height = log2(tb_height);

    tb->max_scan_x = tb->max_scan_y = 0;
    tb->min_scan_x = tb->min_scan_y = 0;

    tb->c_idx = c_idx;
    tb->ts = 0;
    tb->coeffs = lc->coeffs;
    lc->coeffs += tb_width * tb_height;
    return tb;
}

static uint8_t tu_y_coded_flag_decode(VVCLocalContext *lc, const int is_sbt_not_coded,
    const int sub_tu_index, const int is_isp, const int is_chroma_coded)
{
    uint8_t tu_y_coded_flag = 0;
    const VVCSPS *sps       = lc->fc->ps.sps;
    CodingUnit *cu          = lc->cu;

    if (!is_sbt_not_coded) {
        int has_y_coded_flag = sub_tu_index < cu->num_intra_subpartitions - 1 || !lc->parse.infer_tu_cbf_luma;
        if (!is_isp) {
            const int is_large = cu->cb_width > sps->max_tb_size_y || cu->cb_height > sps->max_tb_size_y;
            has_y_coded_flag = (cu->pred_mode == MODE_INTRA && !cu->act_enabled_flag) || is_chroma_coded || is_large;
        }
        tu_y_coded_flag = has_y_coded_flag ? ff_vvc_tu_y_coded_flag(lc) : 1;
    }
    if (is_isp)
        lc->parse.infer_tu_cbf_luma = lc->parse.infer_tu_cbf_luma && !tu_y_coded_flag;
    return tu_y_coded_flag;
}

static void chroma_qp_offset_decode(VVCLocalContext *lc, const int is_128, const int is_chroma_coded)
{
    const VVCPPS *pps   = lc->fc->ps.pps;
    const VVCSH *sh     = &lc->sc->sh;

    if ((is_128 || is_chroma_coded) &&
        sh->cu_chroma_qp_offset_enabled_flag && !lc->parse.is_cu_chroma_qp_offset_coded) {
        const int cu_chroma_qp_offset_flag = ff_vvc_cu_chroma_qp_offset_flag(lc);
        if (cu_chroma_qp_offset_flag) {
            int cu_chroma_qp_offset_idx = 0;
            if (pps->chroma_qp_offset_list_len_minus1 > 0)
                cu_chroma_qp_offset_idx = ff_vvc_cu_chroma_qp_offset_idx(lc);
            for (int i = CB - 1; i < JCBCR; i++)
                lc->parse.chroma_qp_offset[i] = pps->chroma_qp_offset_list[cu_chroma_qp_offset_idx][i];
        } else {
            memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));
        }
        lc->parse.is_cu_chroma_qp_offset_coded = 1;
    }
}

static int hls_transform_unit(VVCLocalContext *lc, int x0, int y0,int tu_width, int tu_height, int sub_tu_index, int ch_type)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    const VVCPPS *pps   = fc->ps.pps;
    CodingUnit *cu      = lc->cu;
    TransformUnit *tu   = add_tu(cu, x0, y0, tu_width, tu_height);
    const int min_cb_width      = pps->min_cb_width;
    const VVCTreeType tree_type = cu->tree_type;
    const int is_128            = cu->cb_width > 64 || cu->cb_height > 64;
    const int is_isp            = cu->isp_split_type != ISP_NO_SPLIT;
    const int is_isp_last_tu    = is_isp && (sub_tu_index == cu->num_intra_subpartitions - 1);
    const int is_sbt_not_coded  = cu->sbt_flag &&
        ((sub_tu_index == 0 && cu->sbt_pos_flag) || (sub_tu_index == 1 && !cu->sbt_pos_flag));
    const int chroma_available  = tree_type != DUAL_TREE_LUMA && sps->chroma_format_idc &&
        (!is_isp || is_isp_last_tu);
    int ret, xc, yc, wc, hc, is_chroma_coded;

    if (!tu)
        return AVERROR_INVALIDDATA;

    if (tree_type == SINGLE_TREE && is_isp_last_tu) {
        const int x_cu = x0 >> fc->ps.sps->min_cb_log2_size_y;
        const int y_cu = y0 >> fc->ps.sps->min_cb_log2_size_y;
        xc = SAMPLE_CTB(fc->tab.cb_pos_x[ch_type],  x_cu, y_cu);
        yc = SAMPLE_CTB(fc->tab.cb_pos_y[ch_type],  x_cu, y_cu);
        wc = SAMPLE_CTB(fc->tab.cb_width[ch_type],  x_cu, y_cu);
        hc = SAMPLE_CTB(fc->tab.cb_height[ch_type], x_cu, y_cu);
    } else {
        xc = x0, yc = y0, wc = tu_width, hc = tu_height;
    }

    if (chroma_available && !is_sbt_not_coded) {
        tu->coded_flag[CB] = ff_vvc_tu_cb_coded_flag(lc);
        tu->coded_flag[CR] = ff_vvc_tu_cr_coded_flag(lc, tu->coded_flag[CB]);
    }

    is_chroma_coded = chroma_available && (tu->coded_flag[CB] || tu->coded_flag[CR]);

    if (tree_type != DUAL_TREE_CHROMA) {
        int has_qp_delta;
        tu->coded_flag[LUMA] = tu_y_coded_flag_decode(lc, is_sbt_not_coded, sub_tu_index, is_isp, is_chroma_coded);
        has_qp_delta = (is_128 || tu->coded_flag[LUMA] || is_chroma_coded) &&
            pps->cu_qp_delta_enabled_flag && !lc->parse.is_cu_qp_delta_coded;
        ret = set_qp_y(lc, x0, y0, has_qp_delta);
        if (ret < 0)
            return ret;
        add_tb(tu, lc, x0, y0, tu_width, tu_height, LUMA);
    }
    if (tree_type != DUAL_TREE_LUMA) {
        chroma_qp_offset_decode(lc, is_128, is_chroma_coded);
        if (chroma_available) {
            const int hs = sps->hshift[CHROMA];
            const int vs = sps->vshift[CHROMA];
            add_tb(tu, lc, xc, yc, wc >> hs, hc >> vs, CB);
            add_tb(tu, lc, xc, yc, wc >> hs, hc >> vs, CR);
        }
    }
    if (sps->joint_cbcr_enabled_flag && ((cu->pred_mode == MODE_INTRA &&
        (tu->coded_flag[CB] || tu->coded_flag[CR])) ||
        (tu->coded_flag[CB] && tu->coded_flag[CR])) &&
        chroma_available) {
        tu->joint_cbcr_residual_flag = ff_vvc_tu_joint_cbcr_residual_flag(lc, tu->coded_flag[1], tu->coded_flag[2]);
    }

    for (int i = 0; i < tu->nb_tbs; i++) {
        TransformBlock *tb  = &tu->tbs[i];
        const int is_chroma = tb->c_idx != LUMA;
        tb->has_coeffs = tu->coded_flag[tb->c_idx];
        if (tb->has_coeffs && is_chroma)
            tb->has_coeffs = tb->c_idx == CB ? 1 : !(tu->coded_flag[CB] && tu->joint_cbcr_residual_flag);
        if (tb->has_coeffs) {
            tb->ts = cu->bdpcm_flag[tb->c_idx];
            if (sps->transform_skip_enabled_flag && !cu->bdpcm_flag[tb->c_idx] &&
                tb->tb_width <= sps->max_ts_size && tb->tb_height <= sps->max_ts_size &&
                !cu->sbt_flag && (is_chroma || !is_isp)) {
                tb->ts = ff_vvc_transform_skip_flag(lc, is_chroma);
            }
            ret = ff_vvc_residual_coding(lc, tb);
            if (ret < 0)
                return ret;
            set_tb_tab(fc->tab.tu_coded_flag[tb->c_idx], tu->coded_flag[tb->c_idx], fc, tb);
        }
        if (tb->c_idx != CR)
            set_tb_pos(fc, tb);
        if (tb->c_idx == CB)
            set_tb_tab(fc->tab.tu_joint_cbcr_residual_flag, tu->joint_cbcr_residual_flag, fc, tb);
    }

    return 0;
}

static int hls_transform_tree(VVCLocalContext *lc, int x0, int y0,int tu_width, int tu_height, int ch_type)
{
    const CodingUnit *cu = lc->cu;
    const VVCSPS *sps = lc->fc->ps.sps;
    int ret;

    lc->parse.infer_tu_cbf_luma = 1;
    if (cu->isp_split_type == ISP_NO_SPLIT && !cu->sbt_flag) {
        if (tu_width > sps->max_tb_size_y || tu_height > sps->max_tb_size_y) {
            const int ver_split_first = tu_width > sps->max_tb_size_y && tu_width > tu_height;
            const int trafo_width  =  ver_split_first ? (tu_width  / 2) : tu_width;
            const int trafo_height = !ver_split_first ? (tu_height / 2) : tu_height;

            #define TRANSFORM_TREE(x, y) do {                                           \
                ret = hls_transform_tree(lc, x, y, trafo_width, trafo_height, ch_type);  \
                if (ret < 0)                                                            \
                    return ret;                                                         \
            } while (0)

            TRANSFORM_TREE(x0, y0);
            if (ver_split_first)
                TRANSFORM_TREE(x0 + trafo_width, y0);
            else
                TRANSFORM_TREE(x0, y0 + trafo_height);

        } else {
            ret = hls_transform_unit(lc, x0, y0, tu_width, tu_height, 0, ch_type);
            if (ret < 0)
                return ret;

        }
    } else if (cu->sbt_flag) {
        if (!cu->sbt_horizontal_flag) {
            #define TRANSFORM_UNIT(x, width, idx) do {                              \
                ret = hls_transform_unit(lc, x, y0, width, tu_height, idx, ch_type); \
                if (ret < 0)                                                        \
                    return ret;                                                     \
            } while (0)

            const int trafo_width = tu_width * lc->parse.sbt_num_fourths_tb0 / 4;
            TRANSFORM_UNIT(x0, trafo_width, 0);
            TRANSFORM_UNIT(x0 + trafo_width, tu_width - trafo_width, 1);

            #undef TRANSFORM_UNIT
        } else {
            #define TRANSFORM_UNIT(y, height, idx) do {                             \
                ret = hls_transform_unit(lc, x0, y, tu_width, height, idx, ch_type); \
                if (ret < 0)                                                        \
                    return ret;                                                     \
            } while (0)

            const int trafo_height = tu_height * lc->parse.sbt_num_fourths_tb0 / 4;
            TRANSFORM_UNIT(y0, trafo_height, 0);
            TRANSFORM_UNIT(y0 + trafo_height, tu_height - trafo_height, 1);

            #undef TRANSFORM_UNIT
        }
    } else if (cu->isp_split_type == ISP_HOR_SPLIT) {
        const int trafo_height = tu_height / cu->num_intra_subpartitions;
        for (int i = 0; i < cu->num_intra_subpartitions; i++) {
            ret = hls_transform_unit(lc, x0, y0 + trafo_height * i, tu_width, trafo_height, i, 0);
            if (ret < 0)
                return ret;
        }
    } else if (cu->isp_split_type == ISP_VER_SPLIT) {
        const int trafo_width = tu_width / cu->num_intra_subpartitions;
        for (int i = 0; i < cu->num_intra_subpartitions; i++) {
            ret = hls_transform_unit(lc, x0 + trafo_width * i , y0, trafo_width, tu_height, i, 0);
            if (ret < 0)
                return ret;
        }
    }

    return 0;
}

static int skipped_transform_tree(VVCLocalContext *lc, int x0, int y0,int tu_width, int tu_height)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;

    if (tu_width > sps->max_tb_size_y || tu_height > sps->max_tb_size_y) {
        const int ver_split_first = tu_width > sps->max_tb_size_y && tu_width > tu_height;
        const int trafo_width  =  ver_split_first ? (tu_width  / 2) : tu_width;
        const int trafo_height = !ver_split_first ? (tu_height / 2) : tu_height;

        #define SKIPPED_TRANSFORM_TREE(x, y) do {                                   \
            int ret = skipped_transform_tree(lc, x, y, trafo_width, trafo_height);  \
            if (ret < 0)                                                            \
                return ret;                                                         \
        } while (0)

        SKIPPED_TRANSFORM_TREE(x0, y0);
        if (ver_split_first)
            SKIPPED_TRANSFORM_TREE(x0 + trafo_width, y0);
        else
            SKIPPED_TRANSFORM_TREE(x0, y0 + trafo_height);
    } else {
        TransformUnit *tu = add_tu(lc->cu, x0, y0, tu_width, tu_height);
        const int c_end = sps->chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : (LUMA + 1);
        if (!tu)
            return AVERROR_INVALIDDATA;
        for (int i = LUMA; i < c_end; i++) {
            TransformBlock *tb = add_tb(tu, lc, x0, y0, tu_width >> sps->hshift[i], tu_height >> sps->vshift[i], i);
            if (i != CR)
                set_tb_pos(fc, tb);
        }
    }

    return 0;
}

//6.4.1 Allowed quad split process
//6.4.2 Allowed binary split process
//6.4.3 Allowed ternary split process
static void can_split(const VVCLocalContext *lc, int x0, int y0,int cb_width, int cb_height,
     int mtt_depth, int depth_offset, int part_idx, VVCSplitMode last_split_mode,
     VVCTreeType tree_type, VVCModeType mode_type, VVCAllowedSplit* split)
{
    int min_qt_size, max_bt_size, max_tt_size, max_mtt_depth;
    const VVCFrameContext *fc   = lc->fc;
    const VVCSH *sh             = &lc->sc->sh;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const int chroma            = tree_type == DUAL_TREE_CHROMA;
    int min_cb_size_y           = sps->min_cb_size_y;
    int *qt                     = &split->qt;
    int *btv                    = &split->btv;
    int *bth                    = &split->bth;
    int *ttv                    = &split->ttv;
    int *tth                    = &split->tth;

    *qt = *bth = *btv = *tth = *ttv = 1;

    if (mtt_depth)
        *qt = 0;

    min_qt_size = sh->min_qt_size[chroma];
    if (cb_width <= min_qt_size)
        *qt = 0;

    if (chroma) {
        int chroma_area = (cb_width >> sps->hshift[1]) * (cb_height >> sps->vshift[1]);
        int chroma_width = cb_width >> sps->hshift[1];

        if (chroma_width == 8)
            *ttv = 0;
        else if (chroma_width <= 4) {
            if (chroma_width == 4)
                *btv = 0;
            *qt = 0;
        }
        if (mode_type == MODE_TYPE_INTRA)
            *qt = *btv = *bth = *ttv = *tth = 0;
        if (chroma_area <= 32) {
            *ttv = *tth = 0;
            if (chroma_area <= 16)
                *btv = *bth = 0;
        }
    }
    max_bt_size = sh->max_bt_size[chroma];
    max_tt_size = sh->max_tt_size[chroma];
    max_mtt_depth = sh->max_mtt_depth[chroma] + depth_offset;

    if (mode_type == MODE_TYPE_INTER) {
        int area = cb_width * cb_height;
        if (area == 32)
            *btv = *bth = 0;
        else if (area == 64)
            *ttv = *tth = 0;
    }
    if (cb_width <= 2 * min_cb_size_y) {
        *ttv = 0;
        if (cb_width <= min_cb_size_y)
            *btv = 0;
    }
    if (cb_height <= 2 * min_cb_size_y) {
        *tth = 0;
        if (cb_height <= min_cb_size_y)
            *bth = 0;
    }
    if (cb_width > max_bt_size || cb_height > max_bt_size)
        *btv = *bth = 0;
    max_tt_size = FFMIN(64, max_tt_size);
    if (cb_width > max_tt_size || cb_height > max_tt_size)
        *ttv = *tth = 0;
    if (mtt_depth >= max_mtt_depth)
        *btv = *bth = *ttv = *tth = 0;
    if (x0 + cb_width > pps->width) {
        *ttv = *tth = 0;
        if (cb_height > 64)
            *btv = 0;
        if (y0 + cb_height <= pps->height)
            *bth = 0;
        else if (cb_width > min_qt_size)
            *btv = *bth = 0;
    }
    if (y0 + cb_height > pps->height) {
        *btv = *ttv = *tth = 0;
        if (cb_width > 64)
            *bth = 0;
    }
    if (mtt_depth > 0 && part_idx  == 1)  {
        if (last_split_mode == SPLIT_TT_VER)
            *btv = 0;
        else if (last_split_mode == SPLIT_TT_HOR)
            *bth = 0;
    }
    if (cb_width <= 64 && cb_height > 64)
        *btv = 0;
    if (cb_width > 64 && cb_height <= 64)
        *bth = 0;
}

static int get_num_intra_subpartitions(enum IspType isp_split_type, int cb_width, int cb_height)
{
    if (isp_split_type == ISP_NO_SPLIT)
        return 1;
    if ((cb_width == 4 && cb_height == 8) || (cb_width == 8 && cb_height == 4))
        return 2;
    return 4;
}

static int get_cclm_enabled(const VVCLocalContext *lc, const int x0, const int y0)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps   = fc->ps.sps;
    int enabled = 0;

    if (!sps->cclm_enabled_flag)
        return 0;
    if (!sps->qtbtt_dual_tree_intra_flag || !IS_I(&lc->sc->sh) || sps->ctb_log2_size_y < 6)
        return 1;
    else {
        const int x64 = x0 >> 6 << 6;
        const int y64 = y0 >> 6 << 6;
        const int y32 = y0 >> 5 << 5;
        const int x64_cu = x64 >> fc->ps.sps->min_cb_log2_size_y;
        const int y64_cu = y64 >> fc->ps.sps->min_cb_log2_size_y;
        const int y32_cu = y32 >> fc->ps.sps->min_cb_log2_size_y;
        const int min_cb_width = fc->ps.pps->min_cb_width;
        const int depth = SAMPLE_CTB(fc->tab.cqt_depth[1], x64_cu, y64_cu);
        const int min_depth = fc->ps.sps->ctb_log2_size_y - 6;
        const VVCSplitMode msm64 = (VVCSplitMode)TAB_MSM(fc, 0, x64, y64);
        const VVCSplitMode msm32 = (VVCSplitMode)TAB_MSM(fc, 1, x64, y32);

        enabled = SAMPLE_CTB(fc->tab.cb_width[1], x64_cu, y64_cu) == 64 &&
            SAMPLE_CTB(fc->tab.cb_height[1], x64_cu, y64_cu) == 64;
        enabled |= depth == min_depth && msm64 == SPLIT_BT_HOR &&
            SAMPLE_CTB(fc->tab.cb_width[1], x64_cu, y32_cu) == 64 &&
            SAMPLE_CTB(fc->tab.cb_height[1], x64_cu, y32_cu) == 32;
        enabled |= depth > min_depth;
        enabled |= depth == min_depth && msm64 == SPLIT_BT_HOR && msm32 == SPLIT_BT_VER;

        if (enabled) {
            const int w = SAMPLE_CTB(fc->tab.cb_width[0], x64_cu, y64_cu);
            const int h = SAMPLE_CTB(fc->tab.cb_height[0], x64_cu, y64_cu);
            const int depth0 = SAMPLE_CTB(fc->tab.cqt_depth[0], x64_cu, y64_cu);
            if ((w == 64 && h == 64 && TAB_ISPMF(fc, x64, y64)) ||
                ((w < 64 || h < 64) && depth0 == min_depth))
                return 0;
        }

    }

    return enabled;
}

static int less(const void *a, const void *b)
{
    return *(const int*)a - *(const int*)b;
}

//8.4.2 Derivation process for luma intra prediction mode
static enum IntraPredMode luma_intra_pred_mode(VVCLocalContext* lc, const int intra_subpartitions_mode_flag)
{
    VVCFrameContext *fc     = lc->fc;
    CodingUnit *cu          = lc->cu;
    const int x0            = cu->x0;
    const int y0            = cu->y0;
    enum IntraPredMode pred;
    int intra_luma_not_planar_flag = 1;
    int intra_luma_mpm_remainder = 0;
    int intra_luma_mpm_flag = 1;
    int intra_luma_mpm_idx = 0;

    if (!cu->intra_luma_ref_idx)
        intra_luma_mpm_flag = ff_vvc_intra_luma_mpm_flag(lc);
    if (intra_luma_mpm_flag) {
        if (!cu->intra_luma_ref_idx)
            intra_luma_not_planar_flag = ff_vvc_intra_luma_not_planar_flag(lc, intra_subpartitions_mode_flag);
        if (intra_luma_not_planar_flag)
            intra_luma_mpm_idx = ff_vvc_intra_luma_mpm_idx(lc);
    } else {
        intra_luma_mpm_remainder = ff_vvc_intra_luma_mpm_remainder(lc);
    }

    if (!intra_luma_not_planar_flag) {
        pred = INTRA_PLANAR;
    } else {
        const VVCSPS *sps       = fc->ps.sps;
        const int x_a           = (x0 - 1) >> sps->min_cb_log2_size_y;
        const int y_a           = (y0 + cu->cb_height - 1) >> sps->min_cb_log2_size_y;
        const int x_b           = (x0 + cu->cb_width - 1) >> sps->min_cb_log2_size_y;
        const int y_b           = (y0 - 1) >> sps->min_cb_log2_size_y;
        int min_cb_width        = fc->ps.pps->min_cb_width;
        int x0b                 = av_mod_uintp2(x0, sps->ctb_log2_size_y);
        int y0b                 = av_mod_uintp2(y0, sps->ctb_log2_size_y);
        const int available_l   = lc->ctb_left_flag || x0b;
        const int available_u   = lc->ctb_up_flag || y0b;

        int a, b, cand[5];

       if (!available_l || (SAMPLE_CTB(fc->tab.cpm[0], x_a, y_a) != MODE_INTRA) ||
            SAMPLE_CTB(fc->tab.imf, x_a, y_a)) {
            a = INTRA_PLANAR;
        } else {
            a = SAMPLE_CTB(fc->tab.ipm, x_a, y_a);
        }

        if (!available_u || (SAMPLE_CTB(fc->tab.cpm[0], x_b, y_b) != MODE_INTRA) ||
            SAMPLE_CTB(fc->tab.imf, x_b, y_b) || !y0b) {
            b = INTRA_PLANAR;
        } else {
            b = SAMPLE_CTB(fc->tab.ipm, x_b, y_b);
        }

        if (a == b && a > INTRA_DC) {
            cand[0] = a;
            cand[1] = 2 + ((a + 61) % 64);
            cand[2] = 2 + ((a -  1) % 64);
            cand[3] = 2 + ((a + 60) % 64);
            cand[4] = 2 + (a % 64);
        } else {
            const int minab = FFMIN(a, b);
            const int maxab = FFMAX(a, b);
            if (a > INTRA_DC && b > INTRA_DC) {
                const int diff = maxab - minab;
                cand[0] = a;
                cand[1] = b;
                if (diff == 1) {
                    cand[2] = 2 + ((minab + 61) % 64);
                    cand[3] = 2 + ((maxab - 1) % 64);
                    cand[4] = 2 + ((minab + 60) % 64);
                } else if (diff >= 62) {
                    cand[2] = 2 + ((minab - 1) % 64);
                    cand[3] = 2 + ((maxab + 61) % 64);
                    cand[4] = 2 + (minab % 64);
                } else if (diff == 2) {
                    cand[2] = 2 + ((minab - 1) % 64);
                    cand[3] = 2 + ((minab + 61) % 64);
                    cand[4] = 2 + ((maxab - 1) % 64);
                } else {
                    cand[2] = 2 + ((minab + 61) % 64);
                    cand[3] = 2 + ((minab - 1) % 64);
                    cand[4] = 2 + ((maxab + 61) % 64);
                }
            } else if (a > INTRA_DC || b > INTRA_DC) {
                cand[0] = maxab;
                cand[1] = 2 + ((maxab + 61 ) % 64);
                cand[2] = 2 + ((maxab - 1) % 64);
                cand[3] = 2 + ((maxab + 60 ) % 64);
                cand[4] = 2 + (maxab % 64);
            } else {
                cand[0] = INTRA_DC;
                cand[1] = INTRA_VERT;
                cand[2] = INTRA_HORZ;
                cand[3] = INTRA_VERT - 4;
                cand[4] = INTRA_VERT + 4;
            }
        }
        if (intra_luma_mpm_flag) {
            pred = cand[intra_luma_mpm_idx];
        } else {
            qsort(cand, FF_ARRAY_ELEMS(cand), sizeof(cand[0]), less);
            pred = intra_luma_mpm_remainder + 1;
            for (int i = 0; i < FF_ARRAY_ELEMS(cand); i++) {
                if (pred >= cand[i])
                    pred++;
            }
        }
    }
    return pred;
}

static int lfnst_idx_decode(VVCLocalContext *lc)
{
    CodingUnit  *cu             = lc->cu;
    const VVCTreeType tree_type = cu->tree_type;
    const VVCSPS *sps           = lc->fc->ps.sps;
    const int cb_width          = cu->cb_width;
    const int cb_height         = cu->cb_height;
    int lfnst_width, lfnst_height, min_lfnst;
    int lfnst_idx = 0;

    memset(cu->apply_lfnst_flag, 0, sizeof(cu->apply_lfnst_flag));

    if (!sps->lfnst_enabled_flag || cu->pred_mode != MODE_INTRA || FFMAX(cb_width, cb_height) > sps->max_tb_size_y)
        return 0;

    for (int i = 0; i < cu->num_tus; i++) {
        const TransformUnit  *tu  = &cu->tus[i];
        for (int j = 0; j < tu->nb_tbs; j++) {
            const TransformBlock *tb = tu->tbs + j;
            if (tu->coded_flag[tb->c_idx] && tb->ts)
                return 0;
        }
    }

    if (tree_type == DUAL_TREE_CHROMA) {
        lfnst_width  = cb_width  >> sps->hshift[1];
        lfnst_height = cb_height >> sps->vshift[1];
    } else {
        const int vs = cu->isp_split_type == ISP_VER_SPLIT;
        const int hs = cu->isp_split_type == ISP_HOR_SPLIT;
        lfnst_width = vs ? cb_width / cu->num_intra_subpartitions : cb_width;
        lfnst_height = hs ? cb_height / cu->num_intra_subpartitions : cb_height;
    }
    min_lfnst = FFMIN(lfnst_width, lfnst_height);
    if (tree_type != DUAL_TREE_CHROMA && cu->intra_mip_flag && min_lfnst < 16)
        return 0;

    if (min_lfnst >= 4) {
        if ((cu->isp_split_type != ISP_NO_SPLIT || !lc->parse.lfnst_dc_only) && lc->parse.lfnst_zero_out_sig_coeff_flag)
            lfnst_idx = ff_vvc_lfnst_idx(lc, tree_type != SINGLE_TREE);
    }

    if (lfnst_idx) {
        cu->apply_lfnst_flag[LUMA] = tree_type != DUAL_TREE_CHROMA;
        cu->apply_lfnst_flag[CB] = cu->apply_lfnst_flag[CR] = tree_type == DUAL_TREE_CHROMA;
    }

    return lfnst_idx;
}

static MtsIdx mts_idx_decode(VVCLocalContext *lc)
{
    const CodingUnit *cu    = lc->cu;
    const VVCSPS     *sps   = lc->fc->ps.sps;
    const int cb_width      = cu->cb_width;
    const int cb_height     = cu->cb_height;
    const uint8_t transform_skip_flag = cu->tus[0].tbs[0].ts; //fix me
    int mts_idx = MTS_DCT2_DCT2;
    if (cu->tree_type != DUAL_TREE_CHROMA && !cu->lfnst_idx &&
        !transform_skip_flag && FFMAX(cb_width, cb_height) <= 32 &&
        cu->isp_split_type == ISP_NO_SPLIT && !cu->sbt_flag &&
        lc->parse.mts_zero_out_sig_coeff_flag && !lc->parse.mts_dc_only) {
        if ((cu->pred_mode == MODE_INTER && sps->explicit_mts_inter_enabled_flag) ||
            (cu->pred_mode == MODE_INTRA && sps->explicit_mts_intra_enabled_flag)) {
            mts_idx = ff_vvc_mts_idx(lc);
        }
    }

    return mts_idx;
}

static int is_cclm(enum IntraPredMode mode)
{
    return mode == INTRA_LT_CCLM || mode == INTRA_L_CCLM || mode == INTRA_T_CCLM;
}

//8.4.5.2.7 Wide angle intra prediction mode mapping proces
int ff_vvc_wide_angle_mode_mapping(const CodingUnit *cu,
    const int tb_width, const int tb_height, const int c_idx, int pred_mode_intra)
{
    int nw, nh, wh_ratio, min, max;

    if (cu->isp_split_type == ISP_NO_SPLIT || c_idx) {
        nw = tb_width;
        nh = tb_height;
    } else {
        nw = cu->cb_width;
        nh = cu->cb_height;
    }
    wh_ratio    = FFABS(ff_log2(nw) - ff_log2(nh));
    max         = (wh_ratio > 1) ? (8  + 2 * wh_ratio) : 8;
    min         = (wh_ratio > 1) ? (60 - 2 * wh_ratio) : 60;

    if (nw > nh && pred_mode_intra >=2 && pred_mode_intra < max)
        pred_mode_intra += 65;
    else if (nh > nw && pred_mode_intra <= 66 && pred_mode_intra > min)
        pred_mode_intra -= 67;
    return pred_mode_intra;
}

static enum IntraPredMode derive_center_luma_intra_pred_mode(const VVCFrameContext *fc, const VVCSPS *sps, const VVCPPS *pps, const CodingUnit *cu)
{
    const int x_center            = (cu->x0 + cu->cb_width / 2) >> sps->min_cb_log2_size_y;
    const int y_center            = (cu->y0 + cu->cb_height / 2) >> sps->min_cb_log2_size_y;
    const int min_cb_width        = pps->min_cb_width;
    const int intra_mip_flag      = SAMPLE_CTB(fc->tab.imf, x_center, y_center);
    const int cu_pred_mode        = SAMPLE_CTB(fc->tab.cpm[0], x_center, y_center);
    const int intra_pred_mode_y   = SAMPLE_CTB(fc->tab.ipm, x_center, y_center);

    if (intra_mip_flag)
        return INTRA_PLANAR;
    if (cu_pred_mode == MODE_IBC || cu_pred_mode == MODE_PLT)
        return INTRA_DC;
    return intra_pred_mode_y;
}

static void derive_chroma_intra_pred_mode(VVCLocalContext *lc,
    const int cclm_mode_flag, const int cclm_mode_idx, const int intra_chroma_pred_mode)
{
    const VVCFrameContext *fc   = lc->fc;
    CodingUnit *cu              = lc->cu;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const int x_cb              = cu->x0 >> sps->min_cb_log2_size_y;
    const int y_cb              = cu->y0 >> sps->min_cb_log2_size_y;
    const int min_cb_width      = pps->min_cb_width;
    const int intra_mip_flag    = SAMPLE_CTB(fc->tab.imf, x_cb, y_cb);
    enum IntraPredMode luma_intra_pred_mode = SAMPLE_CTB(fc->tab.ipm, x_cb, y_cb);

    if (cu->tree_type == SINGLE_TREE && sps->chroma_format_idc == CHROMA_FORMAT_444 &&
        intra_chroma_pred_mode == 4 && intra_mip_flag) {
        cu->mip_chroma_direct_flag = 1;
        cu->intra_pred_mode_c = luma_intra_pred_mode;
        return;
    }
    luma_intra_pred_mode = derive_center_luma_intra_pred_mode(fc, sps, pps, cu);

    if (cu->act_enabled_flag) {
        cu->intra_pred_mode_c = luma_intra_pred_mode;
        return;
    }
    if (cclm_mode_flag) {
        cu->intra_pred_mode_c = INTRA_LT_CCLM + cclm_mode_idx;
    } else if (intra_chroma_pred_mode == 4){
        cu->intra_pred_mode_c = luma_intra_pred_mode;
    } else {
        const static IntraPredMode pred_mode_c[][4 + 1] = {
            {INTRA_VDIAG, INTRA_PLANAR, INTRA_PLANAR, INTRA_PLANAR, INTRA_PLANAR},
            {INTRA_VERT,  INTRA_VDIAG,  INTRA_VERT,   INTRA_VERT,   INTRA_VERT},
            {INTRA_HORZ,  INTRA_HORZ,   INTRA_VDIAG,  INTRA_HORZ,   INTRA_HORZ},
            {INTRA_DC,    INTRA_DC,     INTRA_DC,     INTRA_VDIAG,  INTRA_DC},
        };
        const int modes[4] = {INTRA_PLANAR, INTRA_VERT, INTRA_HORZ, INTRA_DC};
        int idx;

        for (idx = 0; idx < FF_ARRAY_ELEMS(modes); idx++) {
            if (modes[idx] == luma_intra_pred_mode)
                break;
        }
        cu->intra_pred_mode_c = pred_mode_c[intra_chroma_pred_mode][idx];
    }
    if (sps->chroma_format_idc == CHROMA_FORMAT_422 && cu->intra_pred_mode_c <= INTRA_VDIAG) {
        const static int mode_map_422[INTRA_VDIAG + 1] = {
             0,  1, 61, 62, 63, 64, 65, 66,  2,  3,  5,  6,  8, 10, 12, 13,
            14, 16, 18, 20, 22, 23, 24, 26, 28, 30, 31, 33, 34, 35, 36, 37,
            38, 39, 40, 41, 41, 42, 43, 43, 44, 44, 45, 45, 46, 47, 48, 48,
            49, 49, 50, 51, 51, 52, 52, 53, 54, 55, 55, 56, 56, 57, 57, 58,
            59, 59, 60,
        };
        cu->intra_pred_mode_c = mode_map_422[cu->intra_pred_mode_c];
    }
}

static int derive_ilfnst_pred_mode_intra(const VVCLocalContext *lc, const TransformBlock *tb)
{
    const VVCFrameContext *fc = lc->fc;
    const VVCSPS *sps = fc->ps.sps;
    const CodingUnit *cu          = lc->cu;
    const int x_tb                = tb->x0 >> fc->ps.sps->min_cb_log2_size_y;
    const int y_tb                = tb->y0 >> fc->ps.sps->min_cb_log2_size_y;
    const int x_c                 = (tb->x0 + (tb->tb_width << sps->hshift[1] >> 1) ) >> fc->ps.sps->min_cb_log2_size_y;
    const int y_c                 = (tb->y0 + (tb->tb_height << sps->vshift[1] >> 1)) >> fc->ps.sps->min_cb_log2_size_y;
    const int min_cb_width        = fc->ps.pps->min_cb_width;
    const int intra_mip_flag      = SAMPLE_CTB(fc->tab.imf, x_tb, y_tb);
    int pred_mode_intra = tb->c_idx == 0 ? cu->intra_pred_mode_y : cu->intra_pred_mode_c;
    if (intra_mip_flag && !tb->c_idx) {
        pred_mode_intra = INTRA_PLANAR;
    } else if (is_cclm(pred_mode_intra)) {
        int intra_mip_flag_c = SAMPLE_CTB(fc->tab.imf, x_c, y_c);
        int cu_pred_mode = SAMPLE_CTB(fc->tab.cpm[0], x_c, y_c);
        if (intra_mip_flag_c) {
            pred_mode_intra = INTRA_PLANAR;
        } else if (cu_pred_mode == MODE_IBC || cu_pred_mode == MODE_PLT) {
            pred_mode_intra = INTRA_DC;
        } else {
            pred_mode_intra = SAMPLE_CTB(fc->tab.ipm, x_c, y_c);
        }
    }
    pred_mode_intra = ff_vvc_wide_angle_mode_mapping(cu, tb->tb_width, tb->tb_height, tb->c_idx, pred_mode_intra);

    return pred_mode_intra;
}

//8.7.4 Transformation process for scaled transform coefficients
static void ilfnst_transform(const VVCLocalContext *lc, TransformBlock *tb)
{
    const CodingUnit *cu        = lc->cu;
    const int w                 = tb->tb_width;
    const int h                 = tb->tb_height;
    const int n_lfnst_out_size  = (w >= 8 && h >= 8) ? 48 : 16;                         ///< nLfnstOutSize
    const int log2_lfnst_size   = (w >= 8 && h >= 8) ? 3 : 2;                           ///< log2LfnstSize
    const int n_lfnst_size      = 1 << log2_lfnst_size;                                 ///< nLfnstSize
    const int non_zero_size     = ((w == 8 && h == 8) || (w == 4 && h == 4)) ? 8 : 16;  ///< nonZeroSize
    const int pred_mode_intra   = derive_ilfnst_pred_mode_intra(lc, tb);
    const int transpose         = pred_mode_intra > 34;
    int u[16], v[48];

    for (int x = 0; x < non_zero_size; x++) {
        int xc = ff_vvc_diag_scan_x[2][2][x];
        int yc = ff_vvc_diag_scan_y[2][2][x];
        u[x] = tb->coeffs[w * yc + xc];
    }
    ff_vvc_inv_lfnst_1d(v, u, non_zero_size, n_lfnst_out_size, pred_mode_intra, cu->lfnst_idx);
    if (transpose) {
        int *dst = tb->coeffs;
        const int *src = v;
        if (n_lfnst_size == 4) {
            for (int y = 0; y < 4; y++) {
                dst[0] = src[0];
                dst[1] = src[4];
                dst[2] = src[8];
                dst[3] = src[12];
                src++;
                dst += w;
            }
        } else {
            for (int y = 0; y < 8; y++) {
                dst[0] = src[0];
                dst[1] = src[8];
                dst[2] = src[16];
                dst[3] = src[24];
                if (y < 4) {
                    dst[4] = src[32];
                    dst[5] = src[36];
                    dst[6] = src[40];
                    dst[7] = src[44];
                }
                src++;
                dst += w;
            }
        }

    } else {
        int *dst = tb->coeffs;
        const int *src = v;
        for (int y = 0; y < n_lfnst_size; y++) {
            int size = (y < 4) ? n_lfnst_size : 4;
            memcpy(dst, src, size * sizeof(int));
            src += size;
            dst += w;
        }
    }
    tb->max_scan_x = n_lfnst_size - 1;
    tb->max_scan_y = n_lfnst_size - 1;
}

//part of 8.7.4 Transformation process for scaled transform coefficients
static void derive_transform_type(const VVCFrameContext *fc, const VVCLocalContext *lc, const TransformBlock *tb, enum TxType *trh, enum TxType *trv)
{
    const CodingUnit *cu = lc->cu;
    static const enum TxType mts_to_trh[] = {DCT2, DST7, DCT8, DST7, DCT8};
    static const enum TxType mts_to_trv[] = {DCT2, DST7, DST7, DCT8, DCT8};
    const VVCSPS *sps       = fc->ps.sps;
    int implicit_mts_enabled = 0;
    if (tb->c_idx || (cu->isp_split_type != ISP_NO_SPLIT && cu->lfnst_idx)) {
        *trh = *trv = DCT2;
        return;
    }

    if (sps->mts_enabled_flag) {
        if (cu->isp_split_type != ISP_NO_SPLIT ||
            (cu->sbt_flag && FFMAX(tb->tb_width, tb->tb_height) <= 32) ||
            (!sps->explicit_mts_intra_enabled_flag && cu->pred_mode == MODE_INTRA &&
            !cu->lfnst_idx && !cu->intra_mip_flag)) {
            implicit_mts_enabled = 1;
        }
    }
    if (implicit_mts_enabled) {
        const int w = tb->tb_width;
        const int h = tb->tb_height;
        if (cu->sbt_flag) {
            *trh = (cu->sbt_horizontal_flag  || cu->sbt_pos_flag) ? DST7 : DCT8;
            *trv = (!cu->sbt_horizontal_flag || cu->sbt_pos_flag) ? DST7 : DCT8;
        } else {
            *trh = (w >= 4 && w <= 16) ? DST7 : DCT2;
            *trv = (h >= 4 && h <= 16) ? DST7 : DCT2;
        }
        return;
    }
    *trh = mts_to_trh[cu->mts_idx];
    *trv = mts_to_trv[cu->mts_idx];
}

static void add_residual_for_joint_coding_chroma(VVCLocalContext *lc,
    const TransformUnit *tu, TransformBlock *tb, const int chroma_scale)
{
    const VVCFrameContext *fc  = lc->fc;
    const CodingUnit *cu = lc->cu;
    const int c_sign = 1 - 2 * fc->ps.ph->joint_cbcr_sign_flag;
    const int shift  = tu->coded_flag[1] ^ tu->coded_flag[2];
    const int c_idx  = 1 + tu->coded_flag[1];
    const ptrdiff_t stride = fc->frame->linesize[c_idx];
    const int hs = fc->ps.sps->hshift[c_idx];
    const int vs = fc->ps.sps->vshift[c_idx];
    uint8_t *dst = &fc->frame->data[c_idx][(tb->y0 >> vs) * stride +
                                          ((tb->x0 >> hs) << fc->ps.sps->pixel_shift)];
    if (chroma_scale) {
        fc->vvcdsp.itx.pred_residual_joint(tb->coeffs, tb->tb_width, tb->tb_height, c_sign, shift);
        fc->vvcdsp.intra.lmcs_scale_chroma(lc, tb->coeffs, tb->coeffs, tb->tb_width, tb->tb_height, cu->x0, cu->y0);
        fc->vvcdsp.itx.add_residual(dst, tb->coeffs, tb->tb_width, tb->tb_height, stride);
    } else {
        fc->vvcdsp.itx.add_residual_joint(dst, tb->coeffs, tb->tb_width, tb->tb_height, stride, c_sign, shift);
    }
}

static int add_reconstructed_area(VVCLocalContext *lc, const int ch_type, const int x0, const int y0, const int w, const int h)
{
    const VVCSPS *sps       = lc->fc->ps.sps;
    const int hs = sps->hshift[ch_type];
    const int vs = sps->vshift[ch_type];
    ReconstructedArea *a;

    if (lc->num_ras[ch_type] >= FF_ARRAY_ELEMS(lc->ras[ch_type]))
        return AVERROR_INVALIDDATA;

    a = &lc->ras[ch_type][lc->num_ras[ch_type]];
    a->x = x0 >> hs;
    a->y = y0 >> vs;
    a->w = w >> hs;
    a->h = h >> vs;
    lc->num_ras[ch_type]++;

    return 0;
}

static void add_tu_area(const TransformUnit *tu, int *x0, int *y0, int *w, int *h)
{
    *x0 = tu->x0;
    *y0 = tu->y0;
    *w = tu->width;
    *h = tu->height;
}

#define MIN_ISP_PRED_WIDTH 4
static int get_luma_predict_unit(const CodingUnit *cu, const TransformUnit *tu, const int idx, int *x0, int *y0, int *w, int *h)
{
    int has_luma = 1;
    add_tu_area(tu, x0, y0, w, h);
    if (cu->isp_split_type == ISP_VER_SPLIT && tu->width < MIN_ISP_PRED_WIDTH) {
        *w = MIN_ISP_PRED_WIDTH;
        has_luma = !(idx % (MIN_ISP_PRED_WIDTH / tu->width));
    }
    return has_luma;
}

static int get_chroma_predict_unit(const CodingUnit *cu, const TransformUnit *tu, const int idx, int *x0, int *y0, int *w, int *h)
{
    if (cu->isp_split_type == ISP_NO_SPLIT) {
        add_tu_area(tu, x0, y0, w, h);
        return 1;
    }
    if (idx == cu->num_intra_subpartitions - 1) {
        *x0 = cu->x0;
        *y0 = cu->y0;
        *w = cu->cb_width;
        *h = cu->cb_height;
        return 1;
    }
    return 0;
}

//8.4.5.1 General decoding process for intra blocks
static void predict_intra(VVCLocalContext *lc, const TransformUnit *tu, const int idx, const int target_ch_type)
{
    const VVCFrameContext *fc         = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const VVCTreeType tree_type = cu->tree_type;
    int x0, y0, w, h;
    if (cu->pred_mode != MODE_INTRA) {
        add_reconstructed_area(lc, target_ch_type, tu->x0, tu->y0, tu->width, tu->height);
        return;
    }
    if (!target_ch_type && tree_type != DUAL_TREE_CHROMA) {
        if (get_luma_predict_unit(cu, tu, idx, &x0, &y0, &w, &h)) {
            ff_vvc_set_neighbour_available(lc, x0, y0, w, h);
            fc->vvcdsp.intra.intra_pred(lc, x0, y0, w, h, 0);
            add_reconstructed_area(lc, 0, x0, y0, w, h);
        }
    }
    if (target_ch_type && tree_type != DUAL_TREE_LUMA) {
        if (get_chroma_predict_unit(cu, tu, idx, &x0, &y0, &w, &h)){
            ff_vvc_set_neighbour_available(lc, x0, y0, w, h);
            if (is_cclm(cu->intra_pred_mode_c)) {
                fc->vvcdsp.intra.intra_cclm_pred(lc, x0, y0, w, h);
            } else {
                fc->vvcdsp.intra.intra_pred(lc, x0, y0, w, h, 1);
                fc->vvcdsp.intra.intra_pred(lc, x0, y0, w, h, 2);
            }
            add_reconstructed_area(lc, 1, x0, y0, w, h);
        }
    }
}

static void scale_clip(int *coeff, const int nzw, const int w, const int h, const int shift)
{
    const int add = 1 << (shift - 1);
    for (int y = 0; y < h; y++) {
        int *p = coeff + y * w;
        for (int x = 0; x < nzw; x++) {
            *p = av_clip_int16((*p + add) >> shift);
            p++;
        }
        memset(p, 0, sizeof(*p) * (w - nzw));
    }
}

static void scale(int *out, const int *in, const int w, const int h, const int shift)
{
    const int add = 1 << (shift - 1);
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int *o = out + y * w + x;
            const int *i = in + y * w + x;
            *o = (*i + add) >> shift;
        }
    }
}

// part of 8.7.3 Scaling process for transform coefficients
static void derive_qp(const VVCLocalContext *lc, const TransformUnit *tu, TransformBlock *tb)
{
    const VVCSPS *sps           = lc->fc->ps.sps;
    const VVCSH *sh             = &lc->sc->sh;
    const CodingUnit *cu        = lc->cu;
    int qp, qp_act_offset;

    if (tb->c_idx == 0) {
        //fix me
        qp = cu->qp[LUMA] + sps->qp_bd_offset;
        qp_act_offset = cu->act_enabled_flag ? -5 : 0;
    } else {
        const int is_jcbcr = tu->joint_cbcr_residual_flag && tu->coded_flag[CB] && tu->coded_flag[CR];
        const int idx = is_jcbcr ? JCBCR : tb->c_idx;
        qp = cu->qp[idx];
        qp_act_offset = cu->act_enabled_flag ? 1 : 0;
    }
    if (tb->ts) {
        const int qp_prime_ts_min = 4 + 6 * sps->min_qp_prime_ts;

        tb->qp = av_clip(qp + qp_act_offset, qp_prime_ts_min, 63 + sps->qp_bd_offset);
        tb->rect_non_ts_flag = 0;
        tb->bd_shift = 10;
    } else {
        const int log_sum = tb->log2_tb_width + tb->log2_tb_height;
        const int rect_non_ts_flag = log_sum & 1;

        tb->qp = av_clip(qp + qp_act_offset, 0, 63 + sps->qp_bd_offset);
        tb->rect_non_ts_flag = rect_non_ts_flag;
        tb->bd_shift = sps->bit_depth + rect_non_ts_flag + (log_sum / 2) - 5 + sh->dep_quant_used_flag;
    }
    tb->bd_offset = (1 << tb->bd_shift) >> 1;
}

//8.7.3 Scaling process for transform coefficients
static av_always_inline int derive_scale(const TransformBlock *tb, const int sh_dep_quant_used_flag)
{
    static const uint8_t rem6[63 + 2 * 6 + 1] = {
        0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2,
        3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5,
        0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3,
        4, 5, 0, 1, 2, 3, 4, 5, 0, 1, 2, 3
    };

    static const uint8_t div6[63 + 2 * 6 + 1] = {
        0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2, 2, 2, 2, 2, 2, 3,  3,  3,
        3, 3, 3, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6,  6,  6,
        7, 7, 7, 7, 7, 7, 8, 8, 8, 8, 8, 8, 9, 9, 9, 9, 9, 9, 10, 10, 10, 10,
        10, 10, 11, 11, 11, 11, 11, 11, 12, 12, 12, 12
    };

    const static int level_scale[2][6] = {
        { 40, 45, 51, 57, 64, 72 },
        { 57, 64, 72, 80, 90, 102 }
    };
    const int addin = sh_dep_quant_used_flag && !tb->ts;
    const int qp    = tb->qp + addin;

    return level_scale[tb->rect_non_ts_flag][rem6[qp]] << div6[qp];
}

//8.7.3 Scaling process for transform coefficients
static const uint8_t* derive_scale_m(const VVCLocalContext *lc, const TransformBlock *tb, uint8_t *scale_m)
{
    //Table 38 – Specification of the scaling matrix identifier variable id according to predMode, cIdx, nTbW, and nTbH
    const int ids[2][3][6] = {
        {
            {  0,  2,  8, 14, 20, 26 },
            {  0,  3,  9, 15, 21, 21 },
            {  0,  4, 10, 16, 22, 22 }
        },
        {
            {  0,  5, 11, 17, 23, 27 },
            {  0,  6, 12, 18, 24, 24 },
            {  1,  7, 13, 19, 25, 25 },
        }
    };
    const VVCFrameParamSets *ps = &lc->fc->ps;
    const VVCSPS *sps           = ps->sps;
    const VVCSH *sh             = &lc->sc->sh;
    const CodingUnit *cu        = lc->cu;
    const AVBufferRef *ref;
    const VVCScalingList *sl;
    const int id = ids[cu->pred_mode != MODE_INTRA][tb->c_idx][FFMAX(tb->log2_tb_height, tb->log2_tb_width) - 1];
    const int log2_matrix_size = (id < 2) ? 1 : (id < 8) ? 2 : 3;
    uint8_t *p = scale_m;

    av_assert0(!sps->scaling_matrix_for_alternative_colour_space_disabled_flag);

    if (!sh->explicit_scaling_list_used_flag || tb->ts ||
        sps->scaling_matrix_for_lfnst_disabled_flag && cu->apply_lfnst_flag[tb->c_idx])
        return ff_vvc_default_scale_m;

    ref = ps->scaling_list[ps->ph->scaling_list_aps_id];
    if (!ref || !ref->data) {
        av_log(lc->fc->avctx, AV_LOG_WARNING, "bug: no scaling list aps, id = %d", ps->ph->scaling_list_aps_id);
        return ff_vvc_default_scale_m;
    }

    sl = (const VVCScalingList *)ref->data;
    for (int y = tb->min_scan_y; y <= tb->max_scan_y; y++) {
        const int off = y << log2_matrix_size >> tb->log2_tb_height << log2_matrix_size;
        const uint8_t *m = &sl->scaling_matrix_rec[id][off];

        for (int x = tb->min_scan_x; x <= tb->max_scan_x; x++)
            *p++ = m[x << log2_matrix_size >> tb->log2_tb_width];
    }
    if (id >= SL_START_16x16 && !tb->min_scan_x && !tb->min_scan_y)
        *scale_m = sl->scaling_matrix_dc_rec[id - SL_START_16x16];

    return scale_m;
}

//8.7.3 Scaling process for transform coefficients
static av_always_inline int scale_coeff(const TransformBlock *tb, int coeff, const int scale, const int scale_m)
{
    coeff = (coeff * scale * scale_m + tb->bd_offset) >> tb->bd_shift;
    coeff = av_clip(coeff, -(1<<15), (1<<15) - 1);
    return coeff;
}

static void dequant(const VVCLocalContext *lc, const TransformUnit *tu, TransformBlock *tb)
{
    uint8_t tmp[MAX_TB_SIZE * MAX_TB_SIZE];
    const VVCSH *sh         = &lc->sc->sh;
    const uint8_t *scale_m  = derive_scale_m(lc, tb, tmp);
    int scale;

    derive_qp(lc, tu, tb);
    scale = derive_scale(tb, sh->dep_quant_used_flag);

    for (int y = tb->min_scan_y; y <= tb->max_scan_y; y++) {
        for (int x = tb->min_scan_x; x <= tb->max_scan_x; x++) {
            int *coeff = tb->coeffs + y * tb->tb_width + x;

            if (*coeff)
                *coeff = scale_coeff(tb, *coeff, scale, *scale_m);
            scale_m++;
        }
    }
}

static void itx_2d(const VVCFrameContext *fc, TransformBlock *tb, const enum TxType trh, const enum TxType trv, int *temp)
{
    const VVCSPS *sps   = fc->ps.sps;
    const int w         = tb->tb_width;
    const int h         = tb->tb_height;
    const int nzw       = tb->max_scan_x + 1;

    for (int x = 0; x < nzw; x++)
        fc->vvcdsp.itx.itx[trv][tb->log2_tb_height - 1](temp + x, w, tb->coeffs + x, w);
    scale_clip(temp, nzw, w, h, 7);

    for (int y = 0; y < h; y++)
        fc->vvcdsp.itx.itx[trh][tb->log2_tb_width - 1](tb->coeffs + y * w, 1, temp + y * w, 1);
    scale(tb->coeffs, tb->coeffs, w, h, 20 - sps->bit_depth);
}

static void itx_1d(const VVCFrameContext *fc, TransformBlock *tb, const enum TxType trh, const enum TxType trv, int  *temp)
{
    const VVCSPS *sps   = fc->ps.sps;
    const int w         = tb->tb_width;
    const int h         = tb->tb_height;

    if (w > 1)
        fc->vvcdsp.itx.itx[trh][tb->log2_tb_width - 1](temp, 1, tb->coeffs, 1);
    else
        fc->vvcdsp.itx.itx[trv][tb->log2_tb_height - 1](temp, 1, tb->coeffs, 1);
    scale(tb->coeffs, temp, w, h, 21 - sps->bit_depth);
}

static void transform_bdpcm(TransformBlock *tb, const VVCLocalContext *lc, const CodingUnit *cu)
{
    const IntraPredMode mode = tb->c_idx ? cu->intra_pred_mode_c : cu->intra_pred_mode_y;
    const int vertical       = mode == INTRA_VERT;
    lc->fc->vvcdsp.itx.transform_bdpcm(tb->coeffs, tb->tb_width, tb->tb_height, vertical, 15);
    if (vertical)
        tb->max_scan_y = tb->tb_height - 1;
    else
        tb->max_scan_x = tb->tb_width - 1;
}

static void itransform(VVCLocalContext *lc, TransformUnit *tu, const int tu_idx, const int target_ch_type)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCSH *sh             = &lc->sc->sh;
    const CodingUnit *cu        = lc->cu;
    const int ps                = fc->ps.sps->pixel_shift;
    DECLARE_ALIGNED(32, int, temp)[MAX_TB_SIZE * MAX_TB_SIZE];

    for (int i = 0; i < tu->nb_tbs; i++) {
        TransformBlock *tb  = &tu->tbs[i];
        const int c_idx     = tb->c_idx;
        const int ch_type   = c_idx > 0;

        if (ch_type == target_ch_type && tb->has_coeffs) {
            const int w             = tb->tb_width;
            const int h             = tb->tb_height;
            const int chroma_scale  = ch_type && sh->lmcs_used_flag && fc->ps.ph->chroma_residual_scale_flag && (w * h > 4);
            const ptrdiff_t stride  = fc->frame->linesize[c_idx];
            const int hs            = sps->hshift[c_idx];
            const int vs            = sps->vshift[c_idx];
            uint8_t *dst            = &fc->frame->data[c_idx][(tb->y0 >> vs) * stride + ((tb->x0 >> hs) << ps)];

            if (cu->bdpcm_flag[tb->c_idx])
                transform_bdpcm(tb, lc, cu);
            dequant(lc, tu, tb);
            if (!tb->ts) {
                enum TxType trh, trv;

                if (cu->apply_lfnst_flag[c_idx])
                    ilfnst_transform(lc, tb);
                derive_transform_type(fc, lc, tb, &trh, &trv);
                if (w > 1 && h > 1)
                    itx_2d(fc, tb, trh, trv, temp);
                else
                    itx_1d(fc, tb, trh, trv, temp);
            }

            if (chroma_scale)
                fc->vvcdsp.intra.lmcs_scale_chroma(lc, temp, tb->coeffs, w, h, cu->x0, cu->y0);
            fc->vvcdsp.itx.add_residual(dst, chroma_scale ? temp : tb->coeffs, w, h, stride);

            if (tu->joint_cbcr_residual_flag && tb->c_idx)
                add_residual_for_joint_coding_chroma(lc, tu, tb, chroma_scale);
        }
    }
}

static int reconstruct(VVCLocalContext *lc)
{
    VVCFrameContext *fc = lc->fc;
    CodingUnit *cu      = lc->cu;
    const int start     = cu->tree_type == DUAL_TREE_CHROMA;
    const int end       = fc->ps.sps->chroma_format_idc && (cu->tree_type != DUAL_TREE_LUMA);

    for (int ch_type = start; ch_type <= end; ch_type++) {
        for (int i = 0; i < cu->num_tus; i++) {
            TransformUnit *tu = &cu->tus[i];

            predict_intra(lc, tu, i, ch_type);
            itransform(lc, tu, i, ch_type);
        }
    }
    return 0;
}

static void intra_luma_pred_modes(VVCLocalContext *lc)
{
    VVCFrameContext *fc             = lc->fc;
    const VVCSPS *sps               = fc->ps.sps;
    const VVCPPS *pps               = fc->ps.pps;
    CodingUnit *cu                  = lc->cu;
    const int log2_min_cb_size      = sps->min_cb_log2_size_y;
    const int x0                    = cu->x0;
    const int y0                    = cu->y0;
    const int x_cb                  = x0 >> log2_min_cb_size;
    const int y_cb                  = y0 >> log2_min_cb_size;
    const int cb_width              = cu->cb_width;
    const int cb_height             = cu->cb_height;

    cu->intra_luma_ref_idx  = 0;
    if (sps->bdpcm_enabled_flag && cb_width <= sps->max_ts_size && cb_height <= sps->max_ts_size)
        cu->bdpcm_flag[LUMA] = ff_vvc_intra_bdpcm_luma_flag(lc);
    if (cu->bdpcm_flag[LUMA]) {
        cu->intra_pred_mode_y = ff_vvc_intra_bdpcm_luma_dir_flag(lc) ? INTRA_VERT : INTRA_HORZ;
    } else {
        if (sps->mip_enabled_flag)
            cu->intra_mip_flag = ff_vvc_intra_mip_flag(lc, fc->tab.imf);
        if (cu->intra_mip_flag) {
            int intra_mip_transposed_flag = ff_vvc_intra_mip_transposed_flag(lc);
            int intra_mip_mode = ff_vvc_intra_mip_mode(lc);
            int x = y_cb * pps->min_cb_width + x_cb;
            for (int y = 0; y < (cb_height>>log2_min_cb_size); y++) {
                int width = cb_width>>log2_min_cb_size;
                memset(&fc->tab.imf[x],  cu->intra_mip_flag, width);
                fc->tab.imtf[x] = intra_mip_transposed_flag;
                fc->tab.imm[x]  = intra_mip_mode;
                x += pps->min_cb_width;
            }
            cu->intra_pred_mode_y = intra_mip_mode;
        } else {
            const int min_tb_size_y = 1 << 2;
            int intra_subpartitions_mode_flag = 0;
            if (sps->mrl_enabled_flag && ((y0 % sps->ctb_size_y) > 0))
                cu->intra_luma_ref_idx = ff_vvc_intra_luma_ref_idx(lc);
            if (sps->isp_enabled_flag && !cu->intra_luma_ref_idx &&
                (cb_width <= sps->max_tb_size_y && cb_height <= sps->max_tb_size_y) &&
                (cb_width * cb_height > min_tb_size_y * min_tb_size_y) &&
                !cu->act_enabled_flag)
                intra_subpartitions_mode_flag = ff_vvc_intra_subpartitions_mode_flag(lc);
            if (!(x0 & 63) && !(y0 & 63))
                TAB_ISPMF(fc, x0, y0) = intra_subpartitions_mode_flag;
            cu->isp_split_type = ff_vvc_isp_split_type(lc, intra_subpartitions_mode_flag);
            cu->num_intra_subpartitions = get_num_intra_subpartitions(cu->isp_split_type, cb_width, cb_height);
            cu->intra_pred_mode_y = luma_intra_pred_mode(lc, intra_subpartitions_mode_flag);
        }
    }
    ff_vvc_set_cb_tab(lc, fc->tab.ipm, cu->intra_pred_mode_y);
}

static void intra_chroma_pred_modes(VVCLocalContext *lc)
{
    const VVCSPS *sps   = lc->fc->ps.sps;
    CodingUnit *cu      = lc->cu;
    const int hs        = sps->hshift[CHROMA];
    const int vs        = sps->vshift[CHROMA];

    cu->mip_chroma_direct_flag = 0;
    if (sps->bdpcm_enabled_flag &&
        (cu->cb_width  >> hs) <= sps->max_ts_size &&
        (cu->cb_height >> vs) <= sps->max_ts_size) {
        cu->bdpcm_flag[CB] = cu->bdpcm_flag[CR] = ff_vvc_intra_bdpcm_chroma_flag(lc);
    }
    if (cu->bdpcm_flag[CHROMA]) {
        cu->intra_pred_mode_c = ff_vvc_intra_bdpcm_chroma_dir_flag(lc) ? INTRA_VERT : INTRA_HORZ;
    } else {
        const int cclm_enabled = get_cclm_enabled(lc, cu->x0, cu->y0);
        int cclm_mode_flag = 0;
        int cclm_mode_idx = 0;
        int intra_chroma_pred_mode = 0;

        if (cclm_enabled)
            cclm_mode_flag = ff_vvc_cclm_mode_flag(lc);

        if (cclm_mode_flag)
            cclm_mode_idx = ff_vvc_cclm_mode_idx(lc);
        else
            intra_chroma_pred_mode = ff_vvc_intra_chroma_pred_mode(lc);
        derive_chroma_intra_pred_mode(lc, cclm_mode_flag, cclm_mode_idx, intra_chroma_pred_mode);
    }
}

static PredMode pred_mode_decode(VVCLocalContext *lc,
                                 const VVCTreeType tree_type,
                                 const VVCModeType mode_type)
{
    const VVCFrameContext *fc   = lc->fc;
    CodingUnit *cu              = lc->cu;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCSH *sh             = &lc->sc->sh;
    const int ch_type = tree_type == DUAL_TREE_CHROMA ? 1 : 0;
    const int is_4x4 = cu->cb_width == 4 && cu->cb_height == 4;
    int pred_mode_flag;
    int pred_mode_ibc_flag;
    PredMode pred_mode;

    cu->skip_flag = 0;
    if (!IS_I(sh) || sps->ibc_enabled_flag) {
        const int is_128 = cu->cb_width == 128 || cu->cb_height == 128;
        if (tree_type != DUAL_TREE_CHROMA &&
            ((!is_4x4 && mode_type != MODE_TYPE_INTRA) ||
            (sps->ibc_enabled_flag && !is_128))) {
            cu->skip_flag = ff_vvc_cu_skip_flag(lc, fc->tab.skip);
        }

        if (is_4x4 || mode_type == MODE_TYPE_INTRA || IS_I(sh)) {
            pred_mode_flag = 1;
        } else if (mode_type == MODE_TYPE_INTER || cu->skip_flag) {
            pred_mode_flag = 0;
        } else  {
            pred_mode_flag = ff_vvc_pred_mode_flag(lc, ch_type);
        }
        pred_mode = pred_mode_flag ? MODE_INTRA : MODE_INTER;

        if (((IS_I(sh) && !cu->skip_flag) ||
            (!IS_I(sh) && (pred_mode != MODE_INTRA ||
            ((is_4x4 || mode_type == MODE_TYPE_INTRA) && !cu->skip_flag)))) &&
            !is_128 && mode_type != MODE_TYPE_INTER && sps->ibc_enabled_flag &&
            tree_type != DUAL_TREE_CHROMA) {
            pred_mode_ibc_flag = ff_vvc_pred_mode_ibc_flag(lc, ch_type);
        } else if (cu->skip_flag && (is_4x4 || mode_type == MODE_TYPE_INTRA)) {
            pred_mode_ibc_flag = 1;
        } else if (is_128 || mode_type == MODE_TYPE_INTER || tree_type == DUAL_TREE_CHROMA) {
            pred_mode_ibc_flag = 0;
        } else {
            pred_mode_ibc_flag = (IS_I(sh)) ? sps->ibc_enabled_flag : 0;
        }
        if (pred_mode_ibc_flag)
            pred_mode = MODE_IBC;
    } else {
        pred_mode_flag = is_4x4 || mode_type == MODE_TYPE_INTRA ||
            mode_type != MODE_TYPE_INTER || IS_I(sh);
        pred_mode = pred_mode_flag ? MODE_INTRA : MODE_INTER;
    }
    return pred_mode;
}

void ff_vvc_set_cb_tab(const VVCLocalContext *lc, uint8_t *tab, const uint8_t v)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const CodingUnit *cu        = lc->cu;
    const int log2_min_cb_size  = fc->ps.sps->min_cb_log2_size_y;
    const int x_cb              = cu->x0 >> log2_min_cb_size;
    const int y_cb              = cu->y0 >> log2_min_cb_size;
    const int cb_width          = cu->cb_width;
    const int cb_height         = cu->cb_height;
    int x                       = y_cb * pps->min_cb_width + x_cb;

    for (int y = 0; y < (cb_height >> log2_min_cb_size); y++) {
        const int width = cb_width >> log2_min_cb_size;

        memset(&tab[x], v, width);
        x += pps->min_cb_width;
    }
}

static void sbt_info(VVCLocalContext *lc, const VVCSPS *sps)
{
    CodingUnit *cu      = lc->cu;
    const int cb_width  = cu->cb_width;
    const int cb_height = cu->cb_height;

    if (cu->pred_mode == MODE_INTER && sps->sbt_enabled_flag && !cu->ciip_flag
        && cb_width <= sps->max_tb_size_y && cb_height <= sps->max_tb_size_y) {
        const int sbt_ver_h = cb_width  >= 8;
        const int sbt_hor_h = cb_height >= 8;
        cu->sbt_flag = 0;
        if (sbt_ver_h || sbt_hor_h)
            cu->sbt_flag = ff_vvc_sbt_flag(lc);
        if (cu->sbt_flag) {
            const int sbt_ver_q = cb_width  >= 16;
            const int sbt_hor_q = cb_height >= 16;
            int cu_sbt_quad_flag = 0;

            if ((sbt_ver_h || sbt_hor_h) && (sbt_ver_q || sbt_hor_q))
                cu_sbt_quad_flag = ff_vvc_sbt_quad_flag(lc);
            if (cu_sbt_quad_flag) {
                cu->sbt_horizontal_flag = sbt_hor_q;
                if (sbt_ver_q && sbt_hor_q)
                    cu->sbt_horizontal_flag = ff_vvc_sbt_horizontal_flag(lc);
            } else {
                cu->sbt_horizontal_flag = sbt_hor_h;
                if (sbt_ver_h && sbt_hor_h)
                    cu->sbt_horizontal_flag = ff_vvc_sbt_horizontal_flag(lc);
            }
            cu->sbt_pos_flag = ff_vvc_sbt_pos_flag(lc);

            {
                const int sbt_min = cu_sbt_quad_flag ? 1 : 2;
                lc->parse.sbt_num_fourths_tb0 = cu->sbt_pos_flag ? (4 - sbt_min) : sbt_min;
            }
        }
    }
}

static int skipped_transform_tree_unit(VVCLocalContext *lc)
{
    const CodingUnit *cu = lc->cu;
    int ret;

    set_qp_y(lc, cu->x0, cu->y0, 0);
    set_qp_c(lc);
    ret = skipped_transform_tree(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
    if (ret < 0)
        return ret;
    return 0;
}

static void set_cb_pos(const VVCFrameContext *fc, const CodingUnit *cu)
{
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const int log2_min_cb_size  = sps->min_cb_log2_size_y;
    const int x_cb              = cu->x0 >> log2_min_cb_size;
    const int y_cb              = cu->y0 >> log2_min_cb_size;
    const int ch_type           = cu->ch_type;
    int x, y;

    x = y_cb * pps->min_cb_width + x_cb;
    for (y = 0; y < (cu->cb_height >> log2_min_cb_size); y++) {
        const int width = cu->cb_width >> log2_min_cb_size;

        for (int i = 0; i < width; i++) {
            fc->tab.cb_pos_x[ch_type][x + i] = cu->x0;
            fc->tab.cb_pos_y[ch_type][x + i] = cu->y0;
        }
        memset(&fc->tab.cb_width[ch_type][x], cu->cb_width, width);
        memset(&fc->tab.cb_height[ch_type][x], cu->cb_height, width);
        memset(&fc->tab.cqt_depth[ch_type][x], cu->cqt_depth, width);

        x += pps->min_cb_width;
    }
}

static CodingUnit* add_cu(VVCLocalContext *lc, const int x0, const int y0,
    const int cb_width, const int cb_height, const int cqt_depth, const VVCTreeType tree_type)
{
    VVCFrameContext *fc = lc->fc;
    const int ch_type   = tree_type == DUAL_TREE_CHROMA ? 1 : 0;
    const VVCSPS *sps   = fc->ps.sps;
    const VVCPPS *pps   = fc->ps.pps;
    const int rx        = x0 >> sps->ctb_log2_size_y;
    const int ry        = y0 >> sps->ctb_log2_size_y;
    CTU *ctu            = fc->tab.ctus + ry * pps->ctb_width + rx;
    CodingUnit *cu;

    AVBufferRef *buf = av_buffer_pool_get(fc->cu_pool);
    if (!buf)
        return NULL;
    cu = (CodingUnit *)buf->data;
    cu->next = NULL;
    cu->buf = buf;

    if (ctu->cus)
        lc->cu->next = cu;
    else
        ctu->cus = cu;
    lc->cu = cu;

    memset(&cu->pu, 0, sizeof(cu->pu));

    lc->parse.prev_tu_cbf_y = 0;

    cu->sbt_flag = 0;
    cu->act_enabled_flag = 0;

    cu->tree_type = tree_type;
    cu->x0 = x0;
    cu->y0 = y0;
    cu->cb_width = cb_width;
    cu->cb_height = cb_height;
    cu->ch_type = ch_type;
    cu->cqt_depth = cqt_depth;
    cu->num_tus = 0;
    cu->bdpcm_flag[LUMA] = cu->bdpcm_flag[CB] = cu->bdpcm_flag[CR] = 0;
    cu->isp_split_type = ISP_NO_SPLIT;
    cu->intra_mip_flag = 0;
    cu->ciip_flag = 0;
    cu->coded_flag = 1;
    cu->num_intra_subpartitions = 1;

    set_cb_pos(fc, cu);
    return cu;
}

static void set_cu_tabs(const VVCLocalContext *lc, const CodingUnit *cu)
{
    const VVCFrameContext *fc = lc->fc;

    ff_vvc_set_cb_tab(lc, fc->tab.cpm[cu->ch_type], cu->pred_mode);
    if (cu->tree_type != DUAL_TREE_CHROMA)
        ff_vvc_set_cb_tab(lc, fc->tab.skip, cu->skip_flag);

    for (int i = 0; i < cu->num_tus; i++) {
        const TransformUnit *tu = cu->tus + i;
        for (int j = 0; j < tu->nb_tbs; j++) {
            const TransformBlock *tb = tu->tbs + j;
            if (tb->c_idx != LUMA)
                set_qp_c_tab(lc, tu, tb);
            if (tb->c_idx != CR && cu->bdpcm_flag[tb->c_idx])
                set_tb_tab(fc->tab.pcmf[tb->c_idx], 1, fc, tb);
        }
    }
}

static int hls_coding_unit(VVCLocalContext *lc, int x0, int y0, int cb_width, int cb_height,
    int cqt_depth, const VVCTreeType tree_type, VVCModeType mode_type)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCSH *sh             = &lc->sc->sh;
    const int hs                = sps->hshift[CHROMA];
    const int vs                = sps->vshift[CHROMA];
    const int is_128            = cb_width > 64 || cb_height > 64;
    int pred_mode_plt_flag = 0;
    int ret;

    CodingUnit *cu = add_cu(lc, x0, y0, cb_width, cb_height, cqt_depth, tree_type);

    if (!cu)
        return AVERROR(ENOMEM);

    ff_vvc_set_neighbour_available(lc, cu->x0, cu->y0, cu->cb_width, cu->cb_height);

    if (IS_I(sh) && is_128)
        mode_type = MODE_TYPE_INTRA;
    cu->pred_mode = pred_mode_decode(lc, tree_type, mode_type);

    if (cu->pred_mode == MODE_INTRA && sps->palette_enabled_flag && !is_128 && !cu->skip_flag &&
        mode_type != MODE_TYPE_INTER && ((cb_width * cb_height) >
        (tree_type != DUAL_TREE_CHROMA ? 16 : (16 << hs << vs))) &&
        (mode_type != MODE_TYPE_INTRA || tree_type != DUAL_TREE_CHROMA)) {
        pred_mode_plt_flag = ff_vvc_pred_mode_plt_flag(lc);
        if (pred_mode_plt_flag) {
            avpriv_report_missing_feature(lc->fc->avctx, "Palette");
            return AVERROR_PATCHWELCOME;
        }
    }
    if (cu->pred_mode == MODE_INTRA && sps->act_enabled_flag && tree_type == SINGLE_TREE) {
        avpriv_report_missing_feature(fc->avctx, "Adaptive Color Transform");
        return AVERROR_PATCHWELCOME;
    }
    if (cu->pred_mode == MODE_INTRA || cu->pred_mode == MODE_PLT) {
        if (tree_type == SINGLE_TREE || tree_type == DUAL_TREE_LUMA) {
            if (pred_mode_plt_flag) {
                avpriv_report_missing_feature(lc->fc->avctx, "Palette");
                return AVERROR_PATCHWELCOME;
            } else {
                intra_luma_pred_modes(lc);
            }
            ff_vvc_set_intra_mvf(lc);
        }
        if ((tree_type == SINGLE_TREE || tree_type == DUAL_TREE_CHROMA) && sps->chroma_format_idc) {
            if (pred_mode_plt_flag && tree_type == DUAL_TREE_CHROMA) {
                avpriv_report_missing_feature(lc->fc->avctx, "Palette");
                return AVERROR_PATCHWELCOME;
            } else if (!pred_mode_plt_flag) {
                if (!cu->act_enabled_flag)
                    intra_chroma_pred_modes(lc);
            }
        }
    } else if (tree_type != DUAL_TREE_CHROMA) { /* MODE_INTER or MODE_IBC */
        if ((ret = ff_vvc_inter_data(lc)) < 0)
            return ret;
    }
    if (cu->pred_mode != MODE_INTRA && !pred_mode_plt_flag && !lc->cu->pu.general_merge_flag)
        cu->coded_flag = ff_vvc_cu_coded_flag(lc);
    else
        cu->coded_flag = !(cu->skip_flag || pred_mode_plt_flag);

    if (cu->coded_flag) {
        sbt_info(lc, sps);
        if (sps->act_enabled_flag && cu->pred_mode != MODE_INTRA && tree_type == SINGLE_TREE) {
            avpriv_report_missing_feature(fc->avctx, "Adaptive Color Transform");
            return AVERROR_PATCHWELCOME;
        }
        lc->parse.lfnst_dc_only = 1;
        lc->parse.lfnst_zero_out_sig_coeff_flag = 1;
        lc->parse.mts_dc_only = 1;
        lc->parse.mts_zero_out_sig_coeff_flag = 1;
        ret = hls_transform_tree(lc, x0, y0, cb_width, cb_height, cu->ch_type);
        if (ret < 0)
            return ret;
        cu->lfnst_idx = lfnst_idx_decode(lc);
        cu->mts_idx = mts_idx_decode(lc);
        set_qp_c(lc);
        if (ret < 0)
            return ret;
    } else {
        av_assert0(tree_type == SINGLE_TREE);
        ret = skipped_transform_tree_unit(lc);
        if (ret < 0)
            return ret;
    }
    set_cu_tabs(lc, cu);

    return 0;
}

static int derive_mode_type_condition(const VVCLocalContext *lc,
    const VVCSplitMode split, const int cb_width, const int cb_height, const VVCModeType mode_type_curr)
{
    const VVCSH *sh     = &lc->sc->sh;
    const VVCSPS *sps   = lc->fc->ps.sps;
    const int area      = cb_width * cb_height;

    if ((IS_I(sh) && sps->qtbtt_dual_tree_intra_flag) ||
        mode_type_curr != MODE_TYPE_ALL || !sps->chroma_format_idc ||
        sps->chroma_format_idc == CHROMA_FORMAT_444)
        return 0;
    if ((area == 64 && (split == SPLIT_QT || split == SPLIT_TT_HOR || split == SPLIT_TT_VER)) ||
        (area == 32 &&  (split == SPLIT_BT_HOR || split == SPLIT_BT_VER)))
        return 1;
    if ((area == 64 && (split == SPLIT_BT_HOR || split == SPLIT_BT_VER) && sps->chroma_format_idc == CHROMA_FORMAT_420) ||
        (area == 128 && (split == SPLIT_TT_HOR || split == SPLIT_TT_VER) && sps->chroma_format_idc == CHROMA_FORMAT_420) ||
        (cb_width == 8 && split == SPLIT_BT_VER) || (cb_width == 16 && split == SPLIT_TT_VER))
        return 1 + !IS_I(sh);

    return 0;
}

static VVCModeType mode_type_decode(VVCLocalContext *lc, const int x0, const int y0,
    const int cb_width, const int cb_height, const VVCSplitMode split, const int ch_type,
    const VVCModeType mode_type_curr)
{
    VVCModeType mode_type;
    const int mode_type_condition = derive_mode_type_condition(lc, split, cb_width, cb_height, mode_type_curr);

    if (mode_type_condition == 1)
        mode_type = MODE_TYPE_INTRA;
    else if (mode_type_condition == 2) {
        mode_type = ff_vvc_non_inter_flag(lc, x0, y0, ch_type) ? MODE_TYPE_INTRA : MODE_TYPE_INTER;
    } else {
        mode_type = mode_type_curr;
    }

    return mode_type;
}

static int hls_coding_tree(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset, int part_idx,
    VVCSplitMode last_split_mode, VVCTreeType tree_type_curr, VVCModeType mode_type_curr);

static int coding_tree_btv(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(x, idx) do { \
    ret = hls_coding_tree(lc, x, y0, cb_width / 2, cb_height, \
        qg_on_y, qg_on_c, cb_sub_div + 1, cqt_depth, mtt_depth + 1, \
        depth_offset, idx, SPLIT_BT_VER, tree_type, mode_type); \
    if (ret < 0) \
        return ret; \
} while (0);

    const VVCPPS *pps = lc->fc->ps.pps;
    const int x1 = x0 + cb_width / 2;
    int ret = 0;

    depth_offset += (x0 + cb_width > pps->width) ? 1 : 0;
    CODING_TREE(x0, 0);
    if (x1 < pps->width)
        CODING_TREE(x1, 1);

    return 0;

#undef CODING_TREE
}

static int coding_tree_bth(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(y, idx) do { \
        ret = hls_coding_tree(lc, x0, y, cb_width , cb_height / 2, \
            qg_on_y, qg_on_c, cb_sub_div + 1, cqt_depth, mtt_depth + 1, \
            depth_offset, idx, SPLIT_BT_HOR, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCPPS *pps = lc->fc->ps.pps;
    const int y1 = y0 + (cb_height / 2);
    int ret = 0;

    depth_offset += (y0 + cb_height > pps->height) ? 1 : 0;
    CODING_TREE(y0, 0);
    if (y1 < pps->height)
        CODING_TREE(y1, 1);

    return 0;

#undef CODING_TREE
}

static int coding_tree_ttv(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(x, w, sub_div, idx) do { \
        ret = hls_coding_tree(lc, x, y0, w, cb_height, \
            qg_on_y, qg_on_c, sub_div, cqt_depth, mtt_depth + 1, \
            depth_offset, idx, SPLIT_TT_VER, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCSH *sh = &lc->sc->sh;
    const int x1    = x0 + cb_width / 4;
    const int x2    = x0 + cb_width * 3 / 4;
    int ret;

    qg_on_y = qg_on_y && (cb_sub_div + 2 <= sh->cu_qp_delta_subdiv);
    qg_on_c = qg_on_c && (cb_sub_div + 2 <= sh->cu_chroma_qp_offset_subdiv);

    CODING_TREE(x0, cb_width / 4, cb_sub_div + 2, 0);
    CODING_TREE(x1, cb_width / 2, cb_sub_div + 1, 1);
    CODING_TREE(x2, cb_width / 4, cb_sub_div + 2, 2);

    return 0;

#undef CODING_TREE
}

static int coding_tree_tth(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(y, h, sub_div, idx) do { \
        ret = hls_coding_tree(lc, x0, y, cb_width, h, \
            qg_on_y, qg_on_c, sub_div, cqt_depth, mtt_depth + 1, \
            depth_offset, idx, SPLIT_TT_HOR, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCSH *sh = &lc->sc->sh;
    const int y1    = y0 + (cb_height / 4);
    const int y2    = y0 + (3 * cb_height / 4);
    int ret;

    qg_on_y = qg_on_y && (cb_sub_div + 2 <= sh->cu_qp_delta_subdiv);
    qg_on_c = qg_on_c && (cb_sub_div + 2 <= sh->cu_chroma_qp_offset_subdiv);

    CODING_TREE(y0, cb_height / 4, cb_sub_div + 2, 0);
    CODING_TREE(y1, cb_height / 2, cb_sub_div + 1, 1);
    CODING_TREE(y2, cb_height / 4, cb_sub_div + 2, 2);

    return 0;

#undef CODING_TREE
}

static int coding_tree_qt(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type)
{
#define CODING_TREE(x, y, idx) do { \
        ret = hls_coding_tree(lc, x, y, cb_width / 2, cb_height / 2, \
            qg_on_y, qg_on_c, cb_sub_div + 2, cqt_depth + 1, 0, 0, \
            idx, SPLIT_QT, tree_type, mode_type); \
        if (ret < 0) \
            return ret; \
    } while (0);

    const VVCPPS *pps = lc->fc->ps.pps;
    const int x1 = x0 + cb_width / 2;
    const int y1 = y0 + cb_height / 2;
    int ret = 0;

    CODING_TREE(x0, y0, 0);
    if (x1 < pps->width)
        CODING_TREE(x1, y0, 1);
    if (y1 < pps->height)
        CODING_TREE(x0, y1, 2);
    if (x1 < pps->width &&
        y1 < pps->height)
        CODING_TREE(x1, y1, 3);

    return 0;

#undef CODING_TREE
}

typedef int (*coding_tree_fn)(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset,
    VVCTreeType tree_type, VVCModeType mode_type);

const static coding_tree_fn coding_tree[] = {
    coding_tree_tth,
    coding_tree_bth,
    coding_tree_ttv,
    coding_tree_btv,
    coding_tree_qt,
};

static int hls_coding_tree(VVCLocalContext *lc,
    int x0, int y0, int cb_width, int cb_height, int qg_on_y, int qg_on_c,
    int cb_sub_div, int cqt_depth, int mtt_depth, int depth_offset, int part_idx,
    VVCSplitMode last_split_mode, VVCTreeType tree_type_curr, VVCModeType mode_type_curr)
{
    VVCFrameContext *fc = lc->fc;
    const VVCPPS *pps   = fc->ps.pps;
    const VVCSH *sh     = &lc->sc->sh;
    const int ch_type   = tree_type_curr == DUAL_TREE_CHROMA;
    int ret;
    VVCAllowedSplit allowed;

    if (pps->cu_qp_delta_enabled_flag && qg_on_y && cb_sub_div <= sh->cu_qp_delta_subdiv) {
        lc->parse.is_cu_qp_delta_coded = 0;
        lc->parse.cu_qg_top_left_x = x0;
        lc->parse.cu_qg_top_left_y = y0;
    }
    if (sh->cu_chroma_qp_offset_enabled_flag && qg_on_c &&
        cb_sub_div <= sh->cu_chroma_qp_offset_subdiv) {
        lc->parse.is_cu_chroma_qp_offset_coded = 0;
        memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));
    }

    can_split(lc, x0, y0, cb_width, cb_height, mtt_depth, depth_offset, part_idx,
        last_split_mode, tree_type_curr, mode_type_curr, &allowed);
    if (ff_vvc_split_cu_flag(lc, x0, y0, cb_width, cb_height, ch_type, &allowed)) {
        VVCSplitMode split      = ff_vvc_split_mode(lc, x0, y0, cb_width, cb_height, cqt_depth, mtt_depth, ch_type, &allowed);
        VVCModeType mode_type   = mode_type_decode(lc, x0, y0, cb_width, cb_height, split, ch_type, mode_type_curr);

        VVCTreeType tree_type   = (mode_type == MODE_TYPE_INTRA) ? DUAL_TREE_LUMA : tree_type_curr;

        if (split != SPLIT_QT) {
            if (!(x0 & 31) && !(y0 & 31) && mtt_depth <= 1)
                TAB_MSM(fc, mtt_depth, x0, y0) = split;
        }
        ret = coding_tree[split - 1](lc, x0, y0, cb_width, cb_height, qg_on_y, qg_on_c,
            cb_sub_div, cqt_depth, mtt_depth, depth_offset, tree_type, mode_type);
        if (ret < 0)
            return ret;
        if (mode_type_curr == MODE_TYPE_ALL && mode_type == MODE_TYPE_INTRA) {
            ret = hls_coding_tree(lc, x0, y0, cb_width, cb_height, 0, qg_on_c, cb_sub_div,
                cqt_depth, mtt_depth, 0, 0, split, DUAL_TREE_CHROMA, mode_type);
            if (ret < 0)
                return ret;
        }
    } else {
        ret = hls_coding_unit(lc, x0, y0, cb_width, cb_height, cqt_depth, tree_type_curr, mode_type_curr);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int dual_tree_implicit_qt_split(VVCLocalContext *lc,
    const int x0, const int y0, const int cb_size, const int cqt_depth)
{
    const VVCSH *sh     = &lc->sc->sh;
    const VVCPPS *pps   = lc->fc->ps.pps;
    const int cb_subdiv = 2 * cqt_depth;
    int ret;

    if (cb_size > 64) {
        #define DUAL_TREE(x, y) do {                                                \
            ret = dual_tree_implicit_qt_split(lc, x, y, cb_size / 2, cqt_depth + 1); \
            if (ret < 0)                                                            \
                return ret;                                                         \
        } while (0)

        const int x1 = x0 + (cb_size / 2);
        const int y1 = y0 + (cb_size / 2);
        if (pps->cu_qp_delta_enabled_flag && cb_subdiv <= sh->cu_qp_delta_subdiv) {
            lc->parse.is_cu_qp_delta_coded = 0;
            lc->parse.cu_qg_top_left_x = x0;
            lc->parse.cu_qg_top_left_y = y0;
        }
        if (sh->cu_chroma_qp_offset_enabled_flag && cb_subdiv <= sh->cu_chroma_qp_offset_subdiv) {
            lc->parse.is_cu_chroma_qp_offset_coded = 0;
            memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));
        }
        DUAL_TREE(x0, y0);
        if (x1 < pps->width)
            DUAL_TREE(x1, y0);
        if (y1 < pps->height)
            DUAL_TREE(x0, y1);
        if (x1 < pps->width && y1 < pps->height)
            DUAL_TREE(x1, y1);
    #undef DUAL_TREE
    } else {
        #define CODING_TREE(tree_type) do {                                             \
            const int qg_on_y = tree_type == DUAL_TREE_LUMA;                            \
            ret = hls_coding_tree(lc, x0, y0, cb_size, cb_size, qg_on_y, !qg_on_y,           \
                 cb_subdiv, cqt_depth, 0, 0, 0, SPLIT_NONE, tree_type, MODE_TYPE_ALL);  \
            if (ret < 0)                                                                \
                return ret;                                                             \
        } while (0)
        CODING_TREE(DUAL_TREE_LUMA);
        CODING_TREE(DUAL_TREE_CHROMA);
        #undef CODING_TREE
    }
    return 0;
}

#define SET_SAO(elem, value)                            \
do {                                                    \
    if (!sao_merge_up_flag && !sao_merge_left_flag)     \
        sao->elem = value;                              \
    else if (sao_merge_left_flag)                       \
        sao->elem = CTB(fc->tab.sao, rx-1, ry).elem;         \
    else if (sao_merge_up_flag)                         \
        sao->elem = CTB(fc->tab.sao, rx, ry-1).elem;         \
    else                                                \
        sao->elem = 0;                                  \
} while (0)

static void hls_sao(VVCLocalContext *lc, const int rx, const int ry)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCSH *sh         = &lc->sc->sh;
    int sao_merge_left_flag = 0;
    int sao_merge_up_flag   = 0;
    SAOParams *sao          = &CTB(fc->tab.sao, rx, ry);
    int c_idx, i;

    if (sh->sao_used_flag[0] || sh->sao_used_flag[1]) {
        if (rx > 0) {
            if (lc->ctb_left_flag)
                sao_merge_left_flag = ff_vvc_sao_merge_flag_decode(lc);
        }
        if (ry > 0 && !sao_merge_left_flag) {
            if (lc->ctb_up_flag)
                sao_merge_up_flag = ff_vvc_sao_merge_flag_decode(lc);
        }
    }

    for (c_idx = 0; c_idx < (fc->ps.sps->chroma_format_idc ? 3 : 1); c_idx++) {
        if (!sh->sao_used_flag[c_idx]) {
            sao->type_idx[c_idx] = SAO_NOT_APPLIED;
            continue;
        }

        if (c_idx == 2) {
            sao->type_idx[2] = sao->type_idx[1];
            sao->eo_class[2] = sao->eo_class[1];
        } else {
            SET_SAO(type_idx[c_idx], ff_vvc_sao_type_idx_decode(lc));
        }

        if (sao->type_idx[c_idx] == SAO_NOT_APPLIED)
            continue;

        for (i = 0; i < 4; i++)
            SET_SAO(offset_abs[c_idx][i], ff_vvc_sao_offset_abs_decode(lc));

        if (sao->type_idx[c_idx] == SAO_BAND) {
            for (i = 0; i < 4; i++) {
                if (sao->offset_abs[c_idx][i]) {
                    SET_SAO(offset_sign[c_idx][i],
                            ff_vvc_sao_offset_sign_decode(lc));
                } else {
                    sao->offset_sign[c_idx][i] = 0;
                }
            }
            SET_SAO(band_position[c_idx], ff_vvc_sao_band_position_decode(lc));
        } else if (c_idx != 2) {
            SET_SAO(eo_class[c_idx], ff_vvc_sao_eo_class_decode(lc));
        }

        // Inferred parameters
        sao->offset_val[c_idx][0] = 0;
        for (i = 0; i < 4; i++) {
            sao->offset_val[c_idx][i + 1] = sao->offset_abs[c_idx][i];
            if (sao->type_idx[c_idx] == SAO_EDGE) {
                if (i > 1)
                    sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            } else if (sao->offset_sign[c_idx][i]) {
                sao->offset_val[c_idx][i + 1] = -sao->offset_val[c_idx][i + 1];
            }
            sao->offset_val[c_idx][i + 1] *= 1 << (fc->ps.sps->bit_depth - FFMIN(10, fc->ps.sps->bit_depth));
        }
    }
}

static void alf_params(VVCLocalContext *lc, const int rx, const int ry)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSH *sh             = &lc->sc->sh;
    ALFParams *alf              = &CTB(fc->tab.alf, rx, ry);

    alf->ctb_flag[LUMA] = alf->ctb_flag[CB] = alf->ctb_flag[CR] = 0;
    if (sh->alf.enabled_flag[LUMA]) {
        alf->ctb_flag[LUMA] = ff_vvc_alf_ctb_flag(lc, rx, ry, LUMA);
        if (alf->ctb_flag[LUMA]) {
            int alf_use_aps_flag = 0;
            if (sh->alf.num_aps_ids_luma > 0) {
                alf_use_aps_flag = ff_vvc_alf_use_aps_flag(lc);
            }
            if (alf_use_aps_flag) {
                alf->ctb_filt_set_idx_y = 16;
                if (sh->alf.num_aps_ids_luma > 1)
                    alf->ctb_filt_set_idx_y += ff_vvc_alf_luma_prev_filter_idx(lc);
            } else {
                alf->ctb_filt_set_idx_y = ff_vvc_alf_luma_fixed_filter_idx(lc);
            }
        }
        for (int c_idx = CB; c_idx <= CR; c_idx++) {
            if (sh->alf.enabled_flag[c_idx]) {
                const VVCALF *aps = (VVCALF*)fc->ps.alf_list[sh->alf.aps_id_chroma]->data;
                alf->ctb_flag[c_idx] = ff_vvc_alf_ctb_flag(lc, rx, ry, c_idx);
                alf->alf_ctb_filter_alt_idx[c_idx - 1] = 0;
                if (alf->ctb_flag[c_idx] && aps->num_chroma_filters > 1)
                    alf->alf_ctb_filter_alt_idx[c_idx - 1] = ff_vvc_alf_ctb_filter_alt_idx(lc, c_idx, aps->num_chroma_filters);
            }
        }
    }
    for (int i = 0; i < 2; i++) {
        alf->ctb_cc_idc[i] = 0;
        if (sh->alf.cc_enabled_flag[i]) {
            const VVCALF *aps = (VVCALF*)fc->ps.alf_list[sh->alf.cc_aps_id[i]]->data;
            alf->ctb_cc_idc[i] = ff_vvc_alf_ctb_cc_idc(lc, rx, ry, i, aps->cc_filters_signalled[i]);
        }
    }
}

static void deblock_params(VVCLocalContext *lc, const int rx, const int ry)
{
    VVCFrameContext *fc = lc->fc;
    const VVCSH *sh     = &lc->sc->sh;
    CTB(fc->tab.deblock, rx, ry) = sh->deblock;
}

static int hls_coding_tree_unit(VVCLocalContext *lc,  int x0, int y0)
{
    int ret = 0;
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCSH *sh             = &lc->sc->sh;
    const unsigned int ctb_size = sps->ctb_size_y;

    memset(lc->parse.chroma_qp_offset, 0, sizeof(lc->parse.chroma_qp_offset));

    hls_sao(lc, x0 >> sps->ctb_log2_size_y, y0 >> sps->ctb_log2_size_y);
    alf_params(lc, x0 >> sps->ctb_log2_size_y, y0 >> sps->ctb_log2_size_y);
    deblock_params(lc, x0 >> sps->ctb_log2_size_y, y0 >> sps->ctb_log2_size_y);

    if (IS_I(sh) && sps->qtbtt_dual_tree_intra_flag)
        ret = dual_tree_implicit_qt_split(lc, x0, y0, ctb_size, 0);
    else
        ret = hls_coding_tree(lc, x0, y0, ctb_size, ctb_size,
            1, 1, 0, 0, 0, 0, 0, SPLIT_NONE, SINGLE_TREE, MODE_TYPE_ALL);
    if (ret < 0)
        return ret;

    return 0;
}

void ff_vvc_decode_neighbour(VVCLocalContext *lc, const int x_ctb, const int y_ctb,
    const int rx, const int ry, const int rs)
{
    VVCFrameContext *fc = lc->fc;
    const int ctb_size         = fc->ps.sps->ctb_size_y;

    lc->end_of_tiles_x = fc->ps.sps->width;
    lc->end_of_tiles_y = fc->ps.sps->height;
    if (fc->ps.pps->ctb_to_col_bd[rx] != fc->ps.pps->ctb_to_col_bd[rx + 1])
        lc->end_of_tiles_x = FFMIN(x_ctb + ctb_size, lc->end_of_tiles_x);
    if (fc->ps.pps->ctb_to_row_bd[ry] != fc->ps.pps->ctb_to_row_bd[ry + 1])
        lc->end_of_tiles_y = FFMIN(y_ctb + ctb_size, lc->end_of_tiles_y);

    lc->boundary_flags = 0;
    if (rx > 0 && fc->ps.pps->ctb_to_col_bd[rx] != fc->ps.pps->ctb_to_col_bd[rx - 1])
        lc->boundary_flags |= BOUNDARY_LEFT_TILE;
    if (rx > 0 && fc->tab.slice_idx[rs] != fc->tab.slice_idx[rs - 1])
        lc->boundary_flags |= BOUNDARY_LEFT_SLICE;
    if (ry > 0 && fc->ps.pps->ctb_to_row_bd[ry] != fc->ps.pps->ctb_to_row_bd[ry - 1])
        lc->boundary_flags |= BOUNDARY_UPPER_TILE;
    if (ry > 0 && fc->tab.slice_idx[rs] != fc->tab.slice_idx[rs - fc->ps.pps->ctb_width])
        lc->boundary_flags |= BOUNDARY_UPPER_SLICE;
    lc->ctb_left_flag = rx > 0 && !(lc->boundary_flags & BOUNDARY_LEFT_TILE);
    lc->ctb_up_flag   = ry > 0 && !(lc->boundary_flags & BOUNDARY_UPPER_TILE) && !(lc->boundary_flags & BOUNDARY_UPPER_SLICE);
    lc->ctb_up_right_flag = lc->ctb_up_flag && (fc->ps.pps->ctb_to_col_bd[rx] == fc->ps.pps->ctb_to_col_bd[rx + 1]) &&
        (fc->ps.pps->ctb_to_row_bd[ry] == fc->ps.pps->ctb_to_row_bd[ry - 1]);
    lc->ctb_up_left_flag = lc->ctb_left_flag && lc->ctb_up_flag;
}

int ff_vvc_reconstruct(VVCLocalContext *lc, const int rs, const int rx, const int ry)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const int x_ctb             = rx << sps->ctb_log2_size_y;
    const int y_ctb             = ry << sps->ctb_log2_size_y;
    CTU *ctu                    = fc->tab.ctus + rs;
    CodingUnit *cu              = ctu->cus;
    int ret                     = 0;

    lc->num_ras[0] = lc->num_ras[1] = 0;
    lc->lmcs.x_vpdu = -1;
    lc->lmcs.y_vpdu = -1;
    ff_vvc_decode_neighbour(lc, x_ctb, y_ctb, rx, ry, rs);
    while (cu) {
        lc->cu = cu;

        if (cu->ciip_flag)
            ff_vvc_predict_ciip(lc);
        if (cu->coded_flag) {
            ret = reconstruct(lc);
        } else {
            add_reconstructed_area(lc, LUMA, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
            add_reconstructed_area(lc, CHROMA, cu->x0, cu->y0, cu->cb_width, cu->cb_height);
        }
        cu = cu->next;
    }
    ff_vvc_ctu_free_cus(ctu);
    return ret;
}

int ff_vvc_coding_tree_unit(VVCLocalContext *lc, const int ctb_addr, const int rs, const int rx, const int ry)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCSH *sh             = &lc->sc->sh;
    const int x_ctb             = rx << sps->ctb_log2_size_y;
    const int y_ctb             = ry << sps->ctb_log2_size_y;
    const int ctb_size          = 1 << sps->ctb_log2_size_y << sps->ctb_log2_size_y;
    EntryPoint* ep              = lc->ep;
    int ret;

    if (rx == pps->ctb_to_col_bd[rx]) {
        //fix me for ibc
        ep->num_hmvp = 0;
        ep->is_first_qg = ry == pps->ctb_to_row_bd[ry] || !ctb_addr;
    }

    lc->coeffs = fc->tab.coeffs + (ry * pps->ctb_width + rx) * ctb_size * VVC_MAX_SAMPLE_ARRAYS;

    ff_vvc_cabac_init(lc, ctb_addr, rx, ry);
    fc->tab.slice_idx[rs] = lc->sc->slice_idx;
    ff_vvc_decode_neighbour(lc, x_ctb, y_ctb, rx, ry, rs);
    ret = hls_coding_tree_unit(lc, x_ctb, y_ctb);
    if (ret < 0)
        return ret;

    if (rx == pps->ctb_to_col_bd[rx + 1] - 1) {
        if (ctb_addr == sh->num_ctus_in_curr_slice - 1) {
            const int end_of_slice_one_bit = ff_vvc_end_of_slice_flag_decode(lc);
            if (!end_of_slice_one_bit)
                return AVERROR_INVALIDDATA;
        } else {
            if (ry == pps->ctb_to_row_bd[ry + 1] - 1) {
                const int end_of_tile_one_bit = ff_vvc_end_of_tile_one_bit(lc);
                if (!end_of_tile_one_bit)
                    return AVERROR_INVALIDDATA;
            } else {
                if (fc->ps.sps->entropy_coding_sync_enabled_flag) {
                    const int end_of_subset_one_bit = ff_vvc_end_of_subset_one_bit(lc);
                    if (!end_of_subset_one_bit)
                        return AVERROR_INVALIDDATA;
                }
            }
        }
    }
    return 0;
}
