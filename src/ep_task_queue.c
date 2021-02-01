/* ep_task_queue.c
 *
 * Copyright (c) 2013-2021 Inango Systems LTD.
 *
 * Author: Inango Systems LTD. <support@inango-systems.com>
 * Creation Date: May 2013
 *
 * The author may be reached at support@inango-systems.com
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Subject to the terms and conditions of this license, each copyright holder
 * and contributor hereby grants to those receiving rights under this license
 * a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable
 * (except for failure to satisfy the conditions of this license) patent license
 * to make, have made, use, offer to sell, sell, import, and otherwise transfer
 * this software, where such license applies only to those patent claims, already
 * acquired or hereafter acquired, licensable by such copyright holder or contributor
 * that are necessarily infringed by:
 *
 * (a) their Contribution(s) (the licensed copyrights of copyright holders and
 * non-copyrightable additions of contributors, in source or binary form) alone;
 * or
 *
 * (b) combination of their Contribution(s) with the work of authorship to which
 * such Contribution(s) was added by such copyright holder or contributor, if,
 * at the time the Contribution is added, such addition causes such combination
 * to be necessarily infringed. The patent license shall not apply to any other
 * combinations which include the Contribution.
 *
 * Except as expressly stated above, no rights or licenses from any copyright
 * holder or contributor is granted under this license, whether expressly, by
 * implication, estoppel or otherwise.
 *
 * DISCLAIMER
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * NOTE
 *
 * This is part of a management middleware software package called MMX that was developed by Inango Systems Ltd.
 *
 * This version of MMX provides web and command-line management interfaces.
 *
 * Please contact us at Inango at support@inango-systems.com if you would like to hear more about
 * - other management packages, such as SNMP, TR-069 or Netconf
 * - how we can extend the data model to support all parts of your system
 * - professional sub-contract and customization services
 */

/*
 * Thread pool queue
 */

#include "ep_threadpool.h"
#include "ep_common.h"

ep_stat_t tp_queue_init(tp_queue_t *q, BOOL blocking)
{
    memset(q, 0, sizeof(tp_queue_t));

    if (pthread_cond_init(&(q->cv_got_task), NULL))
    {
        ERROR("Could not initialize condition variable: %s", strerror(errno));
        return EPS_SYSTEM_ERROR;
    }

    if (pthread_mutex_init(&(q->mutex), NULL))
    {
        ERROR("Could not initialize mutex: %s", strerror(errno));
        return EPS_SYSTEM_ERROR;
    }

    q->is_blocking = blocking;

    return EPS_OK;
}

ep_stat_t tp_queue_destroy(tp_queue_t *q)
{
    pthread_mutex_destroy(&(q->mutex));
    pthread_cond_destroy(&(q->cv_got_task));
    return EPS_OK;
}

ep_stat_t tp_queue_enqueue(tp_queue_t *q, tp_task_t *elem)
{
    RETURN_ERROR_IF_NULL(q);
    RETURN_ERROR_IF_NULL(elem);

    LOCK_OR_RETURN(q->mutex);

    if (q->length >= TP_MAX_TASK_QUEUE_SIZE)
    {
        INFO("Max queue size (%d) is reached. Could not enqueue.", TP_MAX_TASK_QUEUE_SIZE);
        UNLOCK_OR_RETURN(q->mutex);
        return EPS_FULL;
    }

    memcpy(&(q->elems[q->tail]), elem, sizeof(tp_task_t));
    q->tail = (q->tail + 1) % TP_MAX_TASK_QUEUE_SIZE;
    ++ q->length;

    /* If this is the first task in the queue, then signal worker threads that
     *  there is job for them
     */
    if (q->is_blocking && q->length == 1)
    {
        pthread_cond_broadcast(&(q->cv_got_task));
    }

    /*D("< queue size: %u", q->length);*/

    UNLOCK_OR_RETURN(q->mutex);

    return EPS_OK;
}

ep_stat_t tp_queue_dequeue(tp_queue_t *q, tp_task_t *t)
{
    RETURN_ERROR_IF_NULL(q);

    LOCK_OR_RETURN(q->mutex);

    while (q->is_blocking && q->length <= 0)
    {
        /*D("Queue is empty. Waiting for a task");*/
        pthread_cond_wait(&(q->cv_got_task), &(q->mutex));
    }

    if (q->length <= 0)
    {
        DBG("Queue is empty. Return NULL");
        UNLOCK_OR_RETURN(q->mutex);
        return EPS_EMPTY;
    }

    memcpy(t, &(q->elems[q->head]), sizeof(tp_task_t));
    q->head = (q->head+1) % TP_MAX_TASK_QUEUE_SIZE;
    -- q->length;

    /*D("> queue size: %u", q->length);*/

    UNLOCK_OR_RETURN(q->mutex);

    return EPS_OK;
}

ep_stat_t tp_queue_set_nonblocking(tp_queue_t *q)
{
    RETURN_ERROR_IF_NULL(q);

    if (q->is_blocking)
    {
        LOCK_OR_RETURN(q->mutex);
        q->is_blocking = FALSE;
        pthread_cond_broadcast(&(q->cv_got_task));
        UNLOCK_OR_RETURN(q->mutex);
        return EPS_OK;
    }

    return EPS_NOTHING_DONE;
}

ep_stat_t tp_queue_status(tp_queue_t *q)
{
    RETURN_ERROR_IF_NULL(q);

    LOCK_OR_RETURN(q->mutex);

    ep_stat_t status;

    if (q->length == 0)
        status = EPS_EMPTY;
    else if (q->length == TP_MAX_TASK_QUEUE_SIZE)
        status = EPS_FULL;
    else
        status = EPS_OK;

    UNLOCK_OR_RETURN(q->mutex);

    return status;
}
