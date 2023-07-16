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
#include "internal.h"
#include "thread.h"

#include "executor.h"

#if !HAVE_THREADS
#define pthread_create(t, a, s, ar)     0
#define pthread_join(t, r)              do {} while(0)

#define pthread_cond_init(c, a)         0
#define pthread_cond_broadcast(c)       do {} while(0)
#define pthread_cond_signal(c)          do {} while(0)
#define pthread_cond_wait(c, m)         do {} while(0)
#define pthread_cond_destroy(c)         do {} while(0)

#define pthread_mutex_init(m, a)        0
#define pthread_mutex_lock(l)           do {} while(0)
#define pthread_mutex_unlock(l)         do {} while(0)
#define pthread_mutex_destroy(l)        do {} while(0)
#endif

typedef struct ThreadInfo {
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

static int run_one_task(AVExecutor *e, void *lc)
{
    AVTaskCallbacks *cb = &e->cb;
    AVTask *t           = NULL;
    AVTask **prev;

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

#if HAVE_THREADS
static void *executor_worker_task(void *data)
{
    ThreadInfo *ti = (ThreadInfo*)data;
    AVExecutor *e  = ti->e;
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
#endif

static void executor_free(AVExecutor *e, const int has_lock, const int has_cond)
{
    if (e->thread_count) {
        //signal die
        pthread_mutex_lock(&e->lock);
        e->die = 1;
        pthread_cond_broadcast(&e->cond);
        pthread_mutex_unlock(&e->lock);

        for (int i = 0; i < e->thread_count; i++)
            pthread_join(e->threads[i].thread, NULL);
    }
    if (has_cond)
        pthread_cond_destroy(&e->cond);
    if (has_lock)
        pthread_mutex_destroy(&e->lock);

    av_free(e->threads);
    av_free(e->local_contexts);

    av_free(e);
}

AVExecutor* avpriv_executor_alloc(const AVTaskCallbacks *cb, int thread_count)
{
    AVExecutor *e;
    int has_lock = 0, has_cond = 0;
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
        goto free_executor;

    has_lock = !pthread_mutex_init(&e->lock, NULL);
    has_cond = !pthread_cond_init(&e->cond, NULL);

    if (!has_lock || !has_cond)
        goto free_executor;

    for (/* nothing */; e->thread_count < thread_count; e->thread_count++) {
        ThreadInfo *ti = e->threads + e->thread_count;
        ti->e = e;
        if (pthread_create(&ti->thread, NULL, executor_worker_task, ti))
            goto free_executor;
    }
    return e;

free_executor:
    executor_free(e, has_lock, has_cond);
    return NULL;
}

void avpriv_executor_free(AVExecutor **executor)
{
    if (!executor || !*executor)
        return;
    executor_free(*executor, 1, 1);
    *executor = NULL;
}

void avpriv_executor_execute(AVExecutor *e, AVTask *t)
{
    AVTaskCallbacks *cb = &e->cb;
    AVTask **prev;

    pthread_mutex_lock(&e->lock);
    if (t) {
        for (prev = &e->tasks; *prev && cb->priority_higher(*prev, t); prev = &(*prev)->next)
            /* nothing */;
        add_task(prev, t);
    }
    pthread_cond_signal(&e->cond);
    pthread_mutex_unlock(&e->lock);

#if !HAVE_THREADS
    // We are running in a single-threaded environment, so we must handle all tasks ourselves
    while (run_one_task(e, e->local_contexts))
        /* nothing */;
#endif
}