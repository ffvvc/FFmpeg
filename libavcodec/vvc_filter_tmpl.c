/*
 * VVC LMCS DSP
 *
 * Copyright (C) 2022 Nuo Mi
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */
#include <stddef.h>

static void bitfn(lmcs_filter_luma)(uint8_t *_dst, ptrdiff_t _dst_stride,
    const int width, const int height, const uint8_t *_lut)
{
    const pixel *lut = (const pixel *)_lut;
    const int dst_stride = _dst_stride / sizeof(pixel);
    pixel *dst = (pixel*)_dst;

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = lut[dst[x]];
        dst += dst_stride;
    }
}

// line zero
#define P7 pix[-8 * xstride]
#define P6 pix[-7 * xstride]
#define P5 pix[-6 * xstride]
#define P4 pix[-5 * xstride]
#define P3 pix[-4 * xstride]
#define P2 pix[-3 * xstride]
#define P1 pix[-2 * xstride]
#define P0 pix[-1 * xstride]
#define Q0 pix[0 * xstride]
#define Q1 pix[1 * xstride]
#define Q2 pix[2 * xstride]
#define Q3 pix[3 * xstride]
#define Q4 pix[4 * xstride]
#define Q5 pix[5 * xstride]
#define Q6 pix[6 * xstride]
#define Q7 pix[7 * xstride]
#define P(x) pix[(-(x)-1) * xstride]
#define Q(x) pix[(x)      * xstride]

// line three. used only for deblocking decision
#define TP7 pix[-8 * xstride + 3 * ystride]
#define TP6 pix[-7 * xstride + 3 * ystride]
#define TP5 pix[-6 * xstride + 3 * ystride]
#define TP4 pix[-5 * xstride + 3 * ystride]
#define TP3 pix[-4 * xstride + 3 * ystride]
#define TP2 pix[-3 * xstride + 3 * ystride]
#define TP1 pix[-2 * xstride + 3 * ystride]
#define TP0 pix[-1 * xstride + 3 * ystride]
#define TQ0 pix[0  * xstride + 3 * ystride]
#define TQ1 pix[1  * xstride + 3 * ystride]
#define TQ2 pix[2  * xstride + 3 * ystride]
#define TQ3 pix[3  * xstride + 3 * ystride]
#define TQ4 pix[4  * xstride + 3 * ystride]
#define TQ5 pix[5  * xstride + 3 * ystride]
#define TQ6 pix[6  * xstride + 3 * ystride]
#define TQ7 pix[7  * xstride + 3 * ystride]
#define TP(x) pix[(-(x)-1) * xstride + 3 * ystride]
#define TQ(x) pix[(x)      * xstride + 3 * ystride]

#define FP3 pix[-4 * xstride + 1 * ystride]
#define FP2 pix[-3 * xstride + 1 * ystride]
#define FP1 pix[-2 * xstride + 1 * ystride]
#define FP0 pix[-1 * xstride + 1 * ystride]
#define FQ0 pix[0  * xstride + 1 * ystride]
#define FQ1 pix[1  * xstride + 1 * ystride]
#define FQ2 pix[2  * xstride + 1 * ystride]
#define FQ3 pix[3  * xstride + 1 * ystride]

