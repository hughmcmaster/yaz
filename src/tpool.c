/* This file is part of the YAZ toolkit.
 * Copyright (C) 1995-2009 Index Data
 * See the file LICENSE for details.
 */
/**
 * \file 
 * \brief thread pool workers
 */

#include <assert.h>
#include <yaz/nmem.h>
#include <yaz/tpool.h>
#include <pthread.h>

struct work_item {
    void *data;
    struct work_item *next;
};

struct yaz_tpool_s {
    NMEM nmem;
    pthread_t *thread_id;
    pthread_mutex_t mutex;
    pthread_cond_t input_data;
    int stop_flag;
    size_t no_threads;
    struct work_item *input_queue;
    struct work_item *output_queue;
    struct work_item *free_queue;
    void (*work_handler)(void *work_data);
    void (*work_destroy)(void *work_data);
};

static struct work_item *queue_remove_last(struct work_item **q)
{
    struct work_item **work_p = q, *work_this = 0;

    while (*work_p && (*work_p)->next)
        work_p = &(*work_p)->next;
    if (*work_p)
    {
        work_this = *work_p;
        *work_p = 0;
    }
    return work_this;
}

static void queue_trav(struct work_item *q, void (*f)(void *data))
{
    for (; q; q = q->next)
        f(q->data);
}

void yaz_tpool_add(yaz_tpool_t p, void *data)
{
    struct work_item *work_p;

    pthread_mutex_lock(&p->mutex);

    if (p->free_queue)
    {
        work_p = p->free_queue;
        p->free_queue = p->free_queue->next;
    }
    else
        work_p = nmem_malloc(p->nmem, sizeof(*work_p));

    work_p->data = data;
    work_p->next = p->input_queue;
    p->input_queue = work_p;

    pthread_cond_signal(&p->input_data);
    pthread_mutex_unlock(&p->mutex);
}

void yaz_tpool_destroy(yaz_tpool_t p)
{
    if (p)
    {
        size_t i;

        pthread_mutex_lock(&p->mutex);
        p->stop_flag = 1;
        pthread_cond_broadcast(&p->input_data);
        pthread_mutex_unlock(&p->mutex);
        
        for (i = 0; i < p->no_threads; i++)
            pthread_join(p->thread_id[i], 0);
        
        if (p->work_destroy)
        {
            queue_trav(p->input_queue, p->work_destroy);
            queue_trav(p->output_queue, p->work_destroy);
        }
        
        pthread_cond_destroy(&p->input_data);
        pthread_mutex_destroy(&p->mutex);
        nmem_destroy(p->nmem);
    }
}

static void *tpool_thread_handler(void *vp)
{
    yaz_tpool_t p = (yaz_tpool_t) vp;
    while (1)
    {
        struct work_item *work_this = 0;
        /* wait for some work */
        pthread_mutex_lock(&p->mutex);
        while (!p->stop_flag && !p->input_queue)
            pthread_cond_wait(&p->input_data, &p->mutex);
        /* see if we were waken up because we're shutting down */
        if (p->stop_flag)
            break;
        /* got something. Take the last one out of input_queue */
        assert(p->input_queue);
        work_this = queue_remove_last(&p->input_queue);
        assert(work_this);

        pthread_mutex_unlock(&p->mutex);

        /* work on this item */
        p->work_handler(work_this->data);
    }        
    pthread_mutex_unlock(&p->mutex);
    return 0;
}

yaz_tpool_t yaz_tpool_create(void (*work_handler)(void *work_data),
                             void (*work_destroy)(void *work_data),
                             size_t no_threads)
{
    NMEM nmem = nmem_create();
    yaz_tpool_t p = nmem_malloc(nmem, sizeof(*p));
    size_t i;
    p->nmem = nmem;
    p->stop_flag = 0;
    p->no_threads = no_threads;

    p->input_queue = 0;
    p->output_queue = 0;
    p->free_queue = 0;

    p->work_handler = work_handler;
    p->work_destroy = work_destroy;

    pthread_mutex_init(&p->mutex, 0);
    pthread_cond_init(&p->input_data, 0);

    p->thread_id = nmem_malloc(p->nmem, sizeof(*p->thread_id) * p->no_threads);
    for (i = 0; i < p->no_threads; i++)
        pthread_create (p->thread_id + i, 0, tpool_thread_handler, p);
    return p;
}

/*
 * Local variables:
 * c-basic-offset: 4
 * c-file-style: "Stroustrup"
 * indent-tabs-mode: nil
 * End:
 * vim: shiftwidth=4 tabstop=8 expandtab
 */

