/*
 * VVC video decoder
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

#ifndef AVCODEC_VVCDEC_H
#define AVCODEC_VVCDEC_H

#include <stdatomic.h>

#include "libavutil/buffer.h"
#include "libavutil/md5.h"
#include "libavutil/mem_internal.h"
#include "libavutil/thread.h"

#include "avcodec.h"
#include "bswapdsp.h"
#include "cabac.h"
#include "cbs_h266.h"
#include "get_bits.h"
#include "vvcpred.h"
#include "h2645_parse.h"
#include "vvc.h"
#include "executor.h"
#include "vvc_ps.h"
#include "vvcdsp.h"
#include "internal.h"
#include "threadframe.h"
#include "videodsp.h"

#define LUMA    0
#define CHROMA  1
#define CB      1
#define CR      2
#define JCBCR   3

#define MAX_CTU_SIZE        128
#define MAX_CU_SIZE         MAX_CTU_SIZE
#define MIN_CU_SIZE         4
#define MIN_CU_LOG2         2
#define MIN_PU_SIZE         4
#define MIN_PU_LOG2         2
#define MIN_TB_LOG2         2                       ///< MinTbLog2SizeY
#define MIN_TB_SIZE         (1 << MIN_TB_LOG2)
#define MIN_TB_CHROMA_LOG2  1
#define MAX_CU_DEPTH        7
#define MAX_PARTS_IN_CTU ((MAX_CTU_SIZE >> MIN_CU_LOG2) * (MAX_CTU_SIZE >> MIN_CU_LOG2))

#define MAX_INTER_SBS   ((MAX_CU_SIZE >> MIN_PU_LOG2) * (MAX_CU_SIZE >> MIN_PU_LOG2))

#define MAX_CONTROL_POINTS   3


#define MAX_TB_SIZE 64
#define MAX_TUS_IN_CU 64

#define MAX_QP 63
#define DEFAULT_INTRA_TC_OFFSET 2

#define MRG_MAX_NUM_CANDS       6
#define MAX_NUM_HMVP_CANDS      5

#define L0 0
#define L1 1

#define CHROMA_EXTRA_BEFORE 1
#define CHROMA_EXTRA_AFTER  2
#define CHROMA_EXTRA        3
#define LUMA_EXTRA_BEFORE 3
#define LUMA_EXTRA_AFTER  4
#define LUMA_EXTRA        7
#define BILINEAR_EXTRA_BEFORE 0
#define BILINEAR_EXTRA_AFTER  1
#define BILINEAR_EXTRA        1

#define SAO_PADDING_SIZE      1

#define ALF_PADDING_SIZE      8
#define ALF_BLOCK_SIZE        4

//local size to save stack memroy
#define ALF_SUBBLOCK_SIZE    32

#define ALF_BORDER_LUMA       3
#define ALF_BORDER_CHROMA     2

#define ALF_VB_POS_ABOVE_LUMA       4
#define ALF_VB_POS_ABOVE_CHROMA     2

#define EDGE_EMU_BUFFER_STRIDE (MAX_PB_SIZE + 32)

#define AFFINE_MIN_BLOCK_SIZE 4
#define PROF_BORDER_EXT 1
#define PROF_BLOCK_SIZE (AFFINE_MIN_BLOCK_SIZE + PROF_BORDER_EXT * 2)
#define BDOF_BORDER_EXT 1

#define BDOF_PADDED_SIZE (16 + BDOF_BORDER_EXT * 2)
#define BDOF_BLOCK_SIZE 4
#define BDOF_GRADIENT_SIZE (BDOF_BLOCK_SIZE + BDOF_BORDER_EXT * 2)

/**
 * Value of the luma sample at position (x, y) in the 2D array tab.
 */
#define SAMPLE(tab, x, y) ((tab)[(y) * s->sps->width + (x)])
#define SAMPLE_CTB(tab, x, y) ((tab)[(y) * min_cb_width + (x)])
#define CTB(tab, x, y) ((tab)[(y) * fc->ps.pps->ctb_width + (x)])

