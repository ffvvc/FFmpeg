/*
 * VVC 1D transform
 *
 * Copyright (C) 2023 Nuo Mi
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

/*
transmatrix[2][2] = {
    { a,  a },
    { a, -a },
}
 */
void ff_vvc_inv_dct2_2(int *out, const ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride)
{
    const int a = 64;
    const int x0 = in[0 * in_stride], x1 = in[1 * in_stride];

    out[0 * out_stride] = a * (x0 + x1);
    out[1 * out_stride] = a * (x0 - x1);
}

/*
transmatrix[4][4] = {
    { a,  a,  a,  a},
    { b,  c, -c, -b},
    { a, -a, -a,  a},
    { c, -b,  b, -c},
}
 */
void ff_vvc_inv_dct2_4(int *out, const ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride)
{
    const int a = 64, b = 83, c = 36;
    const int x0 = in[0 * in_stride], x1 = in[1 * in_stride];
    const int x2 = in[2 * in_stride], x3 = in[3 * in_stride];
    const int E[2] = {
        a * (x0 + x2),
        a * (x0 - x2),
    };
    const int O[2] = {
        b * x1 + c * x3,
        c * x1 - b * x3,
    };

    out[0 * out_stride] = E[0] + O[0];
    out[1 * out_stride] = E[1] + O[1];
    out[2 * out_stride] = E[1] - O[1];
    out[3 * out_stride] = E[0] - O[0];
}

/*
transmatrix[8][8] = {
    { a,  a,  a,  a,  a,  a,  a,  a},
    { d,  e,  f,  g, -g, -f, -e, -d},
    { b,  c, -c, -b, -b, -c,  c,  b},
    { e, -g, -d, -f,  f,  d,  g, -e},
    { a, -a, -a,  a,  a, -a, -a,  a},
    { f, -d,  g,  e, -e, -g,  d, -f},
    { c, -b,  b, -c, -c,  b, -b,  c},
    { g, -f,  e, -d,  d, -e,  f, -g},
}
 */
void ff_vvc_inv_dct2_8(int *out, const ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride)
{
    const int a = 64, b = 83, c = 36, d = 89, e = 75, f = 50, g = 18;
    const int x0 = in[0 * in_stride], x1 = in[1 * in_stride];
    const int x2 = in[2 * in_stride], x3 = in[3 * in_stride];
    const int x4 = in[4 * in_stride], x5 = in[5 * in_stride];
    const int x6 = in[6 * in_stride], x7 = in[7 * in_stride];
    const int EE[2] = {
        a * (x0 + x4),
        a * (x0 - x4),
    };
    const int EO[2] = {
        b * x2 + c * x6,
        c * x2 - b * x6,
    };
    const int E[4] = {
        EE[0] + EO[0], EE[1] + EO[1],
        EE[1] - EO[1], EE[0] - EO[0],
    };
    const int O[4] = {
        d * x1 + e * x3 + f * x5 + g * x7,
        e * x1 - g * x3 - d * x5 - f * x7,
        f * x1 - d * x3 + g * x5 + e * x7,
        g * x1 - f * x3 + e * x5 - d * x7,
    };

    out[0 * out_stride] = E[0] + O[0];
    out[1 * out_stride] = E[1] + O[1];
    out[2 * out_stride] = E[2] + O[2];
    out[3 * out_stride] = E[3] + O[3];
    out[4 * out_stride] = E[3] - O[3];
    out[5 * out_stride] = E[2] - O[2];
    out[6 * out_stride] = E[1] - O[1];
    out[7 * out_stride] = E[0] - O[0];
}

/*
transmatrix[16][16] = {
    { a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a},
    { h,  i,  j,  k,  l,  m,  n,  o, -o, -n, -m, -l, -k, -j, -i, -h},
    { d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d},
    { i,  l,  o, -m, -j, -h, -k, -n,  n,  k,  h,  j,  m, -o, -l, -i},
    { b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b},
    { j,  o, -k, -i, -n,  l,  h,  m, -m, -h, -l,  n,  i,  k, -o, -j},
    { e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e},
    { k, -m, -i,  o,  h,  n, -j, -l,  l,  j, -n, -h, -o,  i,  m, -k},
    { a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a},
    { l, -j, -n,  h, -o, -i,  m,  k, -k, -m,  i,  o, -h,  n,  j, -l},
    { f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f},
    { m, -h,  l,  n, -i,  k,  o, -j,  j, -o, -k,  i, -n, -l,  h, -m},
    { c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c},
    { n, -k,  h, -j,  m,  o, -l,  i, -i,  l, -o, -m,  j, -h,  k, -n},
    { g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g},
    { o, -n,  m, -l,  k, -j,  i, -h,  h, -i,  j, -k,  l, -m,  n, -o},
}
 */
void ff_vvc_inv_dct2_16(int *out, const ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride)
{
    const int a = 64, b = 83, c = 36, d = 89, e = 75, f = 50, g = 18, h = 90;
    const int i = 87, j = 80, k = 70, l = 57, m = 43, n = 25, o =  9;
    const int x0  = in[0  * in_stride], x1  = in[1  * in_stride];
    const int x2  = in[2  * in_stride], x3  = in[3  * in_stride];
    const int x4  = in[4  * in_stride], x5  = in[5  * in_stride];
    const int x6  = in[6  * in_stride], x7  = in[7  * in_stride];
    const int x8  = in[8  * in_stride], x9  = in[9  * in_stride];
    const int x10 = in[10 * in_stride], x11 = in[11 * in_stride];
    const int x12 = in[12 * in_stride], x13 = in[13 * in_stride];
    const int x14 = in[14 * in_stride], x15 = in[15 * in_stride];
    const int EEE[2] = {
        a * (x0 + x8),
        a * (x0 - x8),
    };
    const int EEO[2] = {
        b * x4 + c * x12,
        c * x4 - b * x12,
    };
    const int EE[4] = {
        EEE[0] + EEO[0], EEE[1] + EEO[1],
        EEE[1] - EEO[1], EEE[0] - EEO[0],
    };
    const int EO[4] = {
        d * x2  + e * x6 + f * x10 + g * x14,
        e * x2  - g * x6 - d * x10 - f * x14,
        f * x2  - d * x6 + g * x10 + e * x14,
        g * x2  - f * x6 + e * x10 - d * x14,
    };
    const int E[8] = {
        EE[0] + EO[0], EE[1] + EO[1], EE[2] + EO[2], EE[3] + EO[3],
        EE[3] - EO[3], EE[2] - EO[2], EE[1] - EO[1], EE[0] - EO[0],
    };
    const int O[8] = {
        h * x1 + i * x3 + j * x5 + k * x7 + l * x9 + m * x11 + n * x13 + o * x15,
        i * x1 + l * x3 + o * x5 - m * x7 - j * x9 - h * x11 - k * x13 - n * x15,
        j * x1 + o * x3 - k * x5 - i * x7 - n * x9 + l * x11 + h * x13 + m * x15,
        k * x1 - m * x3 - i * x5 + o * x7 + h * x9 + n * x11 - j * x13 - l * x15,
        l * x1 - j * x3 - n * x5 + h * x7 - o * x9 - i * x11 + m * x13 + k * x15,
        m * x1 - h * x3 + l * x5 + n * x7 - i * x9 + k * x11 + o * x13 - j * x15,
        n * x1 - k * x3 + h * x5 - j * x7 + m * x9 + o * x11 - l * x13 + i * x15,
        o * x1 - n * x3 + m * x5 - l * x7 + k * x9 - j * x11 + i * x13 - h * x15,
    };

    out[0  * out_stride] = E[0] + O[0];
    out[1  * out_stride] = E[1] + O[1];
    out[2  * out_stride] = E[2] + O[2];
    out[3  * out_stride] = E[3] + O[3];
    out[4  * out_stride] = E[4] + O[4];
    out[5  * out_stride] = E[5] + O[5];
    out[6  * out_stride] = E[6] + O[6];
    out[7  * out_stride] = E[7] + O[7];
    out[8  * out_stride] = E[7] - O[7];
    out[9  * out_stride] = E[6] - O[6];
    out[10 * out_stride] = E[5] - O[5];
    out[11 * out_stride] = E[4] - O[4];
    out[12 * out_stride] = E[3] - O[3];
    out[13 * out_stride] = E[2] - O[2];
    out[14 * out_stride] = E[1] - O[1];
    out[15 * out_stride] = E[0] - O[0];
}

/*
transMatrix[32][32] = {
    { a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a,  a},
    { p,  q,  r,  s,  t,  u,  v,  w,  x,  y,  z,  A,  B,  C,  D,  E, -E, -D, -C, -B, -A, -z, -y, -x, -w, -v, -u, -t, -s, -r, -q, -p},
    { h,  i,  j,  k,  l,  m,  n,  o, -o, -n, -m, -l, -k, -j, -i, -h, -h, -i, -j, -k, -l, -m, -n, -o,  o,  n,  m,  l,  k,  j,  i,  h},
    { q,  t,  w,  z,  C, -E, -B, -y, -v, -s, -p, -r, -u, -x, -A, -D,  D,  A,  x,  u,  r,  p,  s,  v,  y,  B,  E, -C, -z, -w, -t, -q},
    { d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d,  d,  e,  f,  g, -g, -f, -e, -d, -d, -e, -f, -g,  g,  f,  e,  d},
    { r,  w,  B, -D, -y, -t, -p, -u, -z, -E,  A,  v,  q,  s,  x,  C, -C, -x, -s, -q, -v, -A,  E,  z,  u,  p,  t,  y,  D, -B, -w, -r},
    { i,  l,  o, -m, -j, -h, -k, -n,  n,  k,  h,  j,  m, -o, -l, -i, -i, -l, -o,  m,  j,  h,  k,  n, -n, -k, -h, -j, -m,  o,  l,  i},
    { s,  z, -D, -w, -p, -v, -C,  A,  t,  r,  y, -E, -x, -q, -u, -B,  B,  u,  q,  x,  E, -y, -r, -t, -A,  C,  v,  p,  w,  D, -z, -s},
    { b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b,  b,  c, -c, -b, -b, -c,  c,  b},
    { t,  C, -y, -p, -x,  D,  u,  s,  B, -z, -q, -w,  E,  v,  r,  A, -A, -r, -v, -E,  w,  q,  z, -B, -s, -u, -D,  x,  p,  y, -C, -t},
    { j,  o, -k, -i, -n,  l,  h,  m, -m, -h, -l,  n,  i,  k, -o, -j, -j, -o,  k,  i,  n, -l, -h, -m,  m,  h,  l, -n, -i, -k,  o,  j},
    { u, -E, -t, -v,  D,  s,  w, -C, -r, -x,  B,  q,  y, -A, -p, -z,  z,  p,  A, -y, -q, -B,  x,  r,  C, -w, -s, -D,  v,  t,  E, -u},
    { e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e,  e, -g, -d, -f,  f,  d,  g, -e, -e,  g,  d,  f, -f, -d, -g,  e},
    { v, -B, -p, -C,  u,  w, -A, -q, -D,  t,  x, -z, -r, -E,  s,  y, -y, -s,  E,  r,  z, -x, -t,  D,  q,  A, -w, -u,  C,  p,  B, -v},
    { k, -m, -i,  o,  h,  n, -j, -l,  l,  j, -n, -h, -o,  i,  m, -k, -k,  m,  i, -o, -h, -n,  j,  l, -l, -j,  n,  h,  o, -i, -m,  k},
    { w, -y, -u,  A,  s, -C, -q,  E,  p,  D, -r, -B,  t,  z, -v, -x,  x,  v, -z, -t,  B,  r, -D, -p, -E,  q,  C, -s, -A,  u,  y, -w},
    { a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a,  a, -a, -a,  a},
    { x, -v, -z,  t,  B, -r, -D,  p, -E, -q,  C,  s, -A, -u,  y,  w, -w, -y,  u,  A, -s, -C,  q,  E, -p,  D,  r, -B, -t,  z,  v, -x},
    { l, -j, -n,  h, -o, -i,  m,  k, -k, -m,  i,  o, -h,  n,  j, -l, -l,  j,  n, -h,  o,  i, -m, -k,  k,  m, -i, -o,  h, -n, -j,  l},
    { y, -s, -E,  r, -z, -x,  t,  D, -q,  A,  w, -u, -C,  p, -B, -v,  v,  B, -p,  C,  u, -w, -A,  q, -D, -t,  x,  z, -r,  E,  s, -y},
    { f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f,  f, -d,  g,  e, -e, -g,  d, -f, -f,  d, -g, -e,  e,  g, -d,  f},
    { z, -p,  A,  y, -q,  B,  x, -r,  C,  w, -s,  D,  v, -t,  E,  u, -u, -E,  t, -v, -D,  s, -w, -C,  r, -x, -B,  q, -y, -A,  p, -z},
    { m, -h,  l,  n, -i,  k,  o, -j,  j, -o, -k,  i, -n, -l,  h, -m, -m,  h, -l, -n,  i, -k, -o,  j, -j,  o,  k, -i,  n,  l, -h,  m},
    { A, -r,  v, -E, -w,  q, -z, -B,  s, -u,  D,  x, -p,  y,  C, -t,  t, -C, -y,  p, -x, -D,  u, -s,  B,  z, -q,  w,  E, -v,  r, -A},
    { c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c,  c, -b,  b, -c, -c,  b, -b,  c},
    { B, -u,  q, -x,  E,  y, -r,  t, -A, -C,  v, -p,  w, -D, -z,  s, -s,  z,  D, -w,  p, -v,  C,  A, -t,  r, -y, -E,  x, -q,  u, -B},
    { n, -k,  h, -j,  m,  o, -l,  i, -i,  l, -o, -m,  j, -h,  k, -n, -n,  k, -h,  j, -m, -o,  l, -i,  i, -l,  o,  m, -j,  h, -k,  n},
    { C, -x,  s, -q,  v, -A, -E,  z, -u,  p, -t,  y, -D, -B,  w, -r,  r, -w,  B,  D, -y,  t, -p,  u, -z,  E,  A, -v,  q, -s,  x, -C},
    { g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g,  g, -f,  e, -d,  d, -e,  f, -g, -g,  f, -e,  d, -d,  e, -f,  g},
    { D, -A,  x, -u,  r, -p,  s, -v,  y, -B,  E,  C, -z,  w, -t,  q, -q,  t, -w,  z, -C, -E,  B, -y,  v, -s,  p, -r,  u, -x,  A, -D},
    { o, -n,  m, -l,  k, -j,  i, -h,  h, -i,  j, -k,  l, -m,  n, -o, -o,  n, -m,  l, -k,  j, -i,  h, -h,  i, -j,  k, -l,  m, -n,  o},
    { E, -D,  C, -B,  A, -z,  y, -x,  w, -v,  u, -t,  s, -r,  q, -p,  p, -q,  r, -s,  t, -u,  v, -w,  x, -y,  z, -A,  B, -C,  D, -E},
}
 */
