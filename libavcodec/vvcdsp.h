/*
 * VVC video decoder
 *
 * Copyright (C) 2021 Nuo Mi
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

#ifndef AVCODEC_VVCDSP_H
#define AVCODEC_VVCDSP_H

#include "libavutil/mem_internal.h"

#include "get_bits.h"

#define MAX_PB_SIZE 128

enum TxType {
    DCT2,
    DST7,
    DCT8,
    N_TX_TYPE,
};

enum TxSize {
    TX_SIZE_2,
    TX_SIZE_4,
    TX_SIZE_8,
    TX_SIZE_16,
    TX_SIZE_32,
    TX_SIZE_64,
    N_TX_SIZE,
};

typedef struct SAOParams {
    int offset_abs[3][4];   ///< sao_offset_abs
    int offset_sign[3][4];  ///< sao_offset_sign

    uint8_t band_position[3];   ///< sao_band_position

    int eo_class[3];        ///< sao_eo_class

    int16_t offset_val[3][5];   ///<SaoOffsetVal

    uint8_t type_idx[3];    ///< sao_type_idx
} SAOParams;

typedef struct ALFParams {
    uint8_t ctb_flag[3];                //< alf_ctb_flag[]
    uint8_t ctb_filt_set_idx_y;         //< AlfCtbFiltSetIdxY
    uint8_t alf_ctb_filter_alt_idx[2];  //< alf_ctb_filter_alt_idx[]
    uint8_t ctb_cc_idc[2];              //< alf_ctb_cc_cb_idc, alf_ctb_cc_cr_idc

    uint8_t applied[3];
} ALFParams;

typedef struct VVCInterDSPContext {
    void (*put[2 /* luma, chroma */][2 /* int, frac */][2 /* int, frac */])(
        int16_t *dst, const uint8_t *src, ptrdiff_t src_stride,
        int height, intptr_t mx, intptr_t my, int width, int hf_idx, int vf_idx);

    void (*put_uni[2 /* luma, chroma */][2 /* int, frac */][2 /* int, frac */])(
        uint8_t *dst, ptrdiff_t dst_stride,
        const uint8_t *src, ptrdiff_t src_stride, int height,
        intptr_t mx, intptr_t my, int width, int hf_idx, int vf_idx);
    void (*put_uni_w[2 /* luma, chroma */][2 /* int, frac */][2 /* int, frac */])(
        uint8_t *dst, ptrdiff_t dst_stride,
        const uint8_t *src, ptrdiff_t src_stride, int height, int denom, int wx, int ox,
        intptr_t mx, intptr_t my, int width, int hf_idx, int vf_idx);

    void (*put_bi[2 /* luma, chroma */][2 /* int, frac */][2 /* int, frac */])(
        uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride, const int16_t *src2,
        int height, intptr_t mx, intptr_t my, int width, int hf_idx, int vf_idx);
    void (*put_bi_w[2 /* luma, chroma */][2 /* int, frac */][2 /* int, frac */])(
        uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *_src, ptrdiff_t _src_stride, const int16_t *src2,
        int height, int denom, int wx0, int wx1, int ox0, int ox1,
        intptr_t mx, intptr_t my, int width, int hf_idx, int vf_idx);

    void (*put_ciip)(uint8_t *dst, ptrdiff_t dst_stride, int width, int height,
        const uint8_t *inter, ptrdiff_t inter_stride, int inter_weight);

    void (*put_gpm)(uint8_t *dst, ptrdiff_t dst_stride, int width, int height,
        const int16_t *tmp, const int16_t *tmp1, const ptrdiff_t tmp_stride,
        const uint8_t *weights, int step_x, int step_y);

    void (*fetch_samples)(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride, int x_frac, int y_frac);
    void (*bdof_fetch_samples)(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride, int x_frac, int y_frac,
        int width, int height);

    void (*prof_grad_filter)(int16_t *gradient_h, int16_t *gradient_v, const ptrdiff_t gradient_stride,
        const int16_t *src, const ptrdiff_t src_stride, int width, int height, const int pad);
    void (*apply_prof)(int16_t *dst, const int16_t *src, const int16_t *diff_mv_x, const int16_t *diff_mv_y);

    void (*apply_prof_uni)(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src,
        const int16_t *diff_mv_x, const int16_t *diff_mv_y);
    void (*apply_prof_uni_w)(uint8_t *dst, const ptrdiff_t dst_stride, const int16_t *src,
        const int16_t *diff_mv_x, const int16_t *diff_mv_y, int denom, int wx, int ox);

    void (*apply_prof_bi)(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src0, const int16_t *src1,
        const int16_t *diff_mv_x, const int16_t *diff_mv_y);
    void (*apply_prof_bi_w)(uint8_t *dst, ptrdiff_t dst_stride, const int16_t *src0, const int16_t *src1,
        const int16_t *diff_mv_x, const int16_t *diff_mv_y, int denom, int w0, int w1, int o0, int o1);

    void (*apply_bdof)(uint8_t *dst, ptrdiff_t dst_stride, int16_t *src0, int16_t *src1, int block_w, int block_h);

    int (*sad)(const int16_t *src0, const int16_t *src1, int dx, int dy, int block_w, int block_h);
    void (*dmvr[2][2])(int16_t *dst, const uint8_t *src, ptrdiff_t src_stride, int height,
        intptr_t mx, intptr_t my, int width);
} VVCInterDSPContext;