#define IS_VCL(t) ((t) <= VVC_RSV_IRAP_11 && (t) >= VVC_TRAIL_NUT)

#define IS_IDR(s)  ((s)->vcl_unit_type == VVC_IDR_W_RADL || (s)->vcl_unit_type == VVC_IDR_N_LP)
#define IS_CRA(s)  ((s)->vcl_unit_type == VVC_CRA_NUT)
#define IS_IRAP(s) (IS_IDR(s) || IS_CRA(s))
#define IS_GDR(s)  ((s)->vcl_unit_type == VVC_GDR_NUT)
#define IS_CVSS(s) (IS_IRAP(s)|| IS_GDR(s))
#define IS_CLVSS(s) (IS_CVSS(s) && s->no_output_before_recovery_flag)
#define IS_RASL(s) ((s)->vcl_unit_type == VVC_RASL_NUT)
#define IS_RADL(s) ((s)->vcl_unit_type == VVC_RADL_NUT)

#define IS_I(sh) ((sh)->slice_type == VVC_SLICE_TYPE_I)
#define IS_P(sh) ((sh)->slice_type == VVC_SLICE_TYPE_P)
#define IS_B(sh) ((sh)->slice_type == VVC_SLICE_TYPE_B)

#define INV_POC INT_MIN
#define GDR_IS_RECOVERED(s)  (s->gdr_recovery_point_poc == INV_POC)
#define GDR_SET_RECOVERED(s) (s->gdr_recovery_point_poc =  INV_POC)

typedef struct VVCLocalContext VVCLocalContext;
typedef struct SliceContext SliceContext;
typedef struct VVCFrameContext  VVCFrameContext;
typedef struct VVCFrameThread VVCFrameThread;
typedef struct EntryPoint EntryPoint;
typedef struct VVCTask VVCTask;
typedef struct Mv Mv;
typedef struct MvField MvField;
typedef struct DMVRInfo DMVRInfo;
typedef struct CTU CTU;

enum SAOType {
    SAO_NOT_APPLIED = 0,
    SAO_BAND,
    SAO_EDGE,
    SAO_APPLIED
};

enum SAOEOClass {
    SAO_EO_HORIZ = 0,
    SAO_EO_VERT,
    SAO_EO_135D,
    SAO_EO_45D,
};

typedef struct RefPicList {
    struct VVCFrame *ref[VVC_MAX_REF_ENTRIES];
    int list[VVC_MAX_REF_ENTRIES];
    int isLongTerm[VVC_MAX_REF_ENTRIES];
    int nb_refs;
} RefPicList;

typedef struct RefPicListTab {
    RefPicList refPicList[2];
} RefPicListTab;

#define VVC_FRAME_FLAG_OUTPUT    (1 << 0)
#define VVC_FRAME_FLAG_SHORT_REF (1 << 1)
#define VVC_FRAME_FLAG_LONG_REF  (1 << 2)
#define VVC_FRAME_FLAG_BUMPING   (1 << 3)

typedef struct FrameProgress {
    atomic_int progress;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} FrameProgress;

typedef struct VVCFrame {
    AVFrame *frame;
    ThreadFrame tf;

    MvField  *tab_mvf;
    RefPicList *refPicList;
    RefPicListTab **rpl_tab;

    int ctb_count;

    int poc;

    struct VVCFrame *collocated_ref;

    AVBufferRef *tab_mvf_buf;
    AVBufferRef *rpl_tab_buf;
    AVBufferRef *rpl_buf;
    AVBufferRef *progress_buf;

#if 0
    AVBufferRef *hwaccel_priv_buf;
    void *hwaccel_picture_private;
#endif
    /**
     * A sequence counter, so that old frames are output first
     * after a POC reset
     */
    uint16_t sequence;
    /**
     * A combination of VVC_FRAME_FLAG_*
     */
    uint8_t flags;
} VVCFrame;

