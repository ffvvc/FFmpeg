/*
 * VVC video decoder
 *
 * Copyright (C) 2021 Nuo Mi
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

#include "get_bits.h"
#include "vvcdec.h"

#include "bit_depth_template.c"
#include "vvcdsp.h"

#if 0
static void FUNC(put_pcm)(uint8_t *_dst, ptrdiff_t stride, int width, int height,
                          GetBitContext *gb, int pcm_bit_depth)
{
    int x, y;
    pixel *dst = (pixel *)_dst;

    stride /= sizeof(pixel);

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = get_bits(gb, pcm_bit_depth) << (BIT_DEPTH - pcm_bit_depth);
        dst += stride;
    }
}
#endif

static void FUNC(add_residual)(uint8_t *_dst, const int *res,
    const int w, const int h, const ptrdiff_t _stride)
{
    pixel *dst          = (pixel *)_dst;

    const int stride    = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            dst[x] = av_clip_pixel(dst[x] + *res);
            res++;
        }
        dst += stride;
    }
}

static void FUNC(add_residual_joint)(uint8_t *_dst, const int *res,
    const int w, const int h, const ptrdiff_t _stride, const int c_sign, const int shift)
{
    pixel *dst = (pixel *)_dst;

    const int stride = _stride / sizeof(pixel);

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            const int r = ((*res) * c_sign) >> shift;
            dst[x] = av_clip_pixel(dst[x] + r);
            res++;
        }
        dst += stride;
    }
}

static void FUNC(pred_residual_joint)(int *buf, const int w, const int h,
    const int c_sign, const int shift)
{
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            *buf = ((*buf) * c_sign) >> shift;
            buf++;
        }
    }
}

static void FUNC(transform_bdpcm)(int *coeffs, const int width, const int height,
    const int vertical, const int depth)
{
    int x, y;

    if (vertical) {
        coeffs += width;
        for (y = 0; y < height - 1; y++) {
            for (x = 0; x < width; x++)
                coeffs[x] = av_clip_intp2(coeffs[x] + coeffs[x - width], depth);
            coeffs += width;
        }
    } else {
        for (y = 0; y < height; y++) {
            for (x = 1; x < width; x++)
                coeffs[x] = av_clip_intp2(coeffs[x] + coeffs[x - 1], depth);
            coeffs += width;
        }
    }
}

#if 0
static void FUNC(dequant)(int16_t *coeffs, int16_t log2_size)
{
    int shift  = 15 - BIT_DEPTH - log2_size;
    int x, y;
    int size = 1 << log2_size;

    if (shift > 0) {
        int offset = 1 << (shift - 1);
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                *coeffs = (*coeffs + offset) >> shift;
                coeffs++;
            }
        }
    } else {
        for (y = 0; y < size; y++) {
            for (x = 0; x < size; x++) {
                *coeffs = *(uint16_t*)coeffs << -shift;
                coeffs++;
            }
        }
    }
}

#endif

static void FUNC(lmcs_filter_luma)(uint8_t *_dst, ptrdiff_t dst_stride, const int width, const int height, const uint8_t *_lut)
{
    const pixel *lut = (const pixel *)_lut;
    pixel *dst = (pixel*)_dst;
    dst_stride /= sizeof(pixel);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = lut[dst[x]];
        dst += dst_stride;
    }
}

static void FUNC(sao_band_filter)(uint8_t *_dst, uint8_t *_src,
                                  ptrdiff_t dst_stride, ptrdiff_t src_stride,
                                  int16_t *sao_offset_val, int sao_left_class,
                                  int width, int height)
{
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int offset_table[32] = { 0 };
    int k, y, x;
    int shift  = BIT_DEPTH - 5;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (k = 0; k < 4; k++)
        offset_table[(k + sao_left_class) & 31] = sao_offset_val[k + 1];
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(src[x] + offset_table[src[x] >> shift]);
        dst += dst_stride;
        src += src_stride;
    }
}

#define CMP(a, b) (((a) > (b)) - ((a) < (b)))

static void FUNC(sao_edge_filter)(uint8_t *_dst, uint8_t *_src, ptrdiff_t dst_stride, int16_t *sao_offset_val,
                                  int eo, int width, int height) {

    static const uint8_t edge_idx[] = { 1, 2, 0, 3, 4 };
    static const int8_t pos[4][2][2] = {
        { { -1,  0 }, {  1, 0 } }, // horizontal
        { {  0, -1 }, {  0, 1 } }, // vertical
        { { -1, -1 }, {  1, 1 } }, // 45 degree
        { {  1, -1 }, { -1, 1 } }, // 135 degree
    };
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int a_stride, b_stride;
    int x, y;
    ptrdiff_t src_stride = (2*MAX_PB_SIZE + AV_INPUT_BUFFER_PADDING_SIZE) / sizeof(pixel);
    dst_stride /= sizeof(pixel);

    a_stride = pos[eo][0][0] + pos[eo][0][1] * src_stride;
    b_stride = pos[eo][1][0] + pos[eo][1][1] * src_stride;
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int diff0 = CMP(src[x], src[x + a_stride]);
            int diff1 = CMP(src[x], src[x + b_stride]);
            int offset_val        = edge_idx[2 + diff0 + diff1];
            dst[x] = av_clip_pixel(src[x] + sao_offset_val[offset_val]);
        }
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(sao_edge_restore_0)(uint8_t *_dst, uint8_t *_src,
                                    ptrdiff_t dst_stride, ptrdiff_t src_stride, SAOParams *sao,
                                    int *borders, int _width, int _height,
                                    int c_idx, uint8_t *vert_edge,
                                    uint8_t *horiz_edge, uint8_t *diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int16_t *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, width = _width, height = _height;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    if (sao_eo_class != SAO_EO_VERT) {
        if (borders[0]) {
            int offset_val = sao_offset_val[0];
            for (y = 0; y < height; y++) {
                dst[y * dst_stride] = av_clip_pixel(src[y * src_stride] + offset_val);
            }
            init_x = 1;
        }
        if (borders[2]) {
            int offset_val = sao_offset_val[0];
            int offset     = width - 1;
            for (x = 0; x < height; x++) {
                dst[x * dst_stride + offset] = av_clip_pixel(src[x * src_stride + offset] + offset_val);
            }
            width--;
        }
    }
    if (sao_eo_class != SAO_EO_HORIZ) {
        if (borders[1]) {
            int offset_val = sao_offset_val[0];
            for (x = init_x; x < width; x++)
                dst[x] = av_clip_pixel(src[x] + offset_val);
        }
        if (borders[3]) {
            int offset_val   = sao_offset_val[0];
            ptrdiff_t y_dst_stride = dst_stride * (height - 1);
            ptrdiff_t y_src_stride = src_stride * (height - 1);
            for (x = init_x; x < width; x++)
                dst[x + y_dst_stride] = av_clip_pixel(src[x + y_src_stride] + offset_val);
            height--;
        }
    }
}

static void FUNC(sao_edge_restore_1)(uint8_t *_dst, uint8_t *_src,
                                    ptrdiff_t dst_stride, ptrdiff_t src_stride, SAOParams *sao,
                                    int *borders, int _width, int _height,
                                    int c_idx, uint8_t *vert_edge,
                                    uint8_t *horiz_edge, uint8_t *diag_edge)
{
    int x, y;
    pixel *dst = (pixel *)_dst;
    pixel *src = (pixel *)_src;
    int16_t *sao_offset_val = sao->offset_val[c_idx];
    int sao_eo_class    = sao->eo_class[c_idx];
    int init_x = 0, init_y = 0, width = _width, height = _height;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    if (sao_eo_class != SAO_EO_VERT) {
        if (borders[0]) {
            int offset_val = sao_offset_val[0];
            for (y = 0; y < height; y++) {
                dst[y * dst_stride] = av_clip_pixel(src[y * src_stride] + offset_val);
            }
            init_x = 1;
        }
        if (borders[2]) {
            int offset_val = sao_offset_val[0];
            int offset     = width - 1;
            for (x = 0; x < height; x++) {
                dst[x * dst_stride + offset] = av_clip_pixel(src[x * src_stride + offset] + offset_val);
            }
            width--;
        }
    }
    if (sao_eo_class != SAO_EO_HORIZ) {
        if (borders[1]) {
            int offset_val = sao_offset_val[0];
            for (x = init_x; x < width; x++)
                dst[x] = av_clip_pixel(src[x] + offset_val);
            init_y = 1;
        }
        if (borders[3]) {
            int offset_val   = sao_offset_val[0];
            ptrdiff_t y_dst_stride = dst_stride * (height - 1);
            ptrdiff_t y_src_stride = src_stride * (height - 1);
            for (x = init_x; x < width; x++)
                dst[x + y_dst_stride] = av_clip_pixel(src[x + y_src_stride] + offset_val);
            height--;
        }
    }

    {
        int save_upper_left  = !diag_edge[0] && sao_eo_class == SAO_EO_135D && !borders[0] && !borders[1];
        int save_upper_right = !diag_edge[1] && sao_eo_class == SAO_EO_45D  && !borders[1] && !borders[2];
        int save_lower_right = !diag_edge[2] && sao_eo_class == SAO_EO_135D && !borders[2] && !borders[3];
        int save_lower_left  = !diag_edge[3] && sao_eo_class == SAO_EO_45D  && !borders[0] && !borders[3];

        // Restore pixels that can't be modified
        if (vert_edge[0] && sao_eo_class != SAO_EO_VERT) {
            for(y = init_y+save_upper_left; y< height-save_lower_left; y++)
                dst[y*dst_stride] = src[y*src_stride];
        }
        if (vert_edge[1] && sao_eo_class != SAO_EO_VERT) {
            for(y = init_y+save_upper_right; y< height-save_lower_right; y++)
                dst[y*dst_stride+width-1] = src[y*src_stride+width-1];
        }

        if (horiz_edge[0] && sao_eo_class != SAO_EO_HORIZ) {
            for(x = init_x+save_upper_left; x < width-save_upper_right; x++)
                dst[x] = src[x];
        }
        if (horiz_edge[1] && sao_eo_class != SAO_EO_HORIZ) {
            for(x = init_x+save_lower_left; x < width-save_lower_right; x++)
                dst[(height-1)*dst_stride+x] = src[(height-1)*src_stride+x];
        }
        if (diag_edge[0] && sao_eo_class == SAO_EO_135D)
            dst[0] = src[0];
        if (diag_edge[1] && sao_eo_class == SAO_EO_45D)
            dst[width-1] = src[width-1];
        if (diag_edge[2] && sao_eo_class == SAO_EO_135D)
            dst[dst_stride*(height-1)+width-1] = src[src_stride*(height-1)+width-1];
        if (diag_edge[3] && sao_eo_class == SAO_EO_45D)
            dst[dst_stride*(height-1)] = src[src_stride*(height-1)];

    }
}

#undef CMP

static av_always_inline int16_t FUNC(alf_clip)(pixel curr, pixel v0, pixel v1, int16_t clip)
{
    return av_clip(v0 - curr, -clip, clip) + av_clip(v1 - curr, -clip, clip);
}

static void FUNC(alf_filter_luma)(uint8_t *_dst, const uint8_t *_src, ptrdiff_t dst_stride, ptrdiff_t src_stride,
    const int width, const int height, const int8_t *filter, const int16_t *clip)
{
    const pixel *src    = (pixel *)_src;
    const int shift     = 7;
    const int offset    = 1 << ( shift - 1 );

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const pixel *s0 = src + y * src_stride + x;
            const pixel *s1 = s0 + src_stride;
            const pixel *s2 = s0 - src_stride;
            const pixel *s3 = s1 + src_stride;
            const pixel *s4 = s2 - src_stride;
            const pixel *s5 = s3 + src_stride;
            const pixel *s6 = s4 - src_stride;

            for (int i = 0; i < ALF_BLOCK_SIZE; i++) {
                pixel *dst = (pixel *)_dst + (y + i) * dst_stride + x;

                const pixel *p0 = s0 + i * src_stride;
                const pixel *p1 = s1 + i * src_stride;
                const pixel *p2 = s2 + i * src_stride;
                const pixel *p3 = s3 + i * src_stride;
                const pixel *p4 = s4 + i * src_stride;
                const pixel *p5 = s5 + i * src_stride;
                const pixel *p6 = s6 + i * src_stride;

                for (int j = 0; j < ALF_BLOCK_SIZE; j++) {
                    int sum = 0;
                    const pixel curr = *p0;

                    sum += filter[0]  * FUNC(alf_clip)(curr, p5[+0], p6[+0], clip[0]);
                    sum += filter[1]  * FUNC(alf_clip)(curr, p3[+1], p4[-1], clip[1]);
                    sum += filter[2]  * FUNC(alf_clip)(curr, p3[+0], p4[+0], clip[2]);
                    sum += filter[3]  * FUNC(alf_clip)(curr, p3[-1], p4[+1], clip[3]);
                    sum += filter[4]  * FUNC(alf_clip)(curr, p1[+2], p2[-2], clip[4]);
                    sum += filter[5]  * FUNC(alf_clip)(curr, p1[+1], p2[-1], clip[5]);
                    sum += filter[6]  * FUNC(alf_clip)(curr, p1[+0], p2[+0], clip[6]);
                    sum += filter[7]  * FUNC(alf_clip)(curr, p1[-1], p2[+1], clip[7]);
                    sum += filter[8]  * FUNC(alf_clip)(curr, p1[-2], p2[+2], clip[8]);
                    sum += filter[9]  * FUNC(alf_clip)(curr, p0[+3], p0[-3], clip[9]);
                    sum += filter[10] * FUNC(alf_clip)(curr, p0[+2], p0[-2], clip[10]);
                    sum += filter[11] * FUNC(alf_clip)(curr, p0[+1], p0[-1], clip[11]);

                    sum = (sum + offset) >> shift;
                    sum += curr;
                    dst[j] = CLIP(sum);

                    p0++;
                    p1++;
                    p2++;
                    p3++;
                    p4++;
                    p5++;
                    p6++;
                }
            }
            filter += ALF_NUM_COEFF_LUMA;
            clip += ALF_NUM_COEFF_LUMA;
        }
    }
}

static void FUNC(alf_filter_luma_vb)(uint8_t *_dst, const uint8_t *_src, ptrdiff_t dst_stride, ptrdiff_t src_stride,
    const int width, const int height, const int8_t *filter, const int16_t *clip, const int vb_pos)
{
    const pixel *src    = (pixel *)_src;
    const int shift     = 7;
    const int offset    = 1 << ( shift - 1 );
    const int vb_above  = vb_pos - 4;
    const int vb_below  = vb_pos + 3;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const pixel *s0 = src + y * src_stride + x;
            const pixel *s1 = s0 + src_stride;
            const pixel *s2 = s0 - src_stride;
            const pixel *s3 = s1 + src_stride;
            const pixel *s4 = s2 - src_stride;
            const pixel *s5 = s3 + src_stride;
            const pixel *s6 = s4 - src_stride;

            for (int i = 0; i < ALF_BLOCK_SIZE; i++) {
                pixel *dst = (pixel *)_dst + (y + i) * dst_stride + x;

                const pixel *p0 = s0 + i * src_stride;
                const pixel *p1 = s1 + i * src_stride;
                const pixel *p2 = s2 + i * src_stride;
                const pixel *p3 = s3 + i * src_stride;
                const pixel *p4 = s4 + i * src_stride;
                const pixel *p5 = s5 + i * src_stride;
                const pixel *p6 = s6 + i * src_stride;

                const int is_near_vb_above = (y + i <  vb_pos) && (y + i >= vb_pos - 1);
                const int is_near_vb_below = (y + i >= vb_pos) && (y + i <= vb_pos);
                const int is_near_vb = is_near_vb_above || is_near_vb_below;

                if ((y + i < vb_pos) && ((y + i) >= vb_above)) {
                    p1 = (y + i == vb_pos - 1) ? p0 : p1;
                    p3 = (y + i >= vb_pos - 2) ? p1 : p3;
                    p5 = (y + i >= vb_pos - 3) ? p3 : p5;

                    p2 = (y + i == vb_pos - 1) ? p0 : p2;
                    p4 = (y + i >= vb_pos - 2) ? p2 : p4;
                    p6 = (y + i >= vb_pos - 3) ? p4 : p6;
                } else if ((y + i >= vb_pos) && ((y + i) <= vb_below)) {
                    p2 = (y + i == vb_pos    ) ? p0 : p2;
                    p4 = (y + i <= vb_pos + 1) ? p2 : p4;
                    p6 = (y + i <= vb_pos + 2) ? p4 : p6;

                    p1 = (y + i == vb_pos    ) ? p0 : p1;
                    p3 = (y + i <= vb_pos + 1) ? p1 : p3;
                    p5 = (y + i <= vb_pos + 2) ? p3 : p5;
                }

                for (int j = 0; j < ALF_BLOCK_SIZE; j++) {
                    int sum = 0;
                    const pixel curr = *p0;

                    sum += filter[0]  * FUNC(alf_clip)(curr, p5[+0], p6[+0], clip[0]);
                    sum += filter[1]  * FUNC(alf_clip)(curr, p3[+1], p4[-1], clip[1]);
                    sum += filter[2]  * FUNC(alf_clip)(curr, p3[+0], p4[+0], clip[2]);
                    sum += filter[3]  * FUNC(alf_clip)(curr, p3[-1], p4[+1], clip[3]);
                    sum += filter[4]  * FUNC(alf_clip)(curr, p1[+2], p2[-2], clip[4]);
                    sum += filter[5]  * FUNC(alf_clip)(curr, p1[+1], p2[-1], clip[5]);
                    sum += filter[6]  * FUNC(alf_clip)(curr, p1[+0], p2[+0], clip[6]);
                    sum += filter[7]  * FUNC(alf_clip)(curr, p1[-1], p2[+1], clip[7]);
                    sum += filter[8]  * FUNC(alf_clip)(curr, p1[-2], p2[+2], clip[8]);
                    sum += filter[9]  * FUNC(alf_clip)(curr, p0[+3], p0[-3], clip[9]);
                    sum += filter[10] * FUNC(alf_clip)(curr, p0[+2], p0[-2], clip[10]);
                    sum += filter[11] * FUNC(alf_clip)(curr, p0[+1], p0[-1], clip[11]);

                    if (!is_near_vb)
                        sum = (sum + offset) >> shift;
                    else
                        sum = (sum + (1 << ((shift + 3) - 1))) >> (shift + 3);
                    sum += curr;
                    dst[j] = CLIP(sum);

                    p0++;
                    p1++;
                    p2++;
                    p3++;
                    p4++;
                    p5++;
                    p6++;
                }
            }
            filter += ALF_NUM_COEFF_LUMA;
            clip += ALF_NUM_COEFF_LUMA;
        }
    }
}

static void FUNC(alf_filter_chroma)(uint8_t* _dst, const uint8_t* _src, ptrdiff_t dst_stride, ptrdiff_t src_stride,
    const int width, const int height, const int8_t* filter, const int16_t* clip)
{
    const pixel *src = (pixel *)_src;
    const int shift  = 7;
    const int offset = 1 << ( shift - 1 );

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const pixel *s0 = src + y * src_stride + x;
            const pixel *s1 = s0 + src_stride;
            const pixel *s2 = s0 - src_stride;
            const pixel *s3 = s1 + src_stride;
            const pixel *s4 = s2 - src_stride;
            const pixel *s5 = s3 + src_stride;
            const pixel *s6 = s4 - src_stride;

            for (int i = 0; i < ALF_BLOCK_SIZE; i++) {
                pixel *dst = (pixel *)_dst + (y + i) * dst_stride + x;

                const pixel *p0 = s0 + i * src_stride;
                const pixel *p1 = s1 + i * src_stride;
                const pixel *p2 = s2 + i * src_stride;
                const pixel *p3 = s3 + i * src_stride;
                const pixel *p4 = s4 + i * src_stride;
                const pixel *p5 = s5 + i * src_stride;
                const pixel *p6 = s6 + i * src_stride;

                for (int j = 0; j < ALF_BLOCK_SIZE; j++) {
                    int sum = 0;
                    const pixel curr = *p0;

                    sum += filter[0]  * FUNC(alf_clip)(curr, p3[+0], p4[+0], clip[0]);
                    sum += filter[1]  * FUNC(alf_clip)(curr, p1[+1], p2[-1], clip[1]);
                    sum += filter[2]  * FUNC(alf_clip)(curr, p1[+0], p2[+0], clip[2]);
                    sum += filter[3]  * FUNC(alf_clip)(curr, p1[-1], p2[+1], clip[3]);
                    sum += filter[4]  * FUNC(alf_clip)(curr, p0[+2], p0[-2], clip[4]);
                    sum += filter[5]  * FUNC(alf_clip)(curr, p0[+1], p0[-1], clip[5]);

                    sum = (sum + offset) >> shift;
                    sum += curr;
                    dst[j] = CLIP(sum);

                    p0++;
                    p1++;
                    p2++;
                    p3++;
                    p4++;
                    p5++;
                    p6++;
                }
            }
        }
    }
}

static void FUNC(alf_filter_chroma_vb)(uint8_t* _dst, const uint8_t* _src, ptrdiff_t dst_stride, ptrdiff_t src_stride,
    const int width, const int height, const int8_t* filter, const int16_t* clip, const int vb_pos)
{
    const pixel *src = (pixel *)_src;
    const int shift  = 7;
    const int offset = 1 << ( shift - 1 );
    const int vb_above  = vb_pos - 2;
    const int vb_below  = vb_pos + 1;

    dst_stride /= sizeof(pixel);
    src_stride /= sizeof(pixel);

    for (int y = 0; y < height; y += ALF_BLOCK_SIZE) {
        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const pixel *s0 = src + y * src_stride + x;
            const pixel *s1 = s0 + src_stride;
            const pixel *s2 = s0 - src_stride;
            const pixel *s3 = s1 + src_stride;
            const pixel *s4 = s2 - src_stride;
            const pixel *s5 = s3 + src_stride;
            const pixel *s6 = s4 - src_stride;

            for (int i = 0; i < ALF_BLOCK_SIZE; i++) {
                pixel *dst = (pixel *)_dst + (y + i) * dst_stride + x;

                const pixel *p0 = s0 + i * src_stride;
                const pixel *p1 = s1 + i * src_stride;
                const pixel *p2 = s2 + i * src_stride;
                const pixel *p3 = s3 + i * src_stride;
                const pixel *p4 = s4 + i * src_stride;
                const pixel *p5 = s5 + i * src_stride;
                const pixel *p6 = s6 + i * src_stride;

                const int is_near_vb_above = (y + i <  vb_pos) && (y + i >= vb_pos - 1);
                const int is_near_vb_below = (y + i >= vb_pos) && (y + i <= vb_pos);
                const int is_near_vb = is_near_vb_above || is_near_vb_below;

                if ((y + i < vb_pos) && ((y + i) >= vb_above)) {
                    p1 = (y + i == vb_pos - 1) ? p0 : p1;
                    p3 = (y + i >= vb_pos - 2) ? p1 : p3;
                    p5 = (y + i >= vb_pos - 3) ? p3 : p5;

                    p2 = (y + i == vb_pos - 1) ? p0 : p2;
                    p4 = (y + i >= vb_pos - 2) ? p2 : p4;
                    p6 = (y + i >= vb_pos - 3) ? p4 : p6;
                } else if ((y + i >= vb_pos) && ((y + i) <= vb_below)) {
                    p2 = (y + i == vb_pos    ) ? p0 : p2;
                    p4 = (y + i <= vb_pos + 1) ? p2 : p4;
                    p6 = (y + i <= vb_pos + 2) ? p4 : p6;

                    p1 = (y + i == vb_pos    ) ? p0 : p1;
                    p3 = (y + i <= vb_pos + 1) ? p1 : p3;
                    p5 = (y + i <= vb_pos + 2) ? p3 : p5;
                }

                for (int j = 0; j < ALF_BLOCK_SIZE; j++) {
                    int sum = 0;
                    const pixel curr = *p0;

                    sum += filter[0]  * FUNC(alf_clip)(curr, p3[+0], p4[+0], clip[0]);
                    sum += filter[1]  * FUNC(alf_clip)(curr, p1[+1], p2[-1], clip[1]);
                    sum += filter[2]  * FUNC(alf_clip)(curr, p1[+0], p2[+0], clip[2]);
                    sum += filter[3]  * FUNC(alf_clip)(curr, p1[-1], p2[+1], clip[3]);
                    sum += filter[4]  * FUNC(alf_clip)(curr, p0[+2], p0[-2], clip[4]);
                    sum += filter[5]  * FUNC(alf_clip)(curr, p0[+1], p0[-1], clip[5]);

                    if (!is_near_vb)
                        sum = (sum + offset) >> shift;
                    else
                        sum = (sum + (1 << ((shift + 3) - 1))) >> (shift + 3);
                    sum += curr;
                    dst[j] = CLIP(sum);

                    p0++;
                    p1++;
                    p2++;
                    p3++;
                    p4++;
                    p5++;
                    p6++;
                }
            }
        }
    }
}

static void FUNC(alf_filter_cc)(uint8_t *_dst, const uint8_t *_luma, ptrdiff_t dst_stride, const ptrdiff_t luma_stride,
    const int width, const int height, const int hs, const int vs, const int8_t *filter, const int vb_pos)
{
    const ptrdiff_t stride = luma_stride / sizeof(pixel);

    dst_stride /= sizeof(pixel);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            int sum = 0;
            pixel *dst  = (pixel *)_dst  + y * dst_stride + x;
            const pixel *src  = (pixel *)_luma + (y << vs) * stride + (x << hs);

            const pixel *s0 = src - stride;
            const pixel *s1 = src;
            const pixel *s2 = src + stride;
            const pixel *s3 = src + 2 * stride;

            const int pos = y << vs;
            if (!vs && (pos == vb_pos || pos == vb_pos + 1))
                continue;

            if (pos == (vb_pos - 2) || pos == (vb_pos + 1))
                s3 = s2;
            else  if (pos == (vb_pos - 1) || pos == vb_pos)
                s3 = s2 = s0 = s1;


            sum += filter[0] * (*s0 - *src);
            sum += filter[1] * (*(s1 - 1) - *src);
            sum += filter[2] * (*(s1 + 1) - *src);
            sum += filter[3] * (*(s2 - 1) - *src);
            sum += filter[4] * (*s2 - *src);
            sum += filter[5] * (*(s2 + 1) - *src);
            sum += filter[6] * (*s3 - *src);
            sum = av_clip((sum + 64) >> 7, -(1 << (BIT_DEPTH - 1)), (1 << (BIT_DEPTH - 1)) - 1);
            sum += *dst;
            *dst = av_clip_pixel(sum);
        }
    }
}

#define ALF_GRADIENT_BORDER 2
#define ALF_GRADIENT_SIZE   ((ALF_SUBBLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / 2)
#define ALF_GRADIENT_STEP   2

#define ALF_DIR_VERT        0
#define ALF_DIR_HORZ        1
#define ALF_DIR_DIGA0       2
#define ALF_DIR_DIGA1       3
#define ALF_NUM_DIR         4

static void FUNC(alf_get_idx)(const int *sum, const int ac, int *filt_idx, int *transpose_idx)
{
    static const int arg_var[] = {0, 1, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3, 3, 3, 4 };
    static const int transpose_table[] = { 0, 1, 0, 2, 2, 3, 1, 3 };

    int hv0, hv1, dir_hv, d0, d1, dir_d, hvd1, hvd0, sum_hv;
    int dir1, dir2, dirs; ///< mainDirection, secondaryDirection, directionStrength in vtm
    if (sum[ALF_DIR_VERT] > sum[ALF_DIR_HORZ]) {
        hv1 = sum[ALF_DIR_VERT];
        hv0 = sum[ALF_DIR_HORZ];
        dir_hv = 1;
    } else {
        hv1 = sum[ALF_DIR_HORZ];
        hv0 = sum[ALF_DIR_VERT];
        dir_hv = 3;
    }

    if (sum[ALF_DIR_DIGA0] > sum[ALF_DIR_DIGA1]) {
        d1 = sum[ALF_DIR_DIGA0];
        d0 = sum[ALF_DIR_DIGA1];
        dir_d = 0;
    } else {
        d1 = sum[ALF_DIR_DIGA1];
        d0 = sum[ALF_DIR_DIGA0];
        dir_d = 2;
    }

    //promote to avoid overflow
    if ((uint64_t)d1 * hv0 > (uint64_t)hv1 * d0) {
        hvd1 = d1;
        hvd0 = d0;
        dir1 = dir_d;
        dir2 = dir_hv;
    } else {
        hvd1 = hv1;
        hvd0 = hv0;
        dir1 = dir_hv;
        dir2 = dir_d;
    }
    dirs = (hvd1 * 2 > 9 * hvd0) ? 2 : ((hvd1 > 2 * hvd0) ? 1 : 0);

    *transpose_idx = transpose_table[dir1 * 2 + (dir2 >> 1)];

    sum_hv = sum[ALF_DIR_HORZ] + sum[ALF_DIR_VERT];
    *filt_idx = arg_var[av_clip_uintp2(sum_hv * ac >> (BIT_DEPTH - 1), 4)];
    if (dirs) {
        *filt_idx += (((dir1 & 0x1) << 1) + dirs) * 5;
    }
}

static void FUNC(alf_reconstruct_coeff_and_clip)(const int8_t *src_coeff, const uint8_t *clip_idx, const int transpose_idx, int8_t *coeff, int16_t *clip)
{
    const static int index[][ALF_NUM_COEFF_LUMA] = {
        { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 },
        { 9, 4, 10, 8, 1, 5, 11, 7, 3, 0, 2, 6 },
        { 0, 3, 2, 1, 8, 7, 6, 5, 4, 9, 10, 11 },
        { 9, 8, 10, 4, 3, 7, 11, 5, 1, 0, 2, 6 },
    };

    const int16_t clip_set[] = {
        1 << BIT_DEPTH, 1 << (BIT_DEPTH - 3), 1 << (BIT_DEPTH - 5), 1 << (BIT_DEPTH - 7)
    };

    for (int i = 0; i < ALF_NUM_COEFF_LUMA; i++) {
        const int idx = index[transpose_idx][i];
        coeff[i] = src_coeff[idx];
        clip[i]  = clip_set[clip_idx[idx]];
    }
}

static void FUNC(alf_get_coeff_and_clip)(const uint8_t *_src, ptrdiff_t src_stride,
    const int x0, const int y0, const int width, const int height, const int vb_pos,
    const int8_t *coeff_set, const uint8_t *clip_idx_set, const uint8_t *class_to_filt,
    int8_t *coeff, int16_t *clip)
{
    int gradient[ALF_NUM_DIR][ALF_GRADIENT_SIZE][ALF_GRADIENT_SIZE] = {0};

    const int h = height + ALF_GRADIENT_BORDER * 2;
    const int w = width  + ALF_GRADIENT_BORDER * 2;
    const int size = (ALF_BLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / ALF_GRADIENT_STEP;

    const pixel *src = (const pixel *)_src;
    src_stride /= sizeof(pixel);
    src -= (ALF_GRADIENT_BORDER + 1) * src_stride + ALF_GRADIENT_BORDER;

    for (int y = 0; y < h; y += ALF_GRADIENT_STEP) {
        const pixel *s0  = src + y * src_stride;
        const pixel *s1  = s0 + src_stride;
        const pixel *s2  = s1 + src_stride;
        const pixel *s3  = s2 + src_stride;

        if (y0 + y == vb_pos)          //above
            s3 = s2;
        else if (y0 + y == vb_pos + ALF_GRADIENT_BORDER)
            s0 = s1;

        for (int x = 0; x < w; x += ALF_GRADIENT_STEP) {
            //two points a time
            const int xg = x / ALF_GRADIENT_STEP;
            const int yg = y / ALF_GRADIENT_STEP;
            const pixel *a0  = s0 + x;
            const pixel *p0  = s1 + x;
            const pixel *b0  = s2 + x;
            const int val0   = (*p0) << 1;

            const pixel *a1  = s1 + x + 1;
            const pixel *p1  = s2 + x + 1;
            const pixel *b1  = s3 + x + 1;
            const int val1   = (*p1) << 1;

            gradient[ALF_DIR_VERT] [yg][xg] = FFABS(val0 - *a0 - *b0) + FFABS(val1 - *a1 - *b1);
            gradient[ALF_DIR_HORZ] [yg][xg] = FFABS(val0 - *(p0 - 1) - *(p0 + 1)) + FFABS(val1 - *(p1 - 1) - *(p1 + 1));
            gradient[ALF_DIR_DIGA0][yg][xg] = FFABS(val0 - *(a0 - 1) - *(b0 + 1)) + FFABS(val1 - *(a1 - 1) - *(b1 + 1));
            gradient[ALF_DIR_DIGA1][yg][xg] = FFABS(val0 - *(a0 + 1) - *(b0 - 1)) + FFABS(val1 - *(a1 + 1) - *(b1 - 1));
        }
    }

    for (int y = 0; y < height ; y += ALF_BLOCK_SIZE ) {
        int start = 0;
        int end   = (ALF_BLOCK_SIZE + ALF_GRADIENT_BORDER * 2) / ALF_GRADIENT_STEP;
        int ac    = 2;
        if (y0 + y + ALF_BLOCK_SIZE == vb_pos) {
            end -= ALF_GRADIENT_BORDER / ALF_GRADIENT_STEP;
            ac = 3;
        } else if (y0 + y == vb_pos) {
            start += ALF_GRADIENT_BORDER / ALF_GRADIENT_STEP;
            ac = 3;
        }
        for (int x = 0; x < width; x += ALF_BLOCK_SIZE) {
            const int xg = x / ALF_GRADIENT_STEP;
            const int yg = y / ALF_GRADIENT_STEP;
            int sum[ALF_NUM_DIR] = { 0 };
            int class_idx, transpose_idx;

            //todo: optimize this
            for (int i = start; i < end; i++) {
                for (int j = 0; j < size; j++) {
                    sum[ALF_DIR_VERT]  += gradient[ALF_DIR_VERT][yg + i][xg + j];
                    sum[ALF_DIR_HORZ]  += gradient[ALF_DIR_HORZ][yg + i][xg + j];
                    sum[ALF_DIR_DIGA0] += gradient[ALF_DIR_DIGA0][yg + i][xg + j];
                    sum[ALF_DIR_DIGA1] += gradient[ALF_DIR_DIGA1][yg + i][xg + j];
                }
            }
            FUNC(alf_get_idx)(sum, ac, &class_idx, &transpose_idx);
            FUNC(alf_reconstruct_coeff_and_clip)(&coeff_set[class_to_filt[class_idx] * ALF_NUM_COEFF_LUMA],
                &clip_idx_set[class_idx * ALF_NUM_COEFF_LUMA], transpose_idx, coeff, clip);

            coeff += ALF_NUM_COEFF_LUMA;
            clip  += ALF_NUM_COEFF_LUMA;
        }
    }
}

#undef ALF_GRADIENT_STEP
#undef ALF_DIR_HORZ
#undef ALF_DIR_VERT
#undef ALF_DIR_DIGA0
#undef ALF_DIR_DIGA1
#undef ALF_NUM_DIR


////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////
static void FUNC(put_vvc_pel_pixels)(int16_t *dst,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src          = (const pixel *)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = src[x] << (14 - BIT_DEPTH);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_pel_uni_pixels)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height,
    const intptr_t mx, const intptr_t my, const int width, const int hf_idx, const int vf_idx)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);

    for (int y = 0; y < height; y++) {
        memcpy(dst, src, width * sizeof(pixel));
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_pel_bi_pixels)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src          = (const pixel *)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    pixel *dst                = (pixel *)_dst;
    const ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);

    const int shift           = 14  + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset          = 1 << (shift - 1);
#else
    const int offset          = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((src[x] << (14 - BIT_DEPTH)) + src0[x] + offset) >> shift);
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_pel_uni_w_pixels)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height,
    const int denom, const int wx, const int _ox, const intptr_t mx, const intptr_t my,
    const int width, const int hf_idx, const int vf_idx)
{
    int x, y;
    pixel *src          = (pixel *)_src;
    ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    int shift = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif
    const int ox     = _ox * (1 << (BIT_DEPTH - 8));

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            const int v = (src[x] << (14 - BIT_DEPTH));
            dst[x] = av_clip_pixel(((v * wx + offset) >> shift) + ox);
        }
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_pel_bi_w_pixels)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,  const int16_t *src0,
    const int height, const int denom, const int wx0, const int wx1,
    int ox0, int ox1, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src          = (const pixel *)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    pixel *dst                = (pixel *)_dst;
    const ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);

    const int shift           = 14  + 1 - BIT_DEPTH;
    const int log2Wd          = denom + shift - 1;

    ox0 = ox0 * (1 << (BIT_DEPTH - 8));
    ox1 = ox1 * (1 << (BIT_DEPTH - 8));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(( (src[x] << (14 - BIT_DEPTH)) * wx1 + src0[x] * wx0 + (ox0 + ox1 + 1) * (1 << log2Wd)) >> (log2Wd + 1));
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////
#define LUMA_FILTER(src, stride)                                               \
    (filter[0] * src[x - 3 * stride] +                                         \
     filter[1] * src[x - 2 * stride] +                                         \
     filter[2] * src[x -     stride] +                                         \
     filter[3] * src[x             ] +                                         \
     filter[4] * src[x +     stride] +                                         \
     filter[5] * src[x + 2 * stride] +                                         \
     filter[6] * src[x + 3 * stride] +                                         \
     filter[7] * src[x + 4 * stride])

static void FUNC(put_vvc_luma_h)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src          = (const pixel*)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    const int8_t *filter      = ff_vvc_luma_filters[hf_idx][mx];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_v)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src          = (pixel*)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    const int8_t *filter      = ff_vvc_luma_filters[vf_idx][my];
    for (int y = 0; y < height; y++)  {
        for (int x = 0; x < width; x++)
            dst[x] = LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_hv)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const int8_t *filter;
    const pixel *src = (const pixel*)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;

    src   -= LUMA_EXTRA_BEFORE * src_stride;
    filter = ff_vvc_luma_filters[hf_idx][mx];
    for (y = 0; y < height + LUMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_luma_filters[vf_idx][my];
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6;
        tmp += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_uni_h)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src          = (const pixel*)_src;
    const ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    pixel *dst                = (pixel *)_dst;
    const ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter      = ff_vvc_luma_filters[hf_idx][mx];
    const int shift           = 14 - BIT_DEPTH;

#if BIT_DEPTH < 14
    const int offset          = 1 << (shift - 1);
#else
    const int offset          = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int val = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
            dst[x]        = av_clip_pixel((val + offset) >> shift);
        }
        src   += src_stride;
        dst   += dst_stride;
    }
}

static void FUNC(put_vvc_luma_bi_h)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    pixel        *src       = (pixel*)_src;
    ptrdiff_t     src_stride = _src_stride / sizeof(pixel);
    pixel *dst              = (pixel *)_dst;
    ptrdiff_t dst_stride     = _dst_stride / sizeof(pixel);

    const int8_t *filter    = ff_vvc_luma_filters[hf_idx][mx];

    const int shift         = 14  + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset              = 1 << (shift - 1);
#else
    int offset              = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) + src0[x] + offset) >> shift);
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_uni_v)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{

    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_luma_filters[vf_idx][my];
    const int shift             = 14 - BIT_DEPTH;

#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int val = LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8);
            dst[x]        = av_clip_pixel((val + offset) >> shift);
        }
        src   += src_stride;
        dst   += dst_stride;
    }
}

static void FUNC(put_vvc_luma_bi_v)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);

    const int8_t *filter        = ff_vvc_luma_filters[vf_idx][my];

    const int shift             = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) + src0[x] + offset) >> shift);
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_uni_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const int8_t *filter;
    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    const int shift =  14 - BIT_DEPTH;

#if BIT_DEPTH < 14
    const int offset = 1 << (shift - 1);
#else
    const int offset = 0;
#endif
    src   -= LUMA_EXTRA_BEFORE * src_stride;
    filter = ff_vvc_luma_filters[hf_idx][mx];
    for (y = 0; y < height + LUMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_luma_filters[vf_idx][my];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            int val = LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6;
            dst[x]  = av_clip_pixel((val  + offset) >> shift);


        }
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }

}

static void FUNC(put_vvc_luma_bi_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const int8_t *filter;
    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    const int shift             = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    src   -= LUMA_EXTRA_BEFORE * src_stride;
    filter = ff_vvc_luma_filters[hf_idx][mx];
    for (y = 0; y < height + LUMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_luma_filters[vf_idx][my];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6) + src0[x] + offset) >> shift);
        tmp  += MAX_PB_SIZE;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_uni_w_h)(uint8_t *_dst,  ptrdiff_t _dst_stride,
    const uint8_t *_src, ptrdiff_t _src_stride, int height, int denom, int wx, int ox,
    intptr_t mx, intptr_t my, int width, int hf_idx, int vf_idx)
{
    int x, y;
    pixel        *src       = (pixel*)_src;
    ptrdiff_t     src_stride = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter    = ff_vvc_luma_filters[hf_idx][mx];
    int shift = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    ox = ox * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_luma_bi_w_h)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0, const int height,
    const int denom, const int w0, const int w1, const int o0, int o1,
    const intptr_t mx, const intptr_t my, const int width, const int hf_idx, const int vf_idx)
{
    const pixel *src            = (pixel*)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_luma_filters[hf_idx][mx];
    const int shift             = denom + FFMAX(2, 14 - BIT_DEPTH) + 1;
    const int offset            = (((o0 + o1) << (BIT_DEPTH - 8)) + 1) << (shift - 1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int src1 = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
            dst[x] = av_clip_pixel((src1 * w1 + src0[x] * w0 + offset) >> shift);
        }
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_uni_w_v)(uint8_t *_dst,  ptrdiff_t _dst_stride,
    const uint8_t *_src, ptrdiff_t _src_stride, int height, int denom, int wx, int ox,
    intptr_t mx, intptr_t my, int width, const int hf_idx, const int vf_idx)
{
    int x, y;
    pixel        *src       = (pixel*)_src;
    ptrdiff_t     src_stride = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter    = ff_vvc_luma_filters[vf_idx][my];
    int shift = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    ox = ox * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_luma_bi_w_v)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const int denom, const int wx0, const int wx1,
    int ox0, int ox1, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);

    const int8_t *filter        = ff_vvc_luma_filters[vf_idx][my];

    const int shift             = 14 + 1 - BIT_DEPTH;
    const int log2Wd            = denom + shift - 1;

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((LUMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) * wx1 + src0[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_luma_uni_w_hv)(uint8_t *_dst,  const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int height, const int denom,
    int wx, int ox, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const int8_t *filter;
    pixel *src = (pixel*)_src;
    ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    int shift = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    src   -= LUMA_EXTRA_BEFORE * src_stride;
    filter = ff_vvc_luma_filters[hf_idx][mx];
    for (y = 0; y < height + LUMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_luma_filters[vf_idx][my];

    ox = ox * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx + offset) >> shift) + ox);
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_luma_bi_w_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const int denom, const int wx0, const int wx1,
    int ox0, int ox1, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const int8_t *filter;
    const pixel *src            = (pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + LUMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    const int shift             = 14 + 1 - BIT_DEPTH;
    const int log2Wd            = denom + shift - 1;

    src   -= LUMA_EXTRA_BEFORE * src_stride;
    filter = ff_vvc_luma_filters[hf_idx][mx];
    for (y = 0; y < height + LUMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = LUMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + LUMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_luma_filters[vf_idx][my];

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((LUMA_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx1 + src0[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        tmp  += MAX_PB_SIZE;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

////////////////////////////////////////////////////////////////////////////////
//
////////////////////////////////////////////////////////////////////////////////
#define CHROMA_FILTER(src, stride)                                               \
    (filter[0] * src[x - stride] +                                             \
     filter[1] * src[x]          +                                             \
     filter[2] * src[x + stride] +                                             \
     filter[3] * src[x + 2 * stride])

static void FUNC(put_vvc_chroma_h)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src        = (const pixel *)_src;
    ptrdiff_t src_stride     = _src_stride / sizeof(pixel);
    const int8_t *filter    = ff_vvc_chroma_filters[hf_idx][mx];
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_v)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[vf_idx][my];

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8);
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_hv)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[hf_idx][mx];
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (y = 0; y < height + CHROMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_chroma_filters[vf_idx][my];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6;
        tmp += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_uni_h)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[hf_idx][mx];
    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) + offset) >> shift);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_chroma_bi_h)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[hf_idx][mx];
    const int shift             = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) + src0[x] + offset) >> shift);
        dst  += dst_stride;
        src  += src_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_uni_v)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[vf_idx][my];
    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) + offset) >> shift);
        src += src_stride;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_chroma_bi_v)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    pixel *src = (pixel *)_src;
    ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    const int8_t *filter = ff_vvc_chroma_filters[vf_idx][my];
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    int shift = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) + src0[x] + offset) >> shift);
        dst  += dst_stride;
        src  += src_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_uni_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    pixel *src = (pixel *)_src;
    ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter = ff_vvc_chroma_filters[hf_idx][mx];
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    int shift = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (y = 0; y < height + CHROMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_chroma_filters[vf_idx][my];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6) + offset) >> shift);
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_chroma_bi_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const pixel *src            = (pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    const int8_t *filter = ff_vvc_chroma_filters[hf_idx][mx];
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const int shift             = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (y = 0; y < height + CHROMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_chroma_filters[vf_idx][my];

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6) + src0[x] + offset) >> shift);
        tmp  += MAX_PB_SIZE;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_uni_w_h)(uint8_t *_dst, ptrdiff_t _dst_stride,
    const uint8_t *_src, ptrdiff_t _src_stride, int height, int denom, int wx, int ox,
    intptr_t mx, intptr_t my, int width, const int hf_idx, const int vf_idx)
{
    int x, y;
    pixel *src = (pixel *)_src;
    ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter = ff_vvc_chroma_filters[hf_idx][mx];
    int shift = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    ox     = ox * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            dst[x] = av_clip_pixel((((CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        }
        dst += dst_stride;
        src += src_stride;
    }
}

static void FUNC(put_vvc_chroma_bi_w_h)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const int denom, const int w0, const int w1,
    int o0, int o1, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (pixel *)_src;
    const ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride  = _dst_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[hf_idx][mx];
    const int shift             = denom + FFMAX(2, 14 - BIT_DEPTH) + 1;
    const int offset            = (((o0 + o1) << (BIT_DEPTH - 8)) + 1) << (shift - 1);

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const int src1 = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
            dst[x] = av_clip_pixel((src1 * w1 + src0[x] * w0 + offset) >> shift);
        }
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_uni_w_v)(uint8_t *_dst, ptrdiff_t _dst_stride,
    const uint8_t *_src, ptrdiff_t _src_stride, int height, int denom, int wx, int ox,
    intptr_t mx, intptr_t my, int width, const int hf_idx, const int vf_idx)
{
    int x, y;
    pixel *src = (pixel *)_src;
    ptrdiff_t src_stride  = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter = ff_vvc_chroma_filters[vf_idx][my];
    int shift = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    ox     = ox * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            dst[x] = av_clip_pixel((((CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) * wx + offset) >> shift) + ox);
        }
        dst += dst_stride;
        src += src_stride;
    }
}

static void FUNC(put_vvc_chroma_bi_w_v)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const   int16_t *src0,
    const int height, const int denom, const int wx0, const int wx1,
    int ox0, int ox1, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    const pixel *src            = (pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[vf_idx][my];
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    const int shift             = 14 + 1 - BIT_DEPTH;
    const int log2Wd            = denom + shift - 1;

    ox0 = ox0 * (1 << (BIT_DEPTH - 8));
    ox1 = ox1 * (1 << (BIT_DEPTH - 8));
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(src, src_stride) >> (BIT_DEPTH - 8)) * wx1 + src0[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        src  += src_stride;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_chroma_uni_w_hv)(uint8_t *_dst, ptrdiff_t _dst_stride, const uint8_t *_src, ptrdiff_t _src_stride,
    int height, int denom, int wx, int ox, intptr_t mx, intptr_t my, int width, const int hf_idx, const int vf_idx)
{
    int x, y;
    pixel *src = (pixel *)_src;
    ptrdiff_t src_stride = _src_stride / sizeof(pixel);
    pixel *dst          = (pixel *)_dst;
    ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    const int8_t *filter = ff_vvc_chroma_filters[hf_idx][mx];
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    int shift = denom + 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    int offset = 1 << (shift - 1);
#else
    int offset = 0;
#endif

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (y = 0; y < height + CHROMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_chroma_filters[vf_idx][my];

    ox     = ox * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel((((CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx + offset) >> shift) + ox);
        tmp += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(put_vvc_chroma_bi_w_hv)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const uint8_t *_src, const ptrdiff_t _src_stride, const int16_t *src0,
    const int height, const int denom, const int wx0, const int wx1,
    int ox0, int ox1, const intptr_t mx, const intptr_t my, const int width,
    const int hf_idx, const int vf_idx)
{
    int x, y;
    const pixel *src            = (pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    pixel *dst                  = (pixel *)_dst;
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_chroma_filters[hf_idx][mx];
    int16_t tmp_array[(MAX_PB_SIZE + CHROMA_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp = tmp_array;
    const int shift             = 14 + 1 - BIT_DEPTH;
    const int log2Wd            = denom + shift - 1;

    src -= CHROMA_EXTRA_BEFORE * src_stride;

    for (y = 0; y < height + CHROMA_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = CHROMA_FILTER(src, 1) >> (BIT_DEPTH - 8);
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp      = tmp_array + CHROMA_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_chroma_filters[vf_idx][my];

    ox0     = ox0 * (1 << (BIT_DEPTH - 8));
    ox1     = ox1 * (1 << (BIT_DEPTH - 8));
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = av_clip_pixel(((CHROMA_FILTER(tmp, MAX_PB_SIZE) >> 6) * wx1 + src0[x] * wx0 +
                                    ((ox0 + ox1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        tmp  += MAX_PB_SIZE;
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
    }
}

static void FUNC(put_vvc_ciip)(uint8_t *_dst, const ptrdiff_t _dst_stride, const int width, const int height,
    const uint8_t *_inter, const ptrdiff_t _inter_stride, const int intra_weight)
{
    pixel *dst   = (pixel *)_dst;
    pixel *inter = (pixel *)_inter;
    const int dst_stride   = _dst_stride / sizeof(pixel);
    const int inter_stride = _inter_stride / sizeof(pixel);

    const int inter_weight = 4 - intra_weight;
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = (dst[x] * intra_weight + inter[x] * inter_weight + 2) >> 2;
        dst   += dst_stride;
        inter += inter_stride;
    }
}

static void FUNC(put_vvc_gpm)(uint8_t *_dst, ptrdiff_t dst_stride, int width, int height,
    const int16_t *tmp, const int16_t *tmp1, const ptrdiff_t tmp_stride,
    const uint8_t *weights, const int step_x, const int step_y)
{
    const int shift  = FFMAX(5, 17 - BIT_DEPTH);
    const int offset = 1 << (shift - 1);
    pixel *dst  = (pixel *)_dst;

    dst_stride /= sizeof(pixel);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
            const uint8_t w = weights[x * step_x];
            dst[x] = av_clip_pixel((tmp[x] * w + tmp1[x] * (8 - w) + offset) >> shift);
        }
        dst     += dst_stride;
        tmp     += tmp_stride;
        tmp1    += tmp_stride;
        weights += step_y;
    }
}

//8.5.6.3.3 Luma integer sample fetching process, add one extra pad line
static void FUNC(bdof_fetch_samples)(int16_t *_dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int x_frac, const int y_frac, const int width, const int height)
{
    const int x_off             = (x_frac >> 3) - 1;
    const int y_off             = (y_frac >> 3) - 1;

    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    const pixel *src            = (pixel*)_src + (x_off) + y_off * src_stride;
    int16_t *dst                = _dst - 1 - MAX_PB_SIZE;

    const int shift             = 14 - BIT_DEPTH;
    const int bdof_width        = width + 2 * BDOF_BORDER_EXT;

    // top
    for (int i = 0; i < bdof_width; i++)
        dst[i] = src[i] << shift;

    dst += MAX_PB_SIZE;
    src += src_stride;

    for (int i = 0; i < height; i++) {
        dst[0] = src[0] << shift;
        dst[1 + width] = src[1 + width] << shift;
        dst += MAX_PB_SIZE;
        src += src_stride;
    }
    for (int i = 0; i < bdof_width; i++)
        dst[i] = src[i] << shift;
}

//8.5.6.3.3 Luma integer sample fetching process
static void FUNC(fetch_samples)(int16_t *_dst, const uint8_t *_src, const ptrdiff_t _src_stride, const int x_frac, const int y_frac)
{
    FUNC(bdof_fetch_samples)(_dst, _src, _src_stride, x_frac, y_frac, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE);
}

static void FUNC(prof_grad_filter)(int16_t *_gradient_h, int16_t *_gradient_v, const ptrdiff_t gradient_stride,
    const int16_t *_src, const ptrdiff_t src_stride, const int width, const int height, const int pad)
{
    const int shift = 6;
    const int16_t *src = _src;
    int16_t *gradient_h = _gradient_h + pad * (1 + gradient_stride);
    int16_t *gradient_v = _gradient_v + pad * (1 + gradient_stride);

    for (int y = 0; y < height; y++) {
        const int16_t *p = src;
        for (int x = 0; x < width; x++) {
            gradient_h[x] = (p[1] >> shift) - (p[-1] >> shift);
            gradient_v[x] = (p[src_stride] >> shift) - (p[-src_stride] >> shift);
            p++;
        }
        gradient_h += gradient_stride;
        gradient_v += gradient_stride;
        src += src_stride;
    }
    if (pad) {
        pad_int16(_gradient_h + 1 + gradient_stride, gradient_stride, width, height);
        pad_int16(_gradient_v + 1 + gradient_stride, gradient_stride, width, height);
    }
}

static void FUNC(apply_prof)(int16_t *dst, const int16_t *src, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const int limit     = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit

    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src[x] + av_clip(di, -limit, limit - 1);
            dst[x] = val;

        }
        src += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
    }
}

static void FUNC(apply_prof_uni)(uint8_t *_dst, const ptrdiff_t _dst_stride, const int16_t *src, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const int limit             = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit

    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    pixel *dst                  = (pixel*)_dst;

    const int shift             = 14 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src[x] + av_clip(di, -limit, limit - 1);
            dst[x] = av_clip_pixel((val + offset) >> shift);

        }
        src += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(apply_prof_uni_w)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const int16_t *src, const int16_t *diff_mv_x, const int16_t *diff_mv_y,
    const int denom, const int wx, int ox)
{
    const int limit             = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit

    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    pixel *dst                  = (pixel*)_dst;

    const int shift             = denom + FFMAX(2, 14 - BIT_DEPTH);
    const int offset            = 1 << (shift - 1);

    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];

    ox = ox * (1 << (BIT_DEPTH - 8));

    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src[x] + av_clip(di, -limit, limit - 1);
            dst[x] = av_clip_pixel(((val * wx + offset) >>  shift)  + ox);
        }
        src += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(apply_prof_bi)(uint8_t *_dst, const ptrdiff_t _dst_stride,
    const int16_t *src0, const int16_t *src1, const int16_t *diff_mv_x, const int16_t *diff_mv_y)
{
    const int limit             = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    pixel *dst                  = (pixel*)_dst;


    const int shift             = 14 + 1 - BIT_DEPTH;
#if BIT_DEPTH < 14
    const int offset            = 1 << (shift - 1);
#else
    const int offset            = 0;
#endif

    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src1, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src1[x] + av_clip(di, -limit, limit - 1);
            dst[x] = av_clip_pixel((val + src0[x] + offset) >> shift);

        }
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(apply_prof_bi_w)(uint8_t *_dst, const ptrdiff_t _dst_stride, const int16_t *src0, const int16_t *src1,
    const int16_t *diff_mv_x, const int16_t *diff_mv_y, const int denom, const int w0, const int w1, int o0, int o1)
{
    const int limit             = (1 << FFMAX(13, BIT_DEPTH + 1));          ///< dILimit
    const ptrdiff_t dst_stride   = _dst_stride / sizeof(pixel);
    pixel *dst                  = (pixel*)_dst;


    const int shift             = 14 + 1 - BIT_DEPTH;
    const int log2Wd            = denom + shift - 1;

    int16_t gradient_h[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];
    int16_t gradient_v[AFFINE_MIN_BLOCK_SIZE * AFFINE_MIN_BLOCK_SIZE];

    o0 = o0 * (1 << (BIT_DEPTH - 8));
    o1 = o1 * (1 << (BIT_DEPTH - 8));

    FUNC(prof_grad_filter)(gradient_h, gradient_v, AFFINE_MIN_BLOCK_SIZE, src1, MAX_PB_SIZE, AFFINE_MIN_BLOCK_SIZE, AFFINE_MIN_BLOCK_SIZE, 0);

    for (int y = 0; y < AFFINE_MIN_BLOCK_SIZE; y++) {
        for (int x = 0; x < AFFINE_MIN_BLOCK_SIZE; x++) {
            const int o = y * AFFINE_MIN_BLOCK_SIZE + x;
            const int di = gradient_h[o] * diff_mv_x[o] + gradient_v[o] * diff_mv_y[o];
            const int val = src1[x] + av_clip(di, -limit, limit - 1);
            dst[x] = av_clip_pixel((val * w1 + src0[x] * w0 +  ((o0 + o1 + 1) * (1 << log2Wd))) >> (log2Wd + 1));
        }
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
        dst += dst_stride;
    }
}

static void FUNC(derive_bdof_vx_vy)(const int16_t *_src0, const int16_t *_src1,
    const int16_t **gradient_h, const int16_t **gradient_v, ptrdiff_t gradient_stride,
    int* vx, int* vy)
{
    const int shift2 = 4;
    const int shift3 = 1;
    const int thres = 1 << 4;
    int sgx2 = 0, sgy2 = 0, sgxgy = 0, sgxdi = 0, sgydi = 0;
    const int16_t *src0 = _src0 - 1 - MAX_PB_SIZE;
    const int16_t *src1 = _src1 - 1 - MAX_PB_SIZE;

    for (int y = 0; y < BDOF_GRADIENT_SIZE; y++) {
        for (int x = 0; x < BDOF_GRADIENT_SIZE; x++) {
            const int diff = (src0[x] >> shift2) - (src1[x] >> shift2);
            const int idx = gradient_stride * y  + x;
            const int temph = (gradient_h[0][idx] + gradient_h[1][idx]) >> shift3;
            const int tempv = (gradient_v[0][idx] + gradient_v[1][idx]) >> shift3;
            sgx2 += FFABS(temph);
            sgy2 += FFABS(tempv);
            sgxgy += VVC_SIGN(tempv) * temph;
            sgxdi += -VVC_SIGN(temph) * diff;
            sgydi += -VVC_SIGN(tempv) * diff;
        }
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
    }
    *vx = sgx2 > 0 ? av_clip((sgxdi << 2) >> av_log2(sgx2) , -thres + 1, thres - 1) : 0;
    *vy = sgy2 > 0 ? av_clip(((sgydi << 2) - ((*vx * sgxgy) >> 1)) >> av_log2(sgy2), -thres + 1, thres - 1) : 0;
}

static void FUNC(apply_bdof_min_block)(pixel* dst, const ptrdiff_t dst_stride, const int16_t *src0, const int16_t *src1,
    const int16_t **gradient_h, const int16_t **gradient_v, const int vx, const int vy)
{
    const int shift4 = 15 - BIT_DEPTH;
    const int offset4 = 1 << (shift4 - 1);

    const int16_t* gh[] = { gradient_h[0] + 1 + BDOF_PADDED_SIZE, gradient_h[1] + 1 + BDOF_PADDED_SIZE };
    const int16_t* gv[] = { gradient_v[0] + 1 + BDOF_PADDED_SIZE, gradient_v[1] + 1 + BDOF_PADDED_SIZE };

    for (int y = 0; y < BDOF_BLOCK_SIZE; y++) {
        for (int x = 0; x < BDOF_BLOCK_SIZE; x++) {
            const int idx = y * BDOF_PADDED_SIZE + x;
            const int bdof_offset = vx * (gh[0][idx] - gh[1][idx]) + vy * (gv[0][idx] - gv[1][idx]);
            dst[x] = av_clip_pixel((src0[x] + offset4 + src1[x] + bdof_offset) >> shift4);
        }
        dst  += dst_stride;
        src0 += MAX_PB_SIZE;
        src1 += MAX_PB_SIZE;
    }
}

static void FUNC(apply_bdof)(uint8_t *_dst, const ptrdiff_t _dst_stride, int16_t *_src0, int16_t *_src1,
    const int block_w, const int block_h)
{
    int16_t gradient_h[2][BDOF_PADDED_SIZE * BDOF_PADDED_SIZE];
    int16_t gradient_v[2][BDOF_PADDED_SIZE * BDOF_PADDED_SIZE];
    int vx, vy;

    const ptrdiff_t dst_stride = _dst_stride / sizeof(pixel);
    pixel* dst = (pixel*)_dst;

    FUNC(prof_grad_filter)(gradient_h[0], gradient_v[0], BDOF_PADDED_SIZE,
        _src0, MAX_PB_SIZE, block_w, block_h, 1);
    pad_int16(_src0, MAX_PB_SIZE, block_w, block_h);
    FUNC(prof_grad_filter)(gradient_h[1], gradient_v[1], BDOF_PADDED_SIZE,
        _src1, MAX_PB_SIZE, block_w, block_h, 1);
    pad_int16(_src1, MAX_PB_SIZE, block_w, block_h);

    for (int y = 0; y < block_h; y += BDOF_BLOCK_SIZE) {
        for (int x = 0; x < block_w; x += BDOF_BLOCK_SIZE) {
            const int16_t* src0 = _src0 + y * MAX_PB_SIZE + x;
            const int16_t* src1 = _src1 + y * MAX_PB_SIZE + x;
            pixel *d            = dst + x;
            const int idx       = BDOF_PADDED_SIZE * y  + x;
            const int16_t* gh[] = { gradient_h[0] + idx, gradient_h[1] + idx };
            const int16_t* gv[] = { gradient_v[0] + idx, gradient_v[1] + idx };
            FUNC(derive_bdof_vx_vy)(src0, src1, gh, gv, BDOF_PADDED_SIZE, &vx, &vy);
            FUNC(apply_bdof_min_block)(d, dst_stride, src0, src1, gh, gv, vx, vy);
        }
        dst += BDOF_BLOCK_SIZE * dst_stride;
    }
}

#define DMVR_FILTER(src, stride)                                                \
    (filter[0] * src[x] +                                                       \
     filter[1] * src[x + stride])

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr_vvc_luma)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
                                const int height, const intptr_t mx, const intptr_t my, const int width)
{
    const pixel *src            = (const pixel *)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
#if BIT_DEPTH > 10
    const int shift4            = BIT_DEPTH - 10;
    const int offset4           = 1 << (shift4 - 1);
#endif

    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++) {
#if BIT_DEPTH > 10
            dst[x] = (src[x] + offset4) >> shift4;
#else
            dst[x] = src[x] << (10 - BIT_DEPTH);
#endif
        }
        src += src_stride;
        dst += MAX_PB_SIZE;
    }

}

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr_vvc_luma_h)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width)
{
    const pixel *src            = (const pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_dmvr_filters[mx];
    const int shift1            = BIT_DEPTH - 6;
    const int offset1           = 1 << (shift1 - 1);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = (DMVR_FILTER(src, 1) + offset1) >> shift1;
        src += src_stride;
        dst += MAX_PB_SIZE;
    }
}

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr_vvc_luma_v)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width)
{
    const pixel *src            = (pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    const int8_t *filter        = ff_vvc_dmvr_filters[my];
    const int shift1            = BIT_DEPTH - 6;
    const int offset1           = 1 << (shift1 - 1);
    for (int y = 0; y < height; y++) {
        for (int x = 0; x < width; x++)
            dst[x] = (DMVR_FILTER(src, src_stride) + offset1) >> shift1;
        src += src_stride;
        dst += MAX_PB_SIZE;
    }

}

//8.5.3.2.2 Luma sample bilinear interpolation process
static void FUNC(dmvr_vvc_luma_hv)(int16_t *dst, const uint8_t *_src, const ptrdiff_t _src_stride,
    const int height, const intptr_t mx, const intptr_t my, const int width)
{
    int x, y;
    const int8_t *filter;
    const pixel *src            = (pixel*)_src;
    const ptrdiff_t src_stride   = _src_stride / sizeof(pixel);
    int16_t tmp_array[(MAX_PB_SIZE + BILINEAR_EXTRA) * MAX_PB_SIZE];
    int16_t *tmp                = tmp_array;
    const int shift1            = BIT_DEPTH - 6;
    const int offset1           = 1 << (shift1 - 1);
    const int shift2            = 4;
    const int offset2           = 1 << (shift2 - 1);

    src   -= BILINEAR_EXTRA_BEFORE * src_stride;
    filter = ff_vvc_dmvr_filters[mx];
    for (y = 0; y < height + BILINEAR_EXTRA; y++) {
        for (x = 0; x < width; x++)
            tmp[x] = (DMVR_FILTER(src, 1) + offset1) >> shift1;
        src += src_stride;
        tmp += MAX_PB_SIZE;
    }

    tmp    = tmp_array + BILINEAR_EXTRA_BEFORE * MAX_PB_SIZE;
    filter = ff_vvc_dmvr_filters[my];
    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++)
            dst[x] = (DMVR_FILTER(tmp, MAX_PB_SIZE) + offset2) >> shift2;
        tmp += MAX_PB_SIZE;
        dst += MAX_PB_SIZE;
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

static void FUNC(vvc_loop_filter_luma)(uint8_t* _pix,
    ptrdiff_t _xstride, ptrdiff_t _ystride,
    int _beta, int _tc,
    uint8_t _no_p, uint8_t _no_q,
    uint8_t max_len_p, uint8_t max_len_q,
    int hor_ctu_edge)
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
#if BIT_DEPTH < 10
    const int tc = (_tc + (1 << (9 - BIT_DEPTH))) >> (10 - BIT_DEPTH);
#else
    const int tc = _tc << (BIT_DEPTH - 10);
#endif
    const int no_p = _no_p;
    const int no_q = _no_q;

    const int large_p = (max_len_p > 3 && !hor_ctu_edge);
    const int large_q = max_len_q > 3;
    const int beta = _beta << BIT_DEPTH - 8;

    const int beta_3 = beta >> 3;
    const int beta_2 = beta >> 2;
    const int tc25 = ((tc * 5 + 1) >> 1);


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
                        P0 = av_clip_pixel(p0 + delta0);
                    if (!no_q)
                        Q0 = av_clip_pixel(q0 - delta0);
                    if (!no_p && nd_p > 1) {
                        const int deltap1 = av_clip((((p2 + p0 + 1) >> 1) - p1 + delta0) >> 1, -tc_2, tc_2);
                        P1 = av_clip_pixel(p1 + deltap1);
                    }
                    if (!no_q && nd_q > 1) {
                        const int deltaq1 = av_clip((((q2 + q0 + 1) >> 1) - q1 - delta0) >> 1, -tc_2, tc_2);
                        Q1 = av_clip_pixel(q1 + deltaq1);
                    }
                }
                pix += ystride;
            }
        }
    }
}

static void FUNC(vvc_loop_filter_chroma)(uint8_t *_pix, const ptrdiff_t  _xstride,
    const ptrdiff_t _ystride, const int _beta, const int _tc, const uint8_t no_p, const uint8_t no_q,
    const int shift,  int max_len_p, int max_len_q)
{
    pixel *pix        = (pixel *)_pix;
    const ptrdiff_t xstride = _xstride / sizeof(pixel);
    const ptrdiff_t ystride = _ystride / sizeof(pixel);
    const int end  = shift ? 2 : 4;
    const int beta = _beta << (BIT_DEPTH - 8);
    const int beta_3 = beta >> 3;
    const int beta_2 = beta >> 2;

#if BIT_DEPTH < 10
    const int tc = (_tc + (1 << (9 - BIT_DEPTH))) >> (10 - BIT_DEPTH);
#else
    const int tc = _tc << (BIT_DEPTH - 10);
#endif
    const int tc25 = ((tc * 5 + 1) >> 1);

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
                P0 = av_clip_pixel(p0 + delta0);
            if (!no_q)
                Q0 = av_clip_pixel(q0 - delta0);
            pix += ystride;
        }
    }
}

static void FUNC(vvc_h_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
    int beta, int32_t tc, uint8_t no_p, uint8_t no_q, int shift, int max_len_p, int max_len_q)
{
    FUNC(vvc_loop_filter_chroma)(pix, stride, sizeof(pixel), beta, tc, no_p, no_q, shift, max_len_p, max_len_q);
}

static void FUNC(vvc_v_loop_filter_chroma)(uint8_t *pix, ptrdiff_t stride,
    int beta, int32_t tc, uint8_t no_p, uint8_t no_q, int shift, int max_len_p, int max_len_q)
{
    FUNC(vvc_loop_filter_chroma)(pix, sizeof(pixel), stride, beta, tc, no_p, no_q, shift, max_len_p, max_len_q);
}

static void FUNC(vvc_h_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                          int beta, int32_t tc, uint8_t no_p,
                                          uint8_t no_q, uint8_t max_len_p, uint8_t max_len_q, int hor_ctu_edge)
{
    FUNC(vvc_loop_filter_luma)(pix, stride, sizeof(pixel),
                                beta, tc, no_p, no_q, max_len_p, max_len_q, hor_ctu_edge);
}

static void FUNC(vvc_v_loop_filter_luma)(uint8_t *pix, ptrdiff_t stride,
                                          int beta, int32_t tc, uint8_t no_p,
                                          uint8_t no_q, uint8_t max_len_p, uint8_t max_len_q)
{
    FUNC(vvc_loop_filter_luma)(pix, sizeof(pixel), stride,
                                beta, tc, no_p, no_q, max_len_p, max_len_q, 0);
}

static int FUNC(vvc_loop_ladf_level)(const uint8_t *_pix, const ptrdiff_t _xstride, const ptrdiff_t _ystride)
{
    const pixel *pix        = (pixel *)_pix;
    const ptrdiff_t xstride = _xstride / sizeof(pixel);
    const ptrdiff_t ystride = _ystride / sizeof(pixel);
    return (P0 + TP0 + Q0 + TQ0) >> 2;
}

static int FUNC(vvc_h_loop_ladf_level)(const uint8_t *pix, ptrdiff_t stride)
{
    return FUNC(vvc_loop_ladf_level)(pix, stride, sizeof(pixel));
}

static int FUNC(vvc_v_loop_ladf_level)(const uint8_t *pix, ptrdiff_t stride)
{
    return FUNC(vvc_loop_ladf_level)(pix, sizeof(pixel), stride);
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

static void FUNC(ff_vvc_itx_dsp_init)(VVCItxDSPContext *const itx)
{
#define VVC_ITX(TYPE, type, s)                                                  \
        itx->itx[TYPE][TX_SIZE_##s]      = itx_##type##_##s;                    \

#define VVC_ITX_COMMON(TYPE, type)                                              \
        VVC_ITX(TYPE, type, 4);                                                 \
        VVC_ITX(TYPE, type, 8);                                                 \
        VVC_ITX(TYPE, type, 16);                                                \
        VVC_ITX(TYPE, type, 32);

    itx->add_residual                = FUNC(add_residual);
    itx->add_residual_joint          = FUNC(add_residual_joint);
    itx->pred_residual_joint         = FUNC(pred_residual_joint);
    itx->transform_bdpcm             = FUNC(transform_bdpcm);
    VVC_ITX(DCT2, dct2, 2)
    VVC_ITX(DCT2, dct2, 64)
    VVC_ITX_COMMON(DCT2, dct2)
    VVC_ITX_COMMON(DCT8, dct8)
    VVC_ITX_COMMON(DST7, dst7)

#undef VVC_ITX
#undef VVC_ITX_COMMON
}
