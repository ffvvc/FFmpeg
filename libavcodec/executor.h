/*
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
typedef struct Tasklet Tasklet;

struct Tasklet {
    Tasklet *next;
};

typedef struct TaskletCallbacks {
    void *user_data;

    int local_context_size;

    // return 1 if a's priority > b's priority
    int (*priority_higher)(const Tasklet *a, const Tasklet *b);

    // task is ready for run
    int (*ready)(const Tasklet *t, void *user_data);

    // run the task
    int (*run)(Tasklet *t, void *local_context, void *user_data);
} TaskletCallbacks;

/**
 * Alloc executor
 * @param callbacks callback strucutre for executor
 * @param thread_count worker thread number
 * @return return the executor
 */
Executor* ff_executor_alloc(const TaskletCallbacks *callbacks, int thread_count);

/**
 * Free executor
 * @param e  pointer to executor
 */
void ff_executor_free(Executor **e);

/**
 * Add task to executor
 * @param e pointer to executor
 * @param t pointer to task. If NULL, it will wakeup one work thread
 */
void ff_executor_execute(Executor *e, Tasklet *t);

#endif //AVCODEC_EXECUTOR_H