typedef struct VVCCabacState{
    uint16_t state[2];
    uint8_t  shift[2];
} VVCCabacState;

struct SliceContext {
    int slice_idx;

    VVCSH sh;

    EntryPoint *eps;
    int nb_eps;
};

struct VVCFrameContext {
    AVCodecContext *avctx;

    // +1 for the current frame
    VVCFrame DPB[VVC_MAX_DPB_SIZE + 1];

    AVFrame *frame;
    AVFrame *output_frame;
    VVCFrameParamSets ps;

    SliceContext  **slices;
    int nb_slices;
    int nb_slices_allocated;

    VVCFrame *ref;

    VVCDSPContext vvcdsp;
    VideoDSPContext vdsp;

    VVCFrameThread *frame_thread;

    uint64_t decode_order;

    AVPacket *avpkt;
    H2645Packet pkt;

    AVBufferPool *tab_mvf_pool;
    AVBufferPool *rpl_tab_pool;

    AVBufferPool *cu_pool;

    struct {
        int16_t *slice_idx;

        DBParams  *deblock;
        SAOParams *sao;
        ALFParams *alf;
        DMVRInfo  *dmvr;

        int     *cb_pos_x[2];                           ///< CbPosX[][][]
        int     *cb_pos_y[2];                           ///< CbPosY[][][]
        uint8_t *cb_width[2];                           ///< CbWidth[][][]
        uint8_t *cb_height[2];                          ///< CbHeight[][][]
        uint8_t *cqt_depth[2];                          ///< CqtDepth[][][]
        int8_t  *qp[VVC_MAX_SAMPLE_ARRAYS];

        uint8_t *skip;                                  ///< CuSkipFlag[][]
        uint8_t *ispmf;                                 ///< intra_sub_partitions_mode_flag
        uint8_t *msm[2];                                ///< MttSplitMode[][][] in 32 pixels
        uint8_t *imf;                                   ///< IntraMipFlag[][]
        uint8_t *imtf;                                  ///< intra_mip_transposed_flag[][]
        uint8_t *imm;                                   ///< intra_mip_mode[][]
        uint8_t *ipm;                                   ///< IntraPredModeY[][]
        uint8_t *cpm[2];                                ///< CuPredMode[][][]
        uint8_t *msf;                                   ///< MergeSubblockFlag[][]
        uint8_t *iaf;                                   ///< InterAffineFlag[][]
        uint8_t *mmi;                                   ///< MotionModelIdc[][]
        Mv      *cp_mv[2];                              ///< CpMvLX[][][][MAX_CONTROL_POINTS];

        uint8_t *tu_coded_flag[VVC_MAX_SAMPLE_ARRAYS];  ///< tu_y_coded_flag[][],  tu_cb_coded_flag[][],  tu_cr_coded_flag[][]
        uint8_t *tu_joint_cbcr_residual_flag;           ///< tu_joint_cbcr_residual_flag[][]
        int     *tb_pos_x0[2];
        int     *tb_pos_y0[2];
        uint8_t *tb_width[2];
        uint8_t *tb_height[2];
        uint8_t *pcmf[2];

        uint8_t *horizontal_bs[VVC_MAX_SAMPLE_ARRAYS];
        uint8_t *vertical_bs[VVC_MAX_SAMPLE_ARRAYS];
        uint8_t *horizontal_p;                          ///< horizontal maxFilterLengthPs for luma
        uint8_t *horizontal_q;                          ///< horizontal maxFilterLengthPs for luma
        uint8_t *vertical_p;                            ///< vertical   maxFilterLengthQs for luma
        uint8_t *vertical_q;                            ///< vertical   maxFilterLengthQs for luma

