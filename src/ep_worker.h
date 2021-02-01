/* ep_worker.h
 *
 * Copyright (c) 2013-2021 Inango Systems LTD.
 *
 * Author: Inango Systems LTD. <support@inango-systems.com>
 * Creation Date: Aug 2013
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


#ifndef EP_WORKER_H_
#define EP_WORKER_H_

#include <sqlite3.h>

#include "ep_common.h"
#include "ep_config.h"

#include <mmx-backapi-config.h>

#define UDP_SOCK_TIMEOUT  6 /* sec */  // Waiting for response from backends
#define EP_PORT_STARTNUM  10200

#define MAX_MMX_BE_REQ_LEN      20480
#define EP_BE_VALUES_POOL_LEN   20480

#define EP_REQ_POOL_LEN    16384
#define EP_RESP_POOL_LEN   16384


typedef struct worker_data_s {
    int     mmxDbType;   /* type of MMX DB: 0/1/2 - running/startup/candidate */
    sqlite3 *mdb_conn;   /* Meta db connection */
    sqlite3 *main_conn;  /* Main db connection */
    sqlite3_stmt *stmt_get_obj_info;
    sqlite3_stmt *stmt_get_obj_list;

    int self_w_num;   /* "worker-number" of the worker */

    int udp_sock; /* udp socket for communication with other applications*/
    int udp_port; /* port of the thread's udp socket                     */
    int udp_be_sock; /* udp socket for communication with other applications*/
    int udp_be_port; /* port of the thread's udp socket                     */
    int ipc_sock; /* unix socket for communication with other threads */
    struct sockaddr_un addr;
    int addr_len;

    int be_req_cnt; /* seq num of req to backends (for backend-style methods)*/

    /* Buffer for Backend API request/response XML string*/
    char be_req_xml_buf[MAX_MMX_BE_REQ_LEN];

    /* Memory pool for param values in backend API request/response struct */
    char be_req_values_pool[EP_BE_VALUES_POOL_LEN];

    /* Buffer for XML string of EP response to frontend API*/
    char answer_buf[MAX_MMX_EP_ANSWER_LEN];

    /* Memory pool for parameter values received in frontend API request*/
    char fe_req_values_pool[EP_REQ_POOL_LEN];

    /* Memory pool for parameter values prepared for frontend API response*/
    char fe_resp_values_pool[EP_RESP_POOL_LEN];
} worker_data_t;

typedef struct command_subst_value_s {
    BOOL conditional;
    char leaf_name[MAX_LEAF_NAME_LEN];
    char *obj_name;
    /* These represent cases for conditional substitution value */
    char *true_val;
    char *false_val;
    /* Below ones represent cases for parameter name/value substitution values
     * NOTE: currently take place only for shell-script styled SET operation */
    BOOL  name_formatter;  /* @name subst value marker */
    char *name_val;        /* @name subst value */
    BOOL  value_formatter; /* @value subst value marker */
    char *value_val;       /* @value subst value */
} command_subst_value_t;

typedef struct parsed_operation_s {
    char *command;
    command_subst_value_t subst_val[EP_SUBST_VALUES_NUMBER];
    int subst_val_num;
    char *value_to_extract;
    int bekey_param_num;
    char *bekey_params[MAX_INDECES_PER_OBJECT];
} parsed_operation_t;

typedef struct backend_subst_value_s {
    char *backend_key_name;
    command_subst_value_t mmx_subst_val;
} backend_subst_value_t;

typedef struct parsed_backend_method_s {
    char *beObjName;
    int subst_val_num;
    backend_subst_value_t subst_val[EP_SUBST_VALUES_NUMBER];
    int bekey_param_num;
    char *bekey_params[MAX_INDECES_PER_OBJECT];
} parsed_backend_method_t;


#endif /* EP_WORKER_H_ */
