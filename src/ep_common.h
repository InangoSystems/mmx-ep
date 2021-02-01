/* ep_common.h
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

#ifndef EP_COMMON_H_
#define EP_COMMON_H_

#include <stdio.h>
#include <stdlib.h>

#include <unistd.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/un.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <stdarg.h>
#include <netinet/in.h>
#include <errno.h>
#include <sys/stat.h>
#include <time.h>
#include <sys/time.h>
#include <pthread.h>
#include <syslog.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <ing_gen_utils.h>

#include "ep_config.h"
#include "ep_defines.h"
#include "mmx-frontapi.h"
#include "mmx-backapi.h"

#define MAX_DISP_MSG_LEN     4096
#define MAX_MMX_EP_ANSWER_LEN  (32*1024)
#define FILENAME_BUF_LEN     256

#define SUN_PATH        "/tmp/"      //TODO getenv("INGSUNPATH")
#define SUN_DISP_NAME   SUN_PATH "mmx_disp_socket"

#define DISP_SOCK_TIMEOUT   2 /* sec */

/* Front API */
#define INDEX_PLACEHOLDER   "{i}"
#define LEN_PLACEHOLDER     (sizeof(INDEX_PLACEHOLDER) - 1)

typedef char    BOOL;
typedef unsigned long ep_stat_t;


/* Entry-Point result status values */
enum ep_statuses
{
    EPS_OK = 0,                         /*    0 */
    EPS_GENERAL_ERROR,                  /*    1 */
    EPS_SYSTEM_ERROR,                   /*    2 */
    EPS_INVALID_ARGUMENT,               /*    3 */
    EPS_INVALID_PARAM_NAME,             /*    4 */
    EPS_INVALID_FORMAT,                 /*    5 */
    EPS_FULL,                           /*    6 */
    EPS_EMPTY,                          /*    7 */
    EPS_ALREADY_EXISTS,                 /*    8 */
    EPS_NOT_FOUND,                      /*    9 */
    EPS_OUTOFMEMORY,                    /*   10 */
    EPS_NO_MORE_ROOM,                   /*   11 */
    EPS_NOTHING_DONE,                   /*   12 */
    EPS_IGNORED,                        /*   13 */
    EPS_SQL_ERROR,                      /*   14 */
    EPS_CANNOT_OPEN_DB,                 /*   15 */
    EPS_INVALID_DB_TYPE,                /*   16 */
    EPS_NOT_WRITABLE,                   /*   17 */
    EPS_NO_PERMISSION,                  /*   18 */
    EPS_NOT_IMPLEMENTED,                /*   19 */
    EPS_BACKEND_ERROR,                  /*   20 */
    EPS_EP_HOLD,                        /*   21 */
    EPS_RESOURCE_NOT_FREE,              /*   22 */
    EPS_TIMEOUT,                        /*   23 */
    EPS_LAST = 1001                     /* 1001 */
};

/* Entry-point hold status reasons */
typedef enum
{
    EP_HOLD_NONE = 0,
    EP_HOLD_PRE_REBOOT,
    EP_HOLD_PRE_RESET,
    EP_HOLD_ON_DB_SAVE,
} ep_hold_reason_t;

typedef enum
{
    EP_DATA_OWNER_SYSTEM = 0,
    EP_DATA_OWNER_USER
} ep_dataOwner_t;

/* Parameter parsing types */

typedef enum index_type_e {
    REQ_IDX_TYPE_ALL = 0,
    REQ_IDX_TYPE_EXACT,
    REQ_IDX_TYPE_RANGE,
    REQ_IDX_TYPE_PLACEHOLDER
} index_type_t;

typedef struct param_name_index_s {
    index_type_t type;

    /* TODO what if request contains 'range' index parameter
     * with (range->begin) < (range->end), so special validation
     * function is needed ... */
    union {
        struct {
            int num;
        } exact_val;

        struct {
            int begin;
            int end;
        } range_val;
    };
} param_name_index_t;