        uint8_t *sao_pixel_buffer_h[VVC_MAX_SAMPLE_ARRAYS];
        uint8_t *sao_pixel_buffer_v[VVC_MAX_SAMPLE_ARRAYS];
        uint8_t *alf_pixel_buffer_h[VVC_MAX_SAMPLE_ARRAYS][2];
        uint8_t *alf_pixel_buffer_v[VVC_MAX_SAMPLE_ARRAYS][2];

        int     *coeffs;
        CTU     *ctus;

        //used in arrays_init only
        int ctu_count;
        int ctu_size;
        int pic_size_in_min_cb;
        int pic_size_in_min_pu;
        int pic_size_in_min_tu;
        int ctu_width;
        int ctu_height;
        int width;
        int height;
        int chroma_format_idc;
        int pixel_shift;
        int bs_width;
        int bs_height;
    } tab;
} ;

typedef struct VVCContext {
    const AVClass *c;  // needed by private avoptions
    AVCodecContext *avctx;

    VVCParamSets ps;

    int temporal_id;  ///< temporal_id_plus1 - 1
    //int poc;
    int pocTid0;

    int eos;       ///< current packet contains an EOS/EOB NAL
    int last_eos;  ///< last packet contains an EOS/EOB NAL


    enum VVCNALUnitType vcl_unit_type;
    int no_output_before_recovery_flag; ///< NoOutputBeforeRecoveryFlag
    int gdr_recovery_point_poc;         ///< recoveryPointPocVal

    /**
     * Sequence counters for decoded and output frames, so that old
     * frames are output first after a POC reset
     */
    uint16_t seq_decode;
    uint16_t seq_output;

    int is_nalff;           ///< this flag is != 0 if bitstream is encapsulated
                            ///< as a format defined in 14496-15

    int apply_defdispwin;
    int nal_length_size;    ///< Number of bytes used for nal length (1, 2 or 4)

    Executor *executor;

    VVCFrameContext *fcs;
    int nb_fcs;

    // processed frames
    uint64_t nb_frames;
    // delayed frames
    int nb_delayed;
}  VVCContext ;

//dpb
int ff_vvc_output_frame(VVCContext *s, VVCFrameContext *fc, AVFrame *out, int no_output_of_prior_pics_flag, int flush);
void ff_vvc_bump_frame(VVCContext *s, VVCFrameContext *fc);
int ff_vvc_set_new_ref(VVCContext *s, VVCFrameContext *fc, AVFrame **frame);
const RefPicList *ff_vvc_get_ref_list(const VVCFrameContext *fc, const VVCFrame *ref, int x0, int y0);
int ff_vvc_frame_rpl(VVCContext *s, VVCFrameContext *fc, const SliceContext *sc);
int ff_vvc_slice_rpl(VVCContext *s, VVCFrameContext *fc, const SliceContext *sc);
void ff_vvc_unref_frame(VVCFrameContext *fc, VVCFrame *frame, int flags);
void ff_vvc_clear_refs(VVCFrameContext *fc);

int ff_vvc_get_qPy(const VVCFrameContext *fc, int xc, int yc);

int ff_vvc_get_top_available(const VVCLocalContext *lc, int x0, int y0, int target_size, int c_idx);
int ff_vvc_get_left_available(const VVCLocalContext *lc, int x0, int y0, int target_size, int c_idx);

void ff_vvc_lmcs_filter(const VVCLocalContext *lc, const int x0, const int y0);
void ff_vvc_deblock_vertical(const VVCLocalContext *lc, int x0, int y0);
void ff_vvc_deblock_horizontal(const VVCLocalContext *lc, int x0, int y0);
void ff_vvc_sao_filter(VVCLocalContext *lc, const int x0, const int y0);
void ff_vvc_alf_copy_ctu_to_hv(VVCLocalContext* lc, int x0, int y0);
void ff_vvc_alf_filter(VVCLocalContext *lc, const int x0, const int y0);

void ff_vvc_ctu_free_cus(CTU *ctu);

#endif /* AVCODEC_VVCDEC_H */
