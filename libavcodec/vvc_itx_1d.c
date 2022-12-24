/*
 * VVC shared tables
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

/* The copyright in this software is being made available under the BSD
 * License, included below. This software may be subject to other third party
 * and contributor rights, including patent rights, and no such rights are
 * granted under this license.
 *
 * Copyright (c) 2010-2021, ITU/ISO/IEC
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *  * Neither the name of the ITU/ISO/IEC nor the names of its contributors may
 *    be used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

/* optimizaed with partial butterfly, see Hung C-Y, Landman P (1997)
   Compact inverse discrete cosine transform circuit for MPEG video decoding.
 */

#include "vvc_data.h"
#include "vvc_itx_1d.h"
#include "libavutil/avutil.h"

static void prepare_odd(int *o, int size, const int* x, ptrdiff_t stride)
{
    const int step = DCT2_MAX_SIZE / (size * 2);

    for (int i = 0; i < size; i++) {
        o[i] = 0;
        for (int j = 0; j < size; j++)
            o[i] += x[(1 + 2 * j) * stride] * ff_vvc_dct2[(1 + 2*j) * step][i];
    }
}

static void decomposition(int *out, ptrdiff_t stride, const int *even, const int *odd, int half)
{
    int size = half << 1;

    //start from back, so we can do in place decomposition
    for (int i = 0; i < half; i++)
          out[(size - i - 1) * stride] = even[i] - odd[i];
    for (int i = 0; i < half; i++)
          out[i * stride] = even[i] + odd[i];
}

/*
transMatrix[2][2] =
      { a,  a }
      { a, -a }
 */

void ff_vvc_inv_dct2_2(int *out, ptrdiff_t out_stride,
    const int *in, ptrdiff_t in_stride)
{
    const int a = 64;
    const int x0 = in[0 * in_stride], x1 = in[1 * in_stride];

    out[0 * out_stride] = a * (x0 + x1);
    out[1 * out_stride] = a * (x0 - x1);
}

/*
transMatrix[4][4] =
      { a,  a,  a,  a}
      { b,  c, -c, -b}
      { a, -a, -a,  a}
      { c, -b,  b, -c}
 */
void ff_vvc_inv_dct2_4(int *out, ptrdiff_t out_stride,
    const int *in, ptrdiff_t in_stride)
{
    const int a = 64, b = 83, c = 36;
    const int x0 = in[0 * in_stride], x1 = in[1 * in_stride];
    const int x2 = in[2 * in_stride], x3 = in[3 * in_stride];
    const int e[2] = {
        a * (x0 + x2),
        a * (x0 - x2),
    };
    const int o[2] = {
        b * x1 + c * x3,
        c * x1 - b * x3,
    };

    decomposition(out, out_stride, e, o, 2);
}

static void inv_dct2(int *out, const int size, const ptrdiff_t out_stride,
    const int *in, const ptrdiff_t in_stride, int* e, int* o)
{
      int half = size >> 1;

      if (size == 4) {
            ff_vvc_inv_dct2_4(out, out_stride, in, in_stride);
            return;
      }
      inv_dct2(e, half, 1, in, in_stride << 1, e, o);
      prepare_odd(o, half, in, in_stride);
      decomposition(out, out_stride, e, o, half);
}

#define DEFINE_INV_DCT2_1D(S)                                                                  \
void ff_vvc_inv_dct2_ ## S(int *out, ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride) \
{                                                                                              \
    int o[S>>1], e[S>>1];                                                                      \
    inv_dct2(out, S, out_stride, in, in_stride, e, o);                                         \
}

static void matrix_mul(int *out, const ptrdiff_t out_stride, const int *in, const ptrdiff_t in_stride, const int8_t* matrix, const int size)
{
    for (int i = 0; i < size; i++) {
         int o = 0;

         for (int j = 0; j < size; j++)
              o += in[j * in_stride] * matrix[j * size];
         *out = o;
         out += out_stride;
         matrix++;
    }
}

DEFINE_INV_DCT2_1D( 8);
DEFINE_INV_DCT2_1D(16);
DEFINE_INV_DCT2_1D(32);
DEFINE_INV_DCT2_1D(64);

static void inv_dct8(int *out, const ptrdiff_t out_stride, const int *in, const ptrdiff_t in_stride, const int8_t *matrix, const int size)
{
    matrix_mul(out, out_stride, in, in_stride, matrix, size);
}

#define DEFINE_INV_DCT8_1D(S)                                                                  \
void ff_vvc_inv_dct8_ ## S(int *out, ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride) \
{                                                                                              \
    inv_dct8(out, out_stride, in, in_stride, &ff_vvc_dct8_##S##x##S[0][0], S);                 \
}

DEFINE_INV_DCT8_1D( 4)
DEFINE_INV_DCT8_1D( 8)
DEFINE_INV_DCT8_1D(16)
DEFINE_INV_DCT8_1D(32)

static void inv_dst7(int *out, const ptrdiff_t out_stride, const int *in, const ptrdiff_t in_stride, const int8_t* matrix, const int size)
{
     matrix_mul(out, out_stride, in, in_stride, matrix, size);
}

#define DEFINE_INV_DST7_1D(S)                                                                  \
void ff_vvc_inv_dst7_ ## S(int *out, ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride) \
{                                                                                              \
    inv_dst7(out, out_stride, in, in_stride, &ff_vvc_dst7_##S##x##S[0][0], S);                 \
}

DEFINE_INV_DST7_1D( 4)
DEFINE_INV_DST7_1D( 8)
DEFINE_INV_DST7_1D(16)
DEFINE_INV_DST7_1D(32)

void ff_vvc_inv_lfnst_1d(int *v, const int *u, int no_zero_size, int n_tr_s, int pred_mode_intra, int lfnst_idx)
{
     int lfnst_tr_set_idx    = pred_mode_intra < 0 ? 1 : ff_vvc_lfnst_tr_set_index[pred_mode_intra];
     const int8_t *tr_mat = n_tr_s > 16 ? ff_vvc_lfnst_8x8[lfnst_tr_set_idx][lfnst_idx-1][0] : ff_vvc_lfnst_4x4[lfnst_tr_set_idx][lfnst_idx - 1][0];

     for (int j = 0; j < n_tr_s; j++, tr_mat++) {
        int t = 0;

        for (int i = 0; i < no_zero_size; i++)
            t += u[i] * tr_mat[i * n_tr_s];
        v[j] = av_clip((t + 64) >> 7 , -(1 << 15), (1 << 15) - 1);
     }
}
