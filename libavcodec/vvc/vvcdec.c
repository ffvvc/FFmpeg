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
#include "libavcodec/codec_internal.h"
#include "libavcodec/profiles.h"

#include "vvcdec.h"

static int vvc_decode_frame(AVCodecContext *avctx, AVFrame *output,
    int *got_output, AVPacket *avpkt)
{
    return avpkt->size;
}

static void vvc_decode_flush(AVCodecContext *avctx)
{
}

static av_cold int vvc_decode_free(AVCodecContext *avctx)
{
    return 0;
}

static av_cold int vvc_decode_init(AVCodecContext *avctx)
{
    return 0;
}

const FFCodec ff_vvc_decoder = {
    .p.name                  = "vvc",
    .p.long_name             = NULL_IF_CONFIG_SMALL("VVC (Versatile Video Coding)"),
    .p.type                  = AVMEDIA_TYPE_VIDEO,
    .p.id                    = AV_CODEC_ID_VVC,
    .priv_data_size          = sizeof(VVCContext),
    .init                    = vvc_decode_init,
    .close                   = vvc_decode_free,
    FF_CODEC_DECODE_CB(vvc_decode_frame),
    .flush                   = vvc_decode_flush,
    .p.capabilities          = AV_CODEC_CAP_DR1 | AV_CODEC_CAP_DELAY | AV_CODEC_CAP_OTHER_THREADS,
    .caps_internal           = FF_CODEC_CAP_EXPORTS_CROPPING | FF_CODEC_CAP_INIT_CLEANUP |
                               FF_CODEC_CAP_AUTO_THREADS,
    .p.profiles              = NULL_IF_CONFIG_SMALL(ff_vvc_profiles),
};
