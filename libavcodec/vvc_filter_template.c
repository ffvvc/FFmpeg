/*
 * VVC filter dsp
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

static void FUNC(ff_vvc_sao_dsp_init)(VVCSAODSPContext *const sao)
{
    for (int i = 0; i < FF_ARRAY_ELEMS(sao->band_filter); i++)
        sao->band_filter[i] = FUNC(sao_band_filter);
    for (int i = 0; i < FF_ARRAY_ELEMS(sao->edge_filter); i++)
        sao->edge_filter[i] = FUNC(sao_edge_filter);
    sao->edge_restore[0] = FUNC(sao_edge_restore_0);
    sao->edge_restore[1] = FUNC(sao_edge_restore_1);
}

static void FUNC(ff_vvc_alf_dsp_init)(VVCALFDSPContext *const alf)
{
    alf->filter[LUMA]       = FUNC(alf_filter_luma);
    alf->filter[CHROMA]     = FUNC(alf_filter_chroma);
    alf->filter_vb[LUMA]    = FUNC(alf_filter_luma_vb);
    alf->filter_vb[CHROMA]  = FUNC(alf_filter_chroma_vb);
    alf->filter_cc          = FUNC(alf_filter_cc);
    alf->get_coeff_and_clip = FUNC(alf_get_coeff_and_clip);
}