void ff_vvc_inv_dct2_32(int *out, const ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride)
{
    const int a = 64, b = 83, c = 36, d = 89, e = 75, f = 50, g = 18, h = 90;
    const int i = 87, j = 80, k = 70, l = 57, m = 43, n = 25, o =  9, p = 90;
    const int q = 90, r = 88, s = 85, t = 82, u = 78, v = 73, w = 67, x = 61;
    const int y = 54, z = 46, A = 38, B = 31, C = 22, D = 13, E_=  4;
    const int x0  = in[0  * in_stride], x1  = in[1  * in_stride];
    const int x2  = in[2  * in_stride], x3  = in[3  * in_stride];
    const int x4  = in[4  * in_stride], x5  = in[5  * in_stride];
    const int x6  = in[6  * in_stride], x7  = in[7  * in_stride];
    const int x8  = in[8  * in_stride], x9  = in[9  * in_stride];
    const int x10 = in[10 * in_stride], x11 = in[11 * in_stride];
    const int x12 = in[12 * in_stride], x13 = in[13 * in_stride];
    const int x14 = in[14 * in_stride], x15 = in[15 * in_stride];
    const int x16 = in[16 * in_stride], x17 = in[17 * in_stride];
    const int x18 = in[18 * in_stride], x19 = in[19 * in_stride];
    const int x20 = in[20 * in_stride], x21 = in[21 * in_stride];
    const int x22 = in[22 * in_stride], x23 = in[23 * in_stride];
    const int x24 = in[24 * in_stride], x25 = in[25 * in_stride];
    const int x26 = in[26 * in_stride], x27 = in[27 * in_stride];
    const int x28 = in[28 * in_stride], x29 = in[29 * in_stride];
    const int x30 = in[30 * in_stride], x31 = in[31 * in_stride];
    const int EEEE[2] = {
        a * (x0 + x16),
        a * (x0 - x16),
    };
    const int EEEO[2] = {
        b * x8 + c * x24,
        c * x8 - b * x24,
    };
    const int EEE[4] = {
        EEEE[0] + EEEO[0], EEEE[1] + EEEO[1],
        EEEE[1] - EEEO[1], EEEE[0] - EEEO[0],
    };
    const int EEO[4] = {
        d * x4  + e * x12 + f * x20 + g * x28,
        e * x4  - g * x12 - d * x20 - f * x28,
        f * x4  - d * x12 + g * x20 + e * x28,
        g * x4  - f * x12 + e * x20 - d * x28,
    };
    const int EE[8] = {
        EEE[0] + EEO[0], EEE[1] + EEO[1], EEE[2] + EEO[2], EEE[3] + EEO[3],
        EEE[3] - EEO[3], EEE[2] - EEO[2], EEE[1] - EEO[1], EEE[0] - EEO[0],
    };
    const int EO[8] = {
        h * x2 + i * x6 + j * x10 + k * x14 + l * x18 + m * x22 + n * x26 + o * x30,
        i * x2 + l * x6 + o * x10 - m * x14 - j * x18 - h * x22 - k * x26 - n * x30,
        j * x2 + o * x6 - k * x10 - i * x14 - n * x18 + l * x22 + h * x26 + m * x30,
        k * x2 - m * x6 - i * x10 + o * x14 + h * x18 + n * x22 - j * x26 - l * x30,
        l * x2 - j * x6 - n * x10 + h * x14 - o * x18 - i * x22 + m * x26 + k * x30,
        m * x2 - h * x6 + l * x10 + n * x14 - i * x18 + k * x22 + o * x26 - j * x30,
        n * x2 - k * x6 + h * x10 - j * x14 + m * x18 + o * x22 - l * x26 + i * x30,
        o * x2 - n * x6 + m * x10 - l * x14 + k * x18 - j * x22 + i * x26 - h * x30,
    };
    const int E[16] = {
        EE[0] + EO[0], EE[1] + EO[1], EE[2] + EO[2], EE[3] + EO[3], EE[4] + EO[4], EE[5] + EO[5], EE[6] + EO[6], EE[7] + EO[7],
        EE[7] - EO[7], EE[6] - EO[6], EE[5] - EO[5], EE[4] - EO[4], EE[3] - EO[3], EE[2] - EO[2], EE[1] - EO[1], EE[0] - EO[0],
    };
    const int O[16] = {
        p * x1 + q * x3 + r * x5 + s * x7 + t * x9 + u * x11 + v * x13 + w * x15 + x * x17 + y * x19 + z * x21 + A * x23 + B * x25 + C * x27 + D * x29 + E_* x31,
        q * x1 + t * x3 + w * x5 + z * x7 + C * x9 - E_* x11 - B * x13 - y * x15 - v * x17 - s * x19 - p * x21 - r * x23 - u * x25 - x * x27 - A * x29 - D * x31,
        r * x1 + w * x3 + B * x5 - D * x7 - y * x9 - t * x11 - p * x13 - u * x15 - z * x17 - E_* x19 + A * x21 + v * x23 + q * x25 + s * x27 + x * x29 + C * x31,
        s * x1 + z * x3 - D * x5 - w * x7 - p * x9 - v * x11 - C * x13 + A * x15 + t * x17 + r * x19 + y * x21 - E_* x23 - x * x25 - q * x27 - u * x29 - B * x31,
        t * x1 + C * x3 - y * x5 - p * x7 - x * x9 + D * x11 + u * x13 + s * x15 + B * x17 - z * x19 - q * x21 - w * x23 + E_* x25 + v * x27 + r * x29 + A * x31,
        u * x1 - E_* x3 - t * x5 - v * x7 + D * x9 + s * x11 + w * x13 - C * x15 - r * x17 - x * x19 + B * x21 + q * x23 + y * x25 - A * x27 - p * x29 - z * x31,
        v * x1 - B * x3 - p * x5 - C * x7 + u * x9 + w * x11 - A * x13 - q * x15 - D * x17 + t * x19 + x * x21 - z * x23 - r * x25 - E_* x27 + s * x29 + y * x31,
        w * x1 - y * x3 - u * x5 + A * x7 + s * x9 - C * x11 - q * x13 + E_* x15 + p * x17 + D * x19 - r * x21 - B * x23 + t * x25 + z * x27 - v * x29 - x * x31,
        x * x1 - v * x3 - z * x5 + t * x7 + B * x9 - r * x11 - D * x13 + p * x15 - E_* x17 - q * x19 + C * x21 + s * x23 - A * x25 - u * x27 + y * x29 + w * x31,
        y * x1 - s * x3 - E_* x5 + r * x7 - z * x9 - x * x11 + t * x13 + D * x15 - q * x17 + A * x19 + w * x21 - u * x23 - C * x25 + p * x27 - B * x29 - v * x31,
        z * x1 - p * x3 + A * x5 + y * x7 - q * x9 + B * x11 + x * x13 - r * x15 + C * x17 + w * x19 - s * x21 + D * x23 + v * x25 - t * x27 + E_* x29 + u * x31,
        A * x1 - r * x3 + v * x5 - E_* x7 - w * x9 + q * x11 - z * x13 - B * x15 + s * x17 - u * x19 + D * x21 + x * x23 - p * x25 + y * x27 + C * x29 - t * x31,
        B * x1 - u * x3 + q * x5 - x * x7 + E_* x9 + y * x11 - r * x13 + t * x15 - A * x17 - C * x19 + v * x21 - p * x23 + w * x25 - D * x27 - z * x29 + s * x31,
        C * x1 - x * x3 + s * x5 - q * x7 + v * x9 - A * x11 - E_* x13 + z * x15 - u * x17 + p * x19 - t * x21 + y * x23 - D * x25 - B * x27 + w * x29 - r * x31,
        D * x1 - A * x3 + x * x5 - u * x7 + r * x9 - p * x11 + s * x13 - v * x15 + y * x17 - B * x19 + E_* x21 + C * x23 - z * x25 + w * x27 - t * x29 + q * x31,
        E_* x1 - D * x3 + C * x5 - B * x7 + A * x9 - z * x11 + y * x13 - x * x15 + w * x17 - v * x19 + u * x21 - t * x23 + s * x25 - r * x27 + q * x29 - p * x31,
    };

    out[0  * out_stride] = E[0]  + O[0];
    out[1  * out_stride] = E[1]  + O[1];
    out[2  * out_stride] = E[2]  + O[2];
    out[3  * out_stride] = E[3]  + O[3];
    out[4  * out_stride] = E[4]  + O[4];
    out[5  * out_stride] = E[5]  + O[5];
    out[6  * out_stride] = E[6]  + O[6];
    out[7  * out_stride] = E[7]  + O[7];
    out[8  * out_stride] = E[8]  + O[8];
    out[9  * out_stride] = E[9]  + O[9];
    out[10 * out_stride] = E[10] + O[10];
    out[11 * out_stride] = E[11] + O[11];
    out[12 * out_stride] = E[12] + O[12];
    out[13 * out_stride] = E[13] + O[13];
    out[14 * out_stride] = E[14] + O[14];
    out[15 * out_stride] = E[15] + O[15];
    out[16 * out_stride] = E[15] - O[15];
    out[17 * out_stride] = E[14] - O[14];
    out[18 * out_stride] = E[13] - O[13];
    out[19 * out_stride] = E[12] - O[12];
    out[20 * out_stride] = E[11] - O[11];
    out[21 * out_stride] = E[10] - O[10];
    out[22 * out_stride] = E[9]  - O[9];
    out[23 * out_stride] = E[8]  - O[8];
    out[24 * out_stride] = E[7]  - O[7];
    out[25 * out_stride] = E[6]  - O[6];
    out[26 * out_stride] = E[5]  - O[5];
    out[27 * out_stride] = E[4]  - O[4];
    out[28 * out_stride] = E[3]  - O[3];
    out[29 * out_stride] = E[2]  - O[2];
    out[30 * out_stride] = E[1]  - O[1];
    out[31 * out_stride] = E[0]  - O[0];
}