/* Exact index-values set - actually it is a two-dimensional array
 *   dim1 (outer) - 1 .. MAX_INSTANCES_PER_OBJECT
 *   dim2 (inner) - 1 .. MAX_INDECES_PER_OBJECT
 *
 * Such an array can be used to present the set of Object instances
 *  with exactly defined index-parameter values. Example:
 *
 * To present instances like the following
 *
 *   Device.Users.User.1.X_Inango_AuthKey.1.
 *   Device.Users.User.1.X_Inango_AuthKey.2.
 *   Device.Users.User.2.X_Inango_AuthKey.1.
 *   Device.Users.User.2.X_Inango_AuthKey.2.
 *   Device.Users.User.3.X_Inango_AuthKey.1.
 *   Device.Users.User.3.X_Inango_AuthKey.2.
 *
 *  this structure will be filled like this:
 *
 *   .inst_num     = 6 (number of instances)
 *   .index_num    = 2 (number of indexes in instance)
 *
 *   .indexvalues[0][0] = 1, .indexvalues[0][1] = 1,
 *   .indexvalues[1][0] = 1, .indexvalues[1][1] = 2,
 *   .indexvalues[2][0] = 2, .indexvalues[2][1] = 1,
 *   .indexvalues[3][0] = 2, .indexvalues[3][1] = 2,
 *   .indexvalues[4][0] = 3, .indexvalues[4][1] = 1,
 *   .indexvalues[5][0] = 3, .indexvalues[5][1] = 2
 */
typedef struct exact_indexvalues_set_s {
    int inst_num;
    int index_num;
    int indexvalues[MAX_INSTANCES_PER_OBJECT][MAX_INDECES_PER_OBJECT];
} exact_indexvalues_set_t;


/* Exact index-values of the single instance - actually it is an array
 *   of the instance indexes (1 .. MAX_INDECES_PER_OBJECT)
 *
 * Example:
 *
 * To present instance like the following
 *   Device.Users.User.1.X_Inango_AuthKey.120.
 *  this structure will be filled like this:
 *
 *   .index_num = 2 (number of indexes in instance)
 *   .indexvalues[0] = 1,
 *   .indexvalues[1] = 120
 */
typedef struct exact_indexvalues_s {
    int index_num;
    int indexvalues[MAX_INDECES_PER_OBJECT];
} exact_indexvalues_t;


/* Type of path name token */
typedef enum {
    PATH_TOKEN_UNDEF = 0,
    PATH_TOKEN_NODE,
    PATH_TOKEN_INDEX,
    PATH_TOKEN_PARAMNAME,
    PATH_TOKEN_PLACEHOLDER
} path_token_type_t;


/*
 * Parsed parameter name from request string
 */
typedef struct parsed_param_name_s {
    char obj_name[MSG_MAX_STR_LEN];
    char leaf_name[MSG_MAX_STR_LEN];
    BOOL partial_path;
    path_token_type_t last_token_type;
    int index_num;
    int index_set_num;
    param_name_index_t indices[MAX_INDECES_PER_OBJECT];
} parsed_param_name_t;

typedef enum oper_style_e {
    OP_STYLE_NOT_DEF,
    OP_STYLE_SCRIPT,
    OP_STYLE_SHELL_SCRIPT,
    OP_STYLE_UCI,
    OP_STYLE_UBUS,
    OP_STYLE_BACKEND,
    OP_STYLE_DB,
    OP_STYLE_ERROR

    /* NOTE: OP_STYLE_SHELL_SCRIPT currently takes place
     * just for SET operation and nothing else */
} oper_style_t;

typedef enum access_perm_e {
    ACCESS_VIEWER = 0,
    ACCESS_CONFIG,
    ACCESS_ADMIN,
} access_perm_t;

typedef enum oper_method_e {
    OP_GET,
    OP_SET,
    OP_ADDOBJ,
    OP_DELOBJ,
    OP_GETALL,
    OP_ERROR,
} oper_method_t;

/* Supported Objects dependency classes */
typedef enum obj_depclass_e {
    OBJ_DEP_AUTO_CREATE,
    OBJ_DEP_AUTO_DELETE,
    OBJ_DEP_ERROR,
} obj_depclass_t;


/*
 * Object meta information from management model
 */
typedef struct obj_info_s {
    char objName[MSG_MAX_STR_LEN];
    char objInfoTblName[MSG_MAX_STR_LEN];
    char objValuesDbName[MSG_MAX_STR_LEN];
    char objValuesTblName[MSG_MAX_STR_LEN];
    char backEndName[MAX_BENAME_STR_LEN];

    BOOL writable;
    BOOL configurable;
    int readFrontEnds;
    int writeFrontEnds;

    oper_style_t addObjStyle;
    char addObjMethod[MAX_METHOD_STR_LEN];
    oper_style_t delObjStyle;
    char delObjMethod[MAX_METHOD_STR_LEN];
    oper_style_t getOperStyle;
    char getMethod[MAX_METHOD_STR_LEN];
    oper_style_t setOperStyle;
    char setMethod[MAX_METHOD_STR_LEN];
    oper_style_t getAllOperStyle;
    char getAllMethod[MAX_METHOD_STR_LEN];

    /*  Unused Object's meta properties */
    /*
        int objInternalId;
        char packageName[MSG_MAX_STR_LEN];
        access_perm_t userAccessPerm;
        int minEntNumber;
        int maxEntNumber;
        char numOfEntParamName[MSG_MAX_STR_LEN];
        char enableParamName[MAX_LEAF_NAME_LEN];
        char uniqueKeyParamNames[MSG_MAX_STR_LEN];
        char indexParamNames[MSG_MAX_STR_LEN];
        char objDesc[MSG_MAX_STR_LEN];
    */
} obj_info_t;

