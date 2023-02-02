/*
 * VVC video Decoder
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
#include "config_components.h"

#include "libavutil/attributes.h"
#include "libavutil/common.h"
#include "libavutil/cpu.h"
#include "libavutil/display.h"
#include "libavutil/internal.h"
#include "libavutil/mastering_display_metadata.h"
#include "libavutil/md5.h"
#include "libavutil/opt.h"
#include "libavutil/pixdesc.h"
#include "libavutil/stereo3d.h"
#include "libavutil/timecode.h"

#include "bswapdsp.h"
#include "bytestream.h"
#include "codec_internal.h"
#include "decode.h"
#include "golomb.h"
#include "vvc.h"
#include "vvcdec.h"
#include "vvc_ctu.h"
#include "vvc_data.h"
#include "vvc_thread.h"
#include "profiles.h"
#include "thread.h"
#include "threadframe.h"

static int vvc_frame_start(VVCContext *s, VVCFrameContext *fc, SliceContext *sc)
{
    VVCPH *ph = s->ps.ph;
    const VVCSH *sh = &sc->sh;
    int ret;
    if (s->no_output_before_recovery_flag) {
        if (IS_GDR(s))
            s->gdr_recovery_point_poc = ph->poc + ph->recovery_poc_cnt;
        if (!GDR_IS_RECOVERED(s) && s->gdr_recovery_point_poc <= ph->poc)
            GDR_SET_RECOVERED(s);
    }

    // 8.3.1 Decoding process for picture order count
    if (!s->temporal_id && !ph->non_ref_pic_flag && !(IS_RASL(s) || IS_RADL(s)))
        s->pocTid0 = ph->poc;

    if ((ret = ff_vvc_set_new_ref(s, fc, &fc->frame)) < 0)
        goto fail;

    if (!IS_IDR(s))
        ff_vvc_bump_frame(s, fc);

    av_frame_unref(fc->output_frame);

    if ((ret = ff_vvc_output_frame(s, fc, fc->output_frame, sh->no_output_of_prior_pics_flag, 0)) < 0)
        goto fail;

    if ((ret = ff_vvc_frame_rpl(s, fc, sc)) < 0)
        goto fail;

    if ((ret = ff_vvc_frame_thread_init(fc)) < 0)
        goto fail;
    return 0;
fail:
    if (fc->ref)
        ff_vvc_unref_frame(fc, fc->ref, ~0);
    fc->ref = NULL;
    return ret;
}

void ff_vvc_ctu_free_cus(CTU *ctu)
{
    while (ctu->cus) {
        CodingUnit *cu      = ctu->cus;
        AVBufferRef *buf    = cu->buf;

        ctu->cus = ctu->cus->next;
        av_buffer_unref(&buf);
    }
}

static void ctb_arrays_free(VVCFrameContext *fc)
{
    av_freep(&fc->tab.deblock);
    av_freep(&fc->tab.sao);
    av_freep(&fc->tab.alf);
    av_freep(&fc->tab.slice_idx);
    av_freep(&fc->tab.coeffs);
    if (fc->tab.ctus) {
        for (int i = 0; i < fc->tab.ctu_count; i++)
            ff_vvc_ctu_free_cus(fc->tab.ctus + i);
        av_freep(&fc->tab.ctus);
    }
    av_buffer_pool_uninit(&fc->rpl_tab_pool);
}

static int ctb_arrays_init(VVCFrameContext *fc, const int ctu_count, const int ctu_size)
{
    if (fc->tab.ctu_count != ctu_count || fc->tab.ctu_size != ctu_size) {
        ctb_arrays_free(fc);
        fc->tab.deblock         = av_calloc(ctu_count, sizeof(*fc->tab.deblock));
        fc->tab.sao             = av_calloc(ctu_count, sizeof(*fc->tab.sao));
        fc->tab.alf             = av_calloc(ctu_count, sizeof(*fc->tab.alf));
        fc->tab.ctus            = av_calloc(ctu_count, sizeof(*fc->tab.ctus));
        fc->tab.slice_idx       = av_malloc(ctu_count * sizeof(*fc->tab.slice_idx));
        if (!fc->tab.deblock || !fc->tab.sao || !fc->tab.alf || !fc->tab.ctus || !fc->tab.slice_idx )
            return AVERROR(ENOMEM);
        fc->tab.coeffs = av_malloc(ctu_count * sizeof(*fc->tab.coeffs) * ctu_size * VVC_MAX_SAMPLE_ARRAYS);
        if (!fc->tab.coeffs)
            return AVERROR(ENOMEM);
        fc->rpl_tab_pool = av_buffer_pool_init(ctu_count * sizeof(RefPicListTab), av_buffer_allocz);
        if (!fc->rpl_tab_pool)
            return AVERROR(ENOMEM);
    } else {
        memset(fc->tab.deblock, 0, ctu_count * sizeof(*fc->tab.deblock));
        memset(fc->tab.sao, 0, ctu_count * sizeof(*fc->tab.sao));
        memset(fc->tab.alf, 0, ctu_count * sizeof(*fc->tab.alf));
        for (int i = 0; i < fc->tab.ctu_count; i++)
            ff_vvc_ctu_free_cus(fc->tab.ctus + i);
        memset(fc->tab.ctus, 0, ctu_count * sizeof(*fc->tab.ctus));
    }
    memset(fc->tab.slice_idx, -1, ctu_count * sizeof(*fc->tab.slice_idx));

    return 0;
}

static void min_cb_arrays_free(VVCFrameContext *fc)
{
    for (int i = LUMA; i <= CHROMA; i++) {
        av_freep(&fc->tab.cb_pos_x[i]);
        av_freep(&fc->tab.cb_pos_y[i]);
        av_freep(&fc->tab.cb_width[i]);
        av_freep(&fc->tab.cb_height[i]);
        av_freep(&fc->tab.cqt_depth[i]);
        av_freep(&fc->tab.cpm[i]);
        av_freep(&fc->tab.cp_mv[i]);
    }

    av_freep(&fc->tab.ipm);
    av_freep(&fc->tab.imf);
    av_freep(&fc->tab.imtf);
    av_freep(&fc->tab.imm);
    av_freep(&fc->tab.skip);
}

static int min_cb_arrays_init(VVCFrameContext *fc, const int pic_size_in_min_cb)
{
    if (fc->tab.pic_size_in_min_cb != pic_size_in_min_cb) {
        min_cb_arrays_free(fc);
        for (int i = LUMA; i <= CHROMA; i++) {
            fc->tab.cb_pos_x[i]  = av_mallocz(pic_size_in_min_cb * sizeof(int));
            fc->tab.cb_pos_y[i]  = av_mallocz(pic_size_in_min_cb * sizeof(int));
            fc->tab.cb_width[i]  = av_mallocz(pic_size_in_min_cb);
            fc->tab.cb_height[i] = av_mallocz(pic_size_in_min_cb);
            fc->tab.cqt_depth[i] = av_mallocz(pic_size_in_min_cb);
            if (!fc->tab.cb_pos_x[i] || !fc->tab.cb_pos_y[i] || !fc->tab.cb_width[i] || !fc->tab.cb_height[i] || !fc->tab.cqt_depth[i])
                return AVERROR(ENOMEM);

            fc->tab.cpm[i]   = av_mallocz(pic_size_in_min_cb);
            fc->tab.cp_mv[i] = av_mallocz(pic_size_in_min_cb * sizeof(Mv) * MAX_CONTROL_POINTS);
            if (!fc->tab.cpm[i] || !fc->tab.cp_mv[i])
                return AVERROR(ENOMEM);
        }

        fc->tab.ipm  = av_mallocz(pic_size_in_min_cb);
        fc->tab.imf  = av_mallocz(pic_size_in_min_cb);
        fc->tab.imtf = av_mallocz(pic_size_in_min_cb);
        fc->tab.imm  = av_mallocz(pic_size_in_min_cb);
        fc->tab.skip = av_mallocz(pic_size_in_min_cb);
        if (!fc->tab.ipm || !fc->tab.imf || !fc->tab.imtf || !fc->tab.imm || !fc->tab.skip)
            return AVERROR(ENOMEM);
    } else {
        for (int i = LUMA; i <= CHROMA; i++) {
            memset(fc->tab.cb_pos_x[i], 0, pic_size_in_min_cb * sizeof(int));
            memset(fc->tab.cb_pos_y[i], 0, pic_size_in_min_cb * sizeof(int));
            memset(fc->tab.cb_width[i], 0, pic_size_in_min_cb);
            memset(fc->tab.cb_height[i], 0, pic_size_in_min_cb);
            memset(fc->tab.cqt_depth[i], 0, pic_size_in_min_cb);
            memset(fc->tab.cpm[i], 0, pic_size_in_min_cb);
            memset(fc->tab.cp_mv[i], 0, pic_size_in_min_cb * sizeof(Mv) * MAX_CONTROL_POINTS);
        }

        memset(fc->tab.ipm, 0, pic_size_in_min_cb);
        memset(fc->tab.imf, 0, pic_size_in_min_cb);
        memset(fc->tab.imtf, 0, pic_size_in_min_cb);
        memset(fc->tab.imm, 0, pic_size_in_min_cb);
        memset(fc->tab.skip, 0, pic_size_in_min_cb);
    }
    return 0;
}

static void min_tu_arrays_free(VVCFrameContext *fc)
{
    for (int i = LUMA; i <= CHROMA; i++) {
        av_freep(&fc->tab.tb_pos_x0[i]);
        av_freep(&fc->tab.tb_pos_y0[i]);
        av_freep(&fc->tab.tb_width[i]);
        av_freep(&fc->tab.tb_height[i]);
        av_freep(&fc->tab.pcmf[i]);
    }

    for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
        av_freep(&fc->tab.qp[i]);
        av_freep(&fc->tab.tu_coded_flag[i]);
    }

    av_freep(&fc->tab.tu_joint_cbcr_residual_flag);
}

static int min_tu_arrays_init(VVCFrameContext *fc, const int pic_size_in_min_tu)
{
    if (fc->tab.pic_size_in_min_tu != pic_size_in_min_tu) {
        min_tu_arrays_free(fc);
        for (int i = LUMA; i <= CHROMA; i++) {
            fc->tab.tb_pos_x0[i] = av_mallocz(pic_size_in_min_tu * sizeof(*fc->tab.tb_pos_x0[0]));
            fc->tab.tb_pos_y0[i] = av_mallocz(pic_size_in_min_tu * sizeof(*fc->tab.tb_pos_y0[0])) ;
            fc->tab.tb_width[i]  = av_mallocz(pic_size_in_min_tu);
            fc->tab.tb_height[i] = av_mallocz(pic_size_in_min_tu);
            fc->tab.pcmf[i]      = av_mallocz(pic_size_in_min_tu);
            if (!fc->tab.tb_pos_x0[i] || !fc->tab.tb_pos_y0[i] ||
                !fc->tab.tb_width[i] || !fc->tab.tb_height[i] || !fc->tab.pcmf[i])
                return AVERROR(ENOMEM);
        }

        for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
            fc->tab.tu_coded_flag[i] = av_mallocz(pic_size_in_min_tu);
            if (!fc->tab.tu_coded_flag[i])
                return AVERROR(ENOMEM);

            fc->tab.qp[i] = av_mallocz(pic_size_in_min_tu);
            if (!fc->tab.qp[i])
                return AVERROR(ENOMEM);
        }

        fc->tab.tu_joint_cbcr_residual_flag  = av_mallocz(pic_size_in_min_tu);
        if (!fc->tab.tu_joint_cbcr_residual_flag)
            return AVERROR(ENOMEM);
    } else {
        for (int i = LUMA; i <= CHROMA; i++) {
            memset(fc->tab.tb_pos_x0[i], 0, pic_size_in_min_tu * sizeof(*fc->tab.tb_pos_x0[0]));
            memset(fc->tab.tb_pos_y0[i], 0, pic_size_in_min_tu * sizeof(*fc->tab.tb_pos_y0[0])) ;
            memset(fc->tab.tb_width[i], 0, pic_size_in_min_tu);
            memset(fc->tab.tb_height[i], 0, pic_size_in_min_tu);
            memset(fc->tab.pcmf[i], 0, pic_size_in_min_tu);
        }

        for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
            memset(fc->tab.tu_coded_flag[i], 0, pic_size_in_min_tu);
            memset(fc->tab.qp[i], 0, pic_size_in_min_tu);
        }
        memset(fc->tab.tu_joint_cbcr_residual_flag, 0, pic_size_in_min_tu);
    }
    return 0;
}

static void min_pu_arrays_free(VVCFrameContext *fc)
{
    av_freep(&fc->tab.msf);
    av_freep(&fc->tab.iaf);
    av_freep(&fc->tab.mmi);
    av_freep(&fc->tab.dmvr);
    av_buffer_pool_uninit(&fc->tab_mvf_pool);
}

static int min_pu_arrays_init(VVCFrameContext *fc, const int pic_size_in_min_pu)
{
    if (fc->tab.pic_size_in_min_pu != pic_size_in_min_pu) {
        min_pu_arrays_free(fc);
        fc->tab.msf  = av_mallocz(pic_size_in_min_pu);
        fc->tab.iaf  = av_mallocz(pic_size_in_min_pu);
        fc->tab.mmi  = av_mallocz(pic_size_in_min_pu);
        fc->tab.dmvr = av_calloc(pic_size_in_min_pu, sizeof(*fc->tab.dmvr));
        if (!fc->tab.msf || !fc->tab.iaf || !fc->tab.mmi || !fc->tab.dmvr)
            return AVERROR(ENOMEM);
        fc->tab_mvf_pool  = av_buffer_pool_init(pic_size_in_min_pu * sizeof(MvField), av_buffer_allocz);
        if (!fc->tab_mvf_pool)
            return AVERROR(ENOMEM);
    } else {
        memset(fc->tab.msf, 0, pic_size_in_min_pu);
        memset(fc->tab.iaf, 0, pic_size_in_min_pu);
        memset(fc->tab.mmi, 0, pic_size_in_min_pu);
        memset(fc->tab.dmvr, 0, pic_size_in_min_pu * sizeof(*fc->tab.dmvr));
    }

    return 0;
}

static void bs_arrays_free(VVCFrameContext *fc)
{
    for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
        av_freep(&fc->tab.horizontal_bs[i]);
        av_freep(&fc->tab.vertical_bs[i]);
    }
    av_freep(&fc->tab.horizontal_q);
    av_freep(&fc->tab.horizontal_p);
    av_freep(&fc->tab.vertical_p);
    av_freep(&fc->tab.vertical_q);
}

static int bs_arrays_init(VVCFrameContext *fc, const int bs_width, const int bs_height)
{
    if (fc->tab.bs_width != bs_width || fc->tab.bs_height != bs_height) {
        bs_arrays_free(fc);
        for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
            fc->tab.horizontal_bs[i] = av_calloc(bs_width, bs_height);
            fc->tab.vertical_bs[i]   = av_calloc(bs_width, bs_height);
            if (!fc->tab.horizontal_bs[i] || !fc->tab.vertical_bs[i])
                return AVERROR(ENOMEM);
        }
        fc->tab.horizontal_q = av_calloc(bs_width, bs_height);
        fc->tab.horizontal_p = av_calloc(bs_width, bs_height);
        fc->tab.vertical_p   = av_calloc(bs_width, bs_height);
        fc->tab.vertical_q   = av_calloc(bs_width, bs_height);
        if (!fc->tab.horizontal_q || !fc->tab.horizontal_p || !fc->tab.vertical_p || !fc->tab.vertical_q)
            return AVERROR(ENOMEM);
    } else {
        for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
            memset(fc->tab.horizontal_bs[i], 0, bs_width * bs_height);
            memset(fc->tab.vertical_bs[i], 0, bs_width * bs_height);
        }
        memset(fc->tab.horizontal_q, 0, bs_width * bs_height);
        memset(fc->tab.horizontal_p, 0, bs_width * bs_height);
        memset(fc->tab.vertical_p, 0, bs_width * bs_height);
        memset(fc->tab.vertical_q, 0, bs_width * bs_height);
    }
    return 0;
}

static void pixel_buffer_free(VVCFrameContext *fc)
{
    for (int i = 0; i < VVC_MAX_SAMPLE_ARRAYS; i++) {
        av_freep(&fc->tab.sao_pixel_buffer_h[i]);
        av_freep(&fc->tab.sao_pixel_buffer_v[i]);
        for (int j = 0; j < 2; j++) {
            av_freep(&fc->tab.alf_pixel_buffer_h[i][j]);
            av_freep(&fc->tab.alf_pixel_buffer_v[i][j]);
        }
    }
}

static int pixel_buffer_init(VVCFrameContext *fc, const int width, const int height,
    const int ctu_width, const int ctu_height, const int chroma_format_idc, const int ps)
{
    const VVCSPS *sps = fc->ps.sps;
    const int c_end   = chroma_format_idc ? VVC_MAX_SAMPLE_ARRAYS : 1;

    if (fc->tab.chroma_format_idc != chroma_format_idc ||
        fc->tab.width != width || fc->tab.height != height ||
        fc->tab.ctu_width != ctu_width || fc->tab.ctu_height != ctu_height) {
        pixel_buffer_free(fc);
        for(int c_idx = 0; c_idx < c_end; c_idx++) {
            const int w = width >> sps->hshift[c_idx];
            const int h = height >> sps->vshift[c_idx];
            fc->tab.sao_pixel_buffer_h[c_idx] = av_malloc((w * 2 * ctu_height) << ps);
            fc->tab.sao_pixel_buffer_v[c_idx] = av_malloc((h * 2 * ctu_width)  << ps);
            if (!fc->tab.sao_pixel_buffer_h[c_idx] || !fc->tab.sao_pixel_buffer_v[c_idx])
                return AVERROR(ENOMEM);
        }

        for(int c_idx = 0; c_idx < c_end; c_idx++) {
            const int w = width >> sps->hshift[c_idx];
            const int h = height >> sps->vshift[c_idx];
            const int border_pixels = c_idx ? ALF_BORDER_CHROMA : ALF_BORDER_LUMA;
            for (int i = 0; i < 2; i++) {
                fc->tab.alf_pixel_buffer_h[c_idx][i] = av_malloc((w * border_pixels * ctu_height) << ps);
                fc->tab.alf_pixel_buffer_v[c_idx][i] = av_malloc(h * ALF_PADDING_SIZE * ctu_width);
                if (!fc->tab.alf_pixel_buffer_h[c_idx][i] || !fc->tab.alf_pixel_buffer_v[c_idx][i])
                    return AVERROR(ENOMEM);
            }
        }
    }
    return 0;
}

static void pic_arrays_free(VVCFrameContext *fc)
{
    ctb_arrays_free(fc);
    min_cb_arrays_free(fc);
    min_pu_arrays_free(fc);
    min_tu_arrays_free(fc);
    bs_arrays_free(fc);
    av_buffer_pool_uninit(&fc->cu_pool);
    pixel_buffer_free(fc);

    for (int i = 0; i < 2; i++)
        av_freep(&fc->tab.msm[i]);
    av_freep(&fc->tab.ispmf);

    fc->tab.ctu_count = 0;
    fc->tab.ctu_size  = 0;
    fc->tab.pic_size_in_min_cb = 0;
    fc->tab.pic_size_in_min_pu = 0;
    fc->tab.pic_size_in_min_tu = 0;
    fc->tab.width              = 0;
    fc->tab.height             = 0;
    fc->tab.ctu_width          = 0;
    fc->tab.ctu_height         = 0;
    fc->tab.bs_width           = 0;
    fc->tab.bs_height          = 0;
}

static int pic_arrays_init(VVCContext *s, VVCFrameContext *fc)
{
    const VVCSPS *sps               = fc->ps.sps;
    const VVCPPS *pps               = fc->ps.pps;
    const int ctu_size              = 1 << sps->ctb_log2_size_y << sps->ctb_log2_size_y;
    const int pic_size_in_min_cb    = pps->min_cb_width * pps->min_cb_height;
    const int pic_size_in_min_pu    = pps->min_pu_width * pps->min_pu_height;
    const int pic_size_in_min_tu    = pps->min_tb_width * pps->min_tb_height;
    const int w32                   = AV_CEIL_RSHIFT(pps->width,  5);
    const int h32                   = AV_CEIL_RSHIFT(pps->height,  5);
    const int w64                   = AV_CEIL_RSHIFT(pps->width,  6);
    const int h64                   = AV_CEIL_RSHIFT(pps->height,  6);
    const int bs_width              = (fc->ps.pps->width >> 2) + 1;
    const int bs_height             = (fc->ps.pps->height >> 2) + 1;
    int ret;

    if ((ret = ctb_arrays_init(fc, pps->ctb_count, ctu_size)) < 0)
        goto fail;

    if ((ret = min_cb_arrays_init(fc, pic_size_in_min_cb)) < 0)
        goto fail;

    if ((ret = min_pu_arrays_init(fc, pic_size_in_min_pu)) < 0)
        goto fail;

    if ((ret = min_tu_arrays_init(fc, pic_size_in_min_tu)) < 0)
        goto fail;

    if ((ret = bs_arrays_init(fc, bs_width, bs_height)) < 0)
        goto fail;

    if ((ret = pixel_buffer_init(fc, pps->width, pps->height, pps->ctb_width, pps->ctb_height,
        sps->chroma_format_idc, sps->pixel_shift)) < 0)
        goto fail;

    if (AV_CEIL_RSHIFT(fc->tab.width,  5) != w32 || AV_CEIL_RSHIFT(fc->tab.height,  5) != h32) {
        for (int i = LUMA; i <= CHROMA; i++) {
            av_freep(&fc->tab.msm[i]);
            fc->tab.msm[i] = av_calloc(w32, h32);
            if (!fc->tab.msm[i])
                goto fail;
        }
    } else {
        for (int i = LUMA; i <= CHROMA; i++)
            memset(fc->tab.msm[i], 0, w32 * h32);
    }
    if (AV_CEIL_RSHIFT(fc->tab.width,  6) != w64 || AV_CEIL_RSHIFT(fc->tab.height,  6) != h64) {
        av_freep(&fc->tab.ispmf);
        fc->tab.ispmf = av_calloc(w64, h64);
        if (!fc->tab.ispmf)
            goto fail;
    } else {
        memset(fc->tab.ispmf, 0, w64 * h64);
    }

    if (!fc->cu_pool) {
        fc->cu_pool = av_buffer_pool_init(sizeof(CodingUnit), NULL);
        if (!fc->cu_pool)
            goto fail;
    }

    fc->tab.ctu_count = pps->ctb_count;
    fc->tab.ctu_size  = ctu_size;
    fc->tab.pic_size_in_min_cb = pic_size_in_min_cb;
    fc->tab.pic_size_in_min_pu = pic_size_in_min_pu;
    fc->tab.pic_size_in_min_tu = pic_size_in_min_tu;
    fc->tab.width              = pps->width;
    fc->tab.height             = pps->height;
    fc->tab.ctu_width          = pps->ctb_width;
    fc->tab.ctu_height         = pps->ctb_height;
    fc->tab.chroma_format_idc  = sps->chroma_format_idc;
    fc->tab.pixel_shift        = sps->pixel_shift;
    fc->tab.bs_width           = bs_width;
    fc->tab.bs_height          = bs_height;

    return 0;
fail:
    pic_arrays_free(fc);
    return ret;
}

static int min_positive(const int idx, const int diff, const int min_diff)
{
    return diff > 0 && (idx < 0 || diff < min_diff);
}

static int max_negtive(const int idx, const int diff, const int max_diff)
{
    return diff < 0 && (idx < 0 || diff > max_diff);
}

typedef int (*smvd_find_fxn)(const int idx, const int diff, const int old_diff);

static int8_t smvd_find(const VVCFrameContext *fc, const VVCSH *sh, int lx, smvd_find_fxn find)
{
    const RefPicList *rpl   = fc->ref->refPicList + lx;
    const int poc           = fc->ref->poc;
    int8_t idx              = -1;
    int old_diff            = -1;
    for (int i = 0; i < sh->nb_refs[lx]; i++) {
        if (!rpl->isLongTerm[i]) {
            int diff = poc - rpl->list[i];
            if (find(idx, diff, old_diff)) {
                idx = i;
                old_diff = diff;
            }
        }
    }
    return idx;
}

static void vvc_smvd_ref_idx(const VVCFrameContext *fc, VVCSH *sh)
{
    if (IS_B(sh)) {
        sh->ref_idx_sym[0] = smvd_find(fc, sh, 0, min_positive);
        sh->ref_idx_sym[1] = smvd_find(fc, sh, 1, max_negtive);
        if (sh->ref_idx_sym[0] == -1 || sh->ref_idx_sym[1] == -1) {
            sh->ref_idx_sym[0] = smvd_find(fc, sh, 0, max_negtive);
            sh->ref_idx_sym[1] = smvd_find(fc, sh, 1, min_positive);
        }
    }
}

static void eps_free(SliceContext *slice)
{
    if (slice->eps) {
        for (int j = 0; j < slice->nb_eps; j++)
            av_free(slice->eps[j].parse_task);
        av_freep(&slice->eps);
    }
}

static void slices_free(VVCFrameContext *fc)
{
    if (fc->slices) {
        for (int i = 0; i < fc->nb_slices_allocated; i++) {
            SliceContext *slice = fc->slices[i];
            if (slice) {
                eps_free(slice);
                av_free(slice);
            }
        }
        av_freep(&fc->slices);
    }
    fc->nb_slices_allocated = 0;
    fc->nb_slices = 0;
}

static int slices_realloc(VVCFrameContext *fc)
{
    void *p;
    const int size = (fc->nb_slices_allocated + 1) * 3 / 2;

    if (fc->nb_slices < fc->nb_slices_allocated)
        return 0;

    p = av_realloc(fc->slices, size * sizeof(*fc->slices));
    if (!p)
        return AVERROR(ENOMEM);

    fc->slices = p;
    for (int i = fc->nb_slices_allocated; i < size; i++) {
        fc->slices[i] = av_calloc(1, sizeof(*fc->slices[0]));
        if (!fc->slices[i]) {
            for (int j = fc->nb_slices_allocated; j < i; j++)
                av_freep(&fc->slices[j]);
            return AVERROR(ENOMEM);
        }
        fc->slices[i]->slice_idx = i;
    }
    fc->nb_slices_allocated = size;
    return 0;
}

static void ep_init_cabac_decoder(SliceContext *sc, const int index, const H2645NAL *nal, GetBitContext *gb)
{
    const VVCSH *sh = &sc->sh;
    EntryPoint *ep  = sc->eps + index;
    int size;

    if (index < sh->num_entry_points) {
        int skipped = 0;
        int64_t start =  (gb->index >> 3);
        int64_t end = start + sh->entry_point_offset[index];
        while (skipped < nal->skipped_bytes && nal->skipped_bytes_pos[skipped] <= start) {
            skipped++;
        }
        while (skipped < nal->skipped_bytes && nal->skipped_bytes_pos[skipped] < end) {
            end--;
            skipped++;
        }
        size = end - start;
    } else {
        size = get_bits_left(gb) / 8;
    }
    ff_init_cabac_decoder (&ep->cc, gb->buffer + get_bits_count(gb) / 8, size);
    skip_bits(gb, size * 8);
}

static int init_slice_context(SliceContext *sc, VVCFrameContext *fc, const H2645NAL *nal, GetBitContext *gb)
{
    const VVCSH *sh = &sc->sh;
    int nb_eps      = sh->num_entry_points + 1;
    int ctu_addr    = 0;

    if (sc->nb_eps != nb_eps) {
        eps_free(sc);
        sc->eps = av_calloc(nb_eps, sizeof(*sc->eps));
        if (!sc->eps)
            return AVERROR(ENOMEM);
        sc->nb_eps = nb_eps;
        for (int i = 0; i < sc->nb_eps; i++) {
            EntryPoint *ep = sc->eps + i;
            ep->parse_task = ff_vvc_task_alloc();
            if (!ep->parse_task)
                return AVERROR(ENOMEM);
        }
    }

    for (int i = 0; i < sc->nb_eps; i++)
    {
        EntryPoint *ep = sc->eps + i;
        ff_vvc_parse_task_init(ep->parse_task, VVC_TASK_TYPE_PARSE, fc, sc, ep, ctu_addr);
        ep->ctu_addr_last = (i + 1 == sc->nb_eps ? sh->num_ctus_in_curr_slice : sh->entry_point_start_ctu[i]);
        ep_init_cabac_decoder(sc, i, nal, gb);
        if (i + 1 < sc->nb_eps)
            ctu_addr = sh->entry_point_start_ctu[i];
    }

    return 0;
}

static VVCFrameContext* get_frame_context(const VVCContext *s, const VVCFrameContext *fc, const int delta)
{
    const int size = s->nb_fcs;
    const int idx = (fc - s->fcs + delta  + size) % size;
    return s->fcs + idx;
}

static int vvc_ref_frame(VVCFrameContext *fc, VVCFrame *dst, VVCFrame *src)
{
    int ret;

    ret = ff_thread_ref_frame(&dst->tf, &src->tf);
    if (ret < 0)
        return ret;

#if 0
    if (src->needs_fg) {
        ret = ff_thread_ref_frame(&dst->tf_grain, &src->tf_grain);
        if (ret < 0)
            return ret;
        dst->needs_fg = 1;
    }
#endif
    dst->progress_buf = av_buffer_ref(src->progress_buf);

    dst->tab_mvf_buf = av_buffer_ref(src->tab_mvf_buf);
    if (!dst->tab_mvf_buf)
        goto fail;
    dst->tab_mvf = src->tab_mvf;

    dst->rpl_tab_buf = av_buffer_ref(src->rpl_tab_buf);
    if (!dst->rpl_tab_buf)
        goto fail;
    dst->rpl_tab = src->rpl_tab;

    dst->rpl_buf = av_buffer_ref(src->rpl_buf);
    if (!dst->rpl_buf)
        goto fail;

    dst->poc = src->poc;
    dst->ctb_count = src->ctb_count;
    dst->flags = src->flags;
    dst->sequence = src->sequence;

#if 0
    if (src->hwaccel_picture_private) {
        dst->hwaccel_priv_buf = av_buffer_ref(src->hwaccel_priv_buf);
        if (!dst->hwaccel_priv_buf)
            goto fail;
        dst->hwaccel_picture_private = dst->hwaccel_priv_buf->data;
    }
#endif
    return 0;
fail:
    ff_vvc_unref_frame(fc, dst, ~0);
    return AVERROR(ENOMEM);
}

static int frame_context_init_params(VVCFrameContext *fc, const VVCContext *s)
{
    int ret;
    for (int i = 0; i < FF_ARRAY_ELEMS(fc->ps.alf_list); i++) {
        ret = av_buffer_replace(&fc->ps.alf_list[i], s->ps.alf_list[i]);
        if (ret < 0)
            return ret;
    }
    for (int i = 0; i < FF_ARRAY_ELEMS(fc->ps.lmcs_list); i++) {
        ret = av_buffer_replace(&fc->ps.lmcs_list[i], s->ps.lmcs_list[i]);
        if (ret < 0)
            return ret;
    }
    for (int i = 0; i < FF_ARRAY_ELEMS(fc->ps.scaling_list); i++) {
        ret = av_buffer_replace(&fc->ps.scaling_list[i], s->ps.scaling_list[i]);
        if (ret < 0)
            return ret;
    }
    ret = av_buffer_replace(&fc->ps.ph_buf, s->ps.ph_buf);
    if (ret < 0)
        return ret;
    fc->ps.ph = (VVCPH *)fc->ps.ph_buf->data;

    ret = av_buffer_replace(&fc->ps.pps_buf, s->ps.pps_list[fc->ps.ph->pic_parameter_set_id]);
    if (ret < 0)
        return ret;
    fc->ps.pps = (VVCPPS*)fc->ps.pps_buf->data;

    ret = av_buffer_replace(&fc->ps.sps_buf, s->ps.sps_list[fc->ps.pps->seq_parameter_set_id]);
    if (ret < 0)
        return ret;
    fc->ps.sps = (VVCSPS*)fc->ps.sps_buf->data;

    if (s->nb_frames && s->nb_fcs > 1) {
        VVCFrameContext *prev = get_frame_context(s, fc, -1);
        for (int i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            ff_vvc_unref_frame(fc, &fc->DPB[i], ~0);
            if (prev->DPB[i].frame->buf[0]) {
                ret = vvc_ref_frame(fc, &fc->DPB[i], &prev->DPB[i]);
                if (ret < 0)
                    return ret;
            }
        }
    }
    return 0;
}

static void export_frame_params(VVCFrameContext *fc)
{
    AVCodecContext *c   = fc->avctx;
    const VVCSPS *sps   = fc->ps.sps;
    const VVCPPS *pps   = fc->ps.pps;
    const VVCWindow *ow = &pps->conf_win;

    c->pix_fmt          = sps->pix_fmt;
    c->coded_width      = pps->width;
    c->coded_height     = pps->height;
    c->width            = pps->width  - ow->left_offset - ow->right_offset;
    c->height           = pps->height - ow->top_offset  - ow->bottom_offset;
}

static int decode_slice(VVCContext *s, VVCFrameContext *fc, const H2645NAL *nal, GetBitContext *gb)
{
    int ret = 0;
    SliceContext *sc;
    VVCSH  *sh;
    const int is_first_slice = !fc->nb_slices;

    ret = slices_realloc(fc);
    if (ret < 0)
        return ret;
    sc = fc->slices[fc->nb_slices];

    sh = &sc->sh;
    ret = ff_vvc_decode_sh(sh, s, is_first_slice, gb);
    if (ret < 0)
        goto fail;

    if (is_first_slice) {
        ret = frame_context_init_params(fc, s);
        if (ret < 0)
            goto fail;
    }

    if (is_first_slice) {
        if (IS_IDR(s)) {
            s->seq_decode = (s->seq_decode + 1) & 0xff;
            ff_vvc_clear_refs(fc);
        }

        if (pic_arrays_init(s, fc))
            goto fail;
        export_frame_params(fc);
        ff_vvc_dsp_init (&fc->vvcdsp, fc->ps.sps->bit_depth);
        ff_videodsp_init (&fc->vdsp, fc->ps.sps->bit_depth);
        ret = vvc_frame_start(s, fc, sc);
        if (ret < 0)
            return ret;
    } else if (fc->ref) {
        if (!IS_I(sh)) {
            ret = ff_vvc_slice_rpl(s, fc, sc);
            if (ret < 0) {
                av_log(fc->avctx, AV_LOG_WARNING,
                       "Error constructing the reference lists for the current slice.\n");
                return ret;
            }
        }
    } else {
        av_log(fc->avctx, AV_LOG_ERROR, "First slice in a frame missing.\n");
        return ret;
    }


    if (!IS_I(sh))
        vvc_smvd_ref_idx(fc, sh);

    ret = init_slice_context(sc, fc, nal, gb);
    if (ret < 0)
        goto fail;
    fc->nb_slices++;

    for (int i = 0; i < (fc->ps.sps->entropy_coding_sync_enabled_flag ? 1 : sc->nb_eps); i++)
        ff_vvc_frame_add_task(s, sc->eps[i].parse_task);

fail:
    return ret;
}

static int decode_nal_unit(VVCContext *s, VVCFrameContext *fc, const H2645NAL *nal)
{
    GetBitContext gb    = nal->gb;
    int  ret;

    s->temporal_id   = nal->temporal_id;

    switch (nal->type) {
    case VVC_VPS_NUT:
#if 0
        if (s->avctx->hwaccel && s->avctx->hwaccel->decode_params) {
            ret = s->avctx->hwaccel->decode_params(s->avctx,
                                                   nal->type,
                                                   nal->raw_data,
                                                   nal->raw_size);
            if (ret < 0)
                goto fail;
        }
        ret = ff_vvc_decode_nal_vps(gb, s->avctx, &s->ps);
        if (ret < 0)
            goto fail;
#endif
        break;
    case VVC_SPS_NUT:
        if (s->avctx->hwaccel && s->avctx->hwaccel->decode_params) {
            ret = s->avctx->hwaccel->decode_params(s->avctx,
                                                   nal->type,
                                                   nal->raw_data,
                                                   nal->raw_size);
            if (ret < 0)
                goto fail;
        }
        ret = ff_vvc_decode_sps(&s->ps, &gb, s->apply_defdispwin, nal->nuh_layer_id, s->avctx);
        if (ret < 0)
            goto fail;
        break;
    case VVC_PPS_NUT:
        if (s->avctx->hwaccel && s->avctx->hwaccel->decode_params) {
            ret = s->avctx->hwaccel->decode_params(s->avctx,
                                                   nal->type,
                                                   nal->raw_data,
                                                   nal->raw_size);
            if (ret < 0)
                goto fail;
        }
        ret = ff_vvc_decode_pps(&s->ps, &gb, s->avctx);
        if (ret < 0)
            goto fail;
        break;
    case VVC_PH_NUT:
        if (s->avctx->hwaccel && s->avctx->hwaccel->decode_params) {
            ret = s->avctx->hwaccel->decode_params(s->avctx,
                                                   nal->type,
                                                   nal->raw_data,
                                                   nal->raw_size);
            if (ret < 0)
                goto fail;
        }
        ret = ff_vvc_decode_ph(&s->ps, s->pocTid0, IS_CLVSS(s), &gb, s->avctx);
        if (ret < 0)
            goto fail;
        break;
    case VVC_TRAIL_NUT:
    case VVC_STSA_NUT:
    case VVC_RADL_NUT:
    case VVC_RASL_NUT:
    case VVC_IDR_W_RADL:
    case VVC_IDR_N_LP:
    case VVC_CRA_NUT:
    case VVC_GDR_NUT:
        ret = decode_slice(s, fc, nal, &gb);
        if (ret < 0)
            goto fail;
        break;

    case VVC_SUFFIX_APS_NUT:
    case VVC_PREFIX_APS_NUT:
        ret = ff_vvc_decode_aps(&s->ps, &gb, s->avctx);
        if (ret < 0)
            goto fail;
        break;

    default:
        av_log(s->avctx, AV_LOG_INFO,
               "Skipping NAL unit %d\n", nal->type);
    }

    return 0;
fail:
    return ret;
}


static int decode_nal_units(VVCContext *s, VVCFrameContext *fc, const uint8_t *buf, int length)
{
    int i, ret = 0;
    int eos_at_start = 1;
    s->last_eos = s->eos;
    s->eos = 0;

    /* split the input packet into NAL units, so we know the upper bound on the
     * number of slices in the frame */
    ret = ff_h2645_packet_split(&fc->pkt, buf, length, s->avctx, s->is_nalff,
                                s->nal_length_size, s->avctx->codec_id, 1, 0);
    if (ret < 0) {
        av_log(s->avctx, AV_LOG_ERROR,
               "Error splitting the input into NAL units.\n");
        return ret;
    }

    for (i = 0; i < fc->pkt.nb_nals; i++) {
        const H2645NAL *nal = &fc->pkt.nals[i];
        if (nal->type == VVC_EOB_NUT || nal->type == VVC_EOS_NUT) {
            if (eos_at_start) {
                s->last_eos = 1;
            } else {
                s->eos = 1;
            }
        } else {
            if (IS_VCL(nal->type)) {
                s->vcl_unit_type = nal->type;
                if (IS_IDR(s))
                    s->no_output_before_recovery_flag = 0;
                else if (IS_CRA(s) || IS_GDR(s))
                    s->no_output_before_recovery_flag = s->last_eos;
            }
            eos_at_start = 0;
        }
    }

    /* decode the NAL units */
    for (i = 0; i < fc->pkt.nb_nals; i++) {
        H2645NAL *nal = &fc->pkt.nals[i];
        ret = decode_nal_unit(s, fc, nal);
        if (ret < 0) {
            av_log(s->avctx, AV_LOG_WARNING,
                    "Error parsing NAL unit #%d.\n", i);
            goto fail;
        }
    }
    return 0;

