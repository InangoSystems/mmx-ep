/* ep_ext.c
 *
 * Copyright (c) 2013-2021 Inango Systems LTD.
 *
 * Author: Inango Systems LTD. <support@inango-systems.com>
 * Creation Date: Jan 2016
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
#include "ep_common.h"
#include "ep_ext.h"
#include "ep_worker.h"


#define EXT_PORT 31337
#define SOCK_TIMEOUT 10

/* Buffer to hold response values */
#define MEM_BUF_LEN 512

/* Device.Ethernet.Interface.*.Stats.PacketsReceived */
#define MODEL_OBJECT "Device.X_Inango_Gfast.Line.*.Stats.PacketsSent"

#define CHECK_LIMIT(a) check_limit_average(a)


static int txaId;
static mmx_ep_connection_t ext_conn;
static pthread_t ext_thread;
static tp_threadpool_t *tpool;


/* Default threshold value that is expected - 30,000,000 */


static unsigned long str2ulong(const char *nptr)
{
    unsigned long val;

    errno = 0;
    val = strtoul(nptr, NULL, 10);
    if (errno == 0)
    {
        return val;
    }

    return 0;
}

/*
 * Computes and returns the greatest value
 */
static unsigned long check_limit_any(ep_message_t *msg)
{
    int i;
    unsigned long cur, val;

    cur = val = 0;

    for (i = 0; i < msg->body.getParamValueResponse.arraySize; ++i)
    {
/*
        INFO("msg.body.getParamValueResponse.%d.name: %s", i, msg->body.getParamValueResponse.paramValues[i].name);
        INFO("msg.body.getParamValueResponse.%d.value: %s", i, msg->body.getParamValueResponse.paramValues[i].pValue);
*/
        cur = str2ulong(msg->body.getParamValueResponse.paramValues[i].pValue);
        if (cur > val)
        {
            val = cur;
        }
    }

    return val;
}

/*
 * Computes and returns a sum over all values
 */
static unsigned long check_limit_sum(ep_message_t *msg)
{
    int i;
    unsigned long sum, val;

    sum = 0;

    for (i = 0; i < msg->body.getParamValueResponse.arraySize; ++i)
    {
/*
        INFO("msg.body.getParamValueResponse.%d.name: %s", i, msg->body.getParamValueResponse.paramValues[i].name);
        INFO("msg.body.getParamValueResponse.%d.value: %s", i, msg->body.getParamValueResponse.paramValues[i].pValue);
*/
        val = str2ulong(msg->body.getParamValueResponse.paramValues[i].pValue);
        if (ULONG_MAX - sum > val)
        {
            sum += val;
        }
        else
        {
            /* Overflow */
            sum = ULONG_MAX;
            break;
        }
    }

    return sum;
}

/*
 * Computes and returns an average over all values
 */
static unsigned long check_limit_average(ep_message_t *msg)
{
    unsigned long sum;

    sum = check_limit_sum(msg);

    if (msg->body.getParamValueResponse.arraySize)
    {
        return sum / msg->body.getParamValueResponse.arraySize;
    }
    else
    {
        return 0;
    }
}

/*
 * Common operations to initialize EP request message
 */
static int init_msg(ep_message_t *msg, char *mem_buf, unsigned short mem_buf_size)
{
    int res;

    memset(msg, 0, sizeof(ep_message_t));

    if (mem_buf)
    {
        res = mmx_frontapi_msg_struct_init(msg, mem_buf, mem_buf_size);
        if (res != FA_OK)
        {
            ERROR("mmx_frontapi_msg_struct_init failed (%d)", res);
            return res;
        }
    }

    msg->header.callerId = 1; /* TODO what is a correct value? */
    msg->header.txaId = ++txaId;
    msg->header.respIpAddr = htonl(MMX_EP_ADDR);
    msg->header.respPort = EXT_PORT;

    return FA_OK;
}

/*
 * This function performs the same as mmx_frontapi_make_request. The only
 * exception is that it push messages directly into thread pool queue.
 */
