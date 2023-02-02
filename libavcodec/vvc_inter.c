/*
 * VVC inter predict
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
#include "vvc_mvs.h"
#include "vvc_thread.h"

static const int bcw_w_lut[] = {4, 5, 3, 10, -2};

static int emulated_edge(const VVCFrameContext *fc, uint8_t *dst, const uint8_t **src, ptrdiff_t *src_stride,
    const int x_off, const int y_off, const int block_w, const int block_h, const int is_luma)
{
    const int extra_before = is_luma ? LUMA_EXTRA_BEFORE : CHROMA_EXTRA_BEFORE;
    const int extra_after  = is_luma ? LUMA_EXTRA_AFTER : CHROMA_EXTRA_AFTER;
    const int extra        = is_luma ? LUMA_EXTRA : CHROMA_EXTRA;
    const int pic_width    = is_luma ? fc->ps.pps->width  : (fc->ps.pps->width >> fc->ps.sps->hshift[1]);
    const int pic_height   = is_luma ? fc->ps.pps->height : (fc->ps.pps->height >> fc->ps.sps->vshift[1]);

    if (x_off < extra_before || y_off < extra_before ||
        x_off >= pic_width - block_w - extra_after ||
        y_off >= pic_height - block_h - extra_after) {
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << fc->ps.sps->pixel_shift;
        int offset     = extra_before * *src_stride      + (extra_before << fc->ps.sps->pixel_shift);
        int buf_offset = extra_before * edge_emu_stride + (extra_before << fc->ps.sps->pixel_shift);

        fc->vdsp.emulated_edge_mc(dst, *src - offset, edge_emu_stride, *src_stride,
            block_w + extra, block_h + extra, x_off - extra_before, y_off - extra_before,
            pic_width, pic_height);

        *src = dst + buf_offset;
        *src_stride = edge_emu_stride;
        return 1;
    }
    return 0;
}

static void emulated_edge_dmvr(const VVCFrameContext *fc, uint8_t *dst, const uint8_t **src, ptrdiff_t *src_stride,
    const int x_sb, const int y_sb, const int x_off, const int y_off, const int block_w, const int block_h, const int is_luma)
{
    const int extra_before = is_luma ? LUMA_EXTRA_BEFORE : CHROMA_EXTRA_BEFORE;
    const int extra_after  = is_luma ? LUMA_EXTRA_AFTER : CHROMA_EXTRA_AFTER;
    const int extra        = is_luma ? LUMA_EXTRA : CHROMA_EXTRA;
    const int pic_width    = is_luma ? fc->ps.pps->width  : (fc->ps.pps->width >> fc->ps.sps->hshift[1]);
    const int pic_height   = is_luma ? fc->ps.pps->height : (fc->ps.pps->height >> fc->ps.sps->vshift[1]);

    if (x_off < extra_before || y_off < extra_before ||
        x_off >= pic_width - block_w - extra_after ||
        y_off >= pic_height - block_h - extra_after||
        (x_off != x_sb || y_off !=  y_sb)) {
        const int ps                    = fc->ps.sps->pixel_shift;
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << ps;
        const int offset                = extra_before * *src_stride + (extra_before << ps);
        const int buf_offset            = extra_before * edge_emu_stride + (extra_before << ps);

        const int start_x               = FFMIN(FFMAX(x_sb - extra_before, 0), pic_width  - 1);
        const int start_y               = FFMIN(FFMAX(y_sb - extra_before, 0), pic_height - 1);
        const int width                 = FFMAX(FFMIN(pic_width, x_sb + block_w + extra_after) - start_x, 1);
        const int height                = FFMAX(FFMIN(pic_height, y_sb + block_h + extra_after) - start_y, 1);

        fc->vdsp.emulated_edge_mc(dst, *src - offset, edge_emu_stride, *src_stride, block_w + extra, block_h + extra,
            x_off - start_x - extra_before, y_off - start_y - extra_before, width, height);

        *src = dst + buf_offset;
        *src_stride = edge_emu_stride;
   }
}

static void emulated_edge_bilinear(const VVCFrameContext *fc, uint8_t *dst, uint8_t **src, ptrdiff_t *src_stride,
    const int x_off, const int y_off, const int block_w, const int block_h)
{
    int pic_width   = fc->ps.pps->width;
    int pic_height  = fc->ps.pps->height;

    if (x_off < BILINEAR_EXTRA_BEFORE || y_off < BILINEAR_EXTRA_BEFORE ||
        x_off >= pic_width - block_w - BILINEAR_EXTRA_AFTER ||
        y_off >= pic_height - block_h - BILINEAR_EXTRA_AFTER) {
        const ptrdiff_t edge_emu_stride = EDGE_EMU_BUFFER_STRIDE << fc->ps.sps->pixel_shift;
        const int offset                = BILINEAR_EXTRA_BEFORE * *src_stride + (BILINEAR_EXTRA_BEFORE << fc->ps.sps->pixel_shift);
        const int buf_offset            = BILINEAR_EXTRA_BEFORE * edge_emu_stride + (BILINEAR_EXTRA_BEFORE << fc->ps.sps->pixel_shift);

        fc->vdsp.emulated_edge_mc(dst, *src - offset, edge_emu_stride, *src_stride, block_w + BILINEAR_EXTRA, block_h + BILINEAR_EXTRA,
            x_off - BILINEAR_EXTRA_BEFORE, y_off - BILINEAR_EXTRA_BEFORE,  pic_width, pic_height);

        *src = dst + buf_offset;
        *src_stride = edge_emu_stride;
    }
}


#define EMULATED_EDGE_LUMA(dst, src, src_stride, x_off, y_off)                      \
    emulated_edge(fc, dst, src, src_stride, x_off, y_off, block_w, block_h, 1)

#define EMULATED_EDGE_CHROMA(dst, src, src_stride, x_off, y_off)                    \
    emulated_edge(fc, dst, src, src_stride, x_off, y_off, block_w, block_h, 0)

#define EMULATED_EDGE_DMVR_LUMA(dst, src, src_stride, x_sb, y_sb, x_off, y_off)     \
    emulated_edge_dmvr(fc, dst, src, src_stride, x_sb, y_sb, x_off, y_off, block_w, block_h, 1)

#define EMULATED_EDGE_DMVR_CHROMA(dst, src, src_stride, x_sb, y_sb, x_off, y_off)   \
    emulated_edge_dmvr(fc, dst, src, src_stride, x_sb, y_sb, x_off, y_off, block_w, block_h, 0)

#define EMULATED_EDGE_BILINEAR(dst, src, src_stride, x_off, y_off)                  \
    emulated_edge_bilinear(fc, dst, src, src_stride, x_off, y_off, pred_w, pred_h)

// part of 8.5.6.6 Weighted sample prediction process
static int derive_weight_uni(int *denom, int *wx, int *ox,
    const VVCLocalContext *lc, const MvField *mvf, const int c_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCSH *sh             = &lc->sc->sh;
    const int weight_flag       = (IS_P(sh) && pps->weighted_pred_flag) ||
                                  (IS_B(sh) && pps->weighted_bipred_flag);
    if (weight_flag) {
        const int lx                = mvf->pred_flag - PF_L0;
        const PredWeightTable *w    = pps->wp_info_in_ph_flag ? &fc->ps.ph->pwt : &sh->pwt;

        *denom = w->log2_denom[c_idx > 0];
        *wx = w->weight[lx][c_idx][mvf->ref_idx[lx]];
        *ox = w->offset[lx][c_idx][mvf->ref_idx[lx]];
    }
    return weight_flag;
}

// part of 8.5.6.6 Weighted sample prediction process
static int derive_weight(int *denom, int *w0, int *w1, int *o0, int *o1,
    const VVCLocalContext *lc, const MvField *mvf, const int c_idx, const int dmvr_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCSH *sh             = &lc->sc->sh;
    const int bcw_idx           = mvf->bcw_idx;
    const int weight_flag       = (IS_P(sh) && pps->weighted_pred_flag) ||
                                  (IS_B(sh) && fc->ps.pps->weighted_bipred_flag && !dmvr_flag);
    if ((!weight_flag && !bcw_idx) || (bcw_idx && lc->cu->ciip_flag))
        return 0;

    if (bcw_idx) {
        *denom = 2;
        *w1 = bcw_w_lut[bcw_idx];
        *w0 = 8 - *w1;
        *o0 = *o1 = 0;
    } else {
        const VVCPPS *pps = fc->ps.pps;
        const PredWeightTable *w = pps->wp_info_in_ph_flag ? &fc->ps.ph->pwt : &sh->pwt;

        *denom = w->log2_denom[c_idx > 0];
        *w0 = w->weight[L0][c_idx][mvf->ref_idx[L0]];
        *w1 = w->weight[L1][c_idx][mvf->ref_idx[L1]];
        *o0 = w->offset[L0][c_idx][mvf->ref_idx[L0]];
        *o1 = w->offset[L1][c_idx][mvf->ref_idx[L1]];
    }
    return 1;
}

static void luma_mc(VVCLocalContext *lc, int16_t *dst, const AVFrame *ref, const Mv *mv,
    int x_off, int y_off, const int block_w, const int block_h)
{
    const VVCFrameContext *fc   = lc->fc;
    const uint8_t *src          = ref->data[0];
    ptrdiff_t src_stride        = ref->linesize[0];

    const int mx                = mv->x & 0xf;
    const int my                = mv->y & 0xf;

    x_off += mv->x >> 4;
    y_off += mv->y >> 4;
    src   += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);

    fc->vvcdsp.inter.put[LUMA][!!my][!!mx](dst, src, src_stride, block_h, mx, my, block_w, 0, 0);
}

static void chroma_mc(VVCLocalContext *lc, int16_t *dst, const AVFrame *ref, const Mv *mv,
    int x_off, int y_off, const int block_w, const int block_h, const int c_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const uint8_t *src          = ref->data[c_idx];
    ptrdiff_t src_stride        = ref->linesize[c_idx];
    int hs                      = fc->ps.sps->hshift[c_idx];
    int vs                      = fc->ps.sps->vshift[c_idx];

    const intptr_t mx           = av_mod_uintp2(mv->x, 4 + hs);
    const intptr_t my           = av_mod_uintp2(mv->y, 4 + vs);
    const intptr_t _mx          = mx << (1 - hs);
    const intptr_t _my          = my << (1 - vs);

    x_off += mv->x >> (4 + hs);
    y_off += mv->y >> (4 + vs);
    src  += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_CHROMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    fc->vvcdsp.inter.put[CHROMA][!!my][!!mx](dst, src, src_stride, block_h, _mx, _my, block_w, 0, 0);
}

static void luma_mc_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref, const MvField *mvf, int x_off, int y_off, const int block_w, const int block_h,
    const int hf_idx, const int vf_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const int lx                = mvf->pred_flag - PF_L0;
    const Mv *mv                = mvf->mv + lx;
    const uint8_t *src          = ref->data[0];
    ptrdiff_t src_stride        = ref->linesize[0];
    const int mx                = mv->x & 0xf;
    const int my                = mv->y & 0xf;
    int denom, wx, ox;

    x_off += mv->x >> 4;
    y_off += mv->y >> 4;
    src   += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);

    if (derive_weight_uni(&denom, &wx, &ox, lc, mvf, LUMA)) {
        fc->vvcdsp.inter.put_uni_w[LUMA][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, denom, wx, ox, mx, my, block_w, hf_idx, vf_idx);
    } else {
        fc->vvcdsp.inter.put_uni[LUMA][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, mx, my, block_w, hf_idx, vf_idx);
    }
}

static void luma_bdof(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *_src0, const ptrdiff_t src0_stride, const int mx0, const int my0,
    const uint8_t *_src1, const ptrdiff_t src1_stride, const int mx1, const int my1,
    const int block_w, const int block_h, const int hf_idx, const int vf_idx)
{
    const VVCFrameContext *fc = lc->fc;
    int16_t *tmp0       = lc->tmp  + 1 + MAX_PB_SIZE;
    int16_t *tmp1       = lc->tmp1 + 1 + MAX_PB_SIZE;

    fc->vvcdsp.inter.put[LUMA][!!my0][!!mx0](tmp0, _src0, src0_stride,
                                         block_h, mx0, my0, block_w, hf_idx, vf_idx);
    fc->vvcdsp.inter.bdof_fetch_samples(tmp0, _src0, src0_stride, mx0, my0, block_w, block_h);

    fc->vvcdsp.inter.put[LUMA][!!my1][!!mx1](tmp1, _src1, src1_stride,
                                         block_h, mx1, my1, block_w, hf_idx, vf_idx);
    fc->vvcdsp.inter.bdof_fetch_samples(tmp1, _src1, src1_stride, mx1, my1, block_w, block_h);
    fc->vvcdsp.inter.apply_bdof(dst, dst_stride, tmp0, tmp1, block_w, block_h);
}

 static void luma_mc_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref0, const Mv *mv0, const int x_off, const int y_off, const int block_w, const int block_h,
    const AVFrame *ref1, const Mv *mv1, const MvField *mvf, const int hf_idx, const int vf_idx,
    const MvField *orig_mv, const int dmvr_flag, const int sb_bdof_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    ptrdiff_t src0_stride       = ref0->linesize[0];
    ptrdiff_t src1_stride       = ref1->linesize[0];
    const int mx0               = mv0->x & 0xf;
    const int my0               = mv0->y & 0xf;
    const int mx1               = mv1->x & 0xf;
    const int my1               = mv1->y & 0xf;

    const int x_off0            = x_off + (mv0->x >> 4);
    const int y_off0            = y_off + (mv0->y >> 4);
    const int x_off1            = x_off + (mv1->x >> 4);
    const int y_off1            = y_off + (mv1->y >> 4);

    const uint8_t *src0         = ref0->data[0] + y_off0 * src0_stride + (int)((unsigned)x_off0 << fc->ps.sps->pixel_shift);
    const uint8_t *src1         = ref1->data[0] + y_off1 * src1_stride + (int)((unsigned)x_off1 << fc->ps.sps->pixel_shift);

    if (dmvr_flag) {
        const int x_sb0 = x_off + (orig_mv->mv[L0].x >> 4);
        const int y_sb0 = y_off + (orig_mv->mv[L0].y >> 4);
        const int x_sb1 = x_off + (orig_mv->mv[L1].x >> 4);
        const int y_sb1 = y_off + (orig_mv->mv[L1].y >> 4);
        EMULATED_EDGE_DMVR_LUMA(lc->edge_emu_buffer,  &src0, &src0_stride, x_sb0, y_sb0, x_off0, y_off0);
        EMULATED_EDGE_DMVR_LUMA(lc->edge_emu_buffer2, &src1, &src1_stride, x_sb1, y_sb1, x_off1, y_off1);
    } else {
        EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src0, &src0_stride, x_off0, y_off0);
        EMULATED_EDGE_LUMA(lc->edge_emu_buffer2, &src1, &src1_stride, x_off1, y_off1);
    }
    if (sb_bdof_flag) {
        luma_bdof(lc, dst, dst_stride, src0, src0_stride, mx0, my0, src1, src1_stride, mx1, my1,
            block_w, block_h, hf_idx, vf_idx);
    } else {
        int denom, w0, w1, o0, o1;
        fc->vvcdsp.inter.put[LUMA][!!my0][!!mx0](lc->tmp, src0, src0_stride,
            block_h, mx0, my0, block_w, hf_idx, vf_idx);
        if (derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, LUMA, dmvr_flag)) {
            fc->vvcdsp.inter.put_bi_w[LUMA][!!my1][!!mx1](dst, dst_stride, src1, src1_stride, lc->tmp,
                block_h, denom, w0, w1, o0, o1, mx1, my1, block_w, hf_idx, vf_idx);
        } else {
            fc->vvcdsp.inter.put_bi[LUMA][!!my1][!!mx1](dst, dst_stride, src1, src1_stride, lc->tmp,
                block_h, mx1, my1, block_w, hf_idx, vf_idx);
        }
    }
}

static void chroma_mc_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const uint8_t *src, ptrdiff_t src_stride, int x_off, int y_off,
    const int block_w, const int block_h, const MvField *mvf, const int c_idx,
    const int hf_idx, const int vf_idx)
{
    const VVCFrameContext *fc   = lc->fc;
    const int lx                = mvf->pred_flag - PF_L0;
    const int hs                = fc->ps.sps->hshift[1];
    const int vs                = fc->ps.sps->vshift[1];
    const Mv *mv                = &mvf->mv[lx];
    const intptr_t mx           = av_mod_uintp2(mv->x, 4 + hs);
    const intptr_t my           = av_mod_uintp2(mv->y, 4 + vs);
    const intptr_t _mx          = mx << (1 - hs);
    const intptr_t _my          = my << (1 - vs);
    int denom, wx, ox;

    x_off += mv->x >> (4 + hs);
    y_off += mv->y >> (4 + vs);
    src  += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_CHROMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    if (derive_weight_uni(&denom, &wx, &ox, lc, mvf, c_idx)) {
        fc->vvcdsp.inter.put_uni_w[CHROMA][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, denom, wx, ox, _mx, _my, block_w, hf_idx, vf_idx);
    } else {
        fc->vvcdsp.inter.put_uni[CHROMA][!!my][!!mx](dst, dst_stride, src, src_stride,
            block_h, _mx, _my, block_w, hf_idx, vf_idx);
    }
}

static void chroma_mc_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref0, const AVFrame *ref1, const int x_off, const int y_off,
    const int block_w, const int block_h,  const MvField *mvf, const int c_idx,
    const int hf_idx, const int vf_idx, const MvField *orig_mv, const int dmvr_flag, const int ciip_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const uint8_t *src0         = ref0->data[c_idx];
    const uint8_t *src1         = ref1->data[c_idx];
    ptrdiff_t src0_stride       = ref0->linesize[c_idx];
    ptrdiff_t src1_stride       = ref1->linesize[c_idx];
    const Mv *mv0               = &mvf->mv[0];
    const Mv *mv1               = &mvf->mv[1];
    const int hs                = fc->ps.sps->hshift[1];
    const int vs                = fc->ps.sps->vshift[1];

    const intptr_t mx0          = av_mod_uintp2(mv0->x, 4 + hs);
    const intptr_t my0          = av_mod_uintp2(mv0->y, 4 + vs);
    const intptr_t mx1          = av_mod_uintp2(mv1->x, 4 + hs);
    const intptr_t my1          = av_mod_uintp2(mv1->y, 4 + vs);
    const intptr_t _mx0         = mx0 << (1 - hs);
    const intptr_t _my0         = my0 << (1 - vs);
    const intptr_t _mx1         = mx1 << (1 - hs);
    const intptr_t _my1         = my1 << (1 - vs);

    const int x_off0            = x_off + (mv0->x >> (4 + hs));
    const int y_off0            = y_off + (mv0->y >> (4 + vs));
    const int x_off1            = x_off + (mv1->x >> (4 + hs));
    const int y_off1            = y_off + (mv1->y >> (4 + vs));
    int denom, w0, w1, o0, o1;

    src0  += y_off0 * src0_stride + (int)((unsigned)x_off0 << fc->ps.sps->pixel_shift);
    src1  += y_off1 * src1_stride + (int)((unsigned)x_off1 << fc->ps.sps->pixel_shift);

    if (dmvr_flag) {
        const int x_sb0 = x_off + (orig_mv->mv[L0].x >> (4 + hs));
        const int y_sb0 = y_off + (orig_mv->mv[L0].y >> (4 + vs));
        const int x_sb1 = x_off + (orig_mv->mv[L1].x >> (4 + hs));
        const int y_sb1 = y_off + (orig_mv->mv[L1].y >> (4 + vs));
        EMULATED_EDGE_DMVR_CHROMA(lc->edge_emu_buffer,  &src0, &src0_stride, x_sb0, y_sb0, x_off0, y_off0);
        EMULATED_EDGE_DMVR_CHROMA(lc->edge_emu_buffer2, &src1, &src1_stride, x_sb1, y_sb1, x_off1, y_off1);
    } else {
        EMULATED_EDGE_CHROMA(lc->edge_emu_buffer, &src0, &src0_stride, x_off0, y_off0);
        EMULATED_EDGE_CHROMA(lc->edge_emu_buffer2, &src1, &src1_stride, x_off1, y_off1);
    }

    fc->vvcdsp.inter.put[CHROMA][!!my0][!!mx0](lc->tmp, src0, src0_stride,
                                                block_h, _mx0, _my0, block_w, hf_idx, vf_idx);
    if (derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, c_idx, dmvr_flag)) {
        fc->vvcdsp.inter.put_bi_w[CHROMA][!!my1][!!mx1](dst, dst_stride, src1, src1_stride, lc->tmp,
            block_h, denom, w0, w1, o0, o1, _mx1, _my1, block_w, hf_idx, vf_idx);
    } else {
        fc->vvcdsp.inter.put_bi[CHROMA][!!my1][!!mx1](dst, dst_stride, src1, src1_stride, lc->tmp,
            block_h, _mx1, _my1, block_w, hf_idx, vf_idx);
    }
}

static void luma_prof_uni(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref, const MvField *mvf, int x_off, int y_off, const int block_w, const int block_h,
    const int cb_prof_flag, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const VVCFrameContext *fc   = lc->fc;
    const uint8_t *src          = ref->data[0];
    ptrdiff_t src_stride        = ref->linesize[0];
    uint16_t *prof_tmp          = lc->tmp + 1 + MAX_PB_SIZE;
    const int lx                = mvf->pred_flag - PF_L0;
    const Mv *mv                = mvf->mv + lx;
    const int mx                = mv->x & 0xf;
    const int my                = mv->y & 0xf;
    int denom, wx, ox;
    const int weight_flag       = derive_weight_uni(&denom, &wx, &ox, lc, mvf, LUMA);

    x_off += mv->x >> 4;
    y_off += mv->y >> 4;
    src   += y_off * src_stride + (x_off * (1 << fc->ps.sps->pixel_shift));

    EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src, &src_stride, x_off, y_off);
    if (cb_prof_flag) {
        fc->vvcdsp.inter.put[LUMA][!!my][!!mx](prof_tmp, src, src_stride, AFFINE_MIN_BLOCK_SIZE, mx, my, AFFINE_MIN_BLOCK_SIZE, 2, 2);
        fc->vvcdsp.inter.fetch_samples(prof_tmp, src, src_stride, mx, my);
        if (!weight_flag)
            fc->vvcdsp.inter.apply_prof_uni(dst, dst_stride, prof_tmp, diff_mv_x, diff_mv_y);
        else
            fc->vvcdsp.inter.apply_prof_uni_w(dst, dst_stride, prof_tmp, diff_mv_x, diff_mv_y, denom, wx, ox);
    } else {
        if (!weight_flag)
            fc->vvcdsp.inter.put_uni[LUMA][!!my][!!mx](dst, dst_stride, src, src_stride, block_h, mx, my, block_w, 2, 2);
        else
            fc->vvcdsp.inter.put_uni_w[LUMA][!!my][!!mx](dst, dst_stride, src, src_stride, block_h, denom, wx, ox, mx, my, block_w, 2, 2);
    }
}

static void luma_prof_bi(VVCLocalContext *lc, uint8_t *dst, const ptrdiff_t dst_stride,
    const AVFrame *ref0, const AVFrame *ref1, const MvField *mvf, const int x_off, const int y_off,
    const int block_w, const int block_h)
{
    const VVCFrameContext *fc   = lc->fc;
    const PredictionUnit *pu    = &lc->cu->pu;
    ptrdiff_t src0_stride       = ref0->linesize[0];
    ptrdiff_t src1_stride       = ref1->linesize[0];
    uint16_t *prof_tmp          = lc->tmp1 + 1 + MAX_PB_SIZE;
    const Mv *mv0               = mvf->mv + L0;
    const Mv *mv1               = mvf->mv + L1;
    const int mx0               = mv0->x & 0xf;
    const int my0               = mv0->y & 0xf;
    const int mx1               = mv1->x & 0xf;
    const int my1               = mv1->y & 0xf;
    const int x_off0            = x_off + (mv0->x >> 4);
    const int y_off0            = y_off + (mv0->y >> 4);
    const int x_off1            = x_off + (mv1->x >> 4);
    const int y_off1            = y_off + (mv1->y >> 4);

    const uint8_t *src0  = ref0->data[0] + y_off0 * src0_stride + (int)((unsigned)x_off0 << fc->ps.sps->pixel_shift);
    const uint8_t *src1  = ref1->data[0] + y_off1 * src1_stride + (int)((unsigned)x_off1 << fc->ps.sps->pixel_shift);

    int denom, w0, w1, o0, o1;
    const int weight_flag      = derive_weight(&denom, &w0, &w1, &o0, &o1, lc, mvf, LUMA, 0);

    EMULATED_EDGE_LUMA(lc->edge_emu_buffer, &src0, &src0_stride, x_off0, y_off0);
    EMULATED_EDGE_LUMA(lc->edge_emu_buffer2, &src1, &src1_stride, x_off1, y_off1);

    if (!pu->cb_prof_flag[L0]) {
        fc->vvcdsp.inter.put[LUMA][!!my0][!!mx0](lc->tmp, src0, src0_stride,
            block_h, mx0, my0, block_w, 2, 2);
    } else{
        fc->vvcdsp.inter.put[LUMA][!!my0][!!mx0](prof_tmp, src0, src0_stride, AFFINE_MIN_BLOCK_SIZE, mx0, my0, AFFINE_MIN_BLOCK_SIZE, 2, 2);
        fc->vvcdsp.inter.fetch_samples(prof_tmp, src0, src0_stride, mx0, my0);
        fc->vvcdsp.inter.apply_prof(lc->tmp, prof_tmp, pu->diff_mv_x[L0], pu->diff_mv_y[L0]);
    }
    if (!pu->cb_prof_flag[L1]) {
        if (weight_flag) {
            fc->vvcdsp.inter.put_bi_w[LUMA][!!my1][!!mx1](dst, dst_stride, src1, src1_stride, lc->tmp,
                block_h, denom, w0, w1, o0, o1, mx1, my1, block_w, 2, 2);
        } else {
            fc->vvcdsp.inter.put_bi[LUMA][!!my1][!!mx1](dst, dst_stride, src1, src1_stride, lc->tmp,
                block_h, mx1, my1, block_w, 2, 2);
        }
    } else {
        fc->vvcdsp.inter.put[LUMA][!!my1][!!mx1](prof_tmp, src1, src1_stride, AFFINE_MIN_BLOCK_SIZE, mx1, my1, AFFINE_MIN_BLOCK_SIZE, 2, 2);
        fc->vvcdsp.inter.fetch_samples(prof_tmp, src1, src1_stride, mx1, my1);
        if (weight_flag) {
            fc->vvcdsp.inter.apply_prof_bi_w(dst, dst_stride, lc->tmp, prof_tmp, pu->diff_mv_x[L1], pu->diff_mv_y[L1],
                denom, w0, w1, o0, o1);
        } else {
            fc->vvcdsp.inter.apply_prof_bi(dst, dst_stride,  lc->tmp, prof_tmp, pu->diff_mv_x[L1], pu->diff_mv_y[L1]);
        }
    }

}

//8.5.2.7 Derivation process for merge motion vector difference
static void derive_mmvd(MvField *mvf, const VVCFrameContext *fc, const Mv *mmvd_offset)
{
    Mv mmvd[2];

    if (mvf->pred_flag == PF_BI) {
        const RefPicList *refPicList = fc->ref->refPicList;
        const int poc = fc->ps.ph->poc;
        const int diff[] = {
            poc - refPicList[0].list[mvf->ref_idx[0]],
            poc - refPicList[1].list[mvf->ref_idx[1]]
        };
        const int sign = FFSIGN(diff[0]) != FFSIGN(diff[1]);

        if (diff[0] == diff[1]) {
            mmvd[1] = mmvd[0] = *mmvd_offset;
        } else {
            const int i = FFABS(diff[0]) < FFABS(diff[1]);
            const int o = !i;
            mmvd[i] = *mmvd_offset;
            if (!refPicList[0].isLongTerm[mvf->ref_idx[0]] && !refPicList[1].isLongTerm[mvf->ref_idx[1]]) {
                ff_vvc_mv_scale(&mmvd[o], mmvd_offset, diff[i], diff[o]);
            } else {
                mmvd[o].x = sign ? -mmvd[i].x : mmvd[i].x;
                mmvd[o].y = sign ? -mmvd[i].y : mmvd[i].y;
            }
        }
        mvf->mv[0].x += mmvd[0].x;
        mvf->mv[0].y += mmvd[0].y;
        mvf->mv[1].x += mmvd[1].x;
        mvf->mv[1].y += mmvd[1].y;
    } else {
        const int idx = mvf->pred_flag - PF_L0;
        mvf->mv[idx].x += mmvd_offset->x;
        mvf->mv[idx].y += mmvd_offset->y;
    }

}

static void mvf_to_mi(const MvField *mvf, MotionInfo *mi)
{
    mi->pred_flag = mvf->pred_flag;
    mi->bcw_idx = mvf->bcw_idx;
    mi->hpel_if_idx = mvf->hpel_if_idx;
    for (int i = 0; i < 2; i++) {
        const PredFlag mask = i + 1;
        if (mvf->pred_flag & mask) {
            mi->mv[i][0] = mvf->mv[i];
            mi->ref_idx[i] = mvf->ref_idx[i];
        }
    }
}

static void mv_merge_refine_pred_flag(MvField *mvf, const int width, const int height)
{
    if (mvf->pred_flag == PF_BI && (width + height) == 12) {
        mvf->pred_flag = PF_L0;
        mvf->bcw_idx   = 0;
    }
}

static int hls_merge_data(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPH  *ph            = fc->ps.ph;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCSH *sh             = &lc->sc->sh;
    CodingUnit *cu              = lc->cu;
    PredictionUnit *pu          = &cu->pu;
    MotionInfo *mi              = &pu->mi;
    const int cb_width          = cu->cb_width;
    const int cb_height         = cu->cb_height;
    MvField mvf;
    int merge_idx = 0;

    pu->merge_gpm_flag = 0;
    mi->num_sb_x = mi->num_sb_y = 1;
    if (cu->pred_mode == MODE_IBC) {
        avpriv_report_missing_feature(lc->fc->avctx, "Intra Block Copy");
        return AVERROR_PATCHWELCOME;
    } else {
        if (ph->max_num_subblock_merge_cand > 0 && cb_width >= 8 && cb_height >= 8) {
            pu->merge_subblock_flag = ff_vvc_merge_subblock_flag(lc);
        }
        if (pu->merge_subblock_flag) {
            int merge_subblock_idx = 0;
            ff_vvc_set_cb_tab(lc, fc->tab.msf, pu->merge_subblock_flag);
            if (ph->max_num_subblock_merge_cand > 1) {
                merge_subblock_idx = ff_vvc_merge_subblock_idx(lc, ph->max_num_subblock_merge_cand);
            }
            ff_vvc_sb_mv_merge_mode(lc, merge_subblock_idx, pu);
        } else {
            int regular_merge_flag = 1;
            const int is_128 = cb_width == 128 || cb_height == 128;
            const int ciip_avaiable = sps->ciip_enabled_flag &&
                !cu->skip_flag && (cb_width * cb_height >= 64);
            const int gpm_avaiable  = sps->gpm_enabled_flag && IS_B(sh) &&
                (cb_width >= 8) && (cb_height >=8) &&
                (cb_width < 8 * cb_height) && (cb_height < 8 *cb_width);
            if (!is_128 &&  (ciip_avaiable || gpm_avaiable)) {
                regular_merge_flag = ff_vvc_regular_merge_flag(lc, cu->skip_flag);
            }
            if (regular_merge_flag) {
                Mv mmvd_offset;
                if (sps->mmvd_enabled_flag) {
                    pu->mmvd_merge_flag = ff_vvc_mmvd_merge_flag(lc);
                }
                if (pu->mmvd_merge_flag) {
                    int mmvd_cand_flag = 0;
                    if (sps->max_num_merge_cand > 1) {
                        mmvd_cand_flag = ff_vvc_mmvd_cand_flag(lc);
                    }
                    ff_vvc_mmvd_offset_coding(lc, &mmvd_offset, ph->mmvd_fullpel_only_flag);
                    merge_idx = mmvd_cand_flag;
                } else if (sps->max_num_merge_cand > 1){
                    merge_idx = ff_vvc_merge_idx(lc);
                }
                ff_vvc_luma_mv_merge_mode(lc, merge_idx, 0, &mvf);
                if (pu->mmvd_merge_flag)
                    derive_mmvd(&mvf, fc, &mmvd_offset);
                mv_merge_refine_pred_flag(&mvf, cb_width, cb_height);
                ff_vvc_store_mvf(lc, &mvf);
                mvf_to_mi(&mvf, mi);
            } else {

                if (ciip_avaiable && gpm_avaiable) {
                    cu->ciip_flag = ff_vvc_ciip_flag(lc);
                } else {
                    cu->ciip_flag = sps->ciip_enabled_flag && !cu->skip_flag &&
                        !is_128 && (cb_width * cb_height >= 64);
                }
                if (cu->ciip_flag && sps->max_num_merge_cand > 1) {
                    merge_idx = ff_vvc_merge_idx(lc);
                }
                if (!cu->ciip_flag) {
                    int merge_gpm_idx[2];

                    pu->merge_gpm_flag = 1;
                    pu->gpm_partition_idx = ff_vvc_merge_gpm_partition_idx(lc);
                    merge_gpm_idx[0] = ff_vvc_merge_gpm_idx(lc, 0);
                    merge_gpm_idx[1] = 0;
                    if (sps->max_num_gpm_merge_cand > 2)
                        merge_gpm_idx[1] = ff_vvc_merge_gpm_idx(lc, 1);

                    ff_vvc_luma_mv_merge_gpm(lc, merge_gpm_idx, pu->gpm_mv);
                    ff_vvc_store_gpm_mvf(lc, pu);
                } else {
                    ff_vvc_luma_mv_merge_mode(lc, merge_idx, 1, &mvf);
                    mv_merge_refine_pred_flag(&mvf, cb_width, cb_height);
                    ff_vvc_store_mvf(lc, &mvf);
                    mvf_to_mi(&mvf, mi);
                    cu->intra_pred_mode_y   = cu->intra_pred_mode_c = INTRA_PLANAR;
                    cu->intra_luma_ref_idx  = 0;
                    cu->intra_mip_flag      = 0;
                }
            }
        }
    }
    return 0;
}

static void hls_mvd_coding(VVCLocalContext *lc, Mv* mvd)
{
    int16_t mv[2];
    int i;

    for (i = 0; i < 2; i++) {
        mv[i] = ff_vvc_abs_mvd_greater0_flag(lc);
    }
    for (i = 0; i < 2; i++) {
        if (mv[i])
            mv[i] += ff_vvc_abs_mvd_greater1_flag(lc);
    }
    for (i = 0; i < 2; i++) {
        if (mv[i] > 0) {
            if (mv[i] == 2)
                mv[i] += ff_vvc_abs_mvd_minus2(lc);
            mv[i] = (1 - 2 * ff_vvc_mvd_sign_flag(lc)) * mv[i];
        }
    }
    mvd->x = mv[0];
    mvd->y = mv[1];
}

static int bcw_idx_decode(VVCLocalContext *lc, const MotionInfo *mi, const int cb_width, const int cb_height)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCPH  *ph            = fc->ps.ph;
    const VVCSH *sh             = &lc->sc->sh;
    const PredWeightTable *w    = pps->wp_info_in_ph_flag ? &ph->pwt : &sh->pwt;
    int bcw_idx                 = 0;

    if (sps->bcw_enabled_flag && mi->pred_flag == PF_BI &&
        !w->weight_flag[L0][LUMA][mi->ref_idx[0]] &&
        !w->weight_flag[L1][LUMA][mi->ref_idx[1]] &&
        !w->weight_flag[L0][CHROMA][mi->ref_idx[0]] &&
        !w->weight_flag[L1][CHROMA][mi->ref_idx[1]] &&
        cb_width * cb_height >= 256) {
        bcw_idx = ff_vvc_bcw_idx(lc, ff_vvc_no_backward_pred_flag(fc));
    }
    return bcw_idx;
}

static int8_t ref_idx_decode(VVCLocalContext *lc, const VVCSH *sh, const int sym_mvd_flag, const int lx)
{
    int ref_idx = 0;
    if (sh->nb_refs[lx] > 1 && !sym_mvd_flag)
        ref_idx = ff_vvc_ref_idx_lx(lc, sh->nb_refs[lx]);
    else if (sym_mvd_flag)
        ref_idx = sh->ref_idx_sym[lx];
    return ref_idx;
}

static int mvp_data(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    PredictionUnit *pu          = &lc->cu->pu;
    const VVCSPS *sps           = fc->ps.sps;
    const VVCPH *ph             = fc->ps.ph;
    const VVCSH *sh             = &lc->sc->sh;
    MotionInfo *mi              = &pu->mi;
    const int cb_width          = cu->cb_width;
    const int cb_height         = cu->cb_height;

    int mvp_lx_flag[2] = {0};
    int cu_affine_type_flag = 0;
    int i, num_cp_mv;
    int amvr_enabled, has_no_zero_mvd = 0, amvr_shift;

    Mv mvds[2][MAX_CONTROL_POINTS];

    mi->pred_flag = ff_vvc_pred_flag(lc, IS_B(sh));
    if (sps->affine_enabled_flag && cb_width >= 16 && cb_height >= 16) {
        pu->inter_affine_flag = ff_vvc_inter_affine_flag(lc);
        ff_vvc_set_cb_tab(lc, fc->tab.iaf, pu->inter_affine_flag);
        if (sps->six_param_affine_enabled_flag && pu->inter_affine_flag)
            cu_affine_type_flag = ff_vvc_cu_affine_type_flag(lc);
    }
    mi->motion_model_idc = pu->inter_affine_flag + cu_affine_type_flag;
    num_cp_mv = mi->motion_model_idc + 1;

    if (sps->smvd_enabled_flag && !ph->mvd_l1_zero_flag &&
        mi->pred_flag == PF_BI && !pu->inter_affine_flag &&
        sh->ref_idx_sym[0] > -1 && sh->ref_idx_sym[1] > -1)
        pu->sym_mvd_flag = ff_vvc_sym_mvd_flag(lc);

    for (int i = L0; i <= L1; i++) {
        const PredFlag pred_flag = PF_L0 + !i;
        if (mi->pred_flag != pred_flag) {
            mi->ref_idx[i] = ref_idx_decode(lc, sh, pu->sym_mvd_flag, i);
            if (i == L1 && ph->mvd_l1_zero_flag && mi->pred_flag == PF_BI) {
                for (int j = 0; j < num_cp_mv; j++)
                    AV_ZERO64(&mvds[i][j]);
            } else {
                Mv *mvd0 = &mvds[i][0];
                if (i == L1 && pu->sym_mvd_flag) {
                    mvd0->x = -mvds[L0][0].x;
                    mvd0->y = -mvds[L0][0].y;
                }
                else
                    hls_mvd_coding(lc, mvd0);
                has_no_zero_mvd |= (mvd0->x || mvd0->y);
                for (int j = 1; j < num_cp_mv; j++) {
                    Mv *mvd = &mvds[i][j];
                    hls_mvd_coding(lc, mvd);
                    mvd->x += mvd0->x;
                    mvd->y += mvd0->y;
                    has_no_zero_mvd |= (mvd->x || mvd->y);
                }
            }
            mvp_lx_flag[i] = ff_vvc_mvp_lx_flag(lc);
        }
    }

    amvr_enabled = mi->motion_model_idc == MOTION_TRANSLATION ?
        sps->amvr_enabled_flag : sps->affine_amvr_enabled_flag;
    amvr_enabled &= has_no_zero_mvd;

    amvr_shift = ff_vvc_amvr_shift(lc, pu->inter_affine_flag, cu->pred_mode, amvr_enabled);

    mi->hpel_if_idx = amvr_shift == 3;
    mi->bcw_idx = bcw_idx_decode(lc, mi, cb_width, cb_height);

    if (mi->motion_model_idc)
        ff_vvc_affine_mvp(lc, mvp_lx_flag, amvr_shift, mi);
    else
        ff_vvc_mvp(lc, mvp_lx_flag, amvr_shift, mi);

    for (i = 0; i < 2; i++) {
        const PredFlag lx = i + 1;
        if (mi->pred_flag & lx) {
            for (int j = 0; j < num_cp_mv; j++) {
                    const Mv *mvd = &mvds[i][j];
                    mi->mv[i][j].x += mvd->x << amvr_shift;
                    mi->mv[i][j].y += mvd->y << amvr_shift;
            }
        }
    }
    if (mi->motion_model_idc)
        ff_vvc_store_sb_mvs(lc, pu);
    else
        ff_vvc_store_mv(lc, &pu->mi);

    return 0;
}

static void vvc_await_progress(const VVCFrameContext *fc, VVCFrame *ref,
    const Mv *mv, const int y0, const int height)
{
    //todo: check why we need magic number 9
    const int y = FFMAX(0, (mv->y >> 4) + y0 + height + 9);

    ff_vvc_await_progress(ref, y);
}

static int pred_await_progress(const VVCFrameContext *fc, VVCFrame *ref[2],
    const MvField *mv, const int y0, const int height)
{
    for (int mask = PF_L0; mask <= PF_L1; mask++) {
        if (mv->pred_flag & mask) {
            const int lx = mask - PF_L0;
            ref[lx] = fc->ref->refPicList[lx].ref[mv->ref_idx[lx]];
            if (!ref[lx])
                return AVERROR_INVALIDDATA;
            vvc_await_progress(fc, ref[lx], mv->mv + lx, y0, height);
        }
    }
    return 0;
}

#define POS(c_idx, x, y)                                                                            \
        &fc->frame->data[c_idx][((y) >> fc->ps.sps->vshift[c_idx]) * fc->frame->linesize[c_idx] +   \
            (((x) >> fc->ps.sps->hshift[c_idx]) << fc->ps.sps->pixel_shift)]

static void pred_gpm_blk(VVCLocalContext *lc)
{
    const VVCFrameContext *fc  = lc->fc;
    const CodingUnit *cu       = lc->cu;
    const PredictionUnit *pu   = &cu->pu;

    const uint8_t angle_idx   = ff_vvc_gpm_angle_idx[pu->gpm_partition_idx];
    const uint8_t weights_idx = ff_vvc_gpm_angle_to_weights_idx[angle_idx];
    const int w = av_log2(cu->cb_width) - 3;
    const int h = av_log2(cu->cb_height) - 3;
    const uint8_t off_x = ff_vvc_gpm_weights_offset_x[pu->gpm_partition_idx][h][w];
    const uint8_t off_y = ff_vvc_gpm_weights_offset_y[pu->gpm_partition_idx][h][w];
    const uint8_t mirror_type = ff_vvc_gpm_angle_to_mirror[angle_idx];
    const uint8_t *weights;

    const int c_end = fc->ps.sps->chroma_format_idc ? 3 : 1;

    int16_t *tmp[2] = {lc->tmp, lc->tmp1};
    const ptrdiff_t tmp_stride = MAX_PB_SIZE;

    for (int c_idx = 0; c_idx < c_end; c_idx++) {
        const int hs     = fc->ps.sps->hshift[c_idx];
        const int vs     = fc->ps.sps->vshift[c_idx];
        const int x      = lc->cu->x0  >> hs;
        const int y      = lc->cu->y0  >> vs;
        const int width  = cu->cb_width >> hs;
        const int height = cu->cb_height >> vs;
        uint8_t *dst = POS(c_idx, lc->cu->x0, lc->cu->y0);
        ptrdiff_t dst_stride = fc->frame->linesize[c_idx];

        int step_x = 1 << hs;
        int step_y = VVC_GPM_WEIGHT_SIZE << vs;
        if (!mirror_type) {
            weights = &ff_vvc_gpm_weights[weights_idx][off_y * VVC_GPM_WEIGHT_SIZE + off_x];
        } else if (mirror_type == 1) {
            step_x = -step_x;
            weights = &ff_vvc_gpm_weights[weights_idx][off_y * VVC_GPM_WEIGHT_SIZE + VVC_GPM_WEIGHT_SIZE - 1- off_x];
        } else {
            step_y = -step_y;
            weights = &ff_vvc_gpm_weights[weights_idx][(VVC_GPM_WEIGHT_SIZE - 1 - off_y) * VVC_GPM_WEIGHT_SIZE + off_x];
        }

        for (int i = 0; i < 2; i++) {
            const MvField *mv = pu->gpm_mv + i;
            const int lx = mv->pred_flag - PF_L0;
            VVCFrame *ref = fc->ref->refPicList[lx].ref[mv->ref_idx[lx]];
            if (!ref)
                return;
            if (c_idx) {
                chroma_mc(lc, tmp[i], ref->frame, mv->mv + lx, x, y, width, height, c_idx);
            } else {
                vvc_await_progress(fc, ref, mv->mv + lx, y, height);
                luma_mc(lc, tmp[i], ref->frame, mv->mv + lx, x, y, width, height);
            }
        }
        fc->vvcdsp.inter.put_gpm(dst, dst_stride, width, height, tmp[0], tmp[1], tmp_stride, weights, step_x, step_y);
    }
    return;
}

static int ciip_derive_intra_weight(const VVCLocalContext *lc, const int x0, const int y0,
    const int width, const int height)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCSPS *sps           = fc->ps.sps;
    const int x0b               = av_mod_uintp2(x0, sps->ctb_log2_size_y);
    const int y0b               = av_mod_uintp2(y0, sps->ctb_log2_size_y);
    const int available_l       = lc->ctb_left_flag || x0b;
    const int available_u       = lc->ctb_up_flag || y0b;
    const int min_pu_width      = fc->ps.pps->min_pu_width;

    int w = 1;

    if (available_u &&fc->ref->tab_mvf[((y0 - 1) >> MIN_PU_LOG2) * min_pu_width + ((x0 - 1 + width)>> MIN_PU_LOG2)].pred_flag == PF_INTRA)
        w++;

    if (available_l && fc->ref->tab_mvf[((y0 - 1 + height)>> MIN_PU_LOG2) * min_pu_width + ((x0 - 1) >> MIN_PU_LOG2)].pred_flag == PF_INTRA)
        w++;

    return w;
}

static void pred_regular_luma(VVCLocalContext *lc, const int hf_idx, const int vf_idx, const MvField *mv,
    const int x0, const int y0, const int sbw, const int sbh, const MvField *orig_mv, const int dmvr_flag, const int sb_bdof_flag)
{
    const SliceContext *sc          = lc->sc;
    const VVCFrameContext *fc       = lc->fc;
    const int ciip_flag             = lc->cu->ciip_flag;
    uint8_t *dst                    = POS(0, x0, y0);
    const ptrdiff_t dst_stride      = fc->frame->linesize[0];
    uint8_t *inter                  = ciip_flag ? (uint8_t *)lc->ciip_tmp1 : dst;
    const ptrdiff_t inter_stride    = ciip_flag ? (MAX_PB_SIZE * sizeof(uint16_t)) : dst_stride;
    VVCFrame *ref[2];

    if (pred_await_progress(fc, ref, mv, y0, sbh) < 0)
        return;

    if (mv->pred_flag != PF_BI) {
        const int lx = mv->pred_flag - PF_L0;
        luma_mc_uni(lc, inter, inter_stride, ref[lx]->frame,
            mv, x0, y0, sbw, sbh, hf_idx, vf_idx);
    } else {
        luma_mc_bi(lc, inter, inter_stride, ref[0]->frame,
            &mv->mv[0], x0, y0, sbw, sbh, ref[1]->frame, &mv->mv[1], mv,
            hf_idx, vf_idx, orig_mv, dmvr_flag, sb_bdof_flag);

    }

    if (ciip_flag) {
        const int intra_weight = ciip_derive_intra_weight(lc, x0, y0, sbw, sbh);
        fc->vvcdsp.intra.intra_pred(lc, x0, y0, sbw, sbh, 0);
        if (sc->sh.lmcs_used_flag)
            fc->vvcdsp.lmcs.filter(inter, inter_stride, sbw, sbh, fc->ps.ph->lmcs_fwd_lut);
        fc->vvcdsp.inter.put_ciip(dst, dst_stride, sbw, sbh, inter, inter_stride, intra_weight);

    }
}

static void pred_regular_chroma(VVCLocalContext *lc, const MvField *mv,
    const int x0, const int y0, const int sbw, const int sbh, const MvField *orig_mv, const int dmvr_flag)
{
    const VVCFrameContext *fc   = lc->fc;
    const int hs                = fc->ps.sps->hshift[1];
    const int vs                = fc->ps.sps->vshift[1];
    const int x0_c              = x0 >> hs;
    const int y0_c              = y0 >> vs;
    const int w_c               = sbw >> hs;
    const int h_c               = sbh >> vs;
    const int do_ciip           = lc->cu->ciip_flag && (w_c > 2);

    uint8_t* dst1               = POS(1, x0, y0);
    uint8_t* dst2               = POS(2, x0, y0);
    const ptrdiff_t dst1_stride = fc->frame->linesize[1];
    const ptrdiff_t dst2_stride = fc->frame->linesize[2];

    uint8_t *inter1 = do_ciip ? (uint8_t *)lc->ciip_tmp1 : dst1;
    const ptrdiff_t inter1_stride = do_ciip ? (MAX_PB_SIZE * sizeof(uint16_t)) : dst1_stride;

    uint8_t *inter2 = do_ciip ? (uint8_t *)lc->ciip_tmp2 : dst2;
    const ptrdiff_t inter2_stride = do_ciip ? (MAX_PB_SIZE * sizeof(uint16_t)) : dst2_stride;

    //fix me
    const int hf_idx = 0;
    const int vf_idx = 0;
    if (mv->pred_flag != PF_BI) {
        const int lx = mv->pred_flag - PF_L0;
        VVCFrame* ref = fc->ref->refPicList[lx].ref[mv->ref_idx[lx]];
        if (!ref)
            return;
        chroma_mc_uni(lc, inter1, inter1_stride, ref->frame->data[1], ref->frame->linesize[1],
            x0_c, y0_c, w_c, h_c, mv, CB, hf_idx, vf_idx);
        chroma_mc_uni(lc, inter2, inter2_stride, ref->frame->data[2], ref->frame->linesize[2],
            x0_c, y0_c, w_c, h_c, mv, CR, hf_idx, vf_idx);
    } else {
        VVCFrame* ref0 = fc->ref->refPicList[0].ref[mv->ref_idx[0]];
        VVCFrame* ref1 = fc->ref->refPicList[1].ref[mv->ref_idx[1]];
        if (!ref0 || !ref1)
            return;
        chroma_mc_bi(lc, inter1, inter1_stride, ref0->frame, ref1->frame,
            x0_c, y0_c, w_c, h_c, mv, CB, hf_idx, vf_idx, orig_mv, dmvr_flag, lc->cu->ciip_flag);

        chroma_mc_bi(lc, inter2, inter2_stride, ref0->frame, ref1->frame,
            x0_c, y0_c, w_c, h_c, mv, CR, hf_idx, vf_idx, orig_mv, dmvr_flag, lc->cu->ciip_flag);

    }
    if (do_ciip) {
        const int intra_weight = ciip_derive_intra_weight(lc, x0, y0, sbw, sbh);
        fc->vvcdsp.intra.intra_pred(lc, x0, y0, sbw, sbh, 1);
        fc->vvcdsp.intra.intra_pred(lc, x0, y0, sbw, sbh, 2);
        fc->vvcdsp.inter.put_ciip(dst1, dst1_stride, w_c, h_c, inter1, inter1_stride, intra_weight);
        fc->vvcdsp.inter.put_ciip(dst2, dst2_stride, w_c, h_c, inter2, inter2_stride, intra_weight);

    }
}

// derive bdofFlag from 8.5.6 Decoding process for inter blocks
// derive dmvr from 8.5.1 General decoding process for coding units coded in inter prediction mode
static void derive_dmvr_bdof_flag(VVCLocalContext *lc, int *dmvr_flag, int *bdof_flag, const PredictionUnit* pu)
{
    const VVCFrameContext *fc   = lc->fc;
    const VVCPPS *pps           = fc->ps.pps;
    const VVCPH *ph             = fc->ps.ph;
    const VVCSH *sh             = &lc->sc->sh;
    const int poc               = ph->poc;
    const RefPicList *rpl0      = fc->ref->refPicList + L0;
    const RefPicList *rpl1      = fc->ref->refPicList + L1;
    const int8_t *ref_idx       = pu->mi.ref_idx;
    const MotionInfo *mi        = &pu->mi;
    const CodingUnit *cu        = lc->cu;
    const PredWeightTable *w    = pps->wp_info_in_ph_flag ? &fc->ps.ph->pwt : &sh->pwt;

    *dmvr_flag = 0;
    *bdof_flag = 0;

    if (mi->pred_flag == PF_BI &&
        (poc - rpl0->list[ref_idx[L0]] == rpl1->list[ref_idx[L1]] - poc) &&
        !rpl0->isLongTerm[ref_idx[L0]] && !rpl1->isLongTerm[ref_idx[L1]] &&
        !cu->ciip_flag &&
        !mi->bcw_idx &&
        !w->weight_flag[L0][LUMA][mi->ref_idx[L0]] && !w->weight_flag[L1][LUMA][mi->ref_idx[L1]] &&
        !w->weight_flag[L0][CHROMA][mi->ref_idx[L0]] && !w->weight_flag[L1][CHROMA][mi->ref_idx[L1]] &&
        cu->cb_width >= 8 && cu->cb_height >= 8 &&
        (cu->cb_width * cu->cb_height >= 128)) {
        // fixme: for RprConstraintsActiveFlag
        if (!ph->bdof_disabled_flag &&
            mi->motion_model_idc == MOTION_TRANSLATION &&
            !pu->merge_subblock_flag &&
            !pu->sym_mvd_flag)
            *bdof_flag = 1;
        if (!ph->dmvr_disabled_flag &&
            pu->general_merge_flag &&
            !pu->mmvd_merge_flag)
            *dmvr_flag = 1;

    }
}

// 8.5.3.5 Parametric motion vector refinement process
static int parametric_mv_refine(const int *sad, const int stride)
{
    const int sad_minus  = sad[-stride];
    const int sad_center = sad[0];
    const int sad_plus   = sad[stride];
    int dmvc;
    int denom = (( sad_minus + sad_plus) - (sad_center << 1 ) ) << 3;
    if (!denom)
        dmvc = 0;
    else {
        if (sad_minus == sad_center)
            dmvc = -8;
        else if (sad_plus == sad_center)
            dmvc = 8;
        else {
            int num = ( sad_minus - sad_plus ) << 4;
            int sign_num = 0;
            int quotient = 0;
            int counter = 3;
            if (num < 0 ) {
                num = - num;
                sign_num = 1;
            }
            while( counter > 0 ) {
                counter = counter - 1;
                quotient = quotient << 1;
                if ( num >= denom ) {
                    num = num - denom;
                    quotient = quotient + 1;
                }
                denom = (denom >> 1);
            }
            if (sign_num == 1 )
                dmvc = -quotient;
            else
                dmvc = quotient;
        }
    }
    return dmvc;
}

#define SAD_ARRAY_SIZE 5
//8.5.3 Decoder-side motion vector refinement process
static void dmvr_mv_refine(VVCLocalContext *lc, MvField *mv, MvField *orig_mv, int *sb_bdof_flag,
    const AVFrame *ref0, const AVFrame *ref1, const int x_off, const int y_off, const int block_w, const int block_h)
{
    const VVCFrameContext *fc   = lc->fc;
    ptrdiff_t src0_stride       = ref0->linesize[0];
    ptrdiff_t src1_stride       = ref1->linesize[0];
    Mv *mv0                     = mv->mv + L0;
    Mv *mv1                     = mv->mv + L1;
    const int sr_range          = 2;
    const int mx0               = mv0->x & 0xf;
    const int my0               = mv0->y & 0xf;
    const int mx1               = mv1->x & 0xf;
    const int my1               = mv1->y & 0xf;
    const int x_off0            = x_off + (mv0->x >> 4) - sr_range;
    const int y_off0            = y_off + (mv0->y >> 4) - sr_range;
    const int x_off1            = x_off + (mv1->x >> 4) - sr_range;
    const int y_off1            = y_off + (mv1->y >> 4) - sr_range;
    const int pred_w            = block_w + 2 * sr_range;
    const int pred_h            = block_h + 2 * sr_range;

    uint8_t *src0               = ref0->data[0] + y_off0 * src0_stride + (int)((unsigned)x_off0 << fc->ps.sps->pixel_shift);
    uint8_t *src1               = ref1->data[0] + y_off1 * src1_stride + (int)((unsigned)x_off1 << fc->ps.sps->pixel_shift);

    int sad[SAD_ARRAY_SIZE][SAD_ARRAY_SIZE];
    int min_dx, min_dy, min_sad, dx, dy;

    *orig_mv = *mv;
    min_dx = min_dy = dx = dy = 2;

    EMULATED_EDGE_BILINEAR(lc->edge_emu_buffer, &src0, &src0_stride, x_off0, y_off0);
    EMULATED_EDGE_BILINEAR(lc->edge_emu_buffer2, &src1, &src1_stride, x_off1, y_off1);
    fc->vvcdsp.inter.dmvr[!!my0][!!mx0](lc->tmp, src0, src0_stride, pred_h, mx0, my0, pred_w);
    fc->vvcdsp.inter.dmvr[!!my1][!!mx1](lc->tmp1, src1, src1_stride, pred_h, mx1, my1, pred_w);

    min_sad = fc->vvcdsp.inter.sad(lc->tmp, lc->tmp1, dx, dy, block_w, block_h);
    min_sad -= min_sad >> 2;
    sad[dy][dx] = min_sad;

    if (min_sad >= block_w * block_h) {
        int dmv[2];
        // 8.5.3.4 Array entry selection process
        for (dy = 0; dy < SAD_ARRAY_SIZE; dy++) {
            for (dx = 0; dx < SAD_ARRAY_SIZE; dx++) {
                if (dx != sr_range || dy != sr_range) {
                    sad[dy][dx] = fc->vvcdsp.inter.sad(lc->tmp, lc->tmp1, dx, dy, block_w, block_h);
                    if (sad[dy][dx] < min_sad) {
                        min_sad = sad[dy][dx];
                        min_dx = dx;
                        min_dy = dy;
                    }
                }
            }
        }
        dmv[0] = (min_dx - sr_range) << 4;
        dmv[1] = (min_dy - sr_range) << 4;
        if (min_dx != 0 && min_dx != 4 && min_dy != 0 && min_dy != 4) {
            dmv[0] += parametric_mv_refine(&sad[min_dy][min_dx], 1);
            dmv[1] += parametric_mv_refine(&sad[min_dy][min_dx], SAD_ARRAY_SIZE);
        }
        mv0->x += dmv[0];
        mv0->y += dmv[1];
        mv1->x += -dmv[0];
        mv1->y += -dmv[1];
        ff_vvc_clip_mv(mv0);
        ff_vvc_clip_mv(mv1);
    }
    if (min_sad < 2 * block_w * block_h) {
        *sb_bdof_flag = 0;
    }
}
static void derive_sb_mv(VVCLocalContext *lc, MvField *mv, MvField *orig_mv, int *sb_bdof_flag,
    const int x0, const int y0, const int sbw, const int sbh, const int dmvr_flag, const int bdof_flag)
{
    VVCFrameContext *fc = lc->fc;

    *orig_mv = *mv = *ff_vvc_get_mvf(fc, x0, y0);
    if (bdof_flag)
        *sb_bdof_flag = 1;
    if (dmvr_flag) {
        VVCFrame* ref[2];
        if (pred_await_progress(fc, ref, mv, y0, sbh) < 0)
            return;
        dmvr_mv_refine(lc, mv, orig_mv, sb_bdof_flag, ref[0]->frame, ref[1]->frame, x0, y0, sbw, sbh);
        ff_vvc_set_dmvr_info(fc, x0, y0, sbw, sbh, mv);
    }
}

static void pred_regular_blk(VVCLocalContext *lc, const int skip_ciip)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const PredictionUnit *pu    = &cu->pu;
    const MotionInfo *mi        = &pu->mi;
    MvField mv, orig_mv;
    int sbw, sbh, num_sb_x, num_sb_y, sb_bdof_flag = 0;
    int dmvr_flag, bdof_flag;

    if (cu->ciip_flag && skip_ciip)
        return;

    derive_dmvr_bdof_flag(lc, &dmvr_flag, &bdof_flag, pu);
    num_sb_x = mi->num_sb_x;
    num_sb_y = mi->num_sb_y;
    if (dmvr_flag || bdof_flag) {
        num_sb_x = (cu->cb_width > 16) ? (cu->cb_width >> 4) : 1;
        num_sb_y = (cu->cb_height > 16) ? (cu->cb_height >> 4) : 1;
    }
    sbw = cu->cb_width / num_sb_x;
    sbh = cu->cb_height / num_sb_y;

    for (int sby = 0; sby < num_sb_y; sby++) {
        for (int sbx = 0; sbx < num_sb_x; sbx++) {
            const int x0 = cu->x0 + sbx * sbw;
            const int y0 = cu->y0 + sby * sbh;

            if (cu->ciip_flag)
                ff_vvc_set_neighbour_available(lc, x0, y0, sbw, sbh);

            derive_sb_mv(lc, &mv, &orig_mv, &sb_bdof_flag, x0, y0, sbw, sbh, dmvr_flag, bdof_flag);
            pred_regular_luma(lc, mi->hpel_if_idx, mi->hpel_if_idx, &mv, x0, y0, sbw, sbh, &orig_mv, dmvr_flag, sb_bdof_flag);
            if (fc->ps.sps->chroma_format_idc)
                pred_regular_chroma(lc, &mv, x0, y0, sbw, sbh, &orig_mv, dmvr_flag);
        }
    }
}

static void derive_affine_mvc(MvField *mvc, const VVCFrameContext *fc, const MvField *mv,
    const int x0, const int y0, const int sbw, const int sbh)
{
    const int hs = fc->ps.sps->hshift[1];
    const int vs = fc->ps.sps->vshift[1];
    const MvField* mv2 = ff_vvc_get_mvf(fc, x0 + hs * sbw, y0 + vs * sbh);
    *mvc = *mv;
    mvc->mv[0].x += mv2->mv[0].x;
    mvc->mv[0].y += mv2->mv[0].y;
    mvc->mv[1].x += mv2->mv[1].x;
    mvc->mv[1].y += mv2->mv[1].y;
    ff_vvc_round_mv(mvc->mv + 0, 0, 1);
    ff_vvc_round_mv(mvc->mv + 1, 0, 1);
}

static void pred_affine_blk(VVCLocalContext *lc)
{
    const VVCFrameContext *fc  = lc->fc;
    const CodingUnit *cu       = lc->cu;
    const PredictionUnit *pu   = &cu->pu;
    const MotionInfo *mi       = &pu->mi;
    const int x0   = cu->x0;
    const int y0   = cu->y0;
    const int sbw  = cu->cb_width / mi->num_sb_x;
    const int sbh  = cu->cb_height / mi->num_sb_y;
    const int hs = fc->ps.sps->hshift[1];
    const int vs = fc->ps.sps->vshift[1];

    for (int sby = 0; sby < mi->num_sb_y; sby++) {
        for (int sbx = 0; sbx < mi->num_sb_x; sbx++) {
            const int x = x0 + sbx * sbw;
            const int y = y0 + sby * sbh;

            uint8_t *dst0 = POS(0, x, y);
            const MvField *mv = ff_vvc_get_mvf(fc, x, y);
            VVCFrame *ref[2];

            if (pred_await_progress(fc, ref, mv, y, sbh) < 0)
                return;

            if (mi->pred_flag != PF_BI) {
                const int lx = mi->pred_flag - PF_L0;
                luma_prof_uni(lc, dst0, fc->frame->linesize[0], ref[lx]->frame,
                    mv, x, y, sbw, sbh, pu->cb_prof_flag[lx],
                    pu->diff_mv_x[lx], pu->diff_mv_y[lx]);
            } else {
                luma_prof_bi(lc, dst0, fc->frame->linesize[0], ref[0]->frame, ref[1]->frame,
                    mv, x, y, sbw, sbh);
            }
            if (fc->ps.sps->chroma_format_idc) {
                if (!av_mod_uintp2(sby, vs) && !av_mod_uintp2(sbx, hs)) {
                    MvField mvc;
                    derive_affine_mvc(&mvc, fc, mv, x, y, sbw, sbh);
                    pred_regular_chroma(lc, &mvc, x, y, sbw<<hs, sbh<<vs, NULL, 0);

                }
            }

        }
    }
}

int ff_vvc_inter_data(VVCLocalContext *lc)
{
    const CodingUnit *cu    = lc->cu;
    PredictionUnit *pu      = &lc->cu->pu;
    const MotionInfo *mi    = &pu->mi;
    int ret                 = 0;

    pu->general_merge_flag = 1;
    if (!cu->skip_flag)
        pu->general_merge_flag = ff_vvc_general_merge_flag(lc);

    if (pu->general_merge_flag) {
        hls_merge_data(lc);
    } else if (cu->pred_mode == MODE_IBC){
        avpriv_report_missing_feature(lc->fc->avctx, "Intra Block Copy");
        return AVERROR_PATCHWELCOME;
    } else {
        ret = mvp_data(lc);
    }
    if (!pu->merge_gpm_flag && !pu->inter_affine_flag && !pu->merge_subblock_flag)
        ff_vvc_update_hmvp(lc, mi);
    return ret;
}

static void predict_inter(VVCLocalContext *lc)
{
    const VVCFrameContext *fc   = lc->fc;
    const CodingUnit *cu        = lc->cu;
    const PredictionUnit *pu    = &cu->pu;

    if (pu->merge_gpm_flag)
        pred_gpm_blk(lc);
    else if (pu->inter_affine_flag)
        pred_affine_blk(lc);
    else
        pred_regular_blk(lc, 1);    //intra block is not ready yet, skip ciip
    if (lc->sc->sh.lmcs_used_flag && !cu->ciip_flag) {
        uint8_t* dst0 = POS(0, cu->x0, cu->y0);
        fc->vvcdsp.lmcs.filter(dst0, fc->frame->linesize[LUMA], cu->cb_width, cu->cb_height, fc->ps.ph->lmcs_fwd_lut);
    }
}

int ff_vvc_predict_inter(VVCLocalContext *lc, const int rs)
{
    const VVCFrameContext *fc   = lc->fc;
    const CTU *ctu              = fc->tab.ctus + rs;
    CodingUnit *cu              = ctu->cus;

    while (cu) {
        lc->cu = cu;
        if (cu->pred_mode != MODE_INTRA && cu->pred_mode != MODE_PLT && cu->tree_type != DUAL_TREE_CHROMA)
            predict_inter(lc);
        cu = cu->next;
    }

    return 0;
}

void ff_vvc_predict_ciip(VVCLocalContext *lc)
{
    av_assert0(lc->cu->ciip_flag);

    //todo: refact out ciip from pred_regular_blk
    pred_regular_blk(lc, 0);
}

#undef POS
