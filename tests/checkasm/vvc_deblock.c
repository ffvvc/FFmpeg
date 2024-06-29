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

static const uint32_t pixel_mask[3] = {0xffffffff, 0x03ff03ff, 0x0fff0fff};

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE 16 * 16

#define randomize_buffers(buf0, buf1, size)               \
    do                                                    \
    {                                                     \
        uint32_t mask = pixel_mask[(bit_depth - 8) >> 1]; \
        int k;                                            \
        for (k = 0; k < size; k += 4 / sizeof(*buf0))     \
        {                                                 \
            uint32_t r = rnd() & mask;                    \
            AV_WN32A(buf0 + k, r);                        \
            AV_WN32A(buf1 + k, r);                        \
        }                                                 \
    } while (0)

#define TC25(x) ((tc[x] * 5 + 1) >> 1)
#define MASK(x) (uint16_t)(x & ((1 << (bit_depth)) - 1))
#define GET(x) ((SIZEOF_PIXEL == 1) ? *(uint8_t *)(&x) : *(uint16_t *)(&x))
#define SET(x, y)                  \
    do                             \
    {                              \
        uint16_t z = MASK(y);      \
        if (SIZEOF_PIXEL == 1)     \
            *(uint8_t *)(&x) = z;  \
        else                       \
            *(uint16_t *)(&x) = z; \
    } while (0)
#define RANDCLIP(x, diff) av_clip(GET(x) - (diff), 0,       \
                                  (1 << (bit_depth)) - 1) + \
                              rnd() % FFMAX(2 * (diff), 1)

#define P3 buf[-4 * xstride]
#define P2 buf[-3 * xstride]
#define P1 buf[-2 * xstride]
#define P0 buf[-1 * xstride]
#define Q0 buf[0 * xstride]
#define Q1 buf[1 * xstride]
#define Q2 buf[2 * xstride]
#define Q3 buf[3 * xstride]

static void check_deblock_chroma_horizontal()
{   
    int32_t tc[4] = {(rnd() & 393) + 3, 600, (rnd() & 393) + 3, (rnd() & 393) + 3};

    uint8_t no_p[4] = {0, 0, 0, 0};
    uint8_t no_q[4] = {0, 0, 0, 0};
    uint8_t max_len_p[4] = {1, 1, 1, 1};
    uint8_t max_len_q[4] = {1, 3, 3, 1};
    int shift = 0;
    int beta[4] = {(rnd() & 81) + 8, (rnd() & 81) + 8, (rnd() & 81) + 8, (rnd() & 81) + 8 };

    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);

    VVCDSPContext context;
    int bit_depth;

    declare_func(void, uint8_t *pix, ptrdiff_t stride, const int32_t *beta, const int32_t *tc,
                 const uint8_t *no_p, const uint8_t *no_q, const uint8_t *max_len_p, const uint8_t *max_len_q, int shift);

    const int size = shift ? 4 : 2;
    const int end = 8 / size;

    for (bit_depth = 8; bit_depth <= 12; bit_depth += 2)
    {
        int xstride = SIZEOF_PIXEL * 8; // bytes
        int ystride = 2;                // bytes

        ff_vvc_dsp_init(&context, bit_depth);
        if (check_func(context.lf.filter_chroma[0], "vvc_h_loop_filter_chroma_%d", bit_depth))
        {
            int i, j, b3, tc25, tc25diff, b3diff;
            randomize_buffers(buf0, buf1, BUF_SIZE);
            uint8_t *buf = buf0 + xstride * 5;

            for (j = 0; j < size; j++)
            {
                tc25 = TC25(j) << (bit_depth - 10);

                tc25diff = FFMAX(tc25 - 1, 0);
                // 2 or 4 lines per tc
                for (i = 0; i < end; i++)
                {
                    b3 = (beta[j] << (bit_depth - 8)) >> 3;

                    SET(P0, rnd() % (1 << bit_depth));
                    SET(Q0, RANDCLIP(P0, tc25diff));

                    // p3 - p0 up to beta3 budget
                    b3diff = rnd() % b3;
                    SET(P3, RANDCLIP(P0, b3diff));
                    // q3 - q0, reduced budget
                    b3diff = rnd() % FFMAX(b3 - b3diff, 1);
                    SET(Q3, RANDCLIP(Q0, b3diff));

                    // same concept, budget across 4 pixels
                    b3 -= b3diff = rnd() % FFMAX(b3, 1);
                    SET(P2, RANDCLIP(P0, b3diff));
                    b3 -= b3diff = rnd() % FFMAX(b3, 1);
                    SET(Q2, RANDCLIP(Q0, b3diff));

                    // extra reduced budget for weighted pixels
                    b3 -= b3diff = rnd() % FFMAX(b3 - (1 << (bit_depth - 8)), 1);
                    SET(P1, RANDCLIP(P0, b3diff));
                    b3 -= b3diff = rnd() % FFMAX(b3 - (1 << (bit_depth - 8)), 1);
                    SET(Q1, RANDCLIP(Q0, b3diff));

                    buf += ystride;
                }
            }
            memcpy(buf0, buf1, BUF_SIZE);

            call_ref(buf0 + xstride * 5, xstride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
            call_new(buf1 + xstride * 5, xstride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);

            if (memcmp(buf0, buf1, BUF_SIZE))
                fail();

            bench_new(buf0 + xstride * 5, xstride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
        }
    }
    report("chroma");
}

void checkasm_check_vvc_deblock(void)
{
    check_deblock_chroma_horizontal();
}