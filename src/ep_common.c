/* ep_common.c
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
 * Common code for entry point
 */

#include <time.h>

#include "ep_common.h"
#include "ep_db_utils.h"


/* Entry-Point common handle
 * Contains information used by all EP components (dispatcher, ep_workers...)
 */

/* Thread ID database - Contains short names for all EP threads */
typedef struct tid_db_s {
    pthread_t tid[EP_TP_WORKER_THREADS_NUM+2];
    char tid_s[EP_TP_WORKER_THREADS_NUM+2][4];
    int num;
    pthread_rwlock_t lock;
} tid_db_t;

/* Entry_point hold status information */
typedef struct  {
    pthread_rwlock_t lock;
    BOOL             is_hold;
    ep_hold_reason_t hold_reason;
    long       start_time;      // time of hold start
    unsigned   interval;        // hold interval in sec
    pthread_t  thread_id;
    char       thread_name[4];  //name of thread performed the last set on hold
} ep_hold_status_t;


/* Entry_point write operation status information */
/* Write-operation lock is used for SetParamValue, AddObject, DelObject,
   DiscoverConfig and SaveConfig operations */
typedef struct {
    BOOL    writing_is_active;
    pthread_mutex_t write_mutex;
    pthread_cond_t  write_cv;
    int     waiting_thread_cnt; // number of worker threads waiting for
                                // the "EP write lock"
    int     reqType;      // Type of mgmt request using the "write lock"
    int     reqTxaId;     // Transaction Id of the request
    int     callerId;     // Caller id of the request using the "write lock"
    long    start_time;   // time of write operation start
    pthread_t  thread_id; // id of thread performing current write operation
    char       thread_name[4];  //name of the thread
} ep_write_status_t;

/* Backend info array: small run-time storage of backend information */
typedef struct
{
    backend_info_t be_info[MAX_BACKEND_NUM];
    int            be_num;
} ep_all_backends_t;

/* Entry-point handle structure */
typedef struct  {
    char              g_db_base_path[FILENAME_BUF_LEN];
    char              g_db_saved_path[FILENAME_BUF_LEN];
    char              g_db_cand_path[FILENAME_BUF_LEN];
    char              g_config_path[FILENAME_BUF_LEN];
    char              g_config_saved_path[FILENAME_BUF_LEN];
    char              g_start_dbtype_file[FILENAME_BUF_LEN];
    ep_all_backends_t g_total_backend_info;
    tid_db_t          g_tid_db;
    ep_hold_status_t  g_ep_hold_status;
    ep_write_status_t g_ep_write_status;
} ep_handle_t;

ep_handle_t g_ep_handle;

/*  ---------  EP Objects dependencies info  -----------  */

/* Internal storage of management Objects' dependencies */
static obj_dependency_info_t *obj_dependency_info = NULL;

/* Storage size (= number of Object dependencies from Meta DB that
 * were successfully parsed, validated and put to internal storage) */
static int obj_dependency_num = -1;


/*  ----------  EP common static functions  ------------  */

/*
 * db_path_init
 * Copies db directory path to a buffer into Entry-Point handle.
 * Must be executed once in the main thread
 */
static void db_path_init(ep_handle_t *p_ep)
{
    strcpy_safe(p_ep->g_db_base_path, DB_PATH, FILENAME_BUF_LEN);
    if (LAST_CHAR(p_ep->g_db_base_path) != '/')
        strcat_safe(p_ep->g_db_base_path, "/", FILENAME_BUF_LEN);

    strcpy_safe(p_ep->g_db_saved_path, SAVEDDB_PATH, FILENAME_BUF_LEN);
    if (LAST_CHAR(p_ep->g_db_saved_path) != '/')
        strcat_safe(p_ep->g_db_saved_path, "/", FILENAME_BUF_LEN);

    strcpy_safe(p_ep->g_db_cand_path, CANDDB_PATH, FILENAME_BUF_LEN);
    if (LAST_CHAR(p_ep->g_db_cand_path) != '/')
        strcat_safe(p_ep->g_db_cand_path, "/", FILENAME_BUF_LEN);

    strcpy_safe(p_ep->g_start_dbtype_file, DBSTARTTYPE_FILE, FILENAME_BUF_LEN);

    DBG("DB path init: DB_PATH = %s, SAVEDDB_PATH = %s, CANDDB_PATH = %s, start dbtype file = %s",
         p_ep->g_db_base_path, p_ep->g_db_saved_path, p_ep->g_db_cand_path,
         p_ep->g_start_dbtype_file);
}