struct VVCLocalContext;

typedef struct VVCIntraDSPContext {
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
} VVCIntraDSPContext;

typedef struct VVCItxDSPContext {
    void (*add_residual)(uint8_t *dst, const int *res, int width, int height, ptrdiff_t stride);
    void (*add_residual_joint)(uint8_t *dst, const int *res, int width, int height, ptrdiff_t stride, int c_sign, int shift);
    void (*pred_residual_joint)(int *buf, int width, int height, int c_sign, int shift);
    void (*itx[N_TX_TYPE][N_TX_SIZE])(int *out, ptrdiff_t out_step, const int *in, ptrdiff_t in_step);
    void (*transform_bdpcm)(int *coeffs, int width, int height, int vertical, int depth);
} VVCItxDSPContext;

typedef struct VVCLMCSDSPContext {
    void (*filter)(uint8_t *dst, ptrdiff_t dst_stride, int width, int height, const uint8_t *lut);
} VVCLMCSDSPContext;

typedef struct VVCLFDSPContext {
    int (*ladf_level[2 /* h, v */])(const uint8_t *pix, ptrdiff_t stride);
    void (*filter_luma[2 /* h, v */])(uint8_t *pix, ptrdiff_t stride, int beta, int32_t tc,
        uint8_t no_p, uint8_t no_q, uint8_t max_len_p, uint8_t max_len_q, int hor_ctu_edge);
    void (*filter_chroma[2 /* h, v */])(uint8_t *pix, ptrdiff_t stride, int beta, int32_t tc,
        uint8_t no_p, uint8_t no_q, int shift, int max_len_p, int max_len_q);
} VVCLFDSPContext;

typedef struct VVCSAODSPContext {
    void (*band_filter[9])(uint8_t *_dst, uint8_t *_src, ptrdiff_t _dst_stride, ptrdiff_t _src_stride,
        int16_t *sao_offset_val, int sao_left_class, int width, int height);

    /* implicit src_stride parameter has value of 2 * MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE */
    void (*edge_filter[9])(uint8_t *_dst /* align 16 */, uint8_t *_src /* align 32 */, ptrdiff_t dst_stride,
        int16_t *sao_offset_val, int sao_eo_class, int width, int height);

    void (*edge_restore[2])(uint8_t *_dst, uint8_t *_src, ptrdiff_t _dst_stride, ptrdiff_t _src_stride,
        struct SAOParams *sao, int *borders, int _width, int _height, int c_idx,
        uint8_t *vert_edge, uint8_t *horiz_edge, uint8_t *diag_edge);
} VVCSAODSPContext;

typedef struct VVCALFDSPContext {
    void (*filter[2 /* luma, chroma */])(uint8_t *dst, const uint8_t *src, ptrdiff_t dst_stride, ptrdiff_t src_stride,
        int width, int height, const int8_t *filter, const int16_t *clip);
    void (*filter_vb[2 /* luma, chroma */])(uint8_t *dst, const uint8_t *src, ptrdiff_t dst_stride, ptrdiff_t src_stride,
        int width, int height, const int8_t *filter, const int16_t *clip, int vb_pos);

    void (*filter_cc)(uint8_t *dst, const uint8_t *luma, ptrdiff_t dst_stride, ptrdiff_t luma_stride,
        int width, int height, int hs, int vs, const int8_t *filter, int vb_pos);

    void (*get_coeff_and_clip)(const uint8_t *src, ptrdiff_t src_stride,
        int x0, int y0, int width, int height, int vb_pos,
        const int8_t *coeff_set, const uint8_t *clip_idx_set, const uint8_t *class_to_filt,
        int8_t *coeff, int16_t *clip);
} VVCALFDSPContext;

typedef struct VVCDSPContext {
    VVCInterDSPContext inter;
    VVCIntraDSPContext intra;
    VVCItxDSPContext itx;
    VVCLMCSDSPContext lmcs;
    VVCLFDSPContext lf;
    VVCSAODSPContext sao;
    VVCALFDSPContext alf;
} VVCDSPContext;

void ff_vvc_dsp_init(VVCDSPContext *hpc, int bit_depth);

extern const int8_t ff_vvc_chroma_filters[3][32][4];
extern const int8_t ff_vvc_luma_filters[3][16][8];
extern const int8_t ff_vvc_dmvr_filters[16][2];

void ff_vvc_dsp_init_aarch64(VVCDSPContext *c, const int bit_depth);
void ff_vvc_dsp_init_arm(VVCDSPContext *c, const int bit_depth);
void ff_vvc_dsp_init_ppc(VVCDSPContext *c, const int bit_depth);
void ff_vvc_dsp_init_x86(VVCDSPContext *c, const int bit_depth);
void ff_vvc_dsp_init_mips(VVCDSPContext *c, const int bit_depth);

#endif /* AVCODEC_VVCDSP_H */
