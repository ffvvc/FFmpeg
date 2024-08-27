/*
 * VVC Supplementary Enhancement Information messages
 *
 * copyright (c) 2024 Wu Jianhua <toqsxw@outlook.com>
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

#include "sei.h"
#include "dec.h"

static int decode_film_grain_characteristics(const VVCFrameContext *fc, H2645SEIFilmGrainCharacteristics *h, const SEIRawFilmGrainCharacteristics *s)
{
    const VVCSPS *sps = fc->ps.sps;

    h->present = !s->fg_characteristics_cancel_flag;
    if (h->present) {
        h->model_id                                 = s->fg_model_id;
        h->separate_colour_description_present_flag = s->fg_separate_colour_description_present_flag;
        if (h->separate_colour_description_present_flag) {
            h->bit_depth_luma           =  s->fg_bit_depth_luma_minus8 + 8;
            h->bit_depth_chroma         =  s->fg_bit_depth_chroma_minus8 + 8;
            h->full_range               =  s->fg_full_range_flag;
            h->color_primaries          =  s->fg_colour_primaries;
            h->transfer_characteristics =  s->fg_transfer_characteristics;
            h->matrix_coeffs            =  s->fg_matrix_coeffs;
        }  else {
            if (!sps) {
                av_log(fc->log_ctx, AV_LOG_ERROR,
                    "No active SPS for film_grain_characteristics.\n");
                return AVERROR_INVALIDDATA;
            }
            h->bit_depth_luma           = sps->bit_depth;
            h->bit_depth_chroma         = sps->bit_depth;
            h->full_range               = sps->r->vui.vui_full_range_flag;
            h->color_primaries          = sps->r->vui.vui_colour_primaries;
            h->transfer_characteristics = sps->r->vui.vui_transfer_characteristics;
            h->matrix_coeffs            = sps->r->vui.vui_matrix_coeffs ;
        }

        h->blending_mode_id  =  s->fg_blending_mode_id;
        h->log2_scale_factor =  s->fg_log2_scale_factor;

        for (int c = 0; c < 3; c++) {
            h->comp_model_present_flag[c] = s->fg_comp_model_present_flag[c];
            if (h->comp_model_present_flag[c]) {
                h->num_intensity_intervals[c] = s->fg_num_intensity_intervals_minus1[c] + 1;
                h->num_model_values[c]        = s->fg_num_model_values_minus1[c] + 1;

                if (h->num_model_values[c] > 6)
                    return AVERROR_INVALIDDATA;

                for (int i = 0; i < h->num_intensity_intervals[c]; i++) {
                    h->intensity_interval_lower_bound[c][i] = s->fg_intensity_interval_lower_bound[c][i];
                    h->intensity_interval_upper_bound[c][i] = s->fg_intensity_interval_upper_bound[c][i];
                    for (int j = 0; j < h->num_model_values[c]; j++)
                        h->comp_model_value[c][i][j] = s->fg_comp_model_value[c][i][j];
                }
            }
        }

        h->persistence_flag = s->fg_characteristics_persistence_flag;
    }

    return 0;
}

int ff_vvc_decode_nal_sei(void *logctx, VVCSEI *s, const H266RawSEI *sei)
{
    const VVCFrameContext *fc = logctx;
    H2645SEI              *c  = &s->common;
    SEIRawMessage *message;
    void *payload;

    if (!sei)
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < sei->message_list.nb_messages; i++) {
        message = &sei->message_list.messages[i];
        payload = message->payload;

        switch (message->payload_type) {
        case SEI_TYPE_FILM_GRAIN_CHARACTERISTICS:
            return decode_film_grain_characteristics(fc, &c->film_grain_characteristics, payload);

        default:
            av_log(fc->log_ctx, AV_LOG_DEBUG, "Skipped %s SEI %d\n",
                sei->nal_unit_header.nal_unit_type == VVC_PREFIX_SEI_NUT ?
                    "PREFIX" : "SUFFIX", message->payload_type);
            return FF_H2645_SEI_MESSAGE_UNHANDLED;
        }
    }

    return 0;
}

void ff_vvc_reset_sei(VVCSEI *s)
{
    ff_h2645_sei_reset(&s->common);
    s->common.film_grain_characteristics.present = 0;
}
