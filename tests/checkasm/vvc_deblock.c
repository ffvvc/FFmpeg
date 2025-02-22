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
#define BUF_STRIDE (16 * 2)
#define BUF_LINES (16)
// large buffer sizes based on high bit depth
#define BUF_OFFSET (2 * BUF_STRIDE * BUF_LINES)
#define BUF_SIZE (2 * BUF_STRIDE * BUF_LINES + BUF_OFFSET * 2)

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
#define RANDCLIP(x, diff) av_clip(GET(x) - (diff)  + rnd() % FFMAX(2 * (diff), 1), \
0, (1 << (bit_depth)) - 1)

static void randomize_params(int32_t *beta, int32_t *tc, const int is_luma, const int bit_depth, const int size)
{
    const int tc_min   = 1 << FFMAX(0, 9 - bit_depth);
    //const int beta_min = is_luma;                     // for luma, beta == 0 will disable the deblock, for chroma beta == 0 is a valid value
    const int beta_min = 1;                             // chroma weak generator still has bug, let us use 1 as min
    for (int i = 0; i < size; i++) {
        beta[i] = FFMAX(rnd() % 89, beta_min); // minimum useful value is beta_min, full range [0, 89]
        tc[i] = FFMAX(rand() % 396, tc_min);   // minimum useful value is tc_min, full range [0, 395]
    }
}

