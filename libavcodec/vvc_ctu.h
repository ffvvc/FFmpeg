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

#ifndef AVCODEC_VVC_CTU_H
#define AVCODEC_VVC_CTU_H

#include "vvcdec.h"

typedef struct NeighbourAvailable {
    int cand_left;
    int cand_up;
    int cand_up_left;
    int cand_up_right;
    int cand_up_right_sap;
} NeighbourAvailable;

enum IspType{
    ISP_NO_SPLIT,
    ISP_HOR_SPLIT,
    ISP_VER_SPLIT,
};

typedef enum VVCSplitMode {
    SPLIT_NONE,
    SPLIT_TT_HOR,
    SPLIT_BT_HOR,
    SPLIT_TT_VER,
    SPLIT_BT_VER,
    SPLIT_QT,
} VVCSplitMode;

typedef enum MtsIdx {
    MTS_DCT2_DCT2,
    MTS_DST7_DST7,
    MTS_DST7_DCT8,
    MTS_DCT8_DST7,
    MTS_DCT8_DCT8,
} MtsIdx;

typedef struct TransformBlock {
    uint8_t has_coeffs;
    uint8_t c_idx;
    uint8_t ts;             ///<  transform_skip_flag
    int x0;
    int y0;

    int tb_width;
    int tb_height;
    int log2_tb_width;
    int log2_tb_height;

    int max_scan_x;
    int max_scan_y;
    int min_scan_x;
    int min_scan_y;

    int qp;
    int rect_non_ts_flag;
    int bd_shift;
    int bd_offset;

    int *coeffs;
} TransformBlock;

typedef enum VVCTreeType {
    SINGLE_TREE,
    DUAL_TREE_LUMA,
    DUAL_TREE_CHROMA,
} VVCTreeType;

typedef struct TransformUnit {
    int x0;
    int y0;
    int width;
    int height;

    uint8_t joint_cbcr_residual_flag;                   ///< tu_joint_cbcr_residual_flag

    uint8_t coded_flag[VVC_MAX_SAMPLE_ARRAYS];          ///< tu_y_coded_flag, tu_cb_coded_flag, tu_cr_coded_flag
    uint8_t nb_tbs;
    TransformBlock tbs[VVC_MAX_SAMPLE_ARRAYS];
} TransformUnit;

typedef enum PredMode {
    MODE_INTER,
    MODE_INTRA,
    MODE_SKIP,
    MODE_PLT,
    MODE_IBC,
} PredMode;

typedef struct Mv {
    int x;  ///< horizontal component of motion vector
    int y;  ///< vertical component of motion vector
} Mv;

typedef struct MvField {
    DECLARE_ALIGNED(4, Mv, mv)[2];  ///< mvL0, vvL1
    int8_t  ref_idx[2];             ///< refIdxL0, refIdxL1
    uint8_t hpel_if_idx;            ///< hpelIfIdx
    uint8_t bcw_idx;                ///< bcwIdx
    uint8_t pred_flag;
    uint8_t ciip_flag;              ///< ciip_flag
} MvField;

typedef struct DMVRInfo {
    DECLARE_ALIGNED(4, Mv, mv)[2];  ///< mvL0, vvL1
    uint8_t dmvr_enabled;
} DMVRInfo;

typedef enum MotionModelIdc {
    MOTION_TRANSLATION,
    MOTION_4_PARAMS_AFFINE,
    MOTION_6_PARAMS_AFFINE,
} MotionModelIdc;

typedef enum PredFlag {
    PF_INTRA = 0x0,
    PF_L0    = 0x1,
    PF_L1    = 0x2,
    PF_BI    = 0x3,
} PredFlag;

typedef enum IntraPredMode {
    INTRA_PLANAR    = 0,
    INTRA_DC,
    INTRA_HORZ      = 18,
    INTRA_DIAG      = 34,
    INTRA_VERT      = 50,
    INTRA_VDIAG     = 66,
    INTRA_LT_CCLM   = 81,
    INTRA_L_CCLM,
    INTRA_T_CCLM
} IntraPredMode;