/*
 * Objects dependency meta information from management model
 */
typedef struct obj_dependency_info_s {
    obj_depclass_t objDepClass;
    char parentObjName[MSG_MAX_STR_LEN];
    char parentParamName[MAX_LEAF_NAME_LEN];
    char childObjName[MSG_MAX_STR_LEN];
    char childParamName[MAX_LEAF_NAME_LEN];
} obj_dependency_info_t;

typedef struct scalar_info_s {
    char objName[MSG_MAX_STR_LEN];
    char objValuesDbName[MSG_MAX_STR_LEN];
    char objValuesTblName[MSG_MAX_STR_LEN];

    /*  Unused Scalar Object's meta properties */
    /*
        char objInfoTblName[MSG_MAX_STR_LEN];
    */
} scalar_info_t;

/*
 * Parameter meta information from management model
 */
typedef struct param_info_s {
    char paramName[MAX_LEAF_NAME_LEN];
    char paramType[MAX_PARAM_TYPE_LEN];
    BOOL isIndex;
    BOOL hidden;
    BOOL notSaveInDb;

    BOOL writable;
    int readFrontEnds;
    int writeFrontEnds;

    oper_style_t getOperStyle;
    oper_style_t setOperStyle;
    char getMethod[MAX_METHOD_STR_LEN];
    char setMethod[MAX_METHOD_STR_LEN];
    /* without this flag we cannot detect default
     * value equal empty string */
    BOOL hasDefValue;
    char defValue[MSG_MAX_STR_LEN];

    /*  Unused Object Parameter's meta properties */
    /*
        access_perm_t userAccessPerm;
        BOOL valueIsList;
        long minValue;
        long maxValue;
        int minLength;
        int maxLength;
        char units[MAX_UNITS_STR_LEN];
        char enumValues[MSG_MAX_STR_LEN];
        char paramDescr[MSG_MAX_STR_LEN];
    */
} param_info_t;

/*
 * Backend information from management model
 */
typedef struct backend_info_s {
    char beName[MAX_BACKEND_NAMELEN];
    char initScript[MSG_MAX_STR_LEN];
    int  portNumber;
} backend_info_t;


