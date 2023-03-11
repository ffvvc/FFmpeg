/*
 * Copyright (c) 2023 Nuo Mi <nuomi2021@gmail.com>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <string.h>

#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

#include "libavcodec/avcodec.h"

#include "libavcodec/vvcdsp.h"
#include "libavcodec/vvcdec.h"

#include "checkasm.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define PIXEL_STRIDE (MAX_CU_SIZE + 2 * ALF_PADDING_SIZE)
#define BUF_SIZE (PIXEL_STRIDE * (MAX_CTU_SIZE + 3 * 2) * 2) //+3 * 2 for top and bottom row, *2 for high bit depth
#define LUMA_PARAMS_SIZE (MAX_CU_SIZE * MAX_CU_SIZE / ALF_BLOCK_SIZE / ALF_BLOCK_SIZE * ALF_NUM_COEFF_LUMA)

#define randomize_buffers(buf0, buf1, size)                 \
    do {                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];   \
        int k;                                              \
        for (k = 0; k < size; k += 4) {                     \
            uint32_t r = rnd() & mask;                      \
            AV_WN32A(buf0 + k, r);                          \
            AV_WN32A(buf1 + k, r);                          \
        }                                                   \
    } while (0)

#define randomize_buffers2(buf, size, filter)               \
    do {                                                    \
        int k;                                              \
        if (filter) {                                       \
            for (k = 0; k < size; k++) {                    \
                int8_t r = rnd();                           \
                buf[k] = r;                                 \
            }                                               \
        } else {                                            \
            for (k = 0; k < size; k++) {                    \
                int r = rnd() % FF_ARRAY_ELEMS(clip_set);   \
                buf[k] = clip_set[r];                       \
            }                                               \
        }                                                   \
    } while (0)

static void check_alf_filter(VVCDSPContext *c, const int bit_depth)
{
    LOCAL_ALIGNED_32(uint8_t, dst0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, dst1, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, src1, [BUF_SIZE]);
    int8_t filter[LUMA_PARAMS_SIZE];
    int16_t clip[LUMA_PARAMS_SIZE];
    const int16_t clip_set[] = {
        1 << bit_depth, 1 << (bit_depth - 3), 1 << (bit_depth - 5), 1 << (bit_depth - 7)
    };

    ptrdiff_t stride = PIXEL_STRIDE * SIZEOF_PIXEL;
    int offset = (3 * PIXEL_STRIDE + 3) * SIZEOF_PIXEL;

    declare_func_emms(AV_CPU_FLAG_AVX2, void, uint8_t *dst, ptrdiff_t dst_stride, const uint8_t *src, ptrdiff_t src_stride,
        int width, int height, const int8_t *filter, const int16_t *clip);

    randomize_buffers(src0, src1, BUF_SIZE);
    randomize_buffers2(filter, LUMA_PARAMS_SIZE, 1);
    randomize_buffers2(clip, LUMA_PARAMS_SIZE, 0);

    for (int h = 4; h <= MAX_CU_SIZE; h += 4) {
        for (int w = 4; w <= MAX_CU_SIZE; w += 4) {
            if (check_func(c->alf.filter[LUMA], "vvc_alf_filter_luma_%dx%d_%d", w, h, bit_depth)) {
                memset(dst0, 0, BUF_SIZE);
                memset(dst1, 0, BUF_SIZE);
                call_ref(dst0, stride, src0 + offset, stride, w, h, filter, clip);
                call_new(dst1, stride, src1 + offset, stride, w, h, filter, clip);
                for (int i = 0; i < h; i++) {
                    if (memcmp(dst0 + i * stride, dst1 + i * stride, w * SIZEOF_PIXEL))
                        fail();
                }
                bench_new(dst1, stride, src1 + offset, stride, w, h, filter, clip);
            }
            if (check_func(c->alf.filter[CHROMA], "vvc_alf_filter_chroma_%dx%d_%d", w, h, bit_depth)) {
                memset(dst0, 0, BUF_SIZE);
                memset(dst1, 0, BUF_SIZE);
                call_ref(dst0, stride, src0 + offset, stride, w, h, filter, clip);
                call_new(dst1, stride, src1 + offset, stride, w, h, filter, clip);
                for (int i = 0; i < h; i++) {
                    if (memcmp(dst0 + i * stride, dst1 + i * stride, w * SIZEOF_PIXEL))
                        fail();
                }
                bench_new(dst1, stride, src1 + offset, stride, w, h, filter, clip);
            }
        }
    }
}

void checkasm_check_vvc_alf(void)
{
    int bit_depth;
    VVCDSPContext h;

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vvc_dsp_init(&h, bit_depth);
        check_alf_filter(&h, bit_depth);
    }
    report("alf_filter");
}
