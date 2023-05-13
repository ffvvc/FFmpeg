/*
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

#include "libavutil/common.h"
#include "libavutil/opt.h"

#include "bsf.h"
#include "bsf_internal.h"
#include "cbs.h"
#include "cbs_bsf.h"
#include "cbs_h266.h"
#include "vvc.h"

#define IS_SLICE(nut) (nut <= VVC_RASL_NUT || (nut >= VVC_IDR_W_RADL && nut <= VVC_GDR_NUT))
#define IS_PH(nut) (nut == VVC_PH_NUT)

typedef struct VVCMetadataContext {
    CBSBSFContext common;

    H266RawAUD aud_nal;

    int aud;
} VVCMetadataContext;

static int h266_metadata_update_fragment(AVBSFContext *bsf, AVPacket *pkt,
                                         CodedBitstreamFragment *pu)
{
    VVCMetadataContext *ctx = bsf->priv_data;
    int err, i;

    // If an AUD is present, it must be the first NAL unit.
    if (pu->units[0].type == VVC_AUD_NUT) {
        if (ctx->aud == BSF_ELEMENT_REMOVE)
            ff_cbs_delete_unit(pu, 0);
    } else {
        if (ctx->aud == BSF_ELEMENT_INSERT) {
            const H266RawSlice *first_slice = NULL;
            const H266RawPH    *ph = NULL;
            H266RawAUD *aud = &ctx->aud_nal;
            int pic_type = 0, temporal_id = 8, layer_id = 0;
            for (i = 0; i < pu->nb_units; i++) {
                const H266RawNALUnitHeader *nal = pu->units[i].content;
                if (!nal)
                    continue;
                if (nal->nuh_temporal_id_plus1 < temporal_id + 1)
                    temporal_id = nal->nuh_temporal_id_plus1 - 1;
                if (IS_PH(nal->nal_unit_type)) {
                    ph = pu->units[i].content;
                } else if(IS_SLICE(nal->nal_unit_type)) {
                    const H266RawSlice *slice = pu->units[i].content;
                    layer_id = nal->nuh_layer_id;
                    if (slice->header.sh_slice_type == VVC_SLICE_TYPE_B &&
                        pic_type < 2)
                        pic_type = 2;
                    if (slice->header.sh_slice_type == VVC_SLICE_TYPE_P &&
                        pic_type < 1)
                        pic_type = 1;
                    if (!first_slice) {
                        first_slice = slice;
                        if (first_slice->header.sh_picture_header_in_slice_header_flag)
                            ph = &first_slice->header.sh_picture_header;
                        else if (!ph)
                            break;
                    }
                }
            }
            if (!ph) {
                av_log(bsf, AV_LOG_ERROR, "no avaliable picture header");
                return AVERROR_INVALIDDATA;
            }

            aud->nal_unit_header = (H266RawNALUnitHeader) {
                .nal_unit_type         = VVC_AUD_NUT,
                .nuh_layer_id          = layer_id,
                .nuh_temporal_id_plus1 = temporal_id + 1,
            };
            aud->aud_pic_type = pic_type;
            aud->aud_irap_or_gdr_flag = ph->ph_gdr_or_irap_pic_flag;

            err = ff_cbs_insert_unit_content(pu, 0, VVC_AUD_NUT, aud, NULL);
            if (err < 0) {
                av_log(bsf, AV_LOG_ERROR, "Failed to insert AUD.\n");
                return err;
            }
        }
    }
    return 0;
}

static const CBSBSFType h266_metadata_type = {
    .codec_id        = AV_CODEC_ID_VVC,
    .fragment_name   = "access unit",
    .unit_name       = "NAL unit",
    .update_fragment = &h266_metadata_update_fragment,
};

static int vvc_metadata_init(AVBSFContext *bsf)
{
    return ff_cbs_bsf_generic_init(bsf, &h266_metadata_type);
}

#define OFFSET(x) offsetof(VVCMetadataContext, x)
#define FLAGS (AV_OPT_FLAG_VIDEO_PARAM|AV_OPT_FLAG_BSF_PARAM)
static const AVOption vvc_metadata_options[] = {
    BSF_ELEMENT_OPTIONS_PIR("aud", "Access Unit Delimiter NAL units",
                            aud, FLAGS),

    { NULL }
};

static const AVClass vvc_metadata_class = {
    .class_name = "vvc_metadata_bsf",
    .item_name  = av_default_item_name,
    .option     = vvc_metadata_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

static const enum AVCodecID vvc_metadata_codec_ids[] = {
    AV_CODEC_ID_VVC, AV_CODEC_ID_NONE,
};

const FFBitStreamFilter ff_vvc_metadata_bsf = {
    .p.name           = "vvc_metadata",
    .p.codec_ids      = vvc_metadata_codec_ids,
    .p.priv_class     = &vvc_metadata_class,
    .priv_data_size = sizeof(VVCMetadataContext),
    .init           = &vvc_metadata_init,
    .close          = &ff_cbs_bsf_generic_close,
    .filter         = &ff_cbs_bsf_generic_filter,
};