static void config_saved_path_init(ep_handle_t *p_ep)
{
    strcpy_safe(p_ep->g_config_saved_path, SAVEDCONFIG_PATH, FILENAME_BUF_LEN);
    if (LAST_CHAR(p_ep->g_config_saved_path) != '/')
        strcat_safe(p_ep->g_config_saved_path, "/", FILENAME_BUF_LEN);

    strcpy_safe(p_ep->g_config_path, MMXCONFIG_PATH, FILENAME_BUF_LEN);
    if (LAST_CHAR(p_ep->g_config_path) != '/')
        strcat_safe(p_ep->g_config_path, "/", FILENAME_BUF_LEN);

    DBG("MMX config path init completed: MMXCONFIG_PATH = %s, SAVEDCONFIG_PATH = %s",
        p_ep->g_config_path, p_ep->g_config_saved_path);
}

static void tiddb_init(ep_handle_t *p_ep)
{
    memset((char *)&(p_ep->g_tid_db), 0, sizeof(tid_db_t));
    pthread_rwlock_init(&(p_ep->g_tid_db.lock), NULL);
}


static void ep_hold_info_init(ep_handle_t *p_ep)
{
    memset((char *)&(p_ep->g_ep_hold_status), 0, sizeof(ep_hold_status_t));
    pthread_rwlock_init(&(p_ep->g_ep_hold_status.lock), NULL);
}


static void ep_write_status_init(ep_handle_t *p_ep)
{
    memset((char *)&(p_ep->g_ep_write_status), 0, sizeof(ep_write_status_t));

    pthread_mutex_init(&p_ep->g_ep_write_status.write_mutex, NULL);
    pthread_cond_init(&p_ep->g_ep_write_status.write_cv, NULL);
}


/* ep_init_backend_info -
   Copies backend information from the meta database to the internal structure.
   This info will be used by all ep-workers during Entry-Point run-time
   (Backend information is not changed during EP run-time, so we can keep
   it internally instead of select it from DB each time when it is needed)*/
#define SQL_QUERY_GET_BEINFO  \
     "SELECT [backendName], [mgmtPort], [initScript] FROM [mmx_backendinfo]"
static void ep_init_backend_info(ep_handle_t *p_ep)
{
    char query[80] = SQL_QUERY_GET_BEINFO;
    char          db_path[FILENAME_BUF_LEN];
    int           res = 0, be_cnt = 0;
    sqlite3      *dbconn = NULL;
    sqlite3_stmt  *stmt = NULL;
    ep_all_backends_t *p_total_be = &(p_ep->g_total_backend_info);

    memset((char *)(p_total_be), 0, sizeof(ep_all_backends_t));

    if (sql_getDbConn(&dbconn, get_full_db_path(db_path, sizeof(db_path),
                                                    "mmx_meta_db")) != EPS_OK)
    {
        ERROR("Could not get Object DB connection");
        return;
    }

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
    {
        ERROR("Could not prepare statement to get be info  %s", sqlite3_errmsg(dbconn));
        sqlite3_close(dbconn);
        return;
    }

    res = sqlite3_step(stmt);
    while ((res == SQLITE_ROW) && be_cnt < MAX_BACKEND_NUM)
    {
        strcpy_safe(p_total_be->be_info[be_cnt].beName, (char *)sqlite3_column_text(stmt, 0),
                    sizeof(p_total_be->be_info[be_cnt].beName));
        p_total_be->be_info[be_cnt].portNumber =  sqlite3_column_int(stmt, 1);
        strcpy_safe(p_total_be->be_info[be_cnt].initScript, (char *)sqlite3_column_text(stmt, 2),
                    sizeof(p_total_be->be_info[be_cnt].initScript));
        be_cnt++;
        res = sqlite3_step(stmt);
    }

    if (res == SQLITE_DONE)
    {
        p_total_be->be_num = be_cnt;
        DBG("Successfull init of backend info: be_num = %d",  p_total_be->be_num);
        int i = 0;
        for (i = 0; i < p_total_be->be_num; i++)
            DBG("\tbackend %d: %s, port: %d, init script: %s", i, p_total_be->be_info[i].beName,
                p_total_be->be_info[i].portNumber, p_total_be->be_info[i].initScript);
    }
    else //SQL error
    {
        ERROR("Fail to init back-end info: err = %d - %s", res, sqlite3_errmsg(dbconn));
    }

    if (dbconn)
        sqlite3_close(dbconn);
    return;
}

/* Conversion functions for Object dependency class
 *  (string)  to  (enum)
 *  (enum)    to  (string)
 */
