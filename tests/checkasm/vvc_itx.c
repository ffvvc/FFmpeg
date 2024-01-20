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

#include "checkasm.h"
#include "libavcodec/vvc/vvc_ctu.h"
#include "libavcodec/vvc/vvcdsp.h"

static void randomize_coeffs(int *c0, int *c1,
    const int w, const int h, const size_t nzw, const int nzh, intptr_t log2_transform_range)
{
    for (int ht = 0; ht < h; ht++) {
        for (int wd = 0; wd < w; wd++) {
            if (wd < nzw && ht < nzh)
                *c0 = *c1 = av_clip_intp2(rnd(), log2_transform_range);
            c0++;
            c1++;
        }
    }
}

static const char* name(int tr)
{
    static const char *name[] = {
        "dct2",
        "dst7",
        "dct8",
    };
    return name[tr];
}

static const int zero_out_size(const int tr, const int size)
{
    return FFMIN(tr == DCT2 ? 32 : 16, size);
}

static enum TxType tr_end(const int size)
{
    return size >= 4 && size <= 32 ? DCT8 : DCT2;
}

static void check_itx_2d(void)
{
    VVCDSPContext dsp;
    LOCAL_ALIGNED_32(int, c0, [MAX_TB_SIZE * MAX_TB_SIZE]);
    LOCAL_ALIGNED_32(int, c1, [MAX_TB_SIZE * MAX_TB_SIZE]);

    declare_func_emms(AV_CPU_FLAG_AVX2, void, int *coeffs,
        size_t nzw, size_t nzh, intptr_t log2_transform_range, intptr_t bd);

    for (int bd = 8; bd <= 12; bd += 2) {
        const intptr_t log2_transform_range = rnd() ? FFMAX(15, FFMIN(20, bd + 6)) : 15;
        ff_vvc_dsp_init(&dsp, bd);
        for (int w = 2; w <= 64; w <<= 1) {
            for (int h = 2; h <= 64; h <<= 1) {
                for (int trh = DCT2; trh <= tr_end(w); trh++) {
                    for (int trv = DCT2; trv <= tr_end(h); trv++) {
                        const size_t nzw = rnd() % zero_out_size(trh, w) + 1;
                        const size_t nzh = rnd() % zero_out_size(trv, h) + 1;
                        randomize_coeffs(c0, c1, w, h, nzw, nzh, log2_transform_range);
                        if (check_func(dsp.itx.itx[trh][trv][av_log2(w)][av_log2(h)],
                            "vvc_itx_%s_%s_%dx%d_%d", name(trh), name(trv), w, h, bd)) {
                            call_ref(c0, nzw, nzh, log2_transform_range, bd);
                            call_new(c1, nzw, nzh, log2_transform_range, bd);
                            for (int i = 0; i < h; i++) {
                                if (memcmp(c0 + i * w, c1 + i * w, w * sizeof(int)))
                                    fail();
                            }
                        }
                    }
                }

            }
        }
    }

}

void checkasm_check_vvc_itx(void)
{
    check_itx_2d();
}
