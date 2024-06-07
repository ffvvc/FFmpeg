/*
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

#include "checkasm.h"
#include "libavcodec/vvc/ctu.h"
#include "libavcodec/vvc/data.h"
#include "libavcodec/vvc/dsp.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"

static const uint32_t pixel_mask[3] = { 0xffffffff, 0x03ff03ff, 0x0fff0fff };

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE 16 * 16

#define randomize_buffers(buf0, buf1, size)                 \
    do {                                                    \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1];   \
        int k;                                              \
        for (k = 0; k < size; k += 2) {                     \
            uint32_t r = rnd() & mask;                      \
            AV_WN32A(buf0 + k, r);                          \
            AV_WN32A(buf1 + k, r);                          \
        }                                                   \
    } while (0)

static void check_deblock_chroma(VVCDSPContext *context, int bit_depth, int c)
{
    // see tctable[] in vvc_filter.c, we check full range

    int32_t tc[2] = {  10, 10 };
    uint8_t no_p[4] = {0, 0, 0, 0};
    uint8_t no_q[4] = {0, 0, 0, 0};
    uint8_t max_len_p[4] = {1, 1, 1, 1};
    uint8_t max_len_q[4] = {1, 1, 1, 1};
    int shift = 0;
    int beta = 10;

    LOCAL_ALIGNED_32(uint16_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint16_t, buf1, [BUF_SIZE]);



    declare_func(void, uint8_t *pix, ptrdiff_t stride, const int32_t *beta, const int32_t *tc,
        const uint8_t *no_p, const uint8_t *no_q, const uint8_t *max_len_p, const uint8_t *max_len_q, int shift);

    if (check_func(context->lf.filter_chroma_asm[0], "vvc_h_loop_filter_chroma%d", bit_depth))
    {
        randomize_buffers(buf0, buf1, BUF_SIZE);
   
        if(memcmp(buf0, buf1, BUF_SIZE))
            fail();

        context->lf.filter_chroma[0](buf1 + 8 * 5, 8 * 2, &beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
        context->lf.filter_chroma_asm[0](buf0 + 8 * 5, 8 * 2, &beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
        if (memcmp(buf0, buf1, BUF_SIZE))
            fail();
        bench_new(buf0 + 8, 8 * 5, &beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
    }

}

void checkasm_check_vvc_deblock(void)
{
    VVCDSPContext c;
    int bit_depth;

    bit_depth = 10;
        ff_vvc_dsp_init(&c, bit_depth);
        check_deblock_chroma(&c, bit_depth, 1);
    
    report("chroma_full");
}