static obj_depclass_t objdepclass2enum(const char *db_val)
{
    if (!db_val) return OBJ_DEP_ERROR;
    else if (strlen(db_val) == 0) return OBJ_DEP_ERROR;
    else if (!strcmp(db_val, "autoDelete")) return OBJ_DEP_AUTO_DELETE;
    else if (!strcmp(db_val, "autoCreate")) return OBJ_DEP_AUTO_CREATE;
    else
    {
        DBG ("Bad value of objDepClass: %s (len %d) of Object dependency",
             db_val, strlen(db_val));
        return OBJ_DEP_ERROR;
    }
}

char * objdepclass2string(int objdepclass)
{
    switch (objdepclass)
    {
        case OBJ_DEP_AUTO_DELETE:  return "autoDelete";
        case OBJ_DEP_AUTO_CREATE:  return "autoCreate";
        case OBJ_DEP_ERROR:        return "error";
        default:                   return "unknown";
    }
}

/* ep_init_objdep_info -
   Copies Objects dependencies information from the meta database to the internal
   storage - once on Entry-Point init (memory for needed number of dependencies
   is allocated dynamically - so this memory must be freed once Entry-Point
   finishes its workflow). Objects dependencies info will be used by all
   ep-workers during Entry-Point run-time (information is not changed during EP
   run-time, so we can keep it internally instead of select it from DB each time
   when it is needed)
 */
#define SQL_QUERY_GET_OBJDEP_INFO \
        "SELECT [ParentObjName], [ParentParamName], [ChildObjName], [ChildParamName], \
          [DependencyClass] FROM [MMX_Objects_Deps_InfoTbl]"
#define SQL_QUERY_GET_OBJDEP_NUM \
        "SELECT COUNT(*) FROM [MMX_Objects_Deps_InfoTbl]"
