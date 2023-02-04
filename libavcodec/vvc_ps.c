/*
 * VVC Parameter Set decoding
 *
 * Copyright (C) 2021 Nuo Mi
 * Copyright (C) 2022 Xu Mu
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
#include "libavutil/imgutils.h"
#include "libavutil/internal.h"
#include "golomb.h"
#include "vvc_data.h"
#include "vvc_ps.h"
#include "vvcdec.h"

#define u(b, f, min, max) do { \
    f = get_bits(gb, b); \
    if ((f < (min)) || (f > (max))) { \
        av_log(log_ctx, AV_LOG_ERROR, \
               #f " %d should in range [%d, %d]", f, (min), (max)); \
    }\
} while (0)

#define uep(f, plus, min, max) do { \
    f = get_ue_golomb_long(gb) + plus; \
    if (((f) < (min)) || ((f) > (max))) {\
        av_log(log_ctx, AV_LOG_ERROR, \
               #f " %d should in range [%d, %d]", f, (min), (max)); \
        return AVERROR_INVALIDDATA; \
    } \
} while (0)

#define ue(f, max) do { \
    f = get_ue_golomb_long(gb); \
    if ((f) > (max)) {\
        av_log(log_ctx, AV_LOG_ERROR, \
               #f " %d should < %d", f, (max)); \
        return AVERROR_INVALIDDATA; \
    } \
} while (0)

#define sep(f, plus, min, max) do {\
    f = get_se_golomb(gb) + plus; \
    if ((f < (min)) || (f > (max))) {\
        av_log(log_ctx, AV_LOG_ERROR, \
               #f " %d should in range [%d, %d]", f, (min), (max)); \
        return AVERROR_INVALIDDATA; \
    } \
} while (0)

#define se(f, min, max) do {\
    f = get_se_golomb(gb); \
    if ((f < (min)) || (f > (max))) {\
        av_log(log_ctx, AV_LOG_ERROR, \
               #f " %d should in range [%d, %d]", f, (min), (max)); \
        return AVERROR_INVALIDDATA; \
    } \
} while (0)

typedef struct VVCLMCS {
    int min_bin_idx;
    int max_bin_idx;
    int delta_cw[LMCS_MAX_BIN_SIZE];
    int delta_crs;
} VVCLMCS;

static av_always_inline unsigned int vvc_ceil(unsigned int v, unsigned int align)
{
    return (((v) + (align) - 1) / (align));
}

static void remove_pps(VVCParamSets *s, int id)
{
    av_buffer_unref(&s->pps_list[id]);
}

static void remove_sps(VVCParamSets *s, int id)
{
    if (s->sps_list[id]) {
        /* drop all PPS that depend on this SPS */
        for (int i = 0; i < FF_ARRAY_ELEMS(s->pps_list); i++)
            if (s->pps_list[i] && ((VVCPPS*)s->pps_list[i]->data)->seq_parameter_set_id == id)
                remove_pps(s, i);
    }
    av_buffer_unref(&s->sps_list[id]);
}

static int gci_parse(GeneralConstraintsInfo *gci, GetBitContext *gb, void *log_ctx)
{
    int i;

    gci->present_flag = get_bits1(gb);
    if (gci->present_flag) {
        unsigned int num_reserved_bits;
        /* general */
        gci->intra_only_constraint_flag = get_bits1(gb);
        gci->all_layers_independent_constraint_flag = get_bits1(gb);
        gci->one_au_only_constraint_flag = get_bits1(gb);

        /* picture format */
        gci->sixteen_minus_max_bitdepth_constraint_idc = get_bits(gb, 4);
        if (gci->sixteen_minus_max_bitdepth_constraint_idc > 8U) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "sixteen_minus_max_bitdepth_constraint_idc  %d is invalid\n",
                   gci->sixteen_minus_max_bitdepth_constraint_idc);
            return AVERROR_INVALIDDATA;
        }
        gci-> three_minus_max_chroma_format_constraint_idc = get_bits(gb, 2);

        /* NAL unit type related */
        gci->no_mixed_nalu_types_in_pic_constraint_flag = get_bits1(gb);
        gci->no_trail_constraint_flag = get_bits1(gb);
        gci->no_stsa_constraint_flag = get_bits1(gb);
        gci->no_rasl_constraint_flag = get_bits1(gb);
        gci->no_radl_constraint_flag = get_bits1(gb);
        gci->no_idr_constraint_flag = get_bits1(gb);
        gci->no_cra_constraint_flag = get_bits1(gb);
        gci->no_gdr_constraint_flag = get_bits1(gb);
        gci->no_aps_constraint_flag = get_bits1(gb);
        gci->no_idr_rpl_constraint_flag = get_bits1(gb);

        /* tile, slice, subpicture partitioning */
        gci->one_tile_per_pic_constraint_flag = get_bits1(gb);
        gci->pic_header_in_slice_header_constraint_flag = get_bits1(gb);
        gci->one_slice_per_pic_constraint_flag = get_bits1(gb);
        gci->no_rectangular_slice_constraint_flag = get_bits1(gb);
        gci->one_slice_per_subpic_constraint_flag = get_bits1(gb);
        gci->no_subpic_info_constraint_flag = get_bits1(gb);

        /* CTU and block partitioning */
        gci->three_minus_max_log2_ctu_size_constraint_idc = get_bits(gb, 2);
        gci->no_partition_constraints_override_constraint_flag = get_bits1(gb);
        gci->no_mtt_constraint_flag = get_bits1(gb);
        gci->no_qtbtt_dual_tree_intra_constraint_flag = get_bits1(gb);

        /* intra */
        gci->no_palette_constraint_flag = get_bits1(gb);
        gci->no_ibc_constraint_flag = get_bits1(gb);
        gci->no_isp_constraint_flag = get_bits1(gb);
        gci->no_mrl_constraint_flag = get_bits1(gb);
        gci->no_mip_constraint_flag = get_bits1(gb);
        gci->no_cclm_constraint_flag = get_bits1(gb);

        /* inter */
        gci->no_ref_pic_resampling_constraint_flag = get_bits1(gb);
        gci->no_res_change_in_clvs_constraint_flag = get_bits1(gb);
        gci->no_weighted_prediction_constraint_flag = get_bits1(gb);
        gci->no_ref_wraparound_constraint_flag = get_bits1(gb);
        gci->no_temporal_mvp_constraint_flag = get_bits1(gb);
        gci->no_sbtmvp_constraint_flag = get_bits1(gb);
        gci->no_amvr_constraint_flag = get_bits1(gb);
        gci->no_bdof_constraint_flag = get_bits1(gb);
        gci->no_smvd_constraint_flag = get_bits1(gb);
        gci->no_dmvr_constraint_flag = get_bits1(gb);
        gci->no_mmvd_constraint_flag = get_bits1(gb);
        gci->no_affine_motion_constraint_flag = get_bits1(gb);
        gci->no_prof_constraint_flag = get_bits1(gb);
        gci->no_bcw_constraint_flag = get_bits1(gb);
        gci->no_ciip_constraint_flag = get_bits1(gb);
        gci->no_gpm_constraint_flag = get_bits1(gb);

        /* transform, quantization, residual */
        gci->no_luma_transform_size_64_constraint_flag = get_bits1(gb);
        gci->no_transform_skip_constraint_flag = get_bits1(gb);
        gci->no_bdpcm_constraint_flag = get_bits1(gb);
        gci->no_mts_constraint_flag = get_bits1(gb);
        gci->no_lfnst_constraint_flag = get_bits1(gb);
        gci->no_joint_cbcr_constraint_flag = get_bits1(gb);
        gci->no_sbt_constraint_flag = get_bits1(gb);
        gci->no_act_constraint_flag = get_bits1(gb);
        gci->no_explicit_scaling_list_constraint_flag = get_bits1(gb);
        gci->no_dep_quant_constraint_flag = get_bits1(gb);
        gci->no_sign_data_hiding_constraint_flag = get_bits1(gb);
        gci->no_cu_qp_delta_constraint_flag = get_bits1(gb);
        gci->no_chroma_qp_offset_constraint_flag = get_bits1(gb);

        /* loop filter */
        gci->no_sao_constraint_flag = get_bits1(gb);
        gci->no_alf_constraint_flag = get_bits1(gb);
        gci->no_ccalf_constraint_flag = get_bits1(gb);
        gci->no_lmcs_constraint_flag = get_bits1(gb);
        gci->no_ladf_constraint_flag = get_bits1(gb);
        gci->no_virtual_boundaries_constraint_flag = get_bits1(gb);
        num_reserved_bits = get_bits(gb, 8);
        for (i = 0; i < num_reserved_bits; i++) {
            skip_bits1(gb);          //reserved_zero_bit[i]
        }
    }
    align_get_bits(gb);
    return 0;
}

static int ptl_parse(PTL *ptl, const int profile_tier_present_flag,
    const int max_num_sub_layers_minus1, GetBitContext *gb, void *log_ctx)
{
    int ret, i;

    if (profile_tier_present_flag) {
        ptl->general_profile_idc = get_bits(gb, 7);
        ptl->general_tier_flag = get_bits1(gb);
    }
    ptl->general_level_idc = get_bits(gb, 8);
    ptl->frame_only_constraint_flag = get_bits1(gb);
    ptl->multilayer_enabled_flag = get_bits1(gb);
    if (profile_tier_present_flag) {
        ret = gci_parse(&ptl->gci, gb, log_ctx);
        if (ret < 0)
            return ret;
    }

    for (i = max_num_sub_layers_minus1 - 1; i >= 0; i--)
        ptl->sublayer_level_present_flag[i] = get_bits1(gb);

    align_get_bits(gb);

    for (i = max_num_sub_layers_minus1 - 1; i >= 0; i--) {
        if (ptl->sublayer_level_present_flag[i])
            ptl->sublayer_level_idc[i] = get_bits(gb, 8);
    }

    if (profile_tier_present_flag) {
        ptl->num_sub_profiles = get_bits(gb, 8);
        for (i = 0; i < ptl->num_sub_profiles; i++)
            ptl->general_sub_profile_idc[i] = get_bits(gb, 32);
    }
    return 0;
}

