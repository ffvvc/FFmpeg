/*
 * VVC parameter set parser
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

#ifndef AVCODEC_VVC_PS_H
#define AVCODEC_VVC_PS_H

#include <stdint.h>

#include "libavutil/buffer.h"
#include "libavutil/pixfmt.h"
#include "libavutil/rational.h"

#include "avcodec.h"
#include "get_bits.h"
#include "vvc.h"

#define LMCS_MAX_BIT_DEPTH  12
#define LMCS_MAX_LUT_SIZE   (1 << LMCS_MAX_BIT_DEPTH)
#define LMCS_MAX_BIN_SIZE 16
#define LADF_MAX_INTERVAL   5

enum {
    CHROMA_FORMAT_MONO,
    CHROMA_FORMAT_420,
    CHROMA_FORMAT_422,
    CHROMA_FORMAT_444,
};

typedef struct VUI {
    uint16_t payload_size;

    uint8_t  progressive_source_flag;
    uint8_t  interlaced_source_flag;
    uint8_t  non_packed_constraint_flag;
    uint8_t  non_projected_constraint_flag;

    uint8_t  aspect_ratio_info_present_flag;
    uint8_t  aspect_ratio_constant_flag;
    uint8_t  aspect_ratio_idc;

    uint16_t sar_width;
    uint16_t sar_height;

    uint8_t  overscan_info_present_flag;
    uint8_t  overscan_appropriate_flag;

    uint8_t  colour_description_present_flag;
    uint8_t  colour_primaries;

    uint8_t  transfer_characteristics;
    uint8_t  matrix_coeffs;
    uint8_t  full_range_flag;

    uint8_t  chroma_loc_info_present_flag;
    uint8_t  chroma_sample_loc_type_frame;
    uint8_t  chroma_sample_loc_type_top_field;
    uint8_t  chroma_sample_loc_type_bottom_field;
} VUI;

typedef struct GeneralConstraintsInfo {
    uint8_t present_flag;
    /* general */
    uint8_t intra_only_constraint_flag;
    uint8_t all_layers_independent_constraint_flag;
    uint8_t one_au_only_constraint_flag;

    /* picture format */
    uint8_t sixteen_minus_max_bitdepth_constraint_idc;
    uint8_t three_minus_max_chroma_format_constraint_idc;

    /* NAL unit type related */
    uint8_t no_mixed_nalu_types_in_pic_constraint_flag;
    uint8_t no_trail_constraint_flag;
    uint8_t no_stsa_constraint_flag;
    uint8_t no_rasl_constraint_flag;
    uint8_t no_radl_constraint_flag;
    uint8_t no_idr_constraint_flag;
    uint8_t no_cra_constraint_flag;
    uint8_t no_gdr_constraint_flag;
    uint8_t no_aps_constraint_flag;
    uint8_t no_idr_rpl_constraint_flag;

    /* tile, slice, subpicture partitioning */
    uint8_t one_tile_per_pic_constraint_flag;
    uint8_t pic_header_in_slice_header_constraint_flag;
    uint8_t one_slice_per_pic_constraint_flag;
    uint8_t no_rectangular_slice_constraint_flag;
    uint8_t one_slice_per_subpic_constraint_flag;
    uint8_t no_subpic_info_constraint_flag;

    /* CTU and block partitioning */
    uint8_t three_minus_max_log2_ctu_size_constraint_idc;
    uint8_t no_partition_constraints_override_constraint_flag;
    uint8_t no_mtt_constraint_flag;
    uint8_t no_qtbtt_dual_tree_intra_constraint_flag;

    /* intra */
    uint8_t no_palette_constraint_flag;
    uint8_t no_ibc_constraint_flag;
    uint8_t no_isp_constraint_flag;
    uint8_t no_mrl_constraint_flag;
    uint8_t no_mip_constraint_flag;
    uint8_t no_cclm_constraint_flag;

    /* inter */
    uint8_t no_ref_pic_resampling_constraint_flag;
    uint8_t no_res_change_in_clvs_constraint_flag;
    uint8_t no_weighted_prediction_constraint_flag;
    uint8_t no_ref_wraparound_constraint_flag;
    uint8_t no_temporal_mvp_constraint_flag;
    uint8_t no_sbtmvp_constraint_flag;
    uint8_t no_amvr_constraint_flag;
    uint8_t no_bdof_constraint_flag;
    uint8_t no_smvd_constraint_flag;
    uint8_t no_dmvr_constraint_flag;
    uint8_t no_mmvd_constraint_flag;
    uint8_t no_affine_motion_constraint_flag;
    uint8_t no_prof_constraint_flag;
    uint8_t no_bcw_constraint_flag;
    uint8_t no_ciip_constraint_flag;
    uint8_t no_gpm_constraint_flag;

    /* transform, quantization, residual */
    uint8_t no_luma_transform_size_64_constraint_flag;
    uint8_t no_transform_skip_constraint_flag;
    uint8_t no_bdpcm_constraint_flag;
    uint8_t no_mts_constraint_flag;
    uint8_t no_lfnst_constraint_flag;
    uint8_t no_joint_cbcr_constraint_flag;
    uint8_t no_sbt_constraint_flag;
    uint8_t no_act_constraint_flag;
    uint8_t no_explicit_scaling_list_constraint_flag;
    uint8_t no_dep_quant_constraint_flag;
    uint8_t no_sign_data_hiding_constraint_flag;
    uint8_t no_cu_qp_delta_constraint_flag;
    uint8_t no_chroma_qp_offset_constraint_flag;

    /* loop filter */
    uint8_t no_sao_constraint_flag;
    uint8_t no_alf_constraint_flag;
    uint8_t no_ccalf_constraint_flag;
    uint8_t no_lmcs_constraint_flag;
    uint8_t no_ladf_constraint_flag;
    uint8_t no_virtual_boundaries_constraint_flag;
} GeneralConstraintsInfo;

