/*
 * VVC parameter set parser
 *
 * Copyright (C) 2023 Nuo Mi
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
#include "libavcodec/cbs_h266.h"
#include "libavutil/imgutils.h"
#include "vvc_data.h"
#include "vvc_ps.h"
#include "vvcdec.h"


typedef void (*free_fn)(uint8_t *data);

static void ps_free(void *opaque, uint8_t *data)
{
    free_fn free = (free_fn)opaque;

    free(data);
    av_freep(&data);
}

static AVBufferRef* ps_alloc(size_t size, free_fn free)
{
    AVBufferRef *buf;
    uint8_t *data = av_mallocz(size);

    if (!data)
        return NULL;

    buf = av_buffer_create(data, size, ps_free, free, 0);
    if (!buf)
        av_freep(&data);

    return buf;
}

static int sps_map_pixel_format(VVCSPS *sps, void *log_ctx)
{
    const H266RawSPS *r = sps->r;
    const AVPixFmtDescriptor *desc;

    switch (sps->bit_depth) {
    case 8:
        if (r->sps_chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY8;
        if (r->sps_chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P;
        if (r->sps_chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P;
        if (r->sps_chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P;
       break;
    case 10:
        if (r->sps_chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY10;
        if (r->sps_chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P10;
        if (r->sps_chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P10;
        if (r->sps_chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P10;
        break;
    case 12:
        if (r->sps_chroma_format_idc == 0) sps->pix_fmt = AV_PIX_FMT_GRAY12;
        if (r->sps_chroma_format_idc == 1) sps->pix_fmt = AV_PIX_FMT_YUV420P12;
        if (r->sps_chroma_format_idc == 2) sps->pix_fmt = AV_PIX_FMT_YUV422P12;
        if (r->sps_chroma_format_idc == 3) sps->pix_fmt = AV_PIX_FMT_YUV444P12;
        break;
    default:
        av_log(log_ctx, AV_LOG_ERROR,
               "The following bit-depths are currently specified: 8, 10, 12 bits, "
               "chroma_format_idc is %d, depth is %d\n",
               r->sps_chroma_format_idc, sps->bit_depth);
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

static int sps_bit_depth(VVCSPS *sps, void *log_ctx)
{
    const H266RawSPS *r = sps->r;

    sps->bit_depth = r->sps_bitdepth_minus8 + 8;
    sps->qp_bd_offset = 6 * (sps->bit_depth - 8);
    sps->log2_transform_range =
        r->sps_extended_precision_flag ? FFMAX(15, FFMIN(20, sps->bit_depth + 6)) : 15;
    return sps_map_pixel_format(sps, log_ctx);
}

static int sps_chroma_qp_table(VVCSPS *sps)
{
    const H266RawSPS *r = sps->r;
    const int num_qp_tables = r->sps_same_qp_table_for_chroma_flag ?
        1 : (r->sps_joint_cbcr_enabled_flag ? 3 : 2);

    for (int i = 0; i < num_qp_tables; i++) {
        int num_points_in_qp_table;
        int8_t qp_in[VVC_MAX_POINTS_IN_QP_TABLE], qp_out[VVC_MAX_POINTS_IN_QP_TABLE];
        unsigned int delta_qp_in[VVC_MAX_POINTS_IN_QP_TABLE];
        int off = sps->qp_bd_offset;

        num_points_in_qp_table = r->sps_num_points_in_qp_table_minus1[i] + 1;

        qp_out[0] = qp_in[0] = r->sps_qp_table_start_minus26[i] + 26;
        for (int j = 0; j < num_points_in_qp_table; j++ ) {
            delta_qp_in[j] = r->sps_delta_qp_in_val_minus1[i][j] + 1;
            qp_in[j+1] = qp_in[j] + delta_qp_in[j];
            qp_out[j+1] = qp_out[j] + (r->sps_delta_qp_in_val_minus1[i][j] ^ r->sps_delta_qp_diff_val[i][j]);
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
    if (r->sps_same_qp_table_for_chroma_flag) {
        memcpy(&sps->chroma_qp_table[1], &sps->chroma_qp_table[0], sizeof(sps->chroma_qp_table[0]));
        memcpy(&sps->chroma_qp_table[2], &sps->chroma_qp_table[0], sizeof(sps->chroma_qp_table[0]));
    }

    return 0;
}

static void sps_poc(VVCSPS *sps)
{
    sps->max_pic_order_cnt_lsb = 1 << (sps->r->sps_log2_max_pic_order_cnt_lsb_minus4 + 4);
}

static void sps_inter(VVCSPS *sps)
{
    const H266RawSPS *r = sps->r;

    sps->max_num_merge_cand     = 6 - r->sps_six_minus_max_num_merge_cand;
    sps->max_num_ibc_merge_cand = 6 - r->sps_six_minus_max_num_ibc_merge_cand;

    if (sps->r->sps_gpm_enabled_flag) {
        sps->max_num_gpm_merge_cand = 2;
        if (sps->max_num_merge_cand >= 3)
            sps->max_num_gpm_merge_cand = sps->max_num_merge_cand - r->sps_max_num_merge_cand_minus_max_num_gpm_cand;
    }

    sps->log2_parallel_merge_level = r->sps_log2_parallel_merge_level_minus2 + 2;
}

static void sps_partition_constraints(VVCSPS* sps)
{
    const H266RawSPS *r = sps->r;

    sps->ctb_log2_size_y    = r->sps_log2_ctu_size_minus5 + 5;
    sps->ctb_size_y         = 1 << sps->ctb_log2_size_y;
    sps->min_cb_log2_size_y = r->sps_log2_min_luma_coding_block_size_minus2 + 2;
    sps->min_cb_size_y      = 1 << sps->min_cb_log2_size_y;
    sps->max_tb_size_y      = 1 << (r->sps_max_luma_transform_size_64_flag ? 6 : 5);
    sps->max_ts_size        = 1 << (r->sps_log2_transform_skip_max_size_minus2 + 2);
}

static void sps_ladf(VVCSPS* sps)
{
    const H266RawSPS *r = sps->r;

    if (r->sps_ladf_enabled_flag) {
        sps->num_ladf_intervals = r->sps_num_ladf_intervals_minus2 + 2;
        sps->ladf_interval_lower_bound[0] = 0;
        for (int i = 0; i < sps->num_ladf_intervals - 1; i++) {
            sps->ladf_interval_lower_bound[i + 1] =
                sps->ladf_interval_lower_bound[i] + r->sps_ladf_delta_threshold_minus1[i] + 1;
        }
    }
}

static int sps_derive(VVCSPS *sps, void *log_ctx)
{
    int ret;
    const H266RawSPS *r = sps->r;

    sps->width  = r->sps_pic_width_max_in_luma_samples;
    sps->height = r->sps_pic_height_max_in_luma_samples;

    ret = sps_bit_depth(sps, log_ctx);
    if (ret < 0)
        return ret;
    sps_poc(sps);
    sps_inter(sps);
    sps_partition_constraints(sps);
    sps_ladf(sps);
    if (r->sps_chroma_format_idc != 0)
        sps_chroma_qp_table(sps);

    return 0;
}

static void sps_free(uint8_t *data)
{
    VVCSPS *sps = (VVCSPS*)data;
    av_buffer_unref(&sps->rref);
}

static AVBufferRef *sps_alloc(const H266RawSPS *rsps, AVBufferRef *rsps_buf, void *log_ctx)
{
    int ret;
    VVCSPS *sps;
    AVBufferRef *sps_buf = ps_alloc(sizeof(*sps), sps_free);

    if (!sps_buf)
        return NULL;
    sps = (VVCSPS *)sps_buf->data;

    ret = av_buffer_replace(&sps->rref, rsps_buf);
    if (ret < 0)
        goto fail;
    sps->r = rsps;

    ret = sps_derive(sps, log_ctx);
    if (ret < 0)
        goto fail;

    return sps_buf;

fail:
    av_buffer_unref(&sps_buf);
    return NULL;
}

static int decode_sps(VVCParamSets *ps,
    const H266RawSPS *rsps, AVBufferRef *rsps_buf, void *log_ctx)
{
    int ret;
    const int sps_id = rsps->sps_seq_parameter_set_id;
    AVBufferRef *sps_buf = ps->sps_list[sps_id];

    if (sps_buf && ((VVCSPS *)sps_buf->data)->r == rsps)
        return 0;

    sps_buf = sps_alloc(rsps, rsps_buf, log_ctx);
    if (!sps_buf)
        return AVERROR(ENOMEM);

    ret = av_buffer_replace(&ps->sps_list[sps_id], sps_buf);

    av_buffer_unref(&sps_buf);
    return ret;
}

static void pps_chroma_qp_offset(VVCPPS *pps)
{
    pps->chroma_qp_offset[CB - 1]   = pps->r->pps_cb_qp_offset;
    pps->chroma_qp_offset[CR - 1]   = pps->r->pps_cr_qp_offset;
    pps->chroma_qp_offset[JCBCR - 1]= pps->r->pps_joint_cbcr_qp_offset_value;
    for (int i = 0; i < 6; i++) {
        pps->chroma_qp_offset_list[i][CB - 1]   = pps->r->pps_cb_qp_offset_list[i];
        pps->chroma_qp_offset_list[i][CR - 1]   = pps->r->pps_cr_qp_offset_list[i];
        pps->chroma_qp_offset_list[i][JCBCR - 1]= pps->r->pps_joint_cbcr_qp_offset_list[i];
    }
}

static void pps_width_height(VVCPPS *pps, const VVCSPS *sps)
{
    const H266RawPPS *r = pps->r;

    pps->width          = r->pps_pic_width_in_luma_samples;
    pps->height         = r->pps_pic_height_in_luma_samples;

    pps->ctb_width      = AV_CEIL_RSHIFT(pps->width,  sps->ctb_log2_size_y);
    pps->ctb_height     = AV_CEIL_RSHIFT(pps->height, sps->ctb_log2_size_y);
    pps->ctb_count      = pps->ctb_width * pps->ctb_height;

    pps->min_cb_width   = pps->width  >> sps->min_cb_log2_size_y;
    pps->min_cb_height  = pps->height >> sps->min_cb_log2_size_y;

    pps->min_pu_width   = pps->width  >> MIN_PU_LOG2;
    pps->min_pu_height  = pps->height >> MIN_PU_LOG2;
    pps->min_tu_width   = pps->width  >> MIN_TU_LOG2;
    pps->min_tu_height  = pps->height >> MIN_TU_LOG2;

    pps->width32        = AV_CEIL_RSHIFT(pps->width,  5);
    pps->height32       = AV_CEIL_RSHIFT(pps->height, 5);
    pps->width64        = AV_CEIL_RSHIFT(pps->width,  6);
    pps->height64       = AV_CEIL_RSHIFT(pps->height, 6);
}

static int pps_bd(VVCPPS *pps)
{
    const H266RawPPS *r = pps->r;

    pps->col_bd        = av_calloc(r->num_tile_columns  + 1, sizeof(*pps->col_bd));
    pps->row_bd        = av_calloc(r->num_tile_rows  + 1,    sizeof(*pps->row_bd));
    pps->ctb_to_col_bd = av_calloc(pps->ctb_width  + 1,      sizeof(*pps->ctb_to_col_bd));
    pps->ctb_to_row_bd = av_calloc(pps->ctb_height + 1,      sizeof(*pps->ctb_to_col_bd));
    if (!pps->col_bd || !pps->row_bd || !pps->ctb_to_col_bd || !pps->ctb_to_row_bd)
        return AVERROR(ENOMEM);

    for (int i = 0, j = 0; i < r->num_tile_columns; i++) {
        pps->col_bd[i] = j;
        j += r->col_width_val[i];
        for (int k = pps->col_bd[i]; k < j; k++)
            pps->ctb_to_col_bd[k] = pps->col_bd[i];
    }

    for (int i = 0, j = 0; i < r->num_tile_rows; i++) {
        pps->row_bd[i] = j;
        j += r->row_height_val[i];
        for (int k = pps->row_bd[i]; k < j; k++)
            pps->ctb_to_row_bd[k] = pps->row_bd[i];
    }
    return 0;
}


static int next_tile_idx(int tile_idx, const int i, const H266RawPPS *r)
{
    if (r->pps_tile_idx_delta_present_flag) {
        tile_idx += r->pps_tile_idx_delta_val[i];
    } else {
        tile_idx += r->pps_slice_width_in_tiles_minus1[i] + 1;
        if (tile_idx % r->num_tile_columns == 0)
            tile_idx += (r->pps_slice_height_in_tiles_minus1[i]) * r->num_tile_columns;
    }
    return tile_idx;
}

static void tile_xy(int *tile_x, int *tile_y, const int tile_idx, const VVCPPS *pps)
{
    *tile_x = tile_idx % pps->r->num_tile_columns;
    *tile_y = tile_idx / pps->r->num_tile_columns;
}

static void ctu_xy(int *ctu_x, int *ctu_y, const int tile_x, const int tile_y, const VVCPPS *pps)
{
    *ctu_x = pps->col_bd[tile_x];
    *ctu_y = pps->row_bd[tile_y];
}

static int ctu_rs(const int ctu_x, const int ctu_y, const VVCPPS *pps)
{
    return pps->ctb_width * ctu_y + ctu_x;
}

static int pps_add_ctus(VVCPPS *pps, int *off, const int ctu_x, const int ctu_y,
    const int w, const int h)
{
    int start = *off;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            pps->ctb_addr_in_slice[*off] = ctu_rs(ctu_x + x, ctu_y + y, pps);
            (*off)++;
        }
    }
    return *off - start;
}

static int pps_one_tile_slices(VVCPPS *pps, const int tile_idx, int i, int *off)
{
    const H266RawPPS *r = pps->r;
    int ctu_x, ctu_y, ctu_y_end, tile_x, tile_y;

    tile_xy(&tile_x, &tile_y, tile_idx, pps);
    ctu_xy(&ctu_x, &ctu_y, tile_x, tile_y, pps);
    ctu_y_end = ctu_y + r->row_height_val[tile_y];
    while (ctu_y < ctu_y_end) {
        pps->slice_start_offset[i] = *off;
        pps->num_ctus_in_slice[i] = pps_add_ctus(pps, off, ctu_x, ctu_y,
            r->col_width_val[tile_x], r->slice_height_in_ctus[i]);
        ctu_y += r->slice_height_in_ctus[i++];
    }
    i--;
    return i;
}

static void pps_multi_tiles_slice(VVCPPS *pps, const int tile_idx, const int i, int *off)
{
    const H266RawPPS *r = pps->r;
    int ctu_x, ctu_y,tile_x, tile_y;

    tile_xy(&tile_x, &tile_y, tile_idx, pps);
    pps->slice_start_offset[i] = *off;
    pps->num_ctus_in_slice[i] = 0;
    for (int ty = tile_y; ty <= tile_y + r->pps_slice_height_in_tiles_minus1[i]; ty++) {
        for (int tx = tile_x; tx <= tile_x + r->pps_slice_width_in_tiles_minus1[i]; tx++) {
            ctu_xy(&ctu_x, &ctu_y, tx, ty, pps);
            pps->num_ctus_in_slice[i] += pps_add_ctus(pps, off, ctu_x, ctu_y,
                r->col_width_val[tx], r->row_height_val[ty]);
        }
    }
}

static void pps_rect_slice(VVCPPS* pps)
{
    const H266RawPPS* r = pps->r;
    int tile_idx = 0, off = 0;

    for (int i = 0; i < r->pps_num_slices_in_pic_minus1 + 1; i++) {
        if (!r->pps_slice_width_in_tiles_minus1[i] &&
            !r->pps_slice_height_in_tiles_minus1[i]) {
            i = pps_one_tile_slices(pps, tile_idx, i, &off);
        } else {
            pps_multi_tiles_slice(pps, tile_idx, i, &off);

        }
        tile_idx = next_tile_idx(tile_idx, i, r);
    }
}

static void pps_no_rect_slice(VVCPPS* pps)
{
    const H266RawPPS* r = pps->r;
    int ctu_x, ctu_y, off = 0;

    for (int tile_y = 0; tile_y < r->num_tile_rows; tile_y++) {
        for (int tile_x = 0; tile_x < r->num_tile_columns; tile_x++) {
            ctu_xy(&ctu_x, &ctu_y, tile_x, tile_y, pps);
            pps_add_ctus(pps, &off, ctu_x, ctu_y, r->col_width_val[tile_x], r->row_height_val[tile_y]);
        }
    }
}

static int pps_slice_map(VVCPPS *pps)
{
    pps->ctb_addr_in_slice = av_calloc(pps->ctb_count, sizeof(*pps->ctb_addr_in_slice));
    if (!pps->ctb_addr_in_slice)
        return AVERROR(ENOMEM);

    if (pps->r->pps_rect_slice_flag)
        pps_rect_slice(pps);
    else
        pps_no_rect_slice(pps);

    return 0;
}

static void pps_ref_wraparound_offset(VVCPPS *pps, const VVCSPS *sps)
{
    const H266RawPPS *r = pps->r;

    if (r->pps_ref_wraparound_enabled_flag)
        pps->ref_wraparound_offset = (pps->width / sps->min_cb_size_y) - r->pps_pic_width_minus_wraparound_offset;
}

static int pps_derive(VVCPPS *pps, const VVCSPS *sps)
{
    int ret;

    pps_chroma_qp_offset(pps);
    pps_width_height(pps, sps);

    ret = pps_bd(pps);
    if (ret < 0)
        return ret;

    ret = pps_slice_map(pps);
    if (ret < 0)
        return ret;

    pps_ref_wraparound_offset(pps, sps);

    return 0;
}

static void pps_free(uint8_t *data)
{
    VVCPPS *pps = (VVCPPS *)data;

    av_buffer_unref(&pps->rref);

    av_freep(&pps->col_bd);
    av_freep(&pps->row_bd);
    av_freep(&pps->ctb_to_col_bd);
    av_freep(&pps->ctb_to_row_bd);
    av_freep(&pps->ctb_addr_in_slice);
}

static AVBufferRef *pps_alloc(const H266RawPPS *rpps, AVBufferRef *rpps_buf, const VVCSPS *sps)
{
    int ret;
    AVBufferRef *pps_buf;
    VVCPPS *pps;

    pps_buf = ps_alloc(sizeof(*pps), pps_free);
    if (!pps_buf)
        return NULL;
    pps = (VVCPPS *)pps_buf->data;

    ret = av_buffer_replace(&pps->rref, rpps_buf);
    if (ret < 0)
        goto fail;
    pps->r = rpps;

    ret = pps_derive(pps, sps);
    if (ret < 0)
        goto fail;

    return pps_buf;

fail:
    av_buffer_unref(&pps_buf);
    return NULL;
}

static int decode_pps(VVCParamSets *ps,
    const H266RawPPS *rpps, AVBufferRef *rpps_buf)
{
    AVBufferRef *pps_buf;
    int ret = 0;
    const int pps_id = rpps->pps_pic_parameter_set_id;
    const int sps_id = rpps->pps_seq_parameter_set_id;

    pps_buf = ps->pps_list[pps_id];
    if (pps_buf && ((VVCPPS*)pps_buf->data)->r == rpps)
        return 0;

    pps_buf = pps_alloc(rpps, rpps_buf, (VVCSPS*)(ps->sps_list[sps_id]->data));
    if (!pps_buf)
        return AVERROR(ENOMEM);

    ret = av_buffer_replace(&ps->pps_list[pps_id], pps_buf);

    av_buffer_unref(&pps_buf);
    return ret;
}

static int decode_ps(VVCParamSets *ps, const CodedBitstreamH266Context *h266, void *log_ctx)
{
    const H266RawPictureHeader *ph = h266->ph;
    const H266RawPPS *rpps;
    const H266RawSPS *rsps;
    AVBufferRef *rsps_buf, *rpps_buf;
    int ret;

    if (!ph)
        return AVERROR_INVALIDDATA;

    rpps     = h266->pps[ph->ph_pic_parameter_set_id];
    rpps_buf = h266->pps_ref[ph->ph_pic_parameter_set_id];
    if (!rpps || !rpps_buf)
        return AVERROR_INVALIDDATA;

    rsps     = h266->sps[rpps->pps_seq_parameter_set_id];
    rsps_buf = h266->sps_ref[rpps->pps_seq_parameter_set_id];
    if (!rsps || !rsps_buf)
        return AVERROR_INVALIDDATA;

    ret = decode_sps(ps, rsps, rsps_buf, log_ctx);
    if (ret < 0)
        return ret;

    ret = decode_pps(ps, rpps, rpps_buf);
    if (ret < 0)
        return ret;

    return 0;
}

#define WEIGHT_TABLE(x)                                                                                 \
    w->nb_weights[L##x] = r->num_weights_l##x;                                                          \
    for (int i = 0; i < w->nb_weights[L##x]; i++) {                                                     \
        w->weight_flag[L##x][LUMA][i]     = r->luma_weight_l##x##_flag[i];                              \
        w->weight_flag[L##x][CHROMA][i]   = r->chroma_weight_l##x##_flag[i];                            \
        w->weight[L##x][LUMA][i]          = denom[LUMA] + r->delta_luma_weight_l##x[i];                 \
        w->offset[L##x][LUMA][i]          = r->luma_offset_l##x[i];                                     \
        for (int j = CB; j <= CR; j++) {                                                                \
            w->weight[L##x][j][i]         = denom[CHROMA] + r->delta_chroma_weight_l##x[i][j - 1];      \
            w->offset[L##x][j][i]         = 128 + r->delta_chroma_offset_l##x[i][j - 1];                \
            w->offset[L##x][j][i]        -= (128 * w->weight[L##x][j][i]) >> w->log2_denom[CHROMA];     \
            w->offset[L##x][j][i]         = av_clip_intp2(w->offset[L##x][j][i], 7);                    \
        }                                                                                               \
    }                                                                                                   \

static void pred_weight_table(PredWeightTable *w, const H266RawPredWeightTable *r)
{
    int denom[2];

    w->log2_denom[LUMA] = r->luma_log2_weight_denom;
    w->log2_denom[CHROMA] = w->log2_denom[LUMA] + r->delta_chroma_log2_weight_denom;
    denom[LUMA] = 1 << w->log2_denom[LUMA];
    denom[CHROMA] = 1 << w->log2_denom[CHROMA];
    WEIGHT_TABLE(0)
    WEIGHT_TABLE(1)
}

// 8.3.1 Decoding process for picture order count
static int ph_compute_poc(const H266RawPictureHeader *ph, const H266RawSPS *sps, const int poc_tid0, const int is_clvss)
{
    const int max_poc_lsb       = 1 << (sps->sps_log2_max_pic_order_cnt_lsb_minus4 + 4);
    const int prev_poc_lsb      = poc_tid0 % max_poc_lsb;
    const int prev_poc_msb      = poc_tid0 - prev_poc_lsb;
    const int poc_lsb           = ph->ph_pic_order_cnt_lsb;
    int poc_msb;

    if (ph->ph_poc_msb_cycle_present_flag) {
        poc_msb = ph->ph_poc_msb_cycle_val * max_poc_lsb;
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

static av_always_inline uint16_t lmcs_derive_lut_sample(uint16_t sample,
    uint16_t *pivot1, uint16_t *pivot2, uint16_t *scale_coeff, const int idx, const int max)
{
    const int lut_sample =
        pivot1[idx] + ((scale_coeff[idx] * (sample - pivot2[idx]) + (1<< 10)) >> 11);
    return av_clip(lut_sample, 0, max - 1);
}

//8.8.2.2 Inverse mapping process for a luma sample
static int lmcs_derive_lut(VVCLMCS *lmcs, const AVBufferRef *lmcs_buf, const H266RawSPS *sps)
{
    const H266RawAPS *rlmcs;
    const int bit_depth = (sps->sps_bitdepth_minus8 + 8);
    const int max       = (1 << bit_depth);
    const int org_cw    = max / LMCS_MAX_BIN_SIZE;
    const int shift     = av_log2(org_cw);
    const int off       = 1 << (shift - 1);
    int cw[LMCS_MAX_BIN_SIZE];
    uint16_t input_pivot[LMCS_MAX_BIN_SIZE];
    uint16_t scale_coeff[LMCS_MAX_BIN_SIZE];
    uint16_t inv_scale_coeff[LMCS_MAX_BIN_SIZE];
    int i, delta_crs;
    if (bit_depth > LMCS_MAX_BIT_DEPTH)
        return AVERROR_PATCHWELCOME;

    if (!lmcs_buf)
        return AVERROR_INVALIDDATA;
    rlmcs = (const H266RawAPS *)lmcs_buf->data;


    lmcs->min_bin_idx = rlmcs->lmcs_min_bin_idx;
    lmcs->max_bin_idx = LMCS_MAX_BIN_SIZE - 1 - rlmcs->lmcs_min_bin_idx;

    memset(cw, 0, sizeof(cw));
    for (int i = lmcs->min_bin_idx; i <= lmcs->max_bin_idx; i++)
        cw[i] = org_cw + (1 - 2 * rlmcs->lmcs_delta_sign_cw_flag[i]) * rlmcs->lmcs_delta_abs_cw[i];

    delta_crs = (1 - 2 * rlmcs->lmcs_delta_sign_crs_flag) * rlmcs->lmcs_delta_abs_crs;

    lmcs->pivot[0] = 0;
    for (i = 0; i < LMCS_MAX_BIN_SIZE; i++) {
        input_pivot[i]        = i * org_cw;
        lmcs->pivot[i + 1] = lmcs->pivot[i] + cw[i];
        scale_coeff[i]        = (cw[i] * (1 << 11) +  off) >> shift;
        if (cw[i] == 0) {
            inv_scale_coeff[i] = 0;
            lmcs->chroma_scale_coeff[i] = (1 << 11);
        } else {
            inv_scale_coeff[i] = org_cw * (1 << 11) / cw[i];
            lmcs->chroma_scale_coeff[i] = org_cw * (1 << 11) / (cw[i] + delta_crs);
        }
    }

    //derive lmcs_fwd_lut
    for (uint16_t sample = 0; sample < max; sample++) {
        const int idx_y = sample / org_cw;
        const uint16_t fwd_sample = lmcs_derive_lut_sample(sample, lmcs->pivot,
            input_pivot, scale_coeff, idx_y, max);
        if (bit_depth > 8)
            ((uint16_t *)lmcs->fwd_lut)[sample] = fwd_sample;
        else
            lmcs->fwd_lut[sample] = fwd_sample;

    }

    //derive lmcs_inv_lut
    i = lmcs->min_bin_idx;
    for (uint16_t sample = 0; sample < max; sample++) {
        uint16_t inv_sample;
        while (sample >= lmcs->pivot[i + 1] && i <= lmcs->max_bin_idx)
            i++;

        inv_sample = lmcs_derive_lut_sample(sample, input_pivot, lmcs->pivot,
            inv_scale_coeff, i, max);

        if (bit_depth > 8)
            ((uint16_t *)lmcs->inv_lut)[sample] = inv_sample;
        else
            lmcs->inv_lut[sample] = inv_sample;
    }

    return 0;
}

static int ph_max_num_subblock_merge_cand(const H266RawSPS *sps, const H266RawPictureHeader *ph)
{
    if (sps->sps_affine_enabled_flag)
        return 5 - sps->sps_five_minus_max_num_subblock_merge_cand;
    return sps->sps_sbtmvp_enabled_flag && ph->ph_temporal_mvp_enabled_flag;
}

static int ph_derive(VVCPH *ph, const H266RawSPS *sps, const H266RawPPS *pps, const int poc_tid0, const int is_clvss)
{
    ph->max_num_subblock_merge_cand = ph_max_num_subblock_merge_cand(sps, ph->r);

    ph->poc = ph_compute_poc(ph->r, sps, poc_tid0, is_clvss);

    if (pps->pps_wp_info_in_ph_flag)
        pred_weight_table(&ph->pwt, &ph->r->ph_pred_weight_table);

    return 0;
}

static int decode_ph(VVCFrameParamSets *fps, const H266RawPictureHeader *rph, AVBufferRef *rph_ref,
    const int poc_tid0, const int is_clvss)
{
    int ret;
    VVCPH *ph = &fps->ph;
    const H266RawSPS *sps = fps->sps->r;
    const H266RawPPS *pps = fps->pps->r;

    ret = av_buffer_replace(&ph->rref, rph_ref);
    if (ret < 0)
        return ret;
    ph->r = rph;

    ret = ph_derive(ph, sps, pps, poc_tid0, is_clvss);
    if (ret < 0)
        return ret;

    return 0;
}

static int decode_scaling_list(VVCFrameParamSets *fps, AVBufferRef *sl_buf)
{
    int ret;

    if (!sl_buf)
        return AVERROR_INVALIDDATA;

    ret = av_buffer_replace(&fps->sl_buf, sl_buf);
    if (ret < 0)
        return  ret;

    fps->sl = (const VVCScalingList*)sl_buf->data;

    return 0;
}

static int decode_frame_ps(VVCFrameParamSets *fps, const VVCParamSets *ps,
    const CodedBitstreamH266Context *h266, const int poc_tid0, const int is_clvss)
{
    const H266RawPictureHeader *ph = h266->ph;
    const H266RawPPS *rpps;
    int ret;

    if (!ph)
        return AVERROR_INVALIDDATA;

    rpps = h266->pps[ph->ph_pic_parameter_set_id];
    if (!rpps)
        return AVERROR_INVALIDDATA;

    ret = av_buffer_replace(&fps->sps_buf, ps->sps_list[rpps->pps_seq_parameter_set_id]);
    if (ret < 0)
        return ret;
    fps->sps = (VVCSPS *)fps->sps_buf->data;

    ret = av_buffer_replace(&fps->pps_buf, ps->pps_list[rpps->pps_pic_parameter_set_id]);
    if (ret < 0)
        return ret;
    fps->pps = (VVCPPS *)fps->pps_buf->data;

    ret = decode_ph(fps, ph, h266->ph_ref, poc_tid0, is_clvss);
    if (ret < 0)
        return ret;

    if (ph->ph_explicit_scaling_list_enabled_flag) {
        ret = decode_scaling_list(fps, ps->scaling_list[ph->ph_scaling_list_aps_id]);
        if (ret < 0)
            return ret;
    }

    if (ph->ph_lmcs_enabled_flag) {
        ret = lmcs_derive_lut(&fps->lmcs, ps->lmcs_list[ph->ph_lmcs_aps_id], fps->sps->r);
        if (ret < 0)
            return ret;
    }

    for (int i = 0; i < FF_ARRAY_ELEMS(fps->alf_list); i++) {
        ret = av_buffer_replace(&fps->alf_list[i], ps->alf_list[i]);
        if (ret < 0)
            return ret;
    }
    return 0;
}

static void decode_recovery_flag(VVCContext *s)
{
    if (IS_IDR(s))
        s->no_output_before_recovery_flag = 0;
    else if (IS_CRA(s) || IS_GDR(s))
        s->no_output_before_recovery_flag = s->last_eos;
}

static void decode_recovery_poc(VVCContext *s, const VVCPH *ph)
{
    if (s->no_output_before_recovery_flag) {
        if (IS_GDR(s))
            s->gdr_recovery_point_poc = ph->poc + ph->r->ph_recovery_poc_cnt;
        if (!GDR_IS_RECOVERED(s) && s->gdr_recovery_point_poc <= ph->poc)
            GDR_SET_RECOVERED(s);
    }
}

int ff_vvc_decode_frame_ps(VVCFrameParamSets *fps, struct VVCContext *s)
{
    int ret = 0;
    VVCParamSets *ps                        = &s->ps;
    const CodedBitstreamH266Context *h266   = s->cbc->priv_data;

    ret = decode_ps(ps, h266, s->avctx);
    if (ret < 0)
        return ret;

    decode_recovery_flag(s);
    ret = decode_frame_ps(fps, ps, h266, s->poc_tid0, IS_CLVSS(s));
    decode_recovery_poc(s, &fps->ph);
    return ret;
}

void ff_vvc_frame_ps_free(VVCFrameParamSets *fps)
{
    av_buffer_unref(&fps->sps_buf);
    av_buffer_unref(&fps->pps_buf);
    av_buffer_unref(&fps->ph.rref);
    av_buffer_unref(&fps->sl_buf);
    for (int i = 0; i < FF_ARRAY_ELEMS(fps->alf_list); i++)
        av_buffer_unref(&fps->alf_list[i]);
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
}

enum {
    APS_ALF,
    APS_LMCS,
    APS_SCALING,
};

static void alf_coeff(int16_t *coeff,
    const uint8_t *abs, const uint8_t *sign, const int size)
{
    for (int i = 0; i < size; i++)
        coeff[i] = (1 - 2 * sign[i]) * abs[i];
}

static void alf_coeff_cc(int16_t *coeff,
    const uint8_t *mapped_abs, const uint8_t *sign)
{
    for (int i = 0; i < ALF_NUM_COEFF_CC; i++) {
        int c = mapped_abs[i];;
        if (c)
            c = (1 - 2 * sign[i]) * (1 << (c - 1));
        coeff[i] = c;
    }
}

static void alf_luma(VVCALF *alf)
{
    const H266RawAPS *aps = (const H266RawAPS *)alf->rref->data;

    if (!aps->alf_luma_filter_signal_flag)
        return;

    for (int i = 0; i < ALF_NUM_FILTERS_LUMA; i++) {
        const int ref       = aps->alf_luma_coeff_delta_idx[i];
        const uint8_t *abs  = aps->alf_luma_coeff_abs[ref];
        const uint8_t *sign = aps->alf_luma_coeff_sign[ref];

        alf_coeff(alf->luma_coeff[i], abs, sign, ALF_NUM_COEFF_LUMA);
        memcpy(alf->luma_clip_idx[i], aps->alf_luma_clip_idx[ref],
            sizeof(alf->luma_clip_idx[i]));
    }
}

static void alf_chroma(VVCALF *alf)
{
    const H266RawAPS *aps = (const H266RawAPS *)alf->rref->data;


    if (!aps->alf_chroma_filter_signal_flag)
        return;

    alf->num_chroma_filters  = aps->alf_chroma_num_alt_filters_minus1 + 1;
    for (int i = 0; i < alf->num_chroma_filters; i++) {
        const uint8_t *abs  = aps->alf_chroma_coeff_abs[i];
        const uint8_t *sign = aps->alf_chroma_coeff_sign[i];

        alf_coeff(alf->chroma_coeff[i], abs, sign, ALF_NUM_COEFF_CHROMA);
        memcpy(alf->chroma_clip_idx[i], aps->alf_chroma_clip_idx[i],
            sizeof(alf->chroma_clip_idx[i]));
    }
}

static void alf_cc(VVCALF *alf)
{
    const H266RawAPS *aps  = (const H266RawAPS *)alf->rref->data;
    const uint8_t (*abs[])[ALF_NUM_COEFF_CC] =
        { aps->alf_cc_cb_mapped_coeff_abs, aps->alf_cc_cr_mapped_coeff_abs };
    const uint8_t (*sign[])[ALF_NUM_COEFF_CC] =
        {aps->alf_cc_cb_coeff_sign, aps->alf_cc_cr_coeff_sign };
    const int signaled[] = { aps->alf_cc_cb_filter_signal_flag, aps->alf_cc_cr_filter_signal_flag};

    alf->num_cc_filters[0] = aps->alf_cc_cb_filters_signalled_minus1 + 1;
    alf->num_cc_filters[1] = aps->alf_cc_cr_filters_signalled_minus1 + 1;

    for (int idx = 0; idx < 2; idx++) {
        if (signaled[idx]) {
            for (int i = 0; i < alf->num_cc_filters[idx]; i++)
                alf_coeff_cc(alf->cc_coeff[idx][i], abs[idx][i], sign[idx][i]);
        }
    }
}

static void alf_derive(VVCALF *alf)
{
    alf_luma(alf);
    alf_chroma(alf);
    alf_cc(alf);
}

static void alf_free(uint8_t *data)
{
    VVCALF *alf = (VVCALF*)data;

    av_buffer_unref(&alf->rref);
}

static AVBufferRef *alf_alloc(AVBufferRef *aps_buf)
{
    int ret;
    VVCALF *alf;
    AVBufferRef *buf = ps_alloc(sizeof(*alf), alf_free);
    if (!buf)
        return NULL;

    alf = (VVCALF *)buf->data;
    ret = av_buffer_replace(&alf->rref, aps_buf);
    if (ret < 0)
        goto fail;

    alf_derive(alf);

    return buf;

fail:
    av_buffer_unref(&buf);
    return buf;
}

static int aps_decode_alf(AVBufferRef **alf_buf, AVBufferRef *aps_buf)
{
    int ret;
    AVBufferRef *buf = alf_alloc(aps_buf);

    if (!buf)
        return AVERROR(ENOMEM);

    ret = av_buffer_replace(alf_buf, buf);

    av_buffer_unref(&buf);
    return ret;

}

static int is_luma_list(const int id)
{
    return id % VVC_MAX_SAMPLE_ARRAYS == SL_START_4x4 || id == SL_START_64x64 + 1;
}

static int derive_matrix_size(const int id)
{
    return id < SL_START_4x4 ? 2 : (id < SL_START_8x8 ? 4 : 8);
}

// 7.4.3.20 Scaling list data semantics
static int scaling_derive(VVCScalingList *sl)
{
    const H266RawAPS *aps = (const H266RawAPS *)sl->rref->data;

    for (int id = 0; id < SL_MAX_ID; id++) {
        const int matrix_size   = derive_matrix_size(id);
        const int log2_size     = log2(matrix_size);
        const int list_size     = matrix_size * matrix_size;
        int coeff[SL_MAX_MATRIX_SIZE * SL_MAX_MATRIX_SIZE];
        const uint8_t *pred;
        const int *scaling_list;
        int dc = 0;

        if (aps->aps_chroma_present_flag || is_luma_list(id)) {
            if (!aps->scaling_list_copy_mode_flag[id]) {
                int next_coef = 0;

                if (id >= SL_START_16x16)
                    dc = next_coef = aps->scaling_list_dc_coef[id - SL_START_16x16];

                for (int i = 0; i < list_size; i++) {
                    const int x = ff_vvc_diag_scan_x[3][3][i];
                    const int y = ff_vvc_diag_scan_y[3][3][i];

                    if (!(id >= SL_START_64x64 && x >= 4 && y >= 4))
                        next_coef += aps->scaling_list_delta_coef[id][i];
                    coeff[i] = next_coef;
                }
            }
        }

        //dc
        if (id >= SL_START_16x16) {
            if (!aps->scaling_list_copy_mode_flag[id] && !aps->scaling_list_pred_mode_flag[id]) {
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = 8;
            } else if (!aps->scaling_list_pred_id_delta[id]) {
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = 16;
            } else {
                const int ref_id = id - aps->scaling_list_pred_id_delta[id];
                if (ref_id >= SL_START_16x16)
                    dc += sl->scaling_matrix_dc_rec[ref_id - SL_START_16x16];
                else
                    dc += sl->scaling_matrix_rec[ref_id][0];
                sl->scaling_matrix_dc_rec[id - SL_START_16x16] = dc & 255;
            }
        }

        //ac
        scaling_list = aps->scaling_list_copy_mode_flag[id] ? ff_vvc_scaling_list0 : coeff;
        if (!aps->scaling_list_copy_mode_flag[id] && !aps->scaling_list_pred_mode_flag[id])
            pred = ff_vvc_scaling_pred_8;
        else if (!aps->scaling_list_pred_id_delta[id])
            pred = ff_vvc_scaling_pred_16;
        else
            pred = sl->scaling_matrix_rec[id - aps->scaling_list_pred_id_delta[id]];
        for (int i = 0; i < list_size; i++) {
            const int x = ff_vvc_diag_scan_x[log2_size][log2_size][i];
            const int y = ff_vvc_diag_scan_y[log2_size][log2_size][i];
            const int off = y * matrix_size + x;
            sl->scaling_matrix_rec[id][off] = (pred[off] + scaling_list[i]) & 255;
        }
    }

    return 0;
}

static void scaling_free(uint8_t *data)
{
    VVCScalingList *sl = (VVCScalingList*)data;

    av_buffer_unref(&sl->rref);
}

static AVBufferRef *scaling_alloc(AVBufferRef *aps_buf)
{
    int ret;
    VVCScalingList *sl;
    AVBufferRef *buf = ps_alloc(sizeof(*sl), scaling_free);

    if (!buf)
        return NULL;
    sl = (VVCScalingList *)buf->data;

    ret = av_buffer_replace(&sl->rref, aps_buf);
    if (ret < 0)
        goto fail;

    ret = scaling_derive(sl);
    if (ret < 0)
        goto fail;

    return buf;

fail:
    av_buffer_unref(&buf);
    return buf;
}

static int aps_decode_scaling(AVBufferRef **sl_buf, AVBufferRef *aps_buf)
{
    int ret;
    AVBufferRef *buf = scaling_alloc(aps_buf);
    if (!buf)
        return AVERROR(ENOMEM);

    ret = av_buffer_replace(sl_buf, buf);

    av_buffer_unref(&buf);
    return ret;
}

int ff_vvc_decode_aps(VVCParamSets *ps, const CodedBitstreamUnit *unit)
{
    AVBufferRef *aps_buf                    = unit->content_ref;
    const H266RawAPS *aps                   = unit->content;
    int ret                                 = 0;

    if (!aps_buf || !aps)
        return AVERROR_INVALIDDATA;

    switch (aps->aps_params_type) {
        case APS_ALF:
            ret = aps_decode_alf(&ps->alf_list[aps->aps_adaptation_parameter_set_id], aps_buf);
            break;
        case APS_LMCS:
            ret = av_buffer_replace(&ps->lmcs_list[aps->aps_adaptation_parameter_set_id], aps_buf);
            break;
        case APS_SCALING:
            ret = aps_decode_scaling(&ps->scaling_list[aps->aps_adaptation_parameter_set_id], aps_buf);
            break;
    }

    return ret;
}

static void sh_slice_address(VVCSH *sh, const H266RawSPS *sps, const VVCPPS *pps)
{
    const int slice_address     = sh->r->sh_slice_address;

    if (pps->r->pps_rect_slice_flag) {
        int pic_level_slice_idx = slice_address;
        for (int j = 0; j < sh->r->curr_subpic_idx; j++)
            pic_level_slice_idx += pps->r->num_slices_in_subpic[j];
        sh->ctb_addr_in_curr_slice = pps->ctb_addr_in_slice + pps->slice_start_offset[pic_level_slice_idx];
        sh->num_ctus_in_curr_slice = pps->num_ctus_in_slice[pic_level_slice_idx];
    } else {
        int tile_x = slice_address % pps->r->num_tile_columns;
        int tile_y = slice_address / pps->r->num_tile_columns;
        const int slice_start_ctb = pps->row_bd[tile_y] * pps->ctb_width + pps->col_bd[tile_x] * pps->r->row_height_val[tile_y];

        sh->ctb_addr_in_curr_slice = pps->ctb_addr_in_slice + slice_start_ctb;

        sh->num_ctus_in_curr_slice = 0;
        for (int tile_idx = slice_address; tile_idx <= slice_address + sh->r->sh_num_tiles_in_slice_minus1; tile_idx++) {
            tile_x = tile_idx % pps->r->num_tile_columns;
            tile_y = tile_idx / pps->r->num_tile_columns;
            sh->num_ctus_in_curr_slice += pps->r->row_height_val[tile_y] * pps->r->col_width_val[tile_x];
        }
    }
}

static void sh_qp_y(VVCSH *sh, const H266RawPPS *pps, const H266RawPictureHeader *ph)
{
    const int init_qp = pps->pps_init_qp_minus26 + 26;

    if (!pps->pps_qp_delta_info_in_ph_flag)
        sh->slice_qp_y = init_qp + sh->r->sh_qp_delta;
    else
        sh->slice_qp_y = init_qp + ph->ph_qp_delta;
}

static void sh_inter(VVCSH *sh, const H266RawSPS *sps, const H266RawPPS *pps)
{
    const H266RawSliceHeader *rsh = sh->r;

    if (!pps->pps_wp_info_in_ph_flag &&
        ((pps->pps_weighted_pred_flag && IS_P(rsh)) ||
         (pps->pps_weighted_bipred_flag && IS_B(rsh))))
        pred_weight_table(&sh->pwt, &rsh->sh_pred_weight_table);
}

static void sh_deblock_offsets(VVCSH *sh)
{
    const H266RawSliceHeader *r = sh->r;

    if (!r->sh_deblocking_filter_disabled_flag) {
        sh->deblock.beta_offset[LUMA] = r->sh_luma_beta_offset_div2 << 1;
        sh->deblock.tc_offset[LUMA]   = r->sh_luma_tc_offset_div2 << 1;
        sh->deblock.beta_offset[CB]   = r->sh_cb_beta_offset_div2 << 1;
        sh->deblock.tc_offset[CB]     = r->sh_cb_tc_offset_div2 << 1;
        sh->deblock.beta_offset[CR]   = r->sh_cr_beta_offset_div2 << 1;
        sh->deblock.tc_offset[CR]     = r->sh_cr_tc_offset_div2 << 1;
    }
}

static void sh_partition_constraints(VVCSH *sh, const H266RawSPS *sps, const H266RawPictureHeader *ph)
{
    const int min_cb_log2_size_y = sps->sps_log2_min_luma_coding_block_size_minus2 + 2;
    int min_qt_log2_size_y[2];

    if (IS_I(sh->r)) {
        min_qt_log2_size_y[LUMA]        = (min_cb_log2_size_y + ph->ph_log2_diff_min_qt_min_cb_intra_slice_luma);
        min_qt_log2_size_y[CHROMA]      = (min_cb_log2_size_y + ph->ph_log2_diff_min_qt_min_cb_intra_slice_chroma);

        sh->max_bt_size[LUMA]           = 1 << (min_qt_log2_size_y[LUMA]  + ph->ph_log2_diff_max_bt_min_qt_intra_slice_luma);
        sh->max_bt_size[CHROMA]         = 1 << (min_qt_log2_size_y[CHROMA]+ ph->ph_log2_diff_max_bt_min_qt_intra_slice_chroma);

        sh->max_tt_size[LUMA]           = 1 << (min_qt_log2_size_y[LUMA]  + ph->ph_log2_diff_max_tt_min_qt_intra_slice_luma);
        sh->max_tt_size[CHROMA]         = 1 << (min_qt_log2_size_y[CHROMA]+ ph->ph_log2_diff_max_tt_min_qt_intra_slice_chroma);

        sh->max_mtt_depth[LUMA]         = ph->ph_max_mtt_hierarchy_depth_intra_slice_luma;
        sh->max_mtt_depth[CHROMA]       = ph->ph_max_mtt_hierarchy_depth_intra_slice_chroma;

        sh->cu_qp_delta_subdiv          = ph->ph_cu_qp_delta_subdiv_intra_slice;
        sh->cu_chroma_qp_offset_subdiv  = ph->ph_cu_chroma_qp_offset_subdiv_intra_slice;
    } else {
        for (int i = LUMA; i <= CHROMA; i++)  {
            min_qt_log2_size_y[i]        = (min_cb_log2_size_y + ph->ph_log2_diff_min_qt_min_cb_inter_slice);
            sh->max_bt_size[i]           = 1 << (min_qt_log2_size_y[i]  + ph->ph_log2_diff_max_bt_min_qt_inter_slice);
            sh->max_tt_size[i]           = 1 << (min_qt_log2_size_y[i]  + ph->ph_log2_diff_max_tt_min_qt_inter_slice);
            sh->max_mtt_depth[i]         = ph->ph_max_mtt_hierarchy_depth_inter_slice;
        }

        sh->cu_qp_delta_subdiv          = ph->ph_cu_qp_delta_subdiv_inter_slice;
        sh->cu_chroma_qp_offset_subdiv  = ph->ph_cu_chroma_qp_offset_subdiv_inter_slice;
    }

    sh->min_qt_size[LUMA]   = 1 << min_qt_log2_size_y[LUMA];
    sh->min_qt_size[CHROMA] = 1 << min_qt_log2_size_y[CHROMA];
}

static void sh_entry_points(VVCSH *sh, const H266RawSPS *sps, const VVCPPS *pps)
{
    if (sps->sps_entry_point_offsets_present_flag) {
        for (int i = 1, j = 0; i < sh->num_ctus_in_curr_slice; i++) {
            const int pre_ctb_addr_x = sh->ctb_addr_in_curr_slice[i - 1] % pps->ctb_width;
            const int pre_ctb_addr_y = sh->ctb_addr_in_curr_slice[i - 1] / pps->ctb_width;
            const int ctb_addr_x     = sh->ctb_addr_in_curr_slice[i] % pps->ctb_width;
            const int ctb_addr_y     = sh->ctb_addr_in_curr_slice[i] / pps->ctb_width;
            if (pps->ctb_to_row_bd[ctb_addr_y] != pps->ctb_to_row_bd[pre_ctb_addr_y] ||
                pps->ctb_to_col_bd[ctb_addr_x] != pps->ctb_to_col_bd[pre_ctb_addr_x] ||
                (ctb_addr_y != pre_ctb_addr_y && sps->sps_entropy_coding_sync_enabled_flag)) {
                sh->entry_point_start_ctu[j++] = i;
            }
        }
    }
}

static int sh_derive(VVCSH *sh, const VVCFrameParamSets *fps)
{
    const H266RawSPS *sps           = fps->sps->r;
    const H266RawPPS *pps           = fps->pps->r;
    const H266RawPictureHeader *ph  = fps->ph.r;

    sh_slice_address(sh, sps, fps->pps);
    sh_inter(sh, sps, pps);
    sh_qp_y(sh, pps, ph);
    sh_deblock_offsets(sh);
    sh_partition_constraints(sh, sps, ph);
    sh_entry_points(sh, sps, fps->pps);

    return 0;
}

int ff_vvc_decode_sh(VVCSH *sh, const VVCFrameParamSets *fps, const CodedBitstreamUnit *unit)
{
    int ret;

    if (!fps->sps || !fps->pps)
        return AVERROR_INVALIDDATA;

    ret = av_buffer_replace(&sh->rref, unit->content_ref);
    if (ret < 0)
        return ret;
    sh->r = (const H266RawSliceHeader *)unit->content;

    ret = sh_derive(sh, fps);
    if (ret < 0)
        return ret;

    return 0;
}