static int ep_init_objdep_info(ep_handle_t *p_ep)
{
    ep_stat_t status = EPS_OK;

    int i, cnt, res = 0;
    char *dep_class = NULL;
    char empty_field[MAX_LEAF_NAME_LEN] = {0};
    char query[196] = SQL_QUERY_GET_OBJDEP_NUM; // max len is len of SQL_QUERY_GET_OBJDEP_INFO
    char db_path[FILENAME_BUF_LEN];
    sqlite3 *dbconn = NULL;
    sqlite3_stmt *stmt = NULL;

    if (sql_getDbConn(&dbconn, get_full_db_path(db_path, sizeof(db_path),
                                                "mmx_meta_db")) != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(EPS_CANNOT_OPEN_DB, "Could not get Object DB connection");
    }

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
    {
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare statement to "
            "get count of Objects dependencies - %s", sqlite3_errmsg(dbconn));
    }

    /* Run SELECT COUNT(*) request for Objects dependencies db table */
    res = sqlite3_step(stmt);
    if (res != SQLITE_ROW) /* TODO Or (res == SQLITE_DONE) is Ok ? */
    {
        /* SQL error or invalid database - exit with error */
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: err = %d - %s",
                            res, sqlite3_errmsg(dbconn));
    }

    if ((obj_dependency_num = sqlite3_column_int(stmt, 0)) == 0)
    {
        WARN("Objects dependencies are not defined in Object DB");
        sqlite3_finalize(stmt);
        sqlite3_close(dbconn);
        return EPS_OK;
    }
    if (stmt) sqlite3_finalize(stmt);

    DBG("%d Objects dependencies are defined in Object DB", obj_dependency_num);

    /* Allocate memory for run-time storage */
    obj_dependency_info = (obj_dependency_info_t *)calloc(obj_dependency_num,
                                                   sizeof(obj_dependency_info_t));
    if (!obj_dependency_info)
    {
        GOTO_RET_WITH_ERROR(EPS_OUTOFMEMORY,
            "Memory allocation failure - Objects dependencies cannot be stored");
    }
    memset(obj_dependency_info, 0, obj_dependency_num * sizeof(obj_dependency_info_t));

    /* ************************************************************************
     * Object dependencies number was defined, memory for run-time storage was
     *  allocated, now run the second query to fetch dependencies information
     * ************************************************************************/

    memset(query, 0, sizeof(query));
    strcpy_safe(query, SQL_QUERY_GET_OBJDEP_INFO, sizeof(query));

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
    {
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare statement to "
            "get Objects dependencies info - %s", sqlite3_errmsg(dbconn));
    }

    /* Go over Object dependencies from DB */
    i = cnt = 0;
    res = sqlite3_step(stmt);
    while ((res == SQLITE_ROW) && i++ < obj_dependency_num)
    {
        /* Check dependency type and save (valid only) to run-time storage */
        dep_class = (char *)sqlite3_column_text(stmt, 4);
        obj_dependency_info->objDepClass = objdepclass2enum(dep_class);
        if (obj_dependency_info->objDepClass == OBJ_DEP_ERROR)
        {
            WARN("Object dependency %d is not saved - invalid type (%s)",
                 i, dep_class);
            continue;
        }

        strcpy_safe(obj_dependency_info->parentObjName,
              (char *)sqlite3_column_text(stmt, 0), sizeof(obj_dependency_info->parentObjName));
        strcpy_safe(obj_dependency_info->childObjName,
              (char *)sqlite3_column_text(stmt, 2), sizeof(obj_dependency_info->childObjName));
        strcpy_safe(obj_dependency_info->parentParamName,
              (char *)sqlite3_column_text(stmt, 1), sizeof(obj_dependency_info->parentParamName));
        strcpy_safe(obj_dependency_info->childParamName,
              (char *)sqlite3_column_text(stmt, 3), sizeof(obj_dependency_info->childParamName));

        memset(empty_field, 0, sizeof(empty_field));
        if (obj_dependency_info->parentObjName == NULL || strlen(obj_dependency_info->parentObjName) == 0)
            strcpy_safe(empty_field, "parentObjName", sizeof(empty_field));
        if (obj_dependency_info->childObjName == NULL || strlen(obj_dependency_info->childObjName) == 0)
            strcpy_safe(empty_field, "childObjName", sizeof(empty_field));
        if (obj_dependency_info->parentParamName == NULL || strlen(obj_dependency_info->parentParamName) == 0)
            strcpy_safe(empty_field, "parentParamName", sizeof(empty_field));
        if (obj_dependency_info->childParamName == NULL || strlen(obj_dependency_info->childParamName) == 0)
            strcpy_safe(empty_field, "childParamName", sizeof(empty_field));

        if (strlen(empty_field) > 0)
        {
            WARN("Object dependency %d is not saved - empty '%s'", i, empty_field);
            memset(obj_dependency_info, 0, sizeof(obj_dependency_info_t));
            continue;
        }

        /* Object dependency was successfully saved to run-time storage */
        DBG("Object dependency [%d] info: %s%s -%s-> %s%s", cnt,
            obj_dependency_info->parentObjName, obj_dependency_info->parentParamName,
            objdepclass2string(obj_dependency_info->objDepClass),
            obj_dependency_info->childObjName, obj_dependency_info->childParamName);

        cnt++; // Increment counter of valid dependencies
        obj_dependency_info++; // Increment pointer inside run-time storage
        res = sqlite3_step(stmt);
    } /* End of while() over DB dependencies */

    if (res != SQLITE_DONE)
    {
        if (res == SQLITE_ROW)
            WARN("Some more Object dependencies (from DB) were not processed");
        else /* SQL error */
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "SELECT Object dependencies query "
                 "finished with failure (Sqlite returned res %d - '%s')", res, sqlite3_errmsg(dbconn));
    }

    DBG("Successfully saved %d Object dependencies to run-time storage.", cnt);
    obj_dependency_num = cnt;

    /* Re-initialize pointer to run-time storage */
    while (cnt--) obj_dependency_info--;

ret:
    if (status != EPS_OK && obj_dependency_info != NULL)
    {
        free(obj_dependency_info);
        obj_dependency_info = NULL;
    }

    if (stmt) sqlite3_finalize(stmt);
    if (dbconn) sqlite3_close(dbconn);

    return status;
}

/* ep_free_objdep_info -
   Free memory allocated on Entry-Point init for the Objects dependencies
   internal storage (allocated by the function ep_init_objdep_info(.))
 */
static void ep_free_objdep_info()
{
    if (obj_dependency_num > 0 && obj_dependency_info != NULL)
    {
        DBG("Cleaning up Objects dependencies run-time storage.");
        free(obj_dependency_info);
    }
}

/* ep_common_get_objdep_info -
   Get number and info of the Objects dependencies in run-time storage
 */
int ep_common_get_objdep_info(obj_dependency_info_t **objdep_info)
{
    if (obj_dependency_num > 0)
    {
        *objdep_info = obj_dependency_info;

        if (*objdep_info == NULL) return -1;
    }

    return obj_dependency_num;
}