typedef struct PTL {
    uint8_t  general_profile_idc;
    uint8_t  general_tier_flag;
    uint8_t  general_level_idc;
    uint8_t  frame_only_constraint_flag;
    uint8_t  multilayer_enabled_flag;
    GeneralConstraintsInfo gci;
    uint8_t  sublayer_level_present_flag[VVC_MAX_SUBLAYERS - 1];
    uint8_t  sublayer_level_idc[VVC_MAX_SUBLAYERS - 1];
    uint8_t  num_sub_profiles;
    uint32_t general_sub_profile_idc[VVC_MAX_SUB_PROFILES];
} PTL;

typedef struct DpbParameters {
    uint8_t max_dec_pic_buffering[VVC_MAX_SUBLAYERS];
    uint8_t max_num_reorder_pics[VVC_MAX_SUBLAYERS];
    uint8_t max_latency_increase[VVC_MAX_SUBLAYERS];
} DpbParameters;

typedef struct VVCVPS {
    uint8_t  video_parameter_set_id;
    uint8_t  max_layers;
    uint8_t  max_sublayers_minus1;
    uint8_t  default_ptl_dpb_hrd_max_tid_flag;
    uint8_t  all_independent_layers_flag;
    uint8_t  layer_id[VVC_MAX_LAYERS];
    uint8_t  independent_layer_flag[VVC_MAX_LAYERS];
    uint8_t  max_tid_ref_present_flag[VVC_MAX_LAYERS];
    uint8_t  direct_ref_layer_flag[VVC_MAX_LAYERS][VVC_MAX_LAYERS - 1];
    uint8_t  max_tid_il_ref_pics_plus1[VVC_MAX_LAYERS][VVC_MAX_LAYERS - 1];
    uint8_t  each_layer_is_an_ols_flag;
    uint8_t  ols_mode_idc;
    uint8_t  num_output_layer_sets_minus2;
    uint8_t  ols_output_layer_flag[VVC_MAX_TOTAL_NUM_OLSS][VVC_MAX_LAYERS];

    uint8_t  num_ptls_minus1;
    uint8_t  pt_present_flag[VVC_MAX_PTLS];
    uint8_t  ptl_max_tid[VVC_MAX_PTLS];
    //H266RawProfileTierLevel profile_tier_level[VVC_MAX_PTLS];
    uint8_t  ols_ptl_idx[VVC_MAX_TOTAL_NUM_OLSS];

    uint16_t num_dpb_params_minus1;
    uint8_t  sublayer_dpb_params_present_flag;
    uint8_t  dpb_max_tid[VVC_MAX_TOTAL_NUM_OLSS];
    //H266DpbParameters dpb_params[VVC_MAX_TOTAL_NUM_OLSS];
    uint16_t ols_dpb_pic_width[VVC_MAX_TOTAL_NUM_OLSS];
    uint16_t ols_dpb_pic_height[VVC_MAX_TOTAL_NUM_OLSS];
    uint8_t  ols_dpb_chroma_format[VVC_MAX_TOTAL_NUM_OLSS];
    uint8_t  ols_dpb_bitdepth_minus8[VVC_MAX_TOTAL_NUM_OLSS];
    uint16_t ols_dpb_params_idx[VVC_MAX_TOTAL_NUM_OLSS];

    uint8_t  timing_hrd_params_present_flag;
    //H266RawGeneralTimingHrdParameters general_timing_hrd_parameters;
    uint8_t  sublayer_cpb_params_present_flag;
    uint16_t num_ols_timing_hrd_params_minus1;
    uint8_t  hrd_max_tid[VVC_MAX_TOTAL_NUM_OLSS];
    //H266RawOlsTimingHrdParameters ols_timing_hrd_parameters;
    uint8_t  ols_timing_hrd_idx[VVC_MAX_TOTAL_NUM_OLSS];

    uint8_t data[4096];
    int data_size;
} VVCVPS;

