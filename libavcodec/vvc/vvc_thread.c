/*
 * VVC thread logic
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

#include <stdatomic.h>

#include "libavutil/executor.h"
#include "libavutil/thread.h"

#include "vvc_thread.h"
#include "vvc_ctu.h"
#include "vvc_filter.h"
#include "vvc_inter.h"
#include "vvc_intra.h"
#include "vvc_refs.h"

typedef struct ProgressListener {
    VVCProgressListener l;
    VVCTask *task;
    VVCContext *s;
} ProgressListener;

typedef enum VVCTaskType {
    VVC_TASK_TYPE_PARSE,
    VVC_TASK_TYPE_INTER,
    VVC_TASK_TYPE_RECON,
    VVC_TASK_TYPE_LMCS,
    VVC_TASK_TYPE_DEBLOCK_V,
    VVC_TASK_TYPE_DEBLOCK_H,
    VVC_TASK_TYPE_SAO,
    VVC_TASK_TYPE_ALF,
    VVC_TASK_TYPE_LAST
} VVCTaskType;

struct VVCTask {
    union {
        VVCTask *next;                //for executor debug only
        AVTask task;
    };

    VVCTaskType type;

    // ctu x, y in raster order
    int rx, ry;
    VVCFrameContext *fc;

    ProgressListener listener[2][VVC_MAX_REF_ENTRIES];

    // reconstruct task only
    SliceContext *sc;
    EntryPoint *ep;
    int ctu_idx;                    //ctu idx in the current slice

    // tasks with target scores met are ready for scheduling
    atomic_uchar score[VVC_TASK_TYPE_LAST - VVC_TASK_TYPE_INTER];
    atomic_uchar target_inter_score;
};

typedef struct VVCRowThread {
    atomic_int progress[VVC_PROGRESS_LAST];
} VVCRowThread;

struct VVCFrameThread {
    // error return for tasks
    atomic_int ret;

    atomic_uchar *avails;

    VVCRowThread *rows;
    VVCTask *tasks;

    int ctu_size;
    int ctu_width;
    int ctu_height;
    int ctu_count;

    //protected by lock
    int nb_scheduled_tasks;
    int nb_parse_tasks;
    int row_progress[VVC_PROGRESS_LAST];

    AVMutex lock;
    AVCond  cond;
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

static void vvc_task_init(VVCTask *t, VVCTaskType type, VVCFrameContext *fc, const int rx, const int ry)
{
    memset(t, 0, sizeof(*t));
    t->type = type;
    t->fc   = fc;
    t->rx   = rx;
    t->ry   = ry;
    for (int i = 0; i < FF_ARRAY_ELEMS(t->score); i++)
        atomic_store(t->score + i, 0);
    atomic_store(&t->target_inter_score, 0);
}

VVCTask* ff_vvc_parse_task_alloc(VVCFrameContext *fc,
    SliceContext *sc, EntryPoint *ep, const int ctu_idx)
{
    const VVCFrameThread *ft = fc->frame_thread;
    const int rs = sc->sh.ctb_addr_in_curr_slice[ctu_idx];
    VVCTask *t = av_malloc(sizeof(*t));

    if (!t)
        return NULL;

    vvc_task_init(t, VVC_TASK_TYPE_PARSE, fc, rs % ft->ctu_width, rs / ft->ctu_width);
    t->sc = sc;
    t->ep = ep;
    t->ctu_idx = ctu_idx;

    return t;
}

void ff_vvc_parse_task_free(VVCTask *t)
{
    av_free(t);
}

static uint8_t task_add_score(VVCTask *t, const VVCTaskType type, const uint8_t count)
{
    return atomic_fetch_add(&t->score[type - VVC_TASK_TYPE_INTER], count);
}

static int task_has_target_score(VVCTask *t, const VVCTaskType type)
{
    // l:left, r:right, t: top, b: bottom
    static const uint8_t target_score[] =
    {
        0,          //VVC_TASK_TYPE_INTER,     not used
        2,          //VVC_TASK_TYPE_RECON,     need l + rt recon
        3,          //VVC_TASK_TYPE_LMCS,      need r + b + rb recon
        2,          //VVC_TASK_TYPE_DEBLOCK_V, need r lmcs + l deblock v
        2,          //VVC_TASK_TYPE_DEBLOCK_H, need r deblock v + t deblock h
        5,          //VVC_TASK_TYPE_SAO,       need l + r + lb + b + rb deblock h
        8,          //VVC_TASK_TYPE_ALF,       need sao around the ctu
    };
    const uint8_t score = task_add_score(t, type, 1);
    uint8_t target = 0;

    if (type == VVC_TASK_TYPE_INTER) {
        target = atomic_load(&t->target_inter_score);
    } else {
        target = target_score[type - VVC_TASK_TYPE_INTER];
    }

    av_assert0(score <= target);

    return score == target;
}

static void frame_thread_add_score(VVCContext *s, VVCFrameThread *ft,
    const int x, const int y, const VVCTaskType type)
{
    VVCTask *t = ft->tasks + ft->ctu_width * y + x;

    if (x < 0 || x >= ft->ctu_width || y < 0 || y >= ft->ctu_height)
        return;

    if (task_has_target_score(t, type)) {
        av_assert0(s);
        av_assert0(type == t->type + 1);
        t->type++;
        ff_vvc_frame_add_task(s, t);
    }
}

static void progress_done(VVCProgressListener *_l)
{
    ProgressListener *l = (ProgressListener *)_l;
    const VVCTask *t = l->task;

    frame_thread_add_score(l->s, t->fc->frame_thread, t->rx, t->ry, t->type + 1);
}

static void listener_init(ProgressListener *l, VVCTask *t, VVCContext *s, const VVCProgress vp, const int y)
{
    l->task = t;
    l->s    = s;
    l->l.vp = vp;
    l->l.y  = y;
    l->l.progress_done = progress_done;
}

static void add_progress_listener(VVCFrame *ref, ProgressListener *l,
    VVCTask *t, VVCContext *s, const VVCProgress vp, const int y)
{
    listener_init(l, t, s, vp, y);
    atomic_fetch_add(&t->target_inter_score, 1);
    ff_vvc_add_progress_listener(ref, (VVCProgressListener*)l);
}

static void parse_task_done(VVCContext *s, VVCFrameContext *fc, const int rx, const int ry)
{
    VVCFrameThread *ft  = fc->frame_thread;
    const int rs        = ry * ft->ctu_width + rx;
    const int slice_idx = fc->tab.slice_idx[rs];
    VVCTask *t          = ft->tasks + rs;

    if (slice_idx != -1) {
        const SliceContext *sc = fc->slices[slice_idx];
        const VVCSH *sh        = &sc->sh;
        CTU *ctu               = fc->tab.ctus + rs;
        if (!IS_I(sh->r)) {
            for (int lx = 0; lx < 2; lx++) {
                for (int i = 0; i < sh->r->num_ref_idx_active[lx]; i++) {
                    const int y = ctu->max_y[lx][i];
                    VVCFrame *ref = sc->rpl[lx].ref[i];
                    if (ref && y >= 0)
                        add_progress_listener(ref, &t->listener[lx][i], t, s, VVC_PROGRESS_PIXEL, y + LUMA_EXTRA_AFTER);
                }
            }
        }
    }

}

static void decode_state_done(const VVCTask *task, VVCContext *s)
{
    VVCFrameContext *fc = task->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int rx        = task->rx;
    const int ry        = task->ry;
    const int type      = task->type;

#define ADD(dx, dy, type) frame_thread_add_score(s, ft, rx + (dx), ry + (dy), type)

    //this is a reserve map of ready_score, ordered by zigzag
    if (type == VVC_TASK_TYPE_PARSE) {
        parse_task_done(s, fc, task->rx, task->ry);
    } else if (type == VVC_TASK_TYPE_RECON) {
        ADD(-1,  1, VVC_TASK_TYPE_RECON);
        ADD( 1,  0, VVC_TASK_TYPE_RECON);
        ADD(-1, -1, VVC_TASK_TYPE_LMCS);
        ADD( 0, -1, VVC_TASK_TYPE_LMCS);
        ADD(-1,  0, VVC_TASK_TYPE_LMCS);
    } else if (type == VVC_TASK_TYPE_LMCS) {
        ADD(-1,  0,  VVC_TASK_TYPE_DEBLOCK_V);
    } else if (type == VVC_TASK_TYPE_DEBLOCK_V) {
        ADD( 1,  0,  VVC_TASK_TYPE_DEBLOCK_V);
        ADD(-1,  0,  VVC_TASK_TYPE_DEBLOCK_H);
    } else if (type == VVC_TASK_TYPE_DEBLOCK_H) {
        ADD( 0,  1,  VVC_TASK_TYPE_DEBLOCK_H);
        ADD(-1, -1,  VVC_TASK_TYPE_SAO);
        ADD( 0, -1,  VVC_TASK_TYPE_SAO);
        ADD(-1,  0,  VVC_TASK_TYPE_SAO);
        ADD( 1, -1,  VVC_TASK_TYPE_SAO);
        ADD( 1,  0,  VVC_TASK_TYPE_SAO);
    } else if (type == VVC_TASK_TYPE_SAO) {
        ADD(-1, -1,  VVC_TASK_TYPE_ALF);
        ADD( 0, -1,  VVC_TASK_TYPE_ALF);
        ADD(-1,  0,  VVC_TASK_TYPE_ALF);
        ADD( 1, -1,  VVC_TASK_TYPE_ALF);
        ADD(-1,  1,  VVC_TASK_TYPE_ALF);
        ADD( 1,  0,  VVC_TASK_TYPE_ALF);
        ADD( 0,  1,  VVC_TASK_TYPE_ALF);
        ADD( 1,  1,  VVC_TASK_TYPE_ALF);
    }
    if (type < VVC_TASK_TYPE_ALF)
        ADD(0, 0, type + 1);
}

static int check_colocation_ctu(const VVCFrameContext *fc, const VVCTask *t)
{
    if (fc->ps.ph.r->ph_temporal_mvp_enabled_flag || fc->ps.sps->r->sps_sbtmvp_enabled_flag) {
        //-1 to avoid we are waiting for next CTU line.
        const int y = (t->ry << fc->ps.sps->ctb_log2_size_y) - 1;
        VVCFrame *col = fc->ref->collocated_ref;
        if (col && !ff_vvc_check_progress(col, VVC_PROGRESS_MV, y))
            return 0;
    }
    return 1;
}

static int is_parse_ready(const VVCFrameContext *fc, const VVCTask *t)
{
    av_assert0(t->type == VVC_TASK_TYPE_PARSE);
    if (!check_colocation_ctu(fc, t))
        return 0;
    if (fc->ps.sps->r->sps_entropy_coding_sync_enabled_flag && t->ry != fc->ps.pps->ctb_to_row_bd[t->ry])
        return get_avail(fc->frame_thread, t->rx, t->ry - 1, VVC_TASK_TYPE_PARSE);
    return 1;
}

static int task_ready(const AVTask *_t, void *user_data)
{
    const VVCTask *t            = (const VVCTask*)_t;
    VVCFrameThread *ft          = t->fc->frame_thread;

    if (atomic_load(&ft->ret))
        return 1;

    // scheduled by task score board
    if (t->type > VVC_TASK_TYPE_PARSE)
        return 1;

    return is_parse_ready(t->fc, t);
}

#define CHECK(a, b)                         \
    do {                                    \
        if ((a) != (b))                     \
            return (a) < (b);               \
    } while (0)

static int task_priority_higher(const AVTask *_a, const AVTask *_b)
{
    const VVCTask *a = (const VVCTask*)_a;
    const VVCTask *b = (const VVCTask*)_b;

    CHECK(a->fc->decode_order, b->fc->decode_order);             //decode order

    if (a->type == VVC_TASK_TYPE_PARSE || b->type == VVC_TASK_TYPE_PARSE) {
        CHECK(a->type, b->type);
        CHECK(a->ry, b->ry);
        return a->rx < b->rx;
    }

    CHECK(a->rx + a->ry + a->type, b->rx + b->ry + b->type);    //zigzag with type
    CHECK(a->rx + a->ry, b->rx + b->ry);                        //zigzag
    return a->ry < b->ry;
}

static int run_parse(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc     = lc->fc;
    const VVCSPS *sps       = fc->ps.sps;
    const VVCPPS *pps       = fc->ps.pps;
    SliceContext *sc        = t->sc;
    const VVCSH *sh         = &sc->sh;
    EntryPoint *ep          = t->ep;
    VVCFrameThread *ft      = fc->frame_thread;
    int ret, rs, prev_ry;

    lc->sc = sc;
    lc->ep = ep;

    //reconstruct one line a time
    rs = sh->ctb_addr_in_curr_slice[t->ctu_idx];
    do {

        prev_ry = t->ry;

        ret = ff_vvc_coding_tree_unit(lc, t->ctu_idx, rs, t->rx, t->ry);
        if (ret < 0)
            return ret;

        set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_PARSE);
        decode_state_done(t, s);

        if (fc->ps.sps->r->sps_entropy_coding_sync_enabled_flag && t->rx == pps->ctb_to_col_bd[t->rx]) {
            EntryPoint *next = ep + 1;
            if (next < sc->eps + sc->nb_eps) {
                memcpy(next->cabac_state, ep->cabac_state, sizeof(next->cabac_state));
                av_assert0(!next->parse_task->type);
                ff_vvc_ep_init_stat_coeff(lc->ep, sps->bit_depth, sps->r->sps_persistent_rice_adaptation_enabled_flag);
                ff_vvc_frame_add_task(s, next->parse_task);
            }
        }

        t->ctu_idx++;
        if (t->ctu_idx >= ep->ctu_end)
            break;

        rs = sh->ctb_addr_in_curr_slice[t->ctu_idx];
        t->rx = rs % ft->ctu_width;
        t->ry = rs / ft->ctu_width;
    } while (t->ry == prev_ry && is_parse_ready(fc, t));

    if (t->ctu_idx < ep->ctu_end)
        ff_vvc_frame_add_task(s, t);

    return 0;
}

static void report_frame_progress(VVCFrameContext *fc,
   const int ry, const VVCTaskType type)
{
    VVCFrameThread *ft  = fc->frame_thread;
    const int ctu_size  = ft->ctu_size;
    const int idx       = type == VVC_TASK_TYPE_INTER ? VVC_PROGRESS_MV : VVC_PROGRESS_PIXEL;
    int old;

    if (atomic_fetch_add(&ft->rows[ry].progress[idx], 1) == ft->ctu_width - 1) {
        int y;
        ff_mutex_lock(&ft->lock);
        y = old = ft->row_progress[idx];
        while (y < ft->ctu_height && atomic_load(&ft->rows[y].progress[idx]) == ft->ctu_width)
            y++;
        if (old != y) {
            const int progress = y == ft->ctu_height ? INT_MAX : y * ctu_size;
            ft->row_progress[idx] = y;
            ff_vvc_report_progress(fc->ref, idx, progress);
        }
        ff_mutex_unlock(&ft->lock);
    }
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
    }
    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_INTER);
    decode_state_done(t, s);
    report_frame_progress(fc, t->ry, VVC_TASK_TYPE_INTER);

    return 0;
}

static int run_recon(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int rs = t->ry * ft->ctu_width + t->rx;
    const int slice_idx = fc->tab.slice_idx[rs];

    if (slice_idx != -1) {
        lc->sc = fc->slices[slice_idx];
        ff_vvc_reconstruct(lc, rs, t->rx, t->ry);
    }

    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_RECON);
    decode_state_done(t, s);

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
    decode_state_done(t, s);

    return 0;
}

static int run_deblock_v(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int rs        = t->ry * ft->ctu_width + t->rx;
    const int ctb_size  = ft->ctu_size;
    const int x0        = t->rx * ctb_size;
    const int y0        = t->ry * ctb_size;
    const int slice_idx = fc->tab.slice_idx[rs];

    if (slice_idx != -1) {
        lc->sc = fc->slices[slice_idx];
        if (!lc->sc->sh.r->sh_deblocking_filter_disabled_flag) {
            ff_vvc_decode_neighbour(lc, x0, y0, t->rx, t->ry, rs);
            ff_vvc_deblock_vertical(lc, x0, y0);
        }
    }

    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_DEBLOCK_V);
    decode_state_done(t, s);

    return 0;
}

static int run_deblock_h(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int ctb_size  = ft->ctu_size;
    const int rs        = t->ry * ft->ctu_width + t->rx;
    const int x0        = t->rx * ctb_size;
    const int y0        = t->ry * ctb_size;
    const int slice_idx = fc->tab.slice_idx[rs];

    if (slice_idx != -1) {
        lc->sc = fc->slices[slice_idx];
        if (!lc->sc->sh.r->sh_deblocking_filter_disabled_flag) {
            ff_vvc_decode_neighbour(lc, x0, y0, t->rx, t->ry, rs);
            ff_vvc_deblock_horizontal(lc, x0, y0);
        }
        if (fc->ps.sps->r->sps_sao_enabled_flag)
            ff_vvc_sao_copy_ctb_to_hv(lc, t->rx, t->ry, t->ry == ft->ctu_height - 1);
    }

    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_DEBLOCK_H);
    decode_state_done(t, s);

    return 0;
}

static int run_sao(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int rs        = t->ry * fc->ps.pps->ctb_width + t->rx;
    const int ctb_size  = ft->ctu_size;
    const int x0        = t->rx * ctb_size;
    const int y0        = t->ry * ctb_size;

    if (fc->ps.sps->r->sps_sao_enabled_flag) {
        ff_vvc_decode_neighbour(lc, x0, y0, t->rx, t->ry, rs);
        ff_vvc_sao_filter(lc, x0, y0);
    }

    if (fc->ps.sps->r->sps_alf_enabled_flag)
        ff_vvc_alf_copy_ctu_to_hv(lc, x0, y0);

    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_SAO);
    decode_state_done(t, s);

    return 0;
}

static int run_alf(VVCContext *s, VVCLocalContext *lc, VVCTask *t)
{
    VVCFrameContext *fc = lc->fc;
    VVCFrameThread *ft  = fc->frame_thread;
    const int ctu_size  = ft->ctu_size;
    const int x0        = t->rx * ctu_size;
    const int y0        = t->ry * ctu_size;

    if (fc->ps.sps->r->sps_alf_enabled_flag) {
        const int slice_idx = CTB(fc->tab.slice_idx, t->rx, t->ry);
        if (slice_idx != -1) {
            const int rs = t->ry * fc->ps.pps->ctb_width + t->rx;
            lc->sc = fc->slices[slice_idx];
            ff_vvc_decode_neighbour(lc, x0, y0, t->rx, t->ry, rs);
            ff_vvc_alf_filter(lc, x0, y0);
        }
    }
    set_avail(ft, t->rx, t->ry, VVC_TASK_TYPE_ALF);
    report_frame_progress(fc, t->ry, VVC_TASK_TYPE_ALF);

    return 0;
}

static void finished_one_task(VVCFrameThread *ft, const VVCTaskType type)
{
    int parse_done = 0;
    ff_mutex_lock(&ft->lock);

    av_assert0(ft->nb_scheduled_tasks);
    ft->nb_scheduled_tasks--;

    if (type == VVC_TASK_TYPE_PARSE) {
        av_assert0(ft->nb_parse_tasks);
        ft->nb_parse_tasks--;
        if (!ft->nb_parse_tasks)
            parse_done = 1;
    }
    if (parse_done || !ft->nb_scheduled_tasks)
        ff_cond_broadcast(&ft->cond);

    ff_mutex_unlock(&ft->lock);
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

static int task_run(AVTask *_t, void *local_context, void *user_data)
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
#ifdef COMPAT_ATOMICS_WIN32_STDATOMIC_H
            intptr_t zero = 0;
#else
            int zero = 0;
#endif
            atomic_compare_exchange_strong(&ft->ret, &zero, ret);
            av_log(s->avctx, AV_LOG_ERROR,
                "frame %5d, %s(%3d, %3d) failed with %d\r\n",
                (int)t->fc->decode_order, task_name[type], t->rx, t->ry, ret);
        }
    }

    // t->type may changed by run(), we use a local copy of t->type
    finished_one_task(ft, type);

    return ret;
}

AVExecutor* ff_vvc_executor_alloc(VVCContext *s, int thread_count)
{
    AVTaskCallbacks callbacks = {
        s,
        sizeof(VVCLocalContext),
        task_priority_higher,
        task_ready,
        task_run,
    };
    return av_executor_alloc(&callbacks, s->nb_fcs);
}

void ff_vvc_executor_free(AVExecutor **e)
{
    av_executor_free(e);
}

void ff_vvc_frame_thread_free(VVCFrameContext *fc)
{
    VVCFrameThread *ft = fc->frame_thread;

    if (!ft)
        return;

    ff_mutex_destroy(&ft->lock);
    ff_cond_destroy(&ft->cond);
    av_freep(&ft->avails);
    av_freep(&ft->rows);
    av_freep(&ft->tasks);
    av_freep(&ft);
}

static void frame_thread_init_score(VVCFrameContext *fc)
{
    const VVCFrameThread *ft = fc->frame_thread;
    VVCTask task;

    vvc_task_init(&task, VVC_TASK_TYPE_RECON, fc, 0, 0);

    for (int i = VVC_TASK_TYPE_RECON; i < VVC_TASK_TYPE_LAST; i++) {
        task.type = i;

        for (task.rx = -1; task.rx <= ft->ctu_width; task.rx++) {
            task.ry = -1;                           //top
            decode_state_done(&task, NULL);
            task.ry = ft->ctu_height;               //bottom
            decode_state_done(&task, NULL);
        }

        for (task.ry = 0; task.ry < ft->ctu_height; task.ry++) {
            task.rx = -1;                           //left
            decode_state_done(&task, NULL);
            task.rx = ft->ctu_width;                //right
            decode_state_done(&task, NULL);
        }
    }
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

        ft->avails = av_calloc(ft->ctu_count, sizeof(*ft->avails));
        if (!ft->avails)
            goto fail;

        ft->tasks = av_malloc(ft->ctu_count * sizeof(*ft->tasks));
        if (!ft->tasks)
            goto fail;

        if ((ret = ff_cond_init(&ft->cond, NULL)))
            goto fail;

        if ((ret = ff_mutex_init(&ft->lock, NULL))) {
            ff_cond_destroy(&ft->cond);
            goto fail;
        }
    }

    ft->ret = 0;
    for (int y = 0; y < ft->ctu_height; y++) {
        VVCRowThread *row = ft->rows + y;
        memset(row->progress, 0, sizeof(row->progress));
    }

    for (int rs = 0; rs < ft->ctu_count; rs++) {
        VVCTask *t = ft->tasks + rs;

        vvc_task_init(t, VVC_TASK_TYPE_PARSE, fc, rs % ft->ctu_width, rs / ft->ctu_width);
        atomic_store(ft->avails + rs, 0);
    }

    memset(&ft->row_progress[0], 0, sizeof(ft->row_progress));
    fc->frame_thread = ft;
    frame_thread_init_score(fc);

    return 0;

fail:
    if (ft) {
        av_freep(&ft->avails);
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

    ff_mutex_lock(&ft->lock);

    ft->nb_scheduled_tasks++;
    if (t->type == VVC_TASK_TYPE_PARSE)
        ft->nb_parse_tasks++;

    ff_mutex_unlock(&ft->lock);

    av_executor_execute(s->executor, &t->task);
}

int ff_vvc_frame_wait(VVCContext *s, VVCFrameContext *fc)
{
    VVCFrameThread *ft = fc->frame_thread;
    int check_missed_slices = 1;

    ff_mutex_lock(&ft->lock);

    while (ft->nb_scheduled_tasks) {
        if (check_missed_slices && !ft->nb_parse_tasks) {
            // abort for missed slices
            for (int rs = 0; rs < ft->ctu_count; rs++){
                atomic_uchar mask = 1 << VVC_TASK_TYPE_PARSE;
                if (!(atomic_load(ft->avails + rs) & mask)) {
                    atomic_store(&ft->ret, AVERROR_INVALIDDATA);
                    break;
                }
            }
            check_missed_slices = 0;
        }
        ff_cond_wait(&ft->cond, &ft->lock);
    }

    ff_mutex_unlock(&ft->lock);
    ff_vvc_report_frame_finished(fc->ref);

    // maybe all threads are waiting, let us wake up one
    av_executor_execute(s->executor, NULL);

#ifdef VVC_THREAD_DEBUG
    av_log(s->avctx, AV_LOG_DEBUG, "frame %5d done\r\n", (int)fc->decode_order);
#endif
    return ft->ret;
}