/*
transMatrix[64][64] = {
    { aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa,  aa },
    { bf,  bg,  bh,  bi,  bj,  bk,  bl,  bm,  bn,  bo,  bp,  bq,  br,  bs,  bt,  bu,  bv,  bw,  bx,  by,  bz,  ca,  cb,  cc,  cd,  ce,  cf,  cg,  ch,  ci,  cj,  ck, -ck, -cj, -ci, -ch, -cg, -cf, -ce, -cd, -cc, -cb, -ca, -bz, -by, -bx, -bw, -bv, -bu, -bt, -bs, -br, -bq, -bp, -bo, -bn, -bm, -bl, -bk, -bj, -bi, -bh, -bg, -bf },
    { ap,  aq,  ar,  as,  at,  au,  av,  aw,  ax,  ay,  az,  ba,  bb,  bc,  bd,  be, -be, -bd, -bc, -bb, -ba, -az, -ay, -ax, -aw, -av, -au, -at, -as, -ar, -aq, -ap, -ap, -aq, -ar, -as, -at, -au, -av, -aw, -ax, -ay, -az, -ba, -bb, -bc, -bd, -be,  be,  bd,  bc,  bb,  ba,  az,  ay,  ax,  aw,  av,  au,  at,  as,  ar,  aq,  ap },
    { bg,  bj,  bm,  bp,  bs,  bv,  by,  cb,  ce,  ch,  ck, -ci, -cf, -cc, -bz, -bw, -bt, -bq, -bn, -bk, -bh, -bf, -bi, -bl, -bo, -br, -bu, -bx, -ca, -cd, -cg, -cj,  cj,  cg,  cd,  ca,  bx,  bu,  br,  bo,  bl,  bi,  bf,  bh,  bk,  bn,  bq,  bt,  bw,  bz,  cc,  cf,  ci, -ck, -ch, -ce, -cb, -by, -bv, -bs, -bp, -bm, -bj, -bg },
    { ah,  ai,  aj,  ak,  al,  am,  an,  ao, -ao, -an, -am, -al, -ak, -aj, -ai, -ah, -ah, -ai, -aj, -ak, -al, -am, -an, -ao,  ao,  an,  am,  al,  ak,  aj,  ai,  ah,  ah,  ai,  aj,  ak,  al,  am,  an,  ao, -ao, -an, -am, -al, -ak, -aj, -ai, -ah, -ah, -ai, -aj, -ak, -al, -am, -an, -ao,  ao,  an,  am,  al,  ak,  aj,  ai,  ah },
    { bh,  bm,  br,  bw,  cb,  cg, -ck, -cf, -ca, -bv, -bq, -bl, -bg, -bi, -bn, -bs, -bx, -cc, -ch,  cj,  ce,  bz,  bu,  bp,  bk,  bf,  bj,  bo,  bt,  by,  cd,  ci, -ci, -cd, -by, -bt, -bo, -bj, -bf, -bk, -bp, -bu, -bz, -ce, -cj,  ch,  cc,  bx,  bs,  bn,  bi,  bg,  bl,  bq,  bv,  ca,  cf,  ck, -cg, -cb, -bw, -br, -bm, -bh },
    { aq,  at,  aw,  az,  bc, -be, -bb, -ay, -av, -as, -ap, -ar, -au, -ax, -ba, -bd,  bd,  ba,  ax,  au,  ar,  ap,  as,  av,  ay,  bb,  be, -bc, -az, -aw, -at, -aq, -aq, -at, -aw, -az, -bc,  be,  bb,  ay,  av,  as,  ap,  ar,  au,  ax,  ba,  bd, -bd, -ba, -ax, -au, -ar, -ap, -as, -av, -ay, -bb, -be,  bc,  az,  aw,  at,  aq },
    { bi,  bp,  bw,  cd,  ck, -ce, -bx, -bq, -bj, -bh, -bo, -bv, -cc, -cj,  cf,  by,  br,  bk,  bg,  bn,  bu,  cb,  ci, -cg, -bz, -bs, -bl, -bf, -bm, -bt, -ca, -ch,  ch,  ca,  bt,  bm,  bf,  bl,  bs,  bz,  cg, -ci, -cb, -bu, -bn, -bg, -bk, -br, -by, -cf,  cj,  cc,  bv,  bo,  bh,  bj,  bq,  bx,  ce, -ck, -cd, -bw, -bp, -bi },
    { ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad,  ad,  ae,  af,  ag, -ag, -af, -ae, -ad, -ad, -ae, -af, -ag,  ag,  af,  ae,  ad },
    { bj,  bs,  cb,  ck, -cc, -bt, -bk, -bi, -br, -ca, -cj,  cd,  bu,  bl,  bh,  bq,  bz,  ci, -ce, -bv, -bm, -bg, -bp, -by, -ch,  cf,  bw,  bn,  bf,  bo,  bx,  cg, -cg, -bx, -bo, -bf, -bn, -bw, -cf,  ch,  by,  bp,  bg,  bm,  bv,  ce, -ci, -bz, -bq, -bh, -bl, -bu, -cd,  cj,  ca,  br,  bi,  bk,  bt,  cc, -ck, -cb, -bs, -bj },
    { ar,  aw,  bb, -bd, -ay, -at, -ap, -au, -az, -be,  ba,  av,  aq,  as,  ax,  bc, -bc, -ax, -as, -aq, -av, -ba,  be,  az,  au,  ap,  at,  ay,  bd, -bb, -aw, -ar, -ar, -aw, -bb,  bd,  ay,  at,  ap,  au,  az,  be, -ba, -av, -aq, -as, -ax, -bc,  bc,  ax,  as,  aq,  av,  ba, -be, -az, -au, -ap, -at, -ay, -bd,  bb,  aw,  ar },
    { bk,  bv,  cg, -ce, -bt, -bi, -bm, -bx, -ci,  cc,  br,  bg,  bo,  bz,  ck, -ca, -bp, -bf, -bq, -cb,  cj,  by,  bn,  bh,  bs,  cd, -ch, -bw, -bl, -bj, -bu, -cf,  cf,  bu,  bj,  bl,  bw,  ch, -cd, -bs, -bh, -bn, -by, -cj,  cb,  bq,  bf,  bp,  ca, -ck, -bz, -bo, -bg, -br, -cc,  ci,  bx,  bm,  bi,  bt,  ce, -cg, -bv, -bk },
    { ai,  al,  ao, -am, -aj, -ah, -ak, -an,  an,  ak,  ah,  aj,  am, -ao, -al, -ai, -ai, -al, -ao,  am,  aj,  ah,  ak,  an, -an, -ak, -ah, -aj, -am,  ao,  al,  ai,  ai,  al,  ao, -am, -aj, -ah, -ak, -an,  an,  ak,  ah,  aj,  am, -ao, -al, -ai, -ai, -al, -ao,  am,  aj,  ah,  ak,  an, -an, -ak, -ah, -aj, -am,  ao,  al,  ai },
    { bl,  by, -ck, -bx, -bk, -bm, -bz,  cj,  bw,  bj,  bn,  ca, -ci, -bv, -bi, -bo, -cb,  ch,  bu,  bh,  bp,  cc, -cg, -bt, -bg, -bq, -cd,  cf,  bs,  bf,  br,  ce, -ce, -br, -bf, -bs, -cf,  cd,  bq,  bg,  bt,  cg, -cc, -bp, -bh, -bu, -ch,  cb,  bo,  bi,  bv,  ci, -ca, -bn, -bj, -bw, -cj,  bz,  bm,  bk,  bx,  ck, -by, -bl },
    { as,  az, -bd, -aw, -ap, -av, -bc,  ba,  at,  ar,  ay, -be, -ax, -aq, -au, -bb,  bb,  au,  aq,  ax,  be, -ay, -ar, -at, -ba,  bc,  av,  ap,  aw,  bd, -az, -as, -as, -az,  bd,  aw,  ap,  av,  bc, -ba, -at, -ar, -ay,  be,  ax,  aq,  au,  bb, -bb, -au, -aq, -ax, -be,  ay,  ar,  at,  ba, -bc, -av, -ap, -aw, -bd,  az,  as },
    { bm,  cb, -cf, -bq, -bi, -bx,  cj,  bu,  bf,  bt,  ci, -by, -bj, -bp, -ce,  cc,  bn,  bl,  ca, -cg, -br, -bh, -bw,  ck,  bv,  bg,  bs,  ch, -bz, -bk, -bo, -cd,  cd,  bo,  bk,  bz, -ch, -bs, -bg, -bv, -ck,  bw,  bh,  br,  cg, -ca, -bl, -bn, -cc,  ce,  bp,  bj,  by, -ci, -bt, -bf, -bu, -cj,  bx,  bi,  bq,  cf, -cb, -bm },
    { ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab,  ab,  ac, -ac, -ab, -ab, -ac,  ac,  ab },
    { bn,  ce, -ca, -bj, -br, -ci,  bw,  bf,  bv, -cj, -bs, -bi, -bz,  cf,  bo,  bm,  cd, -cb, -bk, -bq, -ch,  bx,  bg,  bu, -ck, -bt, -bh, -by,  cg,  bp,  bl,  cc, -cc, -bl, -bp, -cg,  by,  bh,  bt,  ck, -bu, -bg, -bx,  ch,  bq,  bk,  cb, -cd, -bm, -bo, -cf,  bz,  bi,  bs,  cj, -bv, -bf, -bw,  ci,  br,  bj,  ca, -ce, -bn },
    { at,  bc, -ay, -ap, -ax,  bd,  au,  as,  bb, -az, -aq, -aw,  be,  av,  ar,  ba, -ba, -ar, -av, -be,  aw,  aq,  az, -bb, -as, -au, -bd,  ax,  ap,  ay, -bc, -at, -at, -bc,  ay,  ap,  ax, -bd, -au, -as, -bb,  az,  aq,  aw, -be, -av, -ar, -ba,  ba,  ar,  av,  be, -aw, -aq, -az,  bb,  as,  au,  bd, -ax, -ap, -ay,  bc,  at },
    { bo,  ch, -bv, -bh, -ca,  cc,  bj,  bt, -cj, -bq, -bm, -cf,  bx,  bf,  by, -ce, -bl, -br, -ck,  bs,  bk,  cd, -bz, -bg, -bw,  cg,  bn,  bp,  ci, -bu, -bi, -cb,  cb,  bi,  bu, -ci, -bp, -bn, -cg,  bw,  bg,  bz, -cd, -bk, -bs,  ck,  br,  bl,  ce, -by, -bf, -bx,  cf,  bm,  bq,  cj, -bt, -bj, -cc,  ca,  bh,  bv, -ch, -bo },
    { aj,  ao, -ak, -ai, -an,  al,  ah,  am, -am, -ah, -al,  an,  ai,  ak, -ao, -aj, -aj, -ao,  ak,  ai,  an, -al, -ah, -am,  am,  ah,  al, -an, -ai, -ak,  ao,  aj,  aj,  ao, -ak, -ai, -an,  al,  ah,  am, -am, -ah, -al,  an,  ai,  ak, -ao, -aj, -aj, -ao,  ak,  ai,  an, -al, -ah, -am,  am,  ah,  al, -an, -ai, -ak,  ao,  aj },
    { bp,  ck, -bq, -bo, -cj,  br,  bn,  ci, -bs, -bm, -ch,  bt,  bl,  cg, -bu, -bk, -cf,  bv,  bj,  ce, -bw, -bi, -cd,  bx,  bh,  cc, -by, -bg, -cb,  bz,  bf,  ca, -ca, -bf, -bz,  cb,  bg,  by, -cc, -bh, -bx,  cd,  bi,  bw, -ce, -bj, -bv,  cf,  bk,  bu, -cg, -bl, -bt,  ch,  bm,  bs, -ci, -bn, -br,  cj,  bo,  bq, -ck, -bp },
    { au, -be, -at, -av,  bd,  as,  aw, -bc, -ar, -ax,  bb,  aq,  ay, -ba, -ap, -az,  az,  ap,  ba, -ay, -aq, -bb,  ax,  ar,  bc, -aw, -as, -bd,  av,  at,  be, -au, -au,  be,  at,  av, -bd, -as, -aw,  bc,  ar,  ax, -bb, -aq, -ay,  ba,  ap,  az, -az, -ap, -ba,  ay,  aq,  bb, -ax, -ar, -bc,  aw,  as,  bd, -av, -at, -be,  au },
    { bq, -ci, -bl, -bv,  cd,  bg,  ca, -by, -bi, -cf,  bt,  bn,  ck, -bo, -bs,  cg,  bj,  bx, -cb, -bf, -cc,  bw,  bk,  ch, -br, -bp,  cj,  bm,  bu, -ce, -bh, -bz,  bz,  bh,  ce, -bu, -bm, -cj,  bp,  br, -ch, -bk, -bw,  cc,  bf,  cb, -bx, -bj, -cg,  bs,  bo, -ck, -bn, -bt,  cf,  bi,  by, -ca, -bg, -cd,  bv,  bl,  ci, -bq },
    { ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae,  ae, -ag, -ad, -af,  af,  ad,  ag, -ae, -ae,  ag,  ad,  af, -af, -ad, -ag,  ae },
    { br, -cf, -bg, -cc,  bu,  bo, -ci, -bj, -bz,  bx,  bl,  ck, -bm, -bw,  ca,  bi,  ch, -bp, -bt,  cd,  bf,  ce, -bs, -bq,  cg,  bh,  cb, -bv, -bn,  cj,  bk,  by, -by, -bk, -cj,  bn,  bv, -cb, -bh, -cg,  bq,  bs, -ce, -bf, -cd,  bt,  bp, -ch, -bi, -ca,  bw,  bm, -ck, -bl, -bx,  bz,  bj,  ci, -bo, -bu,  cc,  bg,  cf, -br },
    { av, -bb, -ap, -bc,  au,  aw, -ba, -aq, -bd,  at,  ax, -az, -ar, -be,  as,  ay, -ay, -as,  be,  ar,  az, -ax, -at,  bd,  aq,  ba, -aw, -au,  bc,  ap,  bb, -av, -av,  bb,  ap,  bc, -au, -aw,  ba,  aq,  bd, -at, -ax,  az,  ar,  be, -as, -ay,  ay,  as, -be, -ar, -az,  ax,  at, -bd, -aq, -ba,  aw,  au, -bc, -ap, -bb,  av },
    { bs, -cc, -bi, -cj,  bl,  bz, -bv, -bp,  cf,  bf,  cg, -bo, -bw,  by,  bm, -ci, -bh, -cd,  br,  bt, -cb, -bj, -ck,  bk,  ca, -bu, -bq,  ce,  bg,  ch, -bn, -bx,  bx,  bn, -ch, -bg, -ce,  bq,  bu, -ca, -bk,  ck,  bj,  cb, -bt, -br,  cd,  bh,  ci, -bm, -by,  bw,  bo, -cg, -bf, -cf,  bp,  bv, -bz, -bl,  cj,  bi,  cc, -bs },
    { ak, -am, -ai,  ao,  ah,  an, -aj, -al,  al,  aj, -an, -ah, -ao,  ai,  am, -ak, -ak,  am,  ai, -ao, -ah, -an,  aj,  al, -al, -aj,  an,  ah,  ao, -ai, -am,  ak,  ak, -am, -ai,  ao,  ah,  an, -aj, -al,  al,  aj, -an, -ah, -ao,  ai,  am, -ak, -ak,  am,  ai, -ao, -ah, -an,  aj,  al, -al, -aj,  an,  ah,  ao, -ai, -am,  ak },
    { bt, -bz, -bn,  cf,  bh,  ck, -bi, -ce,  bo,  by, -bu, -bs,  ca,  bm, -cg, -bg, -cj,  bj,  cd, -bp, -bx,  bv,  br, -cb, -bl,  ch,  bf,  ci, -bk, -cc,  bq,  bw, -bw, -bq,  cc,  bk, -ci, -bf, -ch,  bl,  cb, -br, -bv,  bx,  bp, -cd, -bj,  cj,  bg,  cg, -bm, -ca,  bs,  bu, -by, -bo,  ce,  bi, -ck, -bh, -cf,  bn,  bz, -bt },
    { aw, -ay, -au,  ba,  as, -bc, -aq,  be,  ap,  bd, -ar, -bb,  at,  az, -av, -ax,  ax,  av, -az, -at,  bb,  ar, -bd, -ap, -be,  aq,  bc, -as, -ba,  au,  ay, -aw, -aw,  ay,  au, -ba, -as,  bc,  aq, -be, -ap, -bd,  ar,  bb, -at, -az,  av,  ax, -ax, -av,  az,  at, -bb, -ar,  bd,  ap,  be, -aq, -bc,  as,  ba, -au, -ay,  aw },
    { bu, -bw, -bs,  by,  bq, -ca, -bo,  cc,  bm, -ce, -bk,  cg,  bi, -ci, -bg,  ck,  bf,  cj, -bh, -ch,  bj,  cf, -bl, -cd,  bn,  cb, -bp, -bz,  br,  bx, -bt, -bv,  bv,  bt, -bx, -br,  bz,  bp, -cb, -bn,  cd,  bl, -cf, -bj,  ch,  bh, -cj, -bf, -ck,  bg,  ci, -bi, -cg,  bk,  ce, -bm, -cc,  bo,  ca, -bq, -by,  bs,  bw, -bu },
    { aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa,  aa, -aa, -aa,  aa },
    { bv, -bt, -bx,  br,  bz, -bp, -cb,  bn,  cd, -bl, -cf,  bj,  ch, -bh, -cj,  bf, -ck, -bg,  ci,  bi, -cg, -bk,  ce,  bm, -cc, -bo,  ca,  bq, -by, -bs,  bw,  bu, -bu, -bw,  bs,  by, -bq, -ca,  bo,  cc, -bm, -ce,  bk,  cg, -bi, -ci,  bg,  ck, -bf,  cj,  bh, -ch, -bj,  cf,  bl, -cd, -bn,  cb,  bp, -bz, -br,  bx,  bt, -bv },
    { ax, -av, -az,  at,  bb, -ar, -bd,  ap, -be, -aq,  bc,  as, -ba, -au,  ay,  aw, -aw, -ay,  au,  ba, -as, -bc,  aq,  be, -ap,  bd,  ar, -bb, -at,  az,  av, -ax, -ax,  av,  az, -at, -bb,  ar,  bd, -ap,  be,  aq, -bc, -as,  ba,  au, -ay, -aw,  aw,  ay, -au, -ba,  as,  bc, -aq, -be,  ap, -bd, -ar,  bb,  at, -az, -av,  ax },
    { bw, -bq, -cc,  bk,  ci, -bf,  ch,  bl, -cb, -br,  bv,  bx, -bp, -cd,  bj,  cj, -bg,  cg,  bm, -ca, -bs,  bu,  by, -bo, -ce,  bi,  ck, -bh,  cf,  bn, -bz, -bt,  bt,  bz, -bn, -cf,  bh, -ck, -bi,  ce,  bo, -by, -bu,  bs,  ca, -bm, -cg,  bg, -cj, -bj,  cd,  bp, -bx, -bv,  br,  cb, -bl, -ch,  bf, -ci, -bk,  cc,  bq, -bw },
    { al, -aj, -an,  ah, -ao, -ai,  am,  ak, -ak, -am,  ai,  ao, -ah,  an,  aj, -al, -al,  aj,  an, -ah,  ao,  ai, -am, -ak,  ak,  am, -ai, -ao,  ah, -an, -aj,  al,  al, -aj, -an,  ah, -ao, -ai,  am,  ak, -ak, -am,  ai,  ao, -ah,  an,  aj, -al, -al,  aj,  an, -ah,  ao,  ai, -am, -ak,  ak,  am, -ai, -ao,  ah, -an, -aj,  al },
    { bx, -bn, -ch,  bg, -ce, -bq,  bu,  ca, -bk, -ck,  bj, -cb, -bt,  br,  cd, -bh,  ci,  bm, -by, -bw,  bo,  cg, -bf,  cf,  bp, -bv, -bz,  bl,  cj, -bi,  cc,  bs, -bs, -cc,  bi, -cj, -bl,  bz,  bv, -bp, -cf,  bf, -cg, -bo,  bw,  by, -bm, -ci,  bh, -cd, -br,  bt,  cb, -bj,  ck,  bk, -ca, -bu,  bq,  ce, -bg,  ch,  bn, -bx },
    { ay, -as, -be,  ar, -az, -ax,  at,  bd, -aq,  ba,  aw, -au, -bc,  ap, -bb, -av,  av,  bb, -ap,  bc,  au, -aw, -ba,  aq, -bd, -at,  ax,  az, -ar,  be,  as, -ay, -ay,  as,  be, -ar,  az,  ax, -at, -bd,  aq, -ba, -aw,  au,  bc, -ap,  bb,  av, -av, -bb,  ap, -bc, -au,  aw,  ba, -aq,  bd,  at, -ax, -az,  ar, -be, -as,  ay },
    { by, -bk,  cj,  bn, -bv, -cb,  bh, -cg, -bq,  bs,  ce, -bf,  cd,  bt, -bp, -ch,  bi, -ca, -bw,  bm,  ck, -bl,  bx,  bz, -bj,  ci,  bo, -bu, -cc,  bg, -cf, -br,  br,  cf, -bg,  cc,  bu, -bo, -ci,  bj, -bz, -bx,  bl, -ck, -bm,  bw,  ca, -bi,  ch,  bp, -bt, -cd,  bf, -ce, -bs,  bq,  cg, -bh,  cb,  bv, -bn, -cj,  bk, -by },
    { af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af,  af, -ad,  ag,  ae, -ae, -ag,  ad, -af, -af,  ad, -ag, -ae,  ae,  ag, -ad,  af },
    { bz, -bh,  ce,  bu, -bm,  cj,  bp, -br, -ch,  bk, -bw, -cc,  bf, -cb, -bx,  bj, -cg, -bs,  bo,  ck, -bn,  bt,  cf, -bi,  by,  ca, -bg,  cd,  bv, -bl,  ci,  bq, -bq, -ci,  bl, -bv, -cd,  bg, -ca, -by,  bi, -cf, -bt,  bn, -ck, -bo,  bs,  cg, -bj,  bx,  cb, -bf,  cc,  bw, -bk,  ch,  br, -bp, -cj,  bm, -bu, -ce,  bh, -bz },
    { az, -ap,  ba,  ay, -aq,  bb,  ax, -ar,  bc,  aw, -as,  bd,  av, -at,  be,  au, -au, -be,  at, -av, -bd,  as, -aw, -bc,  ar, -ax, -bb,  aq, -ay, -ba,  ap, -az, -az,  ap, -ba, -ay,  aq, -bb, -ax,  ar, -bc, -aw,  as, -bd, -av,  at, -be, -au,  au,  be, -at,  av,  bd, -as,  aw,  bc, -ar,  ax,  bb, -aq,  ay,  ba, -ap,  az },
    { ca, -bf,  bz,  cb, -bg,  by,  cc, -bh,  bx,  cd, -bi,  bw,  ce, -bj,  bv,  cf, -bk,  bu,  cg, -bl,  bt,  ch, -bm,  bs,  ci, -bn,  br,  cj, -bo,  bq,  ck, -bp,  bp, -ck, -bq,  bo, -cj, -br,  bn, -ci, -bs,  bm, -ch, -bt,  bl, -cg, -bu,  bk, -cf, -bv,  bj, -ce, -bw,  bi, -cd, -bx,  bh, -cc, -by,  bg, -cb, -bz,  bf, -ca },
    { am, -ah,  al,  an, -ai,  ak,  ao, -aj,  aj, -ao, -ak,  ai, -an, -al,  ah, -am, -am,  ah, -al, -an,  ai, -ak, -ao,  aj, -aj,  ao,  ak, -ai,  an,  al, -ah,  am,  am, -ah,  al,  an, -ai,  ak,  ao, -aj,  aj, -ao, -ak,  ai, -an, -al,  ah, -am, -am,  ah, -al, -an,  ai, -ak, -ao,  aj, -aj,  ao,  ak, -ai,  an,  al, -ah,  am },
    { cb, -bi,  bu,  ci, -bp,  bn, -cg, -bw,  bg, -bz, -cd,  bk, -bs, -ck,  br, -bl,  ce,  by, -bf,  bx,  cf, -bm,  bq, -cj, -bt,  bj, -cc, -ca,  bh, -bv, -ch,  bo, -bo,  ch,  bv, -bh,  ca,  cc, -bj,  bt,  cj, -bq,  bm, -cf, -bx,  bf, -by, -ce,  bl, -br,  ck,  bs, -bk,  cd,  bz, -bg,  bw,  cg, -bn,  bp, -ci, -bu,  bi, -cb },
    { ba, -ar,  av, -be, -aw,  aq, -az, -bb,  as, -au,  bd,  ax, -ap,  ay,  bc, -at,  at, -bc, -ay,  ap, -ax, -bd,  au, -as,  bb,  az, -aq,  aw,  be, -av,  ar, -ba, -ba,  ar, -av,  be,  aw, -aq,  az,  bb, -as,  au, -bd, -ax,  ap, -ay, -bc,  at, -at,  bc,  ay, -ap,  ax,  bd, -au,  as, -bb, -az,  aq, -aw, -be,  av, -ar,  ba },
    { cc, -bl,  bp, -cg, -by,  bh, -bt,  ck,  bu, -bg,  bx,  ch, -bq,  bk, -cb, -cd,  bm, -bo,  cf,  bz, -bi,  bs, -cj, -bv,  bf, -bw, -ci,  br, -bj,  ca,  ce, -bn,  bn, -ce, -ca,  bj, -br,  ci,  bw, -bf,  bv,  cj, -bs,  bi, -bz, -cf,  bo, -bm,  cd,  cb, -bk,  bq, -ch, -bx,  bg, -bu, -ck,  bt, -bh,  by,  cg, -bp,  bl, -cc },
    { ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac,  ac, -ab,  ab, -ac, -ac,  ab, -ab,  ac },
    { cd, -bo,  bk, -bz, -ch,  bs, -bg,  bv, -ck, -bw,  bh, -br,  cg,  ca, -bl,  bn, -cc, -ce,  bp, -bj,  by,  ci, -bt,  bf, -bu,  cj,  bx, -bi,  bq, -cf, -cb,  bm, -bm,  cb,  cf, -bq,  bi, -bx, -cj,  bu, -bf,  bt, -ci, -by,  bj, -bp,  ce,  cc, -bn,  bl, -ca, -cg,  br, -bh,  bw,  ck, -bv,  bg, -bs,  ch,  bz, -bk,  bo, -cd },
    { bb, -au,  aq, -ax,  be,  ay, -ar,  at, -ba, -bc,  av, -ap,  aw, -bd, -az,  as, -as,  az,  bd, -aw,  ap, -av,  bc,  ba, -at,  ar, -ay, -be,  ax, -aq,  au, -bb, -bb,  au, -aq,  ax, -be, -ay,  ar, -at,  ba,  bc, -av,  ap, -aw,  bd,  az, -as,  as, -az, -bd,  aw, -ap,  av, -bc, -ba,  at, -ar,  ay,  be, -ax,  aq, -au,  bb },
    { ce, -br,  bf, -bs,  cf,  cd, -bq,  bg, -bt,  cg,  cc, -bp,  bh, -bu,  ch,  cb, -bo,  bi, -bv,  ci,  ca, -bn,  bj, -bw,  cj,  bz, -bm,  bk, -bx,  ck,  by, -bl,  bl, -by, -ck,  bx, -bk,  bm, -bz, -cj,  bw, -bj,  bn, -ca, -ci,  bv, -bi,  bo, -cb, -ch,  bu, -bh,  bp, -cc, -cg,  bt, -bg,  bq, -cd, -cf,  bs, -bf,  br, -ce },
    { an, -ak,  ah, -aj,  am,  ao, -al,  ai, -ai,  al, -ao, -am,  aj, -ah,  ak, -an, -an,  ak, -ah,  aj, -am, -ao,  al, -ai,  ai, -al,  ao,  am, -aj,  ah, -ak,  an,  an, -ak,  ah, -aj,  am,  ao, -al,  ai, -ai,  al, -ao, -am,  aj, -ah,  ak, -an, -an,  ak, -ah,  aj, -am, -ao,  al, -ai,  ai, -al,  ao,  am, -aj,  ah, -ak,  an },
    { cf, -bu,  bj, -bl,  bw, -ch, -cd,  bs, -bh,  bn, -by,  cj,  cb, -bq,  bf, -bp,  ca,  ck, -bz,  bo, -bg,  br, -cc, -ci,  bx, -bm,  bi, -bt,  ce,  cg, -bv,  bk, -bk,  bv, -cg, -ce,  bt, -bi,  bm, -bx,  ci,  cc, -br,  bg, -bo,  bz, -ck, -ca,  bp, -bf,  bq, -cb, -cj,  by, -bn,  bh, -bs,  cd,  ch, -bw,  bl, -bj,  bu, -cf },
    { bc, -ax,  as, -aq,  av, -ba, -be,  az, -au,  ap, -at,  ay, -bd, -bb,  aw, -ar,  ar, -aw,  bb,  bd, -ay,  at, -ap,  au, -az,  be,  ba, -av,  aq, -as,  ax, -bc, -bc,  ax, -as,  aq, -av,  ba,  be, -az,  au, -ap,  at, -ay,  bd,  bb, -aw,  ar, -ar,  aw, -bb, -bd,  ay, -at,  ap, -au,  az, -be, -ba,  av, -aq,  as, -ax,  bc },
    { cg, -bx,  bo, -bf,  bn, -bw,  cf,  ch, -by,  bp, -bg,  bm, -bv,  ce,  ci, -bz,  bq, -bh,  bl, -bu,  cd,  cj, -ca,  br, -bi,  bk, -bt,  cc,  ck, -cb,  bs, -bj,  bj, -bs,  cb, -ck, -cc,  bt, -bk,  bi, -br,  ca, -cj, -cd,  bu, -bl,  bh, -bq,  bz, -ci, -ce,  bv, -bm,  bg, -bp,  by, -ch, -cf,  bw, -bn,  bf, -bo,  bx, -cg },
    { ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag,  ag, -af,  ae, -ad,  ad, -ae,  af, -ag, -ag,  af, -ae,  ad, -ad,  ae, -af,  ag },
    { ch, -ca,  bt, -bm,  bf, -bl,  bs, -bz,  cg,  ci, -cb,  bu, -bn,  bg, -bk,  br, -by,  cf,  cj, -cc,  bv, -bo,  bh, -bj,  bq, -bx,  ce,  ck, -cd,  bw, -bp,  bi, -bi,  bp, -bw,  cd, -ck, -ce,  bx, -bq,  bj, -bh,  bo, -bv,  cc, -cj, -cf,  by, -br,  bk, -bg,  bn, -bu,  cb, -ci, -cg,  bz, -bs,  bl, -bf,  bm, -bt,  ca, -ch },
    { bd, -ba,  ax, -au,  ar, -ap,  as, -av,  ay, -bb,  be,  bc, -az,  aw, -at,  aq, -aq,  at, -aw,  az, -bc, -be,  bb, -ay,  av, -as,  ap, -ar,  au, -ax,  ba, -bd, -bd,  ba, -ax,  au, -ar,  ap, -as,  av, -ay,  bb, -be, -bc,  az, -aw,  at, -aq,  aq, -at,  aw, -az,  bc,  be, -bb,  ay, -av,  as, -ap,  ar, -au,  ax, -ba,  bd },
    { ci, -cd,  by, -bt,  bo, -bj,  bf, -bk,  bp, -bu,  bz, -ce,  cj,  ch, -cc,  bx, -bs,  bn, -bi,  bg, -bl,  bq, -bv,  ca, -cf,  ck,  cg, -cb,  bw, -br,  bm, -bh,  bh, -bm,  br, -bw,  cb, -cg, -ck,  cf, -ca,  bv, -bq,  bl, -bg,  bi, -bn,  bs, -bx,  cc, -ch, -cj,  ce, -bz,  bu, -bp,  bk, -bf,  bj, -bo,  bt, -by,  cd, -ci },
    { ao, -an,  am, -al,  ak, -aj,  ai, -ah,  ah, -ai,  aj, -ak,  al, -am,  an, -ao, -ao,  an, -am,  al, -ak,  aj, -ai,  ah, -ah,  ai, -aj,  ak, -al,  am, -an,  ao,  ao, -an,  am, -al,  ak, -aj,  ai, -ah,  ah, -ai,  aj, -ak,  al, -am,  an, -ao, -ao,  an, -am,  al, -ak,  aj, -ai,  ah, -ah,  ai, -aj,  ak, -al,  am, -an,  ao },
    { cj, -cg,  cd, -ca,  bx, -bu,  br, -bo,  bl, -bi,  bf, -bh,  bk, -bn,  bq, -bt,  bw, -bz,  cc, -cf,  ci,  ck, -ch,  ce, -cb,  by, -bv,  bs, -bp,  bm, -bj,  bg, -bg,  bj, -bm,  bp, -bs,  bv, -by,  cb, -ce,  ch, -ck, -ci,  cf, -cc,  bz, -bw,  bt, -bq,  bn, -bk,  bh, -bf,  bi, -bl,  bo, -br,  bu, -bx,  ca, -cd,  cg, -cj },
    { be, -bd,  bc, -bb,  ba, -az,  ay, -ax,  aw, -av,  au, -at,  as, -ar,  aq, -ap,  ap, -aq,  ar, -as,  at, -au,  av, -aw,  ax, -ay,  az, -ba,  bb, -bc,  bd, -be, -be,  bd, -bc,  bb, -ba,  az, -ay,  ax, -aw,  av, -au,  at, -as,  ar, -aq,  ap, -ap,  aq, -ar,  as, -at,  au, -av,  aw, -ax,  ay, -az,  ba, -bb,  bc, -bd,  be },
    { ck, -cj,  ci, -ch,  cg, -cf,  ce, -cd,  cc, -cb,  ca, -bz,  by, -bx,  bw, -bv,  bu, -bt,  bs, -br,  bq, -bp,  bo, -bn,  bm, -bl,  bk, -bj,  bi, -bh,  bg, -bf,  bf, -bg,  bh, -bi,  bj, -bk,  bl, -bm,  bn, -bo,  bp, -bq,  br, -bs,  bt, -bu,  bv, -bw,  bx, -by,  bz, -ca,  cb, -cc,  cd, -ce,  cf, -cg,  ch, -ci,  cj, -ck },
}
 */