typedef struct VVCWindow {
    int left_offset;
    int right_offset;
    int top_offset;
    int bottom_offset;
} VVCWindow;

typedef struct VVCRefPicListStructEntry {
    uint8_t inter_layer_ref_pic_flag;
    uint8_t st_ref_pic_flag;

    //shortterm
    int8_t  delta_poc_val_st;   ///< DeltaPocValSt

    //longterm
    uint8_t lt_msb_flag;        ///< delta_poc_msb_cycle_present_flag
    int16_t lt_poc;

    //interlayer
    uint8_t ilrp_idx;
} VVCRefPicListStructEntry;

typedef struct VVCRefPicListStruct {
    uint8_t num_ref_entries;
    uint8_t num_ltrp_entries;                               ///< NumLtrpEntries
    uint8_t ltrp_in_header_flag;
    VVCRefPicListStructEntry entries[VVC_MAX_REF_ENTRIES];
} VVCRefPicListStruct;

/*
 * we use PartitionConstraints for following fields in spec
    uint8_t  log2_diff_min_qt_min_cb_intra_slice_luma;
    uint8_t  max_mtt_hierarchy_depth_intra_slice_luma;
    uint8_t  log2_diff_max_bt_min_qt_intra_slice_luma;
    uint8_t  log2_diff_max_tt_min_qt_intra_slice_luma;

    uint8_t  log2_diff_min_qt_min_cb_intra_slice_chroma;
    uint8_t  max_mtt_hierarchy_depth_intra_slice_chroma;
    uint8_t  log2_diff_max_bt_min_qt_intra_slice_chroma;
    uint8_t  log2_diff_max_tt_min_qt_intra_slice_chroma;

    uint8_t  log2_diff_min_qt_min_cb_inter_slice;
    uint8_t  max_mtt_hierarchy_depth_inter_slice;
    uint8_t  log2_diff_max_bt_min_qt_inter_slice;
    uint8_t  log2_diff_max_tt_min_qt_inter_slice;

*/
typedef struct PartitionConstraints {
    uint8_t  log2_diff_min_qt_min_cb;
    uint8_t  max_mtt_hierarchy_depth;
    uint8_t  log2_diff_max_bt_min_qt;
    uint8_t  log2_diff_max_tt_min_qt;
} PartitionConstraints;

typedef struct GeneralTimingHrdParameters {
    uint8_t general_nal_hrd_params_present_flag;
    uint8_t general_vcl_hrd_params_present_flag;
    uint8_t general_du_hrd_params_present_flag;
    int hrd_cpb_cnt;    ///< hrd_cpb_cnt_minus1 + 1
} GeneralTimingHrdParameters;

typedef struct VirtualBoundaries {
    uint8_t  virtual_boundaries_present_flag;
    uint8_t  num_ver_virtual_boundaries;
    uint16_t virtual_boundary_pos_x_minus1[3];
    uint8_t  num_hor_virtual_boundaries;
    uint16_t virtual_boundary_pos_y_minus1[3];
} VirtualBoundaries;

