/*
 * VVC thread logic
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

#include "vvc_thread.h"
#include "vvc_ctu.h"
#include "vvc_mvs.h"

typedef struct VVCRowThread {
    VVCTask reconstruct_task;
    VVCTask deblock_v_task;
    VVCTask sao_task;
    atomic_int  alf_progress;
} VVCRowThread;

typedef struct VVCColThread {
    VVCTask deblock_h_task;
} VVCColThread;

struct VVCFrameThread {
    // error return for tasks
    atomic_int ret;

    atomic_uchar *avails;

    VVCRowThread *rows;
    VVCColThread *cols;
    VVCTask *tasks;

    int ctu_size;
    int ctu_width;
    int ctu_height;
    int ctu_count;

    //protected by lock
    int nb_scheduled_tasks;
    int nb_parse_tasks;
    int alf_row_progress;

    pthread_mutex_t lock;
    pthread_cond_t  cond;
};

static int get_avail(const VVCFrameThread *ft, const int rx, const int ry, const VVCTaskType type)
{
    atomic_uchar *avail;
    if (rx < 0 || ry < 0)
        return 1;
    avail = ft->avails + FFMIN(ry,  ft->ctu_height - 1)* ft->ctu_width + FFMIN(rx, ft->ctu_width - 1);
    return atomic_load(avail) & (1 << type);
}

static void set_avail(const VVCFrameThread *ft, const int rx, const int ry, const VVCTaskType type)
{
    atomic_uchar *avail = ft->avails + ry * ft->ctu_width + rx;
    if (rx < 0 || rx >= ft->ctu_width || ry < 0 || ry >= ft->ctu_height)
        return;
    atomic_fetch_or(avail, 1 << type);
}

void ff_vvc_task_init(VVCTask *task, VVCTaskType type, VVCFrameContext *fc)
{
    memset(task, 0, sizeof(*task));
    task->type           = type;
    task->fc             = fc;
    task->decode_order   = fc->decode_order;
}

void ff_vvc_parse_task_init(VVCTask *t, VVCTaskType type, VVCFrameContext *fc,
    SliceContext *sc, EntryPoint *ep, const int ctu_addr)
{
    const VVCFrameThread *ft = fc->frame_thread;
    const int rs = sc->sh.ctb_addr_in_curr_slice[ctu_addr];

    ff_vvc_task_init(t, type, fc);
    t->sc = sc;
    t->ep = ep;
    t->ctb_addr_in_slice = ctu_addr;
    t->rx = rs % ft->ctu_width;
    t->ry = rs / ft->ctu_width;
}

VVCTask* ff_vvc_task_alloc(void)
{
    return av_malloc(sizeof(VVCTask));
}

static int is_parse_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    av_assert0(t->type == VVC_TASK_TYPE_PARSE);
    if (fc->ps.sps->entropy_coding_sync_enabled_flag && t->ry != fc->ps.pps->ctb_to_row_bd[t->ry])
        return get_avail(fc->frame_thread, t->rx, t->ry - 1, VVC_TASK_TYPE_PARSE);
    return 1;
}

static int is_inter_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    av_assert0(t->type == VVC_TASK_TYPE_INTER);
    return 1;
}

static int is_recon_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    const VVCFrameThread *ft = fc->frame_thread;

    av_assert0(t->type == VVC_TASK_TYPE_RECON);
    return get_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_INTER) &&
        get_avail(ft, t->rx + 1, t->ry - 1, VVC_TASK_TYPE_RECON) &&
        get_avail(ft, t->rx - 1, t->ry, VVC_TASK_TYPE_RECON);
}

static int is_lmcs_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    const VVCFrameThread *ft = fc->frame_thread;

    av_assert0(t->type == VVC_TASK_TYPE_LMCS);
    return get_avail(ft, t->rx + 1, t->ry + 1, VVC_TASK_TYPE_RECON) &&
        get_avail(ft, t->rx, t->ry + 1, VVC_TASK_TYPE_RECON) &&
        get_avail(ft, t->rx + 1, t->ry, VVC_TASK_TYPE_RECON);
}

static int is_deblock_v_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    const VVCFrameThread *ft  = fc->frame_thread;

    av_assert0(t->type == VVC_TASK_TYPE_DEBLOCK_V);
    return get_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_LMCS) &&
        get_avail(ft, t->rx + 1, t->ry, VVC_TASK_TYPE_LMCS);
}

static int is_deblock_h_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    const VVCFrameThread *ft = fc->frame_thread;

    av_assert0(t->type == VVC_TASK_TYPE_DEBLOCK_H);
    return get_avail(ft, t->rx - 1, t->ry, VVC_TASK_TYPE_DEBLOCK_H) &&
        get_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_DEBLOCK_V);
}

static int is_sao_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    av_assert0(t->type == VVC_TASK_TYPE_SAO);
    return get_avail(fc->frame_thread, t->rx + 1, t->ry - 1, VVC_TASK_TYPE_SAO) &&
        get_avail(fc->frame_thread, t->rx - 1, t->ry + 1, VVC_TASK_TYPE_DEBLOCK_H) &&
        get_avail(fc->frame_thread, t->rx, t->ry + 1, VVC_TASK_TYPE_DEBLOCK_H) &&
        get_avail(fc->frame_thread, t->rx + 1, t->ry + 1, VVC_TASK_TYPE_DEBLOCK_H);
}

static int is_alf_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    av_assert0(t->type == VVC_TASK_TYPE_ALF);
    return 1;
}

typedef int (*is_ready_func)(const VVCFrameContext *fc, const VVCTask *t);

int ff_vvc_task_ready(const Task *_t, void *user_data)
{
    const VVCTask *t            = (const VVCTask*)_t;
    const VVCFrameThread *ft    = t->fc->frame_thread;
    int ready;
    is_ready_func is_ready[]    = {
        is_parse_ready,
        is_inter_ready,
        is_recon_ready,
        is_lmcs_ready,
        is_deblock_v_ready,
        is_deblock_h_ready,
        is_sao_ready,
        is_alf_ready,
    };

    if (atomic_load(&ft->ret))
        return 1;
    ready = is_ready[t->type](t->fc, t);

    return ready;
}

int ff_vvc_task_priority_higher(const Task *_a, const Task *_b)
{
    const VVCTask *a = (const VVCTask*)_a;
    const VVCTask *b = (const VVCTask*)_b;
    //order by frame decoder order
    if (a->decode_order != b->decode_order)
        return a->decode_order < b->decode_order;

    if (a->type == b->type) {
        // order by y
        if (a->ry != b->ry)
            return a->ry < b->ry;
        // order by x
        return a->rx < b->rx;
    }
    if (a->type != VVC_TASK_TYPE_PARSE && b->type != VVC_TASK_TYPE_PARSE) {
        // order by y
        if (a->ry != b->ry)
            return a->ry < b->ry;
        // order by x
        if (a->rx != b->rx)
            return a->rx < b->rx;
    }
    // order by type
    return a->type < b->type;
}

static void add_task(VVCContext *s, VVCTask *t, const VVCTaskType type)
{
    t->type = type;
    ff_vvc_frame_add_task(s, t);
}

static int run_parse(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCPPS *pps       = fc->ps.pps;
    SliceContext *sc        = t->sc;
    const VVCSH *sh         = &sc->sh;
    EntryPoint *ep          = t->ep;
    VVCFrameThread *ft      = fc->frame_thread;
    int ret, rs, prev_ry;

    lc->sc = sc;
    lc->ep = ep;

    //reconstruct one line a time
    rs = sh->ctb_addr_in_curr_slice[t->ctb_addr_in_slice];
    do {

        prev_ry = t->ry;

        ret = ff_vvc_coding_tree_unit(lc, t->ctb_addr_in_slice, rs, t->rx, t->ry);
        if (ret < 0)
            return ret;

        set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_PARSE);
        add_task(s, ft->tasks + rs, VVC_TASK_TYPE_INTER);

        if (fc->ps.sps->entropy_coding_sync_enabled_flag && t->rx == pps->ctb_to_col_bd[t->rx]) {
            EntryPoint *next = ep + 1;
            if (next < sc->eps + sc->nb_eps) {
                memcpy(next->cabac_state, ep->cabac_state, sizeof(next->cabac_state));
                av_assert0(!next->parse_task->type);
                ff_vvc_frame_add_task(s, next->parse_task);
            }
        }

        t->ctb_addr_in_slice++;
        if (t->ctb_addr_in_slice >= ep->ctu_addr_last)
            break;

        rs = sh->ctb_addr_in_curr_slice[t->ctb_addr_in_slice];
        t->rx = rs % ft->ctu_width;
        t->ry = rs / ft->ctu_width;
    } while (t->ry == prev_ry && is_parse_ready(fc, t));

    if (t->ctb_addr_in_slice < ep->ctu_addr_last)
        ff_vvc_frame_add_task(s, t);

    return 0;
}

static int run_inter(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int rs        = t->ry * ft->ctu_width + t->rx;
    const int slice_idx = fc->tab.slice_idx[rs];

    if (slice_idx != -1) {
        lc->sc = fc->slices[slice_idx];
        ff_vvc_predict_inter(lc, rs);
        if (!t->rx)
            ff_vvc_frame_add_task(s, &ft->rows[t->ry].reconstruct_task);
    }
    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_INTER);

    return 0;
}

static int run_recon(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;

    do {
        const int rs = t->ry * ft->ctu_width + t->rx;
        const int slice_idx = fc->tab.slice_idx[rs];

        if (slice_idx != -1) {
            lc->sc = fc->slices[slice_idx];
            ff_vvc_reconstruct(lc, rs, t->rx, t->ry);
        }

        set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_RECON);
        add_task(s, ft->tasks + rs, VVC_TASK_TYPE_LMCS);

        t->rx++;
    } while (t->rx < ft->ctu_width && is_recon_ready(fc, t));

    if (t->rx < ft->ctu_width)
        ff_vvc_frame_add_task(s, t);
    return 0;
}

static int run_lmcs(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int ctu_size  = ft->ctu_size;
    const int x0        = t->rx * ctu_size;
    const int y0        = t->ry * ctu_size;
    const int rs        = t->ry * ft->ctu_width + t->rx;
    const int slice_idx = fc->tab.slice_idx[rs];

    if (slice_idx != -1) {
        lc->sc = fc->slices[slice_idx];
        ff_vvc_lmcs_filter(lc, x0, y0);
    }
    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_LMCS);
    if (!t->rx)
        add_task(s, &ft->rows[t->ry].deblock_v_task, VVC_TASK_TYPE_DEBLOCK_V);

    return 0;
}

static int run_deblock_v(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    int rs              = t->ry * ft->ctu_width + t->rx;
    const int ctb_size  = ft->ctu_size;

    do {
        const int x0        = t->rx * ctb_size;
        const int y0        = t->ry * ctb_size;
        const int slice_idx = fc->tab.slice_idx[rs];

        if (slice_idx != -1) {
            lc->sc = fc->slices[slice_idx];
            if (!lc->sc->sh.deblocking_filter_disabled_flag) {
                ff_vvc_decode_neighbour(lc, x0, y0, t->rx, t->ry, rs);
                ff_vvc_deblock_vertical(lc, x0, y0);
            }
        }

        set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_DEBLOCK_V);

        if (!t->ry)
            add_task(s, &ft->cols[t->rx].deblock_h_task , VVC_TASK_TYPE_DEBLOCK_H);

        t->rx++;
        rs++;
    } while (t->rx < ft->ctu_width && is_deblock_v_ready(fc, t));

    if (t->rx < ft->ctu_width)
        ff_vvc_frame_add_task(s, t);

    return 0;
}

static int run_deblock_h(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int ctb_size  = ft->ctu_size;
    int rs              = t->ry * ft->ctu_width + t->rx;

    do {
        const int x0 = t->rx * ctb_size;
        const int y0 = t->ry * ctb_size;
        const int slice_idx = fc->tab.slice_idx[rs];

        if (slice_idx != -1) {
            lc->sc = fc->slices[slice_idx];
            if (!lc->sc->sh.deblocking_filter_disabled_flag) {
                ff_vvc_decode_neighbour(lc, x0, y0, t->rx, t->ry, rs);
                ff_vvc_deblock_horizontal(lc, x0, y0);
            }
        }

        set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_DEBLOCK_H);

        if (!t->rx)
            add_task(s, &ft->rows[t->ry].sao_task, VVC_TASK_TYPE_SAO);

        rs += ft->ctu_width;
        t->ry++;
    } while (t->ry < ft->ctu_height && is_deblock_h_ready(fc, t));

    if (t->ry < ft->ctu_height)
        ff_vvc_frame_add_task(s, t);

    return 0;
}

static void add_alf_tasks(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    VVCTask *at = ft->tasks + ft->ctu_width * t->ry + t->rx;
    if (t->ry > 0) {
        VVCTask *top = at - ft->ctu_width;
        if (t->rx > 0)
            add_task(s, top - 1, VVC_TASK_TYPE_ALF);
        if (t->rx == ft->ctu_width - 1)
            add_task(s, top, VVC_TASK_TYPE_ALF);
    }
    if (t->ry == ft->ctu_height - 1) {
        if (t->rx > 0)
            add_task(s, at - 1, VVC_TASK_TYPE_ALF);
        if (t->rx == ft->ctu_width - 1)
            add_task(s, at, VVC_TASK_TYPE_ALF);
    }

}

static int run_sao(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    int rs              = t->ry * fc->ps.pps->ctb_width + t->rx;
    const int ctb_size  = ft->ctu_size;

    do {
        const int x0 = t->rx * ctb_size;
        const int y0 = t->ry * ctb_size;

        if (fc->ps.sps->sao_enabled_flag) {
            ff_vvc_decode_neighbour(lc, x0, y0, t->rx, t->ry, rs);
            ff_vvc_sao_filter(lc, x0, y0);
        }

        if (fc->ps.sps->alf_enabled_flag)
            ff_vvc_alf_copy_ctu_to_hv(lc, x0, y0);

        set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_SAO);

        add_alf_tasks(s, lc, t);

        rs++;
        t->rx++;
    } while (t->rx < ft->ctu_width && is_sao_ready(fc, t));

    if (t->rx < ft->ctu_width)
        ff_vvc_frame_add_task(s, t);

    return 0;
}

static void report_frame_progress(VVCFrameContext *fc, VVCTask *t)
{
    VVCFrameThread *ft  = fc->frame_thread;
    const int ctu_size  = ft->ctu_size;
    int old;

    if (atomic_fetch_add(&ft->rows[t->ry].alf_progress, 1) == ft->ctu_width - 1) {
        pthread_mutex_lock(&ft->lock);
        old = ft->alf_row_progress;
        while (ft->alf_row_progress < ft->ctu_height && atomic_load(&ft->rows[ft->alf_row_progress].alf_progress) == ft->ctu_width)
            ft->alf_row_progress++;
        if (old != ft->alf_row_progress) {
            const int progress = ft->alf_row_progress == ft->ctu_height ? INT_MAX : ft->alf_row_progress * ctu_size;
            ff_vvc_report_progress(fc->ref, progress);
        }
        pthread_mutex_unlock(&ft->lock);
    }
}

static int run_alf(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int ctu_size  = ft->ctu_size;
    const int x0 = t->rx * ctu_size;
    const int y0 = t->ry * ctu_size;

    if (fc->ps.sps->alf_enabled_flag) {
        const int slice_idx = CTB(fc->tab.slice_idx, t->rx, t->ry);
        if (slice_idx != -1) {
            lc->sc = fc->slices[slice_idx];
            ff_vvc_alf_filter(lc, x0, y0);
        }
    }
    ff_vvc_apply_dmvr_info_ctb(fc, x0, y0);
    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_ALF);
    report_frame_progress(fc, t);

    return 0;
}

static void finished_one_task(VVCFrameThread *ft, const VVCTaskType type)
{
    pthread_mutex_lock(&ft->lock);

    av_assert0(ft->nb_scheduled_tasks);
    ft->nb_scheduled_tasks--;

    if (type == VVC_TASK_TYPE_PARSE) {
        av_assert0(ft->nb_parse_tasks);
        ft->nb_parse_tasks--;
    }
    pthread_cond_broadcast(&ft->cond);

    pthread_mutex_unlock(&ft->lock);
}


#define VVC_THREAD_DEBUG
#ifdef VVC_THREAD_DEBUG
const static char* task_name[] = {
    "P",
    "I",
    "R",
    "L",
    "V",
    "H",
    "S",
    "A"
};
#endif

typedef int (*run_func)(VVCContext *s, VVCLocalContext *lc, VVCTask *t);

int ff_vvc_task_run(Task *_t, void *local_context, void *user_data)
{
    VVCTask *t              = (VVCTask*)_t;
    VVCContext *s           = (VVCContext *)user_data;
    VVCLocalContext *lc     = local_context;
    VVCFrameThread *ft      = t->fc->frame_thread;
    const VVCTaskType type  = t->type;
    int ret = 0;
    run_func run[] = {
        run_parse,
        run_inter,
        run_recon,
        run_lmcs,
        run_deblock_v,
        run_deblock_h,
        run_sao,
        run_alf,
    };

    lc->fc = t->fc;

#ifdef VVC_THREAD_DEBUG
    av_log(s->avctx, AV_LOG_DEBUG, "frame %5d, %s(%3d, %3d)\r\n", (int)t->fc->decode_order, task_name[t->type], t->rx, t->ry);
#endif

    if (!atomic_load(&ft->ret)) {
        if ((ret = run[t->type](s, lc, t)) < 0) {
#ifdef WIN32
            intptr_t zero = 0;
#else
            int zero = 0;
#endif
            atomic_compare_exchange_strong(&ft->ret, &zero, ret);
        }
    }

    // t->type may changed bun run(), we use a local copy of t->type
    finished_one_task(ft, type);

    return ret;
}

void ff_vvc_frame_thread_free(VVCFrameContext *fc)
{
    VVCFrameThread *ft = fc->frame_thread;

    if (!ft)
        return;

    pthread_mutex_destroy(&ft->lock);
    pthread_cond_destroy(&ft->cond);
    av_freep(&ft->avails);
    av_freep(&ft->cols);
    av_freep(&ft->rows);
    av_freep(&ft->tasks);
    av_freep(&ft);
}

int ff_vvc_frame_thread_init(VVCFrameContext *fc)
{
    const VVCSPS *sps = fc->ps.sps;
    const VVCPPS *pps = fc->ps.pps;
    VVCFrameThread *ft = fc->frame_thread;
    int ret;

    if (!ft || ft->ctu_width != pps->ctb_width ||
        ft->ctu_height != pps->ctb_height ||
        ft->ctu_size != sps->ctb_size_y) {

        ff_vvc_frame_thread_free(fc);
        ft = av_calloc(1, sizeof(*fc->frame_thread));
        if (!ft)
            return AVERROR(ENOMEM);

        ft->ctu_width  = fc->ps.pps->ctb_width;
        ft->ctu_height = fc->ps.pps->ctb_height;
        ft->ctu_count  = fc->ps.pps->ctb_count;
        ft->ctu_size   = fc->ps.sps->ctb_size_y;

        ft->rows = av_calloc(ft->ctu_height, sizeof(*ft->rows));
        if (!ft->rows)
            goto fail;

        for (int y = 0; y < ft->ctu_height; y++) {
            VVCRowThread *row = ft->rows + y;
            ff_vvc_task_init(&row->deblock_v_task, VVC_TASK_TYPE_DEBLOCK_V, fc);
            row->deblock_v_task.ry = y;
            ff_vvc_task_init(&row->sao_task, VVC_TASK_TYPE_SAO, fc);
            row->sao_task.ry = y;
            ff_vvc_task_init(&row->reconstruct_task, VVC_TASK_TYPE_RECON, fc);
            row->reconstruct_task.ry = y;
        }

        ft->cols = av_calloc(ft->ctu_width, sizeof(*ft->cols));
        if (!ft->cols)
            goto fail;
        for (int x = 0; x < ft->ctu_width; x++) {
            VVCColThread *col = ft->cols + x;
            ff_vvc_task_init(&col->deblock_h_task, VVC_TASK_TYPE_DEBLOCK_H, fc);
            col->deblock_h_task.rx = x;
        }

        ft->avails = av_calloc(ft->ctu_count, sizeof(*ft->avails));
        if (!ft->avails)
            goto fail;

        ft->tasks = av_calloc(ft->ctu_count, sizeof(*ft->tasks));
        if (!ft->tasks)
            goto fail;
        for (int rs = 0; rs < ft->ctu_count; rs++) {
            VVCTask *t = ft->tasks + rs;
            t->rx = rs % ft->ctu_width;
            t->ry = rs / ft->ctu_width;
            t->fc = fc;
        }

        if ((ret = pthread_cond_init(&ft->cond, NULL)))
            goto fail;

        if ((ret = pthread_mutex_init(&ft->lock, NULL))) {
            pthread_cond_destroy(&ft->cond);
            goto fail;
        }
    }

    ft->ret = 0;
    for (int y = 0; y < ft->ctu_height; y++) {
        VVCRowThread *row = ft->rows + y;

        row->reconstruct_task.rx = 0;
        row->alf_progress = 0;
        row->deblock_v_task.rx = 0;
        row->sao_task.rx = 0;
    }

    for (int x = 0; x < ft->ctu_width; x++) {
        VVCColThread *col = ft->cols + x;
        col->deblock_h_task.ry = 0;
    }

    for (int rs = 0; rs < ft->ctu_count; rs++) {
        ft->avails[rs] = 0;
        ft->tasks[rs].decode_order = fc->decode_order;
    }

    ft->alf_row_progress = 0;
    fc->frame_thread = ft;

    return 0;

fail:
    if (ft) {
        av_freep(&ft->avails);
        av_freep(&ft->cols);
        av_freep(&ft->rows);
        av_freep(&ft->tasks);
        av_freep(&ft);
    }

    return AVERROR(ENOMEM);
}

void ff_vvc_frame_add_task(VVCContext *s, VVCTask *t)
{
    VVCFrameContext *fc = t->fc;
    VVCFrameThread *ft  = fc->frame_thread;

    pthread_mutex_lock(&ft->lock);

    ft->nb_scheduled_tasks++;
    if (t->type == VVC_TASK_TYPE_PARSE)
        ft->nb_parse_tasks++;

    pthread_mutex_unlock(&ft->lock);

    ff_executor_execute(s->executor, &t->task);
}

int ff_vvc_frame_wait(VVCContext *s, VVCFrameContext *fc)
{
    VVCFrameThread *ft = fc->frame_thread;
    int check_missed_slices = 1;

    pthread_mutex_lock(&ft->lock);

    while (ft->nb_scheduled_tasks) {
        if (check_missed_slices && !ft->nb_parse_tasks) {
            // abort for missed slices
            for (int rs = 0; rs < ft->ctu_count; rs++){
                atomic_uchar mask = 1 << VVC_TASK_TYPE_PARSE;
                if (!(atomic_load(ft->avails + rs) & mask)) {
                    atomic_store(&ft->ret, AVERROR_INVALIDDATA);
                    // maybe all thread are waiting, let us wake up them
                    ff_executor_wakeup(s->executor);
                    break;
                }
            }
            check_missed_slices = 0;
        }
        pthread_cond_wait(&ft->cond, &ft->lock);
    }

    pthread_mutex_unlock(&ft->lock);
    ff_vvc_report_progress(fc->ref, INT_MAX);

#ifdef VVC_THREAD_DEBUG
    av_log(s->avctx, AV_LOG_DEBUG, "frame %5d done\r\n", (int)fc->decode_order);
#endif
    return ft->ret;
}

void ff_vvc_report_progress(VVCFrame *frame, int n)
{
    FrameProgress *p = (FrameProgress*)frame->progress_buf->data;

    pthread_mutex_lock(&p->lock);

    av_assert0(p->progress < n || p->progress == INT_MAX);
    p->progress = n;

    pthread_cond_broadcast(&p->cond);
    pthread_mutex_unlock(&p->lock);
}

void ff_vvc_await_progress(VVCFrame *frame, int n)
{
    FrameProgress *p = (FrameProgress*)frame->progress_buf->data;

    pthread_mutex_lock(&p->lock);

    // +1 for progress default value 0
    while (p->progress < n + 1)
        pthread_cond_wait(&p->cond, &p->lock);

    pthread_mutex_unlock(&p->lock);
}