static void bitfn(vvc_loop_filter_luma)(uint8_t *_pix, ptrdiff_t _xstride, ptrdiff_t _ystride,
    int _beta, int _tc, uint8_t _no_p, uint8_t _no_q,
    uint8_t max_len_p, uint8_t max_len_q, int hor_ctu_edge HIGHBD_DECL_SUFFIX)
{
    int d;
    pixel* pix = (pixel*)_pix;
    ptrdiff_t xstride = _xstride / sizeof(pixel);
    ptrdiff_t ystride = _ystride / sizeof(pixel);

    const int dp0 = abs(P2 - 2 * P1 + P0);
    const int dq0 = abs(Q2 - 2 * Q1 + Q0);
    const int dp3 = abs(TP2 - 2 * TP1 + TP0);
    const int dq3 = abs(TQ2 - 2 * TQ1 + TQ0);
    const int d0 = dp0 + dq0;
    const int d3 = dp3 + dq3;
    const int no_p = _no_p;
    const int no_q = _no_q;

    const int large_p = (max_len_p > 3 && !hor_ctu_edge);
    const int large_q = max_len_q > 3;
    const int bit_depth = get_bit_depth();
    const int tc = bit_depth < 10 ? ((_tc + (1 << (9 - bit_depth))) >> (10 - bit_depth)) : (_tc << (bit_depth - 10));
    const int tc25 = ((tc * 5 + 1) >> 1);

    const int beta = _beta << bit_depth - 8;
    const int beta_3 = beta >> 3;
    const int beta_2 = beta >> 2;

    if (large_p || large_q) {
        const int dp0l = large_p ? ((dp0 + abs(P5 - 2 * P4 + P3) + 1) >> 1) : dp0;
        const int dq0l = large_q ? ((dq0 + abs(Q5 - 2 * Q4 + Q3) + 1) >> 1) : dq0;
        const int dp3l = large_p ? ((dp3 + abs(TP5 - 2 * TP4 + TP3) + 1) >> 1) : dp3;
        const int dq3l = large_q ? ((dq3 + abs(TQ5 - 2 * TQ4 + TQ3) + 1) >> 1) : dq3;
        const int d0l = dp0l + dq0l;
        const int d3l = dp3l + dq3l;
        const int beta53 = beta * 3 >> 5;
        const int beta_4 = beta >> 4;
        max_len_p = large_p ? max_len_p : 3;
        max_len_q = large_q ? max_len_q : 3;

        if (d0l + d3l < beta) {
            const int sp0l = abs(P3 - P0) + (max_len_p == 7 ? abs(P7 - P6 - P5 + P4) : 0);
            const int sq0l = abs(Q0 - Q3) + (max_len_q == 7 ? abs(Q4 - Q5 - Q6 + Q7) : 0);
            const int sp3l = abs(TP3 - TP0) + (max_len_p == 7 ? abs(TP7 - TP6 - TP5 + TP4) : 0);
            const int sq3l = abs(TQ0 - TQ3) + (max_len_q == 7 ? abs(TQ4 - TQ5 - TQ6 + TQ7) : 0);
            const int sp0 = large_p ? ((sp0l + abs(P3 -   P(max_len_p)) + 1) >> 1) : sp0l;
            const int sp3 = large_p ? ((sp3l + abs(TP3 - TP(max_len_p)) + 1) >> 1) : sp3l;
            const int sq0 = large_q ? ((sq0l + abs(Q3 -   Q(max_len_q)) + 1) >> 1) : sq0l;
            const int sq3 = large_q ? ((sq3l + abs(TQ3 - TQ(max_len_q)) + 1) >> 1) : sq3l;
            if (sp0 + sq0 < beta53 && abs(P0 - Q0) < tc25 &&
                sp3 + sq3 < beta53 && abs(TP0 - TQ0) < tc25 &&
                (d0l << 1) < beta_4 && (d3l << 1) < beta_4) {
                for (d = 0; d < 4; d++) {
                    const int p6 = P6;
                    const int p5 = P5;
                    const int p4 = P4;
                    const int p3 = P3;
                    const int p2 = P2;
                    const int p1 = P1;
                    const int p0 = P0;
                    const int q0 = Q0;
                    const int q1 = Q1;
                    const int q2 = Q2;
                    const int q3 = Q3;
                    const int q4 = Q4;
                    const int q5 = Q5;
                    const int q6 = Q6;
                    int m;
                    if (max_len_p == 5 && max_len_q == 5)
                        m = (p4 + p3 + 2 * (p2 + p1 + p0 + q0 + q1 + q2) + q3 + q4 + 8) >> 4;
                    else if (max_len_p == max_len_q)
                        m = (p6 + p5 + p4 + p3 + p2 + p1 + 2 * (p0 + q0) + q1 + q2 + q3 + q4 + q5 + q6 + 8) >> 4;
                    else if (max_len_p + max_len_q == 12)
                        m = (p5 + p4 + p3 + p2 + 2 * (p1 + p0 + q0 + q1) + q2 + q3 + q4 + q5 + 8) >> 4;
                    else if (max_len_p + max_len_q == 8)
                        m = (p3 + p2 + p1 + p0 + q0 + q1 + q2 + q3 + 4) >> 3;
                    else if (max_len_q == 7)
                        m = (2 * (p2 + p1 + p0 + q0) + p0 + p1 + q1 + q2 + q3 + q4 + q5 + q6 + 8) >> 4;
                    else
                        m = (p6 + p5 + p4 + p3 + p2 + p1 + 2 * (q2 + q1 + q0 + p0) + q0 + q1 + 8) >> 4;
                    if (!no_p) {
                        const int refp = (P(max_len_p) + P(max_len_p - 1) + 1) >> 1;
                        if (max_len_p == 3) {
                            P0 = p0 + av_clip(((m * 53 + refp * 11 + 32) >> 6) - p0, -(tc * 6 >> 1), (tc * 6 >> 1));
                            P1 = p1 + av_clip(((m * 32 + refp * 32 + 32) >> 6) - p1, -(tc * 4 >> 1), (tc * 4 >> 1));
                            P2 = p2 + av_clip(((m * 11 + refp * 53 + 32) >> 6) - p2, -(tc * 2 >> 1), (tc * 2 >> 1));
                        } else if (max_len_p == 5) {
                            P0 = p0 + av_clip(((m * 58 + refp *  6 + 32) >> 6) - p0, -(tc * 6 >> 1), (tc * 6 >> 1));
                            P1 = p1 + av_clip(((m * 45 + refp * 19 + 32) >> 6) - p1, -(tc * 5 >> 1), (tc * 5 >> 1));
                            P2 = p2 + av_clip(((m * 32 + refp * 32 + 32) >> 6) - p2, -(tc * 4 >> 1), (tc * 4 >> 1));
                            P3 = p3 + av_clip(((m * 19 + refp * 45 + 32) >> 6) - p3, -(tc * 3 >> 1), (tc * 3 >> 1));
                            P4 = p4 + av_clip(((m *  6 + refp * 58 + 32) >> 6) - p4, -(tc * 2 >> 1), (tc * 2 >> 1));
                        } else {
                            P0 = p0 + av_clip(((m * 59 + refp *  5 + 32) >> 6) - p0, -(tc * 6 >> 1), (tc * 6 >> 1));
                            P1 = p1 + av_clip(((m * 50 + refp * 14 + 32) >> 6) - p1, -(tc * 5 >> 1), (tc * 5 >> 1));
                            P2 = p2 + av_clip(((m * 41 + refp * 23 + 32) >> 6) - p2, -(tc * 4 >> 1), (tc * 4 >> 1));
                            P3 = p3 + av_clip(((m * 32 + refp * 32 + 32) >> 6) - p3, -(tc * 3 >> 1), (tc * 3 >> 1));
                            P4 = p4 + av_clip(((m * 23 + refp * 41 + 32) >> 6) - p4, -(tc * 2 >> 1), (tc * 2 >> 1));
                            P5 = p5 + av_clip(((m * 14 + refp * 50 + 32) >> 6) - p5, -(tc * 1 >> 1), (tc * 1 >> 1));
                            P6 = p6 + av_clip(((m *  5 + refp * 59 + 32) >> 6) - p6, -(tc * 1 >> 1), (tc * 1 >> 1));
                        }
                    }
                    if (!no_q) {
                        const int refq = (Q(max_len_q) + Q(max_len_q - 1) + 1) >> 1;
                        if (max_len_q == 3) {
                            Q0 = q0 + av_clip(((m * 53 + refq * 11 + 32) >> 6) - q0,  -(tc * 6 >> 1), (tc * 6 >> 1));
                            Q1 = q1 + av_clip(((m * 32 + refq * 32 + 32) >> 6) - q1,  -(tc * 4 >> 1), (tc * 4 >> 1));
                            Q2 = q2 + av_clip(((m * 11 + refq * 53 + 32) >> 6) - q2,  -(tc * 2 >> 1), (tc * 2 >> 1));
                        } else if (max_len_q == 5) {
                            Q0 = q0 + av_clip(((m * 58 + refq *  6 + 32) >> 6) - q0, -(tc * 6 >> 1), (tc * 6 >> 1));
                            Q1 = q1 + av_clip(((m * 45 + refq * 19 + 32) >> 6) - q1, -(tc * 5 >> 1), (tc * 5 >> 1));
                            Q2 = q2 + av_clip(((m * 32 + refq * 32 + 32) >> 6) - q2, -(tc * 4 >> 1), (tc * 4 >> 1));
                            Q3 = q3 + av_clip(((m * 19 + refq * 45 + 32) >> 6) - q3, -(tc * 3 >> 1), (tc * 3 >> 1));
                            Q4 = q4 + av_clip(((m *  6 + refq * 58 + 32) >> 6) - q4, -(tc * 2 >> 1), (tc * 2 >> 1));
                        } else {
                            Q0 = q0 + av_clip(((m * 59 + refq *  5 + 32) >> 6) - q0, -(tc * 6 >> 1), (tc * 6 >> 1));
                            Q1 = q1 + av_clip(((m * 50 + refq * 14 + 32) >> 6) - q1, -(tc * 5 >> 1), (tc * 5 >> 1));
                            Q2 = q2 + av_clip(((m * 41 + refq * 23 + 32) >> 6) - q2, -(tc * 4 >> 1), (tc * 4 >> 1));
                            Q3 = q3 + av_clip(((m * 32 + refq * 32 + 32) >> 6) - q3, -(tc * 3 >> 1), (tc * 3 >> 1));
                            Q4 = q4 + av_clip(((m * 23 + refq * 41 + 32) >> 6) - q4, -(tc * 2 >> 1), (tc * 2 >> 1));
                            Q5 = q5 + av_clip(((m * 14 + refq * 50 + 32) >> 6) - q5, -(tc * 1 >> 1), (tc * 1 >> 1));
                            Q6 = q6 + av_clip(((m *  5 + refq * 59 + 32) >> 6) - q6, -(tc * 1 >> 1), (tc * 1 >> 1));
                        }

                    }

                    pix += ystride;
                }
                return;

            }
        }
    }
    if (d0 + d3 < beta) {
        if (max_len_p > 2 && max_len_q > 2 &&
            abs(P3 - P0) + abs(Q3 - Q0) < beta_3 && abs(P0 - Q0) < tc25 &&
            abs(TP3 - TP0) + abs(TQ3 - TQ0) < beta_3 && abs(TP0 - TQ0) < tc25 &&
            (d0 << 1) < beta_2 && (d3 << 1) < beta_2) {
            // strong filtering
            const int tc2 = tc << 1;
            const int tc3 = tc * 3;
            for (d = 0; d < 4; d++) {
                const int p3 = P3;
                const int p2 = P2;
                const int p1 = P1;
                const int p0 = P0;
                const int q0 = Q0;
                const int q1 = Q1;
                const int q2 = Q2;
                const int q3 = Q3;
                if (!no_p) {
                    P0 = p0 + av_clip(((p2 + 2 * p1 + 2 * p0 + 2 * q0 + q1 + 4) >> 3) - p0, -tc3, tc3);
                    P1 = p1 + av_clip(((p2 + p1 + p0 + q0 + 2) >> 2) - p1, -tc2, tc2);
                    P2 = p2 + av_clip(((2 * p3 + 3 * p2 + p1 + p0 + q0 + 4) >> 3) - p2, -tc, tc);
                }
                if (!no_q) {
                    Q0 = q0 + av_clip(((p1 + 2 * p0 + 2 * q0 + 2 * q1 + q2 + 4) >> 3) - q0, -tc3, tc3);
                    Q1 = q1 + av_clip(((p0 + q0 + q1 + q2 + 2) >> 2) - q1, -tc2, tc2);
                    Q2 = q2 + av_clip(((2 * q3 + 3 * q2 + q1 + q0 + p0 + 4) >> 3) - q2, -tc, tc);
                }
                pix += ystride;
            }
        } else { // weak filtering
            int nd_p = 1;
            int nd_q = 1;
            const int tc_2 = tc >> 1;
            if (max_len_p > 1 && max_len_q > 1) {
                if (dp0 + dp3 < ((beta + (beta >> 1)) >> 3))
                    nd_p = 2;
                if (dq0 + dq3 < ((beta + (beta >> 1)) >> 3))
                    nd_q = 2;
            }

            for (d = 0; d < 4; d++) {
                const int p2 = P2;
                const int p1 = P1;
                const int p0 = P0;
                const int q0 = Q0;
                const int q1 = Q1;
                const int q2 = Q2;
                int delta0 = (9 * (q0 - p0) - 3 * (q1 - p1) + 8) >> 4;
                if (abs(delta0) < 10 * tc) {
                    delta0 = av_clip(delta0, -tc, tc);
                    if (!no_p)
                        P0 = clip_pixel(p0 + delta0);
                    if (!no_q)
                        Q0 = clip_pixel(q0 - delta0);
                    if (!no_p && nd_p > 1) {
                        const int deltap1 = av_clip((((p2 + p0 + 1) >> 1) - p1 + delta0) >> 1, -tc_2, tc_2);
                        P1 = clip_pixel(p1 + deltap1);
                    }
                    if (!no_q && nd_q > 1) {
                        const int deltaq1 = av_clip((((q2 + q0 + 1) >> 1) - q1 - delta0) >> 1, -tc_2, tc_2);
                        Q1 = clip_pixel(q1 + deltaq1);
                    }
                }
                pix += ystride;
            }
        }
    }
}