/* --------- EP common API functions ------------- */
int ep_common_init()
{
    ep_stat_t status = EPS_OK;

    ep_handle_t *p_ep = &g_ep_handle;

    memset((char*)p_ep, 0, sizeof(ep_handle_t));

    /* Initialize DB path */
    db_path_init(p_ep);

    /* Initialize path for saved config files */
    config_saved_path_init(p_ep);

    /* Initialize tid db */
    tiddb_init(p_ep);

    /* Initialize hold status info */
    ep_hold_info_init(p_ep);

    /* Initialize write status info */
    ep_write_status_init(p_ep);

    /* Initialize back-end info */
    ep_init_backend_info(p_ep);

    /* Initialize Objects dependencies info */
    status = ep_init_objdep_info(p_ep);

    return status;
}

void ep_common_cleanup()
{
    /* Cleanup Objects dependencies info */
    ep_free_objdep_info();
}

/*
 * get_full_db_path
 * Concatenates DB path and the input db_name.
 * Returns full path to given db
 */
char *get_full_db_path(char *buf, size_t buf_len, char *db_name)
{
    strcpy_safe(buf, g_ep_handle.g_db_base_path, buf_len);
    return strcat_safe(buf, db_name, buf_len);
}

char *get_db_run_path(char *buf, size_t buf_len)
{
    memset(buf, 0, buf_len);
    return strcpy_safe(buf, g_ep_handle.g_db_base_path, buf_len);
}

char *get_db_saved_path(char *buf, size_t buf_len)
{
    memset(buf, 0, buf_len);
    return strcpy_safe(buf, g_ep_handle.g_db_saved_path, buf_len);
}

char *get_db_cand_path(char *buf, size_t buf_len)
{
    memset(buf, 0, buf_len);
    return strcpy_safe(buf, g_ep_handle.g_db_cand_path, buf_len);
}

char *get_start_dbtype_filename(char *buf, size_t buf_len)
{
    memset(buf, 0, buf_len);
    return strcpy_safe(buf, g_ep_handle.g_start_dbtype_file, buf_len);
}


char *get_db_path_by_dbtype(char *buf, size_t buf_len, mmx_dbtype_t db_type)
{
    char *dbPath = NULL;

    switch (db_type)
    {
        case MMXDBTYPE_STARTUP:
            dbPath = g_ep_handle.g_db_saved_path;
            break;
        case MMXDBTYPE_CANDIDATE:
            dbPath = g_ep_handle.g_db_cand_path;
            break;
        default:
            dbPath = g_ep_handle.g_db_base_path;
            break;
    }

    memset(buf, 0, buf_len);
    return strcpy_safe(buf, dbPath, buf_len);
}

char *get_config_saved_path(char *buf, size_t buf_len)
{
    memset(buf, 0, buf_len);
    return strcpy_safe(buf, g_ep_handle.g_config_saved_path, buf_len);
}

char *get_config_path(char *buf, size_t buf_len)
{
    memset(buf, 0, buf_len);
    return strcpy_safe(buf, g_ep_handle.g_config_path, buf_len);
}


void tiddb_add(char *name)
{
    int curr_db_num;

    pthread_rwlock_wrlock(&(g_ep_handle.g_tid_db.lock));

    curr_db_num = g_ep_handle.g_tid_db.num;

    g_ep_handle.g_tid_db.tid[curr_db_num] = pthread_self();

    if (name)
        strncpy(g_ep_handle.g_tid_db.tid_s[curr_db_num], name, 3);
    else
    {
        static int worker_thread_id = 1;
        sprintf(g_ep_handle.g_tid_db.tid_s[curr_db_num], "w%02d", worker_thread_id++);
    }

    g_ep_handle.g_tid_db.num++;

    pthread_rwlock_unlock(&(g_ep_handle.g_tid_db.lock));
}

const char *tiddb_get(void)
{
    int i;
    pthread_t pself = pthread_self();

    pthread_rwlock_rdlock(&(g_ep_handle.g_tid_db.lock));

    for (i = 0; i < g_ep_handle.g_tid_db.num; i++)
    {
        if (g_ep_handle.g_tid_db.tid[i] == pself)
        {
            pthread_rwlock_unlock(&(g_ep_handle.g_tid_db.lock));
            return g_ep_handle.g_tid_db.tid_s[i];
        }
    }

    pthread_rwlock_unlock(&(g_ep_handle.g_tid_db.lock));
    return "???";
}

int tiddb_get_w_num(void)
{
    int i;
    pthread_t pself = pthread_self();

    pthread_rwlock_rdlock(&(g_ep_handle.g_tid_db.lock));

    for (i = 0; i < g_ep_handle.g_tid_db.num; i++)
    {
        if (g_ep_handle.g_tid_db.tid[i] == pself)
        {
            pthread_rwlock_unlock(&(g_ep_handle.g_tid_db.lock));
            return i++;
        }
    }

    pthread_rwlock_unlock(&(g_ep_handle.g_tid_db.lock));
    return 0xFF;
}