typedef struct VVCSPS {
    unsigned video_parameter_set_id;

    //uint8_t separate_colour_plane_flag;

    VVCWindow output_window;

    VVCWindow conf_win;

    int pixel_shift;

    enum AVPixelFormat pix_fmt;

    uint8_t  max_sublayers;
    uint8_t  chroma_format_idc;

    uint8_t  ref_pic_resampling_enabled_flag;
    uint8_t  res_change_in_clvs_allowed_flag;

    uint16_t width;
    uint16_t height;

    uint8_t  subpic_info_present_flag;
    uint16_t num_subpics;
    uint8_t  independent_subpics_flag;
    uint16_t subpic_ctu_top_left_x[VVC_MAX_SLICES];
    uint16_t subpic_ctu_top_left_y[VVC_MAX_SLICES];
    uint16_t subpic_width[VVC_MAX_SLICES];
    uint16_t subpic_height[VVC_MAX_SLICES];
    uint8_t  subpic_treated_as_pic_flag[VVC_MAX_SLICES];
    uint8_t  loop_filter_across_subpic_enabled_flag[VVC_MAX_SLICES];
    uint8_t  subpic_id_len;
    uint8_t  subpic_id_mapping_explicitly_signalled_flag;
    uint8_t  subpic_id_mapping_present_flag;
    uint32_t subpic_id[VVC_MAX_SLICES];

    uint8_t  entropy_coding_sync_enabled_flag;
    uint8_t  entry_point_offsets_present_flag;

    uint8_t  log2_max_pic_order_cnt_lsb;
    uint8_t  poc_msb_cycle_flag;
    uint8_t  poc_msb_cycle_len;

    uint8_t  num_extra_ph_bytes;
    uint8_t  extra_ph_bit_present_flag[16];

    uint8_t  num_extra_sh_bytes;
    uint8_t  extra_sh_bit_present_flag[16];

    uint8_t  sublayer_dpb_params_flag;
    DpbParameters dpb;

    uint8_t  partition_constraints_override_enabled_flag;
    uint8_t  qtbtt_dual_tree_intra_flag;

    PartitionConstraints intra_slice_luma;
    PartitionConstraints intra_slice_chroma;
    PartitionConstraints inter_slice;

    uint8_t  transform_skip_enabled_flag;
    uint8_t  bdpcm_enabled_flag;

    uint8_t  mts_enabled_flag;
    uint8_t  explicit_mts_intra_enabled_flag;
    uint8_t  explicit_mts_inter_enabled_flag;

    uint8_t  lfnst_enabled_flag;

    uint8_t  joint_cbcr_enabled_flag;

    uint8_t  sao_enabled_flag;
    uint8_t  alf_enabled_flag;
    uint8_t  ccalf_enabled_flag;
    uint8_t  lmcs_enabled_flag;

    // inter
    uint8_t  weighted_pred_flag;
    uint8_t  weighted_bipred_flag;
    uint8_t  long_term_ref_pics_flag;
    uint8_t  inter_layer_prediction_enabled_flag;
    uint8_t  idr_rpl_present_flag;
    uint8_t  num_ref_pic_lists[2];
    VVCRefPicListStruct ref_pic_list_struct[2][VVC_MAX_REF_PIC_LISTS];
    uint8_t  ref_wraparound_enabled_flag;
    uint8_t  temporal_mvp_enabled_flag;
    uint8_t  sbtmvp_enabled_flag;
    uint8_t  amvr_enabled_flag;
    uint8_t  bdof_enabled_flag;
    uint8_t  bdof_control_present_in_ph_flag;
    uint8_t  smvd_enabled_flag;
    uint8_t  dmvr_enabled_flag;
    uint8_t  dmvr_control_present_in_ph_flag;
    uint8_t  mmvd_enabled_flag;
    uint8_t  mmvd_fullpel_only_enabled_flag;
    uint8_t  sbt_enabled_flag;
    uint8_t  affine_enabled_flag;
    uint8_t  five_minus_max_num_subblock_merge_cand;
    uint8_t  six_param_affine_enabled_flag;
    uint8_t  affine_amvr_enabled_flag;
    uint8_t  affine_prof_enabled_flag;
    uint8_t  prof_control_present_in_ph_flag;
    uint8_t  bcw_enabled_flag;
    uint8_t  ciip_enabled_flag;
    uint8_t  gpm_enabled_flag;
    uint8_t  log2_parallel_merge_level;

    // intra
    uint8_t  isp_enabled_flag;
    uint8_t  mrl_enabled_flag;
    uint8_t  mip_enabled_flag;
    uint8_t  cclm_enabled_flag;
    uint8_t  chroma_horizontal_collocated_flag;
    uint8_t  chroma_vertical_collocated_flag;
    uint8_t  palette_enabled_flag;
    uint8_t  act_enabled_flag;
    uint8_t  min_qp_prime_ts;
    uint8_t  ibc_enabled_flag;

    uint8_t  ladf_enabled_flag;
    uint8_t  num_ladf_intervals;                                    ///< sps_num_ladf_intervals_minus2 + 2;
    int8_t   ladf_lowest_interval_qp_offset;
    int8_t   ladf_qp_offset[LADF_MAX_INTERVAL];

    uint8_t  explicit_scaling_list_enabled_flag;
    uint8_t  scaling_matrix_for_lfnst_disabled_flag;
    uint8_t  scaling_matrix_for_alternative_colour_space_disabled_flag;
    uint8_t  scaling_matrix_designated_colour_space_flag;
    uint8_t  dep_quant_enabled_flag;
    uint8_t  sign_data_hiding_enabled_flag;

    uint8_t  virtual_boundaries_enabled_flag;
    VirtualBoundaries vbs;

    uint8_t  timing_hrd_params_present_flag;
    uint8_t  sublayer_cpb_params_present_flag;
    GeneralTimingHrdParameters general_timing_hrd_parameters;

    uint8_t  field_seq_flag;

    VUI vui;

    PTL ptl;

    int hshift[3];
    int vshift[3];

    //derived values
    unsigned int max_pic_order_cnt_lsb;                             ///< MaxPicOrderCntLsb
    uint8_t bit_depth;                                              ///< BitDepth
    uint8_t qp_bd_offset;                                           ///< QpBdOffset
    uint8_t ctb_log2_size_y;                                        ///< CtbLog2SizeY
    uint8_t ctb_size_y;                                             ///< CtbSizeY
    uint8_t min_cb_log2_size_y;                                     ///< MinCbLog2SizeY
    uint8_t min_cb_size_y;                                          ///< MinCbSizeY
    uint8_t max_tb_size_y;                                          ///< MaxTbSizeY
    uint8_t max_ts_size;                                            ///< MaxTsSize
    uint8_t max_num_merge_cand;                                     ///< MaxNumMergeCand
    uint8_t max_num_ibc_merge_cand;                                 ///< MaxNumIbcMergeCand
    uint8_t max_num_gpm_merge_cand;                                 ///< MaxNumGpmMergeCand
    unsigned int ladf_interval_lower_bound[LADF_MAX_INTERVAL];      ///< SpsLadfIntervalLowerBound[]

    uint8_t data[4096];
    int data_size;

    int chroma_qp_table[VVC_MAX_SAMPLE_ARRAYS][VVC_MAX_POINTS_IN_QP_TABLE];
} VVCSPS;

