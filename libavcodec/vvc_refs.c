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

#include "libavutil/avassert.h"
#include "libavutil/pixdesc.h"

#include "internal.h"
#include "thread.h"
#include "vvc_thread.h"
#include "vvc.h"
#include "vvcdec.h"

void ff_vvc_unref_frame(VVCFrameContext *fc, VVCFrame *frame, int flags)
{
    /* frame->frame can be NULL if context init failed */
    if (!frame->frame || !frame->frame->buf[0])
        return;

    frame->flags &= ~flags;
    if (!frame->flags) {
        ff_thread_release_ext_buffer(fc->avctx, &frame->tf);

        av_buffer_unref(&frame->progress_buf);

        av_buffer_unref(&frame->tab_mvf_buf);
        frame->tab_mvf = NULL;

        av_buffer_unref(&frame->rpl_buf);
        av_buffer_unref(&frame->rpl_tab_buf);
        frame->rpl_tab    = NULL;
        frame->refPicList = NULL;

        frame->collocated_ref = NULL;

#if 0
        av_buffer_unref(&frame->hwaccel_priv_buf);
        frame->hwaccel_picture_private = NULL;
#endif
    }
}

const RefPicList *ff_vvc_get_ref_list(const VVCFrameContext *fc, const VVCFrame *ref, int x0, int y0)
{
    int x_cb         = x0 >> fc->ps.sps->ctb_log2_size_y;
    int y_cb         = y0 >> fc->ps.sps->ctb_log2_size_y;
    int pic_width_cb = fc->ps.pps->ctb_width;
    int ctb_addr_rs  = y_cb * pic_width_cb + x_cb;

    return (const RefPicList *)ref->rpl_tab[ctb_addr_rs];
}

void ff_vvc_clear_refs(VVCFrameContext *fc)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++)
        ff_vvc_unref_frame(fc, &fc->DPB[i],
                            VVC_FRAME_FLAG_SHORT_REF |
                            VVC_FRAME_FLAG_LONG_REF);
}
#if 0
void ff_vvc_flush_dpb(VVCContext *s)
{
    int i;
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++)
        ff_vvc_unref_frame(s, &fc->DPB[i], ~0);
}
#endif

static void free_progress(void *opaque, uint8_t *data)
{
    FrameProgress *p = (FrameProgress*)data;
    pthread_cond_destroy(&p->cond);
    pthread_mutex_destroy(&p->lock);
    av_free(data);
}

static AVBufferRef *alloc_progress(void)
{
    int ret;
    AVBufferRef *buf;
    FrameProgress *p = av_mallocz(sizeof(FrameProgress));

    if (!p)
        return NULL;
    ret = pthread_cond_init(&p->cond, NULL);
    if (ret) {
        av_free(p);
        return NULL;
    }

    ret = pthread_mutex_init(&p->lock, NULL);
    if (ret) {
        pthread_cond_destroy(&p->cond);
        av_free(p);
        return NULL;
    }
    buf = av_buffer_create((void*)p, sizeof(*p), free_progress, NULL, 0);
    if (!buf)
        free_progress(NULL, (void*)p);
    return buf;
}

static VVCFrame *alloc_frame(VVCContext *s, VVCFrameContext *fc)
{
    const VVCPPS *pps = s->ps.pps;
    int i, j, ret;
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];
        if (frame->frame->buf[0])
            continue;

        ret = ff_thread_get_ext_buffer(fc->avctx, &frame->tf,
                                   AV_GET_BUFFER_FLAG_REF);
        if (ret < 0)
            return NULL;

        frame->rpl_buf = av_buffer_allocz(fc->pkt.nb_nals * sizeof(RefPicListTab));
        if (!frame->rpl_buf)
            goto fail;

        frame->tab_mvf_buf = av_buffer_pool_get(fc->tab_mvf_pool);
        if (!frame->tab_mvf_buf)
            goto fail;
        frame->tab_mvf = (MvField *)frame->tab_mvf_buf->data;
        //fixme: remove this
        memset(frame->tab_mvf, 0, frame->tab_mvf_buf->size);

        frame->rpl_tab_buf = av_buffer_pool_get(fc->rpl_tab_pool);
        if (!frame->rpl_tab_buf)
            goto fail;
        frame->rpl_tab   = (RefPicListTab **)frame->rpl_tab_buf->data;
        frame->ctb_count = pps->ctb_width * pps->ctb_height;
        for (j = 0; j < frame->ctb_count; j++)
            frame->rpl_tab[j] = (RefPicListTab *)frame->rpl_buf->data;


        frame->progress_buf = alloc_progress();
        if (!frame->progress_buf)
            goto fail;

#if 0
        frame->frame->top_field_first  = s->sei.picture_timing.picture_struct == AV_PICTURE_STRUCTURE_TOP_FIELD;
        frame->frame->interlaced_frame = (s->sei.picture_timing.picture_struct == AV_PICTURE_STRUCTURE_TOP_FIELD) || (s->sei.picture_timing.picture_struct == AV_PICTURE_STRUCTURE_BOTTOM_FIELD);
        if (s->avctx->hwaccel) {
            const AVHWAccel *hwaccel = s->avctx->hwaccel;
            av_assert0(!frame->hwaccel_picture_private);
            if (hwaccel->frame_priv_data_size) {
                frame->hwaccel_priv_buf = av_buffer_allocz(hwaccel->frame_priv_data_size);
                if (!frame->hwaccel_priv_buf)
                    goto fail;
                frame->hwaccel_picture_private = frame->hwaccel_priv_buf->data;
            }
        }
#endif
        return frame;
fail:
        ff_vvc_unref_frame(fc, frame, ~0);
        return NULL;
    }
    av_log(s->avctx, AV_LOG_ERROR, "Error allocating frame, DPB full.\n");
    return NULL;
}

int ff_vvc_set_new_ref(VVCContext *s, VVCFrameContext *fc, AVFrame **frame)
{
    const VVCPH *ph= fc->ps.ph;
    const int poc = ph->poc;
    VVCFrame *ref;
    int i;

    /* check that this POC doesn't already exist */
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];

        if (frame->frame->buf[0] && frame->sequence == s->seq_decode &&
            frame->poc == poc) {
            av_log(s->avctx, AV_LOG_ERROR, "Duplicate POC in a sequence: %d.\n",
                   poc);
            return AVERROR_INVALIDDATA;
        }
    }

    ref = alloc_frame(s, fc);
    if (!ref)
        return AVERROR(ENOMEM);

    *frame = ref->frame;
    fc->ref = ref;

    if (s->no_output_before_recovery_flag && (IS_RASL(s) || !GDR_IS_RECOVERED(s)))
        ref->flags = 0;
    else if (ph->pic_output_flag)
        ref->flags = VVC_FRAME_FLAG_OUTPUT;

    if (!ph->non_ref_pic_flag)
        ref->flags |= VVC_FRAME_FLAG_SHORT_REF;

    ref->poc      = poc;
    ref->sequence = s->seq_decode;
    ref->frame->crop_left   = fc->ps.pps->conf_win.left_offset;
    ref->frame->crop_right  = fc->ps.pps->conf_win.right_offset;
    ref->frame->crop_top    = fc->ps.pps->conf_win.top_offset;
    ref->frame->crop_bottom = fc->ps.pps->conf_win.bottom_offset;

    return 0;
}

int ff_vvc_output_frame(VVCContext *s, VVCFrameContext *fc, AVFrame *out, const int no_output_of_prior_pics_flag, int flush)
{
    const VVCSPS *sps = fc->ps.sps;
    do {
        int nb_output = 0;
        int min_poc   = INT_MAX;
        int i, min_idx, ret;

        if (no_output_of_prior_pics_flag) {
            for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
                VVCFrame *frame = &fc->DPB[i];
                if (!(frame->flags & VVC_FRAME_FLAG_BUMPING) && frame->poc != fc->ps.ph->poc &&
                        frame->sequence == s->seq_output) {
                    ff_vvc_unref_frame(fc, frame, VVC_FRAME_FLAG_OUTPUT);
                }
            }
        }

        for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            VVCFrame *frame = &fc->DPB[i];
            if ((frame->flags & VVC_FRAME_FLAG_OUTPUT) &&
                frame->sequence == s->seq_output) {
                nb_output++;
                if (frame->poc < min_poc || nb_output == 1) {
                    min_poc = frame->poc;
                    min_idx = i;
                }
            }
        }

        /* wait for more frames before output */
        if (!flush && s->seq_output == s->seq_decode && sps &&
            nb_output <= sps->dpb.max_dec_pic_buffering[sps->max_sublayers - 1])
            return 0;

        if (nb_output) {
            VVCFrame *frame = &fc->DPB[min_idx];

            ret = av_frame_ref(out, frame->frame);
            if (frame->flags & VVC_FRAME_FLAG_BUMPING)
                ff_vvc_unref_frame(fc, frame, VVC_FRAME_FLAG_OUTPUT | VVC_FRAME_FLAG_BUMPING);
            else
                ff_vvc_unref_frame(fc, frame, VVC_FRAME_FLAG_OUTPUT);
            if (ret < 0)
                return ret;

            av_log(s->avctx, AV_LOG_DEBUG,
                   "Output frame with POC %d.\n", frame->poc);
            return 1;
        }

        if (s->seq_output != s->seq_decode)
            s->seq_output = (s->seq_output + 1) & 0xff;
        else
            break;
    } while (1);
    return 0;
}