void ff_vvc_inv_dct2_64(int *out, const ptrdiff_t out_stride, const int *in, ptrdiff_t in_stride)
{
    const int aa = 64, ab = 83, ac = 36, ad = 89, ae = 75, af = 50, ag = 18, ah = 90;
    const int ai = 87, aj = 80, ak = 70, al = 57, am = 43, an = 25, ao =  9, ap = 90;
    const int aq = 90, ar = 88, as = 85, at = 82, au = 78, av = 73, aw = 67, ax = 61;
    const int ay = 54, az = 46, ba = 38, bb = 31, bc = 22, bd = 13, be =  4, bf = 91;
    const int bg = 90, bh = 90, bi = 90, bj = 88, bk = 87, bl = 86, bm = 84, bn = 83;
    const int bo = 81, bp = 79, bq = 77, br = 73, bs = 71, bt = 69, bu = 65, bv = 62;
    const int bw = 59, bx = 56, by = 52, bz = 48, ca = 44, cb = 41, cc = 37, cd = 33;
    const int ce = 28, cf = 24, cg = 20, ch = 15, ci = 11, cj =  7, ck =  2;
    const int x0  = in[0  * in_stride], x1  = in[1  * in_stride];
    const int x2  = in[2  * in_stride], x3  = in[3  * in_stride];
    const int x4  = in[4  * in_stride], x5  = in[5  * in_stride];
    const int x6  = in[6  * in_stride], x7  = in[7  * in_stride];
    const int x8  = in[8  * in_stride], x9  = in[9  * in_stride];
    const int x10 = in[10 * in_stride], x11 = in[11 * in_stride];
    const int x12 = in[12 * in_stride], x13 = in[13 * in_stride];
    const int x14 = in[14 * in_stride], x15 = in[15 * in_stride];
    const int x16 = in[16 * in_stride], x17 = in[17 * in_stride];
    const int x18 = in[18 * in_stride], x19 = in[19 * in_stride];
    const int x20 = in[20 * in_stride], x21 = in[21 * in_stride];
    const int x22 = in[22 * in_stride], x23 = in[23 * in_stride];
    const int x24 = in[24 * in_stride], x25 = in[25 * in_stride];
    const int x26 = in[26 * in_stride], x27 = in[27 * in_stride];
    const int x28 = in[28 * in_stride], x29 = in[29 * in_stride];
    const int x30 = in[30 * in_stride], x31 = in[31 * in_stride];
    const int x32 = in[32 * in_stride], x33 = in[33 * in_stride];
    const int x34 = in[34 * in_stride], x35 = in[35 * in_stride];
    const int x36 = in[36 * in_stride], x37 = in[37 * in_stride];
    const int x38 = in[38 * in_stride], x39 = in[39 * in_stride];
    const int x40 = in[40 * in_stride], x41 = in[41 * in_stride];
    const int x42 = in[42 * in_stride], x43 = in[43 * in_stride];
    const int x44 = in[44 * in_stride], x45 = in[45 * in_stride];
    const int x46 = in[46 * in_stride], x47 = in[47 * in_stride];
    const int x48 = in[48 * in_stride], x49 = in[49 * in_stride];
    const int x50 = in[50 * in_stride], x51 = in[51 * in_stride];
    const int x52 = in[52 * in_stride], x53 = in[53 * in_stride];
    const int x54 = in[54 * in_stride], x55 = in[55 * in_stride];
    const int x56 = in[56 * in_stride], x57 = in[57 * in_stride];
    const int x58 = in[58 * in_stride], x59 = in[59 * in_stride];
    const int x60 = in[60 * in_stride], x61 = in[61 * in_stride];
    const int x62 = in[62 * in_stride], x63 = in[63 * in_stride];
    const int EEEEE[2] = {
        aa * (x0 + x32),
        aa * (x0 - x32),
    };
    const int EEEEO[2] = {
        ab * x16 + ac * x48,
        ac * x16 - ab * x48,
    };
    const int EEEE[4] = {
        EEEEE[0] + EEEEO[0], EEEEE[1] + EEEEO[1],
        EEEEE[1] - EEEEO[1], EEEEE[0] - EEEEO[0],
    };
    const int EEEO[4] = {
        ad * x8  + ae * x24 + af * x40 + ag * x56,
        ae * x8  - ag * x24 - ad * x40 - af * x56,
        af * x8  - ad * x24 + ag * x40 + ae * x56,
        ag * x8  - af * x24 + ae * x40 - ad * x56,
    };
    const int EEE[8] = {
        EEEE[0] + EEEO[0], EEEE[1] + EEEO[1], EEEE[2] + EEEO[2], EEEE[3] + EEEO[3],
        EEEE[3] - EEEO[3], EEEE[2] - EEEO[2], EEEE[1] - EEEO[1], EEEE[0] - EEEO[0],
    };
    const int EEO[8] = {
        ah * x4 + ai * x12 + aj * x20 + ak * x28 + al * x36 + am * x44 + an * x52 + ao * x60,
        ai * x4 + al * x12 + ao * x20 - am * x28 - aj * x36 - ah * x44 - ak * x52 - an * x60,
        aj * x4 + ao * x12 - ak * x20 - ai * x28 - an * x36 + al * x44 + ah * x52 + am * x60,
        ak * x4 - am * x12 - ai * x20 + ao * x28 + ah * x36 + an * x44 - aj * x52 - al * x60,
        al * x4 - aj * x12 - an * x20 + ah * x28 - ao * x36 - ai * x44 + am * x52 + ak * x60,
        am * x4 - ah * x12 + al * x20 + an * x28 - ai * x36 + ak * x44 + ao * x52 - aj * x60,
        an * x4 - ak * x12 + ah * x20 - aj * x28 + am * x36 + ao * x44 - al * x52 + ai * x60,
        ao * x4 - an * x12 + am * x20 - al * x28 + ak * x36 - aj * x44 + ai * x52 - ah * x60,
    };
    const int EE[16] = {
        EEE[0] + EEO[0], EEE[1] + EEO[1], EEE[2] + EEO[2], EEE[3] + EEO[3], EEE[4] + EEO[4], EEE[5] + EEO[5], EEE[6] + EEO[6], EEE[7] + EEO[7],
        EEE[7] - EEO[7], EEE[6] - EEO[6], EEE[5] - EEO[5], EEE[4] - EEO[4], EEE[3] - EEO[3], EEE[2] - EEO[2], EEE[1] - EEO[1], EEE[0] - EEO[0],
    };
    const int EO[16] = {
        ap * x2 + aq * x6 + ar * x10 + as * x14 + at * x18 + au * x22 + av * x26 + aw * x30 + ax * x34 + ay * x38 + az * x42 + ba * x46 + bb * x50 + bc * x54 + bd * x58 + be * x62,
        aq * x2 + at * x6 + aw * x10 + az * x14 + bc * x18 - be * x22 - bb * x26 - ay * x30 - av * x34 - as * x38 - ap * x42 - ar * x46 - au * x50 - ax * x54 - ba * x58 - bd * x62,
        ar * x2 + aw * x6 + bb * x10 - bd * x14 - ay * x18 - at * x22 - ap * x26 - au * x30 - az * x34 - be * x38 + ba * x42 + av * x46 + aq * x50 + as * x54 + ax * x58 + bc * x62,
        as * x2 + az * x6 - bd * x10 - aw * x14 - ap * x18 - av * x22 - bc * x26 + ba * x30 + at * x34 + ar * x38 + ay * x42 - be * x46 - ax * x50 - aq * x54 - au * x58 - bb * x62,
        at * x2 + bc * x6 - ay * x10 - ap * x14 - ax * x18 + bd * x22 + au * x26 + as * x30 + bb * x34 - az * x38 - aq * x42 - aw * x46 + be * x50 + av * x54 + ar * x58 + ba * x62,
        au * x2 - be * x6 - at * x10 - av * x14 + bd * x18 + as * x22 + aw * x26 - bc * x30 - ar * x34 - ax * x38 + bb * x42 + aq * x46 + ay * x50 - ba * x54 - ap * x58 - az * x62,
        av * x2 - bb * x6 - ap * x10 - bc * x14 + au * x18 + aw * x22 - ba * x26 - aq * x30 - bd * x34 + at * x38 + ax * x42 - az * x46 - ar * x50 - be * x54 + as * x58 + ay * x62,
        aw * x2 - ay * x6 - au * x10 + ba * x14 + as * x18 - bc * x22 - aq * x26 + be * x30 + ap * x34 + bd * x38 - ar * x42 - bb * x46 + at * x50 + az * x54 - av * x58 - ax * x62,
        ax * x2 - av * x6 - az * x10 + at * x14 + bb * x18 - ar * x22 - bd * x26 + ap * x30 - be * x34 - aq * x38 + bc * x42 + as * x46 - ba * x50 - au * x54 + ay * x58 + aw * x62,
        ay * x2 - as * x6 - be * x10 + ar * x14 - az * x18 - ax * x22 + at * x26 + bd * x30 - aq * x34 + ba * x38 + aw * x42 - au * x46 - bc * x50 + ap * x54 - bb * x58 - av * x62,
        az * x2 - ap * x6 + ba * x10 + ay * x14 - aq * x18 + bb * x22 + ax * x26 - ar * x30 + bc * x34 + aw * x38 - as * x42 + bd * x46 + av * x50 - at * x54 + be * x58 + au * x62,
        ba * x2 - ar * x6 + av * x10 - be * x14 - aw * x18 + aq * x22 - az * x26 - bb * x30 + as * x34 - au * x38 + bd * x42 + ax * x46 - ap * x50 + ay * x54 + bc * x58 - at * x62,
        bb * x2 - au * x6 + aq * x10 - ax * x14 + be * x18 + ay * x22 - ar * x26 + at * x30 - ba * x34 - bc * x38 + av * x42 - ap * x46 + aw * x50 - bd * x54 - az * x58 + as * x62,
        bc * x2 - ax * x6 + as * x10 - aq * x14 + av * x18 - ba * x22 - be * x26 + az * x30 - au * x34 + ap * x38 - at * x42 + ay * x46 - bd * x50 - bb * x54 + aw * x58 - ar * x62,
        bd * x2 - ba * x6 + ax * x10 - au * x14 + ar * x18 - ap * x22 + as * x26 - av * x30 + ay * x34 - bb * x38 + be * x42 + bc * x46 - az * x50 + aw * x54 - at * x58 + aq * x62,
        be * x2 - bd * x6 + bc * x10 - bb * x14 + ba * x18 - az * x22 + ay * x26 - ax * x30 + aw * x34 - av * x38 + au * x42 - at * x46 + as * x50 - ar * x54 + aq * x58 - ap * x62,
    };
    const int E[32] = {
        EE[0]  + EO[0],  EE[1]  + EO[1],  EE[2]  + EO[2],  EE[3]  + EO[3],  EE[4]  + EO[4],  EE[5]  + EO[5],  EE[6] + EO[6], EE[7] + EO[7], EE[8] + EO[8], EE[9] + EO[9], EE[10] + EO[10], EE[11] + EO[11], EE[12] + EO[12], EE[13] + EO[13], EE[14] + EO[14], EE[15] + EO[15],
        EE[15] - EO[15], EE[14] - EO[14], EE[13] - EO[13], EE[12] - EO[12], EE[11] - EO[11], EE[10] - EO[10], EE[9] - EO[9], EE[8] - EO[8], EE[7] - EO[7], EE[6] - EO[6], EE[5]  - EO[5],  EE[4]  - EO[4],  EE[3]  - EO[3],  EE[2]  - EO[2],  EE[1]  - EO[1],  EE[0]  - EO[0],
    };
    const int O[32] = {
        bf * x1 + bg * x3 + bh * x5 + bi * x7 + bj * x9 + bk * x11 + bl * x13 + bm * x15 + bn * x17 + bo * x19 + bp * x21 + bq * x23 +  br * x25 + bs * x27 + bt * x29 + bu * x31 + bv * x33 + bw * x35 + bx * x37 + by * x39 + bz * x41 + ca * x43 + cb * x45 + cc * x47 + cd * x49 + ce * x51 + cf * x53 + cg * x55 + ch * x57 + ci * x59 + cj * x61 + ck * x63,
        bg * x1 + bj * x3 + bm * x5 + bp * x7 + bs * x9 + bv * x11 + by * x13 + cb * x15 + ce * x17 + ch * x19 + ck * x21 - ci * x23 + -cf * x25 - cc * x27 - bz * x29 - bw * x31 - bt * x33 - bq * x35 - bn * x37 - bk * x39 - bh * x41 - bf * x43 - bi * x45 - bl * x47 - bo * x49 - br * x51 - bu * x53 - bx * x55 - ca * x57 - cd * x59 - cg * x61 - cj * x63,
        bh * x1 + bm * x3 + br * x5 + bw * x7 + cb * x9 + cg * x11 - ck * x13 - cf * x15 - ca * x17 - bv * x19 - bq * x21 - bl * x23 + -bg * x25 - bi * x27 - bn * x29 - bs * x31 - bx * x33 - cc * x35 - ch * x37 + cj * x39 + ce * x41 + bz * x43 + bu * x45 + bp * x47 + bk * x49 + bf * x51 + bj * x53 + bo * x55 + bt * x57 + by * x59 + cd * x61 + ci * x63,
        bi * x1 + bp * x3 + bw * x5 + cd * x7 + ck * x9 - ce * x11 - bx * x13 - bq * x15 - bj * x17 - bh * x19 - bo * x21 - bv * x23 + -cc * x25 - cj * x27 + cf * x29 + by * x31 + br * x33 + bk * x35 + bg * x37 + bn * x39 + bu * x41 + cb * x43 + ci * x45 - cg * x47 - bz * x49 - bs * x51 - bl * x53 - bf * x55 - bm * x57 - bt * x59 - ca * x61 - ch * x63,
        bj * x1 + bs * x3 + cb * x5 + ck * x7 - cc * x9 - bt * x11 - bk * x13 - bi * x15 - br * x17 - ca * x19 - cj * x21 + cd * x23 +  bu * x25 + bl * x27 + bh * x29 + bq * x31 + bz * x33 + ci * x35 - ce * x37 - bv * x39 - bm * x41 - bg * x43 - bp * x45 - by * x47 - ch * x49 + cf * x51 + bw * x53 + bn * x55 + bf * x57 + bo * x59 + bx * x61 + cg * x63,
        bk * x1 + bv * x3 + cg * x5 - ce * x7 - bt * x9 - bi * x11 - bm * x13 - bx * x15 - ci * x17 + cc * x19 + br * x21 + bg * x23 +  bo * x25 + bz * x27 + ck * x29 - ca * x31 - bp * x33 - bf * x35 - bq * x37 - cb * x39 + cj * x41 + by * x43 + bn * x45 + bh * x47 + bs * x49 + cd * x51 - ch * x53 - bw * x55 - bl * x57 - bj * x59 - bu * x61 - cf * x63,
        bl * x1 + by * x3 - ck * x5 - bx * x7 - bk * x9 - bm * x11 - bz * x13 + cj * x15 + bw * x17 + bj * x19 + bn * x21 + ca * x23 + -ci * x25 - bv * x27 - bi * x29 - bo * x31 - cb * x33 + ch * x35 + bu * x37 + bh * x39 + bp * x41 + cc * x43 - cg * x45 - bt * x47 - bg * x49 - bq * x51 - cd * x53 + cf * x55 + bs * x57 + bf * x59 + br * x61 + ce * x63,
        bm * x1 + cb * x3 - cf * x5 - bq * x7 - bi * x9 - bx * x11 + cj * x13 + bu * x15 + bf * x17 + bt * x19 + ci * x21 - by * x23 + -bj * x25 - bp * x27 - ce * x29 + cc * x31 + bn * x33 + bl * x35 + ca * x37 - cg * x39 - br * x41 - bh * x43 - bw * x45 + ck * x47 + bv * x49 + bg * x51 + bs * x53 + ch * x55 - bz * x57 - bk * x59 - bo * x61 - cd * x63,
        bn * x1 + ce * x3 - ca * x5 - bj * x7 - br * x9 - ci * x11 + bw * x13 + bf * x15 + bv * x17 - cj * x19 - bs * x21 - bi * x23 + -bz * x25 + cf * x27 + bo * x29 + bm * x31 + cd * x33 - cb * x35 - bk * x37 - bq * x39 - ch * x41 + bx * x43 + bg * x45 + bu * x47 - ck * x49 - bt * x51 - bh * x53 - by * x55 + cg * x57 + bp * x59 + bl * x61 + cc * x63,
        bo * x1 + ch * x3 - bv * x5 - bh * x7 - ca * x9 + cc * x11 + bj * x13 + bt * x15 - cj * x17 - bq * x19 - bm * x21 - cf * x23 +  bx * x25 + bf * x27 + by * x29 - ce * x31 - bl * x33 - br * x35 - ck * x37 + bs * x39 + bk * x41 + cd * x43 - bz * x45 - bg * x47 - bw * x49 + cg * x51 + bn * x53 + bp * x55 + ci * x57 - bu * x59 - bi * x61 - cb * x63,
        bp * x1 + ck * x3 - bq * x5 - bo * x7 - cj * x9 + br * x11 + bn * x13 + ci * x15 - bs * x17 - bm * x19 - ch * x21 + bt * x23 +  bl * x25 + cg * x27 - bu * x29 - bk * x31 - cf * x33 + bv * x35 + bj * x37 + ce * x39 - bw * x41 - bi * x43 - cd * x45 + bx * x47 + bh * x49 + cc * x51 - by * x53 - bg * x55 - cb * x57 + bz * x59 + bf * x61 + ca * x63,
        bq * x1 - ci * x3 - bl * x5 - bv * x7 + cd * x9 + bg * x11 + ca * x13 - by * x15 - bi * x17 - cf * x19 + bt * x21 + bn * x23 +  ck * x25 - bo * x27 - bs * x29 + cg * x31 + bj * x33 + bx * x35 - cb * x37 - bf * x39 - cc * x41 + bw * x43 + bk * x45 + ch * x47 - br * x49 - bp * x51 + cj * x53 + bm * x55 + bu * x57 - ce * x59 - bh * x61 - bz * x63,
        br * x1 - cf * x3 - bg * x5 - cc * x7 + bu * x9 + bo * x11 - ci * x13 - bj * x15 - bz * x17 + bx * x19 + bl * x21 + ck * x23 + -bm * x25 - bw * x27 + ca * x29 + bi * x31 + ch * x33 - bp * x35 - bt * x37 + cd * x39 + bf * x41 + ce * x43 - bs * x45 - bq * x47 + cg * x49 + bh * x51 + cb * x53 - bv * x55 - bn * x57 + cj * x59 + bk * x61 + by * x63,
        bs * x1 - cc * x3 - bi * x5 - cj * x7 + bl * x9 + bz * x11 - bv * x13 - bp * x15 + cf * x17 + bf * x19 + cg * x21 - bo * x23 + -bw * x25 + by * x27 + bm * x29 - ci * x31 - bh * x33 - cd * x35 + br * x37 + bt * x39 - cb * x41 - bj * x43 - ck * x45 + bk * x47 + ca * x49 - bu * x51 - bq * x53 + ce * x55 + bg * x57 + ch * x59 - bn * x61 - bx * x63,
        bt * x1 - bz * x3 - bn * x5 + cf * x7 + bh * x9 + ck * x11 - bi * x13 - ce * x15 + bo * x17 + by * x19 - bu * x21 - bs * x23 +  ca * x25 + bm * x27 - cg * x29 - bg * x31 - cj * x33 + bj * x35 + cd * x37 - bp * x39 - bx * x41 + bv * x43 + br * x45 - cb * x47 - bl * x49 + ch * x51 + bf * x53 + ci * x55 - bk * x57 - cc * x59 + bq * x61 + bw * x63,
        bu * x1 - bw * x3 - bs * x5 + by * x7 + bq * x9 - ca * x11 - bo * x13 + cc * x15 + bm * x17 - ce * x19 - bk * x21 + cg * x23 +  bi * x25 - ci * x27 - bg * x29 + ck * x31 + bf * x33 + cj * x35 - bh * x37 - ch * x39 + bj * x41 + cf * x43 - bl * x45 - cd * x47 + bn * x49 + cb * x51 - bp * x53 - bz * x55 + br * x57 + bx * x59 - bt * x61 - bv * x63,
        bv * x1 - bt * x3 - bx * x5 + br * x7 + bz * x9 - bp * x11 - cb * x13 + bn * x15 + cd * x17 - bl * x19 - cf * x21 + bj * x23 +  ch * x25 - bh * x27 - cj * x29 + bf * x31 - ck * x33 - bg * x35 + ci * x37 + bi * x39 - cg * x41 - bk * x43 + ce * x45 + bm * x47 - cc * x49 - bo * x51 + ca * x53 + bq * x55 - by * x57 - bs * x59 + bw * x61 + bu * x63,
        bw * x1 - bq * x3 - cc * x5 + bk * x7 + ci * x9 - bf * x11 + ch * x13 + bl * x15 - cb * x17 - br * x19 + bv * x21 + bx * x23 + -bp * x25 - cd * x27 + bj * x29 + cj * x31 - bg * x33 + cg * x35 + bm * x37 - ca * x39 - bs * x41 + bu * x43 + by * x45 - bo * x47 - ce * x49 + bi * x51 + ck * x53 - bh * x55 + cf * x57 + bn * x59 - bz * x61 - bt * x63,
        bx * x1 - bn * x3 - ch * x5 + bg * x7 - ce * x9 - bq * x11 + bu * x13 + ca * x15 - bk * x17 - ck * x19 + bj * x21 - cb * x23 + -bt * x25 + br * x27 + cd * x29 - bh * x31 + ci * x33 + bm * x35 - by * x37 - bw * x39 + bo * x41 + cg * x43 - bf * x45 + cf * x47 + bp * x49 - bv * x51 - bz * x53 + bl * x55 + cj * x57 - bi * x59 + cc * x61 + bs * x63,
        by * x1 - bk * x3 + cj * x5 + bn * x7 - bv * x9 - cb * x11 + bh * x13 - cg * x15 - bq * x17 + bs * x19 + ce * x21 - bf * x23 +  cd * x25 + bt * x27 - bp * x29 - ch * x31 + bi * x33 - ca * x35 - bw * x37 + bm * x39 + ck * x41 - bl * x43 + bx * x45 + bz * x47 - bj * x49 + ci * x51 + bo * x53 - bu * x55 - cc * x57 + bg * x59 - cf * x61 - br * x63,
        bz * x1 - bh * x3 + ce * x5 + bu * x7 - bm * x9 + cj * x11 + bp * x13 - br * x15 - ch * x17 + bk * x19 - bw * x21 - cc * x23 +  bf * x25 - cb * x27 - bx * x29 + bj * x31 - cg * x33 - bs * x35 + bo * x37 + ck * x39 - bn * x41 + bt * x43 + cf * x45 - bi * x47 + by * x49 + ca * x51 - bg * x53 + cd * x55 + bv * x57 - bl * x59 + ci * x61 + bq * x63,
        ca * x1 - bf * x3 + bz * x5 + cb * x7 - bg * x9 + by * x11 + cc * x13 - bh * x15 + bx * x17 + cd * x19 - bi * x21 + bw * x23 +  ce * x25 - bj * x27 + bv * x29 + cf * x31 - bk * x33 + bu * x35 + cg * x37 - bl * x39 + bt * x41 + ch * x43 - bm * x45 + bs * x47 + ci * x49 - bn * x51 + br * x53 + cj * x55 - bo * x57 + bq * x59 + ck * x61 - bp * x63,
        cb * x1 - bi * x3 + bu * x5 + ci * x7 - bp * x9 + bn * x11 - cg * x13 - bw * x15 + bg * x17 - bz * x19 - cd * x21 + bk * x23 + -bs * x25 - ck * x27 + br * x29 - bl * x31 + ce * x33 + by * x35 - bf * x37 + bx * x39 + cf * x41 - bm * x43 + bq * x45 - cj * x47 - bt * x49 + bj * x51 - cc * x53 - ca * x55 + bh * x57 - bv * x59 - ch * x61 + bo * x63,
        cc * x1 - bl * x3 + bp * x5 - cg * x7 - by * x9 + bh * x11 - bt * x13 + ck * x15 + bu * x17 - bg * x19 + bx * x21 + ch * x23 + -bq * x25 + bk * x27 - cb * x29 - cd * x31 + bm * x33 - bo * x35 + cf * x37 + bz * x39 - bi * x41 + bs * x43 - cj * x45 - bv * x47 + bf * x49 - bw * x51 - ci * x53 + br * x55 - bj * x57 + ca * x59 + ce * x61 - bn * x63,
        cd * x1 - bo * x3 + bk * x5 - bz * x7 - ch * x9 + bs * x11 - bg * x13 + bv * x15 - ck * x17 - bw * x19 + bh * x21 - br * x23 +  cg * x25 + ca * x27 - bl * x29 + bn * x31 - cc * x33 - ce * x35 + bp * x37 - bj * x39 + by * x41 + ci * x43 - bt * x45 + bf * x47 - bu * x49 + cj * x51 + bx * x53 - bi * x55 + bq * x57 - cf * x59 - cb * x61 + bm * x63,
        ce * x1 - br * x3 + bf * x5 - bs * x7 + cf * x9 + cd * x11 - bq * x13 + bg * x15 - bt * x17 + cg * x19 + cc * x21 - bp * x23 +  bh * x25 - bu * x27 + ch * x29 + cb * x31 - bo * x33 + bi * x35 - bv * x37 + ci * x39 + ca * x41 - bn * x43 + bj * x45 - bw * x47 + cj * x49 + bz * x51 - bm * x53 + bk * x55 - bx * x57 + ck * x59 + by * x61 - bl * x63,
        cf * x1 - bu * x3 + bj * x5 - bl * x7 + bw * x9 - ch * x11 - cd * x13 + bs * x15 - bh * x17 + bn * x19 - by * x21 + cj * x23 +  cb * x25 - bq * x27 + bf * x29 - bp * x31 + ca * x33 + ck * x35 - bz * x37 + bo * x39 - bg * x41 + br * x43 - cc * x45 - ci * x47 + bx * x49 - bm * x51 + bi * x53 - bt * x55 + ce * x57 + cg * x59 - bv * x61 + bk * x63,
        cg * x1 - bx * x3 + bo * x5 - bf * x7 + bn * x9 - bw * x11 + cf * x13 + ch * x15 - by * x17 + bp * x19 - bg * x21 + bm * x23 + -bv * x25 + ce * x27 + ci * x29 - bz * x31 + bq * x33 - bh * x35 + bl * x37 - bu * x39 + cd * x41 + cj * x43 - ca * x45 + br * x47 - bi * x49 + bk * x51 - bt * x53 + cc * x55 + ck * x57 - cb * x59 + bs * x61 - bj * x63,
        ch * x1 - ca * x3 + bt * x5 - bm * x7 + bf * x9 - bl * x11 + bs * x13 - bz * x15 + cg * x17 + ci * x19 - cb * x21 + bu * x23 + -bn * x25 + bg * x27 - bk * x29 + br * x31 - by * x33 + cf * x35 + cj * x37 - cc * x39 + bv * x41 - bo * x43 + bh * x45 - bj * x47 + bq * x49 - bx * x51 + ce * x53 + ck * x55 - cd * x57 + bw * x59 - bp * x61 + bi * x63,
        ci * x1 - cd * x3 + by * x5 - bt * x7 + bo * x9 - bj * x11 + bf * x13 - bk * x15 + bp * x17 - bu * x19 + bz * x21 - ce * x23 +  cj * x25 + ch * x27 - cc * x29 + bx * x31 - bs * x33 + bn * x35 - bi * x37 + bg * x39 - bl * x41 + bq * x43 - bv * x45 + ca * x47 - cf * x49 + ck * x51 + cg * x53 - cb * x55 + bw * x57 - br * x59 + bm * x61 - bh * x63,
        cj * x1 - cg * x3 + cd * x5 - ca * x7 + bx * x9 - bu * x11 + br * x13 - bo * x15 + bl * x17 - bi * x19 + bf * x21 - bh * x23 +  bk * x25 - bn * x27 + bq * x29 - bt * x31 + bw * x33 - bz * x35 + cc * x37 - cf * x39 + ci * x41 + ck * x43 - ch * x45 + ce * x47 - cb * x49 + by * x51 - bv * x53 + bs * x55 - bp * x57 + bm * x59 - bj * x61 + bg * x63,
        ck * x1 - cj * x3 + ci * x5 - ch * x7 + cg * x9 - cf * x11 + ce * x13 - cd * x15 + cc * x17 - cb * x19 + ca * x21 - bz * x23 +  by * x25 - bx * x27 + bw * x29 - bv * x31 + bu * x33 - bt * x35 + bs * x37 - br * x39 + bq * x41 - bp * x43 + bo * x45 - bn * x47 + bm * x49 - bl * x51 + bk * x53 - bj * x55 + bi * x57 - bh * x59 + bg * x61 - bf * x63,
    };

    out[0  * out_stride] = E[0 ] + O[0 ];
    out[1  * out_stride] = E[1 ] + O[1 ];
    out[2  * out_stride] = E[2 ] + O[2 ];
    out[3  * out_stride] = E[3 ] + O[3 ];
    out[4  * out_stride] = E[4 ] + O[4 ];
    out[5  * out_stride] = E[5 ] + O[5 ];
    out[6  * out_stride] = E[6 ] + O[6 ];
    out[7  * out_stride] = E[7 ] + O[7 ];
    out[8  * out_stride] = E[8 ] + O[8 ];
    out[9  * out_stride] = E[9 ] + O[9 ];
    out[10 * out_stride] = E[10] + O[10];
    out[11 * out_stride] = E[11] + O[11];
    out[12 * out_stride] = E[12] + O[12];
    out[13 * out_stride] = E[13] + O[13];
    out[14 * out_stride] = E[14] + O[14];
    out[15 * out_stride] = E[15] + O[15];
    out[16 * out_stride] = E[16] + O[16];
    out[17 * out_stride] = E[17] + O[17];
    out[18 * out_stride] = E[18] + O[18];
    out[19 * out_stride] = E[19] + O[19];
    out[20 * out_stride] = E[20] + O[20];
    out[21 * out_stride] = E[21] + O[21];
    out[22 * out_stride] = E[22] + O[22];
    out[23 * out_stride] = E[23] + O[23];
    out[24 * out_stride] = E[24] + O[24];
    out[25 * out_stride] = E[25] + O[25];
    out[26 * out_stride] = E[26] + O[26];
    out[27 * out_stride] = E[27] + O[27];
    out[28 * out_stride] = E[28] + O[28];
    out[29 * out_stride] = E[29] + O[29];
    out[30 * out_stride] = E[30] + O[30];
    out[31 * out_stride] = E[31] + O[31];
    out[32 * out_stride] = E[31] - O[31];
    out[33 * out_stride] = E[30] - O[30];
    out[34 * out_stride] = E[29] - O[29];
    out[35 * out_stride] = E[28] - O[28];
    out[36 * out_stride] = E[27] - O[27];
    out[37 * out_stride] = E[26] - O[26];
    out[38 * out_stride] = E[25] - O[25];
    out[39 * out_stride] = E[24] - O[24];
    out[40 * out_stride] = E[23] - O[23];
    out[41 * out_stride] = E[22] - O[22];
    out[42 * out_stride] = E[21] - O[21];
    out[43 * out_stride] = E[20] - O[20];
    out[44 * out_stride] = E[19] - O[19];
    out[45 * out_stride] = E[18] - O[18];
    out[46 * out_stride] = E[17] - O[17];
    out[47 * out_stride] = E[16] - O[16];
    out[48 * out_stride] = E[15] - O[15];
    out[49 * out_stride] = E[14] - O[14];
    out[50 * out_stride] = E[13] - O[13];
    out[51 * out_stride] = E[12] - O[12];
    out[52 * out_stride] = E[11] - O[11];
    out[53 * out_stride] = E[10] - O[10];
    out[54 * out_stride] = E[9]  - O[9];
    out[55 * out_stride] = E[8]  - O[8];
    out[56 * out_stride] = E[7]  - O[7];
    out[57 * out_stride] = E[6]  - O[6];
    out[58 * out_stride] = E[5]  - O[5];
    out[59 * out_stride] = E[4]  - O[4];
    out[60 * out_stride] = E[3]  - O[3];
    out[61 * out_stride] = E[2]  - O[2];
    out[62 * out_stride] = E[1]  - O[1];
    out[63 * out_stride] = E[0]  - O[0];
};

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

void ff_vvc_inv_lfnst_1d(int *v, const int *u, int no_zero_size, int n_tr_s,
    int pred_mode_intra, int lfnst_idx, int log2_transform_range)
{
     int lfnst_tr_set_idx    = pred_mode_intra < 0 ? 1 : ff_vvc_lfnst_tr_set_index[pred_mode_intra];
     const int8_t *tr_mat = n_tr_s > 16 ? ff_vvc_lfnst_8x8[lfnst_tr_set_idx][lfnst_idx-1][0] : ff_vvc_lfnst_4x4[lfnst_tr_set_idx][lfnst_idx - 1][0];

     for (int j = 0; j < n_tr_s; j++, tr_mat++) {
        int t = 0;

        for (int i = 0; i < no_zero_size; i++)
            t += u[i] * tr_mat[i * n_tr_s];
        v[j] = av_clip_intp2((t + 64) >> 7 , log2_transform_range);
     }
}