typedef struct MotionInfo {
    MotionModelIdc motion_model_idc; ///< MotionModelIdc
    int8_t   ref_idx[2];             ///< refIdxL0, refIdxL1
    uint8_t  hpel_if_idx;            ///< hpelIfIdx
    uint8_t  bcw_idx;                ///< bcwIdx
    PredFlag pred_flag;

    Mv mv[2][MAX_CONTROL_POINTS];

    int num_sb_x, num_sb_y;
} MotionInfo;

typedef struct PredictionUnit {
    uint8_t general_merge_flag;
    uint8_t mmvd_merge_flag;
    //InterPredIdc inter_pred_idc;
    uint8_t inter_affine_flag;

    //subblock predict
    uint8_t merge_subblock_flag;

    uint8_t merge_gpm_flag;
    uint8_t gpm_partition_idx;
    MvField gpm_mv[2];

    int sym_mvd_flag;

    MotionInfo mi;

    int16_t diff_mv_x[2][AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];   ///< diffMvLX
    int16_t diff_mv_y[2][AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];   ///< diffMvLX
    int cb_prof_flag[2];
} PredictionUnit;

struct CodingUnit;
typedef struct CodingUnit {
    AVBufferRef *buf;

    VVCTreeType tree_type;
    int x0;
    int y0;
    int cb_width;
    int cb_height;
    int ch_type;
    int cqt_depth;

    uint8_t coded_flag;

    uint8_t sbt_flag;
    uint8_t sbt_horizontal_flag;
    uint8_t sbt_pos_flag;

    int lfnst_idx;
    MtsIdx mts_idx;

    uint8_t act_enabled_flag;

    uint8_t intra_luma_ref_idx;             ///< IntraLumaRefLineIdx[][]
    uint8_t intra_mip_flag;                 ///< intra_mip_flag
    uint8_t skip_flag;                      ///< cu_skip_flag;

    // Inferred parameters
    enum IspType isp_split_type;            ///< IntraSubPartitionsSplitType

    enum PredMode pred_mode;                ///< PredMode

    //inter
    uint8_t ciip_flag;

    // Inferred parameters
    int num_intra_subpartitions;

    IntraPredMode intra_pred_mode_y;        ///< IntraPredModeY
    IntraPredMode intra_pred_mode_c;        ///< IntraPredModeC
    int mip_chroma_direct_flag;             ///< MipChromaDirectFlag

    int bdpcm_flag[VVC_MAX_SAMPLE_ARRAYS];  ///< BdpcmFlag

    int apply_lfnst_flag[VVC_MAX_SAMPLE_ARRAYS];    ///< ApplyLfnstFlag[]

    TransformUnit tus[MAX_TUS_IN_CU];
    int num_tus;

    int8_t qp[4];                         ///< QpY, Qp′Cb, Qp′Cr, Qp′CbCr

    PredictionUnit pu;

    struct CodingUnit *next;
} CodingUnit;

struct CTU {
    CodingUnit *cus;
};

typedef struct ReconstructedArea {
    int x;
    int y;
    int w;
    int h;
} ReconstructedArea;

// VVC_CONTEXTS matched with SYNTAX_ELEMENT_LAST, it's checked by cabac_init_state.
#define VVC_CONTEXTS 378
typedef struct EntryPoint {
    int8_t qp_y;                                    //< QpY

    VVCCabacState cabac_state[VVC_CONTEXTS];
    CABACContext cc;

    VVCTask *parse_task;
    int ctu_addr_last;

    uint8_t is_first_qg;
    MvField hmvp[MAX_NUM_HMVP_CANDS];               ///< HmvpCandList
    int     num_hmvp;                               ///< NumHmvpCand
} EntryPoint;

struct VVCLocalContext {
    uint8_t ctb_left_flag;
    uint8_t ctb_up_flag;
    uint8_t ctb_up_right_flag;
    uint8_t ctb_up_left_flag;
    int     end_of_tiles_x;
    int     end_of_tiles_y;