/* Function returns Entry-point hold status and reason   */
ep_stat_t ep_common_get_hold_status (int *p_hold, int *p_reason)
{
    ep_stat_t status = EPS_OK;
    int res = 0;

    res = pthread_rwlock_tryrdlock(&(g_ep_handle.g_ep_hold_status.lock));
    if (res == 0)
    {
        if (p_hold)
            *p_hold = g_ep_handle.g_ep_hold_status.is_hold;

        if (p_reason)
           *p_reason =  g_ep_handle.g_ep_hold_status.hold_reason;

        res = pthread_rwlock_unlock(&(g_ep_handle.g_ep_hold_status.lock));
        DBG("EP hold status = %d, reason %d, last res = %d", *p_hold, *p_reason, res);
    }
    else
    {
        ERROR("!!! Failed to get lock to read EP hold status (%d)", res);
        status = EPS_SYSTEM_ERROR;
    }

    return status;
}

ep_stat_t ep_common_set_hold_status(int reason, unsigned interval)
{
    ep_stat_t status = EPS_OK;
    int res = 0;
    ep_hold_status_t *p_hold_status = &g_ep_handle.g_ep_hold_status;

    pthread_rwlock_wrlock(&(p_hold_status->lock));

        if (!p_hold_status->is_hold)
        {
            p_hold_status->is_hold     = TRUE;
            p_hold_status->hold_reason = reason;
            p_hold_status-> start_time = get_uptime();
            p_hold_status->interval    = interval;
            p_hold_status->thread_id   = pthread_self();
            strcpy_safe(p_hold_status->thread_name, tiddb_get(), sizeof(p_hold_status->thread_name));
            DBG("EP set on hold for %d secs, reason %d (res %d)", interval,reason, res);
        }
        else
        {
            status = EPS_EP_HOLD;
            DBG("Cannot set EP to HOLD status, it'is already hold by %s thread with reason %d",
                    p_hold_status->thread_name, p_hold_status->hold_reason);
        }

    res = pthread_rwlock_unlock(&(p_hold_status->lock));

    return status;
}

/* TODO is not currently used */
ep_stat_t ep_common_set_unhold_status( )
{
    int res = 0;
    ep_hold_status_t *p_hold_status = &g_ep_handle.g_ep_hold_status;
    pthread_t calling_pth = pthread_self();

    pthread_rwlock_wrlock(&(p_hold_status->lock));

        if (!p_hold_status->is_hold)
        {
            DBG("EP is already unhold. Nothing to do");
        }
        else
        {
            if (calling_pth ==  p_hold_status->thread_id)
            {
                DBG("EP is set to unhold (normal run mode) by %s thread", p_hold_status->thread_name);
                memset((char *)p_hold_status + sizeof(pthread_rwlock_t), 0,
                        sizeof(ep_hold_status_t) - sizeof(pthread_rwlock_t));
            }
        }

    res = pthread_rwlock_unlock(&(p_hold_status->lock));

    DBG("EP unhold processing completed res %d)", res);

    return EPS_OK;
}


/* Function returns Entry-point hold status and reason
   Also checks hold interval in case of EP is in HOLD status
   If hold time is over, unhold EP.                        */
ep_stat_t ep_common_check_hold_status (int *p_hold, int *p_reason)
{
    ep_stat_t status = EPS_OK;
    long       start = 0;
    unsigned   interval = 0;
    ep_hold_status_t *p_hold_status = &g_ep_handle.g_ep_hold_status;
    int res = 0;

    if (p_hold)   *p_hold = FALSE;
    if (p_reason) *p_reason = 0;

    res = pthread_rwlock_rdlock(&(p_hold_status->lock));
    if (res == 0)
    {
        /*DBG("EP hold status lock is get; Current status %d, reason %d",
             p_hold_status->is_hold, p_hold_status->hold_reason); */

        if (p_hold_status->is_hold == TRUE)
        {

            if (p_hold) *p_hold     = TRUE;
            if (p_reason) *p_reason = p_hold_status->hold_reason;
            start                  = p_hold_status->start_time;
            interval               = p_hold_status->interval;
            DBG("EP is on hold status: reason %d, thread %s, start %d, interval %d",
                *p_reason, p_hold_status->thread_name,p_hold_status->start_time,
                 p_hold_status->interval);
       }
       res = pthread_rwlock_unlock(&(p_hold_status->lock));
    }
    else
    {
        ERROR("!!! Failed to get lock to read EP hold status (%d)", res);
        return EPS_SYSTEM_ERROR;
    }

    if (*p_hold == FALSE)
        return EPS_OK;

    /* EP is on hold, check if the hold time interval is over.
       If yes - we need to unhold EP.                         */
    if (get_uptime() >= start + interval)
    {
        pthread_rwlock_wrlock(&(p_hold_status->lock));
            memset((char *)p_hold_status + sizeof(pthread_rwlock_t), 0,
                   sizeof(ep_hold_status_t) - sizeof(pthread_rwlock_t));
        res = pthread_rwlock_unlock(&(p_hold_status->lock));

        DBG("EP is set to normal run mode after %d sec of holding (reason %d, res %d)",
             interval, *p_reason, res);

        if (p_hold)   *p_hold = FALSE;
        if (p_reason) *p_reason = 0;
    }

    return status;
}

