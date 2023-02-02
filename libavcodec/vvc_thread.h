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

#ifndef AVCODEC_VVC_THREAD_H
#define AVCODEC_VVC_THREAD_H

#include "vvcdec.h"

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
        Task task;
    };

    VVCTaskType type;
    uint64_t decode_order;

    // ctu x, y in raster order
    int rx, ry;
    VVCFrameContext *fc;

    // reconstruct task only
    SliceContext *sc;
    EntryPoint *ep;
    int ctb_addr_in_slice;
};

void ff_vvc_task_init(VVCTask *task, VVCTaskType type, VVCFrameContext *fc);
void ff_vvc_parse_task_init(VVCTask *task, VVCTaskType type, VVCFrameContext *fc,
    SliceContext *sc,  EntryPoint *ep, int ctu_addr);
VVCTask* ff_vvc_task_alloc(void);

int ff_vvc_task_ready(const Task* t, void* user_data);
int ff_vvc_task_priority_higher(const Task *a, const Task *b);
int ff_vvc_task_run(Task *t, void *local_context, void *user_data);

int ff_vvc_frame_thread_init(VVCFrameContext *fc);
void ff_vvc_frame_thread_free(VVCFrameContext *fc);

int ff_vvc_frame_wait(VVCContext *s, VVCFrameContext *fc);

void ff_vvc_frame_add_task(VVCContext *s, VVCTask *t);

void ff_vvc_report_progress(VVCFrame *frame, int n);
void ff_vvc_await_progress(VVCFrame *frame, int n);

#endif // AVCODEC_VVC_THREAD_H