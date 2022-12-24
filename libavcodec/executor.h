/*
 * VVC video Decoder
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

#ifndef AVCODEC_EXECUTOR_H
#define AVCODEC_EXECUTOR_H

typedef struct Executor Executor;
typedef struct Task Task;

struct Task {
    Task *next;
};

typedef struct TaskCallbacks {
    void *user_data;

    size_t local_context_size;

    // return 1 if a's priority > b's priority
    int (*priority_higher)(const Task *a, const Task *b);

    // task is ready for run
    int (*ready)(const Task *t, void *user_data);

    // run the task
    int (*run)(Task *t, void *local_context, void *user_data);
} TaskCallbacks;

Executor* ff_executor_alloc(const TaskCallbacks *callbacks, int thread_count);
void ff_executor_free(Executor **e);
void ff_executor_execute(Executor *e, Task *t);
void ff_executor_wakeup(Executor *e);

#endif
