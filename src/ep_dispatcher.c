/* ep_dispatcher.c
 *
 * Copyright (c) 2013-2021 Inango Systems LTD.
 *
 * Author: Inango Systems LTD. <support@inango-systems.com>
 * Creation Date: Jul 2013
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
#include <signal.h>
#include <unistd.h>
#include "ep_common.h"
#ifdef MMX_EP_EXT_THRESHOLD
#include "ep_ext.h"
#endif
#include "ep_threadpool.h"
#include "mmx-frontapi.h"

#if defined(__DATE__) && defined(__TIME__)
#    define ING_TIMESTAMP  __DATE__ " " __TIME__
#else
#    define ING_TIMESTAMP "N/A"
#endif

#define EP_INIT_WAIT_TIMEOUT    6  //secs

#define EP_MAX_STARTTYPE_VALUE  1

volatile BOOL g_disp_loop_terminated = FALSE;
volatile BOOL g_disp_initialized = FALSE;
volatile BOOL g_disp_restart = FALSE;

volatile int  g_start_type = 0;   /* 0 - default start, 1 - start with candidate db */


#define DISP_STR_CONFIG_DISCOVER_MSG "<EP_ApiMsg>" \
    "<hdr>" \
        "<callerId>0</callerId>" \
        "<txaId>0</txaId>     " \
        "<respFlag>0</respFlag>   " \
        "<respMode>2</respMode>" \
        "<respIpAddr>0.0.0.0</respIpAddr>" \
        "<respPort>0</respPort>" \
        "<msgType>DiscoverConfig</msgType>" \
        "<flags></flags>" \
    "</hdr>" \
    "<body>" \
        "<DiscoverConfig>" \
            "<backendName></backendName>" \
            "<objName></objName>" \
        "</DiscoverConfig>" \
    "</body>" \
"</EP_ApiMsg>"


#define DISP_STR_INITACTIONS_MSG "<EP_ApiMsg>" \
    "<hdr>" \
        "<callerId>0</callerId>" \
        "<txaId>0</txaId>     " \
        "<respFlag>0</respFlag>   " \
        "<respMode>2</respMode>" \
        "<respIpAddr>0.0.0.0</respIpAddr>" \
        "<respPort>0</respPort>" \
        "<msgType>InitActions</msgType>" \
        "<flags></flags>" \
    "</hdr>" \
    "<body>" \
    "</body>" \
"</EP_ApiMsg>"

/*
// unused function
ep_stat_t disp_get_restart_flag(int *restartFlag)
{
    if (restartFlag)
       *restartFlag = g_disp_restart;

    return EPS_OK;
}
*/

ep_stat_t disp_set_restart_flag(int restartType)
{
    if (g_disp_initialized == TRUE)
        g_disp_restart = TRUE;

    if (restartType < 0 || restartType > EP_MAX_STARTTYPE_VALUE)
        g_start_type = 0;
    else
        g_start_type = restartType;

    return EPS_OK;
}

static ep_stat_t disp_add_config_discovery_task(tp_threadpool_t *tp)
{
    tp_task_t task = { .task_type = TASK_TYPE_RAW };
    strncpy(task.raw_message, DISP_STR_CONFIG_DISCOVER_MSG, MAX_DISP_MSG_LEN-1);

    return tp_add_task(tp, &task);
}

static ep_stat_t disp_add_init_actions_task(tp_threadpool_t *tp)
{
    tp_task_t task = { .task_type = TASK_TYPE_RAW };
    strncpy(task.raw_message, DISP_STR_INITACTIONS_MSG, MAX_DISP_MSG_LEN-1);

    return tp_add_task(tp, &task);
}



static ep_stat_t disp_handle_msg(tp_threadpool_t *tp, const char *xmlmsg)
{
    // TODO flags
    ep_stat_t status = EPS_OK;
    tp_task_t task;
    int hold = TRUE;
    int reason = 0;

    memset((char *)&task, 0, sizeof(task));

    if ((ep_common_check_hold_status(&hold, &reason) == EPS_OK) && (hold == FALSE))
    {
        task.task_type = TASK_TYPE_RAW;
        strncpy(task.raw_message, ((ep_packet_t *)xmlmsg)->msg, MAX_DISP_MSG_LEN-1);
        status = tp_add_task(tp, &task);
    }
    else
    {
        DBG("Request dropped, since Entry-point is on HOLD - reason %d; ", reason);
        status = EPS_EP_HOLD;
    }

    return status;
}

