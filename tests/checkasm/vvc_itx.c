/*
 * Copyright (c) 2023 Frank Plowman <post@frankplowman.com>
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

#include "libavutil/mem_internal.h"

#include "libavcodec/avcodec.h"

#include "libavcodec/vvc/vvcdsp.h"
#include "libavcodec/vvc/vvcdec.h"

#include "checkasm.h"

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE (MAX_TB_SIZE * MAX_TB_SIZE)

#define randomize_buffers(buf0, buf1, size, min, max)                           \
    do {                                                                        \
        int k;                                                                  \
        for (k = 0; k < size; ++k) {                                            \
            uint32_t r = rnd();                                                 \
            int32_t a = min + r / (max / (max - min + 1) + 1);                  \
            AV_WN32A(buf0 + k, a);                                              \
            AV_WN32A(buf1 + k, a);                                              \
        }                                                                       \
    } while (0)

const char *itx_str[N_TX_TYPE] = {
    "dct2", // DCT2
    "dst7", // DST7
    "dct8", // DCT8
};

const int itx_log2_min_size[N_TX_TYPE] = {
    1,  // DCT2
    2,  // DST7
    2,  // DCT8
};

const int itx_log2_max_size[N_TX_TYPE] = {
    6,  // DCT2
    5,  // DST7
    5,  // DCT8
};

static void check_itx(VVCDSPContext h, enum TxType trh, enum TxType trv, int bit_depth)
{
    LOCAL_ALIGNED_32(int, ref_dst, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, new_dst, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, ref_src, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, new_src, [BUF_SIZE]);

    for (int log2_width = itx_log2_min_size[trh]; log2_width <= itx_log2_max_size[trh]; ++log2_width) {
        for (int log2_height = itx_log2_min_size[trv]; log2_height <= itx_log2_max_size[trv]; ++log2_height) {
            const int width = 1 << log2_width;
            const int height = 1 << log2_height;

            declare_func_emms(AV_CPU_FLAG_MMX, void, int *dst, const int *src, int nzw, int log2_transform_range);

            randomize_buffers(ref_src, new_src, BUF_SIZE, -(1 << (bit_depth - 1)), 1 << (bit_depth - 1) - 1);
            memset(ref_dst, 0, BUF_SIZE);
            memset(new_dst, 0, BUF_SIZE);

            // @TODO: test extended precision (l2tr != 15)
            // @TODO: test nzw != width
            if (check_func(h.itx.itx[trh][trv][log2_width][log2_height],
                           "inv_%s_%s_%dx%d_%d",
                           itx_str[trh], itx_str[trv], width, height, bit_depth)) {
                call_ref(ref_dst, ref_src, width, 15);
                call_new(new_dst, new_src, width, 15);
                checkasm_check_int32_t("vvc_itx_1d.asm", 0, ref_dst, 1, new_dst, 1, width, height, "dst");
            }
            bench_new(new_dst, new_src, width, 15);
        }
    }
}

void checkasm_check_vvc_itx(void)
{
    VVCDSPContext h;
    ff_vvc_dsp_init(&h, 8);
    check_itx(h, DCT2, DCT2, 8);
    ff_vvc_dsp_init(&h, 10);
    check_itx(h, DCT2, DCT2, 10);
    report("idct2");
}
