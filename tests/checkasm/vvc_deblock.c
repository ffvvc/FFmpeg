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

#include "libavcodec/vvc/dsp.h"

#include "libavutil/common.h"
#include "libavutil/intreadwrite.h"
#include "libavutil/mem_internal.h"
#include "libavutil/macros.h"

#include "checkasm.h"

static const uint32_t pixel_mask[3] = {0xffffffff, 0x03ff03ff, 0x0fff0fff};

#define SIZEOF_PIXEL ((bit_depth + 7) / 8)
#define BUF_SIZE 16 * 16

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

#define P3 buf[-4 * xstride]
#define P2 buf[-3 * xstride]
#define P1 buf[-2 * xstride]
#define P0 buf[-1 * xstride]
#define Q0 buf[0 * xstride]
#define Q1 buf[1 * xstride]
#define Q2 buf[2 * xstride]
#define Q3 buf[3 * xstride]

#define TC_SHIFT(j) ((bit_depth < 10) ? ((tc[j] + (1 << (9 - bit_depth))) >> (10 - bit_depth)) : (tc[j] << (bit_depth - 10)))
#define TC25(x) ((TC_SHIFT(j) * 5 + 1) >> 1)
#define MASK(x) (uint16_t)(x & ((1 << (bit_depth)) - 1))
#define GET(x) ((SIZEOF_PIXEL == 1) ? *(uint8_t*)(&x) : *(uint16_t*)(&x))
#define SET(x, y) do { \
    uint16_t z = MASK(y); \
    if (SIZEOF_PIXEL == 1) \
        *(uint8_t*)(&x) = z; \
    else \
        *(uint16_t*)(&x) = z; \
} while (0)
#define RANDCLIP(x, diff) av_clip(GET(x) - (diff), 0, \
    (1 << (bit_depth)) - 1) + rnd() % FFMAX(2 * (diff), 1)

static void randomize_params(int32_t beta[4], int32_t tc[4], const int is_luma, const int bit_depth, const int size)
{
    const int tc_min   = 1 << FFMAX(0, 9 - bit_depth);
    const int beta_min = is_luma;                     // for luma, beta == 0 will disable the deblock, for chroma beta == 0 is a valid value
    for (int i = 0; i < size; i++) {
        beta[i] = FFMAX(rnd() % 89, beta_min); // minimum useful value is beta_min, full range [0, 89]
        tc[i] = FFMAX(rand() % 396, tc_min);   // minimum useful value is tc_min, full range [0, 395]
    }
}

static void randomize_chroma_buffers(const int type, const int shift, const int spatial_strong, const int bit_depth, 
uint8_t* max_len_p, uint8_t* max_len_q, int beta[4], int32_t tc[4], 
uint8_t *buf0, uint8_t *buf1, ptrdiff_t xstride, ptrdiff_t ystride)
{
    const int size = shift ? 4 : 2;
    const int end = 8 / size;

    randomize_params(beta, tc, 0, bit_depth, size);
    for(int i = 0; i < size; ++i) {
        switch (type) {
            case 0: // strong
                max_len_p[i] = 3;
                max_len_q[i] = 3;
                break;
            case 1: // strong one-side
                max_len_p[i] = 1;
                max_len_q[i] = 3;
                break;
            case 2: // weak
                max_len_p[i] = 1;
                max_len_q[i] = 1;
                break;
            case 3: // mix
                max_len_p[i] = 1 + (rnd() % 2) * 2;
                max_len_q[i] = 1 + (rnd() % 2) * 2;
                max_len_p[i] = FFMIN(max_len_p[i], max_len_q[i]);
                break;
        }
    }

    randomize_buffers(buf0, buf1, BUF_SIZE);

    uint8_t* buf = buf0 + xstride * 4;

    // NOTE: this function doesn't work 'correctly' in that it won't always generate
    // strong spatial activity. See hevc_deblock.c for details.
    if (spatial_strong) {
        for (int j = 0; j < size; j++) {
            const int tc25 = TC25(j);

            const int tc25diff = FFMAX(tc25 - 1, 0);
            // 2 or 4 lines per tc
            for (int i = 0; i < end; i++)
            {
                int b3diff;
                beta[j] = FFMAX(beta[j], 8); // for bit_depth = 8, minimum useful value is 8
                int b3 = (beta[j] << (bit_depth - 8)) >> 3;

                SET(P0, rnd() % (1 << bit_depth));
                SET(Q0, RANDCLIP(P0, tc25diff));

                // p3 - p0 up to beta3 budget
                b3diff = rnd() % FFMAX(b3, 1);
                if(max_len_p[j] == 1) {
                    SET(P1, RANDCLIP(P0, b3diff));
                } else {
                    SET(P3, RANDCLIP(P0, b3diff));
                }

                // q3 - q0, reduced budget
                b3diff = rnd() % FFMAX(b3 - b3diff, 1);
                SET(Q3, RANDCLIP(Q0, b3diff));

                // same concept, budget across 4 pixels
                b3 -= b3diff = rnd() % FFMAX(b3, 1);
                if(max_len_p[j] == 1) {
                    SET(P1, RANDCLIP(P0, b3diff));
                } else {
                    SET(P2, RANDCLIP(P0, b3diff));
                }
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
        memcpy(buf1, buf0, BUF_SIZE);
    }
}

static void check_deblock_chroma(const VVCDSPContext *h, int bit_depth)
{
    const char *types[] = { "strong", "one-side", "weak", "mix" };
    const char *shifts[] = { "no-shift", "shift" };

    int32_t beta[4], tc[4];
    uint8_t no_p[4] = {0, 0, 0, 0};
    uint8_t no_q[4] = {0, 0, 0, 0};
    uint8_t max_len_p[4];
    uint8_t max_len_q[4];
    int xstride = 16;            // bytes
    int ystride = SIZEOF_PIXEL;  // bytes

    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);

    declare_func(void, uint8_t *pix, ptrdiff_t stride, const int32_t *beta, const int32_t *tc,
                 const uint8_t *no_p, const uint8_t *no_q, const uint8_t *max_len_p, const uint8_t *max_len_q, int shift);

    for(int type = 0; type < sizeof(types)/sizeof(char*); ++type) {
        for(int shift = 0; shift < 2; ++shift) {
            if (check_func(h->lf.filter_chroma[0], "vvc_h_loop_filter_chroma_%d_%s_%s", bit_depth, types[type], shifts[shift])) {
                for(int spatial_strong = 0; spatial_strong < 1; ++spatial_strong) {
                    randomize_chroma_buffers(type, shift, spatial_strong, bit_depth, max_len_p, max_len_q, beta, tc, buf0, buf1, xstride, ystride);
                    call_ref(buf0 + xstride * 4, xstride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
                    call_new(buf1 + xstride * 4, xstride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);

                    if (memcmp(buf0, buf1, BUF_SIZE))
                        fail();
                }
                randomize_chroma_buffers(bit_depth, type, shift, 1, max_len_p, max_len_q, beta, tc, buf0, buf1, xstride, ystride);
                bench_new(buf0 + xstride * 4, xstride, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
            }
        }
    }
}

void checkasm_check_vvc_deblock(void)
{
    VVCDSPContext h;

    for (int bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vvc_dsp_init(&h, bit_depth);
        check_deblock_chroma(&h, bit_depth);
    }
    report("chroma");
}