static ep_stat_t disp_loop(tp_threadpool_t *tp, int udp_sock, int ipc_sock)
{
    ep_stat_t status;

    char msg_buffer[MAX_DISP_MSG_LEN];
    /* Client address */
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
    client_addr_len = sizeof(struct sockaddr_in);

    while (!g_disp_loop_terminated && !g_disp_restart)
    {
        //DBG("dispatcher loop cycle %d", cnt++);
        /* Listen to UDP socket */
        int res = recvfrom(udp_sock, msg_buffer, MAX_DISP_MSG_LEN, 0,
                            (struct sockaddr *) &client_addr, &client_addr_len);
        if (res < 0)
        {
            if (errno != 0 && errno != EAGAIN)
                ERROR("Failed to receive a message in Dispatcher: %s",
                                                               strerror(errno));
            continue;
        }

        msg_buffer[res] = '\0';

        DBG("Received %d bytes:\n%s", res, ((ep_packet_t *)msg_buffer)->msg);

        if ((status = disp_handle_msg(tp, msg_buffer)) != EPS_OK)
        {
            ERROR("Could not handle the message (%d)", status);
            continue;
        }
    }

    DBG("Exit from disp loop. Termination flag %d, restart flag %d",
         g_disp_loop_terminated, g_disp_restart);

    return EPS_OK;
}

/* Create sockets */
static ep_stat_t disp_sockets_init(int *udp_sock, int *ipc_sock)
{
    ep_stat_t status;

    status = udp_socket_init(udp_sock, MMX_EP_ADDR, MMX_EP_PORT);
    if (status != EPS_OK)
    {
        ERROR("Failed to create UDP socket (%d)", status);
        return status;
    }

    status = unix_socket_init(ipc_sock, SUN_DISP_NAME);
    if (status != EPS_OK)
    {
        ERROR("Failed to create IPC socket (%d)", status);
        return status;
    }

    /* set timeout on udp socket */
    struct timeval timeout;
    timeout.tv_sec = DISP_SOCK_TIMEOUT;
    timeout.tv_usec = 0;

    if (setsockopt(*udp_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                                                           sizeof(timeout)) < 0)
    {
        ERROR("Dispatcher's udp sock timeout setting failed: %s", strerror(errno));
        return EPS_SYSTEM_ERROR;
    }

    return EPS_OK;
}

#if !DEBUG
/* Makes us a daemon */
static ep_stat_t m_daemonize()
{
    pid_t pid = fork();

    if (pid < 0)
        return EPS_GENERAL_ERROR;
    else if (pid > 0)
        exit(EXIT_SUCCESS); /* Exit parent process */

    umask(0);

    if (setsid() < 0)
        return EPS_GENERAL_ERROR;

    if ((chdir("/")) < 0)
        return EPS_GENERAL_ERROR;

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    return EPS_OK;
}
#endif


static ep_stat_t disp_restart_ep()
{
    char buf[256] = {0};
    FILE *fp;

    memset((char*)buf, 0, sizeof(buf));

    /* Prepare restart entry-point command */
    if (g_start_type == 1)
        sprintf(buf, "/etc/init.d/mmxep_init restart_with_canddb &>/dev/null ");
    else
       sprintf(buf, "/etc/init.d/mmxep_init &>/dev/null ");

    DBG("Restart command: %s", buf);

    /* Perform the prepared command */
    fp = popen(buf, "r");
    if (!fp)
    {
        DBG("Could not execute prepared command");
        return EPS_SYSTEM_ERROR;
    }
    pclose(fp);

    return EPS_OK;
}

static void m_handle_error_signal(int signo)
{
    CRITICAL("Caught error signal %d (%s)", signo, strsignal(signo));
    abort();
}

