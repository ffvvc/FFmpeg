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
#include "libavutil/executor.h"

typedef struct ThreadInfo {
    int idx;
    AVExecutor *e;
    pthread_t thread;
} ThreadInfo;

struct AVExecutor {
    AVTaskCallbacks cb;
    ThreadInfo *threads;
    uint8_t *local_contexts;
    int thread_count;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    int die;
    AVTask *tasks;
};

static void remove_task(AVTask **prev, AVTask *t)
{
    *prev  = t->next;
    t->next = NULL;
}

static void add_task(AVTask **prev, AVTask *t)
{
    t->next = *prev;
    *prev   = t;
}

static void *executor_worker_task(void *data)
{
    ThreadInfo *ti = (ThreadInfo*)data;
    AVExecutor *e = ti->e;
    void *lc       = e->local_contexts + ti->idx * e->cb.local_context_size;
    AVTask **prev;
    AVTaskCallbacks *cb = &e->cb;

    pthread_mutex_lock(&e->lock);
    while (1) {
        AVTask* t = NULL;
        if (e->die) break;

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
        } else {
            //no task in one loop
            pthread_cond_wait(&e->cond, &e->lock);
        }
    }
    pthread_mutex_unlock(&e->lock);
    return NULL;
}

AVExecutor* avpriv_executor_alloc(const AVTaskCallbacks *cb, int thread_count)
{
    AVExecutor *e;
    int i, j, ret;
    if (!cb || !cb->user_data || !cb->ready || !cb->run || !cb->priority_higher)
        return NULL;
    e = av_calloc(1, sizeof(*e));
    if (!e)
        return NULL;
    e->cb = *cb;

    e->local_contexts = av_malloc(thread_count * e->cb.local_context_size);
    if (!e->local_contexts)
        goto free_executor;

    e->threads = av_calloc(thread_count, sizeof(*e->threads));
    if (!e->threads)
        goto free_contexts;
    for (i = 0; i < thread_count; i++) {
        ThreadInfo *ti = e->threads + i;
        ti->e = e;
        ti->idx = i;
    }

    ret = pthread_mutex_init(&e->lock, NULL);
    if (ret)
        goto free_threads;

    ret = pthread_cond_init(&e->cond, NULL);
    if (ret)
        goto destroy_lock;

    for (i = 0; i < thread_count; i++) {
        ThreadInfo *ti = e->threads + i;
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

void avpriv_executor_free(AVExecutor **executor)
{
    AVExecutor *e;
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

void avpriv_executor_execute(AVExecutor *e, AVTask *t)
{
    AVTaskCallbacks *cb = &e->cb;
    AVTask **prev;

    pthread_mutex_lock(&e->lock);
    for (prev = &e->tasks; *prev && cb->priority_higher(*prev, t); prev = &(*prev)->next)
        /* nothing */;
    add_task(prev, t);
    pthread_cond_signal(&e->cond);
    pthread_mutex_unlock(&e->lock);
}

void avpriv_executor_wakeup(AVExecutor *e)
{
    pthread_mutex_lock(&e->lock);
    pthread_cond_broadcast(&e->cond);
    pthread_mutex_unlock(&e->lock);
}