void ff_vvc_bump_frame(VVCContext *s, VVCFrameContext *fc)
{
    const VVCSPS *sps = fc->ps.sps;
    const int poc = fc->ps.ph->poc;
    int dpb = 0;
    int min_poc = INT_MAX;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];
        if ((frame->flags) &&
            frame->sequence == s->seq_output &&
            frame->poc != poc) {
            dpb++;
        }
    }

    if (sps && dpb >= sps->dpb.max_dec_pic_buffering[sps->max_sublayers - 1]) {
        for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            VVCFrame *frame = &fc->DPB[i];
            if ((frame->flags) &&
                frame->sequence == s->seq_output &&
                frame->poc != poc) {
                if (frame->flags == VVC_FRAME_FLAG_OUTPUT && frame->poc < min_poc) {
                    min_poc = frame->poc;
                }
            }
        }

        for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
            VVCFrame *frame = &fc->DPB[i];
            if (frame->flags & VVC_FRAME_FLAG_OUTPUT &&
                frame->sequence == s->seq_output &&
                frame->poc <= min_poc) {
                frame->flags |= VVC_FRAME_FLAG_BUMPING;
            }
        }

        dpb--;
    }
}

static VVCFrame *find_ref_idx(VVCContext *s, VVCFrameContext *fc, int poc, uint8_t use_msb)
{
    int mask = use_msb ? ~0 : fc->ps.sps->max_pic_order_cnt_lsb - 1;
    int i;

    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *ref = &fc->DPB[i];
        if (ref->frame->buf[0] && ref->sequence == s->seq_decode) {
            if ((ref->poc & mask) == poc)
                return ref;
        }
    }

#if 0
    if (s->nal_unit_type != VVC_CRA_NUT && !IS_BLA(s))
        av_log(s->avctx, AV_LOG_ERROR,
               "Could not find ref with POC %d\n", poc);
#endif
    return NULL;
}

static void mark_ref(VVCFrame *frame, int flag)
{
    frame->flags &= ~(VVC_FRAME_FLAG_LONG_REF | VVC_FRAME_FLAG_SHORT_REF);
    frame->flags |= flag;
}

static VVCFrame *generate_missing_ref(VVCContext *s, VVCFrameContext *fc, int poc)
{
    const VVCSPS *sps = fc->ps.sps;
    VVCFrame *frame;
    int i, y;

    frame = alloc_frame(s, fc);
    if (!frame)
        return NULL;

    if (!s->avctx->hwaccel) {
        if (!sps->pixel_shift) {
            for (i = 0; frame->frame->buf[i]; i++)
                memset(frame->frame->buf[i]->data, 1 << (sps->bit_depth - 1),
                       frame->frame->buf[i]->size);
        } else {
            for (i = 0; frame->frame->data[i]; i++)
                for (y = 0; y < (sps->height >> sps->vshift[i]); y++) {
                    uint8_t *dst = frame->frame->data[i] + y * frame->frame->linesize[i];
                    AV_WN16(dst, 1 << (sps->bit_depth - 1));
                    av_memcpy_backptr(dst + 2, 2, 2*(sps->width >> sps->hshift[i]) - 2);
                }
        }
    }

    frame->poc      = poc;
    frame->sequence = s->seq_decode;
    frame->flags    = 0;

    ff_vvc_report_progress(frame, INT_MAX);

    return frame;
}