    /* +7 is for subpixel interpolation, *2 for high bit depths */
    DECLARE_ALIGNED(32, uint8_t, edge_emu_buffer)[(MAX_PB_SIZE + 7) * EDGE_EMU_BUFFER_STRIDE * 2];
    /* The extended size between the new edge emu buffer is abused by SAO */
    DECLARE_ALIGNED(32, uint8_t, edge_emu_buffer2)[(MAX_PB_SIZE + 7) * EDGE_EMU_BUFFER_STRIDE * 2];
    DECLARE_ALIGNED(32, int16_t, tmp)[MAX_PB_SIZE * MAX_PB_SIZE];
    DECLARE_ALIGNED(32, int16_t, tmp1)[MAX_PB_SIZE * MAX_PB_SIZE];
    DECLARE_ALIGNED(32, uint8_t, ciip_tmp1)[MAX_PB_SIZE * MAX_PB_SIZE * 2];
    DECLARE_ALIGNED(32, uint8_t, ciip_tmp2)[MAX_PB_SIZE * MAX_PB_SIZE * 2];
    DECLARE_ALIGNED(32, uint8_t, sao_buffer)[(MAX_CTU_SIZE + 2 * SAO_PADDING_SIZE) * EDGE_EMU_BUFFER_STRIDE * 2];
    DECLARE_ALIGNED(32, uint8_t, alf_buffer_luma)[(MAX_CTU_SIZE + 2 * ALF_PADDING_SIZE) * EDGE_EMU_BUFFER_STRIDE * 2];
    DECLARE_ALIGNED(32, uint8_t, alf_buffer_chroma)[(MAX_CTU_SIZE + 2 * ALF_PADDING_SIZE) * EDGE_EMU_BUFFER_STRIDE * 2];

    struct {
        int sbt_num_fourths_tb0;                ///< SbtNumFourthsTb0

        uint8_t is_cu_qp_delta_coded;           ///< IsCuQpDeltaCoded
        int cu_qg_top_left_x;                   ///< CuQgTopLeftX
        int cu_qg_top_left_y;                   ///< CuQgTopLeftY
        int is_cu_chroma_qp_offset_coded;       ///< IsCuChromaQpOffsetCoded
        int chroma_qp_offset[3];                ///< CuQpOffsetCb, CuQpOffsetCr, CuQpOffsetCbCr

        int infer_tu_cbf_luma;                  ///< InferTuCbfLuma
        int prev_tu_cbf_y;                      ///< prevTuCbfY;

        int lfnst_dc_only;                      ///< LfnstDcOnly
        int lfnst_zero_out_sig_coeff_flag;      ///< LfnstZeroOutSigCoeffFlag

        int mts_dc_only;                        ///< MtsDcOnly
        int mts_zero_out_sig_coeff_flag;        ///< MtsZeroOutSigCoeffFlag;
    } parse;

    struct {
        // lmcs cache, for recon only
        int chroma_scale;
        int x_vpdu;
        int y_vpdu;
    } lmcs;

    CodingUnit *cu;
    ReconstructedArea ras[2][MAX_PARTS_IN_CTU];
    int  num_ras[2];

    NeighbourAvailable na;

#define BOUNDARY_LEFT_SLICE     (1 << 0)
#define BOUNDARY_LEFT_TILE      (1 << 1)
#define BOUNDARY_UPPER_SLICE    (1 << 2)
#define BOUNDARY_UPPER_TILE     (1 << 3)
    /* properties of the boundary of the current CTB for the purposes
     * of the deblocking filter */
    int boundary_flags;

    SliceContext *sc;
    VVCFrameContext *fc;
    EntryPoint *ep;
    int *coeffs;
} ;

typedef struct VVCAllowedSplit {
    int qt;
    int btv;
    int bth;
    int ttv;
    int tth;
} VVCAllowedSplit;

void ff_vvc_decode_neighbour(VVCLocalContext *lc, int x_ctb, int y_ctb, int rx, int ry, int rs);
int ff_vvc_coding_tree_unit(VVCLocalContext *lc, int ctb_addr, int rs, int rx, int ry);
int ff_vvc_reconstruct(VVCLocalContext *lc, const int rs, const int rx, const int ry);

int ff_vvc_inter_data(VVCLocalContext *lc);
int ff_vvc_predict_inter(VVCLocalContext *lc, int rs);
void ff_vvc_predict_ciip(VVCLocalContext *lc);
void ff_vvc_set_cb_tab(const VVCLocalContext *lc, uint8_t *tab, uint8_t v);
int ff_vvc_wide_angle_mode_mapping(const CodingUnit *cu,
    int tb_width, int tb_height, int c_idx, int pred_mode_intra);

#endif // AVCODEC_VVC_CTU_H