typedef struct DBParams {
    int beta_offset[VVC_MAX_SAMPLE_ARRAYS];
    int tc_offset[VVC_MAX_SAMPLE_ARRAYS];
} DBParams;

typedef struct VVCPPS {

    uint8_t  pic_parameter_set_id;
    uint8_t  seq_parameter_set_id;
    uint8_t  mixed_nalu_types_in_pic_flag;
    uint16_t width;
    uint16_t height;

    VVCWindow conf_win;
    VVCWindow scaling_win;

    uint8_t  output_flag_present_flag;
    uint8_t  no_pic_partition_flag;

    uint8_t  loop_filter_across_tiles_enabled_flag;
    uint8_t  rect_slice_flag;
    uint8_t  single_slice_per_subpic_flag;

    uint8_t  tile_idx_delta_present_flag;

    uint8_t  loop_filter_across_slices_enabled_flag;
    uint8_t  cabac_init_present_flag;

    uint8_t  num_ref_idx_default_active[2];
    uint8_t  rpl1_idx_present_flag;
    uint8_t  weighted_pred_flag;
    uint8_t  weighted_bipred_flag;
    uint8_t  ref_wraparound_enabled_flag;
    int8_t   init_qp;                                   ///< 26 + pps_init_qp_minus26
    uint8_t  cu_qp_delta_enabled_flag;

    uint8_t  chroma_tool_offsets_present_flag;
    int8_t   chroma_qp_offset[3];                       ///< pps_cb_qp_offset, pps_cr_qp_offset, pps_joint_cbcr_qp_offset_value
    uint8_t  slice_chroma_qp_offsets_present_flag;
    uint8_t  cu_chroma_qp_offset_list_enabled_flag;
    uint8_t  chroma_qp_offset_list_len_minus1;
    int8_t   chroma_qp_offset_list[6][3];               ///< pps_cb_qp_offset_list, pps_cr_qp_offset_list, pps_joint_cbcr_qp_offset_list

    uint8_t  deblocking_filter_control_present_flag;
    uint8_t  deblocking_filter_override_enabled_flag;
    uint8_t  deblocking_filter_disabled_flag;
    uint8_t  dbf_info_in_ph_flag;
    DBParams deblock;

    uint8_t  rpl_info_in_ph_flag;
    uint8_t  sao_info_in_ph_flag;
    uint8_t  alf_info_in_ph_flag;
    uint8_t  wp_info_in_ph_flag;
    uint8_t  qp_delta_info_in_ph_flag;

    uint8_t  picture_header_extension_present_flag;
    uint8_t  slice_header_extension_present_flag;

    //derived value;
    uint16_t num_tiles_in_pic;
    uint16_t slice_start_offset  [VVC_MAX_SLICES];
    uint16_t num_slices_in_subpic[VVC_MAX_SLICES];
    uint16_t num_ctus_in_slice   [VVC_MAX_SLICES];

    uint16_t min_cb_width;
    uint16_t min_cb_height;

    uint16_t ctb_width;
    uint16_t ctb_height;
    int      ctb_count;

    uint16_t min_pu_width;
    uint16_t min_pu_height;
    uint16_t min_tb_width;
    uint16_t min_tb_height;

    uint16_t num_tile_columns;              ///< NumTileColumns
    uint16_t num_tile_rows;                 ///< NumTileRows
    unsigned int *column_width;             ///< ColWidthVal
    unsigned int *row_height;               ///< RowHeightVal
    unsigned int *col_bd;                   ///< TileColBdVal
    unsigned int *row_bd;                   ///< TileRowBdVal
    unsigned int *ctb_to_col_bd;            ///< CtbToTileColBd
    unsigned int *ctb_to_row_bd;            ///< CtbToTileRowBd
    unsigned int *ctb_addr_in_slice;        ///< CtbAddrInCurrSlice for entire picture

    uint16_t width32;                       ///< width  in 32 pixels
    uint16_t height32;                      ///< height in 32 pixels
    uint16_t width64;                       ///< width  in 64 pixels
    uint16_t height64;                      ///< height in 64 pixels

    uint16_t subpic_id[VVC_MAX_SLICES];     ///< SubpicIdVal[]
    uint16_t ref_wraparound_offset;         ///< PpsRefWraparoundOffset

    uint8_t data[4096];
    int data_size;
} VVCPPS;

