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
#include "libavutil/avutil.h"
#include "libavutil/thread.h"

#include "executor.h"

typedef struct ThreadInfo {
    Executor *e;
    pthread_t thread;
} ThreadInfo;

struct Executor {
    TaskletCallbacks cb;
    ThreadInfo *threads;
    uint8_t *local_contexts;
    int thread_count;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    int die;
    Tasklet *tasks;
};

static void remove_task(Tasklet **prev, Tasklet *t)
{
    *prev  = t->next;
    t->next = NULL;
}

static void add_task(Tasklet **prev, Tasklet *t)
{
    t->next = *prev;
    *prev   = t;
}

static int run_one_task(Executor *e, void *lc)
{
    TaskletCallbacks *cb = &e->cb;
    Tasklet **prev;
    Tasklet *t = NULL;

    for (prev = &e->tasks; *prev; prev = &(*prev)->next) {
        if (cb->ready(*prev, cb->user_data)) {
            t = *prev;
            break;
        }
    }
    if (t) {
        //found one task
        remove_task(prev, t);
        pthread_mutex_unlock(&e->lock);
        cb->run(t, lc, cb->user_data);
        pthread_mutex_lock(&e->lock);
        return 1;
    }
    return 0;
}

static void *executor_worker_task(void *data)
{
    ThreadInfo *ti = (ThreadInfo*)data;
    Executor *e    = ti->e;
    void *lc       = e->local_contexts + (ti - e->threads) * e->cb.local_context_size;

    pthread_mutex_lock(&e->lock);
    while (1) {
        if (e->die) break;

        if (!run_one_task(e, lc)) {
            //no task in one loop
            pthread_cond_wait(&e->cond, &e->lock);
        }
    }
    pthread_mutex_unlock(&e->lock);
    return NULL;
}

Executor* ff_executor_alloc(const TaskletCallbacks *cb, int thread_count)
{
    Executor *e;
    int i, j, ret;
    if (!cb || !cb->user_data || !cb->ready || !cb->run || !cb->priority_higher)
        return NULL;
    e = av_calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->cb = *cb;

    e->local_contexts = av_calloc(thread_count, e->cb.local_context_size);
    if (!e->local_contexts)
        goto free_executor;

    e->threads = av_calloc(thread_count, sizeof(*e->threads));
    if (!e->threads)
        goto free_contexts;
    ret = pthread_mutex_init(&e->lock, NULL);
    if (ret)
        goto free_threads;

    ret = pthread_cond_init(&e->cond, NULL);
    if (ret)
        goto destroy_lock;

    for (i = 0; i < thread_count; i++) {
        ThreadInfo *ti = e->threads + i;
        ti->e = e;
        ret = pthread_create(&ti->thread, NULL, executor_worker_task, ti);
        if (ret)
            goto join_threads;
    }
    e->thread_count = thread_count;
    return e;

join_threads:
    pthread_mutex_lock(&e->lock);
    e->die = 1;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->lock);
    for (j = 0; j < i; j++)
        pthread_join(e->threads[j].thread, NULL);
    pthread_cond_destroy(&e->cond);
destroy_lock:
    pthread_mutex_destroy(&e->lock);
free_threads:
    av_free(e->threads);
free_contexts:
    av_free(e->local_contexts);
free_executor:
    free(e);
    return NULL;
}

void ff_executor_free(Executor **executor)
{
    Executor *e;
    if (!executor || !*executor)
        return;
    e = *executor;

    //singal die
    pthread_mutex_lock(&e->lock);
    e->die = 1;
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->lock);

    for (int i = 0; i < e->thread_count; i++)
        pthread_join(e->threads[i].thread, NULL);
    pthread_cond_destroy(&e->cond);
    pthread_mutex_destroy(&e->lock);

    av_free(e->threads);
    av_free(e->local_contexts);

    av_freep(executor);
}

void ff_executor_execute(Executor *e, Tasklet *t)
{
    TaskletCallbacks *cb = &e->cb;
    Tasklet **prev;

    pthread_mutex_lock(&e->lock);
    if (t) {
        for (prev = &e->tasks; *prev && cb->priority_higher(*prev, t); prev = &(*prev)->next)
            /* nothing */;
        add_task(prev, t);
    }
    pthread_cond_signal(&e->cond);
    pthread_mutex_unlock(&e->lock);
}