fail:
    if (fc->ref)
        ff_vvc_report_progress(fc->ref, INT_MAX);
    return ret;
}

static int set_output_format(const VVCContext *s, const AVFrame *output)
{
    AVCodecContext *c = s->avctx;
    int ret;

    if (output->width != c->width || output->height != c->height) {
        if ((ret = ff_set_dimensions(c, output->width, output->height)) < 0)
            return ret;
    }
    c->pix_fmt = output->format;
    return 0;
}

static int wait_delayed_frame(VVCContext *s, AVFrame *output, int *got_output)
{
    VVCFrameContext *delayed = get_frame_context(s, s->fcs, s->nb_frames - s->nb_delayed);
    int ret = ff_vvc_frame_wait(s, delayed);

    if (!ret && delayed->output_frame->buf[0]) {
        av_frame_move_ref(output, delayed->output_frame);
        ret = set_output_format(s, output);
        if (!ret)
            *got_output = 1;
    }
    s->nb_delayed--;

    return ret;
}

static int submit_frame(VVCContext *s, VVCFrameContext *fc, AVFrame *output, int *got_output)
{
    int ret;
    s->nb_frames++;
    s->nb_delayed++;
    if (s->nb_delayed >= s->nb_fcs) {
        if ((ret = wait_delayed_frame(s, output, got_output)) < 0)
            return ret;
    }
    return 0;
}

