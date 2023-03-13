/*
 * Copyright © 2018, VideoLAN and dav1d authors
 * Copyright © 2018, Two Orioles, LLC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "libavutil/attributes.h"

#ifdef pixel
#undef pixel
#endif

#ifdef AVCODEC_VVC_BITDEPTH_H
#undef pixel_copy
#undef clip_pixel
#undef bitfn
#undef HIGHBD_DECL_SUFFIX
#undef HIGHBD_TAIL_SUFFIX
#undef bitdepth_from_max
#undef BITDEPTH_MAX
#undef get_bit_depth
#else
#define AVCODEC_VVC_BITDEPTH_H
#endif

#if VVC_BPC == 8
#define pixel  uint8_t
#define pixel_copy memcpy
#define clip_pixel av_clip_uint8
#define bitfn(x) x##_8bpc
#define HIGHBD_DECL_SUFFIX /* nothing */
#define HIGHBD_TAIL_SUFFIX /* nothing */
#define get_bit_depth(x) 8
#elif VVC_BPC == 16
#define pixel  uint16_t
#define pixel_copy(a, b, c) memcpy(a, b, (c) << 1)
#define clip_pixel(x) av_clip_uintp2(x, get_bit_depth())
#define bitfn(x) x##_16bpc
#define HIGHBD_DECL_SUFFIX , const int _bit_depth
#define HIGHBD_TAIL_SUFFIX , BIT_DEPTH
#define get_bit_depth(x) (_bit_depth)
#else
#error invalid value for bitdepth
#endif
#define bytefn(x) bitfn(x)

#define bitfn_decls(name, ...) \
name##_8bpc(__VA_ARGS__); \
name##_16bpc(__VA_ARGS__)