/*  API functions for write-operation lock   */
/*
  All "write" requests should  call
     func ep_common_get_write_lock before update the MMX DB and the backends,
     func ep_common_finalize_write_lock at the end of the work.

   Function ep_common_get_write_lock waits on the conditional variable "write_cv".
   Function ep_common_finalize_write_lock calls pthread_cond_signal to release
    one of the worker threads blocking (waiting) this conditional variable.

   Wait time is set to 30 sec just in case. If a worker thread performing write
   operation cannot complete its task during this time, it means there is
   something bad occurs in the system. So waiting threads will not get
   write lock.
    */
#define  EP_WRITE_OP_WAIT_SEC  30
#define  EP_WRITE_OP_EXT_WAIT_SEC 2
ep_stat_t ep_common_get_write_lock(int reqType, int txaId, int callerId)
{
    ep_stat_t status = EPS_OK;
    int       res = 0;
    int       wait_secs = 1;
    ep_write_status_t *p_write_status = &g_ep_handle.g_ep_write_status;
    struct timespec ts;
    struct timeval  tv;

    /* Wait for EP write-operation flag is freed or timeout is expired*/
    res = pthread_mutex_lock(&p_write_status->write_mutex);
    if (res != 0)
    {
        ERROR("Failure in pthread_mutex_lock: res = %d - %s", res, strerror(errno));
        return EPS_SYSTEM_ERROR;
    }

    if (p_write_status->writing_is_active == TRUE)
    {
        p_write_status->waiting_thread_cnt++;

        wait_secs = EP_WRITE_OP_WAIT_SEC +
                    (p_write_status->waiting_thread_cnt * EP_WRITE_OP_EXT_WAIT_SEC);
        gettimeofday(&tv, NULL);
        ts.tv_sec  = tv.tv_sec + wait_secs;
        ts.tv_nsec = tv.tv_usec * 1000;

        /*DBG("blocking wait on pthread_cond_timedwait, num of waiting threads %d",
              p_write_status->waiting_thread_cnt); */

        res = pthread_cond_timedwait(&p_write_status->write_cv, &p_write_status->write_mutex, &ts);

        p_write_status->waiting_thread_cnt--;
        DBG("pthread_cond_timedwait result is %d, num of waiting threads %d",
                                    res, p_write_status->waiting_thread_cnt);
    }

    if (res == 0)
    {
        if (p_write_status->writing_is_active == FALSE)
        {
            /* Set write-flag to true and save the requestor info */
            p_write_status->writing_is_active = TRUE;

            p_write_status->reqType    = reqType;
            p_write_status->reqTxaId   = txaId;
            p_write_status->callerId   = callerId;
            p_write_status->start_time = get_uptime();
            p_write_status->thread_id  = pthread_self();
            strcpy_safe(p_write_status->thread_name, tiddb_get(), sizeof(p_write_status->thread_name));
            DBG("EP write status is get for req %s, txaId %d, callerId %d",
                 msgtype2str(reqType),txaId, callerId);
            status = EPS_OK;
        }
        else
        {
            status = EPS_RESOURCE_NOT_FREE;
            ERROR ("EP write_is_active flag was not released correctly after previous use");
        }
    }
    else if (res == ETIMEDOUT)
    {
        status = EPS_RESOURCE_NOT_FREE;
        ERROR ("Timeout on get EP write lock (used by thread %s for req %s, txaid %d, caller %d)",
               p_write_status->thread_name, msgtype2str(p_write_status->reqType),
               p_write_status->reqTxaId, p_write_status->callerId);
    }
    else  //Other system error
    {
        status =  EPS_SYSTEM_ERROR;
        ERROR ("Failure on pthread_cond_timedwait - %d", res);
    }

    res = pthread_mutex_unlock(&p_write_status->write_mutex);
    if (res != 0)  //It is almost impossible case, but just in case we check it
    {
        ERROR("Failure in pthread_mutex_unlock!!! res = %d - %s", res, strerror(errno));
    }

    return status;
}