static int vvc_decode_frame(AVCodecContext *avctx, AVFrame *output,
    int *got_output, AVPacket *avpkt)
{
    VVCContext *s = avctx->priv_data;
    VVCFrameContext *fc;
    int ret;

    if (!avpkt->size) {
        while (s->nb_delayed) {
            if ((ret = wait_delayed_frame(s, output, got_output)) < 0)
                return ret;
            if (*got_output)
                return 0;
        }
        if (s->nb_frames) {
            //we still have frames cached in dpb.
            VVCFrameContext *last = get_frame_context(s, s->fcs, s->nb_frames - 1);

            ret = ff_vvc_output_frame(s, last, output, 0, 1);
            if (ret < 0)
                return ret;
            if (ret) {
                *got_output = ret;
                if ((ret = set_output_format(s, output)) < 0)
                    return ret;
            }
        }
        return 0;
    }

    fc = get_frame_context(s, s->fcs, s->nb_frames);
    /* clear ph */
    fc->ps.ph = NULL;
    fc->nb_slices = 0;
    fc->decode_order = s->nb_frames;

    av_packet_unref(fc->avpkt);
    ret = av_packet_ref(fc->avpkt, avpkt);
    if (ret < 0)
        return ret;

    ret = decode_nal_units(s, fc, avpkt->data, avpkt->size);
    if (ret < 0)
        return ret;

    ret = submit_frame(s, fc, output, got_output);
    if (ret < 0)
        return ret;

    return avpkt->size;
}

