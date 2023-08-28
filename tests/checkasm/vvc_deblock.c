/*
 * Copyright (C) 2023 Shaun Loo
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

#include "libavcodec/vvc/vvcdsp.h"
#include "libavcodec/vvc/vvcdec.h"

#include "checkasm.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_STRIDE (8 * 2)
#define BUF_LINES (8)
#define BUF_OFFSET (BUF_STRIDE * BUF_LINES)
#define BUF_SIZE (BUF_STRIDE * BUF_LINES + BUF_OFFSET * 2)

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

static void check_deblock_chroma(VVCDSPContext *h, int bit_depth)
{
    // To-do: figure out values to test this properly
    int beta = 0;
    int32_t tc = 0;
    // no_p and no_q must be 0 for the simpler asm version
    uint8_t no_p = 0;
    uint8_t no_q = 0;
    // Shift == 0 for now, we're only writing the most simple
    // version
    int shift = 0;
    // Assembly version is only called (for now) if max_len_p != 3 && max_len_q != 3,
    // labelled as "weak." HEVC has a similar hack. max_len_p == 0 || max_len_q == 0
    // will trigger immediate exit, so 1 will have to do.
    int max_len_p = 1;
    int max_len_q = 1;

    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);

    declare_func_emms(AV_CPU_FLAG_MMX, void, uint8_t *pix, ptrdiff_t stride, int beta, int32_t tc,
                        int shift,uint8_t no_p, uint8_t no_q, int max_len_p, int max_len_q);
    if (check_func(h->lf.filter_chroma[0], "vvc_h_loop_filter_chroma_%d", bit_depth)) {
        for (int i = 0; i < 4; i++) {
            randomize_buffers(buf0, buf1, BUF_SIZE);
            // Largest betatable value is 88, see vvc_filter.c
            beta = (rnd() & 63) + (rnd() & 15) + (rnd() & 7) + (rnd() & 3);
            tc = (rnd() & 63) + (rnd() & 15) + (rnd() & 7) + (rnd() & 3);

            call_ref(buf0 + BUF_OFFSET, BUF_STRIDE, beta, tc, shift, no_p, no_q, max_len_p, max_len_q);
            call_new(buf1 + BUF_OFFSET, BUF_STRIDE, beta, tc, shift, no_p, no_q, max_len_p, max_len_q);
            if (memcmp(buf0, buf1, BUF_SIZE))
                fail();
        }
        bench_new(buf1 + BUF_OFFSET, BUF_STRIDE, beta, tc, no_p, no_q, shift, max_len_p, max_len_q);
    }

    if (check_func(h->lf.filter_chroma[1], "vvc_v_loop_filter_chroma_%d", bit_depth)) {
        int diff = 0;
        max_len_p = 2;
        max_len_q = 2;
        for (int i = 0; i < 4; i++) {
            randomize_buffers(buf0, buf1, BUF_SIZE);
            // Largest betatable value is 88, see vvc_filter.c
            beta = (rnd() & 63) + (rnd() & 15) + (rnd() & 7) + (rnd() & 3);
            tc = (rnd() & 63) + (rnd() & 15) + (rnd() & 7) + (rnd() & 3);

            call_ref(buf0 + BUF_OFFSET, BUF_STRIDE, beta, tc, shift, no_p, no_q, max_len_p, max_len_q);
            call_new(buf1 + BUF_OFFSET, BUF_STRIDE, beta, tc, shift, no_p, no_q, max_len_p, max_len_q);
            if (diff = (memcmp(buf0, buf1, BUF_SIZE))) {
                fail();
            }
                
        }
        bench_new(buf1 + BUF_OFFSET, BUF_STRIDE, beta, tc, no_p, no_q, shift, max_len_p, max_len_q);
    }
}

void checkasm_check_vvc_deblock(void)
{
    int bit_depth;

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        VVCDSPContext h;
        ff_vvc_dsp_init(&h, bit_depth);
        check_deblock_chroma(&h, bit_depth);
    }
    report("chroma");
}
