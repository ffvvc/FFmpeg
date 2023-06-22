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
#define PIXEL_STRIDE (2*MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE)
#define BUF_SIZE (PIXEL_STRIDE * MAX_TB_SIZE)

#define randomize_buffers(buf0, buf1, size)                                             \
    do {                                                                                \
        int k;                                                                          \
        for (k = 0; k < size; ++k) {                                                    \
            uint32_t r = rnd();                                                         \
            int32_t a = INT16_MIN + r / (UINT32_MAX / (INT16_MAX - INT16_MIN + 1) + 1); \
            AV_WN32A(buf0 + k, a);                                                      \
            AV_WN32A(buf1 + k, a);                                                      \
        }                                                                               \
    } while (0)

static void check_idct2(VVCDSPContext h, int bit_depth)
{
    LOCAL_ALIGNED_32(int, ref_dst, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, new_dst, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, ref_src, [BUF_SIZE]);
    LOCAL_ALIGNED_32(int, new_src, [BUF_SIZE]);

    const ptrdiff_t stride = PIXEL_STRIDE * SIZEOF_PIXEL;

    for (int log2_size = 1; log2_size <= 6; log2_size++) {
        const int size = 1 << log2_size;
        declare_func_emms(AV_CPU_FLAG_MMX, void, int *dst, ptrdiff_t dst_stride,
                                                 int *src, ptrdiff_t src_stride);

        randomize_buffers(ref_src, new_src, BUF_SIZE);
        memset(ref_dst, 0, BUF_SIZE);
        memset(new_dst, 0, BUF_SIZE);

        if (check_func(h.itx.itx[DCT2][log2_size - 1], "vvc_inv_dct2_%d", size)) {
            call_ref(ref_dst, stride, ref_src, stride);
            call_new(new_dst, stride, new_src, stride);
            checkasm_check_int32_t("vvc_itx_1d.asm", 0, ref_dst, stride * sizeof(int), new_dst, stride * sizeof(int), 1, size, "dst");
        }
        bench_new(new_dst, stride, new_src, stride);
    }
}

void checkasm_check_vvc_itx_1d(void)
{
    VVCDSPContext h;
    ff_vvc_dsp_init(&h, 8);
    check_idct2(h, 8);
    report("idct2");
}