static av_cold void frame_context_free(VVCFrameContext *fc)
{
    VVCFrameParamSets *ps = &fc->ps;
    int i;

    slices_free(fc);

    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        ff_vvc_unref_frame(fc, &fc->DPB[i], ~0);
        av_frame_free(&fc->DPB[i].frame);
    }

    ff_vvc_frame_thread_free(fc);
    pic_arrays_free(fc);
    av_frame_free(&fc->output_frame);

    av_packet_free(&fc->avpkt);
    ff_h2645_packet_uninit(&fc->pkt);

    av_buffer_unref(&ps->ph_buf);
    av_buffer_unref(&ps->pps_buf);
    av_buffer_unref(&ps->sps_buf);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->scaling_list); i++)
        av_buffer_unref(&ps->scaling_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->lmcs_list); i++)
        av_buffer_unref(&ps->lmcs_list[i]);
    for (i = 0; i < FF_ARRAY_ELEMS(ps->alf_list); i++)
        av_buffer_unref(&ps->alf_list[i]);
    av_freep(&fc->avctx);
}

static av_cold int frame_context_init(VVCFrameContext *fc, AVCodecContext *avctx)
{

    fc->avctx = av_memdup(avctx, sizeof(*avctx));
    if (!fc->avctx)
        goto fail;

    fc->output_frame = av_frame_alloc();
    if (!fc->output_frame)
        goto fail;

    for (int j = 0; j < FF_ARRAY_ELEMS(fc->DPB); j++) {
        fc->DPB[j].frame = av_frame_alloc();
        if (!fc->DPB[j].frame)
            goto fail;
        fc->DPB[j].tf.f = fc->DPB[j].frame;
    }

    fc->avpkt = av_packet_alloc();
    if (!fc->avpkt)
        goto fail;
    return 0;
fail:
    return AVERROR(ENOMEM);
}