// NOTE: this function doesn't work 'correctly' in that it won't always choose
// strong/strong or weak/weak, in most cases it tends to but will sometimes mix
// weak/strong or even skip sometimes. This is more useful to test correctness
// for these functions, though it does make benching them difficult. The easiest
// way to bench these functions is to check an overall decode since there are too
// many paths and ways to trigger the deblock: we would have to bench all
// permutations of weak/strong/skip/nd_q/nd_p/no_q/no_p and it quickly becomes
// too much.
static void randomize_luma_buffers(int type, int32_t beta[2], int32_t tc[2], uint8_t max_len_p[2], uint8_t max_len_q[2],
   uint8_t *buf, ptrdiff_t xstride, ptrdiff_t ystride, int bit_depth)
{
    int i, j, b3, b3diff;

    randomize_params(beta, tc, 1, bit_depth, 2);

    switch (type) {
    case 0: // strong
        for (j = 0; j < 2; j++) {
            const int tc25     = TC25(j);
            const int tc25diff = FFMAX(tc25 - 1, 0);
            max_len_p[j] = max_len_q[j] = 3;
            // 4 lines per tc
            for (i = 0; i < 4; i++) {
                b3 = (*beta << (bit_depth - 8)) >> 3;

                SET(P0, rnd() % (1 << bit_depth));
                SET(Q0, RANDCLIP(P0, tc25diff));

                // p3 - p0 up to beta3 budget
                b3diff = rnd() % FFMAX(b3, 1);
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
        break;
    case 1: // weak
        for (j = 0; j < 2; j++) {
            const int tc25     = TC25(j);
            const int tc25diff = FFMAX(tc25 - 1, 0);
            max_len_p[j] = max_len_q[j]= 2;
            // 4 lines per tc
            for (i = 0; i < 4; i++) {
                // Weak filtering is signficantly simpler to activate as
                // we only need to satisfy d0 + d3 < beta, which
                // can be simplified to d0 + d0 < beta. Using the above
                // derivations but substiuting b3 for b1 and ensuring
                // that P0/Q0 are at least 1/2 tc25diff apart (tending
                // towards 1/2 range).
                b3 = (beta[j] << (bit_depth - 8)) >> 1;

                SET(P0, rnd() % (1 << bit_depth));
                SET(Q0, RANDCLIP(P0, tc25diff >> 1) +
                    (tc25diff >> 1) * (P0 < (1 << (bit_depth - 1))) ? 1 : -1);

                // p3 - p0 up to beta3 budget
                b3diff = rnd() % FFMAX(b3, 1);
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
        break;
    case 2: // none
        beta[0] = beta[1] = 0; // ensure skip
        for (i = 0; i < 8; i++) {
            // we can just fill with completely random data, nothing should be touched.
            SET(P3, rnd()); SET(P2, rnd()); SET(P1, rnd()); SET(P0, rnd());
            SET(Q0, rnd()); SET(Q1, rnd()); SET(Q2, rnd()); SET(Q3, rnd());
            buf += ystride;
        }
        break;
    }
}

static void check_deblock_luma(VVCDSPContext *h, int bit_depth)
{
    const char *type;
    const char *types[3] = { "strong", "weak", "skip" };
    int32_t beta[2] = {0};
    int32_t tc[2] = {0};
    uint8_t no_p[2] = {0, 0};
    uint8_t no_q[2] = {0, 0};
    uint8_t max_len_q[2] = {1, 1};
    uint8_t max_len_p[2] = {1, 1};
    const int hor_ctu_edge = 0;
    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    uint8_t *ptr0 = buf0 + BUF_OFFSET,
            *ptr1 = buf1 + BUF_OFFSET;

    declare_func(void, uint8_t *pix, ptrdiff_t stride, const int32_t *beta, const int32_t *tc,
        const uint8_t *no_p, const uint8_t *no_q, const uint8_t *max_len_p, const uint8_t *max_len_q, int hor_ctu_edge);
    memset(buf0, 0, BUF_SIZE);

    for (int vertical = 0; vertical <= 1; vertical++) {
        ptrdiff_t xstride = 16 * SIZEOF_PIXEL;
        ptrdiff_t ystride = SIZEOF_PIXEL;

        if (vertical)
            FFSWAP(ptrdiff_t, xstride, ystride);

        for (int j = 0; j < 3; j++) {
            type = types[j];

            if (check_func(h->lf.filter_luma[vertical],"vvc_%s_loop_filter_luma_%d_%s", vertical ? "v" : "h", bit_depth, type))
            {
                randomize_luma_buffers(j, beta, tc, max_len_p, max_len_q, buf0 + BUF_OFFSET, xstride, ystride, bit_depth);
                memcpy(buf1, buf0, BUF_SIZE);

                call_ref(ptr0, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q, max_len_p, max_len_q, hor_ctu_edge);
                call_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q, max_len_p, max_len_q, hor_ctu_edge);
                if (memcmp(buf0, buf1, BUF_SIZE))
                    fail();
                bench_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q, max_len_p, max_len_q, hor_ctu_edge);
            }
        }
    }
}

static void randomize_chroma_buffers(const int type, const int shift, const int bit_depth,
    uint8_t* max_len_p, uint8_t* max_len_q, int beta[4], int32_t tc[4],
    uint8_t *buf, ptrdiff_t xstride, ptrdiff_t ystride)
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

    for (int j = 0; j < size; j++) {
        const int tc25 = TC25(j);

        const int tc25diff = FFMAX(tc25 - 1, 0);
        // 2 or 4 lines per tc
        for (int i = 0; i < end; i++)
        {
            int b3diff, b3;
            beta[j] = FFMAX(beta[j], 8); // for bit_depth = 8, minimum useful value is 8
            b3 = (beta[j] << (bit_depth - 8)) >> 3;

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

    LOCAL_ALIGNED_32(uint8_t, buf0, [BUF_SIZE]);
    LOCAL_ALIGNED_32(uint8_t, buf1, [BUF_SIZE]);
    uint8_t *ptr0 = buf0 + BUF_OFFSET,
            *ptr1 = buf1 + BUF_OFFSET;

    declare_func(void, uint8_t *pix, ptrdiff_t stride, const int32_t *beta, const int32_t *tc,
                 const uint8_t *no_p, const uint8_t *no_q, const uint8_t *max_len_p, const uint8_t *max_len_q, int shift);

    for (int vertical = 0; vertical <= 1; vertical++) {
        ptrdiff_t xstride = 16 * SIZEOF_PIXEL;
        ptrdiff_t ystride = SIZEOF_PIXEL;

        if (vertical)
            FFSWAP(ptrdiff_t, xstride, ystride);

        for(int type = 0; type < FF_ARRAY_ELEMS(types); ++type) {
            for(int shift = 0; shift < FF_ARRAY_ELEMS(shifts); ++shift) {
                if (check_func(h->lf.filter_chroma[vertical], "vvc_%s_loop_filter_chroma_%d_%s_%s", vertical ? "v" : "h", bit_depth, types[type], shifts[shift])) {
                    randomize_chroma_buffers(type, shift, bit_depth, max_len_p, max_len_q, beta, tc, ptr0, xstride, ystride);
                    memcpy(buf1, buf0, BUF_SIZE);
                    call_ref(ptr0, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
                    call_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
                    if (memcmp(buf0, buf1, BUF_SIZE))
                        fail();
                    bench_new(ptr1, 16 * SIZEOF_PIXEL, beta, tc, no_p, no_q, max_len_p, max_len_q, shift);
                }
            }
        }
    }
}

void checkasm_check_vvc_deblock(void)
{
    VVCDSPContext h;

    for (int bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vvc_dsp_init(&h, bit_depth);
        check_deblock_luma(&h, bit_depth);
    }
    report("luma");

    for (int bit_depth = 8; bit_depth <= 12; bit_depth += 2) {
        ff_vvc_dsp_init(&h, bit_depth);
        check_deblock_chroma(&h, bit_depth);
    }
    report("chroma");
}