/* add a reference with the given poc to the list and mark it as used in DPB */
static int add_candidate_ref(VVCContext *s, VVCFrameContext *fc, RefPicList *list,
                             int poc, int ref_flag, uint8_t use_msb)
{
    VVCFrame *ref = find_ref_idx(s, fc, poc, use_msb);

    if (ref == fc->ref || list->nb_refs >= VVC_MAX_REF_ENTRIES)
        return AVERROR_INVALIDDATA;

    if (!ref) {
        ref = generate_missing_ref(s, fc, poc);
        if (!ref)
            return AVERROR(ENOMEM);
    }

    list->list[list->nb_refs] = poc;
    list->ref[list->nb_refs]  = ref;
    list->isLongTerm[list->nb_refs] = ref_flag & VVC_FRAME_FLAG_LONG_REF;
    list->nb_refs++;

    mark_ref(ref, ref_flag);
    return 0;
}

static int init_slice_rpl(const VVCFrameContext *fc, const SliceContext *sc)
{
    VVCFrame *frame = fc->ref;
    const VVCSH *sh = &sc->sh;

    if (sc->slice_idx >= frame->rpl_buf->size / sizeof(RefPicListTab))
        return AVERROR_INVALIDDATA;

    for (int i = 0; i < sh->num_ctus_in_curr_slice; i++) {
        const int rs = sh->ctb_addr_in_curr_slice[i];
        frame->rpl_tab[rs] = (RefPicListTab *)frame->rpl_buf->data + sc->slice_idx;
    }

    frame->refPicList = (RefPicList *)frame->rpl_tab[sh->ctb_addr_in_curr_slice[0]];

    return 0;
}

int ff_vvc_slice_rpl(VVCContext *s, VVCFrameContext *fc, const SliceContext *sc)
{
    const VVCSH *sh = &sc->sh;
    int i, ret = 0;

    init_slice_rpl(fc, sc);

    for (i = 0; i < 2; i++) {
        const VVCRefPicListStruct *rpls = sh->rpls + i;
        RefPicList *rpl = fc->ref->refPicList + i;
        int poc_base = fc->ps.ph->poc;
        rpl->nb_refs = 0;
        for (int j = 0; j < rpls->num_ref_entries; j++) {
            const VVCRefPicListStructEntry *ref = &rpls->entries[j];
            int poc;
            if (!ref->inter_layer_ref_pic_flag) {
                int use_msb = 1;
                int ref_flag;
                if (ref->st_ref_pic_flag) {
                    poc = poc_base + ref->delta_poc_val_st;
                    poc_base = poc;
                    ref_flag = VVC_FRAME_FLAG_SHORT_REF;
                } else {
                    use_msb = ref->lt_msb_flag;
                    poc = ref->lt_poc;
                    ref_flag = VVC_FRAME_FLAG_LONG_REF;
                }
                ret = add_candidate_ref(s, fc, rpl, poc, ref_flag, use_msb);
                if (ret < 0)
                    goto fail;
            } else {
                avpriv_request_sample(fc->avctx, "Inter layer ref");
                ret = AVERROR_PATCHWELCOME;
                goto fail;
            }
        }
        if (sh->collocated_list == i &&
            sh->collocated_ref_idx < rpl->nb_refs)
            fc->ref->collocated_ref = rpl->ref[sh->collocated_ref_idx];
    }
fail:
    return ret;
}

int ff_vvc_frame_rpl(VVCContext *s, VVCFrameContext *fc, const SliceContext *sc)
{
    int i, ret = 0;

    /* clear the reference flags on all frames except the current one */
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++) {
        VVCFrame *frame = &fc->DPB[i];

        if (frame == fc->ref)
            continue;

        mark_ref(frame, 0);
    }

    if ((ret = ff_vvc_slice_rpl(s, fc, sc)) < 0)
        goto fail;

fail:
    /* release any frames that are now unused */
    for (i = 0; i < FF_ARRAY_ELEMS(fc->DPB); i++)
        ff_vvc_unref_frame(fc, &fc->DPB[i], 0);
    return ret;
}