#define VVC_MAX_ALF_COUNT        8
#define VVC_MAX_LMCS_COUNT       4
#define VVC_MAX_SL_COUNT         8

#define ALF_NUM_FILTERS_LUMA    25
#define ALF_NUM_FILTERS_CHROMA   8
#define ALF_NUM_FILTERS_CC       5

#define ALF_NUM_COEFF_LUMA      12
#define ALF_NUM_COEFF_CHROMA     6
#define ALF_NUM_COEFF_CC         7

enum {
    APS_ALF,
    APS_LMCS,
    APS_SCALING,
};

typedef struct Alf {
    //< ph_alf_enabled_flag, ph_alf_cb_enabled_flag, ph_alf_cr_enabled_flag
    //< sh_alf_enabled_flag, sh_alf_cb_enabled_flag, sh_alf_cr_enabled_flag
    uint8_t  enabled_flag[VVC_MAX_SAMPLE_ARRAYS];

    //< ph_num_alf_aps_ids_luma
    //< sh_alf_aps_id_luma
    uint8_t  num_aps_ids_luma;

    //< ph_alf_aps_id_luma
    //< sh_alf_aps_id_luma
    uint8_t  aps_id_luma[8];

    //< ph_alf_aps_id_chroma
    //< sh_alf_aps_id_chroma
    uint8_t  aps_id_chroma;

    //< ph_alf_cc_cb_enabled_flag, ph_alf_cc_cr_enabled_flag
    //< sh_alf_cc_cb_enabled_flag, sh_alf_cc_cr_enabled_flag
    uint8_t  cc_enabled_flag[2];

    //< ph_alf_cc_cb_aps_id, ph_alf_cc_cr_aps_id
    //< sh_alf_cc_cb_aps_id, sh_alf_cc_cr_aps_id
    uint8_t  cc_aps_id[2];
} Alf;

#define MAX_WEIGHTS 15
typedef struct PredWeightTable {
    int log2_denom[2];                                          ///< luma_log2_weight_denom, ChromaLog2WeightDenom

    int nb_weights[2];                                          ///< num_l0_weights, num_l1_weights
    int weight_flag[2][2][MAX_WEIGHTS];                         ///< luma_weight_l0_flag, chroma_weight_l0_flag,
                                                                ///< luma_weight_l1_flag, chroma_weight_l1_flag,
    int weight[2][VVC_MAX_SAMPLE_ARRAYS][MAX_WEIGHTS];          ///< LumaWeightL0, LumaWeightL1, ChromaWeightL0, ChromaWeightL1
    int offset[2][VVC_MAX_SAMPLE_ARRAYS][MAX_WEIGHTS];          ///< luma_offset_l0, luma_offset_l1, ChromaOffsetL0, ChromaOffsetL1
} PredWeightTable;