static void vvc_decode_flush(AVCodecContext *avctx)
{
    VVCContext *s = avctx->priv_data;
    int got_output;
    AVFrame *output = av_frame_alloc();

    if (output) {
        while (s->nb_delayed) {
            wait_delayed_frame(s, output, &got_output);
            if (got_output) {
                av_frame_unref(output);
            }
        }
        av_frame_free(&output);
    }
}

static av_cold int vvc_decode_free(AVCodecContext *avctx)
{
    VVCContext *s = avctx->priv_data;
    int i;

    vvc_decode_flush(avctx);
    ff_executor_free(&s->executor);
    if (s->fcs) {
        for (i = 0; i < s->nb_fcs; i++)
            frame_context_free(s->fcs + i);
        av_free(s->fcs);
    }
    ff_vvc_ps_uninit(&s->ps);

    return 0;
}

static pthread_once_t once_control = PTHREAD_ONCE_INIT;

static void vvc_one_time_init(void)
{
    memset(&ff_vvc_default_scale_m, 16, sizeof(ff_vvc_default_scale_m));
}

#define VVC_MAX_FRMAE_DELAY 16
static av_cold int vvc_decode_init(AVCodecContext *avctx)
{
    VVCContext *s       = avctx->priv_data;
    int ret;
    TaskCallbacks callbacks = {
        s,
        sizeof(VVCLocalContext),
        ff_vvc_task_priority_higher,
        ff_vvc_task_ready,
        ff_vvc_task_run,
    };

    s->avctx = avctx;

    s->nb_fcs = (avctx->flags & AV_CODEC_FLAG_LOW_DELAY) ? 1 : FFMIN(av_cpu_count(), VVC_MAX_FRMAE_DELAY);
    s->fcs = av_calloc(s->nb_fcs, sizeof(*s->fcs));
    if (!s->fcs)
        goto fail;

    for (int i = 0; i < s->nb_fcs; i++) {
        VVCFrameContext *fc = s->fcs + i;
        ret = frame_context_init(fc, avctx);
        if (ret < 0)
            goto fail;
    }

    s->executor = ff_executor_alloc(&callbacks, s->nb_fcs * 3 / 2);
    s->eos = 1;
    GDR_SET_RECOVERED(s);
    pthread_once(&once_control, vvc_one_time_init);

    return 0;

fail:
    vvc_decode_free(avctx);
    return AVERROR(ENOMEM);
}