static void bitfn(vvc_loop_filter_chroma)(uint8_t *_pix, const ptrdiff_t  _xstride,
    const ptrdiff_t _ystride, const int _beta, const int _tc, const uint8_t no_p, const uint8_t no_q,
    const int shift,  int max_len_p, int max_len_q HIGHBD_DECL_SUFFIX)
{
    pixel *pix        = (pixel *)_pix;
    const ptrdiff_t xstride = _xstride / sizeof(pixel);
    const ptrdiff_t ystride = _ystride / sizeof(pixel);
    const int end  = shift ? 2 : 4;

    const int bit_depth = get_bit_depth();
    const int tc = bit_depth < 10 ? ((_tc + (1 << (9 - bit_depth))) >> (10 - bit_depth)) : (_tc << (bit_depth - 10));
    const int tc25 = ((tc * 5 + 1) >> 1);

    const int beta = _beta << bit_depth - 8;
    const int beta_3 = beta >> 3;
    const int beta_2 = beta >> 2;

    if (!max_len_p || !max_len_q)
        return;

    if (max_len_q == 3){
        const int p1n  = shift ? FP1 : TP1;
        const int p2n = max_len_p == 1 ? p1n : (shift ? FP2 : TP2);
        const int p0n  = shift ? FP0 : TP0;
        const int q0n  = shift ? FQ0 : TQ0;
        const int q1n  = shift ? FQ1 : TQ1;
        const int q2n  = shift ? FQ2 : TQ2;
        const int p3   = max_len_p == 1 ? P1 : P3;
        const int p2   = max_len_p == 1 ? P1 : P2;
        const int p1   = P1;
        const int p0   = P0;
        const int dp0  = abs(p2 - 2 * p1 + p0);
        const int dq0  = abs(Q2 - 2 * Q1 + Q0);

        const int dp1 = abs(p2n - 2 * p1n + p0n);
        const int dq1 = abs(q2n - 2 * q1n + q0n);
        const int d0  = dp0 + dq0;
        const int d1  = dp1 + dq1;

        if (d0 + d1 < beta) {
            const int p3n = max_len_p == 1 ? p1n : (shift ? FP3 : TP3);
            const int q3n = shift ? FQ3 : TQ3;
            const int dsam0 = (d0 << 1) < beta_2 && (abs(p3 - p0) + abs(Q0 - Q3)     < beta_3) &&
                abs(p0 - Q0)   < tc25;
            const int dsam1 = (d1 << 1) < beta_2 && (abs(p3n - p0n) + abs(q0n - q3n) < beta_3) &&
                abs(p0n - q0n) < tc25;
            if (!dsam0 || !dsam1)
                max_len_p = max_len_q = 1;
        } else {
            max_len_p = max_len_q = 1;
        }
    }

    if (max_len_p == 3 && max_len_q == 3) {
        //strong
        for (int d = 0; d < end; d++) {
            const int p3 = P3;
            const int p2 = P2;
            const int p1 = P1;
            const int p0 = P0;
            const int q0 = Q0;
            const int q1 = Q1;
            const int q2 = Q2;
            const int q3 = Q3;
            if (!no_p) {
                P0 = av_clip((p3 + p2 + p1 + 2 * p0 + q0 + q1 + q2 + 4) >> 3, p0 - tc, p0 + tc);
                P1 = av_clip((2 * p3 + p2 + 2 * p1 + p0 + q0 + q1 + 4) >> 3, p1 - tc, p1 + tc);
                P2 = av_clip((3 * p3 + 2 * p2 + p1 + p0 + q0 + 4) >> 3, p2 - tc, p2 + tc );
            }
            if (!no_q) {
                Q0 = av_clip((p2 + p1 + p0 + 2 * q0 + q1 + q2 + q3 + 4) >> 3, q0 - tc, q0 + tc);
                Q1 = av_clip((p1 + p0 + q0 + 2 * q1 + q2 + 2 * q3 + 4) >> 3, q1 - tc, q1 + tc);
                Q2 = av_clip((p0 + q0 + q1 + 2 * q2 + 3 * q3 + 4) >> 3, q2 - tc, q2 + tc);
            }
            pix += ystride;
        }
    } else if (max_len_q == 3) {
        for (int d = 0; d < end; d++) {
            const int p1 = P1;
            const int p0 = P0;
            const int q0 = Q0;
            const int q1 = Q1;
            const int q2 = Q2;
            const int q3 = Q3;
            if (!no_p) {
                P0 = av_clip((3 * p1 + 2 * p0 + q0 + q1 + q2 + 4) >> 3, p0 - tc, p0 + tc);
            }
            if (!no_q) {
                Q0 = av_clip((2 * p1 + p0 + 2 * q0 + q1 + q2 + q3 + 4) >> 3, q0 - tc, q0 + tc);
                Q1 = av_clip((p1 + p0 + q0 + 2 * q1 + q2 + 2 * q3 + 4) >> 3, q1 - tc, q1 + tc);
                Q2 = av_clip((p0 + q0 + q1 + 2 * q2 + 3 * q3 + 4) >> 3, q2 - tc, q2 + tc);
            }
            pix += ystride;
        }
    } else {
        //weak
        for (int d = 0; d < end; d++) {
            int delta0;
            const int p1 = P1;
            const int p0 = P0;
            const int q0 = Q0;
            const int q1 = Q1;
            delta0 = av_clip((((q0 - p0) * 4) + p1 - q1 + 4) >> 3, -tc, tc);
            if (!no_p)
                P0 = clip_pixel(p0 + delta0);
            if (!no_q)
                Q0 = clip_pixel(q0 - delta0);
            pix += ystride;
        }
    }
}

static int bitfn(vvc_loop_ladf_level)(const uint8_t *_pix, const ptrdiff_t _xstride, const ptrdiff_t _ystride)
{
    const pixel *pix        = (pixel *)_pix;
    const ptrdiff_t xstride = _xstride / sizeof(pixel);
    const ptrdiff_t ystride = _ystride / sizeof(pixel);
    return (P0 + TP0 + Q0 + TQ0) >> 2;
}

#undef P7
#undef P6
#undef P5
#undef P4
#undef P3
#undef P2
#undef P1
#undef P0
#undef Q0
#undef Q1
#undef Q2
#undef Q3
#undef Q4
#undef Q5
#undef Q6
#undef Q7

#undef TP7
#undef TP6
#undef TP5
#undef TP4
#undef TP3
#undef TP2
#undef TP1
#undef TP0
#undef TQ0
#undef TQ1
#undef TQ2
#undef TQ3
#undef TQ4
#undef TQ5
#undef TQ6
#undef TQ7