#define RETURN_ERROR_IF_NULL(v)     do { \
    if (!(v)) { ERROR("Function `%s': argument `"#v"' is NULL", __func__); return EPS_INVALID_ARGUMENT; } \
} while(0)

#define GOTO_RET_WITH_ERROR(err_num, msg, ...)     do { \
    ERROR(msg, ##__VA_ARGS__); \
    status = err_num; \
    goto ret; \
} while (0)

/* Log-message macros */
#ifdef MMX_EP_DEBUG
    #define DBG(msg, ...)      ing_log(LOG_DEBUG, "[%s] %s (%d): "msg"\n", tiddb_get(), __func__, __LINE__, ##__VA_ARGS__)
#else
    #define DBG(msg, ...)
#endif

#define WARN(msg,...)      ing_log(LOG_WARNING, "[%s] %s (%d): "msg"\n", tiddb_get(), __func__, __LINE__, ##__VA_ARGS__)
#define INFO(msg, ...)     ing_log(LOG_INFO, "[%s] "msg"\n", tiddb_get(), ##__VA_ARGS__)
#define ERROR(msg, ...)    ing_log(LOG_ERR, "[%s] %s (%d): "msg"\n", tiddb_get(), __func__, __LINE__, ##__VA_ARGS__)
#define CRITICAL(msg, ...) ing_log_critical("[%s] "msg"\n", tiddb_get(), ##__VA_ARGS__)

/* Log-message macros for recursion function(s)
 * (recursion function must provide the (int) variable 'level')
 */
#define RECURSLEVEL_DBG(msg, ...)      DBG("/LEVEL=%d/ "msg, level, ##__VA_ARGS__)
#define RECURSLEVEL_WARN(msg, ...)     WARN("/LEVEL=%d/ "msg, level, ##__VA_ARGS__)
#define RECURSLEVEL_INFO(msg, ...)     INFO("/LEVEL=%d/ "msg, level, ##__VA_ARGS__)
#define RECURSLEVEL_ERROR(msg, ...)    ERROR("/LEVEL=%d/ "msg, level, ##__VA_ARGS__)


/*  Common MMX Entry-Point API functions used by all EP components */

/*     ep_common_init
 * Initialization of common Entry-Point handle
 */
int ep_common_init(void);
void ep_common_cleanup();

/*      get_full_db_path
 * Returns full path to given db
 */
char *get_full_db_path(char *buf, size_t buf_len, char *db_name);
char *get_db_path_by_dbtype(char *buf, size_t buf_len, mmx_dbtype_t db_type);

/*      get_db_run_path
 * Returns path to directory with run-time MMX DBs
 */
char *get_db_run_path(char *buf, size_t buf_len);

/*      get_db_saved_path, get_db_cand_path
 * Returns path to directory with saved/candidate MMX DBs
 */
char *get_db_saved_path(char *buf, size_t buf_len);
char *get_db_cand_path(char *buf, size_t buf_len);

/*      get_start_dbtype_filename
 * Returns name of file containing DB type that should be used
 * for the next EP restart (if file is empty - the saved-db will be used)
 */
char *get_start_dbtype_filename(char *buf, size_t buf_len);

/*      get_config_saved_path
 * Returns path to directory with saved config files
 */
char *get_config_saved_path(char *buf, size_t buf_len);

/*      get_config_path
 * Returns path to directory with config files
 */
char *get_config_path(char *buf, size_t buf_len);

/*       tiddb_add
 * Adds the specified thread name to the internal thread ID database
 * (containing short names of all EP threads)
 */
void tiddb_add(char *name);


/*      tiddb_get
 *  Returns thread name (from thread ID db) of the calling thread.
 */
const char *tiddb_get(void);

/*      tiddb_get_w_num
 *  Returns thread index +1 (from thread ID db) of the calling thread.
 *  This number is used as a "worker number" (from 1 to EP_TP_WORKER_THREADS_NUM)
 */
int tiddb_get_w_num(void);


/*    ep_common_get_hold_status
 * Returns current EP's hold status (true - EP is hold, false - otherwise)
 * and hold reason value.
 */
ep_stat_t ep_common_get_hold_status(int *p_hold, int *p_reason);

/*   ep_common_set_hold_status
 * Set EP to HOLD status for the "interval" seconds with the specified reason.
 */
ep_stat_t ep_common_set_hold_status(int reason, unsigned interval);

/*    ep_common_set_unhold_status
 *  Remove HOLD status from Entry-Point
 */
/* TODO is not currently used */
ep_stat_t ep_common_set_unhold_status();

/*    ep_common_check_hold_status
 * Returns current EP's hold status (true - EP is hold, false - otherwise)
 * and hold reason value.
 */
ep_stat_t ep_common_check_hold_status(int *p_hold, int *p_reason);



/*           EP write lock functions
 * EP write operations (SetParamValue, AddObject, DelObject, DiscoverConfig)
 * must be "mutually exclusive". To provide this a special lock (pthread mutex with
 * conditional variable) is used.
 */

 /*    ep_common_get_write_lock
  *  Provides EP write lock (mutex) to the calling thread.
  *  The mutex is waited for EP_WRITE_OP_WAIT_SEC seconds, if it is not
  * locked by other thread (or released during waiting period),
  *  EPS_OK is returned, otherwise - error code is returned.
  * The input reqType, transaction Id and caller Id is saved just for
  *  information purposes.
  */
ep_stat_t ep_common_get_write_lock(int reqType, int txaId, int callerId);


/*   ep_common_finalize_write_lock
 *  Releases EP write lock that was used by the specified transaction
 */
ep_stat_t ep_common_finalize_write_lock(int txaId, int callerId, BOOL force);


/*   ep_common_get_beinfo_index
 *  Returns index of the specified backend in the backend info array
 *  (run-time storage of backend information: name, port and init script)
 */
int ep_common_get_beinfo_index(char *beName);

ep_stat_t ep_common_get_beinfo_by_index(int beIdx, backend_info_t *beInfo);

ep_stat_t ep_common_get_beinfo_by_bename(char *beName, backend_info_t *beInfo);

char * objdepclass2string(int objdepclass);

/*   ep_common_get_objdep_info
 *  Returns number of Objects dependencies known in EP run-time storage,
 *   output arg **objdep_info is set to the first Objects dependency inside
 *   the storage
 */
int ep_common_get_objdep_info(obj_dependency_info_t **objdep_info);

#ifdef USE_SYSLOG
static inline void ep_openlog() { ing_openlog(); }
static inline void ep_closelog() { ing_closelog(); }
#endif

#endif /* EP_COMMON_H_ */