#define OFFSET(x) offsetof(VVCContext, x)
#define PAR (AV_OPT_FLAG_DECODING_PARAM | AV_OPT_FLAG_VIDEO_PARAM)

static const AVOption options[] = {
#if 0
    { "apply_defdispwin", "Apply default display window from VUI", OFFSET(apply_defdispwin),
        AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, PAR },
    { "strict-displaywin", "stricly apply default display window size", OFFSET(apply_defdispwin),
        AV_OPT_TYPE_BOOL, {.i64 = 0}, 0, 1, PAR },
#endif
    { NULL },
};

static const AVClass vvc_decoder_class = {
    .class_name = "vvc decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const FFCodec ff_vvc_decoder = {
    .p.name                  = "vvc",
    .p.long_name             = NULL_IF_CONFIG_SMALL("VVC (Versatile Video Coding)"),
    .p.type                  = AVMEDIA_TYPE_VIDEO,
    .p.id                    = AV_CODEC_ID_VVC,
    .priv_data_size          = sizeof(VVCContext),
    .p.priv_class            = &vvc_decoder_class,
    .init                    = vvc_decode_init,
    .close                   = vvc_decode_free,
    FF_CODEC_DECODE_CB(vvc_decode_frame),
    .flush                   = vvc_decode_flush,
    .p.capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal           = FF_CODEC_CAP_EXPORTS_CROPPING | FF_CODEC_CAP_INIT_CLEANUP |
                               FF_CODEC_CAP_AUTO_THREADS,
    .p.profiles              = NULL_IF_CONFIG_SMALL(ff_vvc_profiles),
};