typedef struct VVCPH {
    uint8_t  gdr_or_irap_pic_flag;
    uint8_t  non_ref_pic_flag;
    uint8_t  gdr_pic_flag;

    uint8_t  inter_slice_allowed_flag;
    uint8_t  intra_slice_allowed_flag;

    uint8_t  pic_parameter_set_id;

    uint16_t pic_order_cnt_lsb;
    uint8_t  recovery_poc_cnt;
    uint8_t  poc_msb_cycle_present_flag;
    uint8_t  poc_msb_cycle_val;

    Alf alf;

    uint8_t  lmcs_enabled_flag;
    uint8_t  lmcs_aps_id;

    uint8_t  chroma_residual_scale_flag;
    uint8_t  explicit_scaling_list_enabled_flag;
    uint8_t  scaling_list_aps_id;

    VirtualBoundaries vbs;

    uint8_t  pic_output_flag;
    VVCRefPicListStruct rpls[2];

    uint8_t  partition_constraints_override_flag;
    PartitionConstraints intra_slice_luma;
    PartitionConstraints intra_slice_chroma;
    PartitionConstraints inter_slice;


    uint8_t  cu_qp_delta_subdiv[2];             ///< cu_qp_delta_subdiv_intra_slice, cu_qp_delta_subdiv_inter_slice
    uint8_t  cu_chroma_qp_offset_subdiv[2];     ///< cu_chroma_qp_offset_subdiv_intra_slice, cu_chroma_qp_offset_subdiv_inter_slice

    uint8_t  temporal_mvp_enabled_flag;
    uint8_t  collocated_ref_idx;
    uint8_t  mmvd_fullpel_only_flag;
    uint8_t  mvd_l1_zero_flag;
    uint8_t  bdof_disabled_flag;
    uint8_t  dmvr_disabled_flag;
    uint8_t  prof_disabled_flag;
    PredWeightTable pwt;

    int8_t   qp_delta;
    uint8_t  joint_cbcr_sign_flag;

    uint8_t  sao_luma_enabled_flag;
    uint8_t  sao_chroma_enabled_flag;

    uint8_t  deblocking_filter_disabled_flag;
    DBParams deblock;

    //derived values
    unsigned int max_num_subblock_merge_cand;   ///< MaxNumSubblockMergeCand

    int     poc;                                ///< PicOrderCntVal
    uint8_t collocated_list;                    ///< !collocated_from_l0_flag

    //*2 for high depth
    uint8_t  lmcs_fwd_lut[LMCS_MAX_LUT_SIZE * 2];
    uint8_t  lmcs_inv_lut[LMCS_MAX_LUT_SIZE * 2];
    int      lmcs_min_bin_idx;
    int      lmcs_max_bin_idx;
    int      lmcs_pivot[LMCS_MAX_BIN_SIZE + 1];
    int      lmcs_chroma_scale_coeff[LMCS_MAX_BIN_SIZE];

    int size;
} VVCPH;

typedef struct VVCALF {
    int8_t  luma_coeff     [ALF_NUM_FILTERS_LUMA * ALF_NUM_COEFF_LUMA];
    uint8_t luma_clip_idx  [ALF_NUM_FILTERS_LUMA * ALF_NUM_COEFF_LUMA];

    uint8_t num_chroma_filters;
    int8_t  chroma_coeff   [ALF_NUM_FILTERS_CHROMA * ALF_NUM_COEFF_CHROMA];
    uint8_t chroma_clip_idx[ALF_NUM_FILTERS_CHROMA * ALF_NUM_COEFF_CHROMA];

    uint8_t cc_filters_signalled[2];        //< alf_cc_cb_filters_signalled + 1, alf_cc_cr_filters_signalled + 1
    int8_t  cc_coeff[2][ALF_NUM_FILTERS_CC * ALF_NUM_COEFF_CC];
} VVCALF;

#define SL_MAX_ID          28
#define SL_MAX_MATRIX_SIZE 8

enum {
  SL_START_2x2    = 0,
  SL_START_4x4    = 2,
  SL_START_8x8    = 8,
  SL_START_16x16  = 14,
  SL_START_32x32  = 20,
  SL_START_64x64  = 26,
};

typedef struct VVCScalingList {
    uint8_t scaling_matrix_rec[SL_MAX_ID][SL_MAX_MATRIX_SIZE * SL_MAX_MATRIX_SIZE];  ///< ScalingMatrixRec
    uint8_t scaling_matrix_dc_rec[SL_MAX_ID - SL_START_16x16];                       ///< ScalingMatrixDcRec[refId − 14]
} VVCScalingList;