static void m_handle_term_signal(int signo)
{
    CRITICAL("Caught termination signal %d (%s)", signo, strsignal(signo));
    g_disp_loop_terminated = TRUE;
}

/* TODO check Signal handlers work properly */
static void m_setup_signals(void)
{
    struct sigaction s;

    memset(&s, 0, sizeof(s));

    s.sa_handler = m_handle_term_signal;
    sigaction(SIGINT, &s, NULL);
    sigaction(SIGTERM, &s, NULL);

    s.sa_handler = m_handle_error_signal;
    sigaction(SIGSEGV, &s, NULL);
    sigaction(SIGFPE, &s, NULL);
    sigaction(SIGILL, &s, NULL);
    sigaction(SIGBUS, &s, NULL);
}

int main(int argc, char **argv)
{
    ep_stat_t status = EPS_OK;
    int udp_sock, ipc_sock;
    tp_threadpool_t *tp = NULL;

#if USE_SYSLOG
    ep_openlog();
#endif

    DBG("define MMXBA_MAX_NUMBER_OF_GET_PARAMS = [%d]", MMXBA_MAX_NUMBER_OF_GET_PARAMS);
    DBG("define MMXBA_MAX_NUMBER_OF_SET_PARAMS = [%d]", MMXBA_MAX_NUMBER_OF_SET_PARAMS);
    DBG("define MAX_PARAMS_PER_OBJECT = [%d]", MAX_PARAMS_PER_OBJECT);
    DBG("define EP_TP_WORKER_THREADS_NUM = [%d]", EP_TP_WORKER_THREADS_NUM);
    DBG("define EP_PTHREAD_STACK_SIZE = [%d]", EP_PTHREAD_STACK_SIZE);

#if !DEBUG
    if (m_daemonize())
    {
        CRITICAL("Could not daemonize properly. Exiting");
        exit(EXIT_FAILURE);
    }
#endif

ep_disp_init:

    g_disp_initialized = FALSE;

    status = ep_common_init();
    if (status != EPS_OK)
    {
        ERROR("Common Entry point init failed (status = %d)", status);
        exit(EXIT_FAILURE);
    }

    tiddb_add("DSP");

    INFO(" ++++++ Entry point started (compiled on %s) ++++++", ING_TIMESTAMP);

    if (disp_sockets_init(&udp_sock, &ipc_sock))
    {
        CRITICAL("Could not initialize sockets for EP dispatcher. Exiting");
        exit(EXIT_FAILURE);
    }
    DBG("EP sockets created: udp sock %d, ipc sock %d", udp_sock, ipc_sock);

    m_setup_signals();

    /* Start pool of worker threads */
    if (tp_init(&tp) != EPS_OK)
    {
        ERROR("Could not initialize thread pool");
        exit(EXIT_FAILURE);
    }
    sleep(1);

    /* Perform init actions and wait to allow normal work of init actions*/
    disp_add_init_actions_task(tp);
    sleep(1);

    /* Add DiscoverConfig task to the queue and wait
       before starting the main dispatcher loop      */
    disp_add_config_discovery_task(tp);
    sleep(EP_INIT_WAIT_TIMEOUT);

#ifdef MMX_EP_EXT_THRESHOLD
    ep_ext_init(tp);
#endif

    g_disp_initialized = TRUE;

    DBG("Entering main loop - listening on port %d)", MMX_EP_PORT);
    disp_loop(tp, udp_sock, ipc_sock);
    DBG("Leaving main dispatcher loop");

#ifdef MMX_EP_EXT_THRESHOLD
    ep_ext_cleanup();
#endif

    if (tp_destroy(tp) != EPS_OK)
    {
        ERROR("Could not destroy thread pool properly");
        exit(EXIT_FAILURE);
    }

    close(udp_sock); udp_sock = 0;
    close(ipc_sock); ipc_sock = 0;

    if (g_disp_restart == TRUE)
    {
       g_disp_restart = FALSE;

       if (disp_restart_ep() != EPS_OK)
          goto ep_disp_init;
    }

    ep_common_cleanup();

#if USE_SYSLOG
    ep_closelog();
#endif

    INFO(" ------ Entry point exited ------ ");

    return 0;
}