static int map_pixel_format(VVCSPS *sps, void *log_ctx)
{
    const AVPixFmtDescriptor *desc;
    switch (sps->bit_depth) {
    case 8:
        if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY8;
        if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P;
        if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P;
        if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P;
       break;
    case 10:
        if (sps->chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY10;
        if (sps->chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P10;
        if (sps->chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P10;
        if (sps->chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P10;
        break;
    default:
        av_log(log_ctx, AV_LOG_ERROR,
               "The following bit-depths are currently specified: 8, 10 bits, "
               "chroma_format_idc is %d, depth is %d\n",
               sps->chroma_format_idc, sps->bit_depth);
        return AVERROR_INVALIDDATA;
    }

    desc = av_pix_fmt_desc_get(sps->pix_fmt);
    if (!desc)
        return AVERROR(EINVAL);

    sps->hshift[0] = sps->vshift[0] = 0;
    sps->hshift[2] = sps->hshift[1] = desc->log2_chroma_w;
    sps->vshift[2] = sps->vshift[1] = desc->log2_chroma_h;

    sps->pixel_shift = sps->bit_depth > 8;

    return 0;
}

static int sps_parse_pic_resampling(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    sps->ref_pic_resampling_enabled_flag = get_bits1(gb);
    if (sps->ref_pic_resampling_enabled_flag)
        sps->res_change_in_clvs_allowed_flag = get_bits1(gb);
    if (sps->res_change_in_clvs_allowed_flag) {
        avpriv_request_sample(log_ctx, "res_change_in_clvs_allowed_flag");
        return AVERROR_PATCHWELCOME;
    }

    return 0;
}

static void win_parse(VVCWindow* win, const VVCSPS *sps, GetBitContext *gb)
{
    win->left_offset   = get_ue_golomb_long(gb) << sps->hshift[1];
    win->right_offset  = get_ue_golomb_long(gb) << sps->hshift[1];
    win->top_offset    = get_ue_golomb_long(gb) << sps->vshift[1];
    win->bottom_offset = get_ue_golomb_long(gb) << sps->vshift[1];
}

static void sps_parse_conf_win(VVCSPS *sps, GetBitContext *gb, AVCodecContext *avctx)
{
    if (get_bits1(gb)) { // sps_conformance_window_flag
        win_parse(&sps->conf_win, sps, gb);

        if (avctx->flags2 & AV_CODEC_FLAG2_IGNORE_CROP) {
            av_log(avctx, AV_LOG_DEBUG,
                   "discarding sps conformance window, "
                   "original values are l:%u r:%u t:%u b:%u\n",
                   sps->conf_win.left_offset,
                   sps->conf_win.right_offset,
                   sps->conf_win.top_offset,
                   sps->conf_win.bottom_offset);

            sps->conf_win.left_offset   =
            sps->conf_win.right_offset  =
            sps->conf_win.top_offset    =
            sps->conf_win.bottom_offset = 0;
        }
        sps->output_window = sps->conf_win;
    }
}

static void pps_parse_conf_win(VVCPPS *pps, const VVCSPS *sps, GetBitContext *gb)
{
    if (get_bits1(gb)) { // pps_conformance_window_flag
        win_parse(&pps->conf_win, sps, gb);
    } else if (pps->width  == sps->width && pps->height == sps->height) {
        pps->conf_win = sps->conf_win;
    }
}

static void scaling_win_parse(VVCWindow* win, const VVCSPS *sps, GetBitContext *gb)
{
    win->left_offset   = get_se_golomb(gb) << sps->hshift[1];
    win->right_offset  = get_se_golomb(gb) << sps->hshift[1];
    win->top_offset    = get_se_golomb(gb) << sps->vshift[1];
    win->bottom_offset = get_se_golomb(gb) << sps->vshift[1];
}

static int pps_scaling_win_parse(VVCPPS *pps, const VVCSPS *sps,
    GetBitContext *gb, void *log_ctx)
{
    if (get_bits1(gb)) { //pps_scaling_window_explicit_signalling_flag
        if (!sps->ref_pic_resampling_enabled_flag) {
            av_log(log_ctx, AV_LOG_ERROR,
                "Invalid data: sps_ref_pic_resampling_enabled_flag is false, "
                "but pps_scaling_window_explicit_signalling_flag is true.\n");
            return AVERROR_INVALIDDATA;
        }
        scaling_win_parse(&pps->scaling_win, sps, gb);
     } else {
        pps->scaling_win = pps->conf_win;
    }
    return 0;
}

static int sps_parse_subpic(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int i;
    unsigned int ctb_size_y = sps->ctb_size_y;
    unsigned int tmp_width_val =
        vvc_ceil(sps->width, ctb_size_y);
    unsigned int tmp_height_val =
        vvc_ceil(sps->height, ctb_size_y);

    sps->subpic_info_present_flag = get_bits1(gb);
    if (sps->subpic_info_present_flag) {
        sps->num_subpics = get_ue_golomb_long(gb) + 1;
        if (sps->num_subpics > VVC_MAX_SLICES) {
            av_log(log_ctx, AV_LOG_ERROR, "num_subpics out of range: %d\n",
                   sps->num_subpics);
            return AVERROR_INVALIDDATA;
        }
        if (sps->num_subpics > 1) {
            const int wlen = av_ceil_log2(tmp_width_val);
            const int hlen = av_ceil_log2(tmp_height_val);
            int sps_subpic_same_size_flag;

            sps->independent_subpics_flag = get_bits1(gb);
            sps_subpic_same_size_flag = get_bits1(gb);
            if (sps->width > ctb_size_y)
                sps->subpic_width[0]  = get_bits(gb, wlen) + 1;
            else
                sps->subpic_width[0] = tmp_width_val;
            if (sps->height > ctb_size_y)
                sps->subpic_height[0] = get_bits(gb, hlen) + 1;
            else
                sps->subpic_height[0] = tmp_height_val;
            if (!sps->independent_subpics_flag) {
                sps->subpic_treated_as_pic_flag[0] = get_bits1(gb);
                sps->loop_filter_across_subpic_enabled_flag[0] = get_bits1(gb);
            } else {
                sps->subpic_treated_as_pic_flag[0] = 1;
                sps->loop_filter_across_subpic_enabled_flag[0] = 1;
            }
            for (i = 1; i < sps->num_subpics; i++) {
                if (!sps_subpic_same_size_flag) {
                    if (sps->width > ctb_size_y)
                        sps->subpic_ctu_top_left_x[i] = get_bits(gb, wlen);
                    if (sps->height > ctb_size_y)
                        sps->subpic_ctu_top_left_y[i] = get_bits(gb, hlen);
                    if (i < sps->num_subpics - 1 &&
                        sps->width > ctb_size_y) {
                        sps->subpic_width[i]  = get_bits(gb, wlen) + 1;
                    } else {
                        sps->subpic_width[i]  =
                            tmp_width_val - sps->subpic_ctu_top_left_x[i];
                    }
                    if (i < sps->num_subpics - 1 &&
                        sps->height > ctb_size_y) {
                        sps->subpic_height[i] = get_bits(gb, hlen) + 1;
                    } else   {
                        sps->subpic_height[i] =
                            tmp_height_val - sps->subpic_ctu_top_left_y[i];
                    }
                } else {
                    int num_subpic_cols =
                        tmp_width_val / sps->subpic_width[0];
                    sps->subpic_ctu_top_left_x[i] =
                        (i % num_subpic_cols) * sps->subpic_width[0];
                    sps->subpic_ctu_top_left_y[i] =
                        (i / num_subpic_cols) * sps->subpic_height[0];
                    sps->subpic_width[i]  = sps->subpic_width[0];
                    sps->subpic_height[i] = sps->subpic_height[0];
                }
                if (!sps->independent_subpics_flag) {
                    sps->subpic_treated_as_pic_flag[i] = get_bits1(gb);
                    sps->loop_filter_across_subpic_enabled_flag[i] = get_bits1(gb);
                } else {
                    sps->subpic_treated_as_pic_flag[i] = 1;
                }
            }
            sps->subpic_id_len = get_ue_golomb_long(gb) + 1;

            if (sps->subpic_id_len > 16U ||
                ((1 << sps->subpic_id_len) < sps->num_subpics)) {
                av_log(log_ctx, AV_LOG_ERROR,
                    "subpic_id_len(%d) is invalid\n",
                    sps->subpic_id_len);
                return AVERROR_INVALIDDATA;
            }
            sps->subpic_id_mapping_explicitly_signalled_flag = get_bits1(gb);
            if (sps->subpic_id_mapping_explicitly_signalled_flag) {
                sps->subpic_id_mapping_present_flag = get_bits1(gb);
                if (sps->subpic_id_mapping_present_flag) {
                    for (i = 0; i < sps->num_subpics; i++) {
                        sps->subpic_id[i] = get_bits(gb, sps->subpic_id_len);
                    }
                }
            }
        } else {
            sps->subpic_ctu_top_left_x[0] = 0;
            sps->subpic_ctu_top_left_y[0] = 0;
            sps->subpic_width[0] = tmp_width_val;
            sps->subpic_height[0] = tmp_height_val;
        }
    } else {
        sps->num_subpics = 1;
        sps->independent_subpics_flag = 1;
        sps->subpic_width[0] = tmp_width_val;
        sps->subpic_height[0] = tmp_height_val;
    }

    return 0;
}

static int sps_parse_bit_depth(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    sps->bit_depth = get_ue_golomb_long(gb) + 8;
    if (sps->bit_depth > 10) {
        avpriv_report_missing_feature(log_ctx, "%d bits", sps->bit_depth);
        return AVERROR_PATCHWELCOME;
    }

    sps->qp_bd_offset = 6 * (sps->bit_depth - 8);
    return map_pixel_format(sps, log_ctx);
}

static int sps_parse_poc(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    sps->log2_max_pic_order_cnt_lsb = get_bits(gb, 4) + 4;
    if (sps->log2_max_pic_order_cnt_lsb > 16U) {
        av_log(log_ctx, AV_LOG_ERROR,
               "log2_max_pic_order_cnt_lsb %d > 16 is invalid",
               sps->log2_max_pic_order_cnt_lsb);
        return AVERROR_INVALIDDATA;
    }
    sps->max_pic_order_cnt_lsb = 1 << sps->log2_max_pic_order_cnt_lsb;

    sps->poc_msb_cycle_flag = get_bits1(gb);
    if (sps->poc_msb_cycle_flag)
        uep(sps->poc_msb_cycle_len, 1, 1, 32 - sps->log2_max_pic_order_cnt_lsb);

    return 0;
}

static int sps_parse_extra_bytes(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    u(2, sps->num_extra_ph_bytes, 0, 2);
    for (int i = 0; i < (sps->num_extra_ph_bytes * 8); i++)
        sps->extra_ph_bit_present_flag[i] = get_bits1(gb);

    u(2, sps->num_extra_sh_bytes, 0, 2);
    for (int i = 0; i < (sps->num_extra_sh_bytes * 8); i++)
        sps->extra_sh_bit_present_flag[i] = get_bits1(gb);

    return 0;
}

static void dpb_parameters_parse(DpbParameters *dpb, const uint8_t max_sublayers_minus1,
    const uint8_t sublayer_info_flag, GetBitContext *gb)
{
    int i = (sublayer_info_flag ? 0 : max_sublayers_minus1);
    for (/* nothing */; i <= max_sublayers_minus1; i++) {
        dpb->max_dec_pic_buffering[i] = get_ue_golomb_long(gb) + 1;
        dpb->max_num_reorder_pics[i]  = get_ue_golomb_long(gb);
        dpb->max_latency_increase[i]  = get_ue_golomb_long(gb) - 1;
    }
}

static int partition_constraints_parse(PartitionConstraints *pc, const VVCSPS *sps,
    GetBitContext *gb, void *log_ctx)
{
    const int max = FFMIN(6, sps->ctb_log2_size_y);

    ue(pc->log2_diff_min_qt_min_cb, max - sps->min_cb_log2_size_y);
    ue(pc->max_mtt_hierarchy_depth, 2 * (sps->ctb_log2_size_y - sps->min_cb_log2_size_y));
    if (pc->max_mtt_hierarchy_depth) {
        const int min_qt_log2_size = pc->log2_diff_min_qt_min_cb + sps->min_cb_log2_size_y;
        ue(pc->log2_diff_max_bt_min_qt, sps->ctb_log2_size_y - min_qt_log2_size);
        ue(pc->log2_diff_max_tt_min_qt, FFMIN(6, sps->ctb_log2_size_y) - min_qt_log2_size);
    }

    return 0;
}

static int sps_parse_partition_constraints(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int ret = 0;

    sps->partition_constraints_override_enabled_flag = get_bits1(gb);

    ret = partition_constraints_parse(&sps->intra_slice_luma, sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    if (sps->chroma_format_idc != 0) {
        sps->qtbtt_dual_tree_intra_flag = get_bits1(gb);
        if (sps->qtbtt_dual_tree_intra_flag) {
            ret = partition_constraints_parse(&sps->intra_slice_chroma, sps, gb, log_ctx);
            if (ret < 0)
                return ret;
        }
    }

    ret = partition_constraints_parse(&sps->inter_slice, sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    return ret;
}

static int rpls_parse(VVCRefPicListStruct *rpls, GetBitContext *gb,
    const uint8_t list_idx, const uint8_t rpls_idx, const VVCSPS *sps, void *log_ctx)
{
    int i, j;

    rpls->num_ltrp_entries = 0;
    ue(rpls->num_ref_entries, VVC_MAX_REF_ENTRIES);
    if (sps->long_term_ref_pics_flag &&
        rpls_idx < sps->num_ref_pic_lists[list_idx] &&
        rpls->num_ref_entries > 0)
        rpls->ltrp_in_header_flag = get_bits1(gb);
    if (sps->long_term_ref_pics_flag &&
        rpls_idx == sps->num_ref_pic_lists[list_idx])
        rpls->ltrp_in_header_flag = 1;
    for (i = 0, j = 0; i < rpls->num_ref_entries; i++) {
        VVCRefPicListStructEntry *entry = &rpls->entries[i];
        if (sps->inter_layer_prediction_enabled_flag)
            entry->inter_layer_ref_pic_flag = get_bits1(gb);

        if (!entry->inter_layer_ref_pic_flag) {
            if (sps->long_term_ref_pics_flag)
                entry->st_ref_pic_flag = get_bits1(gb);
            else
                entry->st_ref_pic_flag = 1;
            if (entry->st_ref_pic_flag) {
                int abs_delta_poc_st, strp_entry_sign_flag = 0;
                ue(abs_delta_poc_st, (1<<15) - 1);
                if (!((sps->weighted_pred_flag ||
                    sps->weighted_bipred_flag) && i != 0))
                    abs_delta_poc_st++;
                if (abs_delta_poc_st > 0)
                    strp_entry_sign_flag = get_bits1(gb);
                entry->delta_poc_val_st = (1 - 2 * strp_entry_sign_flag) * abs_delta_poc_st;
            } else {
                if (!rpls->ltrp_in_header_flag) {
                    uint8_t bits = sps->log2_max_pic_order_cnt_lsb;
                    entry->lt_poc = get_bits(gb, bits);
                    j++;
                }
                rpls->num_ltrp_entries++;
            }
        } else {
            avpriv_request_sample(log_ctx, "Inter layer ref");
            return AVERROR_PATCHWELCOME;
        }
    }

    return 0;
}

static int sps_parse_chroma_qp_table(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    const int sps_same_qp_table_for_chroma_flag = get_bits1(gb);
    const int num_qp_tables = sps_same_qp_table_for_chroma_flag ?
        1 : (sps->joint_cbcr_enabled_flag ? 3 : 2);

    for (int i = 0; i < num_qp_tables; i++) {
        int qp_table_start_minus26, num_points_in_qp_table_minus1;
        int num_points_in_qp_table;
        int qp_in[VVC_MAX_POINTS_IN_QP_TABLE], qp_out[VVC_MAX_POINTS_IN_QP_TABLE];
        unsigned int delta_qp_in[VVC_MAX_POINTS_IN_QP_TABLE];
        int off = sps->qp_bd_offset;

        se(qp_table_start_minus26, -26 - sps->qp_bd_offset, 36);
        ue(num_points_in_qp_table_minus1, 36 - qp_table_start_minus26);
        num_points_in_qp_table = num_points_in_qp_table_minus1 + 1;

        qp_out[0] = qp_in[0] = qp_table_start_minus26 + 26;
        for (int j = 0; j < num_points_in_qp_table; j++ ) {
            const uint8_t max = 0xff;
            unsigned int delta_qp_diff_val, delta_qp_in_val_minus1;

            ue(delta_qp_in_val_minus1, max);
            ue(delta_qp_diff_val, max);
            delta_qp_in[j] = delta_qp_in_val_minus1 + 1;
            qp_in[j+1] = qp_in[j] + delta_qp_in[j];
            qp_out[j+1] = qp_out[j] + (delta_qp_in_val_minus1 ^ delta_qp_diff_val);
        }
        sps->chroma_qp_table[i][qp_in[0] + off] = qp_out[0];
        for (int k = qp_in[0] - 1 + off; k >= 0; k--)
            sps->chroma_qp_table[i][k] = av_clip(sps->chroma_qp_table[i][k+1]-1, -off, 63);

        for (int j  = 0; j < num_points_in_qp_table; j++) {
            int sh = delta_qp_in[j] >> 1;
            for (int k = qp_in[j] + 1 + off, m = 1; k <= qp_in[j+1] + off; k++, m++) {
                sps->chroma_qp_table[i][k] = sps->chroma_qp_table[i][qp_in[j] + off] +
                    ((qp_out[j+1] - qp_out[j]) * m + sh) / delta_qp_in[j];
            }
        }
        for (int k = qp_in[num_points_in_qp_table] + 1 + off; k <= 63 + off; k++)
            sps->chroma_qp_table[i][k]  = av_clip(sps->chroma_qp_table[i][k-1] + 1, -sps->qp_bd_offset, 63);
    }
    if (sps_same_qp_table_for_chroma_flag) {
        memcpy(&sps->chroma_qp_table[1], &sps->chroma_qp_table[0], sizeof(sps->chroma_qp_table[0]));
        memcpy(&sps->chroma_qp_table[2], &sps->chroma_qp_table[0], sizeof(sps->chroma_qp_table[0]));
    }

    return 0;
}

static int sps_parse_transform(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int ret = 0;

    sps->transform_skip_enabled_flag = get_bits1(gb);
    if (sps->transform_skip_enabled_flag) {
        sps->max_ts_size = 1 << (get_ue_golomb_long(gb) + 2);
        if (sps->max_ts_size > 32) {
            av_log(log_ctx, AV_LOG_ERROR,
               "sps_log2_transform_skip_max_size_minus2 > 3 is invalid");
            return AVERROR_INVALIDDATA;
        }
        sps->bdpcm_enabled_flag = get_bits1(gb);
    }

    sps->mts_enabled_flag = get_bits1(gb);
    if (sps->mts_enabled_flag) {
        sps->explicit_mts_intra_enabled_flag = get_bits1(gb);
        sps->explicit_mts_inter_enabled_flag = get_bits1(gb);
    }

    sps->lfnst_enabled_flag = get_bits1(gb);

    if (sps->chroma_format_idc != 0) {
        sps->joint_cbcr_enabled_flag = get_bits1(gb);
        ret = sps_parse_chroma_qp_table(sps, gb, log_ctx);
        if (ret < 0)
            return ret;
    }

    return ret;
}

static void sps_parse_filter(VVCSPS *sps, GetBitContext *gb)
{
    sps->sao_enabled_flag = get_bits1(gb);

    sps->alf_enabled_flag = get_bits1(gb);
    if (sps->alf_enabled_flag && sps->chroma_format_idc)
        sps->ccalf_enabled_flag = get_bits1(gb);

    sps->lmcs_enabled_flag = get_bits1(gb);
}

static int sps_parse_rpls(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int sps_rpl1_same_as_rpl0_flag;

    sps->idr_rpl_present_flag = get_bits1(gb);
    sps_rpl1_same_as_rpl0_flag = get_bits1(gb);
    for (int i = 0; i < (sps_rpl1_same_as_rpl0_flag ? 1 : 2); i++) {
        ue(sps->num_ref_pic_lists[i], VVC_MAX_REF_PIC_LISTS);
        for (int j = 0; j < sps->num_ref_pic_lists[i]; j++) {
            int ret = rpls_parse(&sps->ref_pic_list_struct[i][j], gb, i, j, sps, log_ctx);
            if (ret < 0)
                return ret;
        }
    }

    if (sps_rpl1_same_as_rpl0_flag) {
        sps->num_ref_pic_lists[1] = sps->num_ref_pic_lists[0];
        for (int j = 0; j < sps->num_ref_pic_lists[0]; j++)
            memcpy(&sps->ref_pic_list_struct[1][j],
                   &sps->ref_pic_list_struct[0][j],
                   sizeof(sps->ref_pic_list_struct[0][j]));
    }

    return 0;
}

static int sps_parse_max_num_gpm_merge_cand(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    sps->max_num_gpm_merge_cand = 0;
    if (sps->max_num_merge_cand >= 2) {
        sps->gpm_enabled_flag = get_bits1(gb);
        if (sps->gpm_enabled_flag) {
            sps->max_num_gpm_merge_cand = 2;
            if (sps->max_num_merge_cand >= 3) {
                uint8_t sps_max_num_merge_cand_minus_max_num_gpm_cand;
                ue(sps_max_num_merge_cand_minus_max_num_gpm_cand, sps->max_num_merge_cand - 2);
                sps->max_num_gpm_merge_cand = sps->max_num_merge_cand - sps_max_num_merge_cand_minus_max_num_gpm_cand;
            }
        }
    }

    return 0;
}

static int sps_parse_inter(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int ret, sps_six_minus_max_num_merge_cand;

    sps->weighted_pred_flag = get_bits1(gb);
    sps->weighted_bipred_flag = get_bits1(gb);
    sps->long_term_ref_pics_flag = get_bits1(gb);
    if (sps->video_parameter_set_id > 0)
        sps->inter_layer_prediction_enabled_flag = get_bits1(gb);

    ret = sps_parse_rpls(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    sps->ref_wraparound_enabled_flag = get_bits1(gb);

    sps->temporal_mvp_enabled_flag = get_bits1(gb);
    if (sps->temporal_mvp_enabled_flag)
        sps->sbtmvp_enabled_flag = get_bits1(gb);

    sps->amvr_enabled_flag = get_bits1(gb);
    sps->bdof_enabled_flag = get_bits1(gb);
    if (sps->bdof_enabled_flag)
        sps->bdof_control_present_in_ph_flag = get_bits1(gb);

    sps->smvd_enabled_flag = get_bits1(gb);
    sps->dmvr_enabled_flag = get_bits1(gb);
    if (sps->dmvr_enabled_flag)
        sps->dmvr_control_present_in_ph_flag = get_bits1(gb);

    sps->mmvd_enabled_flag = get_bits1(gb);
    if (sps->mmvd_enabled_flag)
        sps->mmvd_fullpel_only_enabled_flag = get_bits1(gb);

    ue(sps_six_minus_max_num_merge_cand, 5);
    sps->max_num_merge_cand = 6 - sps_six_minus_max_num_merge_cand;

    sps->sbt_enabled_flag = get_bits1(gb);

    sps->affine_enabled_flag = get_bits1(gb);
    if (sps->affine_enabled_flag) {
        ue(sps->five_minus_max_num_subblock_merge_cand,
           5 - sps->sbtmvp_enabled_flag);
        sps->six_param_affine_enabled_flag = get_bits1(gb);
        if (sps->amvr_enabled_flag)
            sps->affine_amvr_enabled_flag = get_bits1(gb);
        sps->affine_prof_enabled_flag = get_bits1(gb);
        if (sps->affine_prof_enabled_flag)
            sps->prof_control_present_in_ph_flag = get_bits1(gb);
    }

    sps->bcw_enabled_flag = get_bits1(gb);
    sps->ciip_enabled_flag = get_bits1(gb);

    ret = sps_parse_max_num_gpm_merge_cand(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    uep(sps->log2_parallel_merge_level, 2, 2, sps->ctb_log2_size_y);

    return 0;
}

static int sps_parse_intra(VVCSPS *sps, const int sps_max_luma_transform_size_64_flag,
    GetBitContext *gb, void *log_ctx)
{
    sps->isp_enabled_flag = get_bits1(gb);
    sps->mrl_enabled_flag = get_bits1(gb);
    sps->mip_enabled_flag = get_bits1(gb);

    if (sps->chroma_format_idc != 0)
        sps->cclm_enabled_flag = get_bits1(gb);
    if (sps->chroma_format_idc == 1) {
        sps->chroma_horizontal_collocated_flag = get_bits1(gb);
        sps->chroma_vertical_collocated_flag = get_bits1(gb);
    } else {
        sps->chroma_horizontal_collocated_flag = 1;
        sps->chroma_vertical_collocated_flag = 1;
    }

    sps->palette_enabled_flag = get_bits1(gb);
    if (sps->chroma_format_idc == 3 &&
        !sps_max_luma_transform_size_64_flag)
        sps->act_enabled_flag = get_bits1(gb);
    if (sps->transform_skip_enabled_flag || sps->palette_enabled_flag)
        ue(sps->min_qp_prime_ts, 8);

    sps->ibc_enabled_flag = get_bits1(gb);
    if (sps->ibc_enabled_flag) {
        uint8_t six_minus_max_num_ibc_merge_cand;
        ue(six_minus_max_num_ibc_merge_cand, 5);
        sps->max_num_ibc_merge_cand = 6 - six_minus_max_num_ibc_merge_cand;
    }

    return 0;
}

static int sps_parse_ladf(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    sps->ladf_enabled_flag = get_bits1(gb);
    if (sps->ladf_enabled_flag) {
        sps->num_ladf_intervals = get_bits(gb, 2) + 2;
        se(sps->ladf_lowest_interval_qp_offset, -63, 63);

        sps->ladf_interval_lower_bound[0] = 0;
        for (int i = 0; i < sps->num_ladf_intervals - 1; i++) {
            int sps_ladf_delta_threshold_minus1;

            se(sps->ladf_qp_offset[i], -63, 63);

            ue(sps_ladf_delta_threshold_minus1, (2 << sps->bit_depth) - 3);
            sps->ladf_interval_lower_bound[i + 1] = sps->ladf_interval_lower_bound[i] + sps_ladf_delta_threshold_minus1 + 1;
        }
    }

    return 0;
}

static void sps_parse_dequant(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    sps->explicit_scaling_list_enabled_flag = get_bits1(gb);
    if (sps->lfnst_enabled_flag && sps->explicit_scaling_list_enabled_flag)
        sps->scaling_matrix_for_lfnst_disabled_flag = get_bits1(gb);

    if (sps->act_enabled_flag && sps->explicit_scaling_list_enabled_flag)
        sps->scaling_matrix_for_alternative_colour_space_disabled_flag = get_bits1(gb);

    if (sps->scaling_matrix_for_alternative_colour_space_disabled_flag)
        sps->scaling_matrix_designated_colour_space_flag = get_bits1(gb);

    sps->dep_quant_enabled_flag = get_bits1(gb);
    sps->sign_data_hiding_enabled_flag = get_bits1(gb);
}

static int virtual_boundaries_parse(VirtualBoundaries *vbs, const int width, const int height,
    GetBitContext *gb, void *log_ctx)
{
    vbs->virtual_boundaries_present_flag = get_bits1(gb);
    if (vbs->virtual_boundaries_present_flag) {
        ue(vbs->num_ver_virtual_boundaries, width <= 8 ? 0 : 3);
        for (int i = 0; i < vbs->num_ver_virtual_boundaries; i++)
            ue(vbs->virtual_boundary_pos_x_minus1[i], (width + 7) / 8 - 2);
        ue(vbs->num_hor_virtual_boundaries, height <= 8 ? 0 : 3);
        for (int i = 0; i < vbs->num_hor_virtual_boundaries; i++)
            ue(vbs->virtual_boundary_pos_y_minus1[i], (height + 7) / 8 - 2);
    }

    return 0;
}

static int sps_parse_virtual_boundaries(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    sps->virtual_boundaries_enabled_flag = get_bits1(gb);
    if (sps->virtual_boundaries_enabled_flag) {
        const int ret = virtual_boundaries_parse(&sps->vbs, sps->width, sps->height, gb, log_ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

// 7.3.5.1 General timing and HRD parameters syntax
static int general_timing_hrd_parameters_parse(GeneralTimingHrdParameters *hrd, GetBitContext *gb, void *log_ctx)
{
    skip_bits_long(gb, 32);     ///< num_units_in_tick
    skip_bits_long(gb, 32);     ///< time_scale
    hrd->general_nal_hrd_params_present_flag = get_bits1(gb);
    hrd->general_vcl_hrd_params_present_flag = get_bits1(gb);
    if (hrd->general_nal_hrd_params_present_flag || hrd->general_vcl_hrd_params_present_flag) {
        skip_bits1(gb);         ///< general_same_pic_timing_in_all_ols_flag
        hrd->general_du_hrd_params_present_flag             = get_bits1(gb);
        if (hrd->general_du_hrd_params_present_flag)
            skip_bits(gb, 8);   ///< tick_divisor_minus2
        skip_bits(gb, 4);       ///< bit_rate_scale
        skip_bits(gb, 4);       ///< cpb_size_scale
        if (hrd->general_du_hrd_params_present_flag)
            skip_bits(gb, 4);   ///< cpb_size_du_scale
        uep(hrd->hrd_cpb_cnt, 1, 1, 32);
    }
    return 0;
}

static int ols_timing_hrd_parameters_parse(const GeneralTimingHrdParameters *hrd,
    const uint8_t first_sublayer, const uint8_t last_sublayer, GetBitContext *gb, void *log_ctx)
{
    const int hrd_params_present_flag = hrd->general_nal_hrd_params_present_flag ||
            hrd->general_vcl_hrd_params_present_flag;
    int elemental_duration_in_tc_minus1;
    for (uint8_t i = first_sublayer; i < last_sublayer; i++) {
        int fixed_pic_rate_within_cvs_flag = 0;
        if (!get_bits1(gb))         ///< fixed_pic_rate_general_flag
            fixed_pic_rate_within_cvs_flag = get_bits1(gb);
        if (fixed_pic_rate_within_cvs_flag)
            ue(elemental_duration_in_tc_minus1, 2047);
        else if (hrd_params_present_flag && hrd->hrd_cpb_cnt == 1)
            skip_bits1(gb);          ///< low_delay_hrd_flag

        if (hrd_params_present_flag) {
            int bit_rate_value_minus1, cpb_size_value_minus1;
            int cpb_size_du_value_minus1, bit_rate_du_value_minus1;
            for (int j = 0; j < hrd->hrd_cpb_cnt; j++) {
                ue(bit_rate_value_minus1, UINT32_MAX - 1);
                ue(cpb_size_value_minus1, UINT32_MAX - 1);
                if (hrd->general_du_hrd_params_present_flag) {
                    ue(cpb_size_du_value_minus1, UINT32_MAX - 1);
                    ue(bit_rate_du_value_minus1, UINT32_MAX - 1);
                }
                skip_bits1(gb);     ///< cbr_flag
            }
        }
    }
    return 0;
}

static int sps_parse_hrd(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int ret;
    sps->timing_hrd_params_present_flag = get_bits1(gb);
    if (sps->timing_hrd_params_present_flag) {
        uint8_t first_sublayer = 0 ;
        GeneralTimingHrdParameters *hrd = &sps->general_timing_hrd_parameters;
        ret = general_timing_hrd_parameters_parse(hrd, gb, log_ctx);
        if (ret < 0)
            return ret;
        if (sps->max_sublayers > 1 && get_bits1(gb))        ///< sps_sublayer_cpb_params_present_flag
            first_sublayer = sps->max_sublayers - 1;
        ret = ols_timing_hrd_parameters_parse(hrd, first_sublayer, sps->max_sublayers, gb, log_ctx);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int sps_parse_vui(VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    VUI *vui = &sps->vui;

    if (get_bits1(gb)) {         //vui_parameters_present_flag
        uep(vui->payload_size, 1, 1, 1024);
        align_get_bits(gb);
        //fixme
    } else {
        vui->colour_primaries = 2;
        vui->transfer_characteristics = 2;
        vui->matrix_coeffs =  2;
        vui->chroma_sample_loc_type_frame = 6;
        vui->chroma_sample_loc_type_top_field = 6;
        vui->chroma_sample_loc_type_bottom_field = 6;
    }

    return 0;
}

static int sps_parse(VVCSPS *sps, GetBitContext *gb, unsigned int *sps_id,
    int apply_defdispwin, AVBufferRef **vps_list, AVCodecContext *avctx, int nuh_layer_id)
{
    int sps_ptl_dpb_hrd_params_present_flag;
    int ret = 0, sps_max_luma_transform_size_64_flag = 0;
    const VVCVPS *vps;
    void *log_ctx = avctx;

    // Coded parameters

    *sps_id = get_bits(gb, 4);

    sps->video_parameter_set_id = get_bits(gb, 4);
    if (vps_list && !vps_list[sps->video_parameter_set_id]) {
        if (!sps->video_parameter_set_id) {
            VVCVPS *vps0;
            AVBufferRef* buf = av_buffer_allocz(sizeof(*vps));
            if (!buf)
                return AVERROR(ENOMEM);
            vps_list[0] = buf;
            vps0 = (VVCVPS *)buf->data;
            vps0->max_layers = 1;
            vps0->independent_layer_flag[0] = 1;
            vps0->layer_id[0] = nuh_layer_id;
        } else {
            av_log(log_ctx, AV_LOG_ERROR, "VPS %d does not exist\n",
                sps->video_parameter_set_id);
            return AVERROR_INVALIDDATA;
        }
    }
    vps = (VVCVPS*)vps_list[sps->video_parameter_set_id]->data;

    sps->max_sublayers = get_bits(gb, 3) + 1;
    if (sps->max_sublayers > VVC_MAX_SUBLAYERS) {
        av_log(log_ctx, AV_LOG_ERROR, "sps_max_sublayers out of range: %d\n",
               sps->max_sublayers);
        return AVERROR_INVALIDDATA;
    }

    sps->chroma_format_idc = get_bits(gb, 2);

    sps->ctb_log2_size_y  = get_bits(gb, 2) + 5;
    if (sps->ctb_log2_size_y == 8) {
        av_log(log_ctx, AV_LOG_ERROR, "sps_log2_ctu_size_minus5 can't be 3\n");
        return AVERROR_INVALIDDATA;
    }
    sps->ctb_size_y = 1 << sps->ctb_log2_size_y;

    sps_ptl_dpb_hrd_params_present_flag = get_bits1(gb);
    if (sps_ptl_dpb_hrd_params_present_flag) {
        ret = ptl_parse(&sps->ptl, 1, sps->max_sublayers - 1, gb, log_ctx);
        if (ret < 0)
            return ret;
    }
    skip_bits1(gb);           ///< sps_gdr_enabled_flag

    ret = sps_parse_pic_resampling(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    ue(sps->width, VVC_MAX_WIDTH);
    ue(sps->height, VVC_MAX_HEIGHT);
    if (!sps->width || !sps->height)
        return AVERROR_INVALIDDATA;

    sps_parse_conf_win(sps, gb, avctx);

    ret = sps_parse_subpic(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = sps_parse_bit_depth(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    sps->entropy_coding_sync_enabled_flag = get_bits1(gb);
    sps->entry_point_offsets_present_flag = get_bits1(gb);

    ret = sps_parse_poc(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = sps_parse_extra_bytes(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    if (sps_ptl_dpb_hrd_params_present_flag) {
        const int sps_sublayer_dpb_params_flag = sps->max_sublayers > 1 ? get_bits1(gb) : 0;
        dpb_parameters_parse(&sps->dpb, sps->max_sublayers - 1, sps_sublayer_dpb_params_flag, gb);
    }

    uep(sps->min_cb_log2_size_y, 2, 2, FFMIN(6, sps->ctb_log2_size_y));
    sps->min_cb_size_y = 1 << sps->min_cb_log2_size_y;

    ret = sps_parse_partition_constraints(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    if (sps->ctb_size_y > 32)
        sps_max_luma_transform_size_64_flag = get_bits1(gb);
    sps->max_tb_size_y = 1 << (sps_max_luma_transform_size_64_flag ? 6 : 5);

    ret = sps_parse_transform(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    sps_parse_filter(sps, gb);

    ret = sps_parse_inter(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = sps_parse_intra(sps, sps_max_luma_transform_size_64_flag, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = sps_parse_ladf(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    sps_parse_dequant(sps, gb, log_ctx);

    ret = sps_parse_virtual_boundaries(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    if (sps_ptl_dpb_hrd_params_present_flag) {
        ret = sps_parse_hrd(sps, gb, log_ctx);
        if (ret < 0)
            return ret;
    }

    sps->field_seq_flag = get_bits1(gb);

    ret = sps_parse_vui(sps, gb, log_ctx);
    if (ret < 0)
        return ret;

    skip_bits1(gb);                 ///< sps_extension_flag

    if (get_bits_left(gb) < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Overread SPS by %d bits\n", -get_bits_left(gb));
        return AVERROR_INVALIDDATA;
    }

    return 0;
}

int ff_vvc_decode_sps(VVCParamSets *ps, GetBitContext *gb,
    int apply_defdispwin, int nuh_layer_id, AVCodecContext *avctx)
{
    VVCSPS *sps;
    AVBufferRef *sps_buf = av_buffer_allocz(sizeof(*sps));
    unsigned int sps_id;
    int ret;
    ptrdiff_t nal_size;

    if (!sps_buf)
        return AVERROR(ENOMEM);
    sps = (VVCSPS*)sps_buf->data;

    av_log(avctx, AV_LOG_DEBUG, "Decoding SPS\n");

    nal_size = gb->buffer_end - gb->buffer;
    if (nal_size > sizeof(sps->data)) {
        av_log(avctx, AV_LOG_WARNING, "Truncating likely oversized SPS "
               "(%"PTRDIFF_SPECIFIER" > %"SIZE_SPECIFIER")\n",
               nal_size, sizeof(sps->data));
        sps->data_size = sizeof(sps->data);
    } else {
        sps->data_size = nal_size;
    }
    memcpy(sps->data, gb->buffer, sps->data_size);

    ret = sps_parse(sps, gb, &sps_id, apply_defdispwin, ps->vps_list, avctx, nuh_layer_id);
    if (ret < 0) {
        av_buffer_unref(&sps_buf);
        return ret;
    }

    if (avctx->debug & FF_DEBUG_BITSTREAM) {
        av_log(avctx, AV_LOG_DEBUG,
               "Parsed SPS: id %d; coded wxh: %dx%d; "
               "cropped wxh: %dx%d; pix_fmt: %s.\n",
               sps_id, sps->width, sps->height,
               sps->width - (sps->output_window.left_offset + sps->output_window.right_offset),
               sps->height - (sps->output_window.top_offset + sps->output_window.bottom_offset),
               av_get_pix_fmt_name(sps->pix_fmt));
    }

    /* check if this is a repeat of an already parsed SPS, then keep the
     * original one.
     * otherwise drop all PPSes that depend on it */
    if (ps->sps_list[sps_id] &&
        !memcmp(ps->sps_list[sps_id]->data, sps_buf->data, sps_buf->size)) {
        av_buffer_unref(&sps_buf);
    } else {
        remove_sps(ps, sps_id);
        ps->sps_list[sps_id] = sps_buf;
    }
    return 0;
}

static void vvc_pps_free(void *opaque, uint8_t *data)
{
    VVCPPS *pps = (VVCPPS *)data;

    av_freep(&pps->column_width);
    av_freep(&pps->row_height);
    av_freep(&pps->col_bd);
    av_freep(&pps->row_bd);
    av_freep(&pps->ctb_to_col_bd);
    av_freep(&pps->ctb_to_row_bd);
    av_freep(&pps->ctb_addr_in_slice);
    av_freep(&pps);
}

static int pps_parse_width_height(VVCPPS *pps, const VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    const int divisor = FFMAX(sps->min_cb_size_y, 8);

    ue(pps->width,  sps->width);
    ue(pps->height, sps->height);

    if (!pps->width || !pps->height)
        return AVERROR_INVALIDDATA;

    if (pps->width % divisor || pps->height % divisor) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Invalid dimensions: %ux%u not divisible "
               "by %u, MinCbSizeY = %u.\n", pps->width,
               pps->height, divisor, sps->min_cb_size_y);
        return AVERROR_INVALIDDATA;
    }
    if (!sps->res_change_in_clvs_allowed_flag &&
        (pps->width != sps->width || pps->height != sps->height)) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Resoltuion change is not allowed, "
               "in max resolution (%ux%u) mismatched with pps(%ux%u).\n",
               sps->width, sps->height,
               pps->width, pps->height);
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static unsigned int* pps_parse_tile_sizes(uint16_t *num, unsigned int num_exp,
    unsigned int max_num, unsigned int max_size, GetBitContext *gb, void *log_ctx)
{
    unsigned int i, exp_tile_size = 0;
    unsigned int unified_size, remaining_size;
    unsigned int *sizes, *p;
    sizes = av_malloc_array(num_exp, sizeof(*sizes));
    if (!sizes)
        return NULL;
    for (i = 0; i < num_exp; i++) {
        sizes[i] = get_ue_golomb_long(gb) + 1;
        if (exp_tile_size + sizes[i] > max_size) {
            goto err;
        }
        exp_tile_size += sizes[i];
    }
    remaining_size = max_size - exp_tile_size;
    unified_size = (i == 0 ? max_size : sizes[i-1]);
    *num = i + vvc_ceil(remaining_size, unified_size);
    if (*num > max_num) {
        av_log(log_ctx, AV_LOG_ERROR, "num(%d) large than %d.\n", *num, max_num);
        goto err;
    }
    p = av_realloc_array(sizes, *num, sizeof(*sizes));
    if (!p)
        goto err;
    sizes = p;
    while (remaining_size > unified_size) {
        sizes[i] = unified_size;
        remaining_size -= unified_size;
        i++;
    }
    if (remaining_size > 0) {
        sizes[i] = remaining_size;
    }
    return sizes;
err:
    av_free(sizes);
    return NULL;
}

static inline unsigned int* pps_setup_bd(unsigned int *sizes, int num_sizes)
{
    unsigned int *bd = av_malloc_array(num_sizes + 1, sizeof(*bd));
    if (!bd)
        return NULL;
    *bd= 0;
    for (int i = 0; i < num_sizes; i++) {
        bd[i+1] = bd[i] + sizes[i];
    }
    return bd;
}

static int pps_setup(VVCPPS *pps, const VVCSPS *sps)
{
    int ret = AVERROR(ENOMEM), tile_x = 0, tile_y = 0;

    pps->ctb_to_col_bd = av_calloc(pps->ctb_width  + 1, sizeof(*pps->ctb_to_col_bd));
    pps->ctb_to_row_bd = av_calloc(pps->ctb_height + 1, sizeof(*pps->ctb_to_col_bd));
    if (!pps->ctb_to_col_bd || !pps->ctb_to_row_bd)
        goto err;

    for (int ctb_addr_x = 0; ctb_addr_x < pps->ctb_width; ctb_addr_x++) {
        if (ctb_addr_x == pps->col_bd[tile_x + 1])
            tile_x++;
        pps->ctb_to_col_bd[ctb_addr_x] = pps->col_bd[tile_x];
    }
    pps->ctb_to_col_bd[pps->ctb_width] = pps->ctb_width;

    for (int ctb_addr_y = 0; ctb_addr_y < pps->ctb_height; ctb_addr_y++) {
        if (ctb_addr_y == pps->row_bd[tile_y + 1])
            tile_y++;
        pps->ctb_to_row_bd[ctb_addr_y] = pps->row_bd[tile_y];
    }
    pps->ctb_to_row_bd[pps->ctb_height] = pps->ctb_height;

     return 0;
err:
    return ret;
}

static int pps_parse_subpic_id(VVCPPS *pps, const VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int pps_subpic_id_mapping_present_flag;
    uint8_t subpic_id_len;

    pps->no_pic_partition_flag = get_bits1(gb);
    if (sps->num_subpics > 1 || pps->mixed_nalu_types_in_pic_flag)  {
        if (pps->no_pic_partition_flag) {
            av_log(log_ctx, AV_LOG_ERROR,
                   "no_pic_partition_flag should not true");
            return AVERROR_INVALIDDATA;
        }
    }

    pps_subpic_id_mapping_present_flag = get_bits1(gb);
    if (pps_subpic_id_mapping_present_flag) {
        if (!pps->no_pic_partition_flag) {
            const uint16_t num_subpics = get_ue_golomb_long(gb) + 1;
            if (num_subpics != sps->num_subpics) {
                av_log(log_ctx, AV_LOG_ERROR,
                    "pps_num_subpics %u should equals to "
                    "sps_num_subpics %u .\n", num_subpics, sps->num_subpics);
                return AVERROR_INVALIDDATA;
            }
        }

        subpic_id_len = get_ue_golomb_long(gb) + 1;
        if (subpic_id_len != sps->subpic_id_len) {
            av_log(log_ctx, AV_LOG_ERROR,
                "pps_subpic_id_len %u should equals to "
                "sps_subpic_id_len %u .\n", subpic_id_len, sps->subpic_id_len);
            return AVERROR_INVALIDDATA;
        }

    }
    for (int i = 0; i < sps->num_subpics; i++) {
        if (sps->subpic_id_mapping_explicitly_signalled_flag) {
            if (pps_subpic_id_mapping_present_flag)
                pps->subpic_id[i] = get_bits(gb, sps->subpic_id_len);
            else
                pps->subpic_id[i] = sps->subpic_id[i];
        } else {
            pps->subpic_id[i] = i;
        }
    }
    return 0;
}

static int pps_parse_chroma_tool_offsets(VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    uint8_t pps_joint_cbcr_qp_offset_present_flag;

    pps->chroma_tool_offsets_present_flag = get_bits1(gb);
    if (!pps->chroma_tool_offsets_present_flag)
        return 0;

    se(pps->chroma_qp_offset[CB - 1], -12, 12);
    se(pps->chroma_qp_offset[CR - 1], -12, 12);

    pps_joint_cbcr_qp_offset_present_flag = get_bits1(gb);
    if (pps_joint_cbcr_qp_offset_present_flag)
        se(pps->chroma_qp_offset[JCBCR - 1], -12, 12);
    pps->slice_chroma_qp_offsets_present_flag = get_bits1(gb);
    pps->cu_chroma_qp_offset_list_enabled_flag = get_bits1(gb);
    if (pps->cu_chroma_qp_offset_list_enabled_flag) {
        ue(pps->chroma_qp_offset_list_len_minus1, 5);
        for (int i = 0; i <= pps->chroma_qp_offset_list_len_minus1; i++) {
            for (int j = CB - 1; j < CR + pps_joint_cbcr_qp_offset_present_flag; j++)
                se(pps->chroma_qp_offset_list[i][j], -12, 12);
        }
    }
    return 0;
}

static int deblock_parse(DBParams *deblock, const VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    int beta_offset_div2, tc_offset_div2;
    for (int c_idx = 0; c_idx < VVC_MAX_SAMPLE_ARRAYS; c_idx++) {
        if (!c_idx || pps->chroma_tool_offsets_present_flag) {
            se(beta_offset_div2, -12, 12);
            se(tc_offset_div2, -12, 12);
        }
        deblock->beta_offset[c_idx] = beta_offset_div2 << 1;
        deblock->tc_offset[c_idx] = tc_offset_div2 << 1;
    }
    return 0;
}
static int pps_parse_deblocking_control(VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    if (get_bits1(gb)) { //pps_deblocking_filter_control_present_flag
        pps->deblocking_filter_override_enabled_flag = get_bits1(gb);
        pps->deblocking_filter_disabled_flag = get_bits1(gb);
        if (!pps->no_pic_partition_flag &&
            pps->deblocking_filter_override_enabled_flag)
            pps->dbf_info_in_ph_flag = get_bits1(gb);
        if (!pps->deblocking_filter_disabled_flag)
            return deblock_parse(&pps->deblock, pps, gb, log_ctx);
    }
    return 0;
}

static int pps_parse_num_tiles_in_pic(VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    unsigned int num_exp_tile_columns, num_exp_tile_rows;

    uep(num_exp_tile_columns, 1, 1,
        FFMIN(pps->ctb_width, VVC_MAX_TILE_COLUMNS));
    uep(num_exp_tile_rows, 1, 1,
       FFMIN(pps->ctb_height, VVC_MAX_TILE_ROWS));

    pps->column_width = pps_parse_tile_sizes(&pps->num_tile_columns,
        num_exp_tile_columns, VVC_MAX_TILE_COLUMNS, pps->ctb_width,
        gb, log_ctx);
    if (!pps->column_width)
        return AVERROR(ENOMEM);

    pps->row_height = pps_parse_tile_sizes(&pps->num_tile_rows,
        num_exp_tile_rows, VVC_MAX_TILE_ROWS, pps->ctb_height,
        gb, log_ctx);
    if (!pps->row_height)
        return AVERROR(ENOMEM);

    pps->num_tiles_in_pic =
        pps->num_tile_columns * pps->num_tile_rows;
    if (pps->num_tiles_in_pic > VVC_MAX_TILES_PER_AU) {
        av_log(log_ctx, AV_LOG_ERROR,
               "NumTilesInPic(%d) large than %d.\n",
               pps->num_tiles_in_pic, VVC_MAX_TILES_PER_AU);
        return AVERROR_INVALIDDATA;
    }

    pps->col_bd = pps_setup_bd(pps->column_width, pps->num_tile_columns);
    pps->row_bd = pps_setup_bd(pps->row_height, pps->num_tile_rows);
    if (!pps->col_bd || !pps->row_bd)
        return AVERROR(ENOMEM);

    return 0;
}

static int pps_parse_slice_width_height_in_tiles(VVCPPS *pps, const int i,
    const int tile_x, const int tile_y, const int pps_tile_idx_delta_present_flag,
    GetBitContext *gb, void *log_ctx,
    int *slice_width_in_tiles, int *slice_height_in_tiles)
{
    *slice_width_in_tiles = 1;
    if (tile_x != pps->num_tile_columns - 1) {
        uep(*slice_width_in_tiles, 1, 1, pps->num_tile_columns);
    }
    if (tile_y != pps->num_tile_rows - 1 &&
        (pps_tile_idx_delta_present_flag || tile_x == 0)) {
        uep(*slice_height_in_tiles, 1, 1, pps->num_tile_rows);
    } else {
        if (tile_y == pps->num_tile_rows - 1)
            *slice_height_in_tiles = 1;
    }
    return 0;
}

static void pps_derive_tile_xy(const VVCPPS *pps, const int tile_idx,
    int *tile_x, int *tile_y)
{
    *tile_x = tile_idx % pps->num_tile_columns;
    *tile_y = tile_idx / pps->num_tile_columns;
}

static int pps_parse_tile_idx(const VVCPPS *pps, const int slice_idx, int tile_idx,
    const int slice_width_in_tiles, const int slice_height_in_tiles, const int pps_tile_idx_delta_present_flag,
    GetBitContext *gb)
{
    if (pps_tile_idx_delta_present_flag) {
        tile_idx += get_se_golomb(gb);              //< pps_tile_idx_delta_val
    } else {
        tile_idx += slice_width_in_tiles;
        if (tile_idx % pps->num_tile_columns == 0)
            tile_idx += (slice_height_in_tiles - 1) * pps->num_tile_columns;
    }
    return tile_idx;
}

typedef struct SliceMap {
    uint16_t start_offset;
    const VVCSPS   *sps;
    VVCPPS   *pps;
    uint16_t top_left_ctu_x[VVC_MAX_SLICES];
    uint16_t top_left_ctu_y[VVC_MAX_SLICES];
} SliceMap;

static void slice_map_init(SliceMap *slice_map, const VVCSPS *sps, VVCPPS *pps)
{
    slice_map->start_offset = 0;
    slice_map->pps = pps;
    slice_map->sps = sps;
}

static void slice_map_add_new_slice(SliceMap *slice_map, const int slice_idx, const int ctu_x, const int ctu_y, const int height, const int ctb_count)
{
    VVCPPS *pps = slice_map->pps;
    pps->slice_start_offset[slice_idx] = slice_map->start_offset;

    slice_map->top_left_ctu_x[slice_idx] = ctu_x;
    slice_map->top_left_ctu_y[slice_idx] = ctu_y;
    pps->num_ctus_in_slice[slice_idx]    = ctb_count;
    slice_map->start_offset             += ctb_count;
}

static int slice_map_add_ctus(SliceMap *slice_map, const int slice_idx, const int ctu_x, const int ctu_y, const int width, const int height)
{
    VVCPPS *pps = slice_map->pps;
    const int x_end = ctu_x + width;
    const int y_end = ctu_y + height;
    int ctb_count = 0;

    for (int y = ctu_y; y < y_end; y++) {
        for (int x = ctu_x; x < x_end; x++) {
            const uint16_t ctb_addr_in_rs = y * pps->ctb_width + x;         //< CtbAddrInRs
            const int offset = slice_map->start_offset + ctb_count;
            pps->ctb_addr_in_slice[offset] = ctb_addr_in_rs;
            ctb_count++;
        }
    }
    slice_map_add_new_slice(slice_map, slice_idx, ctu_x, ctu_y, height, ctb_count);
    return slice_map->sps->entropy_coding_sync_enabled_flag ? height - 1 : 0;
}

static void slice_map_add_tiles(SliceMap *slice_map, const int slice_idx,
    const int tile_x_start, const int tile_y_start, const int tile_width, const int tile_height)
{
    VVCPPS *pps = slice_map->pps;
    const int tile_x_end = tile_x_start + tile_width;
    const int tile_y_end = tile_y_start + tile_height;
    int height = 0, ctb_count = 0;

    if (!tile_width || !tile_height)
        return ;

    for (int tile_y = tile_y_start; tile_y < tile_y_end; tile_y++) {
        for (int tile_x = tile_x_start; tile_x < tile_x_end; tile_x++) {

            const int ctu_x = pps->col_bd[tile_x];
            const int ctu_y = pps->row_bd[tile_y];
            const int x_end = ctu_x + pps->column_width[tile_x];
            const int y_end = ctu_y + pps->row_height[tile_y];

            for (int y = ctu_y; y < y_end; y++) {
                for (int x = ctu_x; x < x_end; x++) {
                    const uint16_t ctb_addr_in_rs = y * pps->ctb_width + x;         //< CtbAddrInRs
                    const int offset = slice_map->start_offset + ctb_count;
                    pps->ctb_addr_in_slice[offset] = ctb_addr_in_rs;
                    ctb_count++;
                }
            }
        }
        height += pps->row_height[tile_y];
    }

    slice_map_add_new_slice(slice_map, slice_idx, pps->col_bd[tile_x_start], pps->row_bd[tile_y_start], height, ctb_count);
}

static int pps_parse_slices_in_one_tile(VVCPPS *pps, SliceMap *slice_map,
    const int tile_x, const int tile_y, GetBitContext *gb, void *log_ctx, int *slice_idx)
{
    int num_slices_in_tile, pps_num_exp_slices_in_tile, uniform_slice_height, remaining_height_in_ctbs_y;
    int ctu_x = pps->col_bd[tile_x];
    int ctu_y = pps->row_bd[tile_y];
    int i = *slice_idx;

    remaining_height_in_ctbs_y = pps->row_height[tile_y];
    ue(pps_num_exp_slices_in_tile, pps->row_height[tile_y] - 1);
    if (pps_num_exp_slices_in_tile == 0) {
        num_slices_in_tile = 1;
        slice_map_add_ctus(slice_map, i, ctu_x, ctu_y, pps->column_width[tile_x], pps->row_height[tile_y]);
    } else {
        int j, slice_height_in_ctus;
        for (j = 0; j < pps_num_exp_slices_in_tile; j++) {
            uep(slice_height_in_ctus, 1, 1, pps->row_height[tile_y]);

            slice_map_add_ctus(slice_map, i + j, ctu_x, ctu_y, pps->column_width[tile_x], slice_height_in_ctus);

            ctu_y += slice_height_in_ctus;
            remaining_height_in_ctbs_y -= slice_height_in_ctus;
        }
        uniform_slice_height = (j == 0 ? pps->row_height[tile_y] : slice_height_in_ctus);

        while (remaining_height_in_ctbs_y > 0) {
            slice_map_add_ctus(slice_map, i + j, ctu_x, ctu_y,
                pps->column_width[tile_x], FFMIN(uniform_slice_height, remaining_height_in_ctbs_y));

            ctu_y += uniform_slice_height;
            remaining_height_in_ctbs_y -= uniform_slice_height;
            j++;
        }
        num_slices_in_tile = j;
    }
    *slice_idx += num_slices_in_tile - 1;
    return 0;
}

static int sps_pos_in_subpic(const VVCSPS *sps, const int subpic_idx, const int pos_x, const int pos_y)
{
    return (pos_x >= sps->subpic_ctu_top_left_x[subpic_idx]) &&
        (pos_y >= sps->subpic_ctu_top_left_y[subpic_idx]) &&
        (pos_x < sps->subpic_ctu_top_left_x[subpic_idx] + sps->subpic_width[subpic_idx]) &&
        (pos_y < sps->subpic_ctu_top_left_y[subpic_idx] + sps->subpic_height[subpic_idx]);

}

static int pps_parse_rect_slice(VVCPPS *pps, const VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int i;
    SliceMap slice_map;
    uint16_t num_slices_in_pic;             ///< pps_num_slices_in_pic_minus1 + 1;

    slice_map_init(&slice_map, sps, pps);
    if (!pps->single_slice_per_subpic_flag) {
        int tile_idx = 0, pps_tile_idx_delta_present_flag = 0, tile_x, tile_y, ret, j;
        uep(num_slices_in_pic, 1, 1, VVC_MAX_SLICES);

        if (num_slices_in_pic > 2)
            pps_tile_idx_delta_present_flag = get_bits1(gb);

        for (i = 0; i < num_slices_in_pic; i++) {
            pps_derive_tile_xy(pps, tile_idx, &tile_x, &tile_y);
            if (i < num_slices_in_pic - 1) {
                int slice_width_in_tiles, slice_height_in_tiles;
                ret = pps_parse_slice_width_height_in_tiles(pps, i, tile_x, tile_y, pps_tile_idx_delta_present_flag,
                    gb, log_ctx, &slice_width_in_tiles, &slice_height_in_tiles);
                if (ret < 0)
                    return ret;

                if (slice_width_in_tiles == 1 && slice_height_in_tiles == 1 && pps->row_height[tile_y] > 1) {
                    ret = pps_parse_slices_in_one_tile(pps, &slice_map, tile_x, tile_y, gb, log_ctx, &i);
                    if (ret < 0)
                        return ret;
                } else {
                    slice_map_add_tiles(&slice_map, i, tile_x, tile_y, slice_width_in_tiles, slice_height_in_tiles);
                }
                if (i < num_slices_in_pic - 1) {
                    tile_idx = pps_parse_tile_idx(pps, i, tile_idx, slice_width_in_tiles, slice_height_in_tiles, pps_tile_idx_delta_present_flag, gb);
                    if (tile_idx < 0 || tile_idx >= pps->num_tiles_in_pic)
                        return AVERROR_INVALIDDATA;
                }
            }
            else {
                slice_map_add_tiles(&slice_map, num_slices_in_pic - 1,
                    tile_x, tile_y, pps->num_tile_columns - tile_x, pps->num_tile_rows - tile_y);
            }
        }

        //now, we got all slice information, let's resolve NumSlicesInSubpic
        for (i = 0; i < sps->num_subpics; i++) {
            pps->num_slices_in_subpic[i] = 0;
            for (j = 0; j < num_slices_in_pic; j++) {
                if (sps_pos_in_subpic(sps, i, slice_map.top_left_ctu_x[j], slice_map.top_left_ctu_y[j])) {
                    pps->num_slices_in_subpic[i]++;
                }
            }
        }
    } else {
        num_slices_in_pic = sps->num_subpics;
        for (i = 0; i < num_slices_in_pic; i++) {
            int start_x = -1, start_y = -1, width_in_tiles, height_in_tiles;
            pps->num_slices_in_subpic[i] = 1;
            for (int tile_y = 0; tile_y < pps->num_tile_rows; tile_y++) {
                for (int tile_x = 0; tile_x < pps->num_tile_columns; tile_x++) {
                    if (sps_pos_in_subpic(sps, i, pps->col_bd[tile_x], pps->row_bd[tile_y])) {
                        if (start_x == -1) {
                            start_x = tile_x;
                            start_y = tile_y;
                        }
                        width_in_tiles = tile_x - start_x + 1;
                        height_in_tiles = tile_y - start_y + 1;
                    }
                }
            }
            if (start_x != -1)
                slice_map_add_tiles(&slice_map, i, start_x, start_y, width_in_tiles, height_in_tiles);
        }
    }
    if (pps->single_slice_per_subpic_flag || num_slices_in_pic > 1)
        pps->loop_filter_across_slices_enabled_flag = get_bits1(gb);
    return 0;
}

static int pps_parse_slice(VVCPPS *pps, const VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    int  ret;

    pps->loop_filter_across_slices_enabled_flag = 0;
    if (pps->rect_slice_flag) {
        ret = pps_parse_rect_slice(pps, sps, gb, log_ctx);
        if (ret < 0)
            return ret;
    } else {
        int ctu_idx = 0;
        for (int tile_y = 0; tile_y < pps->num_tile_rows; tile_y++) {
            for (int tile_x = 0; tile_x < pps->num_tile_columns; tile_x++) {
                for (int ctu_y = pps->row_bd[tile_y]; ctu_y < pps->row_bd[tile_y] + pps->row_height[tile_y]; ctu_y++) {
                    for (int ctu_x = pps->col_bd[tile_x]; ctu_x < pps->col_bd[tile_x] + pps->column_width[tile_x]; ctu_x++) {
                        const int ctu_addr_rs = ctu_y * pps->ctb_width + ctu_x;
                        pps->ctb_addr_in_slice[ctu_idx++] = ctu_addr_rs;

                    }
                }
            }
        }
        pps->loop_filter_across_slices_enabled_flag = get_bits1(gb);
    }

    return 0;
}

static int pps_no_pic_partition(VVCPPS *pps, const VVCSPS *sps)
{
    SliceMap slice_map;

    pps->num_tile_columns = 1;
    pps->num_tile_rows    = 1;
    pps->num_tiles_in_pic = 1;

    pps->column_width = av_malloc_array(pps->num_tile_columns, sizeof(*pps->column_width));
    pps->row_height = av_malloc_array(pps->num_tile_rows, sizeof(*pps->row_height));
    if (!pps->column_width || !pps->row_height)
        return AVERROR(ENOMEM);
    pps->column_width[0] = pps->ctb_width;
    pps->row_height[0] = pps->ctb_height;

    pps->col_bd = pps_setup_bd(pps->column_width, pps->num_tile_columns);
    pps->row_bd = pps_setup_bd(pps->row_height, pps->num_tile_rows);
    if (!pps->col_bd || !pps->row_bd)
        return AVERROR(ENOMEM);

    slice_map_init(&slice_map, sps, pps);
    slice_map_add_ctus(&slice_map, 0, 0, 0, pps->ctb_width, pps->ctb_height);

    return 0;
}

static int pps_parse_pic_partition(VVCPPS *pps, const VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    pps->ctb_addr_in_slice = av_calloc(pps->ctb_count, sizeof(*pps->ctb_addr_in_slice));
    if (!pps->ctb_addr_in_slice)
        return AVERROR(ENOMEM);

    if (!pps->no_pic_partition_flag) {
        int ret;
        uint8_t pps_log2_ctu_size_minus5;
        u(2, pps_log2_ctu_size_minus5, sps->ctb_log2_size_y - 5, sps->ctb_log2_size_y - 5);

        ret = pps_parse_num_tiles_in_pic(pps, gb, log_ctx);
        if (ret < 0)
            return ret;

        pps->rect_slice_flag = 1;
        if (pps->num_tiles_in_pic > 1) {
            pps->loop_filter_across_tiles_enabled_flag = get_bits1(gb);
            pps->rect_slice_flag = get_bits1(gb);
        }

        pps->single_slice_per_subpic_flag = pps->rect_slice_flag ? get_bits1(gb) : 0;

        return pps_parse_slice(pps, sps, gb, log_ctx);
    }

    return pps_no_pic_partition(pps, sps);
}

static int pps_parse_inter(VVCPPS *pps, const VVCSPS *sps, GetBitContext *gb, void *log_ctx)
{
    for (int i = 0; i < 2; i++)
        uep(pps->num_ref_idx_default_active[i], 1, 1, 15);

    pps->rpl1_idx_present_flag       = get_bits1(gb);
    pps->weighted_pred_flag          = get_bits1(gb);
    pps->weighted_bipred_flag        = get_bits1(gb);
    pps->ref_wraparound_enabled_flag = get_bits1(gb);

    if (pps->ref_wraparound_enabled_flag)
        pps->ref_wraparound_offset = (pps->width / sps->min_cb_size_y) - get_ue_golomb_long(gb);
    return 0;
}

static void pps_parse_ph_flags(VVCPPS *pps, GetBitContext *gb)
{
    if (!pps->no_pic_partition_flag) {
        pps->rpl_info_in_ph_flag = get_bits1(gb);
        pps->sao_info_in_ph_flag = get_bits1(gb);
        pps->alf_info_in_ph_flag = get_bits1(gb);
        if ((pps->weighted_pred_flag || pps->weighted_bipred_flag) &&
            pps->rpl_info_in_ph_flag)
            pps->wp_info_in_ph_flag   = get_bits1(gb);
        pps->qp_delta_info_in_ph_flag = get_bits1(gb);
    }
}

int ff_vvc_decode_pps(VVCParamSets *ps, GetBitContext *gb, void *log_ctx)
{
    const VVCSPS      *sps = NULL;
    int ret = 0;
    ptrdiff_t nal_size;
    AVBufferRef *pps_buf;

    VVCPPS *pps = av_mallocz(sizeof(*pps));
    if (!pps)
        return AVERROR(ENOMEM);

    pps_buf = av_buffer_create((uint8_t *)pps, sizeof(*pps), vvc_pps_free, NULL, 0);
    if (!pps_buf) {
        av_freep(&pps);
        return AVERROR(ENOMEM);
    }

    av_log(log_ctx, AV_LOG_DEBUG, "Decoding PPS\n");

    nal_size = gb->buffer_end - gb->buffer;
    if (nal_size > sizeof(pps->data)) {
        av_log(log_ctx, AV_LOG_WARNING, "Truncating likely oversized PPS "
               "(%"PTRDIFF_SPECIFIER" > %"SIZE_SPECIFIER")\n",
               nal_size, sizeof(pps->data));
        pps->data_size = sizeof(pps->data);
    } else {
        pps->data_size = nal_size;
    }
    memcpy(pps->data, gb->buffer, pps->data_size);

    pps->pic_parameter_set_id = get_bits(gb, 6);
    pps->seq_parameter_set_id = get_bits(gb, 4);
    if (!ps->sps_list[pps->seq_parameter_set_id]) {
        av_log(log_ctx, AV_LOG_ERROR, "SPS %u does not exist.\n", pps->seq_parameter_set_id);
        ret = AVERROR_INVALIDDATA;
        goto err;
    }
    sps = (VVCSPS *)ps->sps_list[pps->seq_parameter_set_id]->data;

    pps->mixed_nalu_types_in_pic_flag = get_bits1(gb);

    ret = pps_parse_width_height(pps, sps, gb, log_ctx);
    if (ret < 0)
        goto err;

    pps_parse_conf_win(pps, sps, gb);

    ret = pps_scaling_win_parse(pps, sps, gb, log_ctx);
    if (ret < 0)
        goto err;

    pps->output_flag_present_flag = get_bits1(gb);

    ret = pps_parse_subpic_id(pps, sps, gb, log_ctx);
    if (ret < 0)
        goto err;

    pps->ctb_width  = vvc_ceil(pps->width,  sps->ctb_size_y);
    pps->ctb_height = vvc_ceil(pps->height, sps->ctb_size_y);
    pps->ctb_count  = pps->ctb_width * pps->ctb_height;

    ret = pps_parse_pic_partition(pps, sps, gb, log_ctx);
    if (ret < 0)
        goto err;

    pps->cabac_init_present_flag = get_bits1(gb);

    ret = pps_parse_inter(pps, sps, gb, log_ctx);
    if (ret < 0)
        goto err;

    sep(pps->init_qp, 26, -(sps->qp_bd_offset), 63);
    pps->cu_qp_delta_enabled_flag = get_bits1(gb);

    ret = pps_parse_chroma_tool_offsets(pps, gb, log_ctx);
    if (ret < 0)
        goto err;

    ret = pps_parse_deblocking_control(pps, gb, log_ctx);
    if (ret < 0)
        goto err;

    pps_parse_ph_flags(pps, gb);

    pps->picture_header_extension_present_flag  = get_bits1(gb);
    pps->slice_header_extension_present_flag    = get_bits1(gb);

    skip_bits1(gb);                 ///< pps_extension_flag

    pps->min_cb_width  = pps->width  / sps->min_cb_size_y;
    pps->min_cb_height = pps->height / sps->min_cb_size_y;
    pps->min_pu_width  = pps->width  >> MIN_PU_LOG2;
    pps->min_pu_height = pps->height >> MIN_PU_LOG2;
    pps->min_tb_width  = pps->width  >> MIN_TB_LOG2;
    pps->min_tb_height = pps->height >> MIN_TB_LOG2;
    pps->width32       = AV_CEIL_RSHIFT(pps->width,  5);
    pps->height32      = AV_CEIL_RSHIFT(pps->height, 5);
    pps->width64       = AV_CEIL_RSHIFT(pps->width,  6);
    pps->height64      = AV_CEIL_RSHIFT(pps->height, 6);

    ret = pps_setup(pps, sps);
    if (ret < 0)
        goto err;

    if (get_bits_left(gb) < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Overread PPS by %d bits\n", -get_bits_left(gb));
        goto err;
    }
    remove_pps(ps, pps->pic_parameter_set_id);
    ps->pps_list[pps->pic_parameter_set_id] = pps_buf;

    return 0;

err:
    av_buffer_unref(&pps_buf);
    return ret;
}

static int ph_parse_pic_parameter_set_id(VVCPH *ph, VVCParamSets *ps, GetBitContext* gb, void *log_ctx)
{
    AVBufferRef  *buf;

    ue(ph->pic_parameter_set_id, VVC_MAX_PPS_COUNT - 1);
    buf = ps->pps_list[ph->pic_parameter_set_id];
    if (!buf) {
        av_log(log_ctx, AV_LOG_ERROR, "PPS id %d not available.\n",
               ph->pic_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    ps->pps = (VVCPPS *)buf->data;

    buf = ps->sps_list[ps->pps->seq_parameter_set_id];
    if (!buf) {
        av_log(log_ctx, AV_LOG_ERROR, "SPS id %d not available.\n",
            ps->pps->seq_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    ps->sps = (VVCSPS*)buf->data;

    buf = ps->vps_list[ps->sps->video_parameter_set_id];
    if (!buf) {
        av_log(log_ctx, AV_LOG_ERROR, "VPS id %d not available.\n",
            ps->sps->video_parameter_set_id);
        return AVERROR_INVALIDDATA;
    }
    return 0;
}

static int rpls_list_parse(VVCRefPicListStruct *ref_lists, const VVCParamSets *ps,
    const int poc, GetBitContext *gb, void *log_ctx)
{
    const VVCSPS *sps = ps->sps;
    const VVCPPS *pps = ps->pps;
    int i, j, rpl_sps_flag[2], rpl_idx[2];
    for (i = 0; i < 2; i++) {
        VVCRefPicListStruct *rpls = ref_lists + i;
        int prev_delta_poc_msb = 0;

        rpl_sps_flag[i] = 0;
        if (sps->num_ref_pic_lists[i] > 0) {
            if (i == 0 || pps->rpl1_idx_present_flag)
                rpl_sps_flag[i] = get_bits1(gb);
            else
                rpl_sps_flag[i] = rpl_sps_flag[0];
        }
        if (rpl_sps_flag[i]) {
            if (sps->num_ref_pic_lists[i] == 1) {
                rpl_idx[i] = 0;
            } else if (i == 0 || pps->rpl1_idx_present_flag) {
                uint8_t bits = av_ceil_log2(sps->num_ref_pic_lists[i]);
                u(bits, rpl_idx[i], 0, sps->num_ref_pic_lists[i] - 1);
            } else {
                rpl_idx[1] = rpl_idx[0];
            }
            memcpy(rpls, &sps->ref_pic_list_struct[i][rpl_idx[i]], sizeof(*rpls));
        } else {
            rpls_parse(rpls, gb, i, sps->num_ref_pic_lists[i], sps, log_ctx);
        }

        for (j = 0; j < rpls->num_ref_entries; j++) {
            VVCRefPicListStructEntry *ref = &rpls->entries[j];

            if (!ref->st_ref_pic_flag) {
                if (rpls->ltrp_in_header_flag)
                    ref->lt_poc = get_bits(gb, sps->log2_max_pic_order_cnt_lsb);

                ref->lt_msb_flag = get_bits1(gb);
                if (ref->lt_msb_flag) {
                    uint32_t max = 1 << (32 - sps->log2_max_pic_order_cnt_lsb);
                    uint32_t delta_poc_msb_cycle_lt;

                    ue(delta_poc_msb_cycle_lt, max);
                    delta_poc_msb_cycle_lt += prev_delta_poc_msb;
                    ref->lt_poc =
                        poc - delta_poc_msb_cycle_lt * sps->max_pic_order_cnt_lsb -
                        (poc & (sps->max_pic_order_cnt_lsb - 1)) + ref->lt_poc;

                    prev_delta_poc_msb = delta_poc_msb_cycle_lt;
                }
            }
        }
    }

    return 0;

}

static int alf_parse(Alf *alf, const VVCParamSets* ps, GetBitContext *gb, void *log_ctx)
{
    const VVCSPS *sps = ps->sps;
    alf->cc_enabled_flag[0] = alf->cc_enabled_flag[1] = 0;
    alf->enabled_flag[CB]   = alf->enabled_flag[CR] = 0;

    alf->enabled_flag[LUMA] = get_bits1(gb);
    if (alf->enabled_flag[LUMA]) {

        alf->num_aps_ids_luma = get_bits(gb, 3);
        for (int i = 0; i < alf->num_aps_ids_luma; i++) {
            alf->aps_id_luma[i] = get_bits(gb, 3);
            if (!ps->alf_list[alf->aps_id_luma[i]]) {
                av_log(log_ctx, AV_LOG_ERROR, "aps_id_luma %d not available.\n",
                    alf->aps_id_luma[i]);
                return AVERROR_INVALIDDATA;
            }
        }
        if (sps->chroma_format_idc != 0) {
            for (int i = CB; i <= CR; i++)
                alf->enabled_flag[i] = get_bits1(gb);
        }

        if (alf->enabled_flag[CB] || alf->enabled_flag[CR]) {
            alf->aps_id_chroma = get_bits(gb, 3);
            if (!ps->alf_list[alf->aps_id_chroma]) {
                av_log(log_ctx, AV_LOG_ERROR, "aps_id_chroma %d not available.\n",
                    alf->aps_id_chroma);
                return AVERROR_INVALIDDATA;
            }
        }

        if (sps->ccalf_enabled_flag) {
            for (int i = 0; i < 2; i++) {
                alf->cc_enabled_flag[i] = get_bits1(gb);
                if (alf->cc_enabled_flag[i]) {
                    alf->cc_aps_id[i] = get_bits(gb, 3);
                    if (!ps->alf_list[alf->cc_aps_id[i]]) {
                        av_log(log_ctx, AV_LOG_ERROR, "cc_aps_id[%d] %d not available.\n",
                            i, alf->cc_aps_id[i]);
                        return AVERROR_INVALIDDATA;
                    }
                }
            }
        }
    }
    return 0;
}

// 8.3.1 Decoding process for picture order count
static int ph_compute_poc(VVCPH *ph, const VVCSPS *sps, const int poc_tid0, const int is_clvss)
{
    const int max_poc_lsb       = sps->max_pic_order_cnt_lsb;
    const int prev_poc_lsb      = poc_tid0 % max_poc_lsb;
    const int prev_poc_msb      = poc_tid0 - prev_poc_lsb;
    const int poc_lsb           = ph->pic_order_cnt_lsb;
    int poc_msb;

    if (ph->poc_msb_cycle_present_flag) {
        poc_msb = ph->poc_msb_cycle_val * max_poc_lsb;
    } else if (is_clvss) {
        poc_msb = 0;
    } else {
        if (poc_lsb < prev_poc_lsb && prev_poc_lsb - poc_lsb >= max_poc_lsb / 2)
            poc_msb = prev_poc_msb + max_poc_lsb;
        else if (poc_lsb > prev_poc_lsb && poc_lsb - prev_poc_lsb > max_poc_lsb / 2)
            poc_msb = prev_poc_msb - max_poc_lsb;
        else
            poc_msb = prev_poc_msb;
    }

    return poc_msb + poc_lsb;
}

static int ph_parse_poc(VVCPH *ph, const VVCSPS *sps, const int poc_tid0, const int is_clvss, GetBitContext *gb, void *log_ctx)
{
    ph->pic_order_cnt_lsb = get_bits(gb, sps->log2_max_pic_order_cnt_lsb);
    if (ph->gdr_pic_flag)
        ue(ph->recovery_poc_cnt, 1 << sps->log2_max_pic_order_cnt_lsb);

    for (int i = 0; i < sps->num_extra_ph_bytes * 8; i++) {
        if (sps->extra_ph_bit_present_flag[i])
            skip_bits1(gb);
    }
    if (sps->poc_msb_cycle_flag) {
        ph->poc_msb_cycle_present_flag = get_bits1(gb);
        if (ph->poc_msb_cycle_present_flag)
            ph->poc_msb_cycle_val = get_bits(gb, sps->poc_msb_cycle_len);
    }
    ph->poc = ph_compute_poc(ph, sps, poc_tid0, is_clvss);

    return 0;
}

static int ph_parse_alf(VVCPH *ph, const VVCParamSets *ps, GetBitContext *gb, void *log_ctx)
{

    if (ps->sps->alf_enabled_flag && ps->pps->alf_info_in_ph_flag)
        return alf_parse(&ph->alf, ps, gb, log_ctx);
    ph->alf.enabled_flag[LUMA] = 0;
    return 0;
}

static av_always_inline int lmcs_derive_lut_sample(int sample,
    int *pivot1, int *pivot2, int *scale_coeff, const int idx, const int max)
{
    const int lut_sample =
        pivot1[idx] + ((scale_coeff[idx] * (sample - pivot2[idx]) + (1<< 10)) >> 11);
    return av_clip(lut_sample, 0, max - 1);
}

//8.8.2.2 Inverse mapping process for a luma sample
static int ph_lmcs_derive_lut(VVCPH *ph, const VVCParamSets *ps)
{
    const VVCSPS *sps = ps->sps;
    const AVBufferRef *lmcs_buf;
    const VVCLMCS *lmcs;
    const int max    = (1 << sps->bit_depth);
    const int org_cw = max / LMCS_MAX_BIN_SIZE;
    const int shift  = av_log2(org_cw);
    const int off    = 1 << (shift - 1);
    int cw[LMCS_MAX_BIN_SIZE];
    int input_pivot[LMCS_MAX_BIN_SIZE];
    int scale_coeff[LMCS_MAX_BIN_SIZE];
    int inv_scale_coeff[LMCS_MAX_BIN_SIZE];
    int i;
    if (sps->bit_depth > LMCS_MAX_BIT_DEPTH)
        return AVERROR_PATCHWELCOME;

    lmcs_buf = ps->lmcs_list[ph->lmcs_aps_id];
    if (!lmcs_buf)
        return AVERROR_INVALIDDATA;
    lmcs = (VVCLMCS*)lmcs_buf->data;

    memset(cw, 0, sizeof(cw));
    for (int i = lmcs->min_bin_idx; i <= lmcs->max_bin_idx; i++)
        cw[i] = org_cw + lmcs->delta_cw[i];

    ph->lmcs_pivot[0] = 0;
    for (i = 0; i < LMCS_MAX_BIN_SIZE; i++) {
        input_pivot[i]        = i * org_cw;
        ph->lmcs_pivot[i + 1] = ph->lmcs_pivot[i] + cw[i];
        scale_coeff[i]        = (cw[i] * (1 << 11) +  off) >> shift;
        if (cw[i] == 0) {
            inv_scale_coeff[i] = 0;
            ph->lmcs_chroma_scale_coeff[i] = (1 << 11);
        } else {
            inv_scale_coeff[i] = org_cw * (1 << 11) / cw[i];
            ph->lmcs_chroma_scale_coeff[i] = org_cw * (1 << 11) / (cw[i] + lmcs->delta_crs);
        }
    }

    //derive lmcs_fwd_lut
    for (int sample = 0; sample < max; sample++) {
        const int idx_y = sample / org_cw;
        const int fwd_sample = lmcs_derive_lut_sample(sample, ph->lmcs_pivot,
            input_pivot, scale_coeff, idx_y, max);
        if (sps->bit_depth > 8)
            ((uint16_t *)ph->lmcs_fwd_lut)[sample] = fwd_sample;
        else
            ph->lmcs_fwd_lut[sample] = fwd_sample;

    }

    //derive lmcs_inv_lut
    i = lmcs->min_bin_idx;
    for (int sample = 0; sample < max; sample++) {
        int inv_sample;
        while (sample >= ph->lmcs_pivot[i + 1] && i <= lmcs->max_bin_idx)
            i++;

        inv_sample = lmcs_derive_lut_sample(sample, input_pivot, ph->lmcs_pivot,
            inv_scale_coeff, i, max);

        if (sps->bit_depth > 8)
            ((uint16_t *)ph->lmcs_inv_lut)[sample] = inv_sample;
        else
            ph->lmcs_inv_lut[sample] = inv_sample;
    }

    ph->lmcs_min_bin_idx = lmcs->min_bin_idx;
    ph->lmcs_max_bin_idx = lmcs->max_bin_idx;

    return 0;
}

static int ph_parse_lmcs(VVCPH *ph, const VVCParamSets *ps, GetBitContext *gb)
{
    const VVCSPS *sps = ps->sps;
    int ret;

    ph->lmcs_enabled_flag = 0;
    ph->chroma_residual_scale_flag = 0;
    if (sps->lmcs_enabled_flag) {
        ph->lmcs_enabled_flag = get_bits1(gb);
        if (ph->lmcs_enabled_flag) {
            ph->lmcs_aps_id = get_bits(gb, 2);
            if (sps->chroma_format_idc)
                ph->chroma_residual_scale_flag = get_bits1(gb);
            ret = ph_lmcs_derive_lut(ph, ps);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

static void ph_parse_scaling_list(VVCPH *ph, const VVCSPS *sps, GetBitContext *gb)
{
    ph->explicit_scaling_list_enabled_flag =  0;
    if (sps->explicit_scaling_list_enabled_flag) {
        ph->explicit_scaling_list_enabled_flag = get_bits1(gb);
        if (ph->explicit_scaling_list_enabled_flag) {
            //todo: check the ph->scaling_list_aps_id range, when aps ready
            ph->scaling_list_aps_id = get_bits(gb, 3);
        }
    }
}

static int ph_parse_virtual_boundaries(VVCPH *ph, const VVCSPS *sps,
    const VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    ph->vbs.virtual_boundaries_present_flag = 0;
    if (sps->virtual_boundaries_enabled_flag && !sps->vbs.virtual_boundaries_present_flag) {
        const int ret = virtual_boundaries_parse(&ph->vbs, pps->width, pps->height, gb, log_ctx);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static int ph_parse_temporal_mvp(VVCPH *ph, const VVCSPS *sps, const VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    ph->temporal_mvp_enabled_flag = 0;
    if (sps->temporal_mvp_enabled_flag) {
        ph->temporal_mvp_enabled_flag = get_bits1(gb);
        if (ph->temporal_mvp_enabled_flag && pps->rpl_info_in_ph_flag) {
            int num_ref_entries;
            ph->collocated_list = L0;
            if (ph->rpls[1].num_ref_entries > 0)
                ph->collocated_list = !get_bits1(gb);
            num_ref_entries = ph->rpls[ph->collocated_list].num_ref_entries;
            if (num_ref_entries > 1) {
                ue(ph->collocated_ref_idx, num_ref_entries - 1);
            }
        }
    }
    return 0;
}

static void ph_parse_sao(VVCPH *ph, const VVCSPS *sps, const VVCPPS *pps, GetBitContext *gb)
{
    ph->sao_luma_enabled_flag = 0;
    ph->sao_chroma_enabled_flag = 0;
    if (sps->sao_enabled_flag && pps->sao_info_in_ph_flag) {
        ph->sao_luma_enabled_flag = get_bits1(gb);
        if (sps->chroma_format_idc != 0)
            ph->sao_chroma_enabled_flag = get_bits1(gb);
    }
}

static int ph_parse_dbf(VVCPH *ph, const VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    ph->deblocking_filter_disabled_flag = pps->deblocking_filter_disabled_flag;
    if (pps->dbf_info_in_ph_flag) {
        int ph_deblocking_params_present_flag = get_bits1(gb);
        if (ph_deblocking_params_present_flag) {
            ph->deblocking_filter_disabled_flag = 0;
            if (!pps->deblocking_filter_disabled_flag)
                ph->deblocking_filter_disabled_flag = get_bits1(gb);
            if (!ph->deblocking_filter_disabled_flag)
                return deblock_parse(&ph->deblock, pps, gb, log_ctx);
        }
    }
    memcpy(&ph->deblock, &pps->deblock, sizeof(ph->deblock));
    return 0;
}

static int ph_parse_cu_qp(VVCPH *ph, const int inter, const VVCSPS *sps, const VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    const PartitionConstraints *pc  = inter ? &ph->inter_slice : &ph->intra_slice_luma;
    const int ctb_log2_size_y       = sps->ctb_log2_size_y;
    const int min_qt_log2_size_y    = pc->log2_diff_min_qt_min_cb + sps->min_cb_log2_size_y;

    ph->cu_qp_delta_subdiv[inter] = 0;
    if (pps->cu_qp_delta_enabled_flag)
        ue(ph->cu_qp_delta_subdiv[inter],
           2 * (ctb_log2_size_y - min_qt_log2_size_y +
           pc->max_mtt_hierarchy_depth));

    ph->cu_chroma_qp_offset_subdiv[inter] = 0;
    if (pps->cu_chroma_qp_offset_list_enabled_flag)
        ue(ph->cu_chroma_qp_offset_subdiv[inter],
           2 * (ctb_log2_size_y - min_qt_log2_size_y +
           pc->max_mtt_hierarchy_depth));

    return 0;
}

static int ph_parse_intra(VVCPH *ph, const VVCSPS *sps, const VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    int ret;

    if (ph->intra_slice_allowed_flag) {
        ph->intra_slice_luma = sps->intra_slice_luma;
        ph->intra_slice_chroma = sps->intra_slice_chroma;
        if (ph->partition_constraints_override_flag) {
            ret = partition_constraints_parse(&ph->intra_slice_luma, sps, gb, log_ctx);
            if (ret < 0)
                return ret;

            if (sps->qtbtt_dual_tree_intra_flag) {
                ret = partition_constraints_parse(&ph->intra_slice_chroma, sps, gb, log_ctx);
                if (ret < 0)
                    return ret;
            }
        }
        ret = ph_parse_cu_qp(ph, 0, sps, pps, gb, log_ctx);
        if (ret < 0)
            return ret;
    }

    return 0;
}

static int hls_pred_weight_table(PredWeightTable *w, const uint8_t *num_ref_idx_active,
    const VVCRefPicListStruct *rpls, const VVCSPS *sps, const VVCPPS *pps, GetBitContext *gb,
    void *log_ctx)
{
    const int has_chroma = (sps->chroma_format_idc > 0);

    ue(w->log2_denom[LUMA], 7);

    w->log2_denom[CHROMA] = 0;
    if (has_chroma)
        sep(w->log2_denom[CHROMA], w->log2_denom[LUMA], 0, 7);

    for (int lx = L0; lx <= L1; lx++) {
        w->nb_weights[lx] = num_ref_idx_active[lx];
        if (pps->wp_info_in_ph_flag) {
            w->nb_weights[lx] = 0;
            if (lx == L0 || (pps->weighted_bipred_flag && rpls[L1].num_ref_entries))
                ue(w->nb_weights[lx], FFMIN(15, rpls[lx].num_ref_entries));
        }

        for (int is_chroma = 0; is_chroma <= has_chroma; is_chroma++) {
            for (int i = 0; i < w->nb_weights[lx]; i++)
                w->weight_flag[lx][is_chroma][i] = get_bits1(gb);
        }

        for (int i = 0; i < w->nb_weights[lx]; i++) {
            const int c_end = has_chroma ? VVC_MAX_SAMPLE_ARRAYS : 1;
            for (int c_idx = 0; c_idx < c_end; c_idx++) {
                const int is_chroma = !!c_idx;
                const int denom = 1 << w->log2_denom[is_chroma];

                w->weight[lx][c_idx][i] = denom;
                w->offset[lx][c_idx][i] = 0;
                if (w->weight_flag[lx][is_chroma][i]) {
                    sep(w->weight[lx][c_idx][i], denom, -128 + denom, 127 + denom);
                    if (!c_idx) {
                        se(w->offset[lx][c_idx][i], -128, 127);
                    } else {
                        int offset;
                        se(offset, -4 * 128, 4 * 127);
                        offset += 128 - ((128 * w->weight[lx][c_idx][i]) >> w->log2_denom[CHROMA]);
                        w->offset[lx][c_idx][i] = av_clip_intp2(offset, 7);
                    }
                }
            }
        }
    }
    return 0;
}

static int ph_parse_inter(VVCPH *ph, const VVCSPS *sps, const VVCPPS *pps, GetBitContext *gb, void *log_ctx)
{
    int ret;

    if (ph->inter_slice_allowed_flag) {
        ph->inter_slice = sps->inter_slice;
        if (ph->partition_constraints_override_flag) {
            ret = partition_constraints_parse(&ph->inter_slice, sps, gb, log_ctx);
            if (ret < 0)
                return ret;
        }

        ret = ph_parse_cu_qp(ph, 1, sps, pps, gb, log_ctx);
        if (ret < 0)
            return ret;

        ret = ph_parse_temporal_mvp(ph, sps, pps, gb, log_ctx);
        if (ret < 0)
            return ret;

        if (sps->affine_enabled_flag)
            ph->max_num_subblock_merge_cand = 5 - sps->five_minus_max_num_subblock_merge_cand;
        else
            ph->max_num_subblock_merge_cand = sps->sbtmvp_enabled_flag && ph->temporal_mvp_enabled_flag;

        ph->mmvd_fullpel_only_flag = 0;
        if (sps->mmvd_fullpel_only_enabled_flag)
            ph->mmvd_fullpel_only_flag = get_bits1(gb);

        ph->mvd_l1_zero_flag   = 1;
        ph->bdof_disabled_flag = 1;
        ph->dmvr_disabled_flag = 1;
        if (!pps->rpl_info_in_ph_flag || ph->rpls[1].num_ref_entries > 0) {
            ph->mvd_l1_zero_flag = get_bits1(gb);

            ph->bdof_disabled_flag = 1 - sps->bdof_enabled_flag;
            if (sps->bdof_control_present_in_ph_flag)
                ph->bdof_disabled_flag = get_bits1(gb);

            ph->dmvr_disabled_flag = 1 - sps->dmvr_enabled_flag;
            if (sps->dmvr_control_present_in_ph_flag)
                ph->dmvr_disabled_flag = get_bits1(gb);
        }

        ph->prof_disabled_flag = 1;
        if (sps->prof_control_present_in_ph_flag)
            ph->prof_disabled_flag = get_bits1(gb);

        if ((pps->weighted_pred_flag ||
            pps->weighted_bipred_flag) &&
            pps->wp_info_in_ph_flag) {
            // if pps->wp_info_in_ph->flag == 1
            // hls_pred_weight_table will not use num_ref_idx_active
            uint8_t num_ref_idx_active[2] = {0, 0};
            ret = hls_pred_weight_table(&ph->pwt, num_ref_idx_active,
                ph->rpls, sps, pps, gb, log_ctx);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

static int ph_parse(VVCPH *ph, VVCParamSets *ps, const int poc_tid0, const int is_clvss, GetBitContext *gb, void *log_ctx)
{
    const VVCSPS *sps;
    const VVCPPS *pps;
    int ret, start_bit;

    start_bit = get_bits_count(gb);
    ph->gdr_or_irap_pic_flag = get_bits1(gb);
    ph->non_ref_pic_flag     = get_bits1(gb);

    if (ph->gdr_or_irap_pic_flag)
        ph->gdr_pic_flag = get_bits1(gb);

    ph->inter_slice_allowed_flag = get_bits1(gb);
    ph->intra_slice_allowed_flag = !ph->inter_slice_allowed_flag || get_bits1(gb);

    ret = ph_parse_pic_parameter_set_id(ph, ps, gb, log_ctx);
    if (ret < 0)
        return ret;
    sps = ps->sps;
    pps = ps->pps;

    ret = ph_parse_poc(ph, sps, poc_tid0, is_clvss, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = ph_parse_alf(ph, ps, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = ph_parse_lmcs(ph, ps, gb);
    if (ret < 0)
        return ret;
    ph_parse_scaling_list(ph, sps, gb);
    ph_parse_virtual_boundaries(ph, sps, pps, gb, log_ctx);

    ph->pic_output_flag = 1;
    if (pps->output_flag_present_flag && !ph->non_ref_pic_flag)
        ph->pic_output_flag = get_bits1(gb);

    if (pps->rpl_info_in_ph_flag)
        rpls_list_parse(ph->rpls, ps, ph->poc, gb, log_ctx);

    if (sps->partition_constraints_override_enabled_flag)
        ph->partition_constraints_override_flag = get_bits1(gb);

    ret = ph_parse_intra(ph, sps, pps, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = ph_parse_inter(ph, sps, pps, gb, log_ctx);
    if (ret < 0)
        return ret;

    if (pps->qp_delta_info_in_ph_flag)
        se(ph->qp_delta, -sps->qp_bd_offset - pps->init_qp, 63 - pps->init_qp);

    ph->joint_cbcr_sign_flag = 0;
    if (sps->joint_cbcr_enabled_flag)
        ph->joint_cbcr_sign_flag = get_bits1(gb);

    ph_parse_sao(ph, sps, pps, gb);
    ph_parse_dbf(ph, pps, gb, log_ctx);

    if (get_bits_left(gb) < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Overread PH by %d bits\n", -get_bits_left(gb));
        return AVERROR_INVALIDDATA;
    }
    ph->size = get_bits_count(gb) - start_bit;
    return 0;

}

int ff_vvc_decode_ph(VVCParamSets *ps, const int poc_tid0, const int is_clvss, GetBitContext *gb, void *log_ctx)
{
    const VVCSPS *sps = ps->sps;
    const VVCPPS *pps = ps->pps;
    VVCPH *ph;
    AVBufferRef *ph_buf;
    int ret;

    ph_buf = av_buffer_allocz(sizeof(*ps->ph));
    if (!ph_buf)
        return AVERROR(ENOMEM);

    ph = (VVCPH *)ph_buf->data;
    ret = ph_parse(ph, ps, poc_tid0, is_clvss, gb, log_ctx);
    if (ret < 0) {
        //sps and pps may changed
        ps->sps = sps;
        ps->pps = pps;
        av_buffer_unref(&ph_buf);
        return ret;
    }
    av_buffer_unref(&ps->ph_buf);
    ps->ph_buf = ph_buf;
    ps->ph = ph;
    return 0;
}

static int aps_alf_parse_luma(VVCALF *alf, GetBitContext *gb, void *log_ctx)
{
    int     alf_luma_coeff_delta_idx[ALF_NUM_FILTERS_LUMA] = { 0 };
    int8_t  luma_coeff   [ALF_NUM_FILTERS_LUMA * ALF_NUM_COEFF_LUMA];
    uint8_t luma_clip_idx[ALF_NUM_FILTERS_LUMA * ALF_NUM_COEFF_LUMA];

    const int alf_luma_clip_flag = get_bits1(gb);
    const int alf_luma_num_filters_signalled = get_ue_golomb(gb) + 1;

    if (alf_luma_num_filters_signalled > ALF_NUM_FILTERS_LUMA) {
        av_log(log_ctx, AV_LOG_WARNING, "alf_luma_num_filters_signalled(%d) should <= %d\n",
            alf_luma_num_filters_signalled, ALF_NUM_FILTERS_LUMA);
        return AVERROR_INVALIDDATA;
    }

    //alf_luma_coeff_delta_idx[]
    if (alf_luma_num_filters_signalled > 1) {
        const int num_bits = av_ceil_log2(alf_luma_num_filters_signalled);
        for (int i = 0; i < ALF_NUM_FILTERS_LUMA; i++) {
            alf_luma_coeff_delta_idx[i] = get_bits(gb, num_bits);
            if (alf_luma_coeff_delta_idx[i] >= ALF_NUM_FILTERS_LUMA) {
                av_log(log_ctx, AV_LOG_WARNING, "alf_luma_coeff_delta_idx[%d](%d) should < %d\n",
                    i, alf_luma_coeff_delta_idx[i], ALF_NUM_FILTERS_LUMA);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    //alf_luma_coeff_abs, alf_luma_coeff_sign
    for (int i = 0; i < alf_luma_num_filters_signalled; i++) {
        for (int j = 0; j < ALF_NUM_COEFF_LUMA; j++) {

            int alf_luma_coeff = get_ue_golomb(gb);

            if (alf_luma_coeff && get_bits1(gb))
                alf_luma_coeff = -alf_luma_coeff;
            if (alf_luma_coeff < -128 || alf_luma_coeff > 127) {
                av_log(log_ctx, AV_LOG_WARNING, "alf_luma_coeff[%d][%d](%d) should in range [-128, 127]\n",
                    i, j, alf_luma_coeff);
                return AVERROR_INVALIDDATA;
            }
            luma_coeff[i * ALF_NUM_COEFF_LUMA + j] = alf_luma_coeff;
        }
    }

    if (alf_luma_clip_flag ) {
        for (int i = 0; i < alf_luma_num_filters_signalled; i++) {
            for (int j = 0; j < ALF_NUM_COEFF_LUMA; j++)
                luma_clip_idx[i * ALF_NUM_COEFF_LUMA + j] = get_bits(gb, 2);
        }
    } else {
        memset(luma_clip_idx, 0, sizeof(luma_clip_idx));
    }

    for (int i = 0; i < ALF_NUM_FILTERS_LUMA; i++) {
        const int idx = alf_luma_coeff_delta_idx[i];
        memcpy(&alf->luma_coeff[i * ALF_NUM_COEFF_LUMA], &luma_coeff[idx * ALF_NUM_COEFF_LUMA],
            sizeof(luma_coeff[0]) * ALF_NUM_COEFF_LUMA);
        memcpy(&alf->luma_clip_idx[i * ALF_NUM_COEFF_LUMA], &luma_clip_idx[idx * ALF_NUM_COEFF_LUMA],
            sizeof(luma_clip_idx[0]) * ALF_NUM_COEFF_LUMA);
    }
    return 0;
}

static int aps_alf_parse_chroma(VVCALF *alf, GetBitContext *gb, void *log_ctx)
{
    const int alf_chroma_clip_flag = get_bits1(gb);

    alf->num_chroma_filters        = get_ue_golomb_long(gb) + 1;

    if (alf->num_chroma_filters > ALF_NUM_FILTERS_CHROMA) {
        av_log(log_ctx, AV_LOG_WARNING, "alf_num_chroma_filters_signalled(%d) should <= %d\n",
            alf->num_chroma_filters, ALF_NUM_FILTERS_CHROMA);
        return AVERROR_INVALIDDATA;
    }

    //alf_chroma_coeff_abs, alf_chroma_coeff_sign
    for (int i = 0; i < alf->num_chroma_filters; i++) {
        for (int j = 0; j < ALF_NUM_COEFF_CHROMA; j++) {
            int alf_chroma_coeff = get_ue_golomb(gb);

            if (alf_chroma_coeff && get_bits1(gb))
                alf_chroma_coeff = -alf_chroma_coeff;
            if (alf_chroma_coeff < -128 || alf_chroma_coeff > 127) {
                av_log(log_ctx, AV_LOG_WARNING, "alf_chroma_coeff[%d][%d](%d) should in range [-128, 127]\n",
                    i, j, alf_chroma_coeff);
                return AVERROR_INVALIDDATA;
            }
            alf->chroma_coeff[i * ALF_NUM_COEFF_CHROMA + j] = alf_chroma_coeff;
        }
        if (alf_chroma_clip_flag) {
            for (int j = 0; j < ALF_NUM_COEFF_CHROMA; j++) {
                const int alf_chroma_clip_idx = get_bits(gb, 2);
                alf->chroma_clip_idx[i * ALF_NUM_COEFF_CHROMA + j] = alf_chroma_clip_idx;
            }
        }
    }
    return 0;
}

static int aps_alf_parse_cc(VVCALF *alf, const int idx, GetBitContext *gb, void *log_ctx)
{
    alf->cc_filters_signalled[idx] = get_ue_golomb(gb) + 1;
    if (alf->cc_filters_signalled[idx] > ALF_NUM_FILTERS_CC) {
        av_log(log_ctx, AV_LOG_WARNING, "cc_filters_signalled[%d](%d) should in range <= %d\n",
                idx, alf->cc_filters_signalled[idx], ALF_NUM_FILTERS_CC);
            return AVERROR_INVALIDDATA;
    }
    for (int i = 0; i < alf->cc_filters_signalled[idx]; i++) {
        for (int j = 0; j < ALF_NUM_COEFF_CC; j++) {
            int coeff = get_bits(gb, 3);
            if (coeff)
                coeff = (1 - 2 * get_bits1(gb)) * (1 << (coeff - 1));
            alf->cc_coeff[idx][i * ALF_NUM_COEFF_CC + j] = coeff;
        }
    }
    return 0;
}

static int aps_alf_parse(VVCALF *alf, GetBitContext *gb, void *log_ctx)
{
    const int aps_chroma_present_flag         = get_bits1(gb);
    const int alf_luma_filter_signal_flag     = get_bits1(gb);
    int alf_chroma_filter_signal_flag = 0;
    int alf_cc_filter_signal_flag[2]  = { 0 };
    int ret;

    if (aps_chroma_present_flag) {
        alf_chroma_filter_signal_flag = get_bits1(gb);
        alf_cc_filter_signal_flag[0]  = get_bits1(gb);
        alf_cc_filter_signal_flag[1]  = get_bits1(gb);
    }
    if (alf_luma_filter_signal_flag) {
        ret = aps_alf_parse_luma(alf, gb, log_ctx);
        if (ret < 0)
            return ret;
    }
    if (alf_chroma_filter_signal_flag) {
        ret = aps_alf_parse_chroma(alf, gb, log_ctx);
        if (ret < 0 )
            return ret;
    }
    for (int i = 0; i < 2; i++) {
        if (alf_cc_filter_signal_flag[i]) {
            ret = aps_alf_parse_cc(alf, i, gb, log_ctx);
            if (ret < 0)
                return ret;
        }
    }
    return 0;
}

static int aps_lmcs_parse(VVCLMCS *lmcs, GetBitContext *gb, void *log_ctx)
{
    const int aps_chroma_present_flag   = get_bits1(gb);
    int lmcs_delta_cw_prec;

    lmcs->min_bin_idx   = get_ue_golomb(gb);
    lmcs->max_bin_idx   = LMCS_MAX_BIN_SIZE - 1 - get_ue_golomb(gb);
    lmcs_delta_cw_prec  = get_ue_golomb(gb) + 1;
    if (lmcs->min_bin_idx >= LMCS_MAX_BIN_SIZE) {
        av_log(log_ctx, AV_LOG_WARNING, "lmcs_min_bin_idx(%d) should < %d\n",
            lmcs->min_bin_idx, LMCS_MAX_BIN_SIZE);
        return AVERROR_INVALIDDATA;
    }
    if (lmcs->max_bin_idx >= LMCS_MAX_BIN_SIZE || lmcs->max_bin_idx < lmcs->min_bin_idx) {
        av_log(log_ctx, AV_LOG_WARNING, "lmcs_max_bin_idx(%d) should in range [%d, %d]\n",
            lmcs->max_bin_idx, lmcs->min_bin_idx, LMCS_MAX_BIN_SIZE - 1);
        return AVERROR_INVALIDDATA;
    }
    if (lmcs_delta_cw_prec > LMCS_MAX_BIN_SIZE - 1) {
        av_log(log_ctx, AV_LOG_WARNING, "lmcs_delta_cw_prec_minus1(%d) should in range [0, 14]\n",
            lmcs_delta_cw_prec - 1);
        return AVERROR_INVALIDDATA;
    }

    memset(&lmcs->delta_cw, 0, sizeof(lmcs->delta_cw));
    for (int i = lmcs->min_bin_idx; i <= lmcs->max_bin_idx; i++) {
        lmcs->delta_cw[i] = get_bits(gb, lmcs_delta_cw_prec);
        if (lmcs->delta_cw[i] && get_bits1(gb))
            lmcs->delta_cw[i] = -lmcs->delta_cw[i];
    }
    if (aps_chroma_present_flag) {
        lmcs->delta_crs = get_bits(gb, 3);
        if (lmcs->delta_crs && get_bits1(gb))
            lmcs->delta_crs = -lmcs->delta_crs;
    }
    return 0;
}

static int is_luma_list(const int id)
{
    return id % VVC_MAX_SAMPLE_ARRAYS == SL_START_4x4 || id == SL_START_64x64 + 1;
}

static int derive_matrix_size(const int id)
{
    return id < SL_START_4x4 ? 2 : (id < SL_START_8x8 ? 4 : 8);
}

static int derive_max_delta(const int id)
{
    return (id < SL_START_4x4) ? id : ((id < SL_START_8x8) ? (id - 2) : (id - 8));
}

// 7.4.3.20 Scaling list data semantics
static int aps_scaling_parse(VVCScalingList *sl, GetBitContext *gb, AVCodecContext *log_ctx)
{

    int coeff[SL_MAX_MATRIX_SIZE * SL_MAX_MATRIX_SIZE];
    const uint8_t *pred;
    const int *scaling_list;
    const int aps_chroma_present_flag = get_bits1(gb);

    for (int id = 0; id < SL_MAX_ID; id++) {
        const int matrix_size   = derive_matrix_size(id);
        const int log2_size     = log2(matrix_size);
        const int list_size     = matrix_size * matrix_size;
        int scaling_list_copy_mode_flag = 1;
        int scaling_list_pred_mode_flag = 0;
        int scaling_list_pred_id_delta  = 0;
        int dc = 0;

        if (aps_chroma_present_flag || is_luma_list(id)) {
            scaling_list_copy_mode_flag = get_bits1(gb);
            if (!scaling_list_copy_mode_flag)
                scaling_list_pred_mode_flag = get_bits1(gb);
            if ((scaling_list_copy_mode_flag || scaling_list_pred_mode_flag) &&
                id != SL_START_2x2 &&
                id != SL_START_4x4 &&
                id != SL_START_8x8) {
                const int max_delta = derive_max_delta(id);
                ue(scaling_list_pred_id_delta, max_delta);
            }
            if (!scaling_list_copy_mode_flag) {
                int next_coef = 0;
                int scaling_list_delta_coef, scaling_list_dc_coef;

                if (id >= SL_START_16x16) {
                    se(scaling_list_dc_coef, -128, 127);
                    dc = next_coef = scaling_list_dc_coef;
                }

                for (int i = 0; i < list_size; i++) {
                    const int x = ff_vvc_diag_scan_x[3][3][i];
                    const int y = ff_vvc_diag_scan_y[3][3][i];

                    if (!(id >= SL_START_64x64 && x >= 4 && y >= 4)) {
                        se(scaling_list_delta_coef, -128, 127);
                        next_coef += scaling_list_delta_coef;
                    }
                    coeff[i] = next_coef;
                }
            }
        }

        //dc
        if (id >= SL_START_16x16) {
            if (!scaling_list_copy_mode_flag && !scaling_list_pred_mode_flag) {
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = 8;
            } else if (!scaling_list_pred_id_delta) {
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = 16;
            } else {
                const int ref_id = id - scaling_list_pred_id_delta;
                if (ref_id >= SL_START_16x16)
                    dc += sl->scaling_matrix_dc_rec[ref_id - SL_START_16x16];
                else
                    dc += sl->scaling_matrix_rec[ref_id][0];
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = dc & 255;
            }
        }

        //ac
        scaling_list = scaling_list_copy_mode_flag ? ff_vvc_scaling_list0 : coeff;
        if (!scaling_list_copy_mode_flag && !scaling_list_pred_mode_flag)
            pred = ff_vvc_scaling_pred_8;
        else if (!scaling_list_pred_id_delta)
            pred = ff_vvc_scaling_pred_16;
        else
            pred = sl->scaling_matrix_rec[id - scaling_list_pred_id_delta];
        for (int i = 0; i < list_size; i++) {
            const int x = ff_vvc_diag_scan_x[log2_size][log2_size][i];
            const int y = ff_vvc_diag_scan_y[log2_size][log2_size][i];
            const int off = y * matrix_size + x;
            sl->scaling_matrix_rec[id][off] = (pred[off] + scaling_list[i]) & 255;
        }
    }

    return 0;
}

int ff_vvc_decode_aps(VVCParamSets *ps, GetBitContext *gb, void *log_ctx)
{
    int ret = 0;
    const int aps_params_type                 = get_bits(gb, 3);
    const int aps_adaptation_parameter_set_id = get_bits(gb, 5);

    const int max_id[]  = { VVC_MAX_ALF_COUNT, VVC_MAX_LMCS_COUNT, VVC_MAX_SL_COUNT };
    const int size[]    = { sizeof(VVCALF), sizeof(VVCLMCS), sizeof(VVCScalingList) };
    AVBufferRef **list[] = { ps->alf_list, ps->lmcs_list, ps->scaling_list };
    AVBufferRef *buf;

    if (aps_params_type >= FF_ARRAY_ELEMS(max_id)) {
        av_log(log_ctx, AV_LOG_INFO, "Skipping APS type %d\n", aps_params_type);
        return 0;
    }
    if (aps_adaptation_parameter_set_id >= max_id[aps_params_type]) {
        av_log(log_ctx, AV_LOG_WARNING, "aps_adaptation_parameter_set_id(%d) should <= %d\n",
            aps_adaptation_parameter_set_id, max_id[aps_params_type]);
        return AVERROR_INVALIDDATA;
    }

    buf = av_buffer_allocz(size[aps_params_type]);
    if (!buf)
        return AVERROR(ENOMEM);

    switch (aps_params_type) {
        case APS_ALF:
            ret = aps_alf_parse((VVCALF *)buf->data, gb, log_ctx);
            break;
        case APS_LMCS:
            ret = aps_lmcs_parse((VVCLMCS *)buf->data, gb, log_ctx);
            break;
        case APS_SCALING:
            ret = aps_scaling_parse((VVCScalingList *)buf->data, gb, log_ctx);
            break;
        default:
            av_log(log_ctx, AV_LOG_INFO, "Skipping APS type %d\n", aps_params_type);
            goto fail;
    }
    if (ret < 0)
        goto fail;

    get_bits1(gb);   // aps_extension_flag

    if (get_bits_left(gb) < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Overread aps type %d by %d bits\n", aps_params_type, -get_bits_left(gb));
        ret = AVERROR_INVALIDDATA;
        goto fail;
    }

    ret = av_buffer_replace(&list[aps_params_type][aps_adaptation_parameter_set_id], buf);

fail:
    av_buffer_unref(&buf);
    return ret;
}


void ff_vvc_ps_uninit(VVCParamSets *ps)
{
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(ps->scaling_list); i++)
        av_buffer_unref(&ps->scaling_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->lmcs_list); i++)
        av_buffer_unref(&ps->lmcs_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->alf_list); i++)
        av_buffer_unref(&ps->alf_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->vps_list); i++)
        av_buffer_unref(&ps->vps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->sps_list); i++)
        av_buffer_unref(&ps->sps_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->pps_list); i++)
        av_buffer_unref(&ps->pps_list[i]);
    av_buffer_unref(&ps->ph_buf);
}

static int sh_parse_subpic_idx(VVCSH *sh, const VVCParamSets *ps, GetBitContext *gb)
{
    const VVCSPS *sps = ps->sps;
    const VVCPPS *pps = ps->pps;
    if (!sps->subpic_info_present_flag)
        return 0;

    sh->subpic_id = get_bits(gb, sps->subpic_id_len );
    if (!sps->subpic_id_mapping_explicitly_signalled_flag)
        return sh->subpic_id;

    for (int i = 0; i < sps->num_subpics; i++) {
        if (pps->subpic_id[i] == sh->subpic_id)
            return i;
    }
    return sps->num_subpics;
}

static void sh_parse_extra_bits(const VVCSPS *sps, GetBitContext *gb)
{
    for (int i = 0; i < sps->num_extra_sh_bytes * 8; i++) {
        if (sps->extra_sh_bit_present_flag[i])
            get_bits1(gb);
    }
}

static int sh_parse_slice_address(VVCSH *sh, VVCContext *s, const int curr_subpic_idx, GetBitContext *gb)
{
    void *log_ctx           = s->avctx;
    const VVCSPS *sps       = s->ps.sps;
    const VVCPPS *pps       = s->ps.pps;
    int num_slices_in_subpic = pps->num_slices_in_subpic[curr_subpic_idx];

    sh->slice_address = 0;
    if ((pps->rect_slice_flag && num_slices_in_subpic > 1) ||
        (!pps->rect_slice_flag && pps->num_tiles_in_pic > 1)) {
        unsigned int bits, max;
        if (!pps->rect_slice_flag) {
            bits = av_ceil_log2(pps->num_tiles_in_pic);
            max = pps->num_tiles_in_pic - 1;
        } else {
            bits = av_ceil_log2(num_slices_in_subpic);
            max = num_slices_in_subpic - 1;
        }
        u(bits, sh->slice_address, 0, max);
    }

    sh_parse_extra_bits(sps, gb);

    if (pps->rect_slice_flag) {
        int pic_level_slice_idx = sh->slice_address;
        for (int j = 0; j < curr_subpic_idx; j++)
            pic_level_slice_idx += pps->num_slices_in_subpic[j];
        sh->ctb_addr_in_curr_slice = pps->ctb_addr_in_slice + pps->slice_start_offset[pic_level_slice_idx];
        sh->num_ctus_in_curr_slice = pps->num_ctus_in_slice[pic_level_slice_idx];
    } else {
        int tile_x = sh->slice_address % pps->num_tile_columns;
        int tile_y = sh->slice_address / pps->num_tile_columns;
        const int slice_start_ctb = pps->row_bd[tile_y] * pps->ctb_width + pps->col_bd[tile_x] * pps->row_height[tile_y];
        sh->num_tiles_in_slice = 1;
        if (pps->num_tiles_in_pic - sh->slice_address > 1)
            uep(sh->num_tiles_in_slice, 1, 1, pps->num_tiles_in_pic - sh->slice_address);
        sh->ctb_addr_in_curr_slice = pps->ctb_addr_in_slice + slice_start_ctb;

        sh->num_ctus_in_curr_slice = 0;
        for (int tile_idx = sh->slice_address; tile_idx < sh->slice_address + sh->num_tiles_in_slice; tile_idx++) {
            tile_x = tile_idx % pps->num_tile_columns;
            tile_y = tile_idx / pps->num_tile_columns;
            sh->num_ctus_in_curr_slice += pps->row_height[tile_y] * pps->column_width[tile_x];
        }
    }

    return 0;
}

static int sh_parse_nb_refs(const VVCSH *sh, uint8_t *nb_refs, const VVCPPS *pps, GetBitContext* gb, void *log_ctx)
{
    if ((!IS_I(sh)&& sh->rpls[0].num_ref_entries > 1) ||
        (IS_B(sh) &&  sh->rpls[1].num_ref_entries > 1)) {
        int num_ref_idx_active_override_flag = get_bits1(gb);
        for (int i = 0; i < (IS_B(sh) ? 2 : 1); i++) {
            if (num_ref_idx_active_override_flag) {
                nb_refs[i] = 1;
                if (sh->rpls[i].num_ref_entries > 1) {
                    nb_refs[i] = get_ue_golomb_long(gb) + 1;
                    if (nb_refs[i] > 15) {
                        av_log(log_ctx, AV_LOG_ERROR,
                            "num_ref_idx_active_minus1(%d) should in range [0, 14]\n",
                            nb_refs[i] - 1);
                        return AVERROR_INVALIDDATA;
                    }
                }
            } else {
                nb_refs[i] =
                    FFMIN(pps->num_ref_idx_default_active[i], sh->rpls[i].num_ref_entries);
            }
        }
    } else {
        nb_refs[0] = !IS_I(sh);
        nb_refs[1] = IS_B(sh);
    }
    return 0;
}

static int sh_parse_alf(VVCSH *sh, VVCContext *s, GetBitContext *gb)
{
    int ret;
    const VVCParamSets *ps = &s->ps;

    sh->alf.enabled_flag[LUMA] = 0;
    if (ps->sps->alf_enabled_flag) {
        if (!ps->pps->alf_info_in_ph_flag) {
            ret = alf_parse(&sh->alf, ps, gb, s->avctx);
            if (ret < 0)
                return ret;
        } else {
            sh->alf = ps->ph->alf;
        }
    }
    return 0;
}

static int sh_parse_inter(VVCSH *sh, VVCContext *s, GetBitContext *gb)
{
    int ret;
    void *log_ctx = s->avctx;
    const VVCParamSets *ps = &s->ps;
    const VVCSPS *sps      = ps->sps;
    const VVCPPS *pps      = ps->pps;
    const VVCPH  *ph       = ps->ph;

    if (!pps->rpl_info_in_ph_flag && (!IS_IDR(s) || sps->idr_rpl_present_flag)) {
        ret = rpls_list_parse(sh->rpls, ps, ph->poc, gb, log_ctx);
        if (ret < 0)
            return ret;
    } else {
        memcpy(&sh->rpls, &ph->rpls, sizeof(ph->rpls));
    }

    ret = sh_parse_nb_refs(sh, sh->nb_refs, pps, gb, log_ctx);
    if (ret < 0)
        return ret;

    sh->cabac_init_flag = 0;
    if (!IS_I(sh)) {
        if (pps->cabac_init_present_flag)
            sh->cabac_init_flag = get_bits1(gb);
        if (ph->temporal_mvp_enabled_flag && !pps->rpl_info_in_ph_flag) {
            sh->collocated_list = L0;
            if (IS_B(sh))
                sh->collocated_list = !get_bits1(gb);
            if (sh->nb_refs[sh->collocated_list] > 1) {
                ue(sh->collocated_ref_idx, sh->nb_refs[sh->collocated_list] - 1);
            }
            if (!pps->wp_info_in_ph_flag &&
                ((pps->weighted_pred_flag  && IS_P(sh)) ||
                (pps->weighted_bipred_flag && IS_B(sh)))) {
                ret = hls_pred_weight_table(&sh->pwt, sh->nb_refs, sh->rpls,
                    sps, pps, gb, log_ctx);
                if (ret < 0)
                    return ret;
            }
        }

    }
    return 0;
}

static int sh_parse_slice_qp_y(VVCSH *sh, const VVCParamSets *ps, GetBitContext* gb, void *log_ctx)
{
    const VVCSPS *sps   = ps->sps;
    const VVCPPS *pps   = ps->pps;
    const VVCPH *ph     = ps->ph;
    if (!pps->qp_delta_info_in_ph_flag) {
        int8_t sh_qp_delta;
        se(sh_qp_delta, -6 * (sps->bit_depth - 8) - pps->init_qp,
           63 - pps->init_qp);
        sh->slice_qp_y = pps->init_qp + sh_qp_delta;
    } else {
        sh->slice_qp_y = pps->init_qp + ph->qp_delta;
    }
    return 0;
}

static int sh_parse_chroma_qp_offsets(VVCSH *sh, const VVCParamSets *ps, GetBitContext* gb, void *log_ctx)
{
    const VVCSPS *sps   = ps->sps;
    const VVCPPS *pps   = ps->pps;

    memset(sh->chroma_qp_offset, 0, sizeof(sh->chroma_qp_offset));

    if (pps->slice_chroma_qp_offsets_present_flag) {
        int8_t off;
        for (int i = CB - 1; i < CR + sps->joint_cbcr_enabled_flag; i++) {
            se(sh->chroma_qp_offset[i], -12, 12);
            off = pps->chroma_qp_offset[i] + sh->chroma_qp_offset[i];
            if (off < -12 || off > 12) {
                av_log(log_ctx, AV_LOG_ERROR,
                    "chroma_qp_offset(%d) not in range [-12, 12].\n", off);
                return AVERROR_INVALIDDATA;
            }
        }
    }

    sh->cu_chroma_qp_offset_enabled_flag = 0;
    if (pps->cu_chroma_qp_offset_list_enabled_flag)
        sh->cu_chroma_qp_offset_enabled_flag = get_bits1(gb);
    return 0;
}

static void sh_parse_sao_used_flag(VVCSH *sh, const VVCParamSets *ps, GetBitContext *gb)
{
    const VVCSPS *sps   = ps->sps;
    const VVCPPS *pps   = ps->pps;
    const VVCPH *ph     = ps->ph;
    if (sps->sao_enabled_flag && !pps->sao_info_in_ph_flag) {
        const int has_chroma = sps->chroma_format_idc != 0;
        sh->sao_used_flag[0] = get_bits1(gb);
        sh->sao_used_flag[1] = has_chroma ? get_bits1(gb) : 0;
    } else {
        sh->sao_used_flag[0] = ph->sao_luma_enabled_flag;
        sh->sao_used_flag[1] = ph->sao_chroma_enabled_flag;
    }
    sh->sao_used_flag[2] = sh->sao_used_flag[1];
}

static int sh_parse_deblock(VVCSH *sh, const VVCPPS *pps, const VVCPH *ph, GetBitContext *gb, void *log_ctx)
{
    int sh_deblocking_params_present_flag = 0;

    if (pps->deblocking_filter_override_enabled_flag && !pps->dbf_info_in_ph_flag)
        sh_deblocking_params_present_flag = get_bits1(gb);

    sh->deblocking_filter_disabled_flag = ph->deblocking_filter_disabled_flag;
    if (sh_deblocking_params_present_flag) {
        sh->deblocking_filter_disabled_flag = 0;
        if (!pps->deblocking_filter_disabled_flag)
            sh->deblocking_filter_disabled_flag = get_bits1(gb);

        if (!sh->deblocking_filter_disabled_flag)
            return deblock_parse(&sh->deblock,pps, gb, log_ctx);
    }
    memcpy(&sh->deblock, &ph->deblock, sizeof(sh->deblock));

    return 0;
}

static void sh_parse_transform(VVCSH *sh, const VVCSPS *sps, GetBitContext *gb)
{
    sh->dep_quant_used_flag = 0;
    if (sps->dep_quant_enabled_flag)
        sh->dep_quant_used_flag = get_bits1(gb);

    sh->sign_data_hiding_used_flag = 0;
    if (sps->sign_data_hiding_enabled_flag && !sh->dep_quant_used_flag)
        sh->sign_data_hiding_used_flag = get_bits1(gb);

    sh->ts_residual_coding_disabled_flag = 0;
    if (sps->transform_skip_enabled_flag &&
        !sh->dep_quant_used_flag &&
        !sh->sign_data_hiding_used_flag)
        sh->ts_residual_coding_disabled_flag = get_bits1(gb);
}

static int sh_parse_entry_points(VVCSH *sh, const int curr_subpic_idx,
    const VVCParamSets *ps, GetBitContext *gb, void *log_ctx)
{
    const VVCSPS *sps   = ps->sps;
    const VVCPPS *pps   = ps->pps;

    sh->num_entry_points = 0;
    if (sps->entry_point_offsets_present_flag) {
        for (int i = 1; i <sh->num_ctus_in_curr_slice; i++) {
            const int pre_ctb_addr_x = sh->ctb_addr_in_curr_slice[i - 1] % pps->ctb_width;
            const int pre_ctb_addr_y = sh->ctb_addr_in_curr_slice[i - 1] / pps->ctb_width;
            const int ctb_addr_x     = sh->ctb_addr_in_curr_slice[i] % pps->ctb_width;
            const int ctb_addr_y     = sh->ctb_addr_in_curr_slice[i] / pps->ctb_width;
            if (pps->ctb_to_row_bd[ctb_addr_y] != pps->ctb_to_row_bd[pre_ctb_addr_y] ||
                pps->ctb_to_col_bd[ctb_addr_x] != pps->ctb_to_col_bd[pre_ctb_addr_x] ||
                (ctb_addr_y != pre_ctb_addr_y && sps->entropy_coding_sync_enabled_flag)) {
                sh->entry_point_start_ctu[sh->num_entry_points] = i;
                sh->num_entry_points++;
            }
        }

        if (sh->num_entry_points > VVC_MAX_ENTRY_POINTS) {
            avpriv_request_sample(log_ctx, "Too many entry points: "
                   "%"PRIu16".\n", sh->num_entry_points);
            return AVERROR_PATCHWELCOME;
        }
    }

    if (sh->num_entry_points > 0) {
        int sh_entry_offset_len_minus1;
        ue(sh_entry_offset_len_minus1, 31);
        for (int i = 0; i < sh->num_entry_points; i++)
            sh->entry_point_offset[i] = get_bits(gb, sh_entry_offset_len_minus1 + 1) + 1;
    }

    return 0;
}

static int sh_parse_param_set(VVCContext *s, const int sh_picture_header_in_slice_header_flag,
    const int is_first_slice, GetBitContext *gb)
{
    VVCPH *ph                   = s->ps.ph;
    int ret = 0;

    if (!(sh_picture_header_in_slice_header_flag && is_first_slice) && !ph)
        return AVERROR_INVALIDDATA;

    if (sh_picture_header_in_slice_header_flag){
        if (is_first_slice) {
            ret = ff_vvc_decode_ph(&s->ps, s->pocTid0, IS_CLVSS(s), gb, s->avctx);
            if (ret < 0)
                return ret;
        } else {
            skip_bits(gb, ph->size);
        }
    }

    return ret;
}

int ff_vvc_decode_sh(VVCSH *sh, VVCContext *s, const int is_first_slice, GetBitContext *gb)
{
    void *log_ctx = s->avctx;
    VVCParamSets *ps   = &s->ps;
    const int sh_picture_header_in_slice_header_flag = get_bits1(gb);
    int ret, curr_subpic_idx;

    ret = sh_parse_param_set(s, sh_picture_header_in_slice_header_flag, is_first_slice, gb);
    if (ret < 0)
        return ret;

    curr_subpic_idx = sh_parse_subpic_idx(sh, ps, gb);
    if (curr_subpic_idx >= ps->sps->num_subpics) {
        av_log(log_ctx, AV_LOG_ERROR,
               "sh->subpic_id(%d) should in range [0, %d]\n",
               curr_subpic_idx, ps->sps->num_subpics - 1);
        return AVERROR_INVALIDDATA;
    }

    ret = sh_parse_slice_address(sh, s, curr_subpic_idx, gb);
    if (ret < 0)
        return ret;

    sh->slice_type = VVC_SLICE_TYPE_I;
    if (ps->ph->inter_slice_allowed_flag)
        ue(sh->slice_type, VVC_SLICE_TYPE_I);

    if (IS_CVSS(s))
        sh->no_output_of_prior_pics_flag = get_bits1(gb);

    ret = sh_parse_alf(sh, s, gb);
    if (ret < 0)
        return ret;

    if (ps->ph->lmcs_enabled_flag && !sh_picture_header_in_slice_header_flag)
        sh->lmcs_used_flag = get_bits1(gb);
    else
        sh->lmcs_used_flag = sh_picture_header_in_slice_header_flag ? ps->ph->lmcs_enabled_flag : 0;

    sh->explicit_scaling_list_used_flag = 0;
    if (ps->ph->explicit_scaling_list_enabled_flag) {
        sh->explicit_scaling_list_used_flag = 1;
        if (!sh_picture_header_in_slice_header_flag)
            sh->explicit_scaling_list_used_flag = get_bits1(gb);
    }

    ret = sh_parse_inter(sh, s, gb);
    if (ret <  0)
        return ret;

    ret = sh_parse_slice_qp_y(sh, ps, gb, log_ctx);
    if (ret < 0)
        return ret;

    ret = sh_parse_chroma_qp_offsets(sh, ps, gb, log_ctx);
    if (ret < 0)
        return ret;

    sh_parse_sao_used_flag(sh, ps, gb);

    ret = sh_parse_deblock(sh, ps->pps, ps->ph, gb, log_ctx);
    if (ret < 0)
        return ret;

    sh_parse_transform(sh, ps->sps, gb);

    if (ps->pps->slice_header_extension_present_flag) {
        int sh_slice_header_extension_length;
        ue(sh_slice_header_extension_length, 256);
        skip_bits(gb, 8 * sh_slice_header_extension_length);
    }

    ret = sh_parse_entry_points(sh, curr_subpic_idx, ps, gb, log_ctx);
    if (ret < 0)
        return ret;

    skip_bits(gb, 1);
    align_get_bits(gb);

    if (get_bits_left(gb) < 0) {
        av_log(log_ctx, AV_LOG_ERROR,
               "Overread slice header by %d bits\n", -get_bits_left(gb));
        return AVERROR_INVALIDDATA;
    }

    //get calculated values
    {
        const PartitionConstraints *constraints[2] = { &ps->ph->inter_slice, &ps->ph->inter_slice };
        if (IS_I(sh)) {
            constraints[LUMA]   = &ps->ph->intra_slice_luma;
            constraints[CHROMA] = &ps->ph->intra_slice_chroma;
        }

        sh->cu_qp_delta_subdiv = ps->ph->cu_qp_delta_subdiv[!IS_I(sh)];
        sh->cu_chroma_qp_offset_subdiv = ps->ph->cu_chroma_qp_offset_subdiv[!IS_I(sh)];

        for (int i = LUMA; i <= CHROMA; i++) {
            const PartitionConstraints *pc = constraints[i];
            const int min_qt_log2_size = ps->sps->min_cb_log2_size_y + pc->log2_diff_min_qt_min_cb;

            sh->max_bt_size[i]   = 1 << (min_qt_log2_size + pc->log2_diff_max_bt_min_qt);
            sh->max_tt_size[i]   = 1 << (min_qt_log2_size + pc->log2_diff_max_tt_min_qt);
            sh->max_mtt_depth[i] = pc->max_mtt_hierarchy_depth;
            sh->min_qt_size[i]   = 1 << min_qt_log2_size;
        }

    }
    return 0;
}
