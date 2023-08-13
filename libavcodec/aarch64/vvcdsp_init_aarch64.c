/*
 * Copyright (c) 2023 Shaun Loo
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

#include <stdint.h>

#include "libavutil/attributes.h"
#include "libavutil/cpu.h"
#include "libavutil/aarch64/cpu.h"
#include "libavcodec/vvc/vvcdsp.h"

void ff_vvc_sao_band_filter_8x8_8_neon(uint8_t *_dst, uint8_t *_src,
                                  ptrdiff_t stride_dst, ptrdiff_t stride_src,
                                  int16_t *sao_offset_val, int sao_left_class,
                                  int width, int height);
void ff_vvc_sao_edge_filter_16x16_8_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride_dst,
                                          const int16_t *sao_offset_val, int eo, int width, int height);
void ff_vvc_sao_edge_filter_8x8_8_neon(uint8_t *dst, const uint8_t *src, ptrdiff_t stride_dst,
                                        const int16_t *sao_offset_val, int eo, int width, int height);

av_cold void ff_vvc_dsp_init_aarch64(VVCDSPContext *c, const int bit_depth) {
	if (!have_neon(av_get_cpu_flags())) return;
	if (bit_depth == 8) {
        	c->sao.band_filter[0]          =
        	c->sao.band_filter[1]          =
        	c->sao.band_filter[2]          =
        	c->sao.band_filter[3]          =
		c->sao.band_filter[4]	       =
		c->sao.band_filter[5]	       =
		c->sao.band_filter[6]	       =
		c->sao.band_filter[7]	       =
        	c->sao.band_filter[8]          = ff_vvc_sao_band_filter_8x8_8_neon;
        	c->sao.edge_filter[0]          = ff_vvc_sao_edge_filter_8x8_8_neon;
        	c->sao.edge_filter[1]          =
        	c->sao.edge_filter[2]          =
        	c->sao.edge_filter[3]          =
		c->sao.edge_filter[4]	       =
	        c->sao.edge_filter[5]	       =
		c->sao.edge_filter[6]	       =
		c->sao.edge_filter[7]	       =
        	c->sao.edge_filter[8]          = ff_vvc_sao_edge_filter_16x16_8_neon;
	}
}