ep_stat_t ep_common_finalize_write_lock(int txaId, int callerId, BOOL force)
{
    ep_stat_t status = EPS_OK;
    int res = 0;
    ep_write_status_t *p_write_status = &g_ep_handle.g_ep_write_status;

    if(p_write_status->writing_is_active == FALSE)
    {
        //Do nothing
        return EPS_OK;
    }

    pthread_mutex_lock(&p_write_status->write_mutex);

    if ((p_write_status->reqTxaId == txaId) &&
        (p_write_status->callerId == callerId) &&
        (p_write_status->thread_id == pthread_self()))
    {
        p_write_status->writing_is_active = FALSE;
        DBG("EP write lock is freed (req %s, txaId %d from caller %d)",
             msgtype2str(p_write_status->reqType), p_write_status->reqTxaId, p_write_status->callerId);
        p_write_status->reqTxaId  = 0;
        p_write_status->reqType   = 0;
        p_write_status->callerId  = 0;
        p_write_status->start_time = 0;
        p_write_status->thread_id = 0;
        memset(p_write_status->thread_name, 0, sizeof(p_write_status->thread_name));
    }
    else if (force == TRUE)
    {
        p_write_status->writing_is_active = FALSE;
        DBG("EP write lock was forced released by thread %lu (instead of %lu)",
             pthread_self(), p_write_status->thread_id);
    }
    else
    {
        ERROR("Attempt to free EP write lock of not own thread %lu, for req %d",
               p_write_status->thread_id, p_write_status->reqType);
        status = EPS_GENERAL_ERROR;
    }

    /*Send signal to unblock one of waiting threads that are blocked on EP write cond variable*/
    res = pthread_cond_signal(&(p_write_status->write_cv));
    if (res != 0)
        DBG("Bad rescode received from pthread_cond_signal - %d - %s", res, strerror(errno));

    pthread_mutex_unlock(&p_write_status->write_mutex);
    if (res != 0)  //It is almost impossible case, but just in case we check it
    {
        ERROR("Failure in pthread_mutex_unlock!!! res = %d - %s", res, strerror(errno));
    }

    return status;
}


int ep_common_get_beinfo_index(char *beName)
{
    int i = 0, resIdx = -1;
    ep_all_backends_t *p_be_info = &g_ep_handle.g_total_backend_info;

    if ((beName != NULL) && (strlen(beName) > 0))
    {
        for (i = 0; i < p_be_info->be_num; i++)
        {
            if (!strcmp((const char*)beName, (const char *)p_be_info->be_info[i].beName))
            {
                resIdx = i;
                break;
            }
        }
    }

    return resIdx;
}

ep_stat_t ep_common_get_beinfo_by_index (int beIdx, backend_info_t *beInfo)
{
    ep_stat_t status = EPS_OK;
    ep_all_backends_t *p_be_info = &g_ep_handle.g_total_backend_info;

    if ((beIdx < 0) || (beIdx >= p_be_info->be_num) || (beIdx >= MAX_BACKEND_NUM))
    {
        //DBG("Incorrect BE index %d - max value is %d", beIdx, p_be_info->be_num);
        status = EPS_INVALID_ARGUMENT;
    }
    else
        memcpy((char *)beInfo, (char *)&(p_be_info->be_info[beIdx]), sizeof(backend_info_t));

    return status;

}

ep_stat_t ep_common_get_beinfo_by_bename (char * beName, backend_info_t *beInfo)
{
    ep_stat_t status = EPS_OK;
    int beIdx = -1;
    ep_all_backends_t *p_be_info = &g_ep_handle.g_total_backend_info;

    beIdx = ep_common_get_beinfo_index(beName);

    if ((beIdx < 0) || (beIdx >= p_be_info->be_num) || (beIdx >= MAX_BACKEND_NUM))
    {
        //DBG("Incorrect BE index %d - max value is %d", beIdx, p_be_info->be_num);
        status = EPS_NOT_FOUND;
    }
    else
        memcpy((char *)beInfo, (char *)&(p_be_info->be_info[beIdx]), sizeof(backend_info_t));

    return status;
}
