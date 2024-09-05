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

#ifndef AVCODEC_VVC_SEI_H
#define AVCODEC_VVC_SEI_H

#include <stdint.h>

#include "libavcodec/get_bits.h"
#include "libavcodec/cbs.h"
#include "libavcodec/cbs_h266.h"
#include "libavcodec/h2645_sei.h"
#include "libavcodec/sei.h"
#include "libavcodec/vvc.h"
#include "libavcodec/h274.h"

typedef struct VVCSEIPictureHash {
    int present;
    union {
        uint8_t  md5[3][16];
        uint16_t crc[3];
        uint32_t checksum[3];
    };
    uint8_t hash_type;
} VVCSEIPictureHash;

typedef struct VVCSEI {
    H2645SEI common;
    H274SEIPictureHash picture_hash;
} VVCSEI;

int ff_vvc_decode_nal_sei(void *logctx, VVCSEI *s, const H266RawSEI *sei);

void ff_vvc_reset_sei(VVCSEI *s);

#endif /* AVCODEC_VVC_SEI_H */