static int make_request(mmx_ep_connection_t *conn, ep_message_t *msg, int *more)
{
    int res;
    size_t rcvd;
    tp_task_t task = {.task_type = TASK_TYPE_RAW};

    res = mmx_frontapi_message_build(msg, task.raw_message, MAX_DISP_MSG_LEN);
    if (res != FA_OK)
    {
        ERROR("mmx_frontapi_message_build failed (%d)", res);
        return res;
    }

    res = tp_add_task(tpool, &task);
    if (res != EPS_OK)
    {
        ERROR("tp_add_task (%d)", res);
        return res;
    }

    res = mmx_frontapi_receive_resp(conn, msg->header.txaId, task.raw_message, MAX_DISP_MSG_LEN, &rcvd);
    if (res != FA_OK)
    {
        ERROR("mmx_frontapi_receive_resp failed (%d)", res);
        return res;
    }

    res = mmx_frontapi_message_parse(task.raw_message, msg);
    if (res != FA_OK)
    {
        ERROR("mmx_frontapi_message_parse failed (%d)", res);
        return res;
    }

    if (more)
        *more = msg->header.moreFlag;

    return FA_OK;
}

/*
 * Retrieve a new value
 */
static int get_new_val(ep_message_t *msg, unsigned long *new_val)
{
    char mem_buf[MEM_BUF_LEN];
    int more, res;

    res = init_msg(msg, mem_buf, sizeof(mem_buf));
    if (res != FA_OK)
    {
        ERROR("init_msg failed (%d)", res);
        return res;
    }

    msg->header.msgType = MSGTYPE_GETVALUE;
    msg->body.getParamValue.arraySize = 1;
    strcpy(msg->body.getParamValue.paramNames[0], MODEL_OBJECT);

    res = make_request(&ext_conn, msg, &more);
    if (res != FA_OK)
    {
        ERROR("mmx_frontapi_make_request failed (%d)", res);
        return res;
    }

    /* Check response status code */
    if (msg->header.respCode)
    {
        /* Failure if msg->header.respCode != 0. Skip this response. */
        return FA_GENERAL_ERROR;
    }

    *new_val = CHECK_LIMIT(msg);

    return FA_OK;
}

/*
 * Action that performed when a MMX_EP_EXT_THRESHOLD is reached
 */
static void perform_limit_action(ep_message_t *msg)
{
    int more, res;

    res = init_msg(msg, NULL, 0);
    if (res != FA_OK)
    {
        ERROR("init_msg failed (%d)", res);
        return;
    }

    msg->header.msgType = MSGTYPE_RESET;
    msg->body.reset.delaySeconds = 0;
    msg->body.reset.resetType = 0;

    res = make_request(&ext_conn, msg, &more);
    if (res != FA_OK)
    {
        ERROR("mmx_frontapi_make_request failed (%d)", res);
    }
}

/*
 * EP extensions thread
 */
static void *ext_thread_routine(void *data)
{
    int res;
    unsigned long cur_val, initial_val, new_val;
    ep_message_t msg;

    cur_val = initial_val = new_val = 0;
    txaId = 0;

    tiddb_add("EXT");

    INFO("MMX extension thread has started");

    /* Get an initial value from hardware */
    res = get_new_val(&msg, &initial_val);
    if (res != FA_OK)
    {
        ERROR("get_new_val failed (%d)", res);
    }

    while (TRUE)
    {
        sleep(MMX_EP_EXT_QUERY_TIMEOUT);

        /* Get a new value from hardware */
        res = get_new_val(&msg, &new_val);
        if (res != FA_OK)
        {
            ERROR("get_new_val failed (%d)", res);
            continue;
        }

        /* Correct the value */
        new_val -= initial_val;

        if (new_val < cur_val)
        {
            /* Seems hardware counters were reseted */
            if (ULONG_MAX - cur_val > new_val)
            {
                cur_val += new_val;
            }
            else
            {
                /* Overflow */
                cur_val = ULONG_MAX;
            }
        }
        else
        {
            cur_val = new_val;
        }

        if (cur_val > MMX_EP_EXT_THRESHOLD)
        {
            /* Threshold exceeded */
            perform_limit_action(&msg);
        }
    }

    return NULL;
}


/*
 * Initializes extensions
 */
ep_stat_t ep_ext_init(tp_threadpool_t *tp)
{
    int res;
    pthread_attr_t tattr;

    res = mmx_frontapi_connect(&ext_conn, EXT_PORT, SOCK_TIMEOUT);
    if (res)
    {
        ERROR("mmx_frontapi_connect failed (%d)", res);
        return EPS_GENERAL_ERROR;
    }

    tpool = tp;

    res = pthread_attr_init(&tattr);

    pthread_attr_setstacksize(&tattr, EP_PTHREAD_STACK_SIZE);

    pthread_create(&ext_thread, &tattr, ext_thread_routine, NULL);

    return EPS_OK;
}

/*
 * Performs cleanup extensions
 */
void ep_ext_cleanup(void)
{
    pthread_cancel(ext_thread);
    pthread_join(ext_thread, NULL);

    mmx_frontapi_close(&ext_conn);
}