typedef struct VVCParamSets {
    AVBufferRef *vps_list[VVC_MAX_VPS_COUNT];
    AVBufferRef *sps_list[VVC_MAX_SPS_COUNT];
    AVBufferRef *pps_list[VVC_MAX_PPS_COUNT];
    AVBufferRef *alf_list[VVC_MAX_ALF_COUNT];
    AVBufferRef *lmcs_list[VVC_MAX_LMCS_COUNT];
    AVBufferRef *scaling_list[VVC_MAX_SL_COUNT];
    AVBufferRef *ph_buf;

    const VVCSPS *sps;
    const VVCPPS *pps;
    VVCPH  *ph;
} VVCParamSets;

typedef struct VVCFrameParamSets {
    AVBufferRef *sps_buf;
    AVBufferRef *pps_buf;
    AVBufferRef *ph_buf;
    AVBufferRef *alf_list[VVC_MAX_ALF_COUNT];
    AVBufferRef *lmcs_list[VVC_MAX_LMCS_COUNT];
    AVBufferRef *scaling_list[VVC_MAX_SL_COUNT];

    const VVCSPS *sps;
    const VVCPPS *pps;
    const VVCPH  *ph;
} VVCFrameParamSets;

typedef struct VVCSH {

    uint16_t subpic_id;
    uint16_t slice_address;
    uint8_t  num_tiles_in_slice;
    uint8_t  slice_type;
    uint8_t  no_output_of_prior_pics_flag;

    Alf alf;

    uint8_t  lmcs_used_flag;
    uint8_t  explicit_scaling_list_used_flag;

    VVCRefPicListStruct rpls[2];

    uint8_t  num_ref_idx_active_override_flag;
    uint8_t  cabac_init_flag;
    uint8_t  collocated_ref_idx;

    PredWeightTable pwt;

    int8_t   chroma_qp_offset[3];                           ///< cb_qp_offset, cr_qp_offset, joint_cbcr_qp_offset
    uint8_t  cu_chroma_qp_offset_enabled_flag;

    uint8_t  sao_used_flag[VVC_MAX_SAMPLE_ARRAYS];          ///< sh_sao_luma_used_flag and sh_sao_chroma_used_flag

    uint8_t  deblocking_filter_disabled_flag;
    DBParams deblock;
    uint8_t  dep_quant_used_flag;

    uint8_t  sign_data_hiding_used_flag;
    uint8_t  ts_residual_coding_disabled_flag;

    //calculated value;
    uint8_t  collocated_list;                               ///< !sh_collocated_from_l0_flag
    uint8_t  nb_refs[2];                                    ///< NumRefIdxActive[]
    uint8_t  min_qt_size[2];                                ///< MinQtSizeY, MinQtSizeC
    uint8_t  max_bt_size[2];                                ///< MaxBtSizeY, MaxBtSizeC
    uint8_t  max_tt_size[2];                                ///< MaxTtSizeY, MaxTtSizeC
    uint8_t  max_mtt_depth[2];                              ///< MaxMttDepthY, MaxMttDepthC
    uint8_t  cu_qp_delta_subdiv;                            ///< CuQpDeltaSubdiv
    uint8_t  cu_chroma_qp_offset_subdiv;                    ///< CuChromaQpOffsetSubdiv
    int8_t   slice_qp_y;                                    ///< SliceQpY
    int8_t   ref_idx_sym[2];                                ///< RefIdxSymL0, RefIdxSymL1
    int      num_ctus_in_curr_slice;                        ///< NumCtusInCurrSlice
    uint32_t entry_point_offset[VVC_MAX_ENTRY_POINTS];      ///< entry_point_offset_minus1 + 1
    uint32_t entry_point_start_ctu[VVC_MAX_ENTRY_POINTS];   ///< entry point start in ctu_addr
    unsigned int *ctb_addr_in_curr_slice;                   ///< CtbAddrInCurrSlice
    unsigned int num_entry_points;                          ///< NumEntryPoints

    uint8_t first_slice_flag;
} VVCSH;

struct VVCContext;

int ff_vvc_decode_sps(VVCParamSets *ps, GetBitContext *gb,
     int apply_defdispwin, int nuh_layer_id, AVCodecContext *avctx);
int ff_vvc_decode_pps(VVCParamSets *ps, GetBitContext *gb, void *log_ctx);
int ff_vvc_decode_ph(VVCParamSets *ps, const int poc_tid0, const int is_clvss, GetBitContext *gb, void *log_ctx);
int ff_vvc_decode_aps(VVCParamSets *ps, GetBitContext *gb, void *log_ctx);
int ff_vvc_decode_sh(VVCSH *sh, struct VVCContext *s, const int is_first_slice, GetBitContext *gb);
void ff_vvc_ps_uninit(VVCParamSets *ps);

#endif /* AVCODEC_VVC_PS_H */
