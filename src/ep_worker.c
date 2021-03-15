/* ep_worker.c
 *
 * Copyright (c) 2013-2021 Inango Systems LTD.
 *
 * Author: Inango Systems LTD. <support@inango-systems.com>
 * Creation Date: Jun 2013
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

#include <sqlite3.h>
#include <stddef.h>

#include <mmx-frontapi.h>
#include <mmx-backapi.h>

#include "ep_threadpool.h"
#include "ep_common.h"
#include "ep_db_utils.h"

#include "ep_worker.h"

#define MNG_MODEL_TOP_NAME     "Device."
#define MMX_OWN_OBJ_NAME       "Device.X_Inango_MMXSettings."

/* Names of "service" columns in the MMX main DB tables  */
#define MMX_SELFREF_DBCOLNAME  "ObjInstSelfRef"
#define MMX_CFGOWNER_DBCOLNAME "CfgOwner"
#define MMX_CREATEOWNER_DBCOLNAME "CreateOwner"


#define SQL_QUERY_GET_OBJ_INFO  "SELECT DISTINCT \
    ObjName, ObjInternalId, PackageName, InfoTblName, ValuesDbName, \
    ValuesTblName, BackEndName, Writable, UserAccessPerm, ReadFrontEnds, WriteFrontEnds, \
    MinEntNumber, MaxEntNumber, NumOfEntParamName, EnableParamName, UniKeyParamNames, \
    StyleOfAddObj, AddObjMethod, StyleOfDelObj, DelObjMethod, StyleOfGet, \
    GetMethod, StyleOfSet, SetMethod, StyleOfGetAll, GetAllMethod, Configurable \
    FROM [MMX_Objects_InfoTbl] WHERE [ObjName] = ?"

#define SQL_QUERY_GET_OBJ_LIST_INFO  "SELECT \
    ObjName, ObjInternalId, PackageName, InfoTblName, ValuesDbName, \
    ValuesTblName, BackEndName, Writable, UserAccessPerm, ReadFrontEnds, WriteFrontEnds, \
    MinEntNumber, MaxEntNumber, NumOfEntParamName, EnableParamName, UniKeyParamNames, \
    StyleOfAddObj, AddObjMethod, StyleOfDelObj, DelObjMethod, StyleOfGet, \
    GetMethod, StyleOfSet, SetMethod, StyleOfGetAll, GetAllMethod, Configurable \
    FROM [MMX_Objects_InfoTbl] WHERE [ObjName] LIKE ?"

/*
 * --- SQL queries for getting Object info when processing DiscoverConfig ---
 *     6 columns are fetched (column position in query is fixed):
 *        ObjName, Configurable, StyleOfGetAll,
 *        GetAllMethod, AugmentObjects, backendName
 */

/* DiscoverConfig with both specified Backend name and Object Name */
#define SQL_QUERY_GET_OBJECT_BY_BACKEND "SELECT [ObjName], \
    [Configurable], [StyleOfGetAll], [GetAllMethod], [AugmentObjects], [backendName] \
    FROM [MMX_Objects_InfoTbl] WHERE [BackendName] = ?1 AND [ObjName] = ?2"

/* DiscoverConfig with only specified Backend name */
#define SQL_QUERY_GET_OBJECTS_BY_BACKEND_BY_INIT_ORDER "SELECT [ObjName], \
    [Configurable], [StyleOfGetAll], [GetAllMethod], [AugmentObjects], [backendName] \
    FROM [MMX_Objects_InfoTbl] WHERE [BackendName] = ? ORDER BY [ObjInitOrder] ASC"

/* DiscoverConfig with only specified Object Name */
#define SQL_QUERY_GET_OBJECT "SELECT [ObjName], [Configurable], \
    [StyleOfGetAll], [GetAllMethod], [AugmentObjects], [backendName] \
    FROM [MMX_Objects_InfoTbl] WHERE [ObjName] = ?"

/* DiscoverConfig without specified params - system-wide DiscoverConfig (all Objects) */
#define SQL_QUERY_GET_OBJECTS_BY_INIT_ORDER "SELECT [ObjName], [Configurable], \
    [StyleOfGetAll], [GetAllMethod], [AugmentObjects], [backendName] \
    FROM [MMX_Objects_InfoTbl] ORDER BY [ObjInitOrder] ASC"

/* // Old (currently not used) SQL queries for getting Backend info

#define SQL_QUERY_GET_BACKEND_INFO  "SELECT [mgmtPort], [initScript] FROM [mmx_backendinfo] \
    WHERE [backendName] = ? LIMIT 1"
#define SQL_QUERY_GET_BACKEND_LIST  "SELECT [backendName] FROM [mmx_backendinfo]"
*/


extern ep_stat_t disp_set_restart_flag(int restartType);

/* Forward declaration of some static functions  */
static ep_stat_t w_handle_discover_config(worker_data_t *wd,
                                          ep_message_t *message,
                                          BOOL externalReq);

static ep_stat_t w_init_mmxdb_handles(worker_data_t *wd, int dbType, int msgType);

/* -----------------------------------------------------------------------*
 * ------------------ Common helper functions ----------------------------*
 * -----------------------------------------------------------------------*/
static ep_stat_t parse_param_name(char *rawstr, parsed_param_name_t *pn)
{
    path_token_type_t token_type = PATH_TOKEN_UNDEF;
    char buf[MSG_MAX_STR_LEN];
    BOOL with_placeholders = (strstr(rawstr, ".{i}.") != NULL) ? TRUE : FALSE;

    memset(pn, 0, sizeof(parsed_param_name_t));
    memset(buf, 0, sizeof(buf));

    DBG(" raw name string: %s %s", rawstr,
        (with_placeholders) ? "(placeholder name)" : "");

    /* Copy rawstr to buf so it does not get spoiled by strtok */
    strcpy_safe(buf, rawstr, sizeof(buf));
    if (with_placeholders)
    {
        strcpy_safe(pn->obj_name, rawstr, MSG_MAX_STR_LEN);
    }


    if (LAST_CHAR(buf) == '.')
    {
        pn->partial_path = TRUE;
    }

    char *token, *next_token, *strtok_ctx, *strtok_ctx2;

    token = strtok_r(buf, ".", &strtok_ctx);
    while (token)
    {
        next_token = strtok_r(NULL, ".", &strtok_ctx);

        if (!with_placeholders)
        {
            if (isdigit(token[0]))
            {
                pn->indices[pn->index_num].type = REQ_IDX_TYPE_EXACT;
                pn->indices[pn->index_num].exact_val.num = atoi(token);
                pn->index_set_num++;
                pn->index_num++;
                strcat_safe(pn->obj_name, "{i}.", sizeof(pn->obj_name));
                token_type = PATH_TOKEN_INDEX;
            }
            else if (token[0] == '[')
            {
                pn->indices[pn->index_num].type = REQ_IDX_TYPE_RANGE;
                pn->indices[pn->index_num].range_val.begin = atoi(strtok_r(token+1, "-", &strtok_ctx2));
                pn->indices[pn->index_num].range_val.end = atoi(strtok_r(NULL, "-", &strtok_ctx2));
                pn->index_num++;
                pn->index_set_num++;
                strcat_safe(pn->obj_name, "{i}.", sizeof(pn->obj_name));
                token_type = PATH_TOKEN_INDEX;
            }
            else if (token[0] == '*')
            {
                pn->indices[pn->index_num++].type = REQ_IDX_TYPE_ALL;
                strcat_safe(pn->obj_name, "{i}.", sizeof(pn->obj_name));
                token_type = PATH_TOKEN_INDEX;
            }
            else if (isalpha(token[0]))
            {
                if (!next_token && !pn->partial_path)
                {
                    strncpy(pn->leaf_name, token, sizeof(pn->leaf_name)-1);
                    token_type = PATH_TOKEN_PARAMNAME;
                    break;
                }

                strcat_safe(pn->obj_name, token, sizeof(pn->obj_name));
                strcat_safe(pn->obj_name, ".", sizeof(pn->obj_name));
                token_type = PATH_TOKEN_NODE;
            }
            else
            {
                DBG(" Incorrect parameter name: %s", rawstr);
                return EPS_INVALID_ARGUMENT;
            }
        }
        else /* with_placeholders = TRUE */
        {
            if (!strcmp(token, "{i}"))
            {
                pn->indices[pn->index_num].type = REQ_IDX_TYPE_PLACEHOLDER;
                pn->index_num++;
                token_type = PATH_TOKEN_PLACEHOLDER;
            }
            else if (isalpha(token[0]))
            {
                if (!next_token && !pn->partial_path)
                {
                    strncpy(pn->leaf_name, token, sizeof(pn->leaf_name)-1);
                    token_type = PATH_TOKEN_PARAMNAME;
                    break;
                }
                token_type = PATH_TOKEN_NODE;
            }
            else
            {
                DBG(" Incorrect parameter placeholder name: %s", rawstr);
                return EPS_INVALID_ARGUMENT;
            }
        }

        token = next_token;
    }

    pn->last_token_type = token_type;
    return EPS_OK;
}

/*  If the name does not contain the "." it is leaf name,
    othewise there is full name including object name
    For example,  Device.Bridging.Bridge.2.Name is not leaf name
                  Name - is leaf name*/
static BOOL isLeafName(const char *name)
{
    int i = 0;
    int len = strlen(name);

    for (i = 0; i < len; i++)
    {
        if (name[i] == '.')
            return FALSE;
    }

    return TRUE;
}

static char *getLeafParamName(char *name)
{
    char buf[MSG_MAX_STR_LEN];
    char *token, *next_token, *strtok_ctx;
    char *leaf = NULL;

    memset(buf, 0, sizeof(buf));

    if (strlen(name) == 0)
    {
        WARN("Name of parameter is empty");
        return NULL;
    }

    //DBG(" raw name string: %s", name);
    strcpy_safe(buf, name, sizeof(buf));

    if (LAST_CHAR(buf) == '.')
    {
        DBG("Name %s does not have leaf", name);
        return NULL;
    }

    token = strtok_r(buf, ".", &strtok_ctx);
    while (token)
    {
        next_token = strtok_r(NULL, ".", &strtok_ctx);

        if (next_token)
            token = next_token;
        else
           break;
    }

    if (token)
    {
        leaf = strstr(name, token);
        //DBG("Name %s has leaf %s", name, leaf);
    }

    return leaf;
}

static oper_style_t operstyle2enum(const char *db_val, char *name, char *oper)
{
    if (!db_val) return OP_STYLE_NOT_DEF;
    else if (strlen(db_val) == 0) return OP_STYLE_NOT_DEF;
    else if (!strcmp(db_val, "script")) return OP_STYLE_SCRIPT;
    else if (!strcmp(db_val, "shell-script")) return OP_STYLE_SHELL_SCRIPT;
    else if (!strcmp(db_val, "uci")) return OP_STYLE_UCI;
    else if (!strcmp(db_val, "ubus")) return OP_STYLE_UBUS;
    else if (!strcmp(db_val, "backend")) return OP_STYLE_BACKEND;
    else if (!strcmp(db_val, "db")) return OP_STYLE_DB;
    else
    {
        DBG ("Bad value of operstyle: %s (len %d) of operation %s of obj or param %s",
             db_val, strlen(db_val), oper, name);
        return OP_STYLE_ERROR;
    }
}

static char * operstyle2string(int style)
{
    switch (style)
    {
        case OP_STYLE_SCRIPT: return "script";
        case OP_STYLE_SHELL_SCRIPT: return "shell-script";
        case OP_STYLE_UCI: return "uci";
        case OP_STYLE_UBUS: return "ubus";
        case OP_STYLE_BACKEND: return "backend";
        case OP_STYLE_DB: return "db";
        default:          return "unknown";
    }
}

static char * methodType2string( int methodType)
{
    switch (methodType)
    {
        case OP_GET:     return "GET";
        case OP_SET:     return "SET";
        case OP_ADDOBJ:  return "ADDOBJ";
        case OP_DELOBJ:  return "DELOBJ";
        case OP_GETALL:  return "GETALL";
        case OP_ERROR:   return "ERROR";
        default:         return "unknown";
    }
}

const char *soap2db(const char *value, const char *type)
{
    if (!strcmp(type, "boolean"))
    {
        if (value)
        {
            if (!strcmp(value, "true") || !strcmp(value, "1"))
                return "1";
            else
                return "0";
        }
        else
           return "0";
    }
    else // not boolean param type - do nothing
        return value;
}

const char *db2soap(const char *value, const char *type)
{
    if (!strcmp(type, "boolean"))
    {
        if (value)
        {
            if (!strcmp(value, "1"))
                return "true";
            else if (!strcmp(value, "0"))
                return "false";
            else
                return value;
        }
        else
           return "false";
   }
   else // not boolean param type - do nothing
        return value;
}


/* Small helper functions that determine if parameter or object allowed to be
    read or write by the specified caller                                    */
static inline BOOL paramReadAllowed(const param_info_t *param_info, const int j, const int callerId)
{
    return (!param_info[j].readFrontEnds || (param_info[j].readFrontEnds & callerId));
}

static inline BOOL paramWriteAllowed(const param_info_t *param_info, const int j, const int callerId)
{
    return (!param_info[j].writeFrontEnds || (param_info[j].writeFrontEnds & callerId));
}

static inline BOOL objReadAllowed(const obj_info_t *obj_info, const int callerId)
{
    return (!obj_info->readFrontEnds || (obj_info->readFrontEnds & callerId));
}

static inline BOOL objWriteAllowed(const obj_info_t *obj_info, const int callerId)
{
    return (!obj_info->writeFrontEnds || (obj_info->writeFrontEnds & callerId));
}


/* Check if operation is allowed for the specified DB type
   (running, startup, candidate) */
static BOOL operAllowedForDbType(int methodType, mmx_dbtype_t dbType)
{
    BOOL res = FALSE;

    switch (methodType)
    {
        case OP_GET:
            res = TRUE;
            break;
        case OP_GETALL:
            if (dbType == MMXDBTYPE_RUNNING)
                res = TRUE;
            break;
        case OP_SET:
        case OP_ADDOBJ:
        case OP_DELOBJ:
           if ((dbType == MMXDBTYPE_RUNNING) || (dbType == MMXDBTYPE_CANDIDATE))
                res = TRUE;
            break;
    }

    return res;
}

static ep_stat_t fill_obj_info(sqlite3_stmt *stmt, obj_info_t *obj_info)
{
    memset(obj_info, 0, sizeof(obj_info_t));

    strcpy_safe(obj_info->objName, (char *)sqlite3_column_text(stmt, 0), sizeof(obj_info->objName));
    //DBG("Object name: %s", obj_info->objName);

    strcpy_safe(obj_info->objInfoTblName, (char *)sqlite3_column_text(stmt, 3), sizeof(obj_info->objInfoTblName));
    strcpy_safe(obj_info->objValuesDbName, (char *)sqlite3_column_text(stmt, 4), sizeof(obj_info->objValuesDbName));
    strcpy_safe(obj_info->objValuesTblName, (char *)sqlite3_column_text(stmt, 5), sizeof(obj_info->objValuesTblName));
    strcpy_safe(obj_info->backEndName, (char *)sqlite3_column_text(stmt, 6), sizeof(obj_info->backEndName));
    obj_info->writable = (BOOL)sqlite3_column_int(stmt, 7);

    obj_info->readFrontEnds = sqlite3_column_int(stmt, 9);
    obj_info->writeFrontEnds = sqlite3_column_int(stmt, 10);

    obj_info->addObjStyle = operstyle2enum((char *)sqlite3_column_text(stmt, 16),
                                            obj_info->objName, "addObj");
    strcpy_safe(obj_info->addObjMethod, (char *)sqlite3_column_text(stmt, 17), sizeof(obj_info->addObjMethod));
    obj_info->delObjStyle = operstyle2enum((char *)sqlite3_column_text(stmt, 18),
                                            obj_info->objName, "delObj");
    strcpy_safe(obj_info->delObjMethod, (char *)sqlite3_column_text(stmt, 19), sizeof(obj_info->delObjMethod));
    obj_info->getOperStyle = operstyle2enum((char *)sqlite3_column_text(stmt, 20),
                                            obj_info->objName, "getParamValue");
    strcpy_safe(obj_info->getMethod, (char *)sqlite3_column_text(stmt, 21), sizeof(obj_info->getMethod));
    obj_info->setOperStyle = operstyle2enum((char *)sqlite3_column_text(stmt, 22),
                                            obj_info->objName, "setParamValue");
    strcpy_safe(obj_info->setMethod, (char *)sqlite3_column_text(stmt, 23), sizeof(obj_info->setMethod));
    obj_info->getAllOperStyle = operstyle2enum((char *)sqlite3_column_text(stmt, 24),
                                            obj_info->objName, "getAll");
    strcpy_safe(obj_info->getAllMethod, (char *)sqlite3_column_text(stmt, 25), sizeof(obj_info->getAllMethod));

    obj_info->configurable = (BOOL)sqlite3_column_int(stmt, 26);

    /* Unused Object's meta properties */

    //obj_info->objInternalId = sqlite3_column_int(stmt, 1);
    //strcpy_safe(obj_info->packageName, (char *)sqlite3_column_text(stmt, 2), sizeof(obj_info->packageName));
    //obj_info->userAccessPerm = (access_perm_t)sqlite3_column_int(stmt, 8);
    //obj_info->minEntNumber = sqlite3_column_int(stmt, 11);
    //obj_info->maxEntNumber = sqlite3_column_int(stmt, 12);
    //strcpy_safe(obj_info->numOfEntParamName, (char *)sqlite3_column_text(stmt, 13), sizeof(obj_info->numOfEntParamName));
    //strcpy_safe(obj_info->enableParamName, (char *)sqlite3_column_text(stmt, 14), sizeof(obj_info->enableParamName));
    //strcpy_safe(obj_info->uniqueKeyParamNames, (char *)sqlite3_column_text(stmt, 15), sizeof(obj_info->uniqueKeyParamNames));

    return EPS_OK;
}

static ep_stat_t fill_param_info(sqlite3_stmt *stmt, param_info_t *param_info,
                                 char configOnly)
{
    BOOL writable = FALSE, isIndex = FALSE, notSavedInDb = FALSE;

    writable = (BOOL)sqlite3_column_int(stmt, 1);
    isIndex  = (BOOL)sqlite3_column_int(stmt, 6);
    notSavedInDb = (BOOL)sqlite3_column_int(stmt, 14);

    DBG("Param name: %s, writable: %d, isIndex: %d", param_info->paramName, writable, isIndex);
    /* If only configuration parameters are needed,
       skip read-only parameters (indexes are always needed) */
    if ( configOnly )
    {
       if (!writable && !isIndex)
          return EPS_NOTHING_DONE;

       if (notSavedInDb)
          return EPS_NOTHING_DONE;
    }

    memset(param_info, 0, sizeof(param_info_t));
    strcpy_safe(param_info->paramName, (char *)sqlite3_column_text(stmt, 0), sizeof(param_info->paramName));
    //DBG("Param name: %s", param_info->paramName);
    param_info->writable = (BOOL)sqlite3_column_int(stmt, 1);

    param_info->readFrontEnds = sqlite3_column_int(stmt, 3);
    param_info->writeFrontEnds = sqlite3_column_int(stmt, 4);
    strcpy_safe(param_info->paramType, (char *)sqlite3_column_text(stmt, 5), sizeof(param_info->paramType));
    param_info->isIndex = (BOOL)sqlite3_column_int(stmt, 6);

    param_info->hidden = (BOOL)sqlite3_column_int(stmt, 13);
    param_info->notSaveInDb = (BOOL)sqlite3_column_int(stmt, 14);

    param_info->getOperStyle = operstyle2enum((char *)sqlite3_column_text(stmt, 17),
                                            param_info->paramName, "getParamValue");
    param_info->setOperStyle = operstyle2enum((char *)sqlite3_column_text(stmt, 18),
                                            param_info->paramName, "setParamValue");

    strcpy_safe(param_info->getMethod, (char *)sqlite3_column_text(stmt, 19), sizeof(param_info->getMethod));
    strcpy_safe(param_info->setMethod, (char *)sqlite3_column_text(stmt, 20), sizeof(param_info->setMethod));

    strcpy_safe(param_info->defValue, (char *)sqlite3_column_text(stmt, 10), sizeof(param_info->defValue));

    param_info->hasDefValue = (sqlite3_column_type(stmt, 10) != SQLITE_NULL) ? TRUE : FALSE;

    /* Unused Object Parameter's meta properties */

    //param_info->userAccessPerm = (access_perm_t)sqlite3_column_int(stmt, 2);
    //param_info->valueIsList = (BOOL)sqlite3_column_int(stmt, 7);
    //param_info->minValue = sqlite3_column_int(stmt, 8);
    //param_info->maxValue = sqlite3_column_int(stmt, 9);
    //param_info->minLength = sqlite3_column_int(stmt, 11);
    //param_info->maxLength = sqlite3_column_int(stmt, 12);
    //strcpy_safe(param_info->units, (char *)sqlite3_column_text(stmt, 15), sizeof(param_info->units));
    //strcpy_safe(param_info->enumValues, (char *)sqlite3_column_text(stmt, 16), sizeof(param_info->enumValues));

    return EPS_OK;
}

/* Helper function for calculating number of indeces of the specified object
   Number of indeces is number of {i} placeholders in the object's name */
static int w_num_of_obj_indeces(const char *obj_name)
{
    int i = 0;
    char *p_ph;

    if (obj_name)
    {
        p_ph = strstr(obj_name, INDEX_PLACEHOLDER);

        while (p_ph)
        {
            i++;
            /* Advance pointers */
            obj_name = p_ph + strlen(INDEX_PLACEHOLDER);
            p_ph = strstr(obj_name, INDEX_PLACEHOLDER);
        }
    }
    return i;
}

/* Function returns names of all index parameters of the object and
 * number of indeces  */
static ep_stat_t get_index_param_names(param_info_t *param_info, int param_num,
                                       char *idx_params[], int *idx_params_num)
{
    int i, cnt = 0;

    *idx_params_num = 0;
    for (i = 0; i < param_num; i++)
    {
        if (param_info[i].isIndex)
            idx_params[cnt++] = param_info[i].paramName;
    }
    *idx_params_num = cnt;

    return EPS_OK;
}
/*  The function searches the specified param_name into param_info array.
 *  In case of success the output parameter p_idx will contain
 *  index of the appropriated element in param_info array
 */
static ep_stat_t w_check_param_name(param_info_t param_info[], int param_num,
                                    char *param_name, int *p_idx)
{
    int       i = -1;
    BOOL      found = FALSE;
    ep_stat_t status = EPS_NOT_FOUND;

    if (p_idx)
        *p_idx = -1;

    while (!found && (++i < param_num))
    {
        if (!strcmp(param_name, param_info[i].paramName))
        {
            found = TRUE;
            status = EPS_OK;
            if (p_idx)
                *p_idx = i;
        }
    }

    return status;
}

/* Helper function allowing to determine the "previous" multi-instanse object
 * for the specified object.
   For example:
     for object  Device.WiFi.AccessPoint.{i}.AssociatedDevice.{i}.
     the previous multi-instance object is  Device.WiFi.AccessPoint.{i}. */
static ep_stat_t w_get_prev_multi_obj_name(const char *obj_name, char* prevobj_name, int *prevobj_idx_num)
{
    int i = 0, max_idx = 0, len = 0;
    char *p_ph, *p_curr_offset;

    max_idx = w_num_of_obj_indeces(obj_name);

    if (max_idx <= 1)
    {
        if (prevobj_idx_num)
            *prevobj_idx_num = 0;

        DBG("Object %s has no previous multi-instance obj", obj_name);
        return EPS_EMPTY;
    }

    p_curr_offset = (char *)obj_name;
    for (i = 1; i < max_idx; i++)
    {
        p_ph = strstr(p_curr_offset, INDEX_PLACEHOLDER);
        p_curr_offset = p_ph + strlen(INDEX_PLACEHOLDER);
    }

    if (prevobj_name)
    {
        len = p_curr_offset - obj_name;
        memcpy(prevobj_name, obj_name, len);
        memcpy(prevobj_name +len, ".\0", 1);

        DBG("Previous multi-instance obj for %s is %s (len = %d, num of idx = %d)",
            obj_name, prevobj_name, strlen(prevobj_name), max_idx - 1);
    }
    if (prevobj_idx_num)
        *prevobj_idx_num = max_idx - 1;

    return EPS_OK;
}

/* Get information about objects matched by requested parameter name */
/*
 *  pn - parsed parameter name
 *  obj_info - array containing filled obj_info_t struct
 *             This array is allocated by the calling procedure
 *  obj_info_size - number of elements in obj_info array
 *  obj_num - resulting number of objects retrieved by this function
 *  nextLevelOnly - if true the result array will contain just specified object
 *                  (that matches the requested parameter name)
 *                  if false then the result array will include also subsidiary
 *                  (child or augment) objects info
 *  childsOnly - if true then only child (and not augment) subsidiary
 *               objects will be added into the array
 *
 *  In case of number of matched objects is more than obj_info_size
 *  (i.e. there is no enough space into obj_info array), the array will conatain
 *  only obj_info_size elements
 */
static ep_stat_t w_get_obj_info(worker_data_t *wd, parsed_param_name_t *pn,
                                char nextLevelOnly, char childsOnly,
                                obj_info_t *obj_info, int obj_info_size, int *obj_num)
{
    int i = 0, cnt = 0, res = 0;
    int obj_idx_num = 0, child_idx_num = 0;
    char *p_ph, *p_curr_offset;
    char tmp_obj_name[MSG_MAX_STR_LEN];

    /*DBG("%s[%s] (partial path %d, nextLevel %d, childsOnly %d)",
         pn->obj_name, pn->leaf_name,
         pn->partial_path, nextLevelOnly, childsOnly);*/

    *obj_num = 0;
    obj_idx_num = w_num_of_obj_indeces(pn->obj_name);

    sqlite3_stmt *stmt = NULL;

    if (pn->partial_path && nextLevelOnly == 0)
    {
        stmt = wd->stmt_get_obj_list;
        strcat_safe(pn->obj_name, "%", sizeof(pn->obj_name));
    }
    else
    {
        stmt = wd->stmt_get_obj_info;
    }

    if (sqlite3_bind_text(stmt, 1, pn->obj_name, -1, SQLITE_STATIC) != SQLITE_OK)
    {
        ERROR("Could not bind object name %s to the prepared statement: %s",
               pn->obj_name, sqlite3_errmsg(wd->mdb_conn));
        return EPS_SQL_ERROR;
    }

    while (TRUE)
    {
        res = sqlite3_step(stmt);

        if (res == SQLITE_ROW)
        {
            if (cnt == 0)
            {
                /* This is the specified object itself; no additional checks */
            }
            else
            {
                /* This is the subsidiary object (child or augment) */
                /* Check if only child objects are requested        */
                if (nextLevelOnly == 0 && childsOnly == 1)
                {
                    memset(tmp_obj_name, 0, sizeof(tmp_obj_name));
                    strcpy_safe(tmp_obj_name, (char *)sqlite3_column_text(stmt, 0),
                                                              sizeof(tmp_obj_name));
                    if ((child_idx_num = w_num_of_obj_indeces(tmp_obj_name)) == obj_idx_num)
                    {
                        /*DBG("Obj %s isn't a child: it has the same num of indices (%d)",
                                  tmp_obj_name, child_idx_num); */
                        continue;
                    }

                    /* Determine the child obj "suffix", i.e. symbols after the last "."
                       (jumping over object placeholders '{i}') */
                    p_curr_offset = (char *)tmp_obj_name;
                    for (i = 1; i <= child_idx_num; i++)
                    {
                        p_ph = strstr(p_curr_offset,  "."INDEX_PLACEHOLDER".");
                        p_curr_offset = p_ph + strlen( "."INDEX_PLACEHOLDER".");
                    }

                    /* Test "suffix" len: for child object must be 0 */
                    if (strlen(p_curr_offset) != 0)
                    {
                        //DBG("Obj %s is not a child: suffix is %s", tmp_obj_name, p_curr_offset);
                        continue;
                    }
                }
            }

            /* Add object to the obj_info array after the checks */

            if (cnt < obj_info_size)
            {
                fill_obj_info(stmt, &(obj_info[cnt]));
                cnt++;
            }

        }
        else if (res == SQLITE_DONE)
        {
            break;
        }
        else
        {
            ERROR("Could not execute query: %s", sqlite3_errmsg(wd->mdb_conn));
            break;
        }
    }

    if (sqlite3_reset(stmt) != SQLITE_OK)
    {
        ERROR("Could not reset sql statement: %s", sqlite3_errmsg(wd->mdb_conn));
        return EPS_SQL_ERROR;
    }

    if (cnt == 0)
    {
        ERROR("Unknown object name %s", pn->obj_name);
        return EPS_INVALID_PARAM_NAME;
    }

    *obj_num = cnt;
    return EPS_OK;
}

ep_stat_t w_send_answer(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;
    int res;
    struct sockaddr_in client_addr;

    /* check is response has to be sent or not */
    if (message->header.respMode == MMX_API_RESPMODE_NORESP)
    {
        DBG("Response is not needed");
        return status;
    }

    /* Response is needed: create socket, prepare message and send it */
    client_addr.sin_family = AF_INET;
    client_addr.sin_port = htons(message->header.respPort);
    client_addr.sin_addr.s_addr = (message->header.respIpAddr);

    status = mmx_frontapi_message_build(message, wd->answer_buf, sizeof(wd->answer_buf));

    if (status != EPS_OK)
    {
        ERROR("Could not write XML response (%d)", status);
    }
    else
    {
        DBG("Txa Id: %d, resp code: %d, caller Id: %d, moreFlag: %d, resp len: %d bytes",
            message->header.txaId, message->header.respCode,
            message->header.callerId, message->header.moreFlag, strlen(wd->answer_buf));
        //DBG("%s", wd->answer_buf);

        res = sendto(wd->udp_sock, wd->answer_buf, sizeof(wd->answer_buf)+1, 0,
                          (struct sockaddr *)&client_addr, sizeof(client_addr));
        if (res < 0)
        {
            ERROR("Could not send message: %s", strerror(errno));
            status = EPS_SYSTEM_ERROR;
        }
    }

    /* Reset some message's fields */
    message->header.moreFlag = 0;
    memset(&message->body, 0, sizeof(message->body));

    return status;
}

/* Select meta information of parameters of the specified object.
   In case of requested parameter is specified by a partial path, meta info of
   all object's parameters are selected. Otherwise (i.e. parameter is specified
   by a full-path name), meta info of this parameter and of object's index
   parameters are selected.
   If configOnly flag is TRUE, only writable parameters are processed */
ep_stat_t w_get_param_info(worker_data_t *wd, parsed_param_name_t *pn,
                           obj_info_t *obj_info, char configOnly,
                           param_info_t param_info[],
                           int *param_num, int *param_idx)
{
    ep_stat_t status = EPS_OK;
    int res, i, found, paramCnt = 0;
    char query[EP_SQL_REQUEST_BUF_SIZE];
    sqlite3_stmt *stmt = NULL;

    if (param_idx) *param_idx = -1;
    *param_num = 0;

    sprintf(query, "SELECT "
            "ParamName, Writable, UserAccessPerm, ReadFrontEnds, WriteFrontEnds, "
            "ParamType, IsIndex, ValueIsList, MinValue, MaxValue, DefValue, "
            "MinLength, MaxLength, Hidden, NotSaveInDb, Units, EnumValues, "
            "StyleOfGet, StyleOfSet, GetMethod, SetMethod FROM [%s]",
            obj_info->objInfoTblName);

    if (!pn->partial_path && (strlen(pn->leaf_name) > 0))
    {
        strcat_safe(query, " WHERE ParamName='", sizeof(query));
        strcat_safe(query, pn->leaf_name, sizeof(query));
        strcat_safe(query, "' OR IsIndex = 1 ", sizeof(query));
        //DBG("leaf param name: %s", pn->leaf_name);
    }

    //DBG(" query buff size %d; Query: %s", sizeof(query), query);

    if (sqlite3_prepare_v2(wd->mdb_conn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s", sqlite3_errmsg(wd->mdb_conn));

    while (TRUE)
    {
        res = sqlite3_step(stmt);

        if (res == SQLITE_ROW)
        {
            if (paramCnt >= MAX_PARAMS_PER_OBJECT)
            {
                GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR,
                                 "Number parameters of object biggest than defined in MAX_PARAMS_PER_OBJECT=(%d)",
                                 MAX_PARAMS_PER_OBJECT);
            }
            if (fill_param_info(stmt, &(param_info[paramCnt]), configOnly) == EPS_OK)
                paramCnt++;
        }
        else if (res == SQLITE_DONE)
        {
            break;
        }
        else
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query (%d): %s",
                                res, sqlite3_errmsg(wd->mdb_conn));
    }
    *param_num = paramCnt;

    /* Check if the requested param name was found - for full-path name only*/
    if (!pn->partial_path && (strlen(pn->leaf_name) > 0))
    {
        i = -1;
        found = 0;
        while (!found && (++i < *param_num))
        {
            if (!strcmp(pn->leaf_name, param_info[i].paramName) )
                found = 1;
        }

        if (!found)
        {
            ERROR("Unknown param name: %s", pn->leaf_name);
            status = EPS_INVALID_PARAM_NAME;
        }

        if ( param_idx )
            *param_idx = found ? i : -1;
   }

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/* Convert MMX EP error codes to the standard TR-069 fault codes
 * (TR-069 Issue 1 Amendment 4) */
static int w_status2cwmp_error(ep_stat_t status)
{
    switch (status)
    {
        case EPS_OK:                 return MMX_API_RC_OK;                //0
        case EPS_NOT_FOUND:          return MMX_API_RC_NOT_SUPPORTED;     //9000
        case EPS_NOT_IMPLEMENTED:    return MMX_API_RC_REQUEST_DENIED;    //9001;
        case EPS_NO_PERMISSION:      return MMX_API_RC_REQUEST_DENIED;
        case EPS_RESOURCE_NOT_FREE:  return MMX_API_RC_REQUEST_DENIED;
        case EPS_INVALID_FORMAT:     return MMX_API_RC_INVALID_ARGUMENT;  //9003;
        case EPS_INVALID_ARGUMENT:   return MMX_API_RC_INVALID_ARGUMENT;
        case EPS_FULL:               return MMX_API_RC_RESOURCES_EXCEEDED; //9004;
        case EPS_NO_MORE_ROOM:       return MMX_API_RC_RESOURCES_EXCEEDED;
        case EPS_INVALID_PARAM_NAME: return MMX_API_RC_INVALID_PARAM_NAME; //9005
        case EPS_NOT_WRITABLE:       return MMX_API_RC_NOT_WRITABLE;      //9008

        case EPS_CANNOT_OPEN_DB:     return MMX_API_RC_DB_ACCESS_ERROR;   //9810
        case EPS_SQL_ERROR:          return MMX_API_RC_DB_QUERY_ERROR;    //9811
        case EPS_INVALID_DB_TYPE:    return MMX_API_RC_INCORRECT_DB_TYPE; //9812

        default: return MMX_API_RC_INTERNAL_ERROR;                        //9002
    }

    return MMX_API_RC_INTERNAL_ERROR;
}


static char * w_perform_prepared_command(char *buf, int buf_size,
                                         BOOL read_results,
                                         int *res_code)
{
    char *p_res = NULL;
    FILE *fp;

    /* Perform command that was preperated in buf and
       read results if it is requested */
    fp = popen(buf, "r");
    if (!fp)
    {
        DBG("Could not execute prepared command");
        return NULL;
    }

    if (read_results)
    {
        memset(buf, 0, buf_size);
        p_res = fgets(buf, buf_size, fp);
    }

    if (!res_code)
    {
        /* Command's exit status is out of interest */
        pclose(fp);
    }
    else
    {
        /* Command's exit status is needed for the caller.
           Here we expect the command (child process) terminated normally
           and returned its status. See more in waitpid() POSIX Specification */
        *res_code = WEXITSTATUS(pclose(fp));
    }

    if (read_results && !p_res)
    {
        DBG("Could not read command results");
        return NULL;
    }
    //DBG("Results of performed command: %s ", p_res);

    return buf;
}


/* Parse results returned by set, adobj and delobj method scripts
   Result string  contains two elements separated by ";"
   It looks as:      resCode; status;
   resCode can be  0 - in case of the script completed successfully
                   other value if script failed
   status can be 0 in case of no additional action is needed
                 1 - if backend restart is needed
    Output:
    parsedResCode - integer - result code received from the method script
    parsedStatus - integer  - set/add/delStatus received from the
    nextPtr      - pointer to the next token in the buf after ";"

    */
static ep_stat_t w_parse_script_res(char *buf, int buf_size,
                                    int  *parsedResCode,
                                    int  *parsedStatus,
                                    char **nextPtr)
{
    ep_stat_t status = EPS_OK;
    int res_code = 0, res_stat = 0;
    char *strtok_ctx, *token, *ptr;

    if ( parsedResCode )
        *parsedResCode = 1;

    if ( parsedStatus )
        *parsedStatus = 0;

    if ( nextPtr )
       *nextPtr = buf;

    token = strtok_r(buf, ";", &strtok_ctx);
    if (!token || (strlen(trim(token)) == 0))
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Failed to parse script result");


    res_code = (int)strtol(token, &ptr, 10);
    if (ptr && strlen(ptr))
    {
        /* script resCode is not integer */
        res_code = 99;
        DBG("Script returned wrong value of resCode %s (must be integer)", token);
    }

    if (res_code == 0)
    {
        /*Script completed Ok. Parse second token (setStatus) and update DB*/
        token = strtok_r(NULL, "; ", &strtok_ctx);
        ptr = NULL;
        res_stat = 0;
        if (token && (strlen(trim(token)) > 0))
        {
            res_stat = (int)strtol(token, &ptr, 10);
            if ((ptr && strlen(ptr)) || ((res_stat != 0) && (res_stat != 1)))
            {
                /* script Status is not 0 or 1 integer */
                res_stat = 0;
                DBG("Script returned wrong value of status %s (must be 0 or 1)", token);
            }
        }
    }

    if ( parsedResCode )
        *parsedResCode = res_code;

    if ( parsedStatus )
        *parsedStatus = res_stat;

    if ( nextPtr )
        *nextPtr = strtok_ctx;

ret:
    return status;
}

#define SAVE_FILE_COMMAND_START "tmpovrlctl save -f \0"
#define SAVE_FILE_COMMAND_END   " &>/dev/null"

/* Helper function saving file to the permanent memory
   when /etc is mounted over tmp fs.
 (i.e. feature "tmpfs overlay "above" /etc" is supported in the image) */
static ep_stat_t w_save_file(char *fileName)
{
    char buf[FILENAME_BUF_LEN + strlen(SAVE_FILE_COMMAND_START) + strlen(SAVE_FILE_COMMAND_END) + 1];

#ifdef ING_TMP_OVERLAY
    memset((char*)buf, 0, sizeof(buf));
    strcpy_safe(buf, SAVE_FILE_COMMAND_START, sizeof(buf));
    strcat_safe(buf, fileName, sizeof(buf));
    strcat_safe(buf, SAVE_FILE_COMMAND_END, sizeof(buf));

    DBG("Save file command: %s", buf);
    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);
#endif

    return EPS_OK;
}


/* -------------------------------------------------------------------------------*
 * -------------------- Reboot/reset functions  ----------------------------------*
 * -------------------------------------------------------------------------------*/
 #define MMX_MAX_DELAY_FOR_REBOOT  30  //secs
 #define MMX_MIN_DELAY_FOR_REBOOT  6   //secs
 #define MMX_MAX_DELAY_FOR_RESET   10  //secs
 #define MMX_MIN_DELAY_FOR_RESET   6   //secs

 #define MMX_PREREBOOT_HOLD_TIME   6   //secs

static ep_stat_t w_reboot(worker_data_t *wd, int delay_time, ep_message_t *answer)
{
    char buf[EP_SQL_REQUEST_BUF_SIZE] = {0};
    int sleep_time;

    /* Check the received delay value */
    if (delay_time > MMX_MAX_DELAY_FOR_REBOOT) delay_time = MMX_MAX_DELAY_FOR_REBOOT;
    if (delay_time < MMX_MIN_DELAY_FOR_REBOOT) delay_time = MMX_MIN_DELAY_FOR_REBOOT;
    DBG("System reboot will be started in %d seconds", delay_time);

    /* Send response to the requestor now (before sleep) */
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    sleep_time = delay_time - MMX_PREREBOOT_HOLD_TIME;
    if (sleep_time > 0)
    {
        DBG("Sleep time before reboot: %d", sleep_time);
        sleep(sleep_time);
    }

    /*Set EP to hold to allow "gracefull" reboot and then perform the command*/
    ep_common_set_hold_status(EP_HOLD_PRE_REBOOT, MMX_PREREBOOT_HOLD_TIME);

    if (MMX_PREREBOOT_HOLD_TIME - 2 > 0)
       sleep(MMX_PREREBOOT_HOLD_TIME - 2);

    sprintf(buf, "reboot -d 2  &");
    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);
    DBG("Reboot command has been initiated: %s", buf);

    /* We don't release EP hold status here. It will be reset during
       init after the reboot */

    return EPS_OK;
}

static ep_stat_t w_shutdown(worker_data_t *wd, int delay_time, ep_message_t *answer)
{
    char buf[EP_SQL_REQUEST_BUF_SIZE]= {0};
    int sleep_time;

    /* Check the received delay value. The delay time values are the same as
       for reboot */
    if (delay_time > MMX_MAX_DELAY_FOR_REBOOT) delay_time = MMX_MAX_DELAY_FOR_REBOOT;
    if (delay_time < MMX_MIN_DELAY_FOR_REBOOT) delay_time = MMX_MIN_DELAY_FOR_REBOOT;
    DBG("System shutdown will be started in %d seconds", delay_time);

    /* Send response to the requestor now (before sleep) */
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    sleep_time = delay_time - MMX_PREREBOOT_HOLD_TIME;
    if (sleep_time > 0)
    {
        sleep(sleep_time);
        DBG("Sleep time before reboot: %d", sleep_time);
    }

    /*Set EP to hold to allow "gracefull" shutdown and then perform the command*/
    ep_common_set_hold_status(EP_HOLD_PRE_REBOOT, MMX_PREREBOOT_HOLD_TIME);

    if (MMX_PREREBOOT_HOLD_TIME - 2 > 0)
        sleep(MMX_PREREBOOT_HOLD_TIME - 2);

    sprintf(buf, "halt -d 2 &");
    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);
    DBG("Device shudown command has been initiated: %s", buf);

    /* We don't release EP hold status here. It is not needed.
       The device is going to be turned off */

    return EPS_OK;
}

 /* TODO -Think do we need delay time in factory reset request */
 /* Currently supported resetType is 0, i.e. factory reset     */
static ep_stat_t w_reset(worker_data_t *wd, int delay_time, int rst_type,
                         ep_message_t *answer)
{
    int sleep_time = 0;
    char buf[EP_SQL_REQUEST_BUF_SIZE];

    memset((char*)buf, 0, sizeof(buf));

    if (delay_time > MMX_MAX_DELAY_FOR_RESET) delay_time = MMX_MAX_DELAY_FOR_RESET;
    if (delay_time < MMX_MIN_DELAY_FOR_RESET) delay_time = MMX_MIN_DELAY_FOR_RESET;
    DBG("System reset will be started in %d seconds", delay_time);

    /* Send response to the requestor now (before sleep)*/
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    sleep_time = delay_time - MMX_PREREBOOT_HOLD_TIME;
    if (sleep_time > 0)
    {
        DBG("Sleep time before reset: %d", sleep_time);
        sleep(sleep_time);
    }

    /* Set EP to hold to prevent other request processing */
    ep_common_set_hold_status(EP_HOLD_PRE_RESET, MMX_PREREBOOT_HOLD_TIME);

    if (MMX_PREREBOOT_HOLD_TIME - 3 > 0)
        sleep(MMX_PREREBOOT_HOLD_TIME - 3);

    /*Prepare all needed shell commands in the buffer*/
#ifdef ING_TMP_OVERLAY
    if (rst_type == RSTTYPE_FACTORY_RESET)
    {
        sprintf(buf, "tmpovrlctl factory -f / &>/dev/null\n"
            "reboot -d 2 &"
            );
        DBG("Commands for factory reset:\n %s", buf);
    }
    else if (rst_type == RSTTYPE_FACTORY_RESET_KEEPCONN)
    {
        sprintf(buf, "cp /etc/config/network /etc/config/network.keep\n"
            "tmpovrlctl clean_lower -f / &>/dev/null \n"
            "tmpovrlctl save -f /etc/config/network.keep &>/dev/null \n"
            "reboot -d 2 &"
            );
        DBG("Commands for factory reset keeping IP connectivity:\n %s", buf);
    }
#else // this code used to work without ing-tmp-overlay package
    if (rst_type == RSTTYPE_FACTORY_RESET)
    {
        sprintf(buf, "rm -r -f  /overlay/* \n reboot -d 2  &");
        DBG("Commands for factory reset:\n %s", buf);
    }
    else if (rst_type == RSTTYPE_FACTORY_RESET_KEEPCONN)
    {
        sprintf(buf, "cp /etc/config/network /etc/config/network.keep \n"
            "for f in `find /overlay/upper -type f ! -name 'network.keep' -o -type c 2>/dev/null`; do rm $f ; done;\n"
            "reboot -d 2  &");
        DBG("Commands for factory reset keeping IP connectivity: \n%s", buf);
    }
#endif

    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);
    DBG("Reboot after reset to factory default has been initiated");

    /* We don't release EP hold status here. It will be reset during
       init after the reboot */

    return EPS_OK;
}

static ep_stat_t w_restore_config(worker_data_t *wd, int delay_time,
                                  ep_message_t *answer)
{
    int sleep_time = 0;
    char buf[EP_SQL_REQUEST_BUF_SIZE];
    char db_run_path[FILENAME_BUF_LEN];

    get_db_run_path((char*)db_run_path, FILENAME_BUF_LEN);

    if (delay_time > MMX_MAX_DELAY_FOR_RESET) delay_time = MMX_MAX_DELAY_FOR_RESET;
    if (delay_time < MMX_MIN_DELAY_FOR_RESET) delay_time = MMX_MIN_DELAY_FOR_RESET;
    DBG("System reset will be started in %d seconds", delay_time);

    /* Send response to the requestor now (before sleep)*/
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    sleep_time = delay_time - MMX_PREREBOOT_HOLD_TIME;
    if (sleep_time > 0)
    {
        DBG("Sleep time before the command: %d secs", sleep_time);
        sleep(sleep_time);
    }

    /* Set EP to hold to prevent other request processing */
    ep_common_set_hold_status(EP_HOLD_PRE_RESET, MMX_PREREBOOT_HOLD_TIME);

    if (MMX_PREREBOOT_HOLD_TIME - 3 > 0)
        sleep(MMX_PREREBOOT_HOLD_TIME - 3);

    /*Prepare all needed shell commands in the buffer*/

    /* Just remove the MMX DB; All config files will be restored automatically
       by mmx_extinit actions */
    sprintf(buf,"rm -r -f %s* \n reboot -d 2  &", db_run_path);
    DBG("Prepared commands for restore config: %s", buf);

    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);
    DBG("Reboot (after restore config operation) has been initiated");

    /* We don't release EP hold status here. It will be reset during
       init after the reboot */

    return EPS_OK;
}

static ep_stat_t w_save_configuration(worker_data_t *wd, ep_message_t *answer)
{
    char buf[EP_SQL_REQUEST_BUF_SIZE];
    char db_rt_path[FILENAME_BUF_LEN], db_saved_path[FILENAME_BUF_LEN];

    memset((char*)buf, 0, sizeof(buf));

    get_db_run_path((char*)db_rt_path, FILENAME_BUF_LEN);
    get_db_saved_path((char*)db_saved_path, FILENAME_BUF_LEN);

    /* Prepare command: copy all run-time MMX DBs to the savedDB path and
       if needed save configuration files */
#ifdef ING_TMP_OVERLAY
    sprintf(buf, "cp %s*db %s. \n"
        "tmpovrlctl save -i mmx &>/dev/null",
                  db_rt_path, db_saved_path);
#else // this code used to work without ing-tmp-overlay package
    sprintf(buf, "cp %s*db %s. \n /usr/sbin/mmx_save_cfgfiles.sh &>/dev/null",
                  db_rt_path, db_saved_path);
#endif
    DBG("Save configuration command: %s", buf);
    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);

    /* Send response to the requestor */
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    return EPS_OK;
}

/* Copy the running MMX DBs to the candidate DBs  */
static ep_stat_t w_save_config_to_candidate(worker_data_t *wd, ep_message_t *answer)
{
    char buf[EP_SQL_REQUEST_BUF_SIZE];
    char db_rt_path[FILENAME_BUF_LEN], db_cand_path[FILENAME_BUF_LEN];

    memset((char*)buf, 0, sizeof(buf));

    get_db_run_path((char*)db_rt_path, FILENAME_BUF_LEN);
    get_db_cand_path((char*)db_cand_path, FILENAME_BUF_LEN);

    /* Prepare command: copy all run-time MMX DBs to the candDB path */
#ifdef ING_TMP_OVERLAY
    sprintf(buf, "rm %s* &>/dev/null\n cp %s* %s. &>/dev/null \n"
                  "tmpovrlctl save -f %s &>/dev/null   ",
            db_cand_path, db_rt_path, db_cand_path, db_cand_path);
#else
    sprintf(buf, "rm %s* &>/dev/null\n cp %s* %s. &>/dev/null ",
            db_cand_path, db_rt_path, db_cand_path);
#endif
    DBG("Copy configuration command: %s", buf);
    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);

    /* Send response to the requestor */
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    return EPS_OK;
}

/* Remove the candidate DB */
static ep_stat_t w_remove_candidate_config(worker_data_t *wd, ep_message_t *answer)
{
    char buf[EP_SQL_REQUEST_BUF_SIZE];
    char db_cand_path[FILENAME_BUF_LEN];

    memset((char*)buf, 0, sizeof(buf));

    get_db_cand_path((char*)db_cand_path, FILENAME_BUF_LEN);

    /* Prepare command: remove all files of the candDB*/
#ifdef ING_TMP_OVERLAY
    sprintf(buf,"rm %s* &>/dev/null\ntmpovrlctl save -f %s &>/dev/null",
            db_cand_path, db_cand_path);
#else
    sprintf(buf, "rm %s* &>/dev/null\n", db_cand_path);
#endif

    DBG("Remove cand DB command: %s", buf);
    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);

    /* Send response to the requestor */
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    return EPS_OK;
}

static ep_stat_t w_restore_candidate_config(worker_data_t *wd, int delay_time,
                                            ep_message_t *answer)
{
    int sleep_time = 0;
    char buf[EP_SQL_REQUEST_BUF_SIZE];
    char dbtype_fname[FILENAME_BUF_LEN];

    memset((char*)buf, 0, sizeof(buf));

    get_start_dbtype_filename((char*)dbtype_fname, FILENAME_BUF_LEN);

    if (delay_time > MMX_MAX_DELAY_FOR_RESET) delay_time = MMX_MAX_DELAY_FOR_RESET;
    if (delay_time < MMX_MIN_DELAY_FOR_RESET) delay_time = MMX_MIN_DELAY_FOR_RESET;
    DBG("Restore of candidate configuration will be started in %d seconds", delay_time);

    /* Send response to the requestor now (before sleep)*/
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    sleep_time = delay_time - MMX_PREREBOOT_HOLD_TIME;
    if (sleep_time > 0)
    {
        //DBG("Sleep time before the command: %d secs", sleep_time);
        sleep(sleep_time);
    }

    /* Set EP to hold to prevent other request processing */
    ep_common_set_hold_status(EP_HOLD_PRE_RESET, MMX_PREREBOOT_HOLD_TIME);

    if (MMX_PREREBOOT_HOLD_TIME - 2 > 0)
        sleep(MMX_PREREBOOT_HOLD_TIME - 2);

    /* Prepare shell commands: set the start db type to "candidate" and then
       reboot the system. After reboot EP will use the candidate DB as
       the running datastore */

#ifdef ING_TMP_OVERLAY
    sprintf(buf, "echo candidate > %s \n"
                 "tmpovrlctl save -f %s &>/dev/null \nreboot -d 2  &",
                  dbtype_fname, dbtype_fname);
#else
    sprintf(buf,"echo candidate > %s \nreboot -d 2  &", dbtype_fname);
#endif

    DBG("Restore the cand config command: %s", buf);
    w_perform_prepared_command((char *)buf, sizeof(buf), FALSE, NULL);

    /*We don't release EP hold status here. It will be reset during EP restart*/

    return EPS_OK;
}


static ep_stat_t w_refresh_data(worker_data_t *wd, ep_message_t *answer)
{
    ep_stat_t     status = EPS_OK;
    ep_message_t  msg;

    /*  Prepare structure with config discover request for all objects */
    memset((char *)&msg, 0, sizeof(msg));
    msg.header.msgType = MSGTYPE_DISCOVERCONFIG;
    msg.header.respMode = MMX_API_RESPMODE_NORESP;

    status = w_handle_discover_config(wd, &msg, FALSE);
    DBG( "Common MMX refresh completed with %d status", status);

    /* Send response to the requestor */
    if (answer)
    {
        answer->header.respCode = w_status2cwmp_error(EPS_OK);
        w_send_answer(wd, answer);
    }

    return EPS_OK;
}

/* -------------------------------------------------------------------------------*
 * ----------- Procesing GetParamValue request  ----------------------------------*
 * -------------------------------------------------------------------------------*/
 /* This function places indexes of the object instance to the object name
    For example:
        object name is Device.Bridging.Bridge.{i}.Port.{i}.
        index values are 1 and 3
        result will be: Device.Bridging.Bridge.1.Port.3.
*/
static ep_stat_t w_place_indeces_to_objname(const char *obj_name,
                      int *idx_values, int idx_params_num, char *result )
{
    char *p_ph = strstr(obj_name, INDEX_PLACEHOLDER);
    int i = 0;

    //DBG("Object name: %s", obj_name);
    while (p_ph)
    {
        if (i >= idx_params_num)
        {
            ERROR("Num of {} placeholders more than num of indeces (%d)", idx_params_num);
            break;
        }

        memcpy(result, obj_name, p_ph - obj_name); /* Copy string till placeholder */
        result += (ptrdiff_t)(p_ph - obj_name); /* Advance to where the index value should be */
        sprintf(result,"%d", idx_values[i]); /* Copy the current index value into the objname*/
        result = strchr(result, '\0'); /* Advance to the end of the string (after the index value) */

        /* Advance pointers */
        obj_name = p_ph + strlen(INDEX_PLACEHOLDER);
        p_ph = strstr(obj_name, INDEX_PLACEHOLDER);
        i++;
    }
    strcpy(result, obj_name);

    return EPS_OK;
}

/* Function prepares response message for GetParamValue request.
 * It inserts full-path name and value of the specified parameter to the
 * response buffer. If buffer is already full, the procedure sends already
 * prepared response, and then inserts the needed parameter to the next
 * portion response.
 *   */
static ep_stat_t w_insert_value_to_answer(worker_data_t *wd, ep_message_t *answer,
                                          char *obj_name, int *idx_values, int idx_params_num,
                                          char *paramName, char *paramValue)
{
#define   EP_FE_XML_HEADER_SIZE   (400)   /* All header tags                 */
#define   EP_XML_NVP_OVERHEAD     (60)    /* name, value, nameValuePair tags */
    int res;
    int arrsize;
    int val_len;
    nvpair_t *p_res_param;
    char full_param_name[NVP_MAX_NAME_LEN];

    memset ((char *)full_param_name, 0, sizeof(full_param_name));

    /* Check the number of name-value pairs in the response */
    if (answer->body.getParamValueResponse.arraySize >= MAX_NUMBER_OF_RESPONSE_VALUES - 1)
    {
        DBG("Response portion is prepared (%d elems) - too many params; param %s will be sent next time",
             answer->body.getParamValueResponse.arraySize, paramName);
        answer->header.moreFlag = 1;
        w_send_answer(wd, answer);
    }

    arrsize = answer->body.getParamValueResponse.arraySize;
    p_res_param = &(answer->body.getParamValueResponse.paramValues[arrsize]);

    /* Fill parameter name (with all indeces) */
    w_place_indeces_to_objname(obj_name, (int *)idx_values, idx_params_num,
                               (char *)full_param_name);
    strcat_safe((char *)full_param_name, paramName, NVP_MAX_NAME_LEN);

    val_len = paramValue ? strlen(paramValue) : 0;

    /*Check if there is enough space in the answer XML buffer */
    if ((answer->body.getParamValueResponse.totalNVSize + EP_FE_XML_HEADER_SIZE +
         EP_XML_NVP_OVERHEAD * (arrsize + 1) +
         strlen(full_param_name) + val_len) >= MAX_MMX_EP_ANSWER_LEN)
    {
        DBG("Resp portion is prepared (%d elems) - no memory in XML buffer; param %s will be sent next time",
             answer->body.getParamValueResponse.arraySize, paramName);
        answer->header.moreFlag = 1;
        w_send_answer(wd, answer);

        mmx_frontapi_msg_struct_init(answer, (char *)wd->fe_resp_values_pool,
                                     sizeof(wd->fe_resp_values_pool));

        arrsize = answer->body.getParamValueResponse.arraySize;
        p_res_param = &(answer->body.getParamValueResponse.paramValues[arrsize]);
    }

    /* Fill parameter value in the answer array */
    res = mmx_frontapi_msgstruct_insert_nvpair(answer, p_res_param,
                                        (char *)full_param_name, paramValue);
    if (res != FA_OK)
    {
        /* There is not enough space in the pool */
        DBG("Resp portion is prepared (%d elems) - no memory in value pool; param %s will be sent next time",
             answer->body.getParamValueResponse.arraySize, paramName);
        answer->header.moreFlag = 1;
        w_send_answer(wd, answer);

        mmx_frontapi_msg_struct_init(answer, (char *)wd->fe_resp_values_pool,
                                     sizeof(wd->fe_resp_values_pool));

        /* Try again to place value. Now it will a new answer packet and
           the parameter will be at the first place. */
        arrsize = answer->body.getParamValueResponse.arraySize;
        p_res_param = &(answer->body.getParamValueResponse.paramValues[arrsize]);

        res = mmx_frontapi_msgstruct_insert_nvpair(answer, p_res_param,
                                        (char *)full_param_name, paramValue);
        if (res != FA_OK)
        {
            ERROR("Param %s is too long; cannot be placed to answer msg", paramName);
            return EPS_IGNORED;
        }
    }

    /*DBG("Param %s is added to answer array (idx %d)", p_res_param->name,
          answer->body.getParamValueResponse.arraySize);*/

    answer->body.getParamValueResponse.arraySize++;
    answer->body.getParamValueResponse.totalNVSize +=
                                    (strlen(full_param_name) + val_len);

    return EPS_OK;
}

/* Param_info array contains information about configuration parameters
   of the specified object (index parameters are also included to the array) */
static ep_stat_t w_get_values_configonly(worker_data_t *wd, ep_message_t *answer,
                                         parsed_param_name_t *pn,
                                         obj_info_t *obj_info, sqlite3 *obj_db_conn,
                                         param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK;
    int  res = 0, param_cnt = 0;
    int  i, j, c;
    int cfg_owner = 0, create_owner = 0;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char idx_buf[32];

    char query[EP_SQL_REQUEST_BUF_SIZE] = "SELECT ";
    sqlite3_stmt *stmt = NULL;

    /* Save names of all index parameters of the object */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    if (param_num == 0)  // Nothing to do
        return EPS_OK;

    /* Build SELECT query */
    for (c = 0; c < param_num; c++)
    {
        strcat_safe(query, "[", sizeof(query));
        strcat_safe(query, param_info[c].paramName, sizeof(query));
        strcat_safe(query, "],", sizeof(query));
    }
    /*Add service params to determine system or user created/configured instance*/
    strcat_safe(query, MMX_CREATEOWNER_DBCOLNAME "," MMX_CFGOWNER_DBCOLNAME,  sizeof(query));

    strcat_safe(query, " FROM ", sizeof(query));
    strcat_safe(query, obj_info->objValuesTblName, sizeof(query));
    strcat_safe(query, " WHERE 1 AND ", sizeof(query));
    for (i = 0; i < pn->index_num; i++)
    {
        if (pn->indices[i].type == REQ_IDX_TYPE_EXACT)
        {
            strcat_safe(query, "[", sizeof(query));
            strcat_safe(query, idx_params[i], sizeof(query));
            strcat_safe(query, "]=", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].exact_val.num);
            strcat_safe(query, " AND ", sizeof(query));
        }
        else if (pn->indices[i].type == REQ_IDX_TYPE_RANGE)
        {
            /* concatenate (PARAM>=RANGE_BEGIN AND PARAM<=RANGE_END) */
            strcat_safe(query, "([", sizeof(query));
            strcat_safe(query, idx_params[i], sizeof(query));
            strcat_safe(query, "]>=", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].range_val.begin);
            strcat_safe(query, " AND [", sizeof(query));
            strcat_safe(query, idx_params[i], sizeof(query));
            strcat_safe(query, "]<=", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].range_val.end);
            strcat_safe(query, ")", sizeof(query));
            strcat_safe(query, " AND ", sizeof(query));
        }
    }
    query[strlen(query)-5] = '\0'; /* Remove last " AND " */

    /* Execute query */
    DBG("%s", query);
    if (sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s", sqlite3_errmsg(obj_db_conn));

    /* Write answer values */
    while (TRUE)
    {
        res = sqlite3_step(stmt);
        /*DBG("SQL query result %d, param_num %d, col counter %d", res, param_num,
             sqlite3_column_count(stmt)); */
        if (res == SQLITE_ROW)
        {
            /* Save all selected index values - indeces are always the first*/
            memset(idx_buf, 0, sizeof(idx_buf));
            for (j = 0; j < idx_params_num; j++)
            {
                idx_values[j] = sqlite3_column_int(stmt, j);
                sprintf(idx_buf, "%d,",idx_values[j]);
            }
            LAST_CHAR(idx_buf) = '\0'; /* Delete the last comma */


            /* select the service values - they are two last params */
            create_owner = sqlite3_column_int(stmt, param_num);
            cfg_owner = sqlite3_column_int(stmt, param_num + 1);
            if ( (create_owner == 0) && (cfg_owner == 0) )
            {
                //DBG("This is system created and configured instance: %s", idx_buf);
                continue;
            }
            else
            {
                 //DBG("This is user created or configured instance: %s", idx_buf);
            }
            /* Now go over all requested parameters */
            for (c = j; c < param_num; c++)
            {
                //DBG("Requested param %d, paramname = %s", c, param_info[c].paramName);
                if (pn->partial_path || !strcmp(sqlite3_column_name(stmt, c), pn->leaf_name))
                {
                    if (!paramReadAllowed(param_info, i, answer->header.callerId))
                    {
                        /*DBG("Param %s isn't allowed to be read by this requestor (%d)" ,
                            param_info[i].paramName, answer->header.callerId);*/
                        continue;
                    }

                    /*DBG("Param number %d, param name %s, param value %s",
                      c, param_info[c].paramName, sqlite3_column_text(stmt, c));*/

                    if ((sqlite3_column_text(stmt, c) != NULL) && (sqlite3_column_bytes(stmt, c) != 0))
                    {
                        w_insert_value_to_answer(wd, answer, obj_info->objName,
                            idx_values, idx_params_num, param_info[c].paramName,
                            (char *)db2soap((char *)sqlite3_column_text(stmt,c), param_info[c].paramType));

                        param_cnt++;
                        //DBG("Counter of param inserted to answer: %d", param_cnt);
                    }
                }
            }  //End of "for" stmt over requested parameters
        }
        else if (res == SQLITE_DONE)
            break;
        else
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(obj_db_conn));
    }
ret:
    if (param_cnt > 0) DBG(" %d parameters were processed (resp arrsize = %d)",
                           param_cnt, answer->body.getParamValueResponse.arraySize);
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

static ep_stat_t w_get_values_db(worker_data_t *wd, ep_message_t *answer,
                                 parsed_param_name_t *pn,
                                 obj_info_t *obj_info, sqlite3 *obj_db_conn,
                                 param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK;
    int  res = 0, param_cnt = 0;
    int  i, j, c;
    int  db_params[MAX_PARAMS_PER_OBJECT];
    int  db_params_num = 0;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];

    char query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    sqlite3_stmt *stmt = NULL;

    strcpy_safe(query, "SELECT ", sizeof(query));

    /* Save names of all index parameters of the object */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /* Store index params and requested params having "db" get-method style.
       (Index parameters of the objects are always the first)               */
    for (i = 0; i < param_num; i++)
    {
         if ((obj_info->getOperStyle == OP_STYLE_DB) || (param_info[i].isIndex) ||
             ((param_info[i].getOperStyle == OP_STYLE_DB) &&
              (!strcmp(pn->leaf_name, param_info[i].paramName) || pn->partial_path)))
        {
            db_params[db_params_num++] = i;
        }
    }

    if (db_params_num == 0)  // Nothing to do
    {
        return EPS_OK;
    }
    /* Build SELECT query */
    for (c = 0; c < db_params_num; c++)
    {
        strcat_safe(query, "[", sizeof(query));
        strcat_safe(query, param_info[db_params[c]].paramName, sizeof(query));
        strcat_safe(query, "],", sizeof(query));
    }
    LAST_CHAR(query) = '\0'; /* Delete last comma */

    strcat_safe(query, " FROM ", sizeof(query));
    strcat_safe(query, obj_info->objValuesTblName, sizeof(query));
    strcat_safe(query, " WHERE 1 AND ", sizeof(query));
    for (i = 0; i < pn->index_num; i++)
    {
        if (pn->indices[i].type == REQ_IDX_TYPE_EXACT)
        {
            /* concatenate PARAM=ARG */
            strcat_safe(query, "[", sizeof(query));
            strcat_safe(query, param_info[i].paramName, sizeof(query));
            strcat_safe(query, "]=", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].exact_val.num);
            strcat_safe(query, " AND ", sizeof(query));
        }
        else if (pn->indices[i].type == REQ_IDX_TYPE_RANGE)
        {
            /* concatenate (PARAM>=RANGE_BEGIN AND PARAM<=RANGE_END) */
            strcat_safe(query, "([", sizeof(query));
            strcat_safe(query, param_info[i].paramName, sizeof(query));
            strcat_safe(query, "]>=", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].range_val.begin);
            strcat_safe(query, " AND [", sizeof(query));
            strcat_safe(query, param_info[i].paramName, sizeof(query));
            strcat_safe(query, "]<=", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].range_val.end);
            strcat_safe(query, ")", sizeof(query));
            strcat_safe(query, " AND ", sizeof(query));
        }
    }
    query[strlen(query)-5] = '\0'; /* Remove last " AND " */

    /* Execute query */
    DBG("%s", query);
    if (sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s", sqlite3_errmsg(obj_db_conn));

    /* Write answer values */
    while (TRUE)
    {
        res = sqlite3_step(stmt);
        /*DBG("SQL query result %d, column counter %d, idx num %d", res,
            sqlite3_column_count(stmt), idx_params_num); */
        if (res == SQLITE_ROW)
        {
            /* Save all selected index values - indeces are always the first*/
            for (j = 0; j < idx_params_num; j++)
            {
                idx_values[j] = sqlite3_column_int(stmt, j);
            //DBG("Selected indeces: idx = %d, val = %d", j, idx_values[j]);
            }

            /* Now go over all requested parameters */
            for (c = 0; c < sqlite3_column_count(stmt); c++)
            {
                i = db_params[c]; //Index in the param_info array
                //DBG("Requested param %d, idx = %d, paramname = %s", c, i, param_info[i].paramName);

                if (pn->partial_path || !strcmp(sqlite3_column_name(stmt, c), pn->leaf_name))
                {
                    /* Check if this parameter is allowed to be read by the caller */
                    if (!paramReadAllowed(param_info, i, answer->header.callerId))
                    {
                        /*DBG("Param %s isn't allowed to be read by this requestor (%d)" ,
                            param_info[i].paramName, answer->header.callerId);*/
                        continue;
                    }
                    /*DBG("Param number %d, param name %s, param value %s",
                      c, param_info[i].paramName, sqlite3_column_text(stmt, c));*/

                    w_insert_value_to_answer(wd, answer, obj_info->objName,
                            idx_values, idx_params_num, param_info[i].paramName,
                        (char *)db2soap((char *)sqlite3_column_text(stmt, c), param_info[i].paramType));

                    param_cnt++;
                    //DBG("Param counter if inserted params: %d", param_cnt);
                }
            }  //End of "for" stmt over requested parameters
        }
        else if (res == SQLITE_DONE)
            break;
        else
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(obj_db_conn));
    }
ret:
    if (param_cnt > 0) DBG(" %d parameters were processed (num of indexes = %d, resp arrsize = %d)",
                           param_cnt, pn->index_num, answer->body.getParamValueResponse.arraySize);
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

static ep_stat_t w_form_call_str(char *cmd_str, size_t cmd_str_size,
        parsed_operation_t *parsed_ubus_str, sqlite3_stmt *stmt, int idx_subst_param)
{
    const char *subst_val;
    char aux_subst_val[NVP_MAX_VALUE_LEN + 2]; // +2 chars for quotes
    int db_value, i;

    strcpy_safe(cmd_str, parsed_ubus_str->command, cmd_str_size);

    if (stmt)
    {
        for (i = 0; i < parsed_ubus_str->subst_val_num; i++)
        {
            memset(aux_subst_val, 0, NVP_MAX_VALUE_LEN + 2);

            if (parsed_ubus_str->subst_val[i].name_formatter) // must occur only on SET operation
            {
                snprintf(aux_subst_val, NVP_MAX_VALUE_LEN + 2, "'%s'",
                         (char *)parsed_ubus_str->subst_val[i].name_val);
                subst_val = (char *)aux_subst_val;
            }
            else if (parsed_ubus_str->subst_val[i].value_formatter) // must occur only on SET operation
            {
                snprintf(aux_subst_val, NVP_MAX_VALUE_LEN + 2, "'%s'",
                         (char *)parsed_ubus_str->subst_val[i].value_val);
                subst_val = (char *)aux_subst_val;
            }
            else
            {
                if (parsed_ubus_str->subst_val[i].conditional)
                {
                    db_value = sqlite3_column_int(stmt, idx_subst_param+i);
                    subst_val = db_value ? parsed_ubus_str->subst_val[i].true_val :
                                           parsed_ubus_str->subst_val[i].false_val;
                }
                else
                {
                    subst_val = (char *)sqlite3_column_text(stmt, idx_subst_param+i);
                }
            }
            str_replace(cmd_str, cmd_str_size, "$$", subst_val ? subst_val : " ");
        }
    }
    else
    {
        /* No SQL stmt stated but substitution values
         *    @name
         *    @value
         *  may be present so needed to be processed */
        for (i = 0; i < parsed_ubus_str->subst_val_num; i++)
        {
            memset(aux_subst_val, 0, NVP_MAX_VALUE_LEN + 2);

            if (parsed_ubus_str->subst_val[i].name_formatter) // must occur only on SET operation
            {
                snprintf(aux_subst_val, NVP_MAX_VALUE_LEN + 2, "'%s'",
                         (char *)parsed_ubus_str->subst_val[i].name_val);
                subst_val = (char *)aux_subst_val;
            }
            else if (parsed_ubus_str->subst_val[i].value_formatter) // must occur only on SET operation
            {
                snprintf(aux_subst_val, NVP_MAX_VALUE_LEN + 2, "'%s'",
                         (char *)parsed_ubus_str->subst_val[i].value_val);
                subst_val = (char *)aux_subst_val;
            }
            else
            {
                // It actually should never happen
                WARN("Warning - unhandled substitution case");
            }
            str_replace(cmd_str, cmd_str_size, "$$", subst_val ? subst_val : " ");
        }
    }

    //DBG("Formed command string: %s", cmd_str);
    return EPS_OK;
}


/**************************************************************************/
/*! \fn static ep_stat_t w_form_getall_call_str(char *cmd_str
                , size_t cmd_str_size
                , parsed_backend_method_t *parsed_ubus_str
                , sqlite3_stmt *stmt
                , int idx_subst_param
                , parsed_param_name_t *pn
                , parsed_operation_t *parsed_operation)
 *  \brief Combine script name with substitution parameters from database
 *  \param[out] char *cmd_str // Combined string
 *  \param[in] size_t cmd_str_size // cmd_str buffer size
 *  \param[in] parsed_backend_method_t *parsed_ubus_str // Contains arguments for backend command
 *  \param[in] sqlite3_stmt *stmt // Prepared statement for getting substitution values
 *  \param[in] int idx_subst_param // amount of substitution params
 *  \param[in] parsed_param_name_t *pn
 *  \param[in] parsed_operation_t *parsed_operation // This structure has lua script name
 *  \return EPS_OK if success or error code
 */
/**************************************************************************/
static ep_stat_t w_form_getall_call_str(char *cmd_str
                , size_t cmd_str_size
                , parsed_backend_method_t *parsed_ubus_str
                , sqlite3_stmt *stmt
                , int idx_subst_param
                , parsed_param_name_t *pn
                , parsed_operation_t *parsed_operation)
{
    const char *subst_val;
    char aux_subst_val[NVP_MAX_VALUE_LEN + 2]; // +2 chars for quotes
    int db_value, i;
    ep_stat_t status = EPS_OK;

    strcpy_safe(cmd_str, parsed_operation->command, cmd_str_size);

    if (!stmt)
    {
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Database statement does not exist");
    }

    for (i = 0; i < parsed_ubus_str->subst_val_num; i++)
    {
        memset(aux_subst_val, 0, NVP_MAX_VALUE_LEN + 2);

        if (parsed_ubus_str->subst_val[i].mmx_subst_val.name_formatter) // must occur only on SET operation
        {
            snprintf(aux_subst_val, NVP_MAX_VALUE_LEN + 2, "'%s'",
                     (char *)parsed_ubus_str->subst_val[i].mmx_subst_val.name_val);
            subst_val = (char *)aux_subst_val;
        }
        else if (parsed_ubus_str->subst_val[i].mmx_subst_val.value_formatter) // must occur only on SET operation
        {
            snprintf(aux_subst_val, NVP_MAX_VALUE_LEN + 2, "'%s'",
                     (char *)parsed_ubus_str->subst_val[i].mmx_subst_val.value_val);
            subst_val = (char *)aux_subst_val;
        }
        else if (parsed_ubus_str->subst_val[i].mmx_subst_val.conditional)
        {
            db_value = sqlite3_column_int(stmt, idx_subst_param + i);
            subst_val = db_value ? parsed_ubus_str->subst_val[i].mmx_subst_val.true_val :
                                   parsed_ubus_str->subst_val[i].mmx_subst_val.false_val;
        }
        else if (pn->indices[i].type == REQ_IDX_TYPE_EXACT || pn->indices[i].type == REQ_IDX_TYPE_RANGE)
        {

            subst_val = (char *)sqlite3_column_text(stmt, pn->index_set_num + i);
            strcat_safe(cmd_str," -pname ",cmd_str_size);
            strcat_safe(cmd_str,parsed_ubus_str->subst_val[i].backend_key_name,cmd_str_size);
            strcat_safe(cmd_str," -pvalue ",cmd_str_size);
            strcat_safe(cmd_str,subst_val,cmd_str_size);
        }
        else
        {
            DBG("Index is not set");
            break;
        }
    }

ret:
    DBG("Formed command string: %s", cmd_str);
    return status;
}
/*
 * Parses command string for Shell-script styled SET method in the following format:
 *   <command with placeholders>; <substitution values (comma separated)>
 *
 * NOTES:
 *  - `command` and `substitution values` parts are delimited with semicolon ";"
 *     but semilcolon must not be placed at the end of entire command string
 *     (after `substitution values`)
 *  - `substitution values` are comma separated and can be:
 *     - leaf_name              (like "InterfaceName")
 *     - Object's leaf_name     (like "Device.IP.Interface.{i}.InterfaceName")
 *     - specific formatter     (like "@name" or "@value")
 *  - `command` can incorporate shell sub-commands delimited by semicolon ";"
 *     (as usual)
 */
static ep_stat_t w_parse_shell_set_method_string(char *command_string, parsed_operation_t *res)
{
    //int i; // <- uncomment to print last DBG( "Parsing results ..." )
    char *strtok_ctx, *strtok_ctx2, *strtok_ctx3;
    char *subst_values, *t, *p_leaf_name, *cond;

    char *last_semicolon; // Last ";" ends the `command` part

    DBG("Parsing command string for SET (shell-script) operation:\n\t%s", command_string);
    memset(res, 0, sizeof(parsed_operation_t));

    last_semicolon = strrchr(command_string, ';');

    if (last_semicolon == NULL)
    {
        ERROR("Parsing failed - missing semicolon ending the `command`");
        return EPS_GENERAL_ERROR;
    }

    if (last_semicolon == command_string + strlen(command_string) - 1)
    {
        WARN("Warning - shell SET method ends with semicolon (but it must not)");
    }

    /* Replace semicolon with terminating zero;
     * Save `command` and `subst_values` parts */
    last_semicolon[0]      =  0;
    res->command           =  command_string;
    subst_values           =  last_semicolon + 1;
    res->value_to_extract  =  NULL;

    if (!res->command || !strlen(trim(res->command)))
    {
        ERROR("Parsing failed - empty `command`");
        return EPS_GENERAL_ERROR;
    }

    if (!subst_values || !strlen(trim(subst_values)))
    {
        //DBG("No substitution values in command: %s", res->command);
        return EPS_OK;
    }

    /*DBG("\n Parsing command :\n\t%s\n Parsing subst_values :\n\t%s",
         res->command, subst_values);*/

    /* For each substitution value */
    for (t = strtok_r(subst_values, ",", &strtok_ctx); t ; t = strtok_r(NULL, ",", &strtok_ctx))
    {
        trim(t);

        /* Handle @name/@value subst values (if any) */
        if (!strcmp(t, "@name"))
        {
            res->subst_val[res->subst_val_num++].name_formatter = TRUE;
            continue;
        }
        else if (!strcmp(t, "@value"))
        {
            res->subst_val[res->subst_val_num++].value_formatter = TRUE;
            continue;
        }


        /* Take parameter name (object name + leaf name) */
        res->subst_val[res->subst_val_num].obj_name = strtok_r(t, "?", &strtok_ctx2);
        trim(res->subst_val[res->subst_val_num].obj_name);
        p_leaf_name = rstrstr(res->subst_val[res->subst_val_num].obj_name, ".");

        /* If there is a dot in this name, then if is a full object name */
        if (p_leaf_name)
        {
            p_leaf_name++;
            strcpy_safe(res->subst_val[res->subst_val_num].leaf_name,
                    p_leaf_name, sizeof(res->subst_val[res->subst_val_num].leaf_name));
            /* Cut leaf name from object name */
            *p_leaf_name = '\0';
        }
        else /* just leaf name */
        {
            strcpy_safe(res->subst_val[res->subst_val_num].leaf_name,
                    res->subst_val[res->subst_val_num].obj_name,
                    sizeof(res->subst_val[res->subst_val_num].leaf_name));
            res->subst_val[res->subst_val_num].obj_name = NULL;
        }

        /* If '?' found, then it is a conditional substitution (e.g. value?up:down) */
        cond = strtok_r(NULL, "?", &strtok_ctx2);
        if (cond)
        {
            res->subst_val[res->subst_val_num].conditional = TRUE;

            res->subst_val[res->subst_val_num].true_val = strtok_r(cond, ":", &strtok_ctx3);
            trim(res->subst_val[res->subst_val_num].true_val);

            res->subst_val[res->subst_val_num].false_val = strtok_r(NULL, ":", &strtok_ctx3);
            trim(res->subst_val[res->subst_val_num].false_val);
        }
        res->subst_val_num++;
    }

    /*DBG("Parsing results: num of values for substitution: %d", res->subst_val_num);
    for (i = 0; i < res->subst_val_num; i++)
    {
        if (res->subst_val[i].name_formatter)
            DBG("    @name formatter");
        else if (res->subst_val[i].value_formatter)
            DBG("    @value formatter");
        else
        {
            DBG("    obj name: %s, leaf name %s", res->subst_val[i].obj_name, res->subst_val[i].leaf_name);
            if (res->subst_val[i].conditional)
               DBG("Conditional substitution: %s : %s",res->subst_val[i].true_val, res->subst_val[i].false_val);
        }
    }*/

    return EPS_OK;
}

/*
 * Parses command string in the following format:
 *   <command with placeholders>; <substitution values (comma separated)>; <value to extract (optional)>
 */
static ep_stat_t w_parse_operation_string(oper_method_t method, char *command_string, parsed_operation_t *res)
{
    int i;
    char *strtok_ctx, *strtok_ctx2, *strtok_ctx3, *token, *token1, *token2, *strtok_ctx1;
    char *subst_values, *t, *p_leaf_name, *cond;

    DBG("Parsing command string for %s operation:\n\t%s",
                             methodType2string(method), command_string);
    memset(res, 0, sizeof(parsed_operation_t));

    res->command = strtok_r(command_string, ";", &strtok_ctx);
    subst_values = strtok_r(NULL, ";", &strtok_ctx);
    if (method == OP_GET)
        res->value_to_extract = strtok_r(NULL, ";", &strtok_ctx);
    else if (method == OP_ADDOBJ)
    {
        token = strtok_r(NULL, ";", &strtok_ctx);
        DBG("be keys string: %s", token);
        i = 0;
        if (token && strlen(token)>0)
        {
            token1 = strtok_r(token, ",", &strtok_ctx1); trim(token1);
            while (token1 && (strlen(token1) > 0))
            {
                token2 = strtok_r(token1, "=", &strtok_ctx2);
                token2 = strtok_r(NULL, "=", &strtok_ctx2); trim(token2);
                res->bekey_params[i++] = token2;
                //DBG(" bey key param %d: %s ", i, token2);
                token1 = strtok_r(NULL, ",", &strtok_ctx1); trim(token1);
            }
        }
        res->bekey_param_num = i;
    }

    if (!res->command)
    {
        ERROR("Parsing failed - %s method command must be specified",
               methodType2string(method));
        return EPS_GENERAL_ERROR;
    }

    if (!subst_values)
    {
        //DBG(" No substitution values in command: %s",  res->command);
        return EPS_OK;
    }

    trim(res->command);
    trim(subst_values);
    trim(res->value_to_extract);

    /*DBG("\n Parsing command :\n\t%s\n Parsing subst_values :\n\t%s",
         res->command, subst_values);*/

    if (res->value_to_extract && strlen(res->value_to_extract) == 0)
        res->value_to_extract = NULL;

    /* For each substitution value */
    for (t = strtok_r(subst_values, ",", &strtok_ctx); t ; t = strtok_r(NULL, ",", &strtok_ctx))
    {
        /* Take parameter name (object name + leaf name) */
        res->subst_val[res->subst_val_num].obj_name = strtok_r(t, "?", &strtok_ctx2);
        trim(res->subst_val[res->subst_val_num].obj_name);
        p_leaf_name = rstrstr(res->subst_val[res->subst_val_num].obj_name, ".");

        /* If there is a dot in this name, then if is a full object name */
        if (p_leaf_name)
        {
            p_leaf_name++;
            strcpy_safe(res->subst_val[res->subst_val_num].leaf_name,
                    p_leaf_name, sizeof(res->subst_val[res->subst_val_num].leaf_name));
            /* Cut leaf name from object name */
            *p_leaf_name = '\0';
        }
        else /* just leaf name */
        {
            strcpy_safe(res->subst_val[res->subst_val_num].leaf_name,
                    res->subst_val[res->subst_val_num].obj_name,
                    sizeof(res->subst_val[res->subst_val_num].leaf_name));
            res->subst_val[res->subst_val_num].obj_name = NULL;
        }

        /* If '?' found, then it is a conditional substitution (e.g. value?up:down) */
        cond = strtok_r(NULL, "?", &strtok_ctx2);
        if (cond)
        {
            res->subst_val[res->subst_val_num].conditional = TRUE;

            res->subst_val[res->subst_val_num].true_val = strtok_r(cond, ":", &strtok_ctx3);
            trim(res->subst_val[res->subst_val_num].true_val);

            res->subst_val[res->subst_val_num].false_val = strtok_r(NULL, ":", &strtok_ctx3);
            trim(res->subst_val[res->subst_val_num].false_val);
        }
        res->subst_val_num++;
    }

    DBG("Parsing results: num of values for substitution: %d", res->subst_val_num);
    /*
    for (i = 0; i < res->subst_val_num; i++)
    {
            DBG("    obj name: %s, leaf name %s", res->subst_val[i].obj_name, res->subst_val[i].leaf_name);
            if (res->subst_val[i].conditional)
               DBG("Conditional substitution: %s : %s",res->subst_val[i].true_val, res->subst_val[i].false_val);

    }*/

    return EPS_OK;
}

/*
 * Parses backend method string in the following format:
 * beObjName ; bekeyname=<mmxobj-inst-ref.leaf>, ...; <bekeyname = mmxleafname, .., ;
 */
static ep_stat_t w_parse_backend_method_string(oper_method_t method, char *method_string, parsed_backend_method_t *res)
{
    int i = 0;
    char *t, *strtok_ctx, *strtok_ctx1, *strtok_ctx2;
    char *token1, *token2;
    char *subst_values, *output_values;
    char *p_leaf_name;

    DBG("Parsing method string for %s operation: `%s'", methodType2string(method), method_string);
    memset(res, 0, sizeof(parsed_backend_method_t));

    /* The first part of the method string contains the object name according
       to the backend vision  */
    res->beObjName = strtok_r(method_string, ";", &strtok_ctx);

    /* The 2nd part of the method string contains mapping pairs of
       BE key params names according to backend to param path names according
       to MMX. These pairs used as the input parameters of the request*/
    subst_values = strtok_r(NULL, ";", &strtok_ctx);

    /* The 3rd part of the method string is mapping pairs (BE name to MMX name)
       used to interpret the output parameters returned by the backend.    */
    output_values = strtok_r(NULL, ";", &strtok_ctx);

    if (!res->beObjName )
        return EPS_GENERAL_ERROR;

    trim(res->beObjName);
    trim(subst_values);
    trim(output_values);

    /* For each key param mapping pair (beName = MMXpathName) */
    i = 0;
    for (t = strtok_r(subst_values, ",", &strtok_ctx); t ; t = strtok_r(NULL, ",", &strtok_ctx))
    {
        trim(t);

        /* Take backend key name (used as "input key parameter" parameter */
        res->subst_val[i].backend_key_name = strtok_r(t, "=", &strtok_ctx2);
        trim(res->subst_val[i].backend_key_name);

        /* Take substitution for this name (mmx object name + leaf name)*/
        res->subst_val[i].mmx_subst_val.obj_name = strtok_r(NULL, "=", &strtok_ctx2);
        trim(res->subst_val[i].mmx_subst_val.obj_name);
        p_leaf_name = rstrstr(res->subst_val[i].mmx_subst_val.obj_name, ".");

        /* If there is a dot in this name, then if is a full object name */
        if (p_leaf_name)
        {
            p_leaf_name++;
            strcpy_safe(res->subst_val[i].mmx_subst_val.leaf_name,
                                        p_leaf_name, MAX_LEAF_NAME_LEN);
            *p_leaf_name = '\0'; /* Cut leaf name from object name */
        }
        else /* just leaf name */
        {
            strcpy_safe(res->subst_val[i].mmx_subst_val.leaf_name,
                    res->subst_val[i].mmx_subst_val.obj_name,
                    sizeof(res->subst_val[i].mmx_subst_val.leaf_name));
            res->subst_val[i].mmx_subst_val.obj_name = NULL;
        }

        // TODO ?
//        /* If '?' found, then it is a conditional substitution (e.g. value?up:down) */
//        char *cond = strtok_r(NULL, "?", &strtok_ctx2);
//        if (cond)
//        {
//            res->subst_val[i].conditional = TRUE;
//            res->subst_val[i].true_val = strtok_r(cond, ":", &strtok_ctx3);
//            res->subst_val[i].false_val = strtok_r(NULL, ":", &strtok_ctx3);
//        }
        i++;
    }
    res->subst_val_num = i;

    /* Now process the third part of the method string*/
    if (method == OP_ADDOBJ)
    {
        //DBG("Output BE keys string: %s", output_values);
        i = 0;
        if (output_values && strlen(output_values)>0)
        {
            token1 = strtok_r(output_values, ",", &strtok_ctx1); trim(token1);
            while (token1 && (strlen(token1) > 0))
            {
                token2 = strtok_r(token1, "=", &strtok_ctx2);
                token2 = strtok_r(NULL, "=", &strtok_ctx2); trim(token2);
                res->bekey_params[i] = token2;
                //DBG(" BE key param %d: %s ", i, token2);
                token1 = strtok_r(NULL, ",", &strtok_ctx1); trim(token1);
                i++;
            }
        }
        res->bekey_param_num = i;
    }

    /* Debug print */
    /*DBG ("Parsing results: beobjname = %s, subst val num = %d, output key nums = %d",
         res->beObjName, res->subst_val_num, res->bekey_param_num);*/
    for (i = 0; i < res->subst_val_num; i++)
    {
        /*DBG("   input BE key name: %s, subst obj: %s, leaf: %s",
            res->subst_val[i].backend_key_name, res->subst_val[i].mmx_subst_val.obj_name,
            res->subst_val[i].mmx_subst_val.leaf_name);*/
    }
    for (i = 0; i < res->bekey_param_num; i++)
    {
        /*DBG("   output BE key name: %s", res->bekey_params[i]);*/
    }

    return EPS_OK;
}


/**************************************************************************/
/*! \fn static ep_stat_t w_form_getall_subst_sql_select(worker_data_t *wd
                , parsed_param_name_t *pn
                , obj_info_t *obj_info
                , char *query
                , size_t query_size
                , char **idx_params
                , int idx_params_num, parsed_backend_method_t *parsed_ubus_str)
 *  \brief Prepare query to main database with substitution
 *  \param[in] worker_data_t *wd // Main structure for saving databases, sockets, etc
 *  \param[in] parsed_param_name_t *pn // Parsed parameter name from request string
 *  \param[in] obj_info_t *obj_info // Information about object
 *  \param[out] char *query // Prepared query for execution
 *  \param[in] size_t query_size // Size of auery buffer
 *  \param[in] char **idx_params //
 *  \param[in] int idx_params_num //
 *  \param[in] parsed_backend_method_t *parsed_ubus_str // Contains arguments for backend command
 *  \param[out] int *beRestart // Restart backand if need it
 *  \return EPS_OK
 */
/**************************************************************************/
static ep_stat_t w_form_getall_subst_sql_select(worker_data_t *wd
                , parsed_param_name_t *pn
                , obj_info_t *obj_info
                , char *query
                , size_t query_size
                , char **idx_params
                , int idx_params_num, parsed_backend_method_t *parsed_ubus_str)
{
    int  i, j, n;
    int  min_idx_num, s_obj_num;
    obj_info_t s_obj_info;
    parsed_param_name_t s_pn;

    memset(query, 0, query_size);

    if (parsed_ubus_str->subst_val_num == 0)
        return EPS_OK;

    strcpy_safe(query, "SELECT ", query_size);
    for (j = 0; j < idx_params_num; j ++)
    {
        if ( pn->indices[j].type == REQ_IDX_TYPE_EXACT ||
             pn->indices[j].type == REQ_IDX_TYPE_RANGE)
        {
            sprintf(query+strlen(query), "t%d.[", j);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "],", query_size);
        }
    }
    for (j = 0; j < parsed_ubus_str->subst_val_num; j++)
    {
        if ( pn->indices[j].type == REQ_IDX_TYPE_EXACT ||
             pn->indices[j].type == REQ_IDX_TYPE_RANGE)
        {
            if (parsed_ubus_str->subst_val[j].mmx_subst_val.name_formatter ||
                parsed_ubus_str->subst_val[j].mmx_subst_val.value_formatter)
            {
                /* @name/@value subst values are just ignored,
                * as it is not relevant for the SQL query */
                continue;
            }

            sprintf(query+strlen(query), "t%d.[", j);

            strcat_safe(query
            , parsed_ubus_str->subst_val[j].mmx_subst_val.leaf_name, query_size);

            strcat_safe(query, "],", query_size);
        }
    }
    LAST_CHAR(query) = '\0'; /* remove last symbol (comma or space) */

    if (!strcmp(query, "SELECT"))
    {
        /* No substitutions values that require SQL query to be perfomed
         * (for example: only @name and/or @value are assigned to the
         *  command, theirs susbtituions values are fetched right from
         *  the management request and not from DB) */
        DBG("Empty SELECT query - no SQL query needed for substitution values");
        memset(query, 0, query_size); // Query must be set empty

        return EPS_OK;
    }

    for (i = 0; i < parsed_ubus_str->subst_val_num; i++)
    {
        if (parsed_ubus_str->subst_val[j].mmx_subst_val.name_formatter ||
            parsed_ubus_str->subst_val[j].mmx_subst_val.value_formatter)
        {
            /* @name/@value subst values are just ignored,
            * as it is not relevant for the SQL query */
            continue;
        }

        if (parsed_ubus_str->subst_val[i].mmx_subst_val.obj_name)
        {
            /* get name of values table of object used for substitution*/
            strcpy_safe(s_pn.obj_name
            , parsed_ubus_str->subst_val[i].mmx_subst_val.obj_name
            , sizeof(s_pn.obj_name));

            strcpy_safe(s_pn.leaf_name
            , parsed_ubus_str->subst_val[i].mmx_subst_val.leaf_name
            , sizeof(s_pn.leaf_name));

            s_pn.index_num = pn->index_num;
            memcpy(s_pn.indices, pn->indices, sizeof(s_pn.indices));
            s_pn.partial_path = FALSE;

            /* Get obj info and determine number of indeces in the "subst" object.
            We'll use the minimal number of indeces to form INNER JOIN clause*/
            w_get_obj_info(wd, &s_pn, 0, 0, &s_obj_info, 1, &s_obj_num);

            n = (s_obj_num > 0) ?  w_num_of_obj_indeces(s_obj_info.objName) : 0;

            min_idx_num = (n < idx_params_num) ? n : idx_params_num;

            DBG("%d obj retrieved for subst %d; num of indeces %d, objName %s, leafName %s"
            ,s_obj_num
            , i
            , n
            , parsed_ubus_str->subst_val[i].mmx_subst_val.obj_name
            , parsed_ubus_str->subst_val[i].mmx_subst_val.leaf_name);
            if ((min_idx_num > 0) && (s_obj_num > 0))
            {
                if(i > 0)
                {
                    // TODO:
                    DBG("not support inner join for base table now");
                }
                else
                {
                    strcat_safe(query, " FROM ", query_size);
                    strcat_safe(query, s_obj_info.objValuesTblName, query_size);
                    sprintf(query+strlen(query), " AS t%d", i);
                }
            }
        }
        else
        {
            if(i > 0)
            {
                // TODO:
                DBG("not support inner join for base table now");
            }
            else
            {
                strcat_safe(query, " FROM ", query_size);
                strcat_safe(query, obj_info->objValuesTblName, query_size);
                sprintf(query+strlen(query), " AS t%d", i);
            }
        }
    }

    strcat_safe(query, " WHERE 1 AND ", query_size);
    for (j = 0; j < idx_params_num; j++)
    {
        if (pn->indices[j].type == REQ_IDX_TYPE_EXACT)
        {
            /* concatenate PARAM=ARG */
            sprintf(query+strlen(query), "t%d.[", j);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].exact_val.num);
            strcat_safe(query, " AND ", query_size);
        }
        else if (pn->indices[j].type == REQ_IDX_TYPE_RANGE)
        {
            /* concatenate (PARAM>=RANGE_BEGIN AND PARAM<=RANGE_END) */
            strcat_safe(query, "(", query_size);
            sprintf(query+strlen(query), "t%d.[", j);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]>=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].range_val.begin);
            strcat_safe(query, " AND ", query_size);
            sprintf(query+strlen(query), "t%d.[", j);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]<=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].range_val.end);
            strcat_safe(query, ")", query_size);
            strcat_safe(query, " AND ", query_size);
        }
    }
    query[strlen(query)-5] = '\0'; /* Remove last " AND " */
    return EPS_OK;
}

/*
 * Builds SELECT SQL request that retrieves values for substitution
 * If there is no parameter for substitution, do nothing - just return OK
 */
static ep_stat_t w_form_subst_sql_select(worker_data_t *wd, parsed_param_name_t *pn,
        obj_info_t *obj_info, char *query, size_t query_size, char **idx_params,
        int idx_params_num, parsed_operation_t *parsed_ubus_str)
{
    int  i, j, n;
    int  min_idx_num, s_obj_num;
    obj_info_t s_obj_info;
    parsed_param_name_t s_pn;

    /*DBG("Num of index params = %d, Num of subst params = %d",
         idx_params_num, parsed_ubus_str->subst_val_num ); */

    memset(query, 0, query_size);

    if (parsed_ubus_str->subst_val_num == 0)
        return EPS_OK;

    strcpy_safe(query, "SELECT ", query_size);
    for (j = 0; j < idx_params_num; j ++)
    {
        strcat_safe(query, "t.[", query_size);
        strcat_safe(query, idx_params[j], query_size);
        strcat_safe(query, "],", query_size);
    }
    for (j = 0; j < parsed_ubus_str->subst_val_num; j++)
    {
        if (parsed_ubus_str->subst_val[j].name_formatter ||
            parsed_ubus_str->subst_val[j].value_formatter)
        {
            /* @name/@value subst values are just ignored,
             * as it is not relevant for the SQL query */
            continue;
        }

        if (parsed_ubus_str->subst_val[j].obj_name)
            sprintf(query+strlen(query), "t%d.", j);
        else
            sprintf(query+strlen(query), "t.", j);
        strcat_safe(query, "[", query_size);
        strcat_safe(query, parsed_ubus_str->subst_val[j].leaf_name, query_size);
        strcat_safe(query, "],", query_size);
    }
    LAST_CHAR(query) = '\0'; /* remove last symbol (comma or space) */

    if (!strcmp(query, "SELECT"))
    {
        /* No substitutions values that require SQL query to be perfomed
         * (for example: only @name and/or @value are assigned to the
         *  command, theirs susbtituions values are fetched right from
         *  the management request and not from DB) */
        DBG("Empty SELECT query - no SQL query needed for substitution values");
        memset(query, 0, query_size); // Query must be set empty

        return EPS_OK;
    }

    strcat_safe(query, " FROM ", query_size);
    strcat_safe(query, obj_info->objValuesTblName, query_size);
    strcat_safe(query, " AS t ", query_size);

    for (i = 0; i < parsed_ubus_str->subst_val_num; i++)
    {
        if (parsed_ubus_str->subst_val[j].name_formatter ||
            parsed_ubus_str->subst_val[j].value_formatter)
        {
            /* @name/@value subst values are just ignored,
             * as it is not relevant for the SQL query */
            continue;
        }

        if (parsed_ubus_str->subst_val[i].obj_name)
        {
            /* get name of values table of object used for substitution*/
            strcpy_safe(s_pn.obj_name, parsed_ubus_str->subst_val[i].obj_name, sizeof(s_pn.obj_name));
            strcpy_safe(s_pn.leaf_name, parsed_ubus_str->subst_val[i].leaf_name, sizeof(s_pn.leaf_name));
            s_pn.index_num = pn->index_num;
            memcpy(s_pn.indices, pn->indices, sizeof(s_pn.indices));
            s_pn.partial_path = FALSE;

            /* Get obj info and determine number of indeces in the "subst" object.
               We'll use the minimal number of indeces to form INNER JOIN clause*/
            w_get_obj_info(wd, &s_pn, 0, 0, &s_obj_info, 1, &s_obj_num);
            n = (s_obj_num > 0) ?  w_num_of_obj_indeces(s_obj_info.objName) : 0;
            min_idx_num = (n < idx_params_num) ? n : idx_params_num;

            DBG("%d obj retrieved for subst %d; num of indeces %d, objName %s, leafName %s",
                s_obj_num, i, n, parsed_ubus_str->subst_val[i].obj_name, parsed_ubus_str->subst_val[i].leaf_name);

            if ((min_idx_num > 0) && (s_obj_num > 0))
            {
                strcat_safe(query, "INNER JOIN ", query_size);
                strcat_safe(query, s_obj_info.objValuesTblName, query_size);
                sprintf(query+strlen(query), " AS t%d ON (", i);
                for (j = 0; j < min_idx_num; j ++)
                {
                    strcat_safe(query, "t.[", query_size);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "]=", query_size);
                    sprintf(query+strlen(query), "t%d.[", i);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "] AND ", query_size);
                }
                query[strlen(query)-5] = '\0'; /* Remove last " AND " */
                strcat_safe(query, ")", query_size);
            }
        }
    }
    strcat_safe(query, " WHERE 1 AND ", query_size);
    for (j = 0; j < idx_params_num; j++)
    {
        if (pn->indices[j].type == REQ_IDX_TYPE_EXACT)
        {
            /* concatenate PARAM=ARG */
            strcat_safe(query, "t.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].exact_val.num);
            strcat_safe(query, " AND ", query_size);
        }
        else if (pn->indices[j].type == REQ_IDX_TYPE_RANGE)
        {
            /* concatenate (PARAM>=RANGE_BEGIN AND PARAM<=RANGE_END) */
            strcat_safe(query, "(", query_size);
            strcat_safe(query, "t.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]>=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].range_val.begin);
            strcat_safe(query, " AND ", query_size);
            strcat_safe(query, "t.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]<=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].range_val.end);
            strcat_safe(query, ")", query_size);
            strcat_safe(query, " AND ", query_size);
        }
    }
    query[strlen(query)-5] = '\0'; /* Remove last " AND " */
    return EPS_OK;
}

/*
 * Builds SELECT SQL request that retrieves values for substitution
 */
static ep_stat_t w_form_subst_sql_select_backend(worker_data_t *wd, parsed_param_name_t *pn,
        obj_info_t *obj_info, char *query, size_t query_size, char **idx_params,
        int idx_params_num, parsed_backend_method_t *parsed_backend_str)
{
    int i = 0, j = 0, n;
    int min_idx_num, s_obj_num;
    obj_info_t s_obj_info;
    parsed_param_name_t s_pn;

    /* For debugging only */
    /*DBG("Num of index params = %d, Num of subst params = %d",
         idx_params_num, parsed_backend_str->subst_val_num ); */

    strcpy_safe(query, "SELECT ", query_size);
    for (j = 0; j < idx_params_num; j++)
    {
        strcat_safe(query, "t.[", query_size);
        strcat_safe(query, idx_params[j], query_size);
        strcat_safe(query, "],", query_size);
        /* For debugging only */
        //DBG("idx = %d, index param name = %s", j, idx_params[j]);
    }
    for (j = 0; j < parsed_backend_str->subst_val_num; j++)
    {
        if (parsed_backend_str->subst_val[j].mmx_subst_val.obj_name)
        {
            sprintf(query+strlen(query), "t%d.", j);
            /* For debugging only */
            //DBG("subst obj name = %s",parsed_backend_str->subst_val[j].mmx_subst_val.obj_name);
        }
        else
            strcat_safe(query, "t.", query_size);

        strcat_safe(query, "[", query_size);
        strcat_safe(query, parsed_backend_str->subst_val[j].mmx_subst_val.leaf_name, query_size);
        strcat_safe(query, "],", query_size);
    }
    LAST_CHAR(query) = '\0'; /* remove last comma */

    strcat_safe(query, " FROM ", query_size);
    strcat_safe(query, obj_info->objValuesTblName, query_size);
    strcat_safe(query, " AS t ", query_size);

    for (i = 0; i < parsed_backend_str->subst_val_num; i++)
    {
        if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name)
        {
            /* Retieve obj info and values tbl name for substitute object*/
            strcpy_safe(s_pn.obj_name, parsed_backend_str->subst_val[i].mmx_subst_val.obj_name, sizeof(s_pn.obj_name));
            strcpy_safe(s_pn.leaf_name, parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name, sizeof(s_pn.leaf_name));
            s_pn.index_num = pn->index_num;
            memcpy(s_pn.indices, pn->indices, sizeof(s_pn.indices));
            s_pn.partial_path = FALSE;

            w_get_obj_info(wd, &s_pn, 0, 0, &s_obj_info, 1, &s_obj_num);
            //DBG("%d objects retrieved", s_obj_num);
            n = (s_obj_num > 0) ?  w_num_of_obj_indeces(s_obj_info.objName) : 0;
            min_idx_num = (n < idx_params_num) ? n : idx_params_num;

            if ((min_idx_num > 0) && (s_obj_num > 0)) {
                strcat_safe(query, "INNER JOIN ", query_size);
                strcat_safe(query, s_obj_info.objValuesTblName, query_size);
                sprintf(query+strlen(query), " AS t%d ON (", i);
                // TODO idx_params of subst table t0
                for (j = 0; j < min_idx_num; j ++)
                {
                    strcat_safe(query, "t.[", query_size);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "]=", query_size);
                    sprintf(query+strlen(query), "t%d.[", i);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "] AND ", query_size);
                }
                query[strlen(query)-5] = '\0'; /* Remove last " AND " */
                strcat_safe(query, ") ", query_size);
            }
        }
    }
    strcat_safe(query, " WHERE 1 AND ", query_size);
    for (j = 0; j < idx_params_num; j++)
    {
        if (pn->indices[j].type == REQ_IDX_TYPE_EXACT)
        {
            /* concatenate PARAM=ARG */
            strcat_safe(query, "t.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].exact_val.num);
            strcat_safe(query, " AND ", query_size);
        }
        else if (pn->indices[j].type == REQ_IDX_TYPE_RANGE)
        {
            /* concatenate (PARAM>=RANGE_BEGIN AND PARAM<=RANGE_END) */
            strcat_safe(query, "(", query_size);
            strcat_safe(query, "t.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]>=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].range_val.begin);
            strcat_safe(query, " AND ", query_size);
            strcat_safe(query, "t.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]<=", query_size);
            sprintf(query+strlen(query), "%d", pn->indices[j].range_val.end);
            strcat_safe(query, ")", query_size);
            strcat_safe(query, " AND ", query_size);
        }
    }
    query[strlen(query)-5] = '\0'; /* Remove last " AND " */
    return EPS_OK;
}


static ep_stat_t w_get_values_uci(worker_data_t *wd, ep_message_t *answer,
                                  parsed_param_name_t *pn,
                                  obj_info_t *obj_info, sqlite3 *obj_db_conn,
                                  param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK;
    int i, j, param_cnt = 0, res;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char  buf[EP_SQL_REQUEST_BUF_SIZE], query[EP_SQL_REQUEST_BUF_SIZE];
    sqlite3_stmt *stmt = NULL;
    char *p_extr_param;
    FILE *fp;

    /* Save names of all index parameters of the object */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    if (idx_params_num != pn->index_num)
        DBG("Not full set of indeces is used for uci GET request (%d of %d)", pn->index_num, idx_params_num);

    for (i = 0; i < param_num; i++)
    {
        if ((obj_info->getOperStyle == OP_STYLE_UCI) ||
            (!pn->partial_path && !strcmp(pn->leaf_name, param_info[i].paramName) && param_info[i].getOperStyle == OP_STYLE_UCI) ||
            (pn->partial_path && param_info[i].getOperStyle == OP_STYLE_UCI))
        {
            /* Check if this parameter is allowed to be read by the caller */
            if (!paramReadAllowed(param_info, i, answer->header.callerId))
            {
                /*DBG("Param %s isn't allowed to be read by this requestor (%d)",
                     param_info[i].paramName, answer->header.callerId);*/
                continue;
            }

            if (strlen(param_info[i].getMethod) > 0 )
            {
                parsed_operation_t parsed_uci_str;
                w_parse_operation_string(OP_GET, param_info[i].getMethod, &parsed_uci_str);

                w_form_subst_sql_select(wd, pn, obj_info, query, sizeof(query), idx_params, idx_params_num, &parsed_uci_str);

                if (strlen(query) > 0)
                {
                   DBG("query:\n%s", query);
                   if (sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL) != SQLITE_OK)
                       GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s", sqlite3_errmsg(obj_db_conn));
                }

                while (TRUE)
                {
                    res = sqlite3_step(stmt);

                    if ((res == SQLITE_ROW) || (strlen(query) == 0))
                    {
                        strcpy_safe(buf, "uci get ", sizeof(buf));
                        w_form_call_str(buf+strlen(buf), sizeof(buf), &parsed_uci_str, stmt, idx_params_num);
                        DBG("%s", buf);

                        /* Save all selected index values */
                        for (j = 0; j < idx_params_num; j++)
                            idx_values[j] = sqlite3_column_int(stmt, j);

                        if (!(fp = popen(buf, "r")))
                            GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not execute uci");

                        p_extr_param = fgets(buf, sizeof(buf)-1, fp);
                        pclose(fp);

                        trim(p_extr_param);
                        if (!p_extr_param || (strlen(p_extr_param) == 0) ||
                            (param_info[i].hidden == TRUE))
                        {
                            p_extr_param = "";
                        }

                        DBG("Got from UCI: %s",p_extr_param);

                        w_insert_value_to_answer(wd, answer, obj_info->objName,
                                 idx_values, idx_params_num, param_info[i].paramName,
                                 (char *)db2soap((char *)p_extr_param, param_info[i].paramType));

                        param_cnt++;

                        if (strlen(query) == 0)
                            break;

                    }
                    else if (res == SQLITE_DONE)
                        break;
                    else
                        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(obj_db_conn));
                }
            }
        }
    }

ret:
    if (param_cnt > 0) DBG(" %d parameters were processed", param_cnt);

    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/* TODO: This function should be tested!!! */
static ep_stat_t w_get_values_ubus(worker_data_t *wd, ep_message_t *answer,
                                   parsed_param_name_t *pn,
                                   obj_info_t *obj_info, sqlite3 *obj_db_conn,
                                   param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK;
    int  i, j, res, param_cnt = 0;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char buf[EP_SQL_REQUEST_BUF_SIZE], query[EP_SQL_REQUEST_BUF_SIZE];
    char *p_extr_param = NULL;
    FILE *fp;
    parsed_operation_t parsed_ubus_str;
    sqlite3_stmt *stmt = NULL;

    /* Save names of all index parameters of the object */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /*if (idx_params_num != pn->index_num)
        DBG("Not full set of indeces is used for ubus GET request (%d of %d)", pn->index_num, idx_params_num);*/

    for (i = 0; i < param_num; i++)
    {
        if ((obj_info->getOperStyle == OP_STYLE_UBUS) ||
            (!pn->partial_path && !strcmp(pn->leaf_name, param_info[i].paramName) && param_info[i].getOperStyle == OP_STYLE_UBUS) ||
            (pn->partial_path && param_info[i].getOperStyle == OP_STYLE_UBUS))
        {
            /* Check if this parameter is allowed to be read by the caller */
            if (!paramReadAllowed(param_info, i, answer->header.callerId))
            {
                /*DBG("Param %s isn't allowed to be read by this requestor (%d)",
                     param_info[i].paramName, answer->header.callerId);*/
                continue;
            }

            if (strlen(param_info[i].getMethod) == 0)
            {
                DBG("ubus get-method string is not presented for param %s", param_info[i].paramName);
                continue;
            }
            w_parse_operation_string(OP_GET, param_info[i].getMethod, &parsed_ubus_str);
            w_form_subst_sql_select(wd, pn, obj_info, query, sizeof(query), idx_params, idx_params_num, &parsed_ubus_str);

            if (strlen(query) > 0)
            {
                DBG("query:\n%s", query);
                if (sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL) != SQLITE_OK)
                    GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                        sqlite3_errmsg(obj_db_conn));
            }

            while (TRUE)
            {
                res = sqlite3_step(stmt);
                if ((res == SQLITE_ROW) || (strlen(query) == 0))
                {
                    /* Save all selected index values */
                    for (j = 0; j < idx_params_num; j++)
                        idx_values[j] = sqlite3_column_int(stmt, j);

                    strcpy_safe(buf, "ubus call ", sizeof(buf));
                    w_form_call_str(buf+strlen(buf), sizeof(buf), &parsed_ubus_str, stmt, idx_params_num);
                    DBG("%s", buf);

                    fp = popen(buf, "r");
                    if (!fp)
                        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not execute ubus");

                    p_extr_param = NULL;
                    while (fgets(buf, sizeof(buf)-1, fp) != NULL)
                    {
                        p_extr_param = strstr(buf, parsed_ubus_str.value_to_extract);
                        if (p_extr_param)
                        {
                            /* Format: "key": "value",\n */
                            p_extr_param = strstr(buf, " ") + 1;
                            if (p_extr_param[strlen(p_extr_param)-1] == '\n')
                                p_extr_param[strlen(p_extr_param)-1] = '\0';
                            if (p_extr_param[strlen(p_extr_param)-1] == ',')
                                p_extr_param[strlen(p_extr_param)-1] = '\0';
                            trim_quotes(p_extr_param);
                            break;
                        }
                    }
                    pclose(fp);

                    DBG("Got from ubus: name %s, value %s", parsed_ubus_str.value_to_extract, p_extr_param);

                    w_insert_value_to_answer(wd, answer, obj_info->objName,
                            idx_values, idx_params_num, param_info[i].paramName,
                            (char *)db2soap((char *)p_extr_param, param_info[i].paramType));
                    param_cnt++;

                    if (strlen(query) == 0)
                        break;
                }
                else if (res == SQLITE_DONE)
                    break;
                else
                    GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(obj_db_conn));
            } // End of while over DB rows
        }  // End of "if get-style is ubus
    } // End of "for" stmt over parameters

ret:
    if (param_cnt > 0) DBG(" %d parameters were processed", param_cnt);

    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/**************************************************************************/
/*! \fn static ep_stat_t w_prepare_getall_command(worker_data_t *wd
                 , parsed_param_name_t *pn
                 , obj_info_t *obj_info,
                 , parsed_backend_method_t *p_parsed_script_str
                 , char *idx_params[], int *idx_values, int idx_params_num
                 , char *cmd_buf, int cmd_buf_len)
 *  \brief Prepare command for execution backend script
 *  \param[in] worker_data_t *wd // Main structure for saving databases, sockets, etc
 *  \param[in] parsed_param_name_t *pn // Parsed parameter name from request string
 *  \param[in] obj_info_t *obj_info // Information about object
 *  \param[in] parsed_backend_method_t *p_parsed_script_str // Contains arguments for backend command
 *  \param[in] char *idx_params[] //
 *  \param[in] int *idx_values //
 *  \param[in] int idx_params_num //
 *  \param[out] char *cmd_buf // Ready command for execution
 *  \param[in] int cmd_buf_len // cmd_buf size
 *  \return EPS_OK if success or error code
 */
/**************************************************************************/
static ep_stat_t w_prepare_getall_command(worker_data_t *wd
                 , parsed_param_name_t *pn
                 , obj_info_t *obj_info
                 , parsed_backend_method_t *p_parsed_script_str
                 , char *idx_params[], int *idx_values, int idx_params_num
                 , char *cmd_buf, int cmd_buf_len)
{
    ep_stat_t           status       = EPS_OK;
    int                 i            = 0;
    int                 res          = 0;
    sqlite3_stmt       *stmt         = NULL;
    sqlite3            *obj_db_conn  = wd->main_conn;
    parsed_operation_t  parsed_operation;
    char                query[EP_SQL_REQUEST_BUF_SIZE];

    memset(cmd_buf, 0, cmd_buf_len);

    status = w_parse_operation_string(OP_GETALL
             , obj_info->getAllMethod, &parsed_operation);

    if (status)
    {
        GOTO_RET_WITH_ERROR(status, "Couldn't execute w_parse_operation_string");
    }

    w_form_getall_subst_sql_select(wd
    , pn
    , obj_info
    , query, sizeof(query), idx_params, idx_params_num, p_parsed_script_str);

    if (strlen(query) == 0)
    {
        /* There is method string without substitutions */
        w_form_getall_call_str(cmd_buf
        , cmd_buf_len
        , p_parsed_script_str,NULL, idx_params_num, pn, &parsed_operation);

        return EPS_OK;
    }
    else
    {
        DBG("Query to select values for substitution (len=%d):\n\t%s"
        , strlen(query), query);

        res = sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL);

        if (res != SQLITE_OK)
        {
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR
            , "Could not prepare SQL statement: %s", sqlite3_errmsg(obj_db_conn));
        }
    }

    res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
    {
        /* Substitute the selected values in the method string */

        w_form_getall_call_str(cmd_buf
        , cmd_buf_len
        , p_parsed_script_str, stmt, idx_params_num, pn, &parsed_operation);

        /* Save all selected index values */
        if (idx_values != NULL)
        {
            for (i = 0; i < idx_params_num; i++)
            {
                idx_values[i] = sqlite3_column_int(stmt, i);
            }
        }
    }
    else if (res == SQLITE_DONE)  //I.e. no row was selected from db
    {
        status = EPS_EMPTY;
        DBG("No values for substitution");
    }
    else // SQL error
    {
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR
        , "Couldn't execute query to select subst values (%d): %s"
        , res, sqlite3_errmsg(obj_db_conn));
    }

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/*  Function w_prepare_command
 *  forms command (shell script, uci or ubus command) based on method string
 *  from obj_info (specified in cmd_buf) and substitution parameters.
 *  Values for substitution parameters are selected from values DB and
 *  placed into command string.
 *  Resulting command string contains all needed substitution values.
 *  Additionally this function saves values of the index parameters.
 *   Output parameters:
 *     cmd_buff, cmd_buf_len - prepared command string
 *     idx_values            - valus of index parameters
 *     p_stmt                - sqlite3 statement  (see note below)
 *  Important note:
 *  The function works as itterator going over all "DB rows" according to the
 *  substutution parameters.
 *  Parameter p_stmt contains sqlite statement for SELECT operation; on each
 *  call the next "DB row" is processed (by sqlite3_step).
 *  At the first call p_stmt is set to NULL by the caller, at the next calls
 *  p_stmt is not changed by the caller.
 *  At the end of the last call p_stmt is set to NULL by this procedure.
 * */
static ep_stat_t w_prepare_command(worker_data_t *wd, parsed_param_name_t *pn,
                          obj_info_t *obj_info, sqlite3 *obj_db_conn,
                          parsed_operation_t *p_parsed_script_str,
                          char *idx_params[], int *idx_values, int idx_params_num,
                          char *cmd_buf, int cmd_buf_len, sqlite3_stmt **p_stmt)
{
    char         query[EP_SQL_REQUEST_BUF_SIZE];
    int          i, res;
    BOOL         first_time = FALSE;
    ep_stat_t    status = EPS_OK;

    sqlite3_stmt *stmt = NULL;

    memset(cmd_buf, 0, cmd_buf_len);

    /* If stmt is null, this is the first call, so prepare query and stmt */
    if (*p_stmt == NULL)
    {
        first_time = TRUE;
        w_form_subst_sql_select(wd, pn, obj_info, query, sizeof(query), idx_params,
                                idx_params_num, p_parsed_script_str);
        if (strlen(query) == 0)
        {
            /* There is method string without substitutions */
            w_form_call_str(cmd_buf, cmd_buf_len, p_parsed_script_str, NULL, idx_params_num);
            return EPS_OK;
        }
        else
        {
            DBG("Query to select values for substitution (len=%d):\n\t%s", strlen(query), query);
            if ((res = sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL)) != SQLITE_OK)
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                        sqlite3_errmsg(obj_db_conn));
        }
    }
    else  //This is not first call, so we continue to work with the existing sqlite stmt
    {
        stmt = *p_stmt;
    }

    res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
    {
        /* Substitute the selected values in the method string */
        w_form_call_str(cmd_buf, cmd_buf_len, p_parsed_script_str, stmt, idx_params_num);

        /* Save all selected index values */
        if (idx_values != NULL)
        {
            for (i = 0; i < idx_params_num; i++)
                idx_values[i] = sqlite3_column_int(stmt, i);
        }
    }
    else if (res == SQLITE_DONE)  //I.e. no row was selected from db
    {
        if (first_time)
        {
            status = EPS_EMPTY;
            DBG("No values for substitution");
        }
        else
            status = EPS_NOTHING_DONE;  //It was the last ("neutral") itteration
    }
    else // SQL error
    {
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Couldn't execute query to select subst values (%d): %s",
                             res, sqlite3_errmsg(obj_db_conn));
    }

ret:
    //DBG("Prepared command (status %d): \n\t%s", status, cmd_buf);
    if (res == SQLITE_ROW)
    {
        //DBG("Update p_stmt for the next itteration");
        *p_stmt = stmt;
    }
    else
    {
        //DBG("Last itteration: set p_stmt to NULL");
        *p_stmt = NULL;
        if (stmt) sqlite3_finalize(stmt);
    }
    return status;
}

/*  Function w_prepare_command2
 *  similar to function w_prepare_command() - it also forms command
 *  (shell script, uci or ubus command) based on method string
 *  from obj_info (specified in cmd_buf) and substitution parameters.
 *  But also it has 2 distinctions from function w_prepare_command():
 *   1) it does not save values of the index parameters - as values of
 *      index parameters are already set up in input (parsed_param_name_t *pn)
 *   2) it uses internal sqlite3 statement to run query to fetch all needed
 *      substitution values for exactly defined (all index values are known)
 *      parameter name (pn) while function w_prepare_command() uses input
 *      (sqlite3_stmt **p_stmt) to run the same query
 *
 * TODO TODO TODO
 *   - avoid two similar functions - make the generic one;
 *   - optimize function to require less resources.
 */
static ep_stat_t w_prepare_command2(worker_data_t *wd, parsed_param_name_t *pn,
                          obj_info_t *obj_info, sqlite3 *dbconn,
                          parsed_operation_t *p_parsed_script_str,
                          char *idx_params[], int idx_params_num,
                          char *cmd_buf, int cmd_buf_len)
{
    char         query[EP_SQL_REQUEST_BUF_SIZE];
    int          step, res;
    ep_stat_t    status = EPS_OK;

    sqlite3_stmt *stmt = NULL;

    memset(cmd_buf, 0, cmd_buf_len);

    /* Prepare query and stmt */
    w_form_subst_sql_select(wd, pn, obj_info, query, sizeof(query), idx_params,
                            idx_params_num, p_parsed_script_str);

    if (strlen(query) == 0)
    {
        /* There is method string without substitutions */
        w_form_call_str(cmd_buf, cmd_buf_len, p_parsed_script_str, NULL, idx_params_num);
        return EPS_OK;
    }

    DBG("Query to select values for substitution (len=%d):\n\t%s", strlen(query), query);
    if ((res = sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL)) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                sqlite3_errmsg(dbconn));

    /* Running loop to fetch rows from db, actually single (step) row is expected */
    step = 0;
    while (TRUE)
    {
        res = sqlite3_step(stmt);

        if (res == SQLITE_ROW)
        {
            if (step == 0)
            {
                /* Substitute the selected values in the method string */
                w_form_call_str(cmd_buf, cmd_buf_len, p_parsed_script_str, stmt, idx_params_num);
            }
            else
            {
                WARN("Warning: SQL query returned not a single row - ignore extra row(s)");
                break;
            }

            step++;
        }
        else
        {
            if (res != SQLITE_DONE) /* SQL error */
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Couldn't execute query to select subst values (%d): %s",
                                res, sqlite3_errmsg(dbconn));

            /* res = SQLITE_DONE */
            if (step == 0) /* no row was selected from db */
            {
                status = EPS_EMPTY;
                DBG("No values for substitution");
            }
            else /* the last ("neutral") iteration */
            {
                status = EPS_OK;
            }

            break;
        }
    }

ret:
    //DBG("Prepared command (status %d): \n\t%s", status, cmd_buf);

    if (stmt) sqlite3_finalize(stmt);

    return status;
}


static ep_stat_t w_get_values_script_perparam(worker_data_t *wd, ep_message_t *answer,
                                     parsed_param_name_t *pn,
                                     obj_info_t *obj_info, sqlite3 *obj_db_conn,
                                     param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;
    int  i, res_code, param_cnt = 0;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char *p_extr_param, *methodString, buf[EP_SQL_REQUEST_BUF_SIZE];
    char *token, *strtok_ctx1, *p_sep;
    BOOL  more_instance = TRUE;
    parsed_operation_t parsed_script_str;
    sqlite3_stmt *stmt = NULL;

    /* Save names of all index parameters of the object */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /* If one param is requested (i.e. full path is used), find it in param_info*/
    if (!pn->partial_path)
    {
        if (w_check_param_name(param_info, param_num, pn->leaf_name, &i) != EPS_OK)
            GOTO_RET_WITH_ERROR(EPS_INVALID_PARAM_NAME, "Unknown parameter name %s", pn->leaf_name);

        if (param_info[i].getOperStyle != OP_STYLE_SCRIPT)
            return EPS_OK;  //TODO Think: We just ignore ???
    }

    for (i = 0; i < param_num; i++)
    {
        if (param_info[i].getOperStyle != OP_STYLE_SCRIPT)
            continue;

        if (!pn->partial_path && strcmp(pn->leaf_name, param_info[i].paramName) )
            continue;

        if (!paramReadAllowed(param_info, i, answer->header.callerId))
        {
            /*DBG("Param %s isn't allowed to be read by the requestor (%d)",
                     param_info[i].paramName, answer->header.callerId);*/
            continue;
        }

        /* Retrieve and parse get-method string */
        if (strlen(param_info[i].getMethod) == 0)
        {
            DBG("WARNING: Script getMethod string is NULL for param %s", param_info[i].paramName);
            continue;
        }
        methodString = param_info[i].getMethod;
        w_parse_operation_string(OP_GET, methodString, &parsed_script_str);

        more_instance = TRUE;
        stmt = NULL;
        while (more_instance == TRUE)
        {
            /* Prepare shell command (with all needed info) and perform it */
            status1 = w_prepare_command(wd, pn, obj_info, obj_db_conn, &parsed_script_str,
                                           idx_params, idx_values, idx_params_num,
                                           (char*)&buf, sizeof(buf), &stmt);
            if (status1 != EPS_OK)
            {
                if (status1 != EPS_NOTHING_DONE)
                {
                    if (!pn->partial_path)
                        GOTO_RET_WITH_ERROR(status1, "Could not prepare command for param %s", pn->leaf_name);
                    else
                        DBG("Could not prepare command for param %s (%d)",  param_info[i].paramName, status1);
                 }

                more_instance = FALSE;
                continue;
            }
            DBG("Prepared command: \n\t%s", buf);

            p_extr_param = w_perform_prepared_command(buf, sizeof(buf), TRUE, NULL);
            if (!p_extr_param)
                GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not read script results");

            DBG("Result of the command: %s ", trim(p_extr_param));

            /* Parse result buffer and extract param values (if specified) */
            if (parsed_script_str.value_to_extract &&
                (strlen(parsed_script_str.value_to_extract) > 0))
            {
                DBG("Name of parameter to extract: %s, len=%d", parsed_script_str.value_to_extract,
                      strlen(parsed_script_str.value_to_extract));

                p_extr_param = strstr(buf, parsed_script_str.value_to_extract);
                if (p_extr_param)   // TODO!!! WE need to test this scenario!!!
                {
                    p_extr_param = strstr(buf, " ") + 1;
                    DBG("Extracted parameter: %s", p_extr_param);
                }
            }
            else /* Param name for extract is not specified in method string */
            {
                /* Parse received results. There are two possible formats:
                 (1) with result code:  rescode ; paramValue
                 (2) direct value, w/o res code:    paramValue               */
                p_sep = strstr(buf, ";");
                if (p_sep)
                {   /* This is 1st format - with res code */
                    token    = strtok_r(buf, ";", &strtok_ctx1);
                    res_code = atoi(token);
                    if (strlen(token) == 0 || !isdigit(token[0]) || res_code != 0)
                    {
                        DBG("Get method script returned error: %d: %s", res_code, token);
                        if (stmt == NULL)  more_instance = FALSE;

                        continue;
                    }
                    token = strtok_r(NULL, "; ", &strtok_ctx1);
                }
                else /* This is the 2nd format: immediate value without res code */
                {
                    token = buf;
                }
                p_extr_param = trim(token);
            }

            if ((p_extr_param == NULL) || (strlen(p_extr_param) == 0))  p_extr_param = "";
            if (param_info[i].hidden == TRUE) p_extr_param = "";

            DBG("Extracted value of parameter %s: %s", param_info[i].paramName, p_extr_param);

            /* Fill parameter name (with all indeces) and value in the answer buf*/
            w_insert_value_to_answer(wd, answer, obj_info->objName,idx_values,idx_params_num,
                                     param_info[i].paramName,
                                     (char *)db2soap((char *)p_extr_param, param_info[i].paramType));
            param_cnt++;

            if (stmt == NULL) more_instance = FALSE;

        } //End of while over instances

    } //End of for over all received parameters

ret:
    if (stmt) sqlite3_finalize(stmt);
    if (param_cnt > 0) DBG(" %d parameters were processed", param_cnt);
    return status;
}


static ep_stat_t w_get_values_script_perobject(worker_data_t *wd, ep_message_t *answer,
                   parsed_param_name_t *pn, obj_info_t *obj_info, sqlite3 *obj_db_conn,
                   param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;
    int  j, i = 0, param_cnt = 0;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char *methodString,  buf[EP_SQL_REQUEST_BUF_SIZE];
    char *name, *value, *p_extr_param;
    char *strtok_ctx1, *strtok_ctx2, *token;
    int  res_code = 0;
    BOOL leaf_param_retreived = FALSE, more_instance = TRUE;
    parsed_operation_t parsed_script_str;
    sqlite3_stmt *stmt = NULL;

    /* Check if we have parameters with "script" get-style */
    for (i = 0; i < param_num; i++)
    {
        if ( ( (param_info[i].getOperStyle == OP_STYLE_SCRIPT) ||
               (param_info[i].getOperStyle == OP_STYLE_NOT_DEF) ) &&
             ( !strcmp(pn->leaf_name, param_info[i].paramName) || pn->partial_path) )
            param_cnt++;
    }

    if (param_cnt == 0) //Nothing to do
        return EPS_OK;

    /* Save names of all index parameters of the object */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /* Prepare method string and shell command (with all needed info) */
    methodString = obj_info->getMethod;
    w_parse_operation_string(OP_GET, methodString, &parsed_script_str);

    i = 0; param_cnt = 0;
    while (more_instance == TRUE)
    {
        /* Prepare shell command (with all needed info) and perform it */
        status1 = w_prepare_command(wd, pn, obj_info, obj_db_conn, &parsed_script_str,
                                        idx_params, idx_values, idx_params_num,
                                        (char*)&buf, sizeof(buf), &stmt);
        if (status1 != EPS_OK)
        {
            if (status1 != EPS_NOTHING_DONE)
                DBG("Could not prepare command for obj %s (%d)",  obj_info->objName, status1);
            more_instance = FALSE;
            continue;
        }
        DBG("Prepared command %d: \n\t%s", ++i, buf);

        /* Now perform the prepared command and parsed received results*/
        p_extr_param = w_perform_prepared_command(buf, sizeof(buf), TRUE, NULL);
        if (!p_extr_param)
            GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not read script results");

        DBG("Result of the command: %s ", trim(p_extr_param));

        /* Process rescode  (the first returned number)*/
        token = strtok_r(buf, ";", &strtok_ctx1);  trim(token);
        res_code = atoi(token);
        if (strlen(token) == 0 || !isdigit(token[0]) || res_code != 0)
        {
            WARN("Get-script returned bad rescode %d.", res_code);
            if (stmt == NULL)  more_instance = FALSE;

            continue;
        }

        /* Process all returned parameters and values (format: name=value )*/
        token = strtok_r(NULL, ";", &strtok_ctx1); trim(token);
        while (token && (strlen(token) > 0))
        {
            //DBG("Script returned token %s", token);
            name  = strtok_r(token, "=", &strtok_ctx2);
            value = strtok_r(NULL, ";", &strtok_ctx2);
            trim(name); trim(value);
            //DBG("Script returned name = %s, value = %s",  name, value);
            if (!name || strlen(name) == 0)
            {
                DBG("script returned null param name. Ignore");
                goto next_token;
            }

            /* Now check the parameter name */
            if (w_check_param_name(param_info, param_num, name, &j) != EPS_OK)
            {
                //DBG("Unknown/not needed parameter name %s", name);
                goto next_token;
            }

            if (!pn->partial_path && !strcmp(name, pn->leaf_name))
            {
                leaf_param_retreived = TRUE;
            }

            if (param_info[j].getOperStyle != OP_STYLE_NOT_DEF &&
                param_info[j].getOperStyle != OP_STYLE_SCRIPT)
            {
                DBG("Param %s should be retrieved by %s style. Ignore it", name,
                               operstyle2string(param_info[j].getOperStyle));
                goto next_token;
            }

            if (paramReadAllowed(param_info, j, answer->header.callerId))
            {
                if (!value || (strlen(value)== 0) || param_info[j].hidden == TRUE)
                   value = "";

                w_insert_value_to_answer(wd, answer, obj_info->objName,
                                         idx_values, idx_params_num, name,
                                         (char *)db2soap(value, param_info[j].paramType));
                param_cnt++;
            }

            if ( leaf_param_retreived )
                break;

next_token:
            token = strtok_r(NULL, ";", &strtok_ctx1); trim(token);

        } // End of while cycle over "name=value" tokens

       if (stmt == NULL) more_instance = FALSE;
    } // End of while stmt over all instances

ret:
    if (stmt) sqlite3_finalize(stmt);
    if (param_cnt > 0) DBG(" %d parameters were processed", param_cnt);
    return status;
}


/* -------------------------------------------------------------------
 *    Helper functions for processing backend requests
 *      (used by backend-style methods)
 * --------------------------------------------------------------------*/

/* Prepare backend API request structure (bereq)
   for GET,SET,ADDOBJ and DELOBJ operations.
   The GETALL operation is processed by separated function    */
#define EP_MAX_BE_REQ_SEQNUM  999
static ep_stat_t w_prepare_backend_request(worker_data_t *wd, mmxba_op_type_t op_type,
        parsed_backend_method_t *parsed_method, sqlite3_stmt *stmt, int idx_param_num,
        int param_num, param_info_t *param_info,
        int paramValuesNum, nvpair_t *paramValues, mmxba_request_t *bereq)
{
    int i, j;
    int db_value_cond;
    char *leaf_name;
    const char *subst_val;

    memset((char *)bereq, 0, sizeof(mmxba_request_t));
    mmx_backapi_msgstruct_init(bereq, wd->be_req_values_pool, sizeof( wd->be_req_values_pool));

    bereq->op_type = op_type;

    strcpy_safe(bereq->beObjName, parsed_method->beObjName, sizeof(bereq->beObjName));

    for (i = 0; i < idx_param_num; i++)
    {
        sprintf(bereq->mmxInstances+strlen(bereq->mmxInstances), "%d,", sqlite3_column_int(stmt, i));
    }
    if (idx_param_num > 0)
        LAST_CHAR(bereq->mmxInstances) = '\0'; /* remove last comma */

    /* Set backend key params name and values */
    if ( op_type == MMXBA_OP_TYPE_GET || op_type == MMXBA_OP_TYPE_SET ||
         op_type == MMXBA_OP_TYPE_DELOBJ || op_type == MMXBA_OP_TYPE_ADDOBJ )
    {
        for (i = 0; i < parsed_method->subst_val_num; i++)
        {
            if (parsed_method->subst_val[i].mmx_subst_val.conditional)
            {
                db_value_cond = sqlite3_column_int(stmt, idx_param_num + i);
                subst_val = db_value_cond ?
                         parsed_method->subst_val[i].mmx_subst_val.true_val :
                         parsed_method->subst_val[i].mmx_subst_val.false_val;
            }
            else
            {
                subst_val = (char *)sqlite3_column_text(stmt, idx_param_num + i);
            }
            mmx_backapi_msgstruct_insert_nvpair(bereq, (nvpair_t *)&(bereq->beKeyParams[bereq->beKeyParamsNum]),
                                                 parsed_method->subst_val[i].backend_key_name, (char *)subst_val);
            bereq->beKeyParamsNum++;
        }
    }

    /* Set backend key params names (without values) */
    if (op_type == MMXBA_OP_TYPE_ADDOBJ)
    {
        bereq->addObj_req.beKeyNamesNum = parsed_method->bekey_param_num;
        for (i = 0; i < parsed_method->bekey_param_num; i++)
        {
            strcpy_safe(bereq->addObj_req.beKeyNames[i],
                        parsed_method->bekey_params[i], NVP_MAX_NAME_LEN);
        }
    }

    if (op_type == MMXBA_OP_TYPE_GET)
    {
        j = 0;
        for (i = 0; i < param_num; i++)
        {
            if ( param_info[i].isIndex ||
                 ( (param_info[i].getOperStyle != OP_STYLE_NOT_DEF) &&
                   (param_info[i].getOperStyle != OP_STYLE_BACKEND) ) )
            {
                continue;
            }

            strcat_safe(bereq->paramNames.paramNames[j], param_info[i].paramName,
                        sizeof(bereq->paramNames.paramNames[0]));
            j++;
        }
        bereq->paramNames.arraySize = j;

        DBG("Num of object params: %d; num of params in BE req: %d",
             param_num, bereq->paramNames.arraySize);
    }
    else if (op_type == MMXBA_OP_TYPE_SET)
    {
        bereq->paramValues.arraySize = paramValuesNum;
        for (i = 0; i < paramValuesNum; i++)
        {
            /* Check param name in paramValues array; if it is the full path
               name, replace it by its leaf name */
            leaf_name = getLeafParamName(paramValues[i].name);
            if (leaf_name == NULL)
            {
                WARN("Bad param name for SET operation: %s", paramValues[i].name);
                continue;
            }
            mmx_backapi_msgstruct_insert_nvpair(bereq, (nvpair_t *)&(bereq->paramValues.paramValues[i]),
                                                leaf_name, paramValues[i].pValue);
        }
    }
    else if (op_type == MMXBA_OP_TYPE_ADDOBJ)
    {
        bereq->addObj_req.paramNum = paramValuesNum;
        for (i = 0; i < paramValuesNum; i++)
        {
            mmx_backapi_msgstruct_insert_nvpair(bereq, (nvpair_t *)&(bereq->addObj_req.paramValues[i]),
                                                paramValues[i].name, paramValues[i].pValue);
        }
    }

    /* The last thing: calculate sequence number for the request */
    if ((wd->be_req_cnt >= EP_MAX_BE_REQ_SEQNUM) ||  (wd->be_req_cnt < 0))
        wd->be_req_cnt = 0;
    else
        wd->be_req_cnt++;

    bereq->opSeqNum = (wd->self_w_num * (EP_MAX_BE_REQ_SEQNUM + 1)) + wd->be_req_cnt;

    return EPS_OK;
}

/* Form XML string with backend API request (xml_req)
   for GET, SET, ADDOBJ and DELOBJ operation.
   The GETALL operation is processed by separated function     */
static ep_stat_t w_form_backend_request(worker_data_t *wd, mmxba_op_type_t op_type,
        parsed_backend_method_t *parsed_method, sqlite3_stmt *stmt, int idx_param_num,
        int param_num, param_info_t *param_info,
        int paramValuesNum, nvpair_t *paramValues)
{
    ep_stat_t status = EPS_OK;
    int stat;
    mmxba_request_t req;
    char *buf = wd->be_req_xml_buf;      // Buffer for XML req/resp to/from backend
    int  dataSize, bufSize = sizeof(wd->be_req_xml_buf);
    mmxba_packet_t *pkt = (mmxba_packet_t *)buf;

    /* Fill backend request structure */
    status = w_prepare_backend_request(wd, op_type, parsed_method, stmt,
                                       idx_param_num, param_num,param_info,
                                       paramValuesNum, paramValues, &req);
    if (status != EPS_OK)
    {
        ERROR("Cannot prepare request %d to backend (stat %d)", op_type);
        return EPS_GENERAL_ERROR;
    }

    /* Create XML string with the backend request */
    memset((char *)pkt, 0, bufSize);
    memcpy(pkt->flags, mmxba_flags, sizeof(mmxba_flags));
    dataSize = bufSize - sizeof(mmxba_packet_t);

    if ((stat = mmx_backapi_request_build(&req, pkt->msg, dataSize)) != MMXBA_OK)
    {
        ERROR("Could not build mmx backapi message: %d", stat);
        return EPS_GENERAL_ERROR;
    }

    DBG("BE request (%d bytes):\n%s", strlen(pkt->msg), pkt->msg);

    return EPS_OK;
}

static ep_stat_t w_send_pkt_to_backend(int sock, int be_port, mmxba_packet_t *pkt)
{
    struct sockaddr_in dest;
    unsigned long dummy_addr;
    int res = 0;

    dest.sin_family = AF_INET;
    dest.sin_port = htons(be_port);
    dest.sin_addr.s_addr = inet_addr(MMX_BE_IPADDR);

    res = connect(sock, (struct sockaddr *)&dest, sizeof(dest));
    if (res < 0)
    {
        ERROR("Could connect to backend: %s (%d)", strerror(errno), errno);
        return EPS_SYSTEM_ERROR;
    }

    res = sendto(sock, pkt, sizeof(mmxba_packet_t)+strlen(pkt->msg)+1, 0,
                                   (struct sockaddr *)&dest, sizeof(dest));
    if (res <= 0)
    {
        ERROR("Could not send message to backend: %s (%d)", strerror(errno), errno);
        return EPS_SYSTEM_ERROR;
    }

    return EPS_OK;

}

static ep_stat_t w_rcv_answer_from_backend(int sock, int seq_num,
                                           char *buf, size_t buf_size, int *rcvd)
{
    int res;
    int retry_cnt = 0;
    BOOL waiting = TRUE, bad_first_msg = TRUE;
    mmxba_request_t response;
    struct timeval start_time, now;
    double timediff;

    gettimeofday(&start_time , NULL);

    while (waiting)
    {
        retry_cnt++;
        memset(buf, 0, buf_size);
        memset(&response, 0, sizeof(mmxba_request_t));

        res = recv(sock, buf, buf_size, 0);
        if (res <= 0)
        {
            ERROR("Could not receive answer from backend: %s (%d)", strerror(errno), errno);
            return EPS_SYSTEM_ERROR;
        }

        /* Parse header and check sequence number */
        if (mmx_backapi_message_hdr_parse(buf, &response) == MMXBA_OK)
        {
            if (response.opSeqNum == seq_num)
            {
                   /* It's response  that we are waiting for */
                    buf[res] = '\0';
                    *rcvd = res;
                    return EPS_OK;
            }
            else
            {
                DBG("Bad seq num received from backend: %d, expected %d. Ignore",
                     response.opSeqNum, seq_num );

                if (bad_first_msg)
                {
                    /* Take one more chance (i.e. wait one more full period) */
                    bad_first_msg = FALSE;
                    gettimeofday(&start_time , NULL);
                    DBG("Bad seq num received from backend: %d, expected %d. Start waiting again",
                          response.opSeqNum, seq_num );

                }
                else
                {
                    DBG("Bad seq num received from backend: %d, expected %d. Ignore",
                     response.opSeqNum, seq_num );
                }
            }
        }
        else /* Just print the beginning of the bad message */
        {
            DBG("Ignore bad backend msg: %.64s", buf);
        }

        gettimeofday(&now , NULL);
        timediff = (now.tv_sec - start_time.tv_sec) +
                         1e-6 * (now.tv_usec - start_time.tv_usec);
        if (timediff > UDP_SOCK_TIMEOUT)
            waiting = FALSE;
    }

    DBG("Failed to receive reply from backend, seqnum = %d (%d attempts)",
         seq_num, retry_cnt);

    return EPS_TIMEOUT;
}


static ep_stat_t form_and_send_be_request(worker_data_t *wd, int be_port,
                    mmxba_op_type_t op_type, parsed_backend_method_t *parsed_method,
                    sqlite3_stmt *stmt, int idx_param_num,
                    int param_num, param_info_t *param_info,
                    int paramValuesNum, nvpair_t *paramValues,
                    mmxba_request_t *be_resp)
{
    ep_stat_t status = EPS_OK;
    int reqSeqNum = 0;
    int  rcvd = 0;
    char *buf = wd->be_req_xml_buf;      // Buffer for XML req/resp to/from backend
    int  bufSize = sizeof(wd->be_req_xml_buf);

    if (w_form_backend_request (wd, op_type, parsed_method, stmt,  idx_param_num,
                    param_num, param_info, paramValuesNum, paramValues) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could build request to backend");


    reqSeqNum = wd->be_req_cnt + (wd->self_w_num * (EP_MAX_BE_REQ_SEQNUM + 1));

    /* Send message to backend and waiting for reply*/
    if (w_send_pkt_to_backend(wd->udp_be_sock, be_port,(mmxba_packet_t *)wd->be_req_xml_buf) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not send packet to BE");

    DBG("Waiting for BE answer (req seqNum %d)", reqSeqNum);

    if (w_rcv_answer_from_backend(wd->udp_be_sock, reqSeqNum, buf, bufSize, &rcvd) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "No response from BE");

    //DBG("%d bytes received from backend (buf size %d):\n%s", rcvd, bufSize, buf);
    DBG("%d bytes received from BE (buf size %d)", rcvd, bufSize);

    mmx_backapi_msgstruct_init(be_resp, wd->be_req_values_pool,
                               sizeof( wd->be_req_values_pool));
    if (mmx_backapi_message_parse(buf, be_resp) != MMXBA_OK)
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not parse response from BE");

ret:
    return status;
}

static ep_stat_t w_get_backend_info(worker_data_t *wd, char *be_name,
                                    int *port, char *initScriptName, int initScriptLen)
{
    backend_info_t beInfo;
    ep_stat_t status = EPS_OK;

    memset(&beInfo, 0, sizeof(beInfo));

    if (port)
        *port = 0;

    status = ep_common_get_beinfo_by_bename(be_name, &beInfo);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not find backend info for %s", beInfo);

    if (port)
        *port = beInfo.portNumber;

    if (initScriptName && initScriptLen > 0)
        strcpy_safe( initScriptName, beInfo.initScript, initScriptLen);
ret:
    return status;
}


static ep_stat_t w_get_values_backend(worker_data_t *wd, ep_message_t *answer,
                                      parsed_param_name_t *pn,
                                      obj_info_t *obj_info, sqlite3 *obj_db_conn,
                                      param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK;
    int i, j, res, param_cnt = 0,  be_port = -1;
    int idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char *methodString = NULL;
    parsed_backend_method_t parsed_method;
    nvpair_t *p_extr_param;
    mmxba_request_t  be_ans;
    BOOL more_instance = TRUE;
    char query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    sqlite3_stmt *stmt = NULL;

    /* Save names of all index parameters of the object */
    get_index_param_names(param_info, param_num, idx_params, &idx_params_num);

    if (idx_params_num != pn->index_num)
        DBG("Not full set of indeces is used for BE GET request (%d of %d)", pn->index_num, idx_params_num);

    /* Get port number of the backend*/
    w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0);
    DBG("Backend '%s': port %d", obj_info->backEndName, be_port);

    /* If get operation style == "backend" for the whole object */
    if (pn->partial_path && obj_info->getOperStyle == OP_STYLE_BACKEND &&
        strlen(obj_info->getMethod) > 0)
    {
        if (be_port <= 0)
           GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s (per-obj get)",
                            be_port, obj_info->backEndName);

        w_parse_backend_method_string(OP_GET, obj_info->getMethod, &parsed_method);

        if (idx_params_num > 0)
        {
            w_form_subst_sql_select_backend(wd, pn, obj_info, query, sizeof(query), idx_params,
                                            idx_params_num, &parsed_method);
            DBG("Subst query:\n%s", query);

            if (sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL) != SQLITE_OK)
               GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL stmt: %s",
                                   sqlite3_errmsg(obj_db_conn));
        }

        while (more_instance)
        {
            if (stmt)
            {
                res = sqlite3_step(stmt);
                if ((res != SQLITE_ROW) && (res != SQLITE_DONE))
                    GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s",
                                        sqlite3_errmsg(obj_db_conn));
                else if (res == SQLITE_DONE)
                    more_instance = FALSE;
            }
            if (more_instance)
            {
                /* Form BE API request, send it to the backend, wait for reply */
                status = form_and_send_be_request (wd, be_port, MMXBA_OP_TYPE_GET,
                                       &parsed_method,stmt, idx_params_num,
                                        param_num, param_info, 0, NULL, &be_ans);
                if (status != EPS_OK)
                    GOTO_RET_WITH_ERROR(status, "BE GET per-obj request failure (%d)", status);

                if (be_ans.opResCode != 0)
                {
                    if (strlen(be_ans.errMsg) > 0)
                        WARN("Backend returned error msg: %d: %s", be_ans.opExtErrCode, be_ans.errMsg);
                    else
                        WARN("Backend returned error: %d", be_ans.opExtErrCode);

                    if (!stmt) more_instance = FALSE;

                    continue;
                }

                /* Save all selected index values */
                for (j = 0; j < idx_params_num; j++)
                   idx_values[j] = sqlite3_column_int(stmt, j);

                /* Extract all received name-value pairs from BE response and
                  insert them to the aggregated answer for caller (front-end) */
                DBG("Number of extracted values: %d; current ans arrsize %d", be_ans.paramValues.arraySize,
                    answer->body.getParamValueResponse.arraySize);
                for (i = 0; i < be_ans.paramValues.arraySize; i++)
                {
                   p_extr_param = &be_ans.paramValues.paramValues[i];

                   if (w_check_param_name(param_info, param_num, p_extr_param->name, &j) != EPS_OK)
                   {
                       //DBG("Unknown/not needed parameter name %s", name);
                       continue;
                   }
                   if (!paramReadAllowed(param_info, j, answer->header.callerId))
                   {
                       /*DBG("Param %s isn't allowed to be read by the requestor (%d)",
                            param_info[j].paramName, answer->header.callerId);*/
                       continue;
                   }
                   if ( param_info[j].getOperStyle != OP_STYLE_NOT_DEF &&
                        param_info[j].getOperStyle != OP_STYLE_BACKEND )
                   {
                       DBG("Param %s should be retrieved by %s style. Ignore the param",
                           param_info[j].paramName,operstyle2string(param_info[j].getOperStyle));
                       continue;
                   }

                   w_insert_value_to_answer(wd, answer, obj_info->objName,
                         idx_values, idx_params_num, p_extr_param->name,
                         (char *)db2soap((char *)p_extr_param->pValue, param_info[j].paramType));
                   param_cnt++;
                }

                if (!stmt) more_instance = FALSE;
            }
        }  // End of while (more_instance)
    }
    else // Send per-param request to the backend
    {
        for (i = 0; i < param_num; i++)
        {
            /* Some checkings before sending request to backend */
            if ( (param_info[i].getOperStyle != OP_STYLE_BACKEND) &&
                 !( (param_info[i].getOperStyle == OP_STYLE_NOT_DEF) &&
                    (obj_info->getOperStyle == OP_STYLE_BACKEND) ) )
                continue;

            if (be_port < 0)
                GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s (per-param get)",
                            be_port, obj_info->backEndName);

            if (!pn->partial_path && strcmp(pn->leaf_name, param_info[i].paramName) )
                continue;

            if (!paramReadAllowed(param_info, i, answer->header.callerId))
            {
                DBG("Parameter %s isn't allowed to be read by this requestor (%d)",
                           param_info[i].paramName, answer->header.callerId);
                continue;
            }

            if ( (strlen(param_info[i].getMethod) == 0) && (strlen(obj_info->getMethod) == 0) )
            {
                DBG("WARNING: Backend getMethod string is NULL for param %s", param_info[i].paramName);
                continue;
            }
            methodString = (strlen(param_info[i].getMethod) > 0) ?
                                 param_info[i].getMethod : obj_info->getMethod;

            DBG("Request to backend is needed for param %s", param_info[i].paramName);
            w_parse_backend_method_string(OP_GET, methodString, &parsed_method);

            if (idx_params_num > 0)
            {
                w_form_subst_sql_select_backend(wd, pn, obj_info, query, sizeof(query), idx_params,
                                                idx_params_num, &parsed_method);
                DBG("Subst query:\n%s", query);

                if (sqlite3_prepare_v2(obj_db_conn, query, -1, &stmt, NULL) != SQLITE_OK)
                   GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                       sqlite3_errmsg(obj_db_conn));
            }

            more_instance = TRUE;
            while (more_instance)
            {
                if (stmt)
                {
                    res = sqlite3_step(stmt);
                    if ((res != SQLITE_ROW) && (res != SQLITE_DONE))
                        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s",
                                            sqlite3_errmsg(obj_db_conn));
                    else if (res == SQLITE_DONE)
                        more_instance = FALSE;
                }
                if (more_instance)
                {
                    status = form_and_send_be_request (wd, be_port, MMXBA_OP_TYPE_GET,
                                       &parsed_method,stmt, idx_params_num,
                                       param_num, param_info, 0, NULL, &be_ans);
                    if (status != EPS_OK)
                        GOTO_RET_WITH_ERROR(status, "BE GET per-param request failure (%d)", status);

                    if (be_ans.opResCode != 0)
                    {
                        if (strlen(be_ans.errMsg) > 0)
                            WARN("Backend returned error: %d: %s", be_ans.opExtErrCode, be_ans.errMsg);
                        else
                            WARN("Backend returned error: %d", be_ans.opExtErrCode);

                        if (!stmt) more_instance = FALSE;

                        continue;
                    }

                    /* Save all selected index values */
                    for (j = 0; j < idx_params_num; j++)
                       idx_values[j] = sqlite3_column_int(stmt, j);

                    /* extract received name-value pair and place it to answer array*/
                    DBG("Number of extracted values: %d", be_ans.paramValues.arraySize);
                    p_extr_param = &(be_ans.paramValues.paramValues[0]);

                    if (be_ans.paramValues.arraySize > 0)  //It should be 1; we asked 1 parameter
                    {
                        w_insert_value_to_answer(wd, answer, obj_info->objName,
                                    idx_values, idx_params_num, param_info[i].paramName,
                                    (char *)db2soap((char *)p_extr_param->pValue, param_info[i].paramType));
                        param_cnt++;
                    }

                    if (!stmt) more_instance = FALSE;
                }
            } // End of while (more_instance)
        } // End of for stmt over params
    }

ret:
    if (param_cnt > 0) DBG(" %d parameters were processed", param_cnt);

    if (stmt) sqlite3_finalize(stmt);
    return status;
}

static ep_stat_t w_check_get_opstyle(worker_data_t *wd, parsed_param_name_t *pn,
                obj_info_t *obj_info, param_info_t param_info[], int param_num)
{
    /*
     * If operation style is not specified for the whole object, then check
     *  each parameter
     */
    if (obj_info->getOperStyle == OP_STYLE_NOT_DEF)
    {
        for (int i = 0; i < param_num; i++)
        {
            /* If certain leaf specified, then check only it */
            if (!pn->partial_path)
            {
                if (!strcmp(param_info[i].paramName, pn->leaf_name))
                {
                    if (param_info[i].getOperStyle == OP_STYLE_ERROR)
                    {
                        ERROR("Invalid operation style for parameter: %s", param_info[i].paramName);
                        return EPS_GENERAL_ERROR;
                    }
                    else
                    {
                       /* In case of getStyle is not define, DB style will be used */
                       if (param_info[i].getOperStyle == OP_STYLE_NOT_DEF)
                       {
                           param_info[i].getOperStyle = OP_STYLE_DB;
                           DBG("GET operstyle of param %s is not defined; DB style will be used", param_info[i].paramName);
                       }
                       return EPS_OK;
                    }
                }
            }
            else
            {
                /* If getStyle is not define or errored, the parameter
                   will be silently skipped */
                if (param_info[i].getOperStyle == OP_STYLE_ERROR)
                {
                    DBG("GET operstyle of parameter %s is invalid; this param will be skipped", param_info[i].paramName);
                }
                else if (param_info[i].getOperStyle == OP_STYLE_NOT_DEF)
                {
                    //DBG("GET operstyle of param %s is not defined; this param will be skipped", param_info[i].paramName);
                }
            }
        }
        return EPS_OK;
    }
    else if (obj_info->getOperStyle == OP_STYLE_ERROR)
    {
        ERROR("Invalid operation style for object: %s", obj_info->objName);
        return EPS_GENERAL_ERROR;
    }
    else
    {
        return EPS_OK;
    }
}


static ep_stat_t w_handle_getvalue(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;

    parsed_param_name_t pn;
    obj_info_t obj_info[MAX_OBJECTS_NUM];
    int i, j, obj_num, param_num, req_size;
    int obj_success_cnt = 0; //Counter of successfully processed objects
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    sqlite3 *dbconn = NULL;
    ep_message_t answer;

    req_size = message->body.getParamValue.arraySize;

    /* Init response message */
    memset((char *)&answer, 0, sizeof(answer));
    mmx_frontapi_msg_struct_init(&answer, (char *)wd->fe_resp_values_pool,
                                     sizeof(wd->fe_resp_values_pool));
    memcpy(&answer.header, &message->header, sizeof(answer.header));
    answer.header.respFlag = 1;
    answer.header.msgType = MSGTYPE_GETVALUE_RESP;
    answer.body.getParamValueResponse.arraySize = 0;

    if ((status = w_init_mmxdb_handles(wd, message->header.mmxDbType, MSGTYPE_GETVALUE)) != EPS_OK )
        goto ret;

    dbconn = wd->main_conn;

    /* For each request parameter */
    for (i = 0; i < req_size; i++)
    {
        /* Parse request string: extract object name, parameter name, indices provided */
        if ((status = parse_param_name(message->body.getParamValue.paramNames[i], &pn)) != EPS_OK)
        {
            status = EPS_INVALID_FORMAT;
            ERROR("Could not parse object and parameter name %s", message->body.getParamValue.paramNames[i]);
        }
        /* Acquire information about the objects from the meta DB */
        if ((status == EPS_OK ) && (w_get_obj_info(wd, &pn, message->body.getParamValue.nextLevel,
                                                   0, obj_info, MAX_OBJECTS_NUM, &obj_num) != EPS_OK))
        {
            status = EPS_INVALID_FORMAT;
            ERROR("Could not get object info for %s", message->body.getParamValue.paramNames[i]);
        }

        if (status != EPS_OK)
        {
            if (req_size == 1)
                GOTO_RET_WITH_ERROR(status, "Could not process the only requested object");
            else
                continue;
        }

        /* For each object in the requested object's tree */
        for (j = 0; j < obj_num; j++)
        {
            DBG("----- Processing object %s - num of indeces %d (%d, %d, %d, %d, %d)",
                  obj_info[j].objName, pn.index_num, i, j, req_size, obj_num,
                  message->body.getParamValue.configOnly);

            /* Acquire information about the requested parameter(s) from the DB */
            if (((status = w_get_param_info(wd, &pn, obj_info+j, message->body.getParamValue.configOnly,
                                param_info, &param_num, NULL)) != EPS_OK) || param_num == 0)
            {
                DBG("No parameter info (%d). Ignore object", status);
                continue;
            }

            /* If it is get config-only request, special handler is used */
            if (message->body.getParamValue.configOnly)
            {
                status1 = w_get_values_configonly(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num);
                goto  check_step_res;
            }

            if ((status = w_check_get_opstyle(wd, &pn, obj_info+j, param_info, param_num)) != EPS_OK)
                GOTO_RET_WITH_ERROR(status, "One or more operation styles are invalid");

            /* Parameters with style == db are to be retrieved from the db regardless of object's get style */
            if ((status = w_get_values_db(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num)) != EPS_OK)
                GOTO_RET_WITH_ERROR(status, "Could not get values from DB");

            switch (obj_info[j].getOperStyle)
            {
            case OP_STYLE_DB: /* db values already acquired */
                break;
            case OP_STYLE_UCI:
                status1 = w_get_values_uci(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num);
                break;
            case OP_STYLE_UBUS:
                status1 = w_get_values_ubus(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num);
                break;
            case OP_STYLE_SCRIPT:
                status1 = w_get_values_script_perobject(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num);
                break;
            case OP_STYLE_BACKEND:
                status = w_get_values_backend(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num);
                break;

            /* Style OP_STYLE_SHELL_SCRIPT is currently supported for
             *  SET operation per distinct Object parameter(s) only */

            /* object's get style is not set, process it parameter by parameter */
            default:
                if ((status1 = w_get_values_uci(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num)) != EPS_OK)
                    ERROR("Could not get values from UCI for obj %s (err %d)", obj_info[j].objName, status);

                if ((status1 = w_get_values_ubus(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num)) != EPS_OK)
                    ERROR("Could not get values from UBUS for obj %s (err %d)", obj_info[j].objName, status);

                if ((status1 = w_get_values_script_perparam(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num)) != EPS_OK)
                    ERROR("Could not get values from SCRIPT for obj %s (err %d)", obj_info[j].objName, status);

                if ((status1 = w_get_values_backend(wd, &answer, &pn, obj_info+j, dbconn, param_info, param_num)) != EPS_OK)
                    ERROR("Could not get values from BACKEND for obj %s (err %d)", obj_info[j].objName, status);

            } //End of switch by get style

check_step_res:
            if (status == EPS_OK)
                obj_success_cnt++;
            else  /* If only one object is requested, we stop here with error*/
            {
                if ((req_size == 1) && (obj_num == 1))
                   GOTO_RET_WITH_ERROR(status, "Failed to get values for one requested object");
            }
        }   // End of "for j" stmt

        if (obj_num == 0)
            GOTO_RET_WITH_ERROR(EPS_INVALID_PARAM_NAME, "Unknown object `%s'", pn.obj_name);

    }   // End of "for i" stmt


    if ((status != EPS_OK) &&
        ((obj_success_cnt == 0) || (answer.body.getParamValueResponse.arraySize == 0)) )
    {
        ERROR("No one successfully processed object! (last status %d)", status);
        status = EPS_GENERAL_ERROR;
    }

ret:
    answer.header.respCode = w_status2cwmp_error(status);
    if (status != EPS_OK)
    {
        DBG("Unsuccessfull processing of GET request: status=%d, respcode=%d",
            status, answer.header.respCode);
        answer.body.getParamValueResponse.arraySize = 0;
    }
    w_send_answer(wd, &answer);
    return status;
}

/* -------------------------------------------------------------------------------*
 * ----------- Functions for procesing SetParamValue request  --------------------*
 * -------------------------------------------------------------------------------*/
/* Handler function for processing setParamValue operation in case of
   "db" set-style is used for set method                                */
static ep_stat_t w_set_value_db(worker_data_t *wd, parsed_param_name_t *pn, obj_info_t *obj_info,
                         sqlite3 *dbconn, param_info_t param_info[], int param_num,
                         int set_param_index, char *value)
{
    ep_stat_t status = EPS_OK;
    int i;
    int modified_rows_num = 0;
    char ownerStr[3] = {0};

    sprintf((char *)ownerStr, "%d", EP_DATA_OWNER_USER);

    /* Check if parameter should be saved to DB or not*/
    if (param_info[set_param_index].notSaveInDb)
    {
        DBG("Param %s should not be saved in DB", param_info[set_param_index].paramName);
        return EPS_OK;
    }

    /* Build UPDATE query */
    char query[EP_SQL_REQUEST_BUF_SIZE] = "UPDATE ";
    strcat_safe(query, obj_info->objValuesTblName, sizeof(query));
    strcat_safe(query, " SET ", sizeof(query));

    /* param name and its value to be set  */
    strcat_safe(query, "[", sizeof(query));
    strcat_safe(query, pn->leaf_name, sizeof(query));
    strcat_safe(query, "]='", sizeof(query));
    strcat_safe(query, soap2db(value, param_info[set_param_index].paramType), sizeof(query));
    strcat_safe(query, "'", sizeof(query));

    /* Set config owner to 1 (i.e. user)*/
    strcat_safe(query, ", ", sizeof(query));
    strcat_safe(query, MMX_CFGOWNER_DBCOLNAME, sizeof(query));
    strcat_safe(query, "='", sizeof(query));
    strcat_safe(query, ownerStr, sizeof(query));
    strcat_safe(query, "'", sizeof(query));


    strcat_safe(query, " WHERE 1 ", sizeof(query));
    for (i = 0; i < pn->index_num; i++)
    {
        if (pn->indices[i].type == REQ_IDX_TYPE_EXACT)
        {
            /* concatenate PARAM=ARG */
            strcat_safe(query, " AND [", sizeof(query));
            strcat_safe(query, param_info[i].paramName, sizeof(query));
            strcat_safe(query, "] = ", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].exact_val.num);
        }
        else if (pn->indices[i].type == REQ_IDX_TYPE_RANGE)
        {
            strcat_safe(query, " AND ([", sizeof(query));
            strcat_safe(query, param_info[i].paramName, sizeof(query));
            strcat_safe(query, "] BETWEEN ", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].range_val.begin);
            strcat_safe(query, " AND ", sizeof(query));
            sprintf(query+strlen(query), "%d", pn->indices[i].range_val.end);
            strcat_safe(query, " ) ", sizeof(query));
        }
        else // REQ_IDX_TYPE_ALL: no restriction for that index
        {
        }
    }
    DBG("%s", query);

    if ((status = ep_db_exec_write_query(dbconn, query, &modified_rows_num)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not execute UPDATE query: %d", status);

ret:
    return status;
}

/* Helper function
    updates values db of the specified object with the value
    Input params:
       dbconn   - connection to the values DB to be updated
       obj_info - meta information of the object
       paramName - ptr to name of parameter (DB column name)
       paramtype - ptr to string with type of the updated parameter
       value     - ptr to the new value of the parameter
       idx_params, idx_valuex, idx_num - names, values and number of
                   index parameters used as conditions for WHERE clause
    Returns:
        EPS_OK - in case of success, error code - otherwise
*/
ep_stat_t  w_update_values_db(sqlite3 *dbconn, obj_info_t *obj_info,
                              char* paramName, char* paramType, char *value,
                              char *idx_params[], int *idx_values,  int idx_num)
{
    ep_stat_t status = EPS_OK;
    int i;
    int modified_rows_num = 0;
    char query[EP_SQL_REQUEST_BUF_SIZE], *p_query = (char *)&query;
    char ownerStr[3] = {0};
    sqlite3_stmt *stmt = NULL;

    sprintf((char *)ownerStr, "%d", EP_DATA_OWNER_USER);

    //DBG("Update values DB for parameter %s of object %s, index num = %d",
    //     paramName, obj_info->objName, idx_num);

    /* Build UPDATE query */
    memset(query, 0, sizeof(query));
    sprintf(p_query, "UPDATE %s SET [%s] = '%s', %s = '%s' WHERE 1 ",
            obj_info->objValuesTblName, paramName, soap2db(value, paramType),
            MMX_CFGOWNER_DBCOLNAME, ownerStr);
    p_query += strlen(p_query);

    for (i = 0; i < idx_num; i++)
    {
        sprintf(p_query, "AND [%s] = %d ", idx_params[i], idx_values[i]);
        p_query += strlen(p_query);
    }
    DBG("%s", query);

    /* Perform prepared query */
    if ((status = ep_db_exec_write_query(dbconn, query, &modified_rows_num)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Failed to perform update DB query %d", status);

    DBG ("DB update completed successfully");

ret:
    if (stmt) sqlite3_finalize(stmt);

    return status;
}

/* Handler function for processing setParamValue operation in case of
   "uci" set-style is used for set method                                */
ep_stat_t w_set_value_uci(worker_data_t *wd, parsed_param_name_t *pn,
                          obj_info_t *obj_info, sqlite3 *dbconn,
                          param_info_t param_info[], int param_num,
                          int set_param_index, char *value, int *p_set_status)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int  i, res, idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    BOOL more_instance = TRUE, commit_needed = FALSE;
    char buf[EP_SQL_REQUEST_BUF_SIZE];
    char *filename, *strtok_ctx;
    parsed_operation_t parsed_uci_str;
    sqlite3_stmt *stmt = NULL;
    char setMethodBuf[MAX_METHOD_STR_LEN] = {0};

    /* Save index values */
    for (i = 0; i < param_num; i++)
    {
        if (param_info[i].isIndex)
            idx_params[idx_params_num++] = param_info[i].paramName;
    }

    /* Save method string before parsing */
    memcpy(setMethodBuf, param_info[set_param_index].setMethod, sizeof(setMethodBuf));

    w_parse_operation_string(OP_SET, (char *)setMethodBuf, &parsed_uci_str);

    DBG("Value for setting: %s", value);

    /*Determine name of uci config file */
    filename = strtok_r(parsed_uci_str.command, ".", &strtok_ctx);
    if (!filename || (strlen(trim(filename)) == 0))
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not determine uci file name for param");

    i = 0;
    while (more_instance == TRUE)
    {
        /* Prepare shell command (with all needed info) and perform it */
        memset(buf, 0, sizeof(buf));
        strcpy_safe(buf, "uci set ", sizeof(buf));
        status1 = w_prepare_command(wd, pn, obj_info, dbconn, &parsed_uci_str,
                                        idx_params, idx_values, idx_params_num,
                                        (char*)&(buf)+strlen(buf), sizeof(buf)-strlen(buf), &stmt);
        if (status1 != EPS_OK)
        {
            if (status1 != EPS_NOTHING_DONE)
                GOTO_RET_WITH_ERROR(status1, "Could not prepare set command for param %s",
                                     param_info[set_param_index].paramName);
            more_instance = FALSE;
            continue;
        }

        strcat_safe(buf, "='", sizeof(buf));    //value is placed in quotes
        strcat_safe(buf, value, sizeof(buf));
        strcat_safe(buf, "'", sizeof(buf));
        strcat_safe(buf, " 2>/dev/null", sizeof(buf));
        DBG("Prepared command (%d): \n\t%s", ++i, buf);

        res = system(buf);
        if (res)
        {

            ERROR("Could not execute uci set command. Error 0x%x", res);
            status = EPS_SYSTEM_ERROR;
            break;
        }
        else  //uci set was OK, prepare filename for uci commit and update DB
        {
            DBG("Successful uci set");
            if (!commit_needed)   commit_needed = TRUE;

            /* Now update appropriated instance in the values db (if needed)*/
            if (!param_info[set_param_index].notSaveInDb)
            {
                w_update_values_db(dbconn, obj_info, param_info[set_param_index].paramName,
                                   param_info[set_param_index].paramType,
                                   value, idx_params, idx_values, idx_params_num);
            }
            else
                DBG("Param %s should not be saved in DB",param_info[set_param_index].paramName);
        }

        if (stmt == NULL) more_instance = FALSE;

    } //End of while stmt over instances

    /* Check results of operation and perform uci commit or revert if needed */
    memset(buf, 0, sizeof(buf));
    if (commit_needed)
    {
        if (status == EPS_OK)
            strcpy_safe(buf, "uci commit ", sizeof(buf));
        else
            strcpy_safe(buf, "uci revert ", sizeof(buf));

        strcat_safe(buf, filename, sizeof(buf));
        DBG( "uci command: %s", buf);

        res = system(buf);
        if (res)
        {
             /* It is almost impossible case. */
             GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not execute uci commit or revert. Error 0x%x", res);
        }
        else
        {
            if (status == EPS_OK)
                *p_set_status = 1;  //backend restart is always needed after uci set and commit
        }
    }

ret:

    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/* Handler function for processing setParamValue operation in case of
   "ubus" set-style is used for set method                         */
 /* !!!!!!!! This function was not tested !!!!!!!!!!!!!!! */
ep_stat_t w_set_value_ubus(worker_data_t *wd, parsed_param_name_t *pn,
                           obj_info_t *obj_info, param_info_t param_info[],
                           int param_num, int set_param_index, char *value)
{
    ep_stat_t status = EPS_OK;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int  i, idx_params_num = 0;
    char buf[EP_SQL_REQUEST_BUF_SIZE], query[EP_SQL_REQUEST_BUF_SIZE];
    char setMethodBuf[MAX_METHOD_STR_LEN] = {0};
    parsed_operation_t parsed_ubus_str;

    sqlite3 *conn = NULL;
    sqlite3_stmt *stmt = NULL;

    /* Save index values */
    for (i = 0; i < param_num; i++)
    {
        if (param_info[i].isIndex)
            idx_params[idx_params_num++] = param_info[i].paramName;
    }

    /* Save method string before parsing */
    memcpy(setMethodBuf, param_info[set_param_index].setMethod, sizeof(setMethodBuf));

    w_parse_operation_string(OP_SET, (char *)setMethodBuf, &parsed_ubus_str);

    w_form_subst_sql_select(wd, pn, obj_info, query, sizeof(query), idx_params, idx_params_num, &parsed_ubus_str);

    conn = wd->main_conn;
    DBG("query:\n%s", query);

    if (sqlite3_prepare_v2(conn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s", sqlite3_errmsg(conn));

    while (TRUE)
    {
        int res = sqlite3_step(stmt);

        if (res == SQLITE_ROW)
        {
            strcpy_safe(buf, "ubus call ", sizeof(buf));
            w_form_call_str(buf+strlen(buf), sizeof(buf), &parsed_ubus_str, stmt, idx_params_num);
            DBG("%s", buf);
            //TODO - add code to insert the set value to the prepared command string

            status = system(buf);
            if (status)
                GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not execute ubus");
        }
        else if (res == SQLITE_DONE)
            break;
        else
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(conn));
    }

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/* Handler function for processing setParamValue operation in case of
   "script" set-style is used for set method                             */
ep_stat_t w_set_value_script(worker_data_t *wd, parsed_param_name_t *pn,
                             obj_info_t *obj_info, sqlite3 *dbconn,
                             param_info_t param_info[], int param_num,
                             int set_param_index, char *value, int *p_set_status,
                             ep_setParamValueFault_t *setParamFaults)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;
    int  i = 0, res_code, parsed_status;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char *setMethod, *paramName, buf[EP_SQL_REQUEST_BUF_SIZE];
    char setMethodBuf[MAX_METHOD_STR_LEN] = {0};
    char *p_extr_results;
    BOOL  more_instance = TRUE;
    parsed_operation_t parsed_script_str;
    sqlite3_stmt *stmt = NULL;

    /* Save index param names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    setMethod = (strlen(param_info[set_param_index].setMethod) > 0) ?
                    (char *)&param_info[set_param_index].setMethod : (char *)&obj_info->setMethod;

    /* Save method string before parsing */
    memcpy(setMethodBuf, setMethod, sizeof(setMethodBuf));

    w_parse_operation_string(OP_SET, (char *)setMethodBuf, &parsed_script_str);

    paramName = param_info[set_param_index].paramName;

    i = 0;
    while (more_instance == TRUE)
    {
        /* Prepare shell command (with all needed info) and perform it */
        status1 = w_prepare_command(wd, pn, obj_info, dbconn, &parsed_script_str,
                     idx_params, idx_values, idx_params_num, (char*)&buf, sizeof(buf), &stmt);
        if (status1 != EPS_OK)
        {
            if (status1 != EPS_NOTHING_DONE)
                GOTO_RET_WITH_ERROR(status1, "Could not prepare set command for param %s", paramName);

            more_instance = FALSE;
            continue;
        }
        /* Insert name and value of param to be set (value is placed in quotes)*/
        strcat_safe(buf, " -pname ", sizeof(buf));
        strcat_safe(buf, paramName, sizeof(buf));
        strcat_safe(buf, " -pvalue '", sizeof(buf));
        strcat_safe(buf, value, sizeof(buf));
        strcat_safe(buf, "'", sizeof(buf));

        DBG("Full prepared command (%d): \n\t%s", ++i, buf);

        /* Perform prepared command and parse results. Results look as follows:
         in case of failure:  "resCode;", where resCode in not 0
         in case of success:  "0; setStatus" or "0;", where setStatus is 0 or 1 */
        p_extr_results = w_perform_prepared_command(buf, sizeof(buf), TRUE, NULL);
        if (!p_extr_results)
            GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not read script results");

        DBG("Result of the command: %s ", buf);
        status = w_parse_script_res(buf, sizeof(buf), &res_code, &parsed_status, NULL);
        if (status == EPS_OK)
        {
            //DBG ("Parsed script results: resCode %d, setStatus %d", res_code, parsed_status);
            if (parsed_status > 0)
                *p_set_status = parsed_status;
        }

        if (res_code == 0)
        {
            /* Now update appropriated instance in the values db (if needed)*/
            if (!param_info[set_param_index].notSaveInDb)
            {
                w_update_values_db(dbconn, obj_info, paramName,
                               param_info[set_param_index].paramType,
                               value, idx_params, idx_values, idx_params_num);
            }
            else
                DBG("Param %s should not be saved in DB",param_info[set_param_index].paramName);
        }
        else // Script failed - fill fault info in the answer buffer
        {
            if (setParamFaults != NULL)
            {
                DBG("Script returned error: %d; Update set faults", res_code);
                namefaultpair_t *pFault = &setParamFaults->paramFaults[setParamFaults->arraySize];

                w_place_indeces_to_objname(obj_info->objName, (int *)idx_values,
                                          idx_params_num, pFault->name);
                strcat_safe(pFault->name, paramName, NVP_MAX_NAME_LEN);
                pFault->faultcode = w_status2cwmp_error(EPS_BACKEND_ERROR);
                setParamFaults->arraySize++;
            }
        }

        if (stmt == NULL) more_instance = FALSE;

    } //End of while stmt over instances

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/* Handler function for processing setParamValue operation in case of
   "shell-script" set-style is used for set method                       */
ep_stat_t w_set_value_shell(worker_data_t *wd, parsed_param_name_t *pn,
                            obj_info_t *obj_info, sqlite3 *dbconn,
                            param_info_t param_info[], int param_num,
                            int set_param_index, char *value,
                            int *p_set_status /* ! unused */,
                            ep_setParamValueFault_t *setParamFaults)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;
    int  i = 0, res_code;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char *setMethod, *paramName, buf[EP_SQL_REQUEST_BUF_SIZE];
    char setMethodBuf[MAX_METHOD_STR_LEN] = {0};
    char *p_extr_results;
    BOOL  more_instance = TRUE;
    parsed_operation_t parsed_script_str;
    sqlite3_stmt *stmt = NULL;

    /* Save index param names */
    get_index_param_names(param_info, param_num, idx_params, &idx_params_num);

    paramName = param_info[set_param_index].paramName;

    /* By current design this "shell-script" handler is used only for
     *  Object parameter individually, and can not be used for the entire
     *  Object - so setMethod must be fetched exactly for the parameter */
    setMethod = (char *)&param_info[set_param_index].setMethod;
    if (strlen(setMethod) <= 0)
    {
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR,
            "Missing SetMethod command in meta db for parameter - %s", paramName);
    }

    /* Save method string before parsing */
    memcpy(setMethodBuf, setMethod, sizeof(setMethodBuf));

    /* Parse shell-script setMethod in the following format:
     *   <command with placeholders>; <substitution values (comma separated)>
     *
     * (Here substitution values named '@name' and '@value' can be present.
     *  They are decoded as the set parameter name and value)
     */
    w_parse_shell_set_method_string((char *)setMethodBuf, &parsed_script_str);

    for (i = 0; i < parsed_script_str.subst_val_num; i++)
    {
        if (parsed_script_str.subst_val[i].name_formatter)
        {
            /* Assign the substitution '@name' value */
            parsed_script_str.subst_val[i].name_val = (char *)paramName;
        }
        else if (parsed_script_str.subst_val[i].value_formatter)
        {
            /* Assign the substitution '@value' value */
            parsed_script_str.subst_val[i].value_val = (char *)value;
        }
    }

    i = 0;
    while (more_instance == TRUE)
    {
        /* Prepare shell command (with all needed info) and perform it */
        status1 = w_prepare_command(wd, pn, obj_info, dbconn, &parsed_script_str,
                     idx_params, idx_values, idx_params_num, (char*)&buf, sizeof(buf), &stmt);
        if (status1 != EPS_OK)
        {
            if (status1 != EPS_NOTHING_DONE)
                GOTO_RET_WITH_ERROR(status1, "Could not prepare set command for param %s", paramName);

            more_instance = FALSE;
            continue;
        }
        /* Insert the final part of shell command:
         * NOTE: stdout/stderr of the command are only printed to logfile
         * TODO: think if analysis of stdout/stderr could be helpful */
        strcat_safe(buf, " ; exit $?", sizeof(buf));

        DBG("Full prepared command (%d): \n\t%s", ++i, buf);

        res_code = -1;
        /* Perform prepared command and parse results. Result is the common
         *  shell's integer resCode: 0 means success, not 0 - failure */
        p_extr_results = w_perform_prepared_command(buf, sizeof(buf), TRUE, &res_code);
        if (!p_extr_results)
        {
            WARN("Could not read shell-script results");
            DBG("Result of the command: shell exit code: %d", res_code);
        }
        else
        {
            DBG("Result of the command: shell exit code: %d, output:\n\t%s", res_code, buf);
        }

        /* Shell-script results is just the resCode (= shell command exit code)
         * (setStatus is assumed to be always '0' so no post-action like
         *  backend restart is needed) */
        if (res_code == 0)
        {
            /* Now update appropriated instance in the values db (if needed)*/
            if (!param_info[set_param_index].notSaveInDb)
            {
                w_update_values_db(dbconn, obj_info, paramName,
                               param_info[set_param_index].paramType,
                               value, idx_params, idx_values, idx_params_num);
            }
            else
                DBG("Param %s should not be saved in DB",
                     param_info[set_param_index].paramName);
        }
        else /* Script failed - fill fault info in the answer buffer */
        {
            DBG("Shell-script returned error: %d; %s Update set faults",
                 res_code, (setParamFaults) ? "Running" : "Omitting");

            if (setParamFaults != NULL)
            {
                namefaultpair_t *pFault = &setParamFaults->paramFaults[setParamFaults->arraySize];

                w_place_indeces_to_objname(obj_info->objName, (int *)idx_values,
                                          idx_params_num, pFault->name);
                strcat_safe(pFault->name, paramName, NVP_MAX_NAME_LEN);
                pFault->faultcode = w_status2cwmp_error(EPS_BACKEND_ERROR);
                setParamFaults->arraySize++;
            }
        }

        if (stmt == NULL) more_instance = FALSE;

    } /* End of while stmt over instances */

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}


#define MMX_OWN_PARAM_REBOOT         "Reboot"
#define MMX_OWN_PARAM_SHUTDOWN       "Shutdown"
#define MMX_OWN_PARAM_FACTORYRESET   "FactoryReset"
#define MMX_OWN_PARAM_RESTARTMMX     "RestartMMX"
#define MMX_OWN_PARAM_REFRESHDATA    "RefreshData"
#define MMX_OWN_PARAM_SAVECONFIG     "SaveConfig"
#define MMX_OWN_PARAM_RESTORECONFIG  "RestoreConfig"
#define MMX_OWN_PARAM_FACTORYRST_KEEP_CONN   "FactoryResetKeepConn"
#define MMX_OWN_PARAM_CREATECAND     "CreateCandidateConfig"
#define MMX_OWN_PARAM_COMMITCAND     "CommitCandidateConfig"
#define MMX_OWN_PARAM_RESETCAND      "ResetCandidateConfig"

ep_stat_t w_set_mmx_own_params(worker_data_t *wd, ep_message_t *answer,
                               parsed_param_name_t *pn, obj_info_t *obj_info,
                               param_info_t param_info[], int param_num,
                               int set_param_index, char *value, int *p_set_status)
{
    ep_stat_t status = EPS_OK;
    char *paramName = param_info[set_param_index].paramName;

    DBG("Parameter: %s, value %s, index %d", paramName, value, set_param_index);

    if (strcmp(trim(value), "true") != 0)
    {
        GOTO_RET_WITH_ERROR(EPS_INVALID_ARGUMENT,
            "Bad value '%s' for setting %s param - only 'true' is accepted",
             value, paramName);
    }

    if (!strcmp((const char *)paramName, MMX_OWN_PARAM_REBOOT))
    {
        w_reboot(wd, 0, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_SHUTDOWN))
    {
        w_shutdown(wd, 0, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_FACTORYRESET))
    {
        w_reset(wd, 0, RSTTYPE_FACTORY_RESET, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_FACTORYRST_KEEP_CONN))
    {
        w_reset(wd, 0, RSTTYPE_FACTORY_RESET_KEEPCONN, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_RESTORECONFIG))
    {
        w_restore_config(wd, 0, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_SAVECONFIG))
    {
        w_save_configuration(wd, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_REFRESHDATA))
    {
        w_refresh_data(wd, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_CREATECAND))
    {
        w_save_config_to_candidate(wd, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_COMMITCAND))
    {
        w_restore_candidate_config(wd, 0, answer);
    }
    else if (!strcmp((const char *)paramName, MMX_OWN_PARAM_RESETCAND))
    {
        w_remove_candidate_config(wd, answer);
    }

ret:
    return status;
}

/* Handler function for processing setParamValue operation in case of
   "backend" set-style is used for set method                            */
ep_stat_t w_set_value_backend( worker_data_t *wd, parsed_param_name_t *pn,
                obj_info_t *obj_info, sqlite3 *dbconn,
                param_info_t param_info[], int param_num,
                int set_param_index, int paramValuesNum, nvpair_t *paramValues,
                int *setStatus )
{
    ep_stat_t status = EPS_OK;
    int res, be_port;
    int idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char *setMethod;
    char setMethodBuf[MAX_METHOD_STR_LEN] = {0};
    BOOL more_instance = TRUE;
    parsed_backend_method_t parsed_method;
    mmxba_request_t be_ans;
    char query[EP_SQL_REQUEST_BUF_SIZE];
    sqlite3_stmt *stmt = NULL;

    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    DBG("backend set %s", param_info[set_param_index].paramName);

    setMethod = (strlen(param_info[set_param_index].setMethod) > 0) ?
                (char *)&param_info[set_param_index].setMethod : (char *)&obj_info->setMethod;

    /* Save method string before parsing */
    memcpy(setMethodBuf, setMethod, sizeof(setMethodBuf));

    /* Get port number of the backend*/
    if ((w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0) != EPS_OK) ||
        (be_port <= 0))
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s",
                            be_port, obj_info->backEndName);

    DBG("Backend '%s': port %d", obj_info->backEndName, be_port);

    w_parse_backend_method_string(OP_SET, (char *)setMethodBuf, &parsed_method);

    if (idx_params_num > 0)
    {
        w_form_subst_sql_select_backend(wd, pn, obj_info, query, sizeof(query), idx_params,
                                        idx_params_num, &parsed_method);
        DBG("Subst query:\n%s", query);

        if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                sqlite3_errmsg(dbconn));
    }

    while (more_instance)
    {
        if (stmt)
        {
            res = sqlite3_step(stmt);
            if ((res != SQLITE_ROW) && (res != SQLITE_DONE))
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s",
                                    sqlite3_errmsg(dbconn));
            else if (res == SQLITE_DONE)
                more_instance = FALSE;
        }

        if (more_instance)
        {
            /* Form BE API request, send it to the backend, wait for reply */
            status = form_and_send_be_request (wd, be_port, MMXBA_OP_TYPE_SET,
                                              &parsed_method,stmt, idx_params_num, 1,
                                              &param_info[set_param_index],
                                              paramValuesNum, paramValues, &be_ans);
           if (status != EPS_OK)
              GOTO_RET_WITH_ERROR(status, "BE SET request failure (%d)", status);

            if (be_ans.opResCode != 0)
            {
                if (strlen(be_ans.errMsg) > 0)
                    ERROR("Backend returned error message:\n %d: %s", be_ans.opExtErrCode, be_ans.errMsg);

                GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,"Backend returned error (%d)",be_ans.opResCode);
            }

            if (setStatus && (be_ans.postOpStatus != 0))
            {
                DBG("postOpStatus for SET operation = %d",  be_ans.postOpStatus);
                *setStatus = 1;
            }
        }

        if (!stmt) more_instance = FALSE;
    }

ret:
    if (stmt) sqlite3_finalize(stmt);

    return status;
}

/* be_to_restart is array. Not-0 entries correspond to backends that
   should be restarted */
static ep_stat_t w_restart_backends(int reqType,  int *be_to_restart)
{
    ep_stat_t status = EPS_OK;
    int        i = 0, cnt = 0;
    char       buf[EP_SQL_REQUEST_BUF_SIZE];
    backend_info_t beInfo;

    for (i = 0; i < MAX_BACKEND_NUM; i++)
    {
        if (be_to_restart[i])
        {
            memset((char *)&beInfo, 0, sizeof(beInfo));
            memset(buf, 0, sizeof(buf));
            if ((status = ep_common_get_beinfo_by_index(i, &beInfo)) != EPS_OK)
                continue;

            if (strlen(beInfo.initScript) == 0)
                continue;

            /* Prepare command for the backend restart */
            strcpy_safe(buf, "/etc/init.d/", sizeof(buf));
            strcat_safe(buf, trim(beInfo.initScript), sizeof(buf));
            strcat_safe(buf, " restart &",  sizeof(buf));

            DBG("Perform backend %s restart command: %s",beInfo.beName, buf);
            w_perform_prepared_command(buf, sizeof(buf), FALSE, NULL);
            cnt++;

            //sleep(1); //TODO!!! test this when many backends are restarted
        }
    }

    if (cnt > 0)
        DBG(" %d backends were restarted after %s operation", cnt,msgtype2str(reqType) );

    return status;
}

static ep_stat_t w_handle_setvalue(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK, total_status = EPS_OK;
    int i, j, s, set_param_index, prev_fault_arrsize = 0;
    int setStatus = 0, total_setStatus = 0, minFaultCode = 0;
    int obj_num, param_num, setStyle = 0;
    int restart_be[MAX_BACKEND_NUM];
    BOOL mmx_own_params = FALSE, dbSave = FALSE;

    obj_info_t           obj_info[1];
    param_info_t         param_info[MAX_PARAMS_PER_OBJECT];
    parsed_param_name_t  pn;
    namefaultpair_t      *pFault;
    nvpair_t             *p_setPairs;

    sqlite3 *dbconn = NULL;
    ep_message_t answer = {{0}};

    /* Init response message */
    memset((char *)&answer, 0, sizeof(answer));
    mmx_frontapi_msg_struct_init(&answer, (char *)wd->fe_resp_values_pool,
                                 sizeof(wd->fe_resp_values_pool));
    memcpy(&answer.header, &message->header, sizeof(answer.header));
    answer.header.respFlag = 1;
    answer.header.msgType = MSGTYPE_SETVALUE_RESP;

    memset((char *)&restart_be, 0, sizeof(restart_be));

    if (operAllowedForDbType(OP_SET, message->header.mmxDbType) != TRUE)
    {
        ERROR("Operation SET is not permitted for db type %d", message->header.mmxDbType);
        answer.header.respCode = w_status2cwmp_error(EPS_INVALID_DB_TYPE);
        w_send_answer(wd, &answer);
        return status;
    }

    if ((status = w_init_mmxdb_handles(wd, message->header.mmxDbType,
                                     message->header.msgType)) != EPS_OK)
    {
        answer.header.respCode = w_status2cwmp_error(status);
        w_send_answer(wd, &answer);
        return status;
    }

    dbconn = wd->main_conn;

    /*---SetParamValue is write operation. EP write-lock must be received ---*/
    status = ep_common_get_write_lock(MSGTYPE_SETVALUE, message->header.txaId,
                                      message->header.callerId);
    if (status != EPS_OK)
    {
        ERROR("Could not receive EP write lock for SetParamValue");
        answer.header.respCode = w_status2cwmp_error(status);
        w_send_answer(wd, &answer);
        return status;
    }

    /* For each request parameter */
    for (i = 0; i < message->body.setParamValue.arraySize; i++)
    {
        status = EPS_OK;
        setStatus = 0;
        set_param_index = -1;
        prev_fault_arrsize = answer.body.setParamValueFaultResponse.arraySize;
        p_setPairs = (nvpair_t *)&message->body.setParamValue.paramValues;

        /* Parse request string: extract object name, parameter name, arguments provided*/
        if ((status = parse_param_name(p_setPairs[i].name, &pn)) != EPS_OK)
        {
            status = EPS_INVALID_FORMAT;
            ERROR("Could not parse object and parameter name %s", p_setPairs[i].name);
        }

        if ((status == EPS_OK) && (pn.partial_path || (strlen(pn.leaf_name) == 0)))
        {
            status = EPS_INVALID_FORMAT;
            ERROR("Parameter name not specified in %s",p_setPairs[i].name );
        }

        /* Acquire information about the object from the meta DB */
        if ((status == EPS_OK) &&
             (status = w_get_obj_info(wd, &pn, 0, 0, obj_info, 1, &obj_num)) != EPS_OK)
        {
            ERROR("Could not get object info for %s (status %d)", p_setPairs[i].name, status);
        }

        if ((status == EPS_OK) && (obj_num != 1))
        {
            status = EPS_INVALID_FORMAT;
            ERROR("Ambiguous parameter name: %d objects retrieved for %s", obj_num, pn.obj_name);
        }

        mmx_own_params = strcmp(obj_info->objName, MMX_OWN_OBJ_NAME) ? FALSE : TRUE;
        if ((status == EPS_OK) &&
            mmx_own_params && message->header.mmxDbType != MMXDBTYPE_RUNNING)
        {
            status = EPS_NO_PERMISSION;
            ERROR("MMX own params can be set in Running DB only");
        }

        if ((status == EPS_OK) &&
            ((status = w_get_param_info(wd, &pn, obj_info, 0, param_info,
                                        &param_num, &set_param_index)) != EPS_OK))
        {
            ERROR("Could not get parameters info (status %d)", status);
        }

        if ((status == EPS_OK) &&
            !paramWriteAllowed(param_info, set_param_index, message->header.callerId))
        {
            status = EPS_NO_PERMISSION;
            ERROR("Parameter %s cannot be changed by caller %d",
                   p_setPairs[i].name, message->header.callerId);
        }
        if ((status == EPS_OK) && !param_info[set_param_index].writable )
        {
            status = EPS_NOT_WRITABLE;
            ERROR("Parameter %s is not writable", p_setPairs[i].name);
        }

        if (status == EPS_OK)
        {
            if (!param_info[set_param_index].notSaveInDb &&
                (message->body.setParamValue.setType & MMX_SETTYPE_FLAG_SAVE))
                dbSave = TRUE;

            /* Check set style: if not defined or it is not running DB,
                DB style will be used. For DBs */
            if (message->header.mmxDbType == MMXDBTYPE_RUNNING)
            {
                if ((param_info[set_param_index].setOperStyle != OP_STYLE_NOT_DEF) &&
                    (param_info[set_param_index].setOperStyle != OP_STYLE_ERROR))
                {
                    setStyle = param_info[set_param_index].setOperStyle;
                    DBG("Param setStyle '%s' is used for %s",
                        operstyle2string(setStyle), pn.leaf_name);
                }
                else if ((obj_info->setOperStyle != OP_STYLE_NOT_DEF) &&
                         (obj_info->setOperStyle != OP_STYLE_ERROR))
                {
                    /* Set style OP_STYLE_SHELL_SCRIPT is currently supported just
                     *  for Object parameters individually but not for the Object */
                    if (obj_info->setOperStyle == OP_STYLE_SHELL_SCRIPT)
                    {
                        setStyle = OP_STYLE_DB;
                        DBG("Object's setStyle 'shell-script' is not supported, "
                            "so default 'db' setStyle is used for %s", pn.leaf_name);
                    }
                    else
                    {
                        setStyle = obj_info->setOperStyle;
                        DBG("Object's setStyle '%s' is used for %s",
                            operstyle2string(setStyle), pn.leaf_name);
                    }
                }
            }
            else
            {
                setStyle = OP_STYLE_DB;
                DBG("SetStyle '%s' is used for %s (DB type is %d)",
                    operstyle2string(setStyle), pn.leaf_name, message->header.mmxDbType);
            }

            DBG("mmx_own_params = %s; setStyle = %d; dbSave = %d",
                     mmx_own_params ? "true" : "false", setStyle, dbSave);
        }

        /* Call per-style handler functions to perform SetParamValue operation */
        if (status == EPS_OK)
        {
            if (!mmx_own_params)
            {
            switch (setStyle)
            {
                case OP_STYLE_UBUS:
                    status = w_set_value_ubus(wd, &pn, obj_info, param_info, param_num,
                                                set_param_index, p_setPairs[i].pValue);
                    break;
                case OP_STYLE_UCI:
                    status = w_set_value_uci(wd, &pn, obj_info, dbconn, param_info, param_num,
                                                set_param_index, p_setPairs[i].pValue, &setStatus);
                    break;
                case OP_STYLE_SCRIPT:
                    status = w_set_value_script(wd, &pn, obj_info, dbconn, param_info, param_num,
                                set_param_index, p_setPairs[i].pValue, &setStatus, &answer.body.setParamValueFaultResponse);
                    break;
                case OP_STYLE_SHELL_SCRIPT:
                    status = w_set_value_shell(wd, &pn, obj_info, dbconn, param_info, param_num,
                                set_param_index, p_setPairs[i].pValue, &setStatus, &answer.body.setParamValueFaultResponse);
                    break;
                case OP_STYLE_BACKEND:
                    status = w_set_value_backend(wd, &pn, obj_info, dbconn, param_info, param_num, set_param_index,
                                                    1, &(p_setPairs[i]), &setStatus);
                    break;
                case OP_STYLE_DB:
                    status = w_set_value_db(wd, &pn, obj_info, dbconn, param_info, param_num,
                                            set_param_index, p_setPairs[i].pValue);
                    break;
                default:
                    ERROR("unknown set operation style %d", param_info[set_param_index].setOperStyle);
                    status = EPS_NOT_IMPLEMENTED;
                }
            }
            else
            {
                status = w_set_mmx_own_params(wd, &answer, &pn, obj_info,param_info, param_num,
                                              set_param_index, p_setPairs[i].pValue, &setStatus);
            }
        }

        /* Check results of SetParamValue operation  */
        if ((status == EPS_OK) &&
             (prev_fault_arrsize == answer.body.setParamValueFaultResponse.arraySize))
        {
            DBG("Successfull SET operation for parameter %s", p_setPairs[i].name);
            if (setStatus != 0)
            {
                total_setStatus = 1;
                if ((j = ep_common_get_beinfo_index(obj_info->backEndName)) >= 0)
                    restart_be[j] = TRUE;
            }

            /* Update values DB (for not DB or SCRIPT or SHELL_SCRIPT styles,
               otherwise - already updated) */
            if ((setStyle != OP_STYLE_DB) &&
                (setStyle != OP_STYLE_SCRIPT) &&
                (setStyle != OP_STYLE_SHELL_SCRIPT))
            {
                status1 = w_set_value_db(wd, &pn, obj_info, dbconn, param_info, param_num,
                                        set_param_index, p_setPairs[i].pValue);
                if (status1 != EPS_OK)
                {
                    ERROR("Could not save value in db after SET operation (status %d)", status1);
                    //TODO: what can we do in such a case?
                }
            }
        }
        else /* parameter set operation has failed, check if we need to fill
                paramfaults in response body*/
        {
            if (status != EPS_OK)
            {
                /* We need to fill paramfaults in response body here */
                total_status = status;
                s = answer.body.setParamValueFaultResponse.arraySize;
                pFault =  &answer.body.setParamValueFaultResponse.paramFaults[s];

                memcpy(pFault->name, p_setPairs[i].name, strlen(p_setPairs[i].name));
                pFault->faultcode = w_status2cwmp_error(status);
                answer.body.setParamValueFaultResponse.arraySize++;

                DBG("SET failed for %s; status %d, faultcode %d, index %d",
                   p_setPairs[i].name, status, pFault->faultcode, s);
            }
            else
            {
                /* handler function failed and filled fault codes by itself */
                DBG("Set operation failed, new fault array size %d",
                     answer.body.setParamValueFaultResponse.arraySize);
            }

            /* Set failed - don't continue to process other parameters*/
            break;
        }

    }  // End of for stmt over received parameters

    if (message->header.mmxDbType == MMXDBTYPE_RUNNING)
    {
        if ((dbSave == TRUE) && (total_status == EPS_OK) &&
             (answer.body.setParamValueFaultResponse.arraySize == 0))
            w_save_configuration(wd, NULL);
    }
    else if (message->header.mmxDbType == MMXDBTYPE_CANDIDATE)
    {
        char buf[FILENAME_BUF_LEN] = {0};
        w_save_file(get_db_cand_path((char*)buf, FILENAME_BUF_LEN));
    }

    /* ------- Release EP write operation lock -------*/
    ep_common_finalize_write_lock(message->header.txaId, message->header.callerId, FALSE);

    /* Prepare answer and send it to the caller */
    if (answer.body.setParamValueFaultResponse.arraySize == 0 &&
        total_status == EPS_OK)
    {
        answer.header.respCode = 0;
        answer.body.setParamValueResponse.status = total_setStatus;
        DBG("SET results: %d requested param(s) were successfully processed, status = %d",
                message->body.setParamValue.arraySize, setStatus);
    }
    else
    {
        minFaultCode = w_status2cwmp_error(EPS_NOT_WRITABLE);

        if ((total_status != 0) && w_status2cwmp_error(total_status) < minFaultCode)
            minFaultCode = w_status2cwmp_error(total_status);

        for (i = 0; i < answer.body.setParamValueFaultResponse.arraySize; i++)
        {
            if (minFaultCode > answer.body.setParamValueFaultResponse.paramFaults[i].faultcode)
                minFaultCode = answer.body.setParamValueFaultResponse.paramFaults[i].faultcode;
        }
        answer.header.respCode = minFaultCode;
        ERROR("SET operation results: %d of requested %d parameter(s) failed; Total fault code %d",
               answer.body.setParamValueFaultResponse.arraySize,
               message->body.setParamValue.arraySize, minFaultCode);
    }

    /* Send answer to the caller; for successfully processed mmx_own_params
       the answer is already sent */
    if (!mmx_own_params || (total_status != EPS_OK))
        w_send_answer(wd, &answer);

    w_restart_backends(MSGTYPE_SETVALUE, restart_be);

    return status;
}

/* -------------------------------------------------------------------------------*
 * ----------- Functions for procesing GetParamNames request  --------------------*
 * -------------------------------------------------------------------------------*/
/*
 * Functions prepare SQL statement for select indeces of all instances of the
 * object from the specified table of "values DB".
 * Input params:
 *  pn         - ptr to struct containing parsed param name
 *  param_info - array of param_info_t structs of all parameters
 *               of the needed object
 *  param_num  - number of elements in param_info array
 *  valDbConn  - connection to the appropriated values DB
 *  valDbTblName  - DB table name
 * Output params:
 *   p_index_num - number of instance indeces (how many indeces the object has)
 *                 0 is returned in case of non multi-instance object (scalar object)
 *   selIdxStmt  - prepared sqlite3 statement; NULL is returned for scalar obj
 *  Returns:
 *    EPS_OK in case of success, error code - in case of failure.
 */
static ep_stat_t w_prepare_stmt_instances(parsed_param_name_t *pn,
                                          param_info_t param_info[], int param_num,
                                          sqlite3  *valDbConn, char * valDbTblName,
                                          int *p_index_num, sqlite3_stmt **selIdxStmt)
{
    int i, idx_params_num;
    char where[EP_SQL_REQUEST_BUF_SIZE], *p_where = (char *)&where;
    char query[EP_SQL_REQUEST_BUF_SIZE];

    *p_index_num = 0;
    *selIdxStmt = NULL;

    memset(query, 0, sizeof(query));
    memset(where, 0, sizeof(where));

    strcpy_safe(query, "SELECT ", sizeof(query));
    strcpy_safe(p_where, " WHERE 1 ", sizeof(where));
    p_where += strlen(p_where);

    idx_params_num = 0;
    for (i = 0; i < param_num; i++)
    {
        if (param_info[i].isIndex)
        {
            strcat_safe(query, "[", sizeof(query));
            strcat_safe(query, param_info[i].paramName, sizeof(query));
            strcat_safe(query, "], ", sizeof(query));

            /* Check indeces in the received path and form where clause*/
            switch (pn->indices[idx_params_num].type)
            {
            case REQ_IDX_TYPE_EXACT:
                sprintf(p_where, "AND [%s] = %d ",
                        param_info[i].paramName, pn->indices[idx_params_num].exact_val.num);
                DBG("where clause 1: %s", where);
                p_where += strlen(p_where);
                break;
            case REQ_IDX_TYPE_RANGE:
                sprintf(p_where, "AND  ([%s] BETWEEN %d AND %d ) ", param_info[i].paramName,
                        pn->indices[idx_params_num].range_val.begin,
                        pn->indices[idx_params_num].range_val.end);
                DBG("where clause 2: %s", where);
                p_where += strlen(p_where);
                break;
            case REQ_IDX_TYPE_ALL:
                /* no restriction for that index */
                break;
            case REQ_IDX_TYPE_PLACEHOLDER:
                /* no restriction for that index (actually
                 * placeholder index must never occur here
                 * - only {exact, range, all} are expected) */
                break;
            } // End of switch stmt

            idx_params_num++;
        } //End of index parameter processing

    } // End of for over all parameters

    if (idx_params_num == 0)
        return EPS_OK;  //It is a scalar object. Do nothing.

    /* Complete preparing SELECT query (with FROM and WHERE clauses) */
    LAST_CHAR(query) = '\0'; // remove the last comma and space
    LAST_CHAR(query) = '\0';
    strcat_safe(query, "  FROM ", sizeof(query));
    strcat_safe(query, valDbTblName, sizeof(query));
    strcat_safe(query, where, sizeof(query));
    //DBG("query to select instances: %s", query);

    if (sqlite3_prepare_v2(valDbConn, query, -1, selIdxStmt, NULL) != SQLITE_OK)
    {
        DBG("Couldn't prepare SQL statement to select indexes (%)", sqlite3_errmsg(valDbConn));
        return EPS_SQL_ERROR;
    }

    *p_index_num = idx_params_num;
    return EPS_OK;
}

/*
 * Function inserts results to GetParamNames to the answer structure.
 * If complete path name stated in the request, information about the
 * specified parameter (pn->leaf_name) is retrieved and inserted to the
 * answer buffer. In case of partial path, information about object's
 * instance itself and about all its parameter are inserted to the answer.
 * Input params:
 *   pn     - ptr to struct containing parsed param name
 *   answer - ptr to the answer structure
 *   obj_info - ptr to object info structure of the object
 *   param_info - array of param_info_t structs of all object's parameters
 *   param_num  - number of elements in param_info array
 *   idx_num  - number of object indeces
 *   stmt  - SQL statement containing results of instance selection
 *            (i.e. all indeces of all object's instances)
 *  Returns:
 *     EPS_OK - in case of success, error code - otherwise
 */
static ep_stat_t w_insert_gpn_res_to_answer(parsed_param_name_t *pn,
                                            ep_message_t *answer,
                                            const obj_info_t *obj_info,
                                            param_info_t param_info[], int param_num,
                                            int idx_num, int *idx_values)
{
    ep_stat_t status = EPS_OK;
    int i, j, arrsize, newsize;
    BOOL writable;
    char* pname;

    /* Check if there is enough space in the answer buffer */
    if (!pn->partial_path)
        newsize = answer->body.getParamNamesResponse.arraySize + 1;
    else
        newsize = answer->body.getParamNamesResponse.arraySize + param_num;

    if (newsize >= MAX_NUMBER_OF_GPN_RESPONSE_VALUES -1)
        GOTO_RET_WITH_ERROR(EPS_NO_MORE_ROOM,
        "Not enough space for param(s) of object %s", obj_info->objName);


    /* Check permission to object access. If no permission - just ignore */
    if (!objReadAllowed(obj_info, answer->header.callerId))
    {
        DBG("Object %s is not readable for the requestor %d", obj_info->objName,
             answer->header.callerId);
        return EPS_IGNORED;
    }

    /*If param name is partial path - add object's instance info to the answer*/
    if (pn->partial_path)
    {
        /* Determine correct value of object's "writable" attribute */
        writable = obj_info->writable;
        if (!objWriteAllowed(obj_info, answer->header.callerId))
        {
            DBG("Object %s is not writable for the requestor %d", obj_info->objName,
                 answer->header.callerId);
            writable = 0;
        }

        arrsize = answer->body.getParamNamesResponse.arraySize;
        pname = answer->body.getParamNamesResponse.paramInfo[arrsize].name;

        w_place_indeces_to_objname(obj_info->objName, idx_values, idx_num, pname);
        DBG("Object instance: %s (index %d)", pname, arrsize);
        answer->body.getParamNamesResponse.paramInfo[arrsize].writable = writable;
        answer->body.getParamNamesResponse.arraySize++;
    }

    /* Process all object's parameters */
    for (j = 0; j < param_num; j++)
    {
        /* If complete path is used - we need to get info about one parameter only */
        if (!pn->partial_path && strcmp(pn->leaf_name, param_info[j].paramName))
             continue;

        /* Check if this parameter is "implemented", i.e. has get-methosd*/
        if ((param_info[j].getOperStyle == OP_STYLE_NOT_DEF) &&
            (obj_info->getOperStyle == OP_STYLE_NOT_DEF))
        {
            DBG("Parameter `%s' still not full-implemented", param_info[j].paramName);
            continue;
        }

        /* Determine if the parameter can be read by the requestor */
        if (!paramReadAllowed(param_info, j, answer->header.callerId))
        {
            DBG("Parameter `%s' is not allowed to be read by requestor (%d)",
                   param_info[j].paramName, answer->header.callerId);
            continue;
        }

        /* writable" attribute is determined based on "writable" property
           of the parameter and additionaly on caller ID. */
        writable = (char)param_info[j].writable;
        if (!paramWriteAllowed(param_info, j, answer->header.callerId))
            writable = 0;

        /* Add parameter to the answer buffer  */
        arrsize = answer->body.getParamNamesResponse.arraySize;
        pname = answer->body.getParamNamesResponse.paramInfo[arrsize].name;

        w_place_indeces_to_objname(obj_info->objName, idx_values, idx_num, pname);
        strcat_safe(pname, param_info[j].paramName, NVP_MAX_NAME_LEN);
        answer->body.getParamNamesResponse.paramInfo[arrsize].writable = writable;
        answer->body.getParamNamesResponse.arraySize++;

    }  // End of "for" over all parameters of he object

ret:
    return status;
}

/*
 *  Call this function only for multi instance root objects tailing with {i}.
 *  For example,
 *      Device.Routers.{i}.
 *      Device.Users.User.{i}.
 *  Use func "is_multi_instance_root_object" to understand that
 *
 *  Returns:
 *     EPS_OK - in case of success, error code - otherwise
 */
static ep_stat_t w_insert_multi_instance_root_object_to_answer(ep_message_t *answer, obj_info_t *obj_info,
                                                               int idx_num, int *idx_values)
{
    ep_stat_t status;
    size_t len = strlen(obj_info->objName);
    char *p_ph_in_objname = obj_info->objName + len - LEN_PLACEHOLDER - 1 /* . */;
    const char *last_sign = p_ph_in_objname + LEN_PLACEHOLDER;

    parsed_param_name_t pn = {.partial_path = TRUE};

    if (strncmp(p_ph_in_objname, INDEX_PLACEHOLDER, LEN_PLACEHOLDER) != 0)
        return EPS_IGNORED;

    if (*last_sign != '.')
        return EPS_IGNORED;

    *p_ph_in_objname = '\0';
    status = w_insert_gpn_res_to_answer(&pn, answer, (const obj_info_t *)obj_info, NULL, 0, idx_num - 1, idx_values);

    *p_ph_in_objname = INDEX_PLACEHOLDER[0];

    return status;
}


/*
 *  Get param names from current object(including parameters and subobjects)
 *  Previous values are keeped for checking the next level. If next subjects doesnt have
 *  values, just send subobject name, like:
 *     Device.X_Inango_BbfSubIf.47.InlineFrameProcessing.IngressRule.
 *     Device.X_Inango_BbfSubIf.48.InlineFrameProcessing.IngressRule.
 *
 *  Returns:
 *     EPS_OK - in case of success, error code - otherwise
 */
static ep_stat_t w_do_instance_object(worker_data_t *wd,
                                             ep_message_t *answer,
                                             parsed_param_name_t *pn,
                                             obj_info_t *obj_info,
                                             sqlite3        *vdb_conn,
                                             int previous_values[][MAX_INDECES_PER_OBJECT],
                                             int *previous_num_values,
                                             int *previous_num_idx,
                                             int total_cnt
                                             )
{
    ep_stat_t status = EPS_OK; //, status1 = EPS_OK;
    int  index_num, param_num,  res;
    int  inst_cnt = 0, obj_cnt = 0, resp_cnt = 0;
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    sqlite3_stmt   *selIdxStmt  = NULL;
    int idx_params_num = 0, idx_row = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int current_values[MAX_INSTANCES_PER_OBJECT][MAX_INDECES_PER_OBJECT] = {{0}};
    int current_num_values = 0;
    int current_num_idx = 0;
    int idx_values[MAX_INDECES_PER_OBJECT] = {0};

    status = w_get_param_info(wd, pn, obj_info, 0, param_info, &param_num, NULL);
    if (status != EPS_OK)
    {
        if (!pn->partial_path)
        {
            ERROR("Couldn't get param info for object %s (%d)", obj_info->objName, status);
            return EPS_SYSTEM_ERROR;
        }
        else  //Looks impossible; just in case print log and go to next obj
        {
            ERROR("Could not get param info for object %s", obj_info->objName);
            goto ret;
        }
    }

    /* Prepare sqlite statement to select needed instanses of the object */
    status =  w_prepare_stmt_instances(pn, param_info, param_num, vdb_conn,
                             obj_info->objValuesTblName, &index_num, &selIdxStmt);
    if (status != EPS_OK)
    {
        status = EPS_OK;
        goto ret;
    }

    /* Insert param (or object's params) info to the answer buffer. If not
     * enough space in the buffer, send the prepared answer to the caller
     * and repeat inserting to the "new" response buffer.              */
    if (index_num == 0) // If this is scalar (i.e. one-instance) object
    {
        status = w_insert_gpn_res_to_answer(pn, answer, obj_info, param_info,
                                            param_num, 0, idx_values);
        if (status == EPS_OK)
            inst_cnt++;
        if (status == EPS_NO_MORE_ROOM)
        {
            DBG("Response %d: %d of %d obj processed, %d instances, %d elements were sent", obj_cnt,
                ++resp_cnt, total_cnt, inst_cnt, answer->body.getParamNamesResponse.arraySize);
            answer->header.moreFlag = 1;
            w_send_answer(wd, answer);
            status = w_insert_gpn_res_to_answer(pn, answer, obj_info, param_info,
                                                param_num, 0, idx_values);
        }
    }
    /* This if multi-instance object. Select all it's instances */
    else
    {
        res = sqlite3_step(selIdxStmt);
        if ((res != SQLITE_ROW) && (res != SQLITE_DONE))
            DBG("Cannot select instance: %d, error: %s ", res, sqlite3_errmsg(vdb_conn));

        /* Check if there no rows returned*/
        if (res == SQLITE_DONE)
        {
            DBG("SELECT return no rows");
            for (int i = 0; i < *previous_num_values; i++)
            {
                status = w_insert_multi_instance_root_object_to_answer(answer, obj_info, index_num, previous_values[i]);
                if (status == EPS_NO_MORE_ROOM)
                {
                    DBG("Response %d: %d of %d obj processed, %d instances, %d elements were sent", obj_cnt,
                        ++resp_cnt, total_cnt, inst_cnt, answer->body.getParamNamesResponse.arraySize);
                    answer->header.moreFlag = 1;
                    w_send_answer(wd, answer);
                    w_insert_multi_instance_root_object_to_answer(answer, obj_info, index_num, previous_values[i]);
                }
            }
            memcpy(previous_values, current_values, sizeof(current_values));
            *previous_num_values = 0;
            *previous_num_idx = 0;
            goto ret;
        }

        current_num_idx = index_num;
        /* Retrive all indeces of the instance received from DB */
        memset(idx_values, 0, sizeof(idx_values));
        for (int i = 0; i < index_num; i++)
        {
            idx_values[i] = sqlite3_column_int(selIdxStmt, i);
            /* Remember value*/
            current_values[current_num_values][i] = idx_values[i];
        }

        status = w_insert_multi_instance_root_object_to_answer(answer, obj_info, index_num, idx_values);
        if (status == EPS_NO_MORE_ROOM)
        {
            DBG("Response %d: %d of %d obj processed, %d instances, %d elements were sent", obj_cnt,
                ++resp_cnt, total_cnt, inst_cnt, answer->body.getParamNamesResponse.arraySize);
            answer->header.moreFlag = 1;
            w_send_answer(wd, answer);
            w_insert_multi_instance_root_object_to_answer(answer, obj_info, index_num, idx_values);
        }

        status = EPS_OK;
        while ((res == SQLITE_ROW) &&
               ((status == EPS_OK) || (status == EPS_IGNORED)))
        {
            /* Increase values counter*/
            current_num_values++;

            status = w_insert_gpn_res_to_answer(pn, answer, obj_info, param_info,
                                                param_num, index_num, idx_values);
            if (status == EPS_OK)
                inst_cnt++;
            if (status == EPS_NO_MORE_ROOM)
            {
                DBG("No space, moreFlag 1");
                /*DBG("Response %d: %d of %d obj processed, %d instances, %d elements were sent",
                        ++resp_cnt, obj_cnt, total_cnt, inst_cnt, answer->body.getParamNamesResponse.arraySize );*/
                answer->header.moreFlag = 1;
                w_send_answer(wd, answer);

                status = w_insert_gpn_res_to_answer(pn, answer, obj_info, param_info,
                                                    param_num, index_num, idx_values);
            }

            res = sqlite3_step(selIdxStmt); //Retrieve the next row from DB

            /* Retrive next indeces of the instance received from DB */
            memset(idx_values, 0, sizeof(idx_values));
            for (int i = 0; i < index_num; i++)
            {
                idx_values[i] = sqlite3_column_int(selIdxStmt, i);
                /* Remember last value*/
                current_values[current_num_values][i] = idx_values[i];
            }
        } // End of while over instances of the current object

        BOOL found = FALSE;

        /* If there are subject, check for absence by previous indeces*/
        if (current_num_idx > 1)
        {
            for (int i = 0; i < *previous_num_values; i++)
            {
                for (int j = 0; j < current_num_values; j++)
                {
                    if (previous_values[i][*previous_num_idx - 1] == current_values[j][current_num_idx - 2])
                    {
                        // Current object is found, skip
                        found = TRUE;
                        break;
                    }
                }
                if (found)
                {
                    found = FALSE;
                    continue;
                }
                status = w_insert_multi_instance_root_object_to_answer(answer, obj_info, current_num_idx, previous_values[i]);
                if (status == EPS_NO_MORE_ROOM)
                {
                    DBG("Response %d: %d of %d obj processed, %d instances, %d elements were sent", obj_cnt,
                        ++resp_cnt, total_cnt, inst_cnt, answer->body.getParamNamesResponse.arraySize);
                    answer->header.moreFlag = 1;
                    w_send_answer(wd, answer);
                    w_insert_multi_instance_root_object_to_answer(answer, obj_info, current_num_idx, previous_values[i]);
                }
            }
        }

        /* Keep values for next step*/
        memcpy(previous_values, current_values, sizeof(current_values));
        *previous_num_values = current_num_values;
        *previous_num_idx = current_num_idx;

        DBG("While complete, res %d, status1 %d", res, status);

        if ((status != EPS_OK) && (status != EPS_IGNORED))
        {
            DBG("Fail to insert GPN results for object %s (%d)", obj_info->objName, status);
            goto ret;
        }
        if (status == EPS_OK)
            obj_cnt++;
    }
ret:
    if (selIdxStmt)
    {
        sqlite3_finalize(selIdxStmt);
        selIdxStmt = NULL;
    }
    DBG("Response %d (last): %d of %d objects processed, %d instances, %d elements were sent",
         ++resp_cnt, obj_cnt, total_cnt, inst_cnt, answer->body.getParamNamesResponse.arraySize );
    return status;
}

static ep_stat_t w_handle_getparamnames(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;
    int res;
    obj_info_t *obj_info = NULL;
    int objects_count = 0, current_object = 0;
    parsed_param_name_t pn;
    ep_message_t answer = {{0}};
    sqlite3        *vdb_conn = NULL;
    sqlite3_stmt   *stmt = NULL;
    char count_query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    int previous_values[MAX_INSTANCES_PER_OBJECT][MAX_INDECES_PER_OBJECT];
    int previous_num_values = 0;
    int previous_num_idx = 0;

    memcpy(&answer.header, &message->header, sizeof(answer.header));
    answer.header.respFlag = 1;
    answer.header.msgType = MSGTYPE_GETPARAMNAMES_RESP;

    if ((status = parse_param_name(message->body.getParamNames.pathName, &pn)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not parse name");

    /* Special case: empty name means the top hierarchy name */
    if ((strlen(pn.obj_name)==0))
    {
        strcpy(pn.obj_name, MNG_MODEL_TOP_NAME);
        pn.partial_path = TRUE;
        DBG("Received empty pathname; replaced to the top name %s", pn.obj_name);
    }
    DBG("Received object name %s, next level = %d", pn.obj_name,
                                 message->body.getParamNames.nextLevel);

    if ((status = w_init_mmxdb_handles(wd, message->header.mmxDbType,
                                     message->header.msgType)) != EPS_OK)
        goto ret;

    vdb_conn = wd->main_conn;

    /* Prepare query*/
    sprintf(count_query, "%s WHERE ObjName like '%s%%'", "MMX_Objects_InfoTbl", pn.obj_name);
    /* Get number of objects in MMX_Objects_Info_Tbl*/
    if ((status = ep_db_get_tbl_row_count(wd->mdb_conn, count_query,
                                          &objects_count)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Couldn't get objects count (error %d)", status);

    DBG("Objects %d", objects_count);

    /* Allocate memory for objects struct*/
    obj_info = (obj_info_t *) calloc(objects_count, sizeof(obj_info_t));
    if (!obj_info)
    {
        GOTO_RET_WITH_ERROR(EPS_OUTOFMEMORY,
            "Memory allocation failure - Objects info cannot be stored");
    }
    memset(obj_info, 0, sizeof(obj_info_t) * objects_count);

    /* Prepare SQL stmt to get list of all subsidairy objects */
    if (pn.partial_path && !message->body.getParamNames.nextLevel)
    {
        stmt = wd->stmt_get_obj_list;
        strcat_safe(pn.obj_name, "%", sizeof(pn.obj_name));
    }
    else
    {
        stmt = wd->stmt_get_obj_info;
    }
    res = sqlite3_bind_text(stmt, 1, pn.obj_name, -1, SQLITE_STATIC);
    if (res != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not bind obj name %s to prepared stmt (%d)",
                            pn.obj_name, res);

    /* Go over all subsidairy objects in the management model */
    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        fill_obj_info(stmt, &obj_info[current_object]);
        current_object++;
    } //End of while cycle over all objects

    for (int i = 0; i < objects_count; i++)
    {
        DBG("%d Start insert GPN results for object %s", i, obj_info[i].objName);
        status = w_do_instance_object(wd, &answer, &pn, &obj_info[i], vdb_conn,
                                       previous_values, &previous_num_values, &previous_num_idx, objects_count);
        if (status != EPS_OK)
        {
            DBG("Status is not EPS_OK: %d", status);
        }

        /* Check next object for subject*/
        if ((i + 1) != objects_count)
        {
            if (strstr(obj_info[i+1].objName, obj_info[i].objName) == NULL)
            {
                DBG("Next object is not subject");
                previous_num_values = 0;
                previous_num_idx = 0;
            }

        }
    }

    status = EPS_OK;

ret:

    free(obj_info);

    if (sqlite3_reset(stmt) != SQLITE_OK)
        ERROR("Could not reset sql statement: %s", sqlite3_errmsg(wd->mdb_conn));

    answer.header.respCode = w_status2cwmp_error(status);
    w_send_answer(wd, &answer);
    return status;
}

/* -------------------------------------------------------------------*/
/* ------------------- Add object procedures ------------------------ */
/* -------------------------------------------------------------------*/

/* Helper procedure selecting values of index parameters of the DB table
 * row specified by rowid.
 * Names of index parameters are specified by idx_params array of strings.
 * Index values received from DB are placed to the output idx_values array
 */
static ep_stat_t w_select_dbrow_indeces(sqlite3 *dbconn, char *tbl_name, int rowid,
                                        char *idx_params[], int idx_params_num,
                                        int *idx_values)
{
    ep_stat_t status = EPS_OK;
    int i, res;
    sqlite3_stmt *stmt = NULL;
    char query[EP_SQL_REQUEST_BUF_SIZE];

    memset((char *)query, 0, sizeof(query));
    strcpy_safe(query, "SELECT  ", sizeof(query));

    for (i = 0; i < idx_params_num; i++)
    {
        if (i != 0)
            strcat_safe(query, ", ", sizeof(query));

        strcat_safe(query, "[", sizeof(query));
        strcat_safe(query, idx_params[i], sizeof(query));
        strcat_safe(query, "]", sizeof(query));
    }
    strcat_safe(query, " FROM ", sizeof(query));
    strcat_safe(query, tbl_name, sizeof(query));
    strcat_safe(query, " WHERE rowid = ", sizeof(query));
    sprintf(query+strlen(query), "%d ", rowid);

    DBG ("Query to select index values of row %d \n   %s", rowid, query);

    if ((res = sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL)) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Failed to prepare SELECT stmt to determine new insts num: err %d - %s",
                             res, sqlite3_errmsg(dbconn));
    res = sqlite3_step(stmt);
    if ((res != SQLITE_OK) && (res != SQLITE_DONE) && (res != SQLITE_ROW))
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR,"Failed to perform SELECT stmt to determine new insts numb: err %d - %s",
                            res, sqlite3_errmsg(dbconn));

    for (i = 0; i < idx_params_num; i++)
    {
        idx_values[i] = sqlite3_column_int(stmt, i);
    }
ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/* Helper function that parses addObject method string and prepares
   backend script command to add a new object.
   The prepared command is placed to cmd_buf.
   Parsing information of the method string is placed in p_parsed_script_str.
 */
static ep_stat_t w_prepare_addobj_command(
                   worker_data_t *wd, obj_info_t *obj_info, sqlite3 *dbconn,
                   char *idx_params[], int *idx_values, int idx_params_num,
                   nvpair_t *paramValues, int params_num,
                   parsed_param_name_t *pn, parsed_operation_t *p_parsed_script_str,
                   char *cmd_buf, int cmd_buf_len)
{
    ep_stat_t status = EPS_OK;
    int i;
    sqlite3_stmt *stmt = NULL;

    status = w_prepare_command(wd, pn, obj_info, dbconn, p_parsed_script_str,
                 idx_params, idx_values, idx_params_num, cmd_buf, cmd_buf_len, &stmt);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not prepare add command for object %s",
                            obj_info->objName);

    /*Add names and values of params if they are presented in the request
      (values are placed into quotes ' ')                               */
    if (params_num > 0)
    {
        for (i = 0; i < params_num; i++)
        {
            if ((strcat_safe(cmd_buf, " -pname ", cmd_buf_len) == NULL) ||
                (strcat_safe(cmd_buf, paramValues[i].name, cmd_buf_len) == NULL) ||
                (strcat_safe(cmd_buf, " -pvalue '", cmd_buf_len) == NULL) ||
                (strcat_safe(cmd_buf, paramValues[i].pValue, cmd_buf_len) == NULL) ||
                (strcat_safe(cmd_buf, "'", cmd_buf_len) == NULL))
            {
                status = EPS_GENERAL_ERROR;
                DBG("Not enough cmd_buf (size: %d) - command is incomplete: \n\t%s", cmd_buf_len, cmd_buf);
                GOTO_RET_WITH_ERROR(status, "Couldn't prepare parameters script command (%d)", status);
            }
        }
    }

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}


static ep_stat_t w_get_subst_obj_info(worker_data_t *wd, backend_subst_value_t *s_val, obj_info_t *s_obj_info)
{
    parsed_param_name_t s_pn = {{0}};
    strcpy_safe(s_pn.obj_name, s_val->mmx_subst_val.obj_name, sizeof(s_pn.obj_name));
    strcpy_safe(s_pn.leaf_name, s_val->mmx_subst_val.leaf_name, sizeof(s_pn.leaf_name));

    int s_obj_num;
    w_get_obj_info(wd, &s_pn, 0, 0, s_obj_info, 1, &s_obj_num);

    if (s_obj_num == 1)
        return EPS_OK;
    else
        return EPS_INVALID_FORMAT;
}


/* Helper function returning object info for the previous multi-instance
   object of the object specified in the p_pn parameter                 */
static ep_stat_t w_get_prev_obj_info(worker_data_t *wd, char *obj_name,
                                     obj_info_t *prevobj_info)
{
    ep_stat_t status = EPS_OK;
    int obj_num = 0;
    parsed_param_name_t prev_pn = {{0}};

    status = w_get_prev_multi_obj_name(obj_name, prev_pn.obj_name, NULL);
    if (status != EPS_OK)
        return status;

    w_get_obj_info(wd, &prev_pn, 0, 0, prevobj_info, 1, &obj_num);

    if (obj_num == 1)
        return EPS_OK;
    else
        return EPS_INVALID_FORMAT;
}

static ep_stat_t w_form_query_addobject_insert(worker_data_t *wd, ep_message_t *message,
                                               parsed_param_name_t *pn, obj_info_t *obj_info,
                                               param_info_t param_info[], int param_num,
                                               char *query, size_t query_size)
{
    ep_stat_t status  = EPS_OK;
    int i, j;
    char *selectFrom = NULL;
    int idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    obj_info_t prevobj_info;
    BOOL param_value_was_found = FALSE;

     /* Save index names */
    get_index_param_names(param_info, param_num, idx_params, &idx_params_num);

    if (idx_params_num > 1)
    {
        status = w_get_prev_obj_info(wd, pn->obj_name, &prevobj_info);
        if (status == EPS_OK)
            selectFrom = prevobj_info.objValuesTblName;
    }

    strcpy_safe(query, "INSERT INTO ", query_size);
    strcat_safe(query, obj_info->objValuesTblName, query_size);
    strcat_safe(query, " (", query_size);

    /* Collect name of index parameters */
    for (i = 0; i < idx_params_num; i++)
    {
        strcat_safe(query, idx_params[i], query_size);
        strcat_safe(query, ", ", query_size);
    }
    /* Collect names of other (not index) parameters */
    for (i = 0; i < param_num; ++i)
    {
        if (param_info[i].isIndex)
            continue;

        strcat_safe(query, "[", query_size);
        strcat_safe(query, param_info[i].paramName, query_size);
        strcat_safe(query, "], ", query_size);
    }
    /* remove last space and comma */
    LAST_CHAR(query) = '\0';
    LAST_CHAR(query) = '\0';

    strcat_safe(query, ") SELECT ", query_size);

    /* All indeces values (except of the last one) is selected from the
       previous object's value table - this table is "t0" */
    for (i = 0; i < idx_params_num - 1; i++)
    {
        strcat_safe(query, "t0.[", query_size);
        strcat_safe(query, idx_params[i], query_size);
        strcat_safe(query, "], ", query_size);
    }
    /* The last index is calculated in the table of the added object itself
       This table is "t1"                                             */
    strcat_safe(query, "IFNULL(MAX(t1.[", query_size);
    strcat_safe(query, idx_params[idx_params_num-1], query_size);
    strcat_safe(query, "]),0)+1 ", query_size);

    /* Values of parameters (not index) should be taken from request or their
     * defaults otherwise.
     * For example, request from CWMP will contain NO object parameters, so we
     * have to use default values for this frontend
     */
    strcat_safe(query, ", ", query_size);

    for (i = 0; i < param_num; ++i)
    {
        if (param_info[i].isIndex)
            continue;



        param_value_was_found = FALSE;
        for (j = 0; j < message->body.addObject.arraySize; ++j)
        {
            if (!strcmp(param_info[i].paramName, message->body.addObject.paramValues[j].name))
            {
                strcat_safe(query, "'", query_size);
                strcat_safe(query, soap2db(message->body.addObject.paramValues[j].pValue,
                                           param_info[i].paramType), query_size);
                strcat_safe(query, "',", query_size);
                param_value_was_found = TRUE;
                break;
            }
        }

        if (!param_value_was_found)
        {
            if(param_info[i].hasDefValue)
            {
                strcat_safe(query, "'", query_size);
                strcat_safe(query, soap2db(param_info[i].defValue, param_info[i].paramType), query_size);
                strcat_safe(query, "',", query_size);
            }
            else
            {
                strcat_safe(query, "NULL,", query_size);
            }

        }
    }
    LAST_CHAR(query) = '\0'; /* remove last comma */

    /* Select is performed from joined table. Left join on index values */
    strcat_safe(query, " FROM ", query_size);

    if (idx_params_num > 1)
    {
        strcat_safe(query, selectFrom, query_size);
        strcat_safe(query, " AS t0 LEFT JOIN ", query_size);

        strcat_safe(query, obj_info->objValuesTblName, query_size);
        strcat_safe(query, " AS t1 ON (", query_size);

        for (j = 0; j < idx_params_num-1; j++)
        {
            strcat_safe(query, "t0.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "]=t1.[", query_size);
            strcat_safe(query, idx_params[j], query_size);
            strcat_safe(query, "] AND ", query_size);
        }
        query[strlen(query)-5] = '\0'; // Remove the last " AND "
        strcat_safe(query, ")", query_size);
    }
    else
    {
        strcat_safe(query, obj_info->objValuesTblName, query_size);
        strcat_safe(query, " AS t1 ", query_size);
    }

    /* Where indeces of the prev obj table have the specified values */
    strcat_safe(query, " WHERE 1", query_size);

    for (i = 0; i < idx_params_num-1; i++)
    {
        strcat_safe(query, " AND t0.[", query_size);
        strcat_safe(query, idx_params[i], query_size);
        strcat_safe(query, "]=", query_size);
        sprintf(query+strlen(query), "%d", pn->indices[i].exact_val.num);
    }

    if (idx_params_num > 1)
    {
        strcat_safe(query, " GROUP BY ", query_size);
        for (i = 0; i < idx_params_num - 1; i++)
        {
            strcat_safe(query, "t0.[", query_size);
            strcat_safe(query, idx_params[i], query_size);
            strcat_safe(query, "],", query_size);
        }
        LAST_CHAR(query) = '\0'; // Remove the last comma
    }

    return EPS_OK;
}

/*
 * Builds SELECT SQL request that retrieves values for substitution
 */
static ep_stat_t w_form_subst_sql_addobj_backend(worker_data_t *wd, parsed_param_name_t *pn,
        obj_info_t *obj_info, char *query, size_t query_size, char **idx_params,
        int idx_params_num, parsed_backend_method_t *parsed_backend_str, int last_rowid)
{
    strcpy_safe(query, "SELECT ", query_size);
    for (int j = 0; j < idx_params_num; j ++)
    {
        strcat_safe(query, "t.[", query_size);
        strcat_safe(query, idx_params[j], query_size);
        strcat_safe(query, "],", query_size);
    }
    for (int j = 0; j < parsed_backend_str->subst_val_num; j++)
    {
        if (parsed_backend_str->subst_val[j].mmx_subst_val.obj_name)
            sprintf(query+strlen(query), "t%d.", j);
        else
            strcat_safe(query, "t.", query_size);

        strcat_safe(query, "[", query_size);
        strcat_safe(query, parsed_backend_str->subst_val[j].mmx_subst_val.leaf_name, query_size);
        strcat_safe(query, "],", query_size);
    }

    LAST_CHAR(query) = '\0'; /* remove last comma */

    strcat_safe(query, " FROM ", query_size);
    strcat_safe(query, obj_info->objValuesTblName, query_size);
    strcat_safe(query, " AS t ", query_size);

    for (int i = 0; i < parsed_backend_str->subst_val_num; i++)
    {
        if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name)
        {
            parsed_param_name_t s_pn;
            /* get the name of values table for this object */
            strcpy_safe(s_pn.obj_name, parsed_backend_str->subst_val[i].mmx_subst_val.obj_name, sizeof(s_pn.obj_name));
            strcpy_safe(s_pn.leaf_name, parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name, sizeof(s_pn.leaf_name));
            s_pn.index_num = pn->index_num;
            memcpy(s_pn.indices, pn->indices, sizeof(s_pn.indices));
            s_pn.partial_path = FALSE;

            obj_info_t s_obj_info;
            int s_obj_num, min_idx_num, n;

            w_get_obj_info(wd, &s_pn, 0, 0, &s_obj_info, 1, &s_obj_num);
            DBG("%d objects retrieved", s_obj_num);
            n = (s_obj_num > 0) ?  w_num_of_obj_indeces(s_obj_info.objName) : 0;
            min_idx_num = (n < idx_params_num) ? n : idx_params_num;


            if ((min_idx_num > 0) && (s_obj_num > 0)) {
                strcat_safe(query, "INNER JOIN ", query_size);
                strcat_safe(query, s_obj_info.objValuesTblName, query_size);
                sprintf(query+strlen(query), " AS t%d ON (", i);
                // TODO idx_params of subst table t0
                for (int j = 0; j < min_idx_num; j ++)
                {
                    strcat_safe(query, "t.[", query_size);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "]=", query_size);
                    sprintf(query+strlen(query), "t%d.[", i);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "] AND ", query_size);
                }
                query[strlen(query)-5] = '\0'; /* Remove last " AND " */
                strcat_safe(query, ") ", query_size);
            }
        }
    }
    strcat_safe(query, " WHERE t.rowid = ", query_size);
    sprintf(query+strlen(query), "%d", last_rowid);
    return EPS_OK;
}

static ep_stat_t w_delete_row_by_rowid(sqlite3 *conn, char *tbl_name, int rowId)
{
    ep_stat_t status = EPS_OK;
    char buf[EP_SQL_REQUEST_BUF_SIZE] = "DELETE FROM ";
    int modified_rows_num = 0;

    strcat_safe(buf, tbl_name, sizeof(buf));
    strcat_safe(buf, " WHERE rowid=", sizeof(buf));
    sprintf(buf+strlen(buf), "%d", rowId);

    DBG("Query for row delete: \n    %s", buf);

    if ((status = ep_db_exec_write_query(conn, buf, &modified_rows_num)) != EPS_OK)
        ERROR("Could not delete row from DB");

    return status;
}


static ep_stat_t w_update_db_on_addobj(sqlite3 *conn, char *tbl_name,
                                       namevaluepair_t *keyParamValues,
                                       int num_of_params, int rowId)
{
    ep_stat_t status = EPS_OK;
    int i;
    int modified_rows_num = 0;
    char buf[EP_SQL_REQUEST_BUF_SIZE] = "UPDATE ";

    strcat_safe(buf, tbl_name, sizeof(buf));
    strcat_safe(buf, " SET ", sizeof(buf));

    for (i = 0; i < num_of_params; i++)
    {
        if (i != 0)
            strcat_safe(buf, ", ", sizeof(buf));

        strcat_safe(buf, "[", sizeof(buf));
        strcat_safe(buf, keyParamValues[i].name, sizeof(buf));
        strcat_safe(buf, "] = '", sizeof(buf));
        strcat_safe(buf, keyParamValues[i].value, sizeof(buf));
        strcat_safe(buf, "'", sizeof(buf));
    }

    strcat_safe(buf, " WHERE rowid=", sizeof(buf));
    sprintf(buf+strlen(buf), "%d", rowId);

    DBG("Query for row update on add object: \n    %s", buf);
    if ((status = ep_db_exec_write_query(conn, buf, &modified_rows_num)) != EPS_OK)
        ERROR("Could not update row in DB");

    return status;
}

/*
 * Function prepare all parameters of new object
 * If parameter present in request - function will take it,
 * otherwise - will take default value if param has it
 *
 * Arguments:
 *    obj_param_info - data about all object parameters
 *    req_params - data about object parameters present in request
 */
static void w_prepare_addobj_non_idx_params(const uint32_t req_params_num,
                                            const nvpair_t req_params[],
                                            const uint32_t obj_param_num,
                                            param_info_t obj_param_info[],
                                            uint32_t *addobj_non_idx_param_num,   /* OUT */
                                            nvpair_t addobj_non_idx_param[])      /* OUT */
{
    uint32_t i, j, k = 0;
    BOOL param_val_in_req = FALSE;

    for (i = 0; i < obj_param_num; ++i)
    {
        if (obj_param_info[i].isIndex)
            continue;

        param_val_in_req = FALSE;

        for (j = 0; j < req_params_num; ++j)
        {
            if (!strcmp(obj_param_info[i].paramName, req_params[j].name))
            {
                param_val_in_req = TRUE;

                addobj_non_idx_param[k].pValue = req_params[j].pValue;
                break;
            }
        }

        if (!param_val_in_req && obj_param_info[i].hasDefValue == FALSE)
            continue;

        strncpy(addobj_non_idx_param[k].name, obj_param_info[i].paramName, sizeof(addobj_non_idx_param[k].name));

        if (!param_val_in_req && obj_param_info[i].hasDefValue == TRUE)
            addobj_non_idx_param[k].pValue = obj_param_info[i].defValue;

        ++k;
    }

    *addobj_non_idx_param_num = k;
}

static ep_stat_t w_addobject_backend(worker_data_t *wd, ep_message_t *message,
                                     parsed_param_name_t *pn, obj_info_t *obj_info,
                                     sqlite3 *dbconn,param_info_t param_info[], int param_num,
                                     int *addStatus, int *newInstance )
{
    ep_stat_t status = EPS_OK;
    int   res, be_port, rowId, i, j;
    int   modified_rows_num = 0;
    int   idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int   idx_values[MAX_INDECES_PER_OBJECT];
    BOOL  row_inserted = FALSE;
    mmxba_request_t be_ans;
    namevaluepair_t keyPnv[MAX_INDECES_PER_OBJECT + 2];
    parsed_backend_method_t parsed_method;
    char *token, *strtok_ctx;

    uint32_t non_idx_param_num = 0;
    nvpair_t non_idx_param[MAX_PARAMS_PER_OBJECT];

    sqlite3_stmt *stmt = NULL;
    char query[EP_SQL_REQUEST_BUF_SIZE], selfRef[NVP_MAX_NAME_LEN], ownerStr[3] = {0};

    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    w_parse_backend_method_string(OP_ADDOBJ, obj_info->addObjMethod, &parsed_method);

    DBG("Adding object name %s, resp param array size = %d",
         message->body.addObject.objName,message->body.addObject.arraySize);

    /* Insert a new row for the added object into the values db */
    w_form_query_addobject_insert(wd, message, pn, obj_info,param_info,param_num,
                                  query, sizeof(query));
    DBG("query for addobj entry:\n%s", query);
    if ((status = ep_db_exec_write_query(dbconn, query, &modified_rows_num)) != EPS_OK )
        GOTO_RET_WITH_ERROR(status, "Failed to insert new object to DB");
    if (modified_rows_num <= 0)  /* TODO: actually we add only one row and rows num must be = 1 */
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Failed to insert new object to DB "
            "(no rows added: modified_rows_num = %d)", modified_rows_num);

    row_inserted = TRUE;
    rowId = sqlite3_last_insert_rowid(dbconn);
    //DBG ("New object row is inserted to values DB, rowid = %d", rowId);

    /*Determine all index values, including the last one (i.e. instance index)*/
    status = w_select_dbrow_indeces(dbconn, obj_info->objValuesTblName, rowId,
                                   idx_params, idx_params_num, idx_values);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not determine indeces of the added object %s (err %d)",
                            obj_info->objName, status);

    *newInstance =  idx_values[idx_params_num-1];
    DBG("New obj is added to db tbl %s, rowId = %d, new inst %d",
         obj_info->objValuesTblName, rowId, *newInstance);

    /* Now form self-reference object instance value of the added object */
    memset((char *)selfRef, 0, sizeof(selfRef));
    w_place_indeces_to_objname(obj_info->objName, idx_values, idx_params_num, selfRef);
    if (strlen(selfRef) > 0)         /*Check the len just in case */
        DBG("Self ref to the new added obj: %s", selfRef);

    /* Get port number of the backend */
    if ((w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0) != EPS_OK) ||
        (be_port <= 0) )
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s",
                            be_port, obj_info->backEndName);

    DBG("Backend '%s': port %d", obj_info->backEndName, be_port);

    /* Form query to select value of substituted parameters
     */
    w_form_subst_sql_addobj_backend(wd, pn, obj_info, query, sizeof(query),
                    idx_params, idx_params_num, &parsed_method, rowId);
    DBG("query to get subst params:\n%s", query);

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                            sqlite3_errmsg(dbconn));

    res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
    {
        /*
         * w_form_subst_sql_addobj_backen construct request for get indexes values only
         * The rest parameters should be taken from request or defaults
         */
        w_prepare_addobj_non_idx_params(message->body.addObject.arraySize, message->body.addObject.paramValues,
                                        param_num, param_info,
                                        &non_idx_param_num, non_idx_param);

        if ((status = form_and_send_be_request (wd, be_port, MMXBA_OP_TYPE_ADDOBJ,
                          &parsed_method, stmt, idx_params_num, 0, NULL,
                          non_idx_param_num, non_idx_param,
                          &be_ans) != EPS_OK))
            GOTO_RET_WITH_ERROR(status, "be request failure (%d)", status);

        if (be_ans.opResCode != 0)
            GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,"Backend returned error: %d: %d: %s",
                be_ans.opResCode, be_ans.opExtErrCode,
                strlen(be_ans.errMsg) ? be_ans.errMsg : " ");

        if (be_ans.addObj_resp.objNum < 1)
            GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,"No objects was added (%d)",
                                be_ans.addObj_resp.objNum);

        *addStatus = be_ans.postOpStatus;

        /* Now update our instance DB row with the be keys and "meta" params*/
        j = 0;
        token = strtok_r(be_ans.addObj_resp.objects[0], ",", &strtok_ctx);
        trim(token);
        for (i = 0; i< parsed_method.bekey_param_num && token; i++)
        {
            if (strlen(token)>0)
            {
                if (isLeafName(parsed_method.bekey_params[i]))
                {
                    strcpy_safe(keyPnv[j].name, parsed_method.bekey_params[i],
                                                            NVP_MAX_NAME_LEN);
                    strcpy_safe(keyPnv[j].value, token, NVP_MAX_VALUE_LEN);
                    j++;
                }
                else
                    DBG("key param name %s is not leaf name", parsed_method.bekey_params[i]);
            }
            token = strtok_r(NULL, ",", &strtok_ctx);
        }
        /* Add self-reference to the list of updated parameters */
        strcpy_safe(keyPnv[j].name, MMX_SELFREF_DBCOLNAME, sizeof(keyPnv[j].name));
        strcpy_safe(keyPnv[j].value,(char *)selfRef,sizeof(keyPnv[j].value));
        j++;

        /* Add createOwner ("user") to the list of updated parameters */
        sprintf((char *)ownerStr, "%d", EP_DATA_OWNER_USER);
        strcpy_safe(keyPnv[j].name, MMX_CREATEOWNER_DBCOLNAME, sizeof(keyPnv[j].name));
        strcpy_safe(keyPnv[j].value, (char *)ownerStr, sizeof(keyPnv[j].value));
        j++;

        /* Now update our instance DB row with the be keys and self reference */
        w_update_db_on_addobj(dbconn, obj_info->objValuesTblName,
                              (namevaluepair_t *)&keyPnv, j, rowId);
    }
    else if (res == SQLITE_DONE)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not find newly inserted row");
    else
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s",
                                            sqlite3_errmsg(dbconn));
ret:
    if (status != EPS_OK && row_inserted)
    {
        w_delete_row_by_rowid(dbconn, obj_info->objValuesTblName,
                              sqlite3_last_insert_rowid(dbconn));
    }
    if (stmt) sqlite3_finalize(stmt);

    return status;
}

static ep_stat_t w_addobject_script(worker_data_t *wd, ep_message_t *message,
                                    parsed_param_name_t *pn, obj_info_t *obj_info,
                                    sqlite3 *dbconn, param_info_t param_info[], int param_num,
                                    int *addStatus, int *newInstance)
{
    ep_stat_t status = EPS_OK;
    BOOL row_inserted = FALSE;
    int  modified_rows_num = 0;
    int i, j, rowId, res_code = 1, parsed_status = 0;
    int idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_values[MAX_INDECES_PER_OBJECT];
    parsed_operation_t parsed_script_str;
    namevaluepair_t keyParamValues[MAX_INDECES_PER_OBJECT + 2];
    char query[EP_SQL_REQUEST_BUF_SIZE], buf[EP_SQL_REQUEST_BUF_SIZE], selfRef[NVP_MAX_NAME_LEN], ownerStr[3] = {0};
    char *p_extr_results = NULL;
    char *strtok_ctx, *strtok_ctx1, *token, *token1;

    memset((char*)keyParamValues, 0, sizeof(keyParamValues));
    memset((char *)buf, 0, sizeof(buf));

    /* Save index names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /* Prepare query to insert the new values into db */
    w_form_query_addobject_insert(wd, message, pn, obj_info, param_info, param_num, query, sizeof(query));
    if (strlen(query) == 0)
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could form query to insert new object instance");

    /* Perform prepared query and determine instance index of the new row */
    DBG("AddObject insert query:\n%s", query);
    if ((status = ep_db_exec_write_query(dbconn, query, &modified_rows_num)) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Failed to insert new object to DB");
    if (modified_rows_num <= 0) /* TODO: actually we add only one row and rows num must be = 1 */
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Failed to insert new object to DB "
            "(no rows added: modified_rows_num = %d)", modified_rows_num);

    row_inserted = TRUE;
    rowId = sqlite3_last_insert_rowid(dbconn);
    DBG ("New object row is inserted to values DB, rowid = %d", rowId);

    /*Determine all index values, including the last one (i.e. instance index)*/
    status = w_select_dbrow_indeces(dbconn, obj_info->objValuesTblName, rowId,
                                   idx_params, idx_params_num, idx_values);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not determine indeces of added obj %s, rowid %d (err %d)",
                            obj_info->objName, rowId, status);

    *newInstance =  idx_values[idx_params_num-1];
    /*DBG("New object is added to db table %s, rowId = %d, new inst %d",
         obj_info->objValuesTblName, rowId, *newInstance);*/

    /* Now form self-reference object instance value of the added object */
    memset((char *)selfRef, 0, sizeof(selfRef));
    w_place_indeces_to_objname(obj_info->objName, idx_values, idx_params_num, selfRef);
    if (strlen(selfRef) > 0 )  /*Check the len just in case */
        DBG("Self ref of the new added object: %s", selfRef);

    if (strlen(obj_info->addObjMethod) > 0)
    {
        status = w_parse_operation_string(OP_ADDOBJ, obj_info->addObjMethod, &parsed_script_str);
        if (status != EPS_OK)
            GOTO_RET_WITH_ERROR(status, "Could not parse add command (%d)", status);
    }
    else
       GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "AddObj method string is empty for object %s !!!", obj_info->objName);

    /* Form script command for adding object in the backend */
    status = w_prepare_addobj_command(wd, obj_info, dbconn,
                                     idx_params, idx_values,idx_params_num,
                                     message->body.addObject.paramValues,
                                     message->body.addObject.arraySize,
                                     pn, &parsed_script_str, (char*)&buf,sizeof(buf));
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not prepare add object command for %s (err %d)",
                            obj_info->objName, status);
    DBG("Full prepared command: \n\t%s", buf);

    /* Perform prepared command and parse results. Results look as follows:
       in case of failure:  "resCode;", where resCode is not 0
       in case of success:  "0; addStatus;key1, key2, ...", where
       addStatus is 0 or 1, the key1/2/... are values of backend key params*/
    p_extr_results = w_perform_prepared_command(buf, sizeof(buf), TRUE, NULL);
    if (!p_extr_results)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not read script results");

    DBG("Result of the command: %s ", buf);
    status = w_parse_script_res(buf, sizeof(buf), &res_code, &parsed_status, &p_extr_results);

    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR, "Bad format of script result %d", status);

    if (res_code != 0)
        GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR, "Addobj script returned error %d", res_code);

    //DBG ("Parsed script results: resCode %d, setStatus %d",res_code, parsed_status);
    *addStatus = parsed_status;

    /* Parse BE keys value (format: key11, key12, ... ;) and save them in DB */
    //DBG("Received BE keys: %s ", p_extr_result);
    token = strtok_r(p_extr_results, ";", &strtok_ctx);
    if (token && (strlen(token) > 0))
    {
        DBG("BE key token %s", token);
        token1 = strtok_r(token, ",", &strtok_ctx1); trim(token1);
        j = 0;
        for (i = 0; i< parsed_script_str.bekey_param_num && token1; i++)
        {
            if (strlen(token1)>0)
            {
                if (isLeafName(parsed_script_str.bekey_params[i]))
                {
                    strcpy_safe(keyParamValues[j].name, parsed_script_str.bekey_params[i],
                                sizeof(keyParamValues[j].name));
                    strcpy_safe(keyParamValues[j].value, token1,
                                sizeof(keyParamValues[j].value));
                    j++;
                }
            }
            token1 = strtok_r(NULL, ",", &strtok_ctx1); trim(token1);
        }
        /* Add self-reference to the list of updated parameters */
        strcpy_safe(keyParamValues[j].name, MMX_SELFREF_DBCOLNAME,
                                               sizeof(keyParamValues[j].name));
        strcpy_safe(keyParamValues[j].value,(char *)selfRef,sizeof(keyParamValues[j].value));
        j++;

         /* Add createOwner ("user") to the list of updated parameters */
        sprintf((char *)ownerStr, "%d", EP_DATA_OWNER_USER);
        strcpy_safe(keyParamValues[j].name, MMX_CREATEOWNER_DBCOLNAME,
                                            sizeof(keyParamValues[j].name));
        strcpy_safe(keyParamValues[j].value, (char *)ownerStr,
                                             sizeof(keyParamValues[j].value));
        j++;

        /* Now update our instance DB row with the be keys and self reference */
        w_update_db_on_addobj(dbconn, obj_info->objValuesTblName,
                              (namevaluepair_t *)&keyParamValues, j, rowId);
    }
    else //Script didn't return key parameter values. Check if this is OK
    {
        if (parsed_script_str.bekey_param_num > 0 )
            GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,
                   "AddObj script didn't return expected values of %d BE key params",
                    parsed_script_str.bekey_param_num);
    }

ret:
    if (((res_code != 0) || (status != EPS_OK)) && row_inserted)
    {
        ERROR("Delete just now added row %d",rowId);
        w_delete_row_by_rowid(dbconn, obj_info->objValuesTblName, rowId);
    }

    return status;
}

static ep_stat_t w_addobject_db(worker_data_t *wd, ep_message_t *message,
                                parsed_param_name_t *pn, obj_info_t *obj_info,
                                sqlite3 *dbconn, param_info_t param_info[], int param_num,
                                int *addStatus, int *newInstance)
{
    ep_stat_t status = EPS_OK;
    BOOL row_inserted = FALSE;
    int   rowId;
    int   modified_rows_num = 0;
    int   idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int   idx_values[MAX_INDECES_PER_OBJECT];
    char  query[EP_SQL_REQUEST_BUF_SIZE], selfRef[NVP_MAX_NAME_LEN], ownerStr[3] = {0};
    namevaluepair_t nvp[2];

    memset((char*)nvp, 0, sizeof(nvp));

    /* Save index names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /* Prepare query to insert the new values into db */
    w_form_query_addobject_insert(wd, message, pn, obj_info,
                                  param_info, param_num, query, sizeof(query));
    if (strlen(query) == 0)
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could form query to insert new object instance");

    DBG("AddObject insert query:\n     %s", query);

    /* Perform prepared query */
    if ((status = ep_db_exec_write_query(dbconn, query, &modified_rows_num)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not execute AddObject insert query: %d", status);
    if (modified_rows_num <= 0) /* TODO: actually we add only one row and rows num must be = 1 */
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Failed to insert new object to DB "
            "(no rows added: modified_rows_num = %d)", modified_rows_num);

    row_inserted = TRUE;
    rowId = sqlite3_last_insert_rowid(dbconn);

    /* Determine all index values, including the last one (i.e. instance index) */
    status = w_select_dbrow_indeces(dbconn, obj_info->objValuesTblName, rowId,
                                   idx_params, idx_params_num, idx_values);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not determine indeces of added obj %s, rowid %d (err %d)",
                            obj_info->objName, rowId, status);

    *newInstance =  idx_values[idx_params_num-1];

    /* Form self-reference object instance value of the added object */
    memset((char *)selfRef, 0, sizeof(selfRef));
    w_place_indeces_to_objname(obj_info->objName, idx_values, idx_params_num, selfRef);
    if (strlen(selfRef) > 0 )  /*Check the len just in case */
        DBG("Self ref of the new added object: %s", selfRef);

    /* Add self-reference to the list of updated parameters */
    strcpy_safe(nvp[0].name, MMX_SELFREF_DBCOLNAME, sizeof(nvp[0].name));
    strcpy_safe(nvp[0].value,(char *)selfRef, sizeof(nvp[0].value));

    /* Add createOwner ("user") to the list of updated parameters */
    sprintf((char *)ownerStr, "%d", EP_DATA_OWNER_USER);
    strcpy_safe(nvp[1].name, MMX_CREATEOWNER_DBCOLNAME, sizeof(nvp[1].name));
    strcpy_safe(nvp[1].value, (char *)ownerStr, sizeof(nvp[1].value));

    /* Update the new instance row with self ref and create owner values*/
    w_update_db_on_addobj(dbconn, obj_info->objValuesTblName,
                                      (namevaluepair_t *)&nvp, 2, rowId);

    DBG(" New instance was successfully added to DB" );

ret:
    if (status == EPS_OK)
    {
        *addStatus = 0;  //Restart of backend is not needed
    }
    else if (row_inserted)
    {
        w_delete_row_by_rowid(dbconn, obj_info->objValuesTblName, rowId);
    }

    return status;
}



typedef struct obj_param_info_s {
    int param_num;
    param_info_t param[MAX_PARAMS_PER_OBJECT];
} obj_param_info_t;

#define MAX_TOTAL_OBJ_DEPDEPTH (1 /* Object itself */ + MAX_DEPDEPTH_PER_OBJECT)

/* Structure that defines the Object's N-level autoCreate dependency.
 * Used in the below recursion function for automatic creation of
 *  dependent Object instances.
 *
 * Important note:
 *  the structure defines the single N-level deep dependency for the chain of
 *  Objects
 *
 *   (Object0)
 *      -> (some DependentObject at Level 1)
 *            -> (some DependentObject at Level 2)
 *                  -> ...
 *                        -> (some DependentObject at Level N)
 *
 *    where N == MAX_DEPDEPTH_PER_OBJECT
 */
typedef struct addobj_autocreate_objects_s
{
    /* ObjInfo of the added Object and its dependent Object(s) chain */
    obj_info_t        obj_info[MAX_TOTAL_OBJ_DEPDEPTH];
    /* ObjParamInfo of the added Object and its dependent Object(s) chain */
    obj_param_info_t  obj_param_info[MAX_TOTAL_OBJ_DEPDEPTH];
    /* Index values for the added Object (dependent Objects) instance */
    exact_indexvalues_t   obj_indexvalues[MAX_TOTAL_OBJ_DEPDEPTH];
} addobj_autocreate_objects_t;

static addobj_autocreate_objects_t auto_add_objects;


/* [Recursion function] w_addobj_autocreate_recursively
 * Post-process successful AddObject request - function searches 'autoCreate'
 *  dependencies for the created Object and automatically creates dependent
 *  Object instances starting from the most nearest subsidiary dependent Object
 *  to the most far - deepest in the Object dependency chain. In case there is
 *  an AddObject failure at some level the function does not continue the
 *  creation in lower levels.
 *
 * Important note - instance of the Object from management request (level = 0)
 *  is created out of this function - this function starts once that instance
 *  is successfully created in the parent caller.
 */
ep_stat_t w_addobj_autocreate_recursively(worker_data_t *wd,
                                          ep_message_t *message,
                                          int level,
                                          int *total_addStatus,
                                          int total_restart_be[])
{
    ep_stat_t status = EPS_OK;

    static int failed_level;

    int i = 0, j = 0;
    int rowCount = 0;
    sqlite3 *conn = wd->main_conn;

    int addStatus = 0, newInstance = 0;
    int addStyle = 0;

    char *token, *strtok_ctx;
    char pValue[NVP_MAX_VALUE_LEN];
    char aux_objName[MSG_MAX_STR_LEN] = {0};
    char aux_mem_buff[NVP_MAX_VALUE_LEN + 1];
    int  aux_mem_buff_size = NVP_MAX_VALUE_LEN + 1;
    ep_message_t new_message;

    /* Auxiliary variables */
    int obj_num = 0;
    BOOL param_match = FALSE;
    BOOL index_match = TRUE;
    obj_dependency_info_t *db_objdepInfo = NULL;
    int db_obj_depNum = 0;

    /* Current Object */
    obj_info_t             *curr_obj_info = NULL;
    obj_param_info_t       *curr_obj_paraminfo = NULL;
    exact_indexvalues_t    *curr_obj_indexvalues = NULL;
    char                   *curr_obj_idx_params[MAX_INDECES_PER_OBJECT];
    int                     curr_obj_idx_params_num = 0;
    obj_dependency_info_t   curr_obj_depInfo[MAX_DEPCOUNT_PER_OBJECT] = {0};
    int curr_obj_depNum = 0;

    /* Dependent Object */
    obj_info_t             *next_obj_info = NULL;
    obj_param_info_t       *next_obj_paraminfo = NULL;
    exact_indexvalues_t    *next_obj_indexvalues = NULL;
    char                   *next_obj_idx_params[MAX_INDECES_PER_OBJECT];
    int                     next_obj_idx_params_num = 0;
    parsed_param_name_t     next_obj_pn;

    /* Current/Dependent Objects assignments */
    curr_obj_info = &(auto_add_objects.obj_info[level]);
    next_obj_info = &(auto_add_objects.obj_info[level+1]);
    curr_obj_paraminfo = &(auto_add_objects.obj_param_info[level]);
    next_obj_paraminfo = &(auto_add_objects.obj_param_info[level+1]);
    curr_obj_indexvalues = &(auto_add_objects.obj_indexvalues[level]);
    next_obj_indexvalues = &(auto_add_objects.obj_indexvalues[level+1]);

    /* Initialize failed_level variable - once at recursion start */
    if (level == 0)
    {
        failed_level = -1;
    }

    RECURSLEVEL_DBG("=====  Run autoAddObject (L%d) for '%s', newInstance = %d  =====",
        level, curr_obj_info->objName,
        curr_obj_indexvalues->indexvalues[curr_obj_indexvalues->index_num - 1]);

    /* Fetch dependencies between Objects present in DB */
    if ((db_obj_depNum = ep_common_get_objdep_info(&db_objdepInfo)) < 1)
    {
        if (db_obj_depNum < 0)
            RECURSLEVEL_WARN("Warning: failed to read DB dependencies between Objects");
        else /* db_obj_depNum = 0 */
            RECURSLEVEL_DBG("There are no DB dependencies between Objects");

        /* No error */
        status = EPS_NOTHING_DONE;
        goto ret;
    }

    /* Collect 'autoCreate' dependencies for current Object */
    for (i = 0; i < db_obj_depNum; i++, db_objdepInfo++)
    {
        if (strcmp(db_objdepInfo->parentObjName, curr_obj_info->objName) ||
            db_objdepInfo->objDepClass != OBJ_DEP_AUTO_CREATE)
        {
            /* Object name OR dependency type mismatch */
            continue;
        }

        /* Check for the Object dependencies limit */
        if (curr_obj_depNum >= MAX_DEPCOUNT_PER_OBJECT)
        {
            RECURSLEVEL_WARN("Warning: Object '%s' has more than limit (%d) autoCreate dependencies",
                             curr_obj_info->objName, MAX_DEPCOUNT_PER_OBJECT);
            RECURSLEVEL_WARN("(Continue with first %d dependencies)", MAX_DEPCOUNT_PER_OBJECT);
            break;
        }

        memcpy(&curr_obj_depInfo[curr_obj_depNum], db_objdepInfo, sizeof(obj_dependency_info_t));
        curr_obj_depNum++;
    }

    /* Check that 'autoCreate' DB dependencies exist for current Object */
    if (curr_obj_depNum == 0)
    {
        RECURSLEVEL_DBG("Object '%s' has no autoCreate dependencies", curr_obj_info->objName);

        /* No error */
        status = EPS_NOTHING_DONE;
        goto ret;
    }

    /* Check for the Object dependencies depth limit */
    if (level >= MAX_DEPDEPTH_PER_OBJECT)
    {
        RECURSLEVEL_DBG("Object '%s' is requested at max autoCreate dependency depth (>=%d)",
                        curr_obj_info->objName, MAX_DEPDEPTH_PER_OBJECT);
        RECURSLEVEL_DBG("(its (%d) dependent object instances will not be created)",
                        curr_obj_depNum);

        /* No error */
        status = EPS_NOTHING_DONE;
        goto ret;
    }


    RECURSLEVEL_DBG("Object '%s' has %d autoCreate dependency(ies) to run for newInstance = %d",
                    curr_obj_info->objName, curr_obj_depNum,
                    curr_obj_indexvalues->indexvalues[curr_obj_indexvalues->index_num - 1]);

    /*
     * Go over current Object dependencies and per each dependency - go with
     *  current Object instance (newInstance) up to next recursion level to
     *  create newInstance for dependent Object(s)
     */
    for (i = 0; i < curr_obj_depNum; i++)
    {
        RECURSLEVEL_DBG("==> Dependency [L%d, %d of %d] %s: %s%s ---> %s%s",
            level, i + 1, curr_obj_depNum,
            objdepclass2string(curr_obj_depInfo[i].objDepClass),
            curr_obj_depInfo[i].parentObjName, curr_obj_depInfo[i].parentParamName,
            curr_obj_depInfo[i].childObjName, curr_obj_depInfo[i].childParamName);

        /*
         * First validate the dependency:
         *  - check Current Object has dependency parameter in ObjParamInfo
         *  - check Dependent Object
         *     - parse the Object name
         *     - fetch its ObjInfo/ObjParamInfo
         *     - does it have dependency parameter in ObjParamInfo
         *     - verify and assign index params
         */

        /* Check Current Object has dependency parameter in ObjParamInfo */
        param_match = FALSE;
        for (j = 0; j < curr_obj_paraminfo->param_num; j++)
        {
            if (!strcmp(curr_obj_depInfo[i].parentParamName,
                        curr_obj_paraminfo->param[j].paramName))
            {
                param_match = TRUE;
                break;
            }
        }
        if (!param_match)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(current Object '%s' mismatches dependency parameter '%s')",
                curr_obj_info->objName, curr_obj_depInfo[i].parentParamName);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* Check Dependent Object - parse the Object name */
        memset(&next_obj_pn, 0, sizeof(parsed_param_name_t));
        status = parse_param_name(curr_obj_depInfo[i].childObjName, &next_obj_pn);
        if (status != EPS_OK)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(failed to parse dependent Object name '%s', status = %d)",
                curr_obj_depInfo[i].childObjName, status);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* Check Dependent Object - fetch its ObjInfo */
        memset(next_obj_info, 0, sizeof(obj_info_t));
        obj_num = 0;
        status = w_get_obj_info(wd, &next_obj_pn, 1, 0, next_obj_info, 1, &obj_num);
        if (status != EPS_OK)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(failed to get ObjInfo for dependent Object '%s', status = %d)",
                curr_obj_depInfo[i].childObjName, status);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* TODO objnum - must be equal to 1 (check it or not ?) */

        /* Check Dependent Object - if it's writable */
        if (!next_obj_info->writable)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(dependent Object '%s' is not writable)",
                curr_obj_depInfo[i].childObjName);

            /* No error - continue (i) loop with other dependencies */
            /* TODO Or it is an error ..? */
            continue;
        }

        /* Check Dependent Object - fetch its ObjParamInfo */
        memset(next_obj_paraminfo, 0, sizeof(obj_param_info_t));
        status = w_get_param_info(wd, &next_obj_pn, next_obj_info, 0,
                        &(next_obj_paraminfo->param[0]),
                        &(next_obj_paraminfo->param_num), NULL);
        if (status != EPS_OK)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(failed to get ObjParamInfo for dependent Object '%s', status = %d)",
                curr_obj_depInfo[i].childObjName, status);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* Check Dependent Object has dependency parameter in ObjParamInfo */
        param_match = FALSE;
        for (j = 0; j < next_obj_paraminfo->param_num; j++)
        {
            if (!strcmp(curr_obj_depInfo[i].childParamName,
                        next_obj_paraminfo->param[j].paramName))
            {
                param_match = TRUE;
                break;
            }
        }
        if (!param_match)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(dependent Object '%s' mismatches dependency parameter '%s')",
                curr_obj_depInfo[i].childObjName, curr_obj_depInfo[i].childParamName);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* Save current Object index names */
        memset(curr_obj_idx_params, 0, sizeof(unsigned long int) * sizeof(MAX_INDECES_PER_OBJECT));
        curr_obj_idx_params_num = 0;
        get_index_param_names(&(curr_obj_paraminfo->param[0]),
            curr_obj_paraminfo->param_num, curr_obj_idx_params, &curr_obj_idx_params_num);
        /* Save dependent Object index names */
        memset(next_obj_idx_params, 0, sizeof(unsigned long int) * sizeof(MAX_INDECES_PER_OBJECT));
        next_obj_idx_params_num = 0;
        get_index_param_names(&(next_obj_paraminfo->param[0]),
            next_obj_paraminfo->param_num, next_obj_idx_params, &next_obj_idx_params_num);

        /* RECURSLEVEL_DBG("Current / Dependent Objects index num: %d and %d",
            curr_obj_indexvalues->index_num, next_obj_pn.index_num); */

        /* Verify and assign index parameters of Dependent Object */
        if (next_obj_pn.index_num <= 0)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(dependent Object '%s' provides invalid (%d) index number)",
                curr_obj_depInfo[i].childObjName, next_obj_pn.index_num);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }
        else if (next_obj_pn.index_num > 1)
        {
            /* Dependent Object has N (>1) indexes - to create new instance we
             * need to know first (N-1) indexes - these indexes must be taken
             * from the Current Object Instance - example:
             *
             *  Current Object:                   Device.A.{i}.AB.{i}.
             *  Dependent Object:                 Device.A.{i}.AB.{i}.ABC.{i}.
             *
             *  Current Object newInstance:       Device.A.2.AB.3.
             *  Dependent Object newInstance
             *   to be created (last index is     ==> requested in AddObject:
             *   added once AddObject request         Device.A.2.AB.3.ABC.
             *   successfully finished):          ==> when completed request
             *                                        Device.A.2.AB.3.ABC.5
             */

            /*
            RECURSLEVEL_DBG("Dependent object index number: %d > 1", next_obj_pn.index_num);
            RECURSLEVEL_DBG("(%d index(es) must match the Current Object ones)", next_obj_pn.index_num - 1);
            */

            /* Check index numbers: dependent index_num = (current index_num + 1)  */
            if (next_obj_pn.index_num != (curr_obj_indexvalues->index_num + 1))
            {
                RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                    level, i + 1, curr_obj_depNum);
                RECURSLEVEL_WARN("(dependent Object '%s' provides invalid (%d) index_num due to current Object (%d) index_num)",
                    curr_obj_depInfo[i].childObjName, next_obj_pn.index_num, curr_obj_indexvalues->index_num);

                /* No error - continue (i) loop with other dependencies */
                continue;
            }

            /* Check index parameters: N-1 dependent index parameters must be
             *  the same as the Current Object index parameters */
            index_match = TRUE;
            for (j = 0; j < curr_obj_idx_params_num; j++)
            {
                if (strcmp(curr_obj_idx_params[j], next_obj_idx_params[j]))
                {
                    /* Current/dependent Object index parameters do not match */
                    index_match = FALSE;
                    break;
                }
                else
                {
                    /* Save first N-1 index param values for dependent Object */
                    next_obj_pn.indices[j].type = REQ_IDX_TYPE_EXACT;
                    next_obj_pn.indices[j].exact_val.num = curr_obj_indexvalues->indexvalues[j];
                }
            }
            if (!index_match)
            {
                RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                    level, i + 1, curr_obj_depNum);
                RECURSLEVEL_WARN("(dependent Object '%s' index %d of %d - %s does not match %d index of current Object - %s)",
                    curr_obj_depInfo[i].childObjName, j+1, next_obj_pn.index_num, next_obj_idx_params[j],
                    j+1, curr_obj_idx_params[j]);

                /* No error - continue (i) loop with other dependencies */
                continue;
            }
        }
        else /* next_obj_pn.index_num = 1 */
        {
            /*RECURSLEVEL_DBG("Dependent object has single index - set by AddObject");*/
        }

        /* Validation of dependency is complete, index param values for dependent
         *  Object are ready (when taken from Current Object) */
        RECURSLEVEL_DBG("==> Dependency validated OK - preparing AddObject request for Dependent Object '%s'",
                        curr_obj_depInfo[i].childObjName);

        status = EPS_OK;
        addStatus = 0;
        newInstance = 0;

        /*
         * Dependency (i) is valid - now prepare AddObject request to create
         *  newInstance for the dependent Object
         */

        /* Check Object instance number limit */
        if ((status = ep_db_get_tbl_row_count(conn, next_obj_info->objValuesTblName,
                                              &rowCount)) != EPS_OK)
        {
            RECURSLEVEL_ERROR("==> Failed to get row count in dependent Object DB table %s (%d)",
                next_obj_info->objValuesTblName, status);

            /* Error - recursion will be stopped */
            goto ret;
        }

        if (rowCount >= MAX_INSTANCES_PER_OBJECT)
        {
            status = EPS_NO_MORE_ROOM;
            RECURSLEVEL_ERROR("==> Dependent Object %s already has max num of instances (%d)",
                               next_obj_info->objName, rowCount);

            /* Error - recursion will be stopped */
            goto ret;
        }

        /* Prepare AddObject request (ep_message) for dependent Object */
        memset(&new_message, 0, sizeof(new_message));
        memcpy(&new_message.header, &message->header, sizeof(new_message.header));
        /* Fill AddObject ep_message's field (body->objName) */
        strcpy_safe(aux_objName, next_obj_info->objName, MSG_MAX_STR_LEN);
        aux_objName[strlen(aux_objName)-4] = '\0'; // remove last '{i}.'
        if (next_obj_pn.index_num == 1)
        {
            /* Example: objName = "Device.A." */
            strcpy_safe(new_message.body.addObject.objName, aux_objName, MSG_MAX_STR_LEN);
        }
        else
        {
            /* Example objName:
             *  "Device.A.1.AB.2.ABC."  ->  (token.token.index.token.index.token)
             */

            j = 0;
            token = strtok_r(aux_objName, ".", &strtok_ctx);
            while (token)
            {
                if (!strcmp(token, "{i}"))
                {
                    /* Appending ObjName indexes
                     *
                     *     ... (+ token)
                     *     ... (+ token)
                     *  objName + index 1          >> "Device.A.1."
                     *     ... (+ token)
                     *  objName + index 1          >> "Device.A.1.AB.2."
                     *     ... (+ token)
                     */
                    snprintf(new_message.body.addObject.objName + strlen(new_message.body.addObject.objName),
                             MSG_MAX_STR_LEN, "%d.", curr_obj_indexvalues->indexvalues[ j++ ]);
                }
                else
                {
                    /* Appending ObjName tokens
                     *
                     *  objName + token "Device."  >> "Device."
                     *  objName + token "A."       >> "Device.A."
                     *     ... (+ index 1)
                     *  objName + token "AB."      >> "Device.A.1.AB."
                     *     ... (+ index 2)
                     *  objName + token "ABC."     >> "Device.A.1.AB.2.ABC."
                     */
                    snprintf(new_message.body.addObject.objName + strlen(new_message.body.addObject.objName),
                             MSG_MAX_STR_LEN, "%s.", token);
                }

                token = strtok_r(NULL, ".", &strtok_ctx);
            }
        }
        /* // extra debug
            RECURSLEVEL_DBG("Prepared objName '%s' for depenent addObject request",
                new_message.body.addObject.objName);
        */

        /* Fill AddObject ep_message's field (body->paramValues)
         * (the sole paramValue is dependency parameter value fetched from
         *  the Current Object parameter) */
        memset(aux_mem_buff, 0, aux_mem_buff_size);

        new_message.mem_pool.pool = aux_mem_buff;
        new_message.mem_pool.size_bytes = aux_mem_buff_size;
        new_message.mem_pool.curr_offset = 0;
        new_message.mem_pool.initialized = 1;
        new_message.body.addObject.arraySize = 0;

        /* Check if dependency parameter is index-parameter that is implemented
         * by Current Object - in such case it is not included into (body->paramValues)
         * as it is already known (it was injected into (body->objName)) */
        index_match = FALSE;
        for (j = 0; j < next_obj_idx_params_num; j++)
        {
            if (!strcmp(next_obj_idx_params[j], curr_obj_depInfo[i].childParamName))
            {
                index_match = TRUE; /* known index parameter is not filled into (body->paramValues) */
                break;
            }
        }
        if (!index_match)
        {
            /* Unknown (not matched) index parameter or just arbitrary parameter
             * specified in Objects dependency is to be filled into (body->paramValues)
             * but first its value - to be fetched from the Current Object instance
             * with SQL query to Values DB */

            new_message.body.addObject.arraySize = 1;
            /* Add dependency parameter name (of dependent Object) */
            strcpy_safe(new_message.body.addObject.paramValues[0].name,
                        curr_obj_depInfo[i].childParamName, NVP_MAX_NAME_LEN);
            /* To add dependency parameter value (of dependent Object) we run
             *  SQL query to fetch it from Current Object instance */
            status = ep_db_get_tbl_row_column(conn, curr_obj_info->objValuesTblName,
                               curr_obj_idx_params_num, curr_obj_idx_params, curr_obj_indexvalues->indexvalues,
                               (char *)curr_obj_depInfo[i].parentParamName, (char *)&pValue, sizeof(pValue));
            if (status != EPS_OK)
            {
                RECURSLEVEL_ERROR("==> Failed to build AddObject request for dependent Object '%s'",
                                   next_obj_info->objName);

                /* Error - recursion will be stopped */
                goto ret;
            }
            /* Add dependency parameter value (of dependent Object) */
            mmx_frontapi_msg_struct_insert_value(&new_message,
                        &(new_message.body.addObject.paramValues[0]),
                        pValue);
        }

        /* // extra debug
            RECURSLEVEL_DBG("Built AddObject (%s) request: paramValues arraySize (%d)",
                new_message.body.addObject.objName,
                new_message.body.addObject.arraySize);
            if (new_message.body.addObject.arraySize > 0)
                RECURSLEVEL_DBG("      AddObject paramValues[0]: ( %s = %s )",
                    new_message.body.addObject.paramValues[0].name,
                    new_message.body.addObject.paramValues[0].pValue);
        */


        /*
         * Dependent AddObject request is ready - it can be processed just
         *  in common way
         */


        /* Determine style of addobj operation. If DB type is not "running DB",
           the operation will be performed only in the DB and not in the backend */
        if ((next_obj_info->addObjStyle == OP_STYLE_DB) ||
            (next_obj_info->addObjStyle == OP_STYLE_SCRIPT) ||
            (next_obj_info->addObjStyle == OP_STYLE_BACKEND))
        {
            /* Style OP_STYLE_SHELL_SCRIPT is currently supported for
             *  SET operation per distinct Object parameter(s) only */
            if (message->header.mmxDbType == MMXDBTYPE_RUNNING)
                addStyle = next_obj_info->addObjStyle;
            else
                addStyle = OP_STYLE_DB;
        }
        else
        {
            status = EPS_NOT_IMPLEMENTED;
            RECURSLEVEL_ERROR("AddObject style `%s' currently not supported",
                               operstyle2string(next_obj_info->addObjStyle));

            /* Error - recursion will be stopped */
            goto ret;
        }

        /* AddObject is write operation. But we need not to receive EP write-lock
         * as it has been already received in parent caller (w_handle_addobject) */
        switch (addStyle)
        {
            case OP_STYLE_DB:
                status = w_addobject_db(wd, &new_message, &next_obj_pn, next_obj_info, conn,
                                    &(next_obj_paraminfo->param[0]),
                                    next_obj_paraminfo->param_num,
                                    &addStatus, &newInstance);
                break;
            case OP_STYLE_SCRIPT:
                status = w_addobject_script(wd, &new_message, &next_obj_pn, next_obj_info, conn,
                                    &(next_obj_paraminfo->param[0]),
                                    next_obj_paraminfo->param_num,
                                    &addStatus, &newInstance);
                break;
            case OP_STYLE_BACKEND:
                status = w_addobject_backend(wd, &new_message, &next_obj_pn, next_obj_info, conn,
                                    &(next_obj_paraminfo->param[0]),
                                    next_obj_paraminfo->param_num,
                                    &addStatus, &newInstance);
                break;
        }
        if (status != EPS_OK)
        {
            RECURSLEVEL_ERROR("AddObject failed for dependent Object %s (status %d)",
                next_obj_info->objName, status);

            /* Error - recursion will be stopped */
            goto ret;
        }

        /* Check addStatus: if non-zero - save to the total_addStatus and
         *  also save backend index to total_restart_be - all saved backend(s)
         *  will be restarted by parent caller once the recursion is finished */
        if (addStatus != 0)
        {
            *total_addStatus = addStatus;

            if ((j = ep_common_get_beinfo_index(next_obj_info->backEndName)) >= 0)
                total_restart_be[j] = TRUE;
        }

        memset(next_obj_indexvalues, 0, sizeof(exact_indexvalues_t) * MAX_TOTAL_OBJ_DEPDEPTH);
        /* After successful AddObject on dependent Object - its newInstance
         *  index parameter values must be saved to structure auto_add_objects
         *  at needed level - it will be used to create in next level for next
         *  dependent Object(s) instance */
        next_obj_pn.last_token_type = PATH_TOKEN_INDEX;
        next_obj_pn.indices[next_obj_pn.index_num - 1].type = REQ_IDX_TYPE_EXACT;
        next_obj_pn.indices[next_obj_pn.index_num - 1].exact_val.num = newInstance;
        next_obj_indexvalues->index_num = next_obj_pn.index_num;

        for (j = 0; j < next_obj_indexvalues->index_num; j++)
        {
            next_obj_indexvalues->indexvalues[j] = next_obj_pn.indices[j].exact_val.num;
        }


        RECURSLEVEL_DBG("===>>> Go to next level (L%d -> L%d) with Object '%s'",
            level, level+1, next_obj_info->objName);

        status = w_addobj_autocreate_recursively(wd, message, level+1,
                          total_addStatus, total_restart_be);

        if (status != EPS_OK)
        {
            if (failed_level == -1)
            {
                /* This is the time to save the failed_level */
                failed_level = level+1;

                RECURSLEVEL_ERROR("==> Recursion failed between (L%d -> L%d): Obj %s -> %s",
                    level, level+1, curr_obj_info->objName, next_obj_info->objName);
            }
            else
            {
                RECURSLEVEL_ERROR("==> Recursion failure raised upper (L%d -> L%d): Obj %s -> %s",
                    level, level+1, curr_obj_info->objName, next_obj_info->objName);
            }

            goto ret;
        }
    } /* End of for ( over Current Object dependencies ) */

ret:
    if (status == EPS_NOTHING_DONE)
        status = EPS_OK;

    if (status == EPS_OK || level == 0)
    {
        RECURSLEVEL_DBG("=====  Done autoAddObject (L%d) for '%s' - status %d  =====",
                level, curr_obj_info->objName, status);
    }

    return status;
}

static ep_stat_t w_handle_addobject(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;
    int i, j, obj_num, param_num, rowCount = 0;
    int addStatus = 0, newInstance = 0;
    int addStyle = 0;
    int restart_be[MAX_BACKEND_NUM];
    obj_info_t obj_info;
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    parsed_param_name_t pn;

    sqlite3 *conn = NULL;

    ep_message_t answer = {{0}};

    memset((char *)restart_be, 0, sizeof(restart_be));

    memcpy(&answer.header, &message->header, sizeof(answer.header));
    answer.header.respFlag = 1;
    answer.header.msgType = MSGTYPE_ADDOBJECT_RESP;

    if (operAllowedForDbType(OP_ADDOBJ, message->header.mmxDbType) != TRUE)
        GOTO_RET_WITH_ERROR(EPS_INVALID_DB_TYPE,
           "Operation ADDOBJ is not permitted for db type %d", message->header.mmxDbType);

    if ((status = w_init_mmxdb_handles(wd, message->header.mmxDbType,
                                     message->header.msgType)) != EPS_OK)
        goto ret;

    conn = wd->main_conn;

    if ((status = parse_param_name(message->body.addObject.objName, &pn)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not parse name");

    pn.partial_path = FALSE;

    /* Add the last {i} placeholder, since objName in addObj request doesn't contain
       the last index (it is unknown for an object instance to be created) */
    strcat_safe(pn.obj_name, "{i}.", sizeof(pn.obj_name));

    if (w_get_obj_info(wd, &pn, 0, 0, &obj_info, 1, &obj_num) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_INVALID_ARGUMENT, "Could not get object info for %s", pn.obj_name);

    if (obj_num != 1)
        GOTO_RET_WITH_ERROR(EPS_INVALID_ARGUMENT, "Ambiguous or invalid obj name %s: %d objects retrieved", pn.obj_name, obj_num);

    if (!obj_info.writable)
        GOTO_RET_WITH_ERROR(EPS_NO_PERMISSION, "New instance cannot be added. Object %s is not writable (%s)",
                            pn.obj_name, obj_info.objName);

    if (!objWriteAllowed(&obj_info, message->header.callerId))
        GOTO_RET_WITH_ERROR(EPS_NO_PERMISSION, "New instance cannot be added by caller %d", message->header.callerId);

    if (w_get_param_info(wd, &pn, &obj_info, 0, param_info, &param_num, NULL) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_INVALID_ARGUMENT, "Could not retrieve parameters info for object %s", pn.obj_name);

    if ((status = ep_db_get_tbl_row_count(conn, obj_info.objValuesTblName,
                                          &rowCount)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Couldn't get row count in %s (%d)", obj_info.objValuesDbName, status);

    if (rowCount >= MAX_INSTANCES_PER_OBJECT)
        GOTO_RET_WITH_ERROR(EPS_NO_MORE_ROOM, "Object %s already has max num of instances (%d)",
                            pn.obj_name, rowCount);

    /* Determine style of addobj operation. If DB type is not "running DB",
       the operation will be performed only in the DB and not in the backend */

    /* Style OP_STYLE_SHELL_SCRIPT is currently supported for
     *  SET operation per distinct Object parameter(s) only */
    if ((obj_info.addObjStyle == OP_STYLE_DB) ||
        (obj_info.addObjStyle == OP_STYLE_SCRIPT) ||
        (obj_info.addObjStyle == OP_STYLE_BACKEND))
    {
        if (message->header.mmxDbType == MMXDBTYPE_RUNNING)
            addStyle = obj_info.addObjStyle;
        else
            addStyle = OP_STYLE_DB;
    }
    else
        GOTO_RET_WITH_ERROR(EPS_NOT_IMPLEMENTED, "AddObject style `%s' currently not supported",
                            operstyle2string(obj_info.addObjStyle));


    /* ---- AddObject is write operation. EP write-lock must be received ---- */
    if (ep_common_get_write_lock(MSGTYPE_ADDOBJECT, message->header.txaId, message->header.callerId) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_RESOURCE_NOT_FREE, "Could not receive write lock for AddObject operation");

    switch (addStyle)
    {
        case OP_STYLE_DB:
            status = w_addobject_db(wd, message, &pn, &obj_info, conn,
                                    param_info, param_num, &addStatus, &newInstance);
            break;
        case OP_STYLE_SCRIPT:
            status = w_addobject_script(wd, message, &pn, &obj_info, conn,
                                        param_info, param_num, &addStatus, &newInstance);
            break;
        case OP_STYLE_BACKEND:
            status = w_addobject_backend(wd, message, &pn, &obj_info, conn,
                                         param_info, param_num, &addStatus, &newInstance);
            break;
    }

    if (status == EPS_OK)
    {
        /* Initialize auto_add_objects structure to run autoCreate recursion */
        pn.last_token_type = PATH_TOKEN_INDEX;
        pn.indices[pn.index_num].type = REQ_IDX_TYPE_EXACT;
        pn.indices[pn.index_num].exact_val.num = newInstance;
        pn.index_num++;
        /* Fill auto_add_objects structure (added Object instance at level = 0) */
        memset(&auto_add_objects, 0, sizeof(auto_add_objects));
        memcpy(&(auto_add_objects.obj_info[0]), &obj_info, sizeof(obj_info_t));
        memcpy(&(auto_add_objects.obj_param_info[0].param[0]), &param_info, MAX_PARAMS_PER_OBJECT * sizeof(param_info_t));
        auto_add_objects.obj_param_info[0].param_num = param_num;
        auto_add_objects.obj_indexvalues[0].index_num = pn.index_num;

        for (i = 0; i < pn.index_num; i++)
        {
            auto_add_objects.obj_indexvalues[0].indexvalues[i] = pn.indices[i].exact_val.num;
        }

        /* Create dependent Object instances (if any) */
        DBG("======== Starting autoCreate recursion for Object '%s' (newInstance %d) ========",
            obj_info.objName, newInstance);

        status = w_addobj_autocreate_recursively(wd, message,
                         /* dependency level = */ 0, &addStatus, restart_be);

        if (status != EPS_OK)
        {
            /* ------- Release EP write operation lock ------- */
            ep_common_finalize_write_lock(message->header.txaId, message->header.callerId, FALSE);
            /* Jump to the return point with received error status */
            GOTO_RET_WITH_ERROR(status, "Failed to run autoCreate recursion for Object %s (status %d)",
                                obj_info.objName, status);
        }

        DBG("======== Successful finish of autoCreate recursion for Object %s ========",
            obj_info.objName);
    }

    if ((status == EPS_OK) && (message->header.mmxDbType == MMXDBTYPE_CANDIDATE))
    {
        char buf[FILENAME_BUF_LEN] = {0};
        w_save_file(get_db_cand_path((char*)buf, FILENAME_BUF_LEN));
    }

    /* ------- Release EP write operation lock ------- */
    ep_common_finalize_write_lock(message->header.txaId, message->header.callerId, FALSE);

ret:
    answer.header.respCode = w_status2cwmp_error(status);

    if (status == EPS_OK)
    {
        answer.body.addObjectResponse.status = addStatus;
        answer.body.addObjectResponse.instanceNumber = newInstance;
    }
    w_send_answer(wd, &answer);

    /* Perform restart of the backend(s) if needed */
    if (addStatus != 0)
    {
        if ((j = ep_common_get_beinfo_index(obj_info.backEndName)) >= 0)
            restart_be[j] = TRUE;

        w_restart_backends(MSGTYPE_ADDOBJECT, restart_be);
    }

    return status;
}

/* -------------------------------------------------------------------*/
/* ---------------- Delete object procedures ------------------------ */
/*--------------------------------------------------------------------*/
static ep_stat_t w_verify_objname_for_del(parsed_param_name_t *pn,
                                          param_info_t param_info[], int param_num)
{
    ep_stat_t status = EPS_OK;
    int i, idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];

    /* Save index names */
    get_index_param_names(param_info, param_num, idx_params, &idx_params_num);

    if (idx_params_num != pn->index_num)
        GOTO_RET_WITH_ERROR(EPS_INVALID_PARAM_NAME,
        "Wrong number of indeces (%d of %d) stated in objname",pn->index_num,idx_params_num);

    /* All indeces except of the last one must have exact value
     *
     * TODO re-check if this is a limitation from MMX but not TR-069 ..?
     */
    for (i = 0; i < idx_params_num-1; i++)
    {
        if (pn->indices[i].type != REQ_IDX_TYPE_EXACT)
            GOTO_RET_WITH_ERROR(EPS_INVALID_PARAM_NAME,
                                "Wrong not exact index (%d) stated in objname", i);
    }

    /* Verify the last token of the object's path-name */
    if (pn->last_token_type != PATH_TOKEN_INDEX)
        GOTO_RET_WITH_ERROR(EPS_INVALID_PARAM_NAME,
            "Last token of objname must be index (not node or param name) (err: %d)",pn->last_token_type);
ret:
    return status;
}

/* Function deletes rows from DB value tables of all objects specified in
 * obj_info array.
 * obj_info array contains information about object to be deleted and all
 *                its depended objects;
 * where  - contains WHERE clause used for deleted all depended instance
 *           rows
 */
static ep_stat_t w_delete_rows_by_where(sqlite3 *dbconn, obj_info_t *obj_info,
                                        int obj_num, char *where)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;
    int i;
    char query[EP_SQL_REQUEST_BUF_SIZE];
    int  modified_rows_num = 0;

    if (obj_num <= 0)
    {
        DBG("Bad number of objects %d. Operation is ignored", obj_num);
        return EPS_OK;
    }

    /* Remove appropriated instance rows of all depended objects*/
    for (i = 0; i < obj_num; i++)
    {
        memset ((char *)query, 0, sizeof(query));
        strcpy_safe(query, "DELETE  FROM ", sizeof(query));
        strcat_safe(query, obj_info[i].objValuesTblName, sizeof(query));
        strcat_safe(query, where, sizeof(query));

        /* Perform prepared query */
        DBG("Delete query for obj %s:\n  %s",obj_info[i].objName, query);
        if ((status1 = ep_db_exec_write_query(dbconn, query, &modified_rows_num)) != EPS_OK)
        {
            ERROR("Could not execute DELETE query (%d)", status1);

            /*If the "main" obj row cannot be deleted the error should be saved;
              we will return it after all depended obj's rows will be processed*/
            if (i == 0)
                status = status1;
        }
    } /* End of for stmt over depended objects */

//ret:
    return status;
}

#if 0

/* -------------------------------------------------------------------*
 * ---------- Old (MMX-1.04) versioned DelObject handlers ----------- *
 * -------------------------------------------------------------------*
 *   - w_delobject_db()
 *   - w_delobject_script()
 *   - w_delobject_backend()
 *
 *   Currently commented (as not relevant). In next releases these
 *   procedures are to be removed or reused somehow
 * -------------------------------------------------------------------*/



static ep_stat_t w_delobject_db(worker_data_t *wd, ep_message_t *message, sqlite3 *dbconn,
                       parsed_param_name_t *pn, obj_info_t *obj_info, int obj_num,
                       param_info_t param_info[], int param_num, int *delStatus)
{
    ep_stat_t status = EPS_OK;
    int i, idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    char query[1024], where[512];

    /* Save index names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /* Prepare query to delete the requested obj instance from db */
    memset((char *)query, 0, sizeof(query));
    strcpy_safe(query, "DELETE  FROM ", sizeof(query));
    strcat_safe(query, obj_info->objValuesTblName, sizeof(query));
    strcat_safe(query, " WHERE 1 ", sizeof(query));

    /* Prepare WHERE clause with names and values of index parameters */
    memset(where, 0, sizeof(where));
    strcat_safe(where, " WHERE 1 ", sizeof(query));
    for (i = 0; i < pn->index_num; i++)
    {
        if (pn->indices[i].type == REQ_IDX_TYPE_EXACT)
        {
            strcat_safe(where, " AND [", sizeof(query));
            strcat_safe(where, idx_params[i], sizeof(query));
            strcat_safe(where, "] = ", sizeof(query));
            sprintf(where+strlen(where), "%d", pn->indices[i].exact_val.num);
        }
        else if (pn->indices[i].type == REQ_IDX_TYPE_RANGE)
        {
            /* concatenate (PARAM>=RANGE_BEGIN AND PARAM<=RANGE_END) */
            strcat_safe(where, "([", sizeof(where));
            strcat_safe(where, idx_params[i], sizeof(where));
            strcat_safe(where, "]>=", sizeof(where));
            sprintf(where+strlen(where), "%d", pn->indices[i].range_val.begin);
            strcat_safe(where, " AND [", sizeof(where));
            strcat_safe(where, idx_params[i], sizeof(where));
            strcat_safe(where, "]<=", sizeof(where));
            sprintf(where+strlen(where), "%d", pn->indices[i].range_val.end);
            strcat_safe(where, ")", sizeof(where));
            strcat_safe(where, " AND ", sizeof(where));
        }
    }
    DBG("DelObject where clause:\n     %s", where);

    /* Perform delete with prepared where clause */
    if ((status = w_delete_rows_by_where(dbconn, obj_info, obj_num, where)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not execute DelObject SQL query: %d", status);

    DBG("Instance %d of object %s is successfully deleted from DB",
         pn->indices[idx_params_num-1].exact_val.num, obj_info->objName);
ret:
    *delStatus = 0;
    return status;
}

static ep_stat_t w_delobject_script(worker_data_t *wd, ep_message_t *message,
                                    sqlite3 *dbconn, parsed_param_name_t *pn,
                                    obj_info_t *obj_info, int obj_num,
                                    param_info_t param_info[], int param_num,
                                    int *delStatus)
{
    ep_stat_t status = EPS_OK, status1 = EPS_OK;
    int  i = 0, res_code, parsed_status;
    int  idx_params_num = 0, idx_values[MAX_INDECES_PER_OBJECT];
    int  total_cnt = 0, success_cnt = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];

    char *delMethod, buf[1024];
    parsed_operation_t parsed_script_str;

    char *p_extr_results;
    BOOL  more_instance = TRUE;
    char where[256];
    sqlite3_stmt *stmt = NULL;

    /* Save index values */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    *delStatus = 0;

    delMethod = (char *)&obj_info->delObjMethod;
    w_parse_operation_string(OP_DELOBJ, delMethod, &parsed_script_str);

    while (more_instance == TRUE)
    {
        /* Prepare shell command (with all needed info) and perform it */
        status1 = w_prepare_command(wd, pn, obj_info, dbconn, &parsed_script_str,
                                   idx_params, idx_values, idx_params_num,
                                   (char*)&buf, sizeof(buf), &stmt);
        if (status1 != EPS_OK)
        {
            if (status1 != EPS_NOTHING_DONE && status1 != EPS_EMPTY)
                GOTO_RET_WITH_ERROR(status1, "Could not prepare del object command for %s",obj_info->objName);

            more_instance = FALSE;
            continue;
        }

        DBG("Full prepared command (%d): \n\t%s", ++total_cnt, buf);

        /* Perform prepared command and parse results. Results look as follows:
         in case of failure:  "resCode;", where resCode is not 0
         in case of success:  "0; delStatus" or "0;", where delStatus is 0 or 1 */
        p_extr_results = w_perform_prepared_command(buf, sizeof(buf), TRUE, NULL);
        if (!p_extr_results)
            GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not read script results");

        DBG("Result of the command: %s ", buf);
        status = w_parse_script_res(buf, sizeof(buf), &res_code, &parsed_status, NULL);
        if (status == EPS_OK)
        {
            //DBG ("Parsed script results: resCode %d, setStatus %d", res_code, parsed_status);
            if (parsed_status > 0)
                *delStatus = parsed_status;
        }

        if (res_code == 0)
        {
            success_cnt++;

            /*Prepare WHERE clause: it is the same for all depended objects */
            memset ((char *)where, 0, sizeof(where));
            strcat_safe(where, " WHERE 1 ", sizeof(where));
            for (i = 0; i < idx_params_num; i++)
            {
                strcat_safe(where, " AND [", sizeof(where));
                strcat_safe(where, idx_params[i], sizeof(where));
                strcat_safe(where, "] = ", sizeof(where));
                sprintf(where+strlen(where), "%d", idx_values[i]);
            }
            w_delete_rows_by_where(dbconn, obj_info, obj_num, where);
        }
        else // Script failed
        {
            DBG("DelObject method script returned error: %d;", res_code);
        }

        if (stmt == NULL) more_instance = FALSE;

    } //End of while stmt over instances

    if (success_cnt == 0)
        if (status1 != EPS_EMPTY)
            status = EPS_BACKEND_ERROR;
        else
            DBG("requested objects already deleted, operation completed successfully");
    else
        DBG("%d of %d delete operations completed successfully",success_cnt,total_cnt);

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/* Process deleting of an object instance where the objects uses
   backend-style delete method.
   Object instance to be deleted is defined in the pn parameter
   The output parameter delStatus contains
    0 - if operation has been fully completed and no action is needed,
    1 - when backend restart is requested                             */
static ep_stat_t w_delobject_backend(worker_data_t *wd, ep_message_t *message,
                      sqlite3 * dbconn,
                      parsed_param_name_t *pn, obj_info_t *obj_info, int obj_num,
                      param_info_t param_info[], int param_num, int *delStatus)
{
    ep_stat_t status = EPS_OK;
    int res, be_port, i, objCnt = 0;
    int idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_values[MAX_INDECES_PER_OBJECT];

    mmxba_request_t be_ans;
    parsed_backend_method_t parsed_method;
    BOOL  more_instance = TRUE;
    sqlite3_stmt *stmt = NULL;
    char query[1024], where[256];

    /* Save index param names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    w_parse_backend_method_string(OP_DELOBJ, obj_info->delObjMethod, &parsed_method);

    /* Get port number of the backend */
    if ((w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0) != EPS_OK) ||
        (be_port <= 0))
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s",
                            be_port, obj_info->backEndName);

    DBG("Backend '%s': port %d", obj_info->backEndName, be_port);

    /* Form query to select value of substituted parameters*/
    w_form_subst_sql_select_backend(wd, pn, obj_info, query, sizeof(query),
                                    idx_params, idx_params_num, &parsed_method);
    DBG("Query to get subst params for DELOBJ operation:\n%s", query);

    if (strlen(query) == 0)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare subst sql query for obj %s",
                            obj_info->objName);

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                            sqlite3_errmsg(dbconn));
    while (more_instance)
    {
        res = sqlite3_step(stmt);
        if ((res != SQLITE_ROW) && (res != SQLITE_DONE))
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s",
                                               sqlite3_errmsg(dbconn));
        else if (res == SQLITE_DONE)
            more_instance = FALSE;

        if (more_instance)
        {
            /*Save index values i.e. the first values returned by subst SQL query*/
            for (i = 0; i < idx_params_num; i++)
                idx_values[i] = sqlite3_column_int(stmt, i);

            /* Form BE API request, send it to the backend, wait for reply */
            status = form_and_send_be_request(wd, be_port, MMXBA_OP_TYPE_DELOBJ,
                        &parsed_method,stmt, idx_params_num, 0, NULL, 0, NULL, &be_ans);
            if (status != EPS_OK)
                GOTO_RET_WITH_ERROR(status, "BE DELOBJ request failure (%d)", status);

            if (be_ans.opResCode != 0)
                GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,"Backend returned error: %d: %d: %s",
                    be_ans.opResCode, be_ans.opExtErrCode,
                    strlen(be_ans.errMsg) ? be_ans.errMsg : " ");

            *delStatus = be_ans.postOpStatus;

            /* Now delete DB row of the object and all "dependent" objects
               (WHERE clause is the same for all depended objects)        */
            memset((char *)where, 0, sizeof(where));
            strcat_safe(where, " WHERE 1 ", sizeof(where));
            for (i = 0; i < idx_params_num; i++)
            {
                strcat_safe(where, " AND [", sizeof(where));
                strcat_safe(where, idx_params[i], sizeof(where));
                strcat_safe(where, "] = ", sizeof(where));
                sprintf(where+strlen(where), "%d", idx_values[i]);
            }
            w_delete_rows_by_where(dbconn, obj_info, obj_num, where);

            objCnt++;
        }

    } // End of while (more_instance)

ret:
    if (stmt) sqlite3_finalize(stmt);

    if (status != EPS_OK)
        DBG("DELOBJ operation failed (status = %d)", status);
    else
        DBG("%d instances of obj %s were deleted ", objCnt,obj_info->objName);

    return status;
}
#endif /* Old (MMX-1.04) versioned DelObject handlers */


/* -------------------------------------------------------------------*
 * ---------- New (MMX-1.05) versioned DelObject handlers ----------- *
 * -------------------------------------------------------------------*/

/* Structure that defines the Object's N-level autoDelete dependency.
 * Used in the below recursion function for automatic deletion of
 *  dependent Object instances.
 *
 * Important note:
 *  the structure defines the single N-level deep dependency for the chain of
 *  Objects
 *
 *   (Object0)
 *      -> (some DependentObject at Level 1)
 *            -> (some DependentObject at Level 2)
 *                  -> ...
 *                        -> (some DependentObject at Level N)
 *
 *    where N == MAX_DEPDEPTH_PER_OBJECT
 */
typedef struct delobj_autodelete_objects_s
{
    /* ObjInfo of the deleted Object and its dependent Object(s) chain */
    obj_info_t        obj_info[MAX_TOTAL_OBJ_DEPDEPTH];
    /* ObjParamInfo of the deleted Object and its dependent Object(s) chain */
    obj_param_info_t  obj_param_info[MAX_TOTAL_OBJ_DEPDEPTH];
    /* Index values for the deleted Object (dependent Objects) instance(s) */
    exact_indexvalues_set_t   obj_indexvalues_set[MAX_TOTAL_OBJ_DEPDEPTH];
} delobj_autodelete_objects_t;

static delobj_autodelete_objects_t auto_del_objects;


/* w_delobject_instance_from_db
 * deletes an Object instance from ValuesDB
 * Function provides no output
 */
static ep_stat_t w_delobject_instance_from_db(worker_data_t *wd, sqlite3 *dbconn,
                                      obj_info_t *obj_info, char *idx_params[],
                                      int idx_values[], int idx_values_num)
{
    ep_stat_t status = EPS_OK;

    int i = 0;
    char del_query[EP_SQL_REQUEST_BUF_SIZE];
    int modified_rows_num = 0;

    /* Prepare SQL DELETE query */
    snprintf(del_query, sizeof(del_query), "DELETE FROM %s WHERE 1", obj_info->objValuesTblName);

    for (i = 0; i < idx_values_num; i++)
    {
        snprintf(del_query + strlen(del_query), sizeof(del_query),
                 " AND [%s] = %d", idx_params[i], idx_values[i]);
    }
    DBG("Delete query for obj '%s' instance:\n\t%s", obj_info->objName, del_query);

    /* Perform prepared query for Object instance */
    status = ep_db_exec_write_query(dbconn, del_query, &modified_rows_num);
    if (status != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(status, "Could not execute DELETE query (%d)", status);
    }

    DBG("Instance %d of Object %s is successfully deleted from DB",
        idx_values[i-1], obj_info->objName);

ret:
    return status;
}

static ep_stat_t w_delobject_db(worker_data_t *wd, sqlite3 *dbconn,
                             obj_info_t *obj_info, char *idx_params[],
                             exact_indexvalues_set_t *indexvalues_set,
                             int *delStatus)
{
    ep_stat_t status = EPS_OK;
    ep_stat_t status1 = EPS_OK;

    int i = 0, j = 0;
    int inst_num = indexvalues_set->inst_num;
    int idx_num = indexvalues_set->index_num;
    int idx_values[MAX_INDECES_PER_OBJECT] = {0};

    /* Delete each Object instance separately */
    for (i = 0; i < inst_num; i++)
    {
        /* Fill the instance index values */
        for (j = 0; j < idx_num; j++)
        {
            idx_values[j] = indexvalues_set->indexvalues[i][j];
        }

        status1 = w_delobject_instance_from_db(wd, dbconn, obj_info,
                              idx_params, idx_values, idx_num);
        if (status1 != EPS_OK)
        {
            status = status1;
            /* TODO how to handle DELETE Query failure ? now stopping the loop */
            ERROR("Could not execute DelObject SQL query: %d", status);
            GOTO_RET_WITH_ERROR(status, " Stopping DelObject at %d (of %d) instance",
                            i+1, inst_num);
        }
    }

    DBG("DelObject for %d Object '%s' DB instances completed successfully",
        inst_num, obj_info->objName);

ret:
    *delStatus = 0;
    return status;
}

static ep_stat_t w_delobject_script(worker_data_t *wd, sqlite3 *dbconn,
                             obj_info_t *obj_info, char *idx_params[],
                             exact_indexvalues_set_t *indexvalues_set,
                             int *delStatus)
{
    ep_stat_t status = EPS_OK;
    ep_stat_t status1 = EPS_OK;

    int i = 0, j = 0, total_cnt = 0, success_cnt = 0;
    int inst_num = indexvalues_set->inst_num;
    int idx_num = indexvalues_set->index_num;
    int idx_values[MAX_INDECES_PER_OBJECT] = {0};
    int res_code, parsed_status;

    char objName[MSG_MAX_STR_LEN] = {0};
    char *delMethod, buf[EP_SQL_REQUEST_BUF_SIZE];
    char *p_extr_results;
    parsed_param_name_t pn = {0};
    parsed_operation_t parsed_script_str;

    *delStatus = 0;

    strcpy_safe(objName, obj_info->objName, sizeof(objName));
    status = parse_param_name(objName, &pn);
    if (status != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(status, "Could not parse object name: %s", objName);
    }

    if (pn.index_num != idx_num)
    {
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR,
            "Wrong number of indeces (%d of %d) stated in objname", pn.index_num, idx_num);
    }

    delMethod = (char *)&obj_info->delObjMethod;
    w_parse_operation_string(OP_DELOBJ, delMethod, &parsed_script_str);

    /* Delete each Object instance separately */
    for (i = 0; i < inst_num; i++)
    {
        /* Fill the instance index values, parsed_param_name */
        for (j = 0; j < idx_num; j++)
        {
            idx_values[j] = indexvalues_set->indexvalues[i][j];

            pn.indices[j].type = REQ_IDX_TYPE_EXACT;
            pn.indices[j].exact_val.num = indexvalues_set->indexvalues[i][j];
        }

        /* Prepare shell command (with all needed info) and perform it */
        status1 = w_prepare_command2(wd, &pn, obj_info, dbconn,  &parsed_script_str,
                                     idx_params, idx_num, (char*)&buf, sizeof(buf));
        if (status1 != EPS_OK)
        {
            if (status1 == EPS_EMPTY)
            {
                DBG("Instance %d of %s was not found in DB - considered to be already deleted",
                      i+1, inst_num);

                total_cnt++;
                success_cnt++;
                continue;
            }

            /* status1 = EPS_SQL_ERROR */
            GOTO_RET_WITH_ERROR(status1, "Could not prepare del object command for %s (status %d)",
                          objName, status1);
        }

        DBG("Full prepared command (%d): \n\t%s", ++total_cnt, buf);

        /* Perform prepared command and parse results. Results look as follows:
         in case of failure:  "resCode;", where resCode is not 0
         in case of success:  "0; delStatus" or "0;", where delStatus is 0 or 1 */
        p_extr_results = w_perform_prepared_command(buf, sizeof(buf), TRUE, NULL);
        if (!p_extr_results)
            GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not read script results");

        DBG("Result of the command: %s ", buf);
        status = w_parse_script_res(buf, sizeof(buf), &res_code, &parsed_status, NULL);
        if (status == EPS_OK)
        {
            DBG("Parsed script results: resCode %d, setStatus %d", res_code, parsed_status);
            if (parsed_status > 0)
                *delStatus = parsed_status;
        }

        if (res_code == 0)
        {
            success_cnt++;

            status1 = w_delobject_instance_from_db(wd, dbconn, obj_info,
                                  idx_params, idx_values, idx_num);
            if (status1 != EPS_OK)
            {
                /* TODO how to handle DELETE Query failure ? now ignoring */
                //status = status1;
                //ERROR("Could not execute DelObject SQL query: %d", status);

                WARN("Could not delete obj instance from DB (status %d)", status1);
            }
        }
        else /* Script failed */
        {
            /* TODO how to handle DelObject script failure ? now ignoring */
            DBG("DelObject method script returned error: %d;", res_code);
        }
    } /* End of for ( over deleted Object instances ) */

ret:
    /* No successful delete operations - this is an error */
    if (success_cnt == 0)
        status = (status == EPS_OK) ? EPS_BACKEND_ERROR : status;

    /* Print the summary */
    DBG("Results (status %d):\n\t%d instances were requested to be deleted\n\t"
        "%d of %d performed delete operations completed successfully", status,
        inst_num,
        success_cnt, total_cnt);

    return status;
}

static ep_stat_t w_delobject_backend(worker_data_t *wd, sqlite3 *dbconn,
                             obj_info_t *obj_info, char *idx_params[],
                             exact_indexvalues_set_t *indexvalues_set,
                             int *delStatus)
{
    ep_stat_t status = EPS_OK;
    ep_stat_t status1 = EPS_OK;

    int i = 0, j = 0;
    int step, res, be_port, objCnt = 0;
    int inst_num = indexvalues_set->inst_num;
    int idx_num = indexvalues_set->index_num;
    int idx_values[MAX_INDECES_PER_OBJECT] = {0};

    char objName[MSG_MAX_STR_LEN] = {0};
    parsed_param_name_t pn = {0};

    mmxba_request_t be_ans;
    parsed_backend_method_t parsed_method;

    sqlite3_stmt *stmt = NULL;
    char query[EP_SQL_REQUEST_BUF_SIZE];
    char *delMethod;

    strcpy_safe(objName, obj_info->objName, sizeof(objName));
    status = parse_param_name(objName, &pn);
    if (status != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(status, "Could not parse object name: %s", objName);
    }

    if (pn.index_num != idx_num)
    {
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR,
            "Wrong number of indeces (%d of %d) stated in objname", pn.index_num, idx_num);
    }

    delMethod = (char *)&obj_info->delObjMethod;
    w_parse_backend_method_string(OP_DELOBJ, delMethod, &parsed_method);

    /* Get port number of the backend */
    if ((w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0) != EPS_OK) ||
        (be_port <= 0))
    {
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s",
                            be_port, obj_info->backEndName);
    }

    DBG("Backend '%s': port %d", obj_info->backEndName, be_port);

    /* Delete each Object instance separately */
    for (i = 0; i < inst_num; i++)
    {
        memset(query, 0, sizeof(query));

        /* Fill the instance index values, parsed_param_name */
        for (j = 0; j < idx_num; j++)
        {
            idx_values[j] = indexvalues_set->indexvalues[i][j];

            pn.indices[j].type = REQ_IDX_TYPE_EXACT;
            pn.indices[j].exact_val.num = indexvalues_set->indexvalues[i][j];
        }

        /* Form query to select value of substituted parameters (per Object instance) */
        w_form_subst_sql_select_backend(wd, &pn, obj_info, query, sizeof(query),
                                        idx_params, idx_num, &parsed_method);
        DBG("Query to get subst params for DELOBJ (instance %d of %d) operation:\n\t%s",
            i+1, inst_num, query);

        if (strlen(query) == 0)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare subst sql query for obj %s"
                                "(instance %d of %d)", obj_info->objName, i+1, inst_num);

        if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                                sqlite3_errmsg(dbconn));

        /* Running loop to fetch rows from db, actually single (step) row is expected */
        step = 0;
        while (TRUE)
        {
            res = sqlite3_step(stmt);

            if (res == SQLITE_ROW)
            {
                if (step == 0)
                {
                    memset(&be_ans, 0, sizeof(be_ans));

                    /* Form BE API request, send it to the backend, wait for reply */
                    status = form_and_send_be_request(wd, be_port, MMXBA_OP_TYPE_DELOBJ,
                                                      &parsed_method, stmt, idx_num,
                                                      0, NULL, 0, NULL, &be_ans);
                    if (status != EPS_OK)
                        GOTO_RET_WITH_ERROR(status, "BE DELOBJ request failure (%d)", status);

                    if (be_ans.opResCode != 0)
                        GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,"Backend returned error: %d: %d: %s",
                            be_ans.opResCode, be_ans.opExtErrCode,
                            strlen(be_ans.errMsg) ? be_ans.errMsg : " ");

                    *delStatus = be_ans.postOpStatus;

                    status1 = w_delobject_instance_from_db(wd, dbconn, obj_info,
                                          idx_params, idx_values, idx_num);
                    if (status1 != EPS_OK)
                    {
                        /* TODO how to handle DELETE Query failure ? now ignoring */
                        //status = status1;
                        //ERROR("Could not execute DelObject SQL query: %d", status);

                        WARN("Could not delete obj instance from DB (status %d)", status1);
                    }

                    objCnt++;
                }
                else
                {
                    WARN("Warning: SQL query returned not a single row - ignore extra row(s)");
                    break;
                }

                step++;
            }
            else
            {
                if (res != SQLITE_DONE) /* SQL error */
                    GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Couldn't execute query to select subst values (%d): %s",
                                    res, sqlite3_errmsg(dbconn));

                /* SQLITE_DONE */
                if (step == 0) /* no row was selected from db */
                {
                    DBG("Instance %d of %s was not found in DB - considered to be already deleted",
                        i+1, inst_num);

                    objCnt++;
                }
                /* else - the last ("neutral") iteration */

                break;
            }
        }

        if (stmt)
        {
            sqlite3_finalize(stmt);
            stmt = NULL;
        }
    } /* End of for ( over deleted Object instances ) */

ret:
    if (stmt) sqlite3_finalize(stmt);

    /* Print the summary */
    DBG("Results (status %d):\n\t%d instances were requested to be deleted\n\t"
        "%d instances were successfully deleted", status, inst_num,
        objCnt);

    return status;
}

/* print_delobj_inst_indexvalues
 * Print the index-parameter values of the Object instances saved
 *  in array 'auto_del_objects' (this array is used for processing
 *  DelObject request and keeps info of deleted Object instances
 *  and auto-deleted (by dependency) Object instances).
 * Parameter 'dep_level' - index of the deleted/auto-deleted
 *  (by dependency) Object in 'auto_del_objects' array
 * Function provides no output
 */
static void print_delobj_inst_indexvalues(int dep_level)
{
    char printbuf[64];
    int i, j, l = dep_level;

    if (l > MAX_TOTAL_OBJ_DEPDEPTH)
    {
        WARN("Invalid input - dependency level (%d)", l);
        return;
    }

    obj_info_t *obj_info = &(auto_del_objects.obj_info[l]);
    exact_indexvalues_set_t *ivset = &(auto_del_objects.obj_indexvalues_set[l]);

    if (ivset->inst_num <= 0)
    {
        DBG("Obj '%s' has 0 instances - nothing to print", obj_info->objName);
        return;
    }

    DBG("Printing fetched DB Obj '%s' instances (%d)", obj_info->objName, ivset->inst_num);
    for (i = 0; i < ivset->inst_num; i++)
    {
        memset(printbuf, 0, sizeof(printbuf));
        snprintf(printbuf, sizeof(printbuf), "   Obj instance [%d] indexes: { ", i);

        for (j = 0; j < ivset->index_num - 1; j++)
        {
            snprintf(printbuf + strlen(printbuf), sizeof(printbuf),
                     "%d, ", ivset->indexvalues[i][j]);
        }
        snprintf(printbuf + strlen(printbuf), sizeof(printbuf),
                 "%d }", ivset->indexvalues[i][j]);

        DBG("%s", printbuf);
    }
}


/* [Recursion function] w_delobj_autodelete_recursively
 * Pre-process DelObject request - function searches 'autoDelete' dependencies
 *  for the deleted Object, fetches dependent Object instances to be
 *  automatically deleted and deletes it starting from the most deep
 *  subsidiary dependent Object instances to the very first dependent
 *  Object instances. In case there is a DelObject failure at some level
 *  the function does not continue the deletion in upper levels.
 *
 * Important note - instances of the Object from management request (level = 0)
 *  are not deleted by this function - they are deleted by the parent caller.
 */
ep_stat_t w_delobj_autodelete_recursively(worker_data_t *wd,
                                          ep_message_t *message,
                                          int level)
{
    ep_stat_t status = EPS_OK;

    static int failed_level;

    int i = 0, j = 0, k = 0;
    sqlite3 *conn = wd->main_conn;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_params_num = 0;
    int restart_be[MAX_BACKEND_NUM];
    int delStatus = 0;
    int delStyle = 0;
    char filebuf[FILENAME_BUF_LEN] = {0};
    char where_cond[EP_SQL_REQUEST_BUF_SIZE] = {0};

    /* Auxiliary variables */
    int obj_num = 0;
    BOOL param_match = FALSE;
    obj_dependency_info_t *db_objdepInfo = NULL;
    int db_obj_depNum = 0;

    /* Current Object */
    obj_info_t               *curr_obj_info = NULL;
    obj_param_info_t         *curr_obj_paraminfo = NULL;
    exact_indexvalues_set_t  *curr_obj_indexset = NULL;
    obj_dependency_info_t     curr_obj_depInfo[MAX_DEPCOUNT_PER_OBJECT] = {0};
    int curr_obj_depNum = 0;
    int curr_obj_instNum = 0;

    /* Dependent Object */
    obj_info_t               *next_obj_info = NULL;
    obj_param_info_t         *next_obj_paraminfo = NULL;
    exact_indexvalues_set_t  *next_obj_indexset = NULL;
    parsed_param_name_t       next_obj_pn;

    /* Current/Dependent Objects assignments */
    curr_obj_info = &(auto_del_objects.obj_info[level]);
    next_obj_info = &(auto_del_objects.obj_info[level+1]);
    curr_obj_paraminfo = &(auto_del_objects.obj_param_info[level]);
    next_obj_paraminfo = &(auto_del_objects.obj_param_info[level+1]);
    curr_obj_indexset = &(auto_del_objects.obj_indexvalues_set[level]);
    next_obj_indexset = &(auto_del_objects.obj_indexvalues_set[level+1]);

    curr_obj_instNum  = curr_obj_indexset->inst_num;

    /* Initialize failed_level variable - once at recursion start */
    if (level == 0)
    {
        failed_level = -1;
    }

    RECURSLEVEL_DBG("=====  Run autoDeleteObject (L%d) for '%s'  =====",
                    level, curr_obj_info->objName);

    /* Check that Current Object has instances */
    if (curr_obj_instNum == 0)
    {
        RECURSLEVEL_DBG("Object '%s' has no instances to be deleted", curr_obj_info->objName);
        goto ret;
    }

    /* Fetch dependencies between Objects present in DB */
    if ((db_obj_depNum = ep_common_get_objdep_info(&db_objdepInfo)) < 1)
    {
        if (db_obj_depNum < 0)
            RECURSLEVEL_WARN("Warning: failed to read DB dependencies between Objects");
        else /* db_obj_depNum = 0 */
            RECURSLEVEL_DBG("There are no DB dependencies between Objects");

        /* No error */
        goto delete_curr_obj_instances;
    }

    /* Collect 'autoDelete' dependencies for current Object */
    for (i = 0; i < db_obj_depNum; i++, db_objdepInfo++)
    {
        if (strcmp(db_objdepInfo->parentObjName, curr_obj_info->objName) ||
            db_objdepInfo->objDepClass != OBJ_DEP_AUTO_DELETE)
        {
            /* Object name OR dependency type mismatch */
            continue;
        }

        /* Check for the Object dependencies limit */
        if (curr_obj_depNum >= MAX_DEPCOUNT_PER_OBJECT)
        {
            RECURSLEVEL_WARN("Warning: Object '%s' has more than limit (%d) autoDelete dependencies",
                             curr_obj_info->objName, MAX_DEPCOUNT_PER_OBJECT);
            RECURSLEVEL_WARN("(Continue with first %d dependencies)", MAX_DEPCOUNT_PER_OBJECT);
            break;
        }

        memcpy(&curr_obj_depInfo[curr_obj_depNum], db_objdepInfo, sizeof(obj_dependency_info_t));
        curr_obj_depNum++;
    }

    /* Check that 'autoDelete' DB dependencies exist for current Object */
    if (curr_obj_depNum == 0)
    {
        RECURSLEVEL_DBG("Object '%s' has no autoDelete dependencies", curr_obj_info->objName);

        /* No error */
        goto delete_curr_obj_instances;
    }

    /* Check for the Object dependencies depth limit */
    if (level >= MAX_DEPDEPTH_PER_OBJECT)
    {
        RECURSLEVEL_DBG("Object '%s' is requested at max autoDelete dependency depth (>=%d)",
                        curr_obj_info->objName, MAX_DEPDEPTH_PER_OBJECT);
        RECURSLEVEL_DBG("(Only its instances will be deleted)");
        RECURSLEVEL_WARN("(And its (%d) dependent object instances will not be deleted)",
                          curr_obj_depNum);

        /* No error */
        goto delete_curr_obj_instances;
    }

    RECURSLEVEL_DBG("Object '%s' has %d instance(s) x %d autoDelete dependency(ies)",
                    curr_obj_info->objName, curr_obj_instNum, curr_obj_depNum);

    /*
     * Go over current Object dependencies and per each dependency - go over
     *  current Object instances:
     *
     *  for the specified pair (current Object dependency x instance)
     *  - fetch dependent Objects instances (index values in DB rows)
     *  - go up to next recursion level (even with 0 dependent Object instances)
     */
    for (i = 0; i < curr_obj_depNum; i++)
    {
        RECURSLEVEL_DBG("==> Dependency [L%d, %d of %d] %s: %s%s ---> %s%s",
            level, i + 1, curr_obj_depNum,
            objdepclass2string(curr_obj_depInfo[i].objDepClass),
            curr_obj_depInfo[i].parentObjName, curr_obj_depInfo[i].parentParamName,
            curr_obj_depInfo[i].childObjName, curr_obj_depInfo[i].childParamName);

        /*
         * First validate the dependency:
         *  - check Current Object has dependency parameter in ObjParamInfo
         *  - check Dependent Object
         *     - parse the Object name
         *     - fetch its ObjInfo/ObjParamInfo
         *     - does it have dependency parameter in ObjParamInfo
         */

        /* Check Current Object has dependency parameter in ObjParamInfo */
        param_match = FALSE;
        for (j = 0; j < curr_obj_paraminfo->param_num; j++)
        {
            if (!strcmp(curr_obj_depInfo[i].parentParamName,
                        curr_obj_paraminfo->param[j].paramName))
            {
                param_match = TRUE;
                break;
            }
        }
        if (!param_match)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(current Object '%s' mismatches dependency parameter '%s')",
                curr_obj_info->objName, curr_obj_depInfo[i].parentParamName);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* Check Dependent Object - parse the Object name */
        memset(&next_obj_pn, 0, sizeof(parsed_param_name_t));
        status = parse_param_name(curr_obj_depInfo[i].childObjName, &next_obj_pn);
        if (status != EPS_OK)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(failed to parse dependent Object name '%s', status = %d)",
                curr_obj_depInfo[i].childObjName, status);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* Check Dependent Object - fetch its ObjInfo */
        memset(next_obj_info, 0, sizeof(obj_info_t));
        obj_num = 0;
        status = w_get_obj_info(wd, &next_obj_pn, 1, 0, next_obj_info, 1, &obj_num);
        if (status != EPS_OK)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(failed to get ObjInfo for dependent Object '%s', status = %d)",
                curr_obj_depInfo[i].childObjName, status);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* TODO objnum - must be equal to 1 (check it or not ?) */

        /* Check Dependent Object - if it's writable */
        if (!next_obj_info->writable)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(dependent Object '%s' is not writable)",
                curr_obj_depInfo[i].childObjName);

            /* No error - continue (i) loop with other dependencies */
            /* TODO Or it is an error ..? */
            continue;
        }

        /* Check Dependent Object - fetch its ObjParamInfo */
        memset(next_obj_paraminfo, 0, sizeof(obj_param_info_t));
        status = w_get_param_info(wd, &next_obj_pn, next_obj_info, 0,
                        &(next_obj_paraminfo->param[0]),
                        &(next_obj_paraminfo->param_num), NULL);
        if (status != EPS_OK)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(failed to get ObjParamInfo for dependent Object '%s', status = %d)",
                curr_obj_depInfo[i].childObjName, status);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        /* Check Dependent Object has dependency parameter in ObjParamInfo */
        param_match = FALSE;
        for (j = 0; j < next_obj_paraminfo->param_num; j++)
        {
            if (!strcmp(curr_obj_depInfo[i].childParamName,
                        next_obj_paraminfo->param[j].paramName))
            {
                param_match = TRUE;
                break;
            }
        }
        if (!param_match)
        {
            RECURSLEVEL_WARN("==> Dependency [L%d, %d of %d] is invalid - ignored",
                level, i + 1, curr_obj_depNum);
            RECURSLEVEL_WARN("(dependent Object '%s' mismatches dependency parameter '%s')",
                curr_obj_depInfo[i].childObjName, curr_obj_depInfo[i].childParamName);

            /* No error - continue (i) loop with other dependencies */
            continue;
        }

        RECURSLEVEL_DBG("==> Dependency validated OK - looking for Dependent Object '%s' instances",
                        curr_obj_depInfo[i].childObjName);

        status = EPS_OK;

        /*
         * Dependency was successfully validated - start second
         *  (j) loop - over Current Object instances inside
         *  (i) loop - over Current Object dependencies
         */
        for (j = 0; j < curr_obj_instNum; j++)
        {
            /* { i, j } - pair of Current Object Dependency and Instance */

            /* Save current Object index names */
            memset(idx_params, 0, sizeof(unsigned long int) * sizeof(MAX_INDECES_PER_OBJECT));
            idx_params_num = 0;
            get_index_param_names(&(curr_obj_paraminfo->param[0]),
                curr_obj_paraminfo->param_num, idx_params, &idx_params_num);

            memset(where_cond, 0, sizeof(where_cond));
            snprintf(where_cond, sizeof(where_cond), "WHERE 1 AND [%s] = (SELECT [%s] FROM %s WHERE 1",
                curr_obj_depInfo[i].childParamName,
                curr_obj_depInfo[i].parentParamName,
                curr_obj_info->objValuesTblName);

            for (k = 0; k < idx_params_num; k++)
            {
                snprintf(where_cond + strlen(where_cond), sizeof(where_cond), " AND [%s] = %d",
                    idx_params[k], curr_obj_indexset->indexvalues[j][k]);
            }
            strcat_safe(where_cond, ")", sizeof(where_cond));

            /* Save next (dependent) Object index names */
            memset(idx_params, 0, sizeof(unsigned long int) * sizeof(MAX_INDECES_PER_OBJECT));
            idx_params_num = 0;
            get_index_param_names(&(next_obj_paraminfo->param[0]),
                next_obj_paraminfo->param_num, idx_params, &idx_params_num);

            /*
             * Fetch DB instances of the dependent Object with prepared where_cond
             * In case of failure - stop and return the error
             */
            status = ep_db_get_tbl_row_indexes(conn, next_obj_info->objValuesTblName,
                           next_obj_pn.index_num, idx_params, &(next_obj_pn.indices[0]),
                           where_cond, next_obj_indexset);
            if (status != EPS_OK)
            {
                RECURSLEVEL_ERROR("==> Failed to fetch dependent Object '%s' instances in DB",
                    next_obj_info->objName);
                goto ret;
            }

            /* Print the fetched DB instances of dependent Object */
            //print_delobj_inst_indexvalues(level+1);

            RECURSLEVEL_DBG("===>>> Go to next level (L%d -> L%d) with Object '%s'",
                level, level+1, next_obj_info->objName);

            status = w_delobj_autodelete_recursively(wd, message, level+1);
            if (status != EPS_OK)
            {
                if (failed_level == -1)
                {
                    /* This is the time to save the failed_level */
                    failed_level = level+1;

                    RECURSLEVEL_ERROR("==> Recursion failed between (L%d -> L%d): Obj %s -> %s",
                        level, level+1, curr_obj_info->objName, next_obj_info->objName);
                }
                else
                {
                    RECURSLEVEL_ERROR("==> Recursion failure raised upper (L%d -> L%d): Obj %s -> %s",
                        level, level+1, curr_obj_info->objName, next_obj_info->objName);
                }

                goto ret;
            }

            /*
             * ---------------------------------------------------------------
             * Current Object's (i)-th Dependency was successfully processed
             *  for (j)-th Instance - go to the next Object Instance with the
             *  same dependency
             * ---------------------------------------------------------------
             */

        } /* End of for ( over Current Object instances ) */

        /*
         * --------------------------------------------------------------
         * Current Object's (i)-th Dependency was successfully processed
         *  for all Objects Instances - go to the next Dependency
         * --------------------------------------------------------------
         */
    } /* End of for ( over Current Object dependencies ) */

delete_curr_obj_instances:

    if (level == 0)
    {
        /* We do not delete LEVEL=0 Object instances here - we do it in parent caller */
        goto ret;
    }

    /* Save current Object index names */
    memset(idx_params, 0, sizeof(unsigned long int) * sizeof(MAX_INDECES_PER_OBJECT));
    idx_params_num = 0;
    get_index_param_names(&(curr_obj_paraminfo->param[0]),
        curr_obj_paraminfo->param_num, idx_params, &idx_params_num);

    RECURSLEVEL_DBG("==> Deleting object '%s' instances (%d):",
        curr_obj_info->objName, curr_obj_instNum);
    print_delobj_inst_indexvalues(level);

    /*
     * --------------------------------------------------------------
     * Running DelObject for the Current Object instances
     * --------------------------------------------------------------
     */

    /* Determine style of delobj operation. If DB type is not "running DB",
       the operation will be performed only in the DB and not in the backend */

    /* Style OP_STYLE_SHELL_SCRIPT is currently supported for
     *  SET operation per distinct Object parameter(s) only */
    if ((curr_obj_info->delObjStyle == OP_STYLE_DB) ||
        (curr_obj_info->delObjStyle == OP_STYLE_SCRIPT) ||
        (curr_obj_info->delObjStyle == OP_STYLE_BACKEND))
    {
        if (message->header.mmxDbType == MMXDBTYPE_RUNNING)
            delStyle = curr_obj_info->delObjStyle;
        else
            delStyle = OP_STYLE_DB;
    }
    else
    {
        status = EPS_NOT_IMPLEMENTED;
        RECURSLEVEL_ERROR("DelObject style `%s' currently not supported",
                           operstyle2string(curr_obj_info->delObjStyle));
        goto ret;
    }

    switch (delStyle)
    {
        case OP_STYLE_DB:
            status = w_delobject_db(wd, conn,
                       &(auto_del_objects.obj_info[level]), idx_params,
                       &(auto_del_objects.obj_indexvalues_set[level]), &delStatus);
            break;
        case OP_STYLE_SCRIPT:
            status = w_delobject_script(wd, conn,
                       &(auto_del_objects.obj_info[level]), idx_params,
                       &(auto_del_objects.obj_indexvalues_set[level]), &delStatus);
            break;
        case OP_STYLE_BACKEND:
            status = w_delobject_backend(wd, conn,
                       &(auto_del_objects.obj_info[level]), idx_params,
                       &(auto_del_objects.obj_indexvalues_set[level]), &delStatus);
            break;
    }
    if (status != EPS_OK)
    {
        RECURSLEVEL_ERROR("DelObject failed for Object %s (status %d)",
            curr_obj_info->objName, status);
        goto ret;
    }

    /*
     * --------------------------------------------------------------
     *  DelObject for the Current Object instances was successful
     *  Make some additional actions (if needed)
     * --------------------------------------------------------------
     */
    if (message->header.mmxDbType == MMXDBTYPE_CANDIDATE)
    {
        w_save_file(get_db_cand_path((char*)filebuf, FILENAME_BUF_LEN));
    }

    if (delStatus) /* Perform restart of backend if needed */
    {
        if ((i = ep_common_get_beinfo_index(curr_obj_info->backEndName)) >= 0)
        {
            restart_be[i] = TRUE;
            w_restart_backends(MSGTYPE_DELOBJECT, restart_be);
        }
    }

ret:
    if (status == EPS_OK || level == 0)
    {
        RECURSLEVEL_DBG("=====  Done autoDeleteObject (L%d) for '%s' - status %d  =====",
            level, curr_obj_info->objName, status);
    }

    return status;
}


/* Handler of DelObject request.
  Object instances are going to be deleted are specified by complete path name,
   i.e. all indexes of the instance are stated. For ex: Device.IP.Interface.3.
   The last index (the Instance number) can be specified as
   exact value, range or *. All othe indeces must be exact values.
  */
#define MAX_DEPENDED_OBJ_NUM 10
static ep_stat_t w_handle_delobject(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;
    int i, j, obj_num, param_num, req_size;
    int delStatus = 0, total_delStatus = 0, last_failure = 0;
    int delStyle = 0;
    int success_cnt = 0;
    int restart_be[MAX_BACKEND_NUM];
    BOOL write_lock_received = FALSE;
    obj_info_t obj_info[1];
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    parsed_param_name_t pn;

    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_params_num = 0;

    sqlite3 *conn = NULL;

    ep_message_t answer = {{0}};

    memcpy(&answer.header, &message->header, sizeof(answer.header));
    answer.header.respFlag = 1;
    answer.header.msgType = MSGTYPE_DELOBJECT_RESP;

    memset((char *)restart_be, 0, sizeof(restart_be));

    if (operAllowedForDbType(OP_DELOBJ, message->header.mmxDbType) != TRUE)
        GOTO_RET_WITH_ERROR(EPS_INVALID_DB_TYPE,
           "Operation DELOBJ is not permitted for db type %d", message->header.mmxDbType);

    if ((status = w_init_mmxdb_handles(wd, message->header.mmxDbType,
                                       message->header.msgType)) != EPS_OK)
        goto ret;

    conn = wd->main_conn;

    /* ---- DelObject is write operation. EP write-lock must be received ---- */
    if (ep_common_get_write_lock(MSGTYPE_DELOBJECT, message->header.txaId, message->header.callerId) == EPS_OK)
        write_lock_received = TRUE;
    else
        GOTO_RET_WITH_ERROR(EPS_RESOURCE_NOT_FREE, "Could not receive write lock for DelObject operation");

    /* For each request parameter */
    req_size = message->body.delObject.arraySize;
    for (i = 0; i < req_size; i++)
    {
        if ((status = parse_param_name(message->body.delObject.objects[i], &pn)) != EPS_OK)
            GOTO_RET_WITH_ERROR(status, "Could not parse object name: %s", message->body.delObject.objects[i]);

        if (w_get_obj_info(wd, &pn, 1, 0, obj_info, 1, &obj_num) != EPS_OK)
            GOTO_RET_WITH_ERROR(EPS_INVALID_ARGUMENT, "Could not get object info for %s (%d)", pn.obj_name, pn.last_token_type);

        if (!obj_info[0].writable)
            GOTO_RET_WITH_ERROR(EPS_NO_PERMISSION, "Instance cannot be deleted. Object %s is not writable", pn.obj_name);

        if (!objWriteAllowed(&obj_info[0], message->header.callerId))
            GOTO_RET_WITH_ERROR(EPS_NO_PERMISSION, "Obj instance cannot be deleted by caller %d", message->header.callerId);

        if (w_get_param_info(wd, &pn, &obj_info[0], 0, param_info, &param_num, NULL) != EPS_OK)
            GOTO_RET_WITH_ERROR(EPS_INVALID_ARGUMENT, "Could not retrieve param info for object %s", pn.obj_name);

        if (w_verify_objname_for_del(&pn, param_info, param_num) != EPS_OK)
            GOTO_RET_WITH_ERROR(EPS_INVALID_ARGUMENT, "Wrong instance name %s", message->body.delObject.objects[i]);


        memset(&auto_del_objects, 0, sizeof(auto_del_objects));
        memcpy(&(auto_del_objects.obj_info[0]), &obj_info[0], sizeof(obj_info_t));
        memcpy(&(auto_del_objects.obj_param_info[0].param[0]), &param_info, MAX_PARAMS_PER_OBJECT * sizeof(param_info_t));
        auto_del_objects.obj_param_info[0].param_num = param_num;

        /* Save index names */
        get_index_param_names(param_info, param_num, idx_params, &idx_params_num);

        /* Fetch Object instances (only index values) from the DB */
        status = ep_db_get_tbl_row_indexes(conn, obj_info[0].objValuesTblName, pn.index_num,
                       idx_params, &(pn.indices[0]), NULL, &(auto_del_objects.obj_indexvalues_set[0]));
        if (status != EPS_OK)
        {
            GOTO_RET_WITH_ERROR(status, "Could not retrieve Object %s instances (status %d)",
                                pn.obj_name, status);
        }

        if (auto_del_objects.obj_indexvalues_set[0].inst_num == 0)
        {
            //DBG("No instances to be deleted for Object %s - continue", pn.obj_name);
            continue;
        }

        /* Delete dependent Object instances (if any) */
        DBG("======== Starting autoDelete recursion for Object '%s' ========", obj_info[0].objName);
        status = w_delobj_autodelete_recursively(wd, message, /* dependency level = */ 0);
        if (status != EPS_OK)
        {
            GOTO_RET_WITH_ERROR(status, "Failed to run autoDelete recursion for Object %s (status %d)",
                                pn.obj_name, status);
        }
        DBG("======== Deleting Object '%s' instances (%d) ========", obj_info[0].objName,
            auto_del_objects.obj_indexvalues_set[0].inst_num);

        /* Determine style of delobj operation. If DB type is not "running DB",
           the operation will be performed only in the DB and not in the backend */

        /* Style OP_STYLE_SHELL_SCRIPT is currently supported for
         *  SET operation per distinct Object parameter(s) only */
        if ((obj_info[0].delObjStyle == OP_STYLE_DB) ||
            (obj_info[0].delObjStyle == OP_STYLE_SCRIPT) ||
            (obj_info[0].delObjStyle == OP_STYLE_BACKEND))
        {
            if (message->header.mmxDbType == MMXDBTYPE_RUNNING)
                delStyle = obj_info[0].delObjStyle;
            else
                delStyle = OP_STYLE_DB;
        }
        else
            GOTO_RET_WITH_ERROR(EPS_NOT_IMPLEMENTED,"DelObject style `%s' currently not supported",
                                operstyle2string(obj_info[0].delObjStyle));

        /* Print found in DB Object instances (only index values) */
        DBG("Object '%s' instances:", obj_info[0].objName);
        print_delobj_inst_indexvalues(/* dependency level = */ 0);

        switch (delStyle)
        {
            case OP_STYLE_DB:
                status = w_delobject_db(wd, conn,
                           &(auto_del_objects.obj_info[0]), idx_params,
                           &(auto_del_objects.obj_indexvalues_set[0]), &delStatus);

                break;
            case OP_STYLE_SCRIPT:
                status = w_delobject_script(wd, conn,
                           &(auto_del_objects.obj_info[0]), idx_params,
                           &(auto_del_objects.obj_indexvalues_set[0]), &delStatus);

                break;
            case OP_STYLE_BACKEND:
                status = w_delobject_backend(wd, conn,
                           &(auto_del_objects.obj_info[0]), idx_params,
                           &(auto_del_objects.obj_indexvalues_set[0]), &delStatus);

                break;
        }

        if (status == EPS_OK)
        {
            success_cnt++;
            if (delStatus != 0)
            {
                total_delStatus = 1;
                if ((j = ep_common_get_beinfo_index(obj_info->backEndName)) >= 0)
                    restart_be[j] = TRUE;
            }
        }
        else
        {
            /* If only one object is requested, we stop here with error*/
            if (req_size == 1)
                GOTO_RET_WITH_ERROR(status, "Failed to delete one requested obj instance (%d", status);
            else
                last_failure = status;
        }
    }

    if (success_cnt == 0)
        status = last_failure;

    if ((success_cnt > 0) && (message->header.mmxDbType == MMXDBTYPE_CANDIDATE))
    {
        char buf[FILENAME_BUF_LEN] = {0};
        w_save_file(get_db_cand_path((char*)buf, FILENAME_BUF_LEN));
    }

ret:
    /* ------- Release EP write operation lock -------*/
    if (write_lock_received == TRUE)
        ep_common_finalize_write_lock(message->header.txaId, message->header.callerId, FALSE);

    answer.header.respCode = w_status2cwmp_error(status);
    if (status == EPS_OK)
    {
        answer.body.delObjectResponse.status = total_delStatus;
    }
    w_send_answer(wd, &answer);

    /* Perform restart of backend if needed */
    if (total_delStatus != 0)
        w_restart_backends(MSGTYPE_DELOBJECT, restart_be);

    return status;
}


/* -------------------------------------------------------------------*/
/* ------------------DiscoverConfig ("getall") procedures   ----------*/
/* -------------------------------------------------------------------*/
typedef struct getall_keys_row_s {
    int row_index;
    int keys_num;
    int cfg_owner;
    int create_owner;
    int db_keys[MAX_INDECES_PER_OBJECT];
    char be_keys[MAX_INDECES_PER_OBJECT][MMXBA_MAX_STR_LEN];
} getall_keys_row_t;

typedef struct getall_keys_s {
    int rows_num;
    getall_keys_row_t rows[MAX_INSTANCES_PER_OBJECT];
} getall_keys_t;

typedef struct getall_keys_ref_s {
    int rows_num;
    getall_keys_row_t *rows_ptr[MAX_INSTANCES_PER_OBJECT];
} getall_keys_ref_t;

/* Compare key rows by backend key values */
static int compare_getall_rows(const void *a, const void *b)
{
    const getall_keys_row_t *ra = (const getall_keys_row_t *)a;
    const getall_keys_row_t *rb = (const getall_keys_row_t *)b;

    int res = 0;
    for (int i = 0; i < min(ra->keys_num, rb->keys_num) && !res; i++)
    {
        res = strcmp(ra->be_keys[i], rb->be_keys[i]);
    }
    return res;
}

/* Compare key rows by row indexes when the array elements are pointers
 * to getall_keys_row_t structure */
static int compare_getall_ref_row_indexes(const void *a, const void *b)
{
    const getall_keys_row_t **ra = (const getall_keys_row_t **)a;
    const getall_keys_row_t **rb = (const getall_keys_row_t **)b;

    int res = 0;

    if ((*ra)->row_index < (*rb)->row_index)
        res = -1;
    else if ((*ra)->row_index > (*rb)->row_index)
        res = 1;

    return res;
}

#if 0
/* Currently not used function */

/* Compare key rows by row indexes */
static int compare_getall_row_indexes(const void *a, const void *b)
{
    const getall_keys_row_t *ra = (const getall_keys_row_t *)a;
    const getall_keys_row_t *rb = (const getall_keys_row_t *)b;

    int res = 0;

    if (ra->row_index < rb->row_index)
       res = -1;
    else if (ra->row_index > rb->row_index)
       res = 1;

    return res;
}
#endif

/* Helper functions for debugging purposes */
/* Print to buffer one row of getall keys.
   Buffer size should be no less than 512 bytes*/
void print_getall_one_row(getall_keys_row_t *row_keys, char *p_buf, int buf_size)
{
    int j;
    char db_keys[32],  *p_db_keys = (char *)&db_keys;
    char be_keys[374], *p_be_keys = (char *)&be_keys;

    memset(p_db_keys, 0, sizeof(db_keys));
    memset(p_be_keys, 0, sizeof(be_keys));
    memset(p_buf, 0, buf_size);

    if (row_keys->keys_num > 0)
    {
        p_db_keys = (char *)&db_keys;
        p_be_keys = (char *)&be_keys;

        for (j = 0; j < row_keys->keys_num; j++)
        {
            sprintf(p_db_keys, "%d, ", row_keys->db_keys[j]);
            p_db_keys += strlen(p_db_keys);
            sprintf(p_be_keys, "%s, ", row_keys->be_keys[j]);
            p_be_keys += strlen(p_be_keys);
        }

        LAST_CHAR(db_keys) = '\0'; LAST_CHAR(db_keys) = '\0'; // remove the last comma and space
        LAST_CHAR(be_keys) = '\0'; LAST_CHAR(be_keys) = '\0'; // remove the last comma and space
        sprintf(p_buf, "num of keys %d; owners (%d, %d); db keys: %s; be keys: %s; ",
                row_keys->keys_num, row_keys->create_owner, row_keys->cfg_owner, db_keys, be_keys);
    }

    return;
}

#if 0
void print_getall_one_row_keys(getall_keys_t *getallkeys, int row_num,
                               char *p_buf, int buf_size)
{
    int j;
    char db_keys[32],  *p_db_keys = (char *)&db_keys;
    char be_keys[374], *p_be_keys = (char *)&be_keys;

    memset(p_db_keys, 0, sizeof(db_keys));
    memset(p_be_keys, 0, sizeof(be_keys));
    memset(p_buf, 0, buf_size);

    if (row_num < getallkeys->rows_num)
    {
        if (getallkeys->rows[row_num].keys_num > 0)
        {
            p_db_keys = (char *)&db_keys;
            p_be_keys = (char *)&be_keys;

            for (j = 0; j < getallkeys->rows[row_num].keys_num; j++)
            {
                sprintf(p_db_keys, "%d, ", getallkeys->rows[row_num].db_keys[j]);
                p_db_keys += strlen(p_db_keys);
                sprintf(p_be_keys, "%s, ", getallkeys->rows[row_num].be_keys[j]);
                p_be_keys += strlen(p_be_keys);
            }

            LAST_CHAR(db_keys) = '\0'; LAST_CHAR(db_keys) = '\0'; // remove the last comma and space
            LAST_CHAR(be_keys) = '\0'; LAST_CHAR(be_keys) = '\0'; // remove the last comma and space
            sprintf(p_buf, "num of keys %d; owners (%d, %d); db keys: %s; be keys: %s; ",
                    getallkeys->rows[row_num].keys_num,
                    getallkeys->rows[row_num].create_owner,
                    getallkeys->rows[row_num].cfg_owner, db_keys, be_keys);
        }
    }
    return;
}
#endif

/* Print to log all rows in getall_keys_t structure */
void print_getall_keys(getall_keys_t *getallkeys)
{
    int i;
    char buf[512], *p_buf = (char *)&buf;

    for (i = 0; i < getallkeys->rows_num; i++)
    {
        if (getallkeys->rows[i].keys_num > 0)
        {
            //print_getall_one_row_keys(getallkeys, i, p_buf, sizeof(buf));
            print_getall_one_row(&(getallkeys->rows[i]), p_buf, sizeof(buf));
            DBG("Row (%d): %s", i, p_buf);
        }
    }
}

static ep_stat_t w_form_backend_request_getall(worker_data_t * wd,
                                               char *xml_req, size_t xml_req_size,
                                               parsed_backend_method_t *parsed_request)
{
    int i = 0;
    int stat;
    mmxba_request_t req = { .op_type = MMXBA_OP_TYPE_GETALL };

    /* Update backend request counter and set seq num for the request*/
    if ((wd->be_req_cnt >= EP_MAX_BE_REQ_SEQNUM) || (wd->be_req_cnt < 0))
        wd->be_req_cnt = 0;
    else
        wd->be_req_cnt++;

    req.opSeqNum = wd->be_req_cnt + (wd->self_w_num * (EP_MAX_BE_REQ_SEQNUM + 1));

    strcpy_safe(req.beObjName, parsed_request->beObjName, sizeof(req.beObjName));

    req.getAll.beKeyNamesNum = parsed_request->subst_val_num;
    for (i = 0; i < parsed_request->subst_val_num; i++)
    {
        strcpy_safe(req.getAll.beKeyNames[i], parsed_request->subst_val[i].backend_key_name, sizeof(req.getAll.beKeyNames));
    }

    if ((stat = mmx_backapi_request_build(&req, xml_req, xml_req_size)) != MMXBA_OK)
    {
        ERROR("Could not build mmx backapi message: %d", stat);
        return EPS_INVALID_FORMAT;
    }

    return EPS_OK;
}

/*
 * Builds SELECT SQL request that retrieves values for substitution
 */
static ep_stat_t w_form_query_get_all_keys(worker_data_t *wd, obj_info_t *obj_info,
        char *query, size_t query_size, char **idx_params, int idx_params_num,
        parsed_backend_method_t *parsed_backend_str, parsed_param_name_t *pn)
{
    int i, j, n, min_idx_num;
    obj_info_t s_obj_info;
    char where[EP_SQL_REQUEST_BUF_SIZE] = " WHERE 1 ";

    strcpy_safe(query, "SELECT ", query_size);

    for (i = 0; i < idx_params_num; i++)
    {
        strcat_safe(query, "t.[", query_size);
        strcat_safe(query, idx_params[i], query_size);
        strcat_safe(query, "],", query_size);
    }

    for (i = 0; i < parsed_backend_str->subst_val_num; i++)
    {
        if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name)
            sprintf(query+strlen(query), "t%d.", i);
        else
            strcat_safe(query, "t.", query_size);

        strcat_safe(query, "[", query_size);
        strcat_safe(query, parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name, query_size);
        strcat_safe(query, "], ", query_size);
    }

    /* Add the "service" params to the query */
    strcat_safe(query, "t.", query_size);
    strcat_safe(query, MMX_CFGOWNER_DBCOLNAME, query_size);
    strcat_safe(query, ", t.", query_size);
    strcat_safe(query, MMX_CREATEOWNER_DBCOLNAME, query_size);

    //LAST_CHAR(query) = '\0'; /* remove last comma */

    strcat_safe(query, " FROM ", query_size);
    strcat_safe(query, obj_info->objValuesTblName, query_size);
    strcat_safe(query, " AS t ", query_size);

    for (i = 0; i < parsed_backend_str->subst_val_num; i++)
    {
        if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name)
        {
            w_get_subst_obj_info(wd, &parsed_backend_str->subst_val[i], &s_obj_info);
            n = w_num_of_obj_indeces(s_obj_info.objName);
            min_idx_num = (n < idx_params_num) ? n : idx_params_num;

            if (min_idx_num > 0)
            {
                strcat_safe(query, " INNER JOIN ", query_size);
                strcat_safe(query, s_obj_info.objValuesTblName, query_size);
                sprintf(query+strlen(query), " AS t%d ON (", i);
                // TODO idx_params of subst table t0
                for (j = 0; j < min_idx_num; j ++)
                {
                    // TODO: support works with nested table
                    strcat_safe(query, "t.[", query_size);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "]=", query_size);
                    sprintf(query+strlen(query), "t%d.[", i);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "] AND ", query_size);

                    if (pn->indices[j].exact_val.num)
                    {
                        strcat_safe(where, " AND ", sizeof(where));
                        sprintf(where+strlen(where), "t%d.[", i);
                        strcat_safe(where, idx_params[j], sizeof(where));
                        strcat_safe(where, "]=", sizeof(where));
                        sprintf(where+strlen(where), "%d", pn->indices[j].exact_val.num);
                    }
                }
                query[strlen(query)-5] = '\0'; /* Remove last " AND " */
                strcat_safe(query, ")", query_size);
            }
        }
    }
    if (pn->index_set_num) {
        strcat_safe(query, where, query_size);
    }

    //DBG("Prepared query (len=%d) :\n   %s", strlen(query), query);
    return EPS_OK;
}

static ep_stat_t w_fill_dbkeys(worker_data_t *wd,
        obj_info_t *obj_info, parsed_backend_method_t *parsed_backend_string,
        int idx_params_num, char **idx_params, sqlite3 *conn,
        getall_keys_t *dbkeys,parsed_param_name_t *pn)
{
    ep_stat_t status = EPS_OK;
    int res, i;
    char buf[EP_SQL_REQUEST_BUF_SIZE];
    sqlite3_stmt *stmt = NULL;

    memset(dbkeys, 0, sizeof(getall_keys_t));

    if ((status = w_form_query_get_all_keys(wd, obj_info, buf, sizeof(buf),
            idx_params, idx_params_num, parsed_backend_string, pn)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not build query get all keys");

    DBG("Query for getall DB keys: %s", buf);

    if (sqlite3_prepare_v2(conn, buf, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s", sqlite3_errmsg(conn));

    while (TRUE)
    {
        res = sqlite3_step(stmt);

        if (res == SQLITE_ROW)
        {
            if (dbkeys->rows_num >= MAX_INSTANCES_PER_OBJECT)
                GOTO_RET_WITH_ERROR(EPS_NO_MORE_ROOM, "Too much instances of obj  %s", obj_info->objName);

            /* save values of all DB keys (indeces), backend keys and service
               parameters (config and create owners) of the obj instance    */
            dbkeys->rows[dbkeys->rows_num].row_index = dbkeys->rows_num;
            for (i = 0; i < idx_params_num; i++)
            {
                dbkeys->rows[dbkeys->rows_num].db_keys[i] = sqlite3_column_int(stmt, i);
            }

            for (; i < sqlite3_column_count(stmt) - 2; i++)
            {
                if (sqlite3_column_text(stmt, i) != NULL)
                {
                    strcat_safe(dbkeys->rows[dbkeys->rows_num].be_keys[dbkeys->rows[dbkeys->rows_num].keys_num++],
                               (const char *)sqlite3_column_text(stmt, i), MMXBA_MAX_STR_LEN);
                }
                else
                {
                    INFO("Empty value of key parameter i=%d", i);
                }
            }

            /* If the function is used for getting DB instances only
              (without BE keys), we set here the number of DB indeces */
            if (dbkeys->rows[dbkeys->rows_num].keys_num == 0)
                dbkeys->rows[dbkeys->rows_num].keys_num = idx_params_num;

            /* Two last parameters are config and create owners of the instance */
            dbkeys->rows[dbkeys->rows_num].cfg_owner = sqlite3_column_int(stmt, i);
            i++;
            dbkeys->rows[dbkeys->rows_num].create_owner = sqlite3_column_int(stmt, i);

            dbkeys->rows_num++;
        }
        else if (res == SQLITE_DONE)
            break;
        else
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(wd->mdb_conn));
    }

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

static ep_stat_t w_fill_bekeys_backend(worker_data_t *wd, char objects[][MMXBA_MAX_STR_LEN],
                                       int objNum, getall_keys_t *bekeys)
{
    ep_stat_t status = EPS_OK;
    int i, j;
    char *strtokctx, *token;

    memset(bekeys, 0, sizeof(getall_keys_t));

    for (i = 0; i < objNum; i++)
    {
        bekeys->rows[i].row_index = i;
        j = 0;
        for (token = strtok_r(objects[i], ",", &strtokctx); token;
                token = strtok_r(NULL, ",", &strtokctx))
        {
            strcpy_safe(bekeys->rows[i].be_keys[j++], token, MMXBA_MAX_STR_LEN);
            bekeys->rows[i].keys_num++;
        }
        bekeys->rows_num++;
    }

    return status;
}

/* TODO - Think about support for "common case" of substitution objects.
 *        Currently subst objects can be:
 *           - the processed object itself or
 *           - its previous multi-instance object.  */
static ep_stat_t w_form_query_getall_insert_new(worker_data_t *wd, obj_info_t *obj_info,
        char *query, size_t query_size, char **idx_params, int idx_params_num,
        parsed_backend_method_t *parsed_backend_str, getall_keys_row_t *newrow)
{
    ep_stat_t status = EPS_OK;
    int i, j, tmp_cnt = 0;
    int table_counter = 0;
    char *selectFrom = NULL;
    char *selectFromTempObj = NULL;
    char *selectFromTempVal = NULL;
    BOOL subst_other_obj = FALSE;
    BOOL is_first_element = FALSE;
    obj_info_t prevobj_info;
    int backend_index = -1;

    memset(query, 0, query_size);

    /* Debug print */
    DBG("Subst val number = %d, index param num = %d", parsed_backend_str->subst_val_num, idx_params_num);
    /*for (i = 0; i < parsed_backend_str->subst_val_num; i++)
    {
        DBG("Parsed method leaf name: %s, backend key name: %s",
             parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name,
             parsed_backend_str->subst_val[i].backend_key_name );
    }*/

    for (i = 0; i < parsed_backend_str->subst_val_num; i++)
    {
        if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name != NULL)
        {
            subst_other_obj = TRUE;
            break;
        }
    }

    if (subst_other_obj || idx_params_num > 1)
    {
        if(idx_params_num > 1)
        {
            selectFromTempObj = obj_info->objName;
            for( table_counter = 1; table_counter < idx_params_num; table_counter++ )
            {
                status = w_get_prev_obj_info(wd, selectFromTempObj, &prevobj_info);
                if(status == EPS_OK)
                {
                    selectFromTempObj = prevobj_info.objName;
		    selectFrom = prevobj_info.objValuesTblName;
                }
                else
                {
                    DBG("Not prevobj info for '%s'", selectFromTempObj);
		    break;
                }
            }
        }
        else
        {
            status = w_get_prev_obj_info(wd, obj_info->objName, &prevobj_info);
            if (status == EPS_OK)
                selectFrom = prevobj_info.objValuesTblName;
        }
    }
    else
    {
        selectFrom = obj_info->objValuesTblName;
    }

    /* Prepare columns/params names for the insert  clause */
    strcpy_safe(query, "INSERT INTO ", query_size);
    strcat_safe(query, obj_info->objValuesTblName, query_size);
    strcat_safe(query, " (", query_size);

    /* Collect names of index parameters */
    for (i = 0; i < idx_params_num; i++)
    {
        strcat_safe(query, "[", query_size);
        strcat_safe(query, idx_params[i], query_size);
        strcat_safe(query, "]", query_size);
        strcat_safe(query, ",", query_size);
    }

    /* Add names of parameters that are backend keys and belong to that object
       to the INSERT clause */
    tmp_cnt = 0;
    for (i = 0; i < parsed_backend_str->subst_val_num; i++)
    {
        if (strcmp(parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name, idx_params[idx_params_num - 1]) == 0)
        {
            backend_index = i;
            continue;
        }

        if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name == NULL)
        {
            strcat_safe(query, "[", query_size);
            strcat_safe(query, parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name,
                        query_size);
            strcat_safe(query, "]", query_size);
            strcat_safe(query, ",", query_size);
            tmp_cnt++;
        }
    }
    if ((tmp_cnt > 0) || (idx_params_num > 0))
        LAST_CHAR(query) = '\0';       /* Remove the last "," */

    /* Prepare values for the inserted parameters */
    strcat_safe(query, ") SELECT ", query_size);

    /* All indices values (except of the last one) is selected from the
       previous object's value table - this table is "t0" */
    for (i = 0; i < idx_params_num-1; i++)
    {
        strcat_safe(query, "t1.[", query_size);
        strcat_safe(query, idx_params[i], query_size);
        strcat_safe(query, "], ", query_size);
    }
    /* The last index is calculated in the object's table. */
    if(backend_index != -1)
    {
        strcat_safe(query, "IFNULL('", query_size);
        strcat_safe(query, newrow->be_keys[backend_index], query_size);
        if (idx_params_num > 1)
            strcat_safe(query, "', MAX(t0.", query_size);
        else
            strcat_safe(query, "', MAX(t1.", query_size);

    strcat_safe(query, "[", query_size);
    strcat_safe(query, idx_params[idx_params_num-1], query_size);
    strcat_safe(query, "])+1) ", query_size);
    }
    else
    {
        /* Add values of params that are the be-keys and belong to that object*/
        for (i = 0; i < newrow->keys_num; i++)
        {
            if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name == NULL)
            {
                strcat_safe(query, ", '", query_size);
                strcat_safe(query, newrow->be_keys[i], query_size);
                strcat_safe(query, "'", query_size);
            }
        }
    }

    strcat_safe(query, " FROM ", query_size);
    strcat_safe(query, selectFrom, query_size);
    if(idx_params_num > 1 )
        snprintf(query + strlen(query),query_size - strlen(query), " AS t%d", idx_params_num - 1);
    else
        strcat_safe(query, " AS t1", query_size);

   if (subst_other_obj || idx_params_num > 1)
   {
        if(idx_params_num > 1)
        {
            for( i = idx_params_num - 1; i > 0; i-- )
            {
                selectFromTempObj =  obj_info->objName;
                selectFromTempVal =  obj_info->objValuesTblName;
                for( table_counter = 1; table_counter < i; table_counter++ )
                {
                    status = w_get_prev_obj_info(wd, selectFromTempObj, &prevobj_info);
                    if (status != EPS_OK)
                    {
                        DBG("Not prevobj info for '%s'", selectFromTempObj);
                        break;
                    }
                    selectFromTempObj = prevobj_info.objName;
                    selectFromTempVal = prevobj_info.objValuesTblName;
                }
                strcat_safe(query, " LEFT JOIN ", query_size);
                strcat_safe(query, selectFromTempVal, query_size);
                snprintf(query + strlen(query),query_size - strlen(query), " AS t%d", i - 1);
                strcat_safe(query, " ON (", query_size);
                for (j = 0, is_first_element = TRUE; j < (idx_params_num - i); j++)
                {
                    if(is_first_element)
                        is_first_element = FALSE;
                    else
                        strcat_safe(query, " AND ", query_size);
                    snprintf(query + strlen(query),query_size - strlen(query), "t%d.", i - 1);
                    strcat_safe(query, idx_params[j], query_size);
                    strcat_safe(query, "=", query_size);
                    snprintf(query + strlen(query),query_size - strlen(query), "t%d.", i);
                    strcat_safe(query, idx_params[j], query_size);
                }
                strcat_safe(query, ")", query_size);
             }

            /*Form WHERE clause containing params that belong to the parent objs*/
            strcat_safe(query, " WHERE 1 ", query_size);
            for (i = 0; i < parsed_backend_str->subst_val_num; i++)
            {
                if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name)
                {
                    strcat_safe(query, " AND ", query_size);
                    // Search table index by obj name
                    selectFromTempObj =  obj_info->objName;
                    selectFromTempVal =  obj_info->objValuesTblName;
                    for (table_counter = 1; table_counter < idx_params_num; table_counter++)
                    {
                        status = w_get_prev_obj_info(wd, selectFromTempObj, &prevobj_info);
                        if (status != EPS_OK)
                        {
                            DBG("Not prevobj info for '%s'", selectFromTempObj);
                            break;
                        }
                        if(!strcmp( prevobj_info.objName, parsed_backend_str->subst_val[i].mmx_subst_val.obj_name))
                        {
                            snprintf(query + strlen(query),query_size - strlen(query), "t%d.[", table_counter);
                            strcat_safe(query, parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name, query_size);
                            strcat_safe(query, "]='", query_size);
                            strcat_safe(query, newrow->be_keys[i], query_size);
                            strcat_safe(query, "'", query_size);
                            break;
                        }
                        selectFromTempObj = prevobj_info.objName;
                        selectFromTempVal = prevobj_info.objValuesTblName;
                    }
                }
            }
            strcat_safe(query, " GROUP BY ", query_size);
            for (i = 0, is_first_element = TRUE; i < idx_params_num - 1; i++)
            {
                if(is_first_element)
                    is_first_element = FALSE;
                else
                    strcat_safe(query, ", ", query_size);
                strcat_safe(query, " t1.[", query_size);
                strcat_safe(query, idx_params[i], query_size);
                strcat_safe(query, "]", query_size);
            }
        }
        else
        {
            strcat_safe(query, " LEFT JOIN ", query_size);
            strcat_safe(query, obj_info->objValuesTblName, query_size);
            strcat_safe(query, " AS t0 ON (", query_size);

            for (j = 0; j < idx_params_num - 1; j++)
            {
                strcat_safe(query, "t0.[", query_size);
                strcat_safe(query, idx_params[j], query_size);
                strcat_safe(query, "]=t1.[", query_size);
                strcat_safe(query, idx_params[j], query_size);
		strcat_safe(query, "]", query_size);
                if (j < idx_params_num -2)
                    strcat_safe(query, " AND ", query_size);
            }
            strcat_safe(query, ")", query_size);

            /*Form WHERE clause containing params that belong to the parent objs*/
            strcat_safe(query, " WHERE 1", query_size);
            for (i = 0; i < parsed_backend_str->subst_val_num; i++)
            {
                if (parsed_backend_str->subst_val[i].mmx_subst_val.obj_name)
                {
                    strcat_safe(query, " AND ", query_size);
                    strcat_safe(query, " t1.[", query_size);
                    strcat_safe(query, parsed_backend_str->subst_val[i].mmx_subst_val.leaf_name, query_size);
                    strcat_safe(query, "]='", query_size);
                    strcat_safe(query, newrow->be_keys[i], query_size);
                    strcat_safe(query, "'", query_size);
                }
            }
            strcat_safe(query, " GROUP BY ", query_size);
            for (i = 0; i < idx_params_num - 1; i++)
            {
                strcat_safe(query, " t1.[", query_size);
                strcat_safe(query, idx_params[i], query_size);
                strcat_safe(query, "],", query_size);
            }
            LAST_CHAR(query) = '\0';
        }
    }

    return EPS_OK;
}


/* Insert to the DB all object instances discovered in the backend,
   (which were not in the DB)                                     */
static ep_stat_t w_getall_process_new_to_db(worker_data_t *wd,
        obj_info_t *obj_info, parsed_backend_method_t *parsed_backend_string,
        int idx_params_num, char **idx_params, sqlite3 *conn,
        getall_keys_ref_t *newkeys)
{
    ep_stat_t status = EPS_OK;
    int i, cnt = 0, rowid = 0;
    int  modified_rows_num = 0;
    int idx_values[MAX_INDECES_PER_OBJECT];
    char buf[EP_SQL_REQUEST_BUF_SIZE], selfRef[NVP_MAX_NAME_LEN], ownerStr[3];
    char *errormsg = NULL;
    namevaluepair_t pnv[2];
    //char print_buf[512];

    memset(pnv, 0, sizeof(pnv));

    //print_getall_keys(newkeys);
    /* Get indices for each new entry */
    for (i = 0; i < newkeys->rows_num; i++)
    {
        w_form_query_getall_insert_new(wd, obj_info, buf, sizeof(buf),
                                       idx_params, idx_params_num,
                                       parsed_backend_string, newkeys->rows_ptr[i]);
        DBG("Insert query: %s", buf);

        //print_getall_one_row_keys(newkeys, i, (char*)&print_buf, sizeof(print_buf));
        if ((status = ep_db_exec_write_query(conn, buf, &modified_rows_num)) == EPS_OK)
        {
            if (modified_rows_num <= 0) /* TODO: Do we actually add only one row and rows num must always be = 1 ? */
            {
                /* It may appear that backend's GETALL script provides some bad
                 * multi-instance Object instances: so it is not possible to add
                 * child object instance of not known parent instance */
                ERROR("Failed to add new entry to DB (no rows added: modified_rows_num = %d)",
                      modified_rows_num);
                continue;
            }

            cnt++;
            rowid = sqlite3_last_insert_rowid(conn);
            DBG("Inserted row: db rowid = %d", rowid);

            /* form self reference string and update DB with it */
            status = w_select_dbrow_indeces(conn, obj_info->objValuesTblName, rowid,
                                            idx_params, idx_params_num, idx_values);
            memset((char*)selfRef, 0, sizeof(selfRef));
            w_place_indeces_to_objname(obj_info->objName, idx_values, idx_params_num, selfRef);
            /*if (strlen(selfRef) > 0 )
                DBG("Self reference to the new added object: %s", selfRef);*/

            strcpy_safe(pnv[0].name, MMX_SELFREF_DBCOLNAME, sizeof(pnv[0].name));
            strcpy_safe(pnv[0].value,(char *)selfRef, sizeof(pnv[0].value));

            sprintf((char *)ownerStr, "%d", EP_DATA_OWNER_SYSTEM);
            strcpy_safe(pnv[1].name, MMX_CREATEOWNER_DBCOLNAME, sizeof(pnv[1].name));
            strcpy_safe(pnv[1].value, (char *)ownerStr, sizeof(pnv[1].value));

            w_update_db_on_addobj(conn,obj_info->objValuesTblName, pnv, 2, rowid);
        }
        else
        {
            DBG("Cannot insert row (status=%d):" /*%s"*/, status /*, print_buf*/);
        }
    }

    if (newkeys->rows_num > 0)
        DBG("%d of %d rows added to db for object %s",cnt, newkeys->rows_num,
                                                           obj_info->objName);
//ret:
    if (errormsg) sqlite3_free(errormsg);
    return EPS_OK;
}


/* Remove one object instance specified by del_row from the db.
 * Dependent (child) objects instances are deleted as well */
static ep_stat_t w_getall_del_row_from_db(worker_data_t *wd,
                 obj_info_t *obj_info, int obj_num,
                 int idx_params_num, char **idx_params,
                 sqlite3 *conn, getall_keys_row_t *del_row)
{
    ep_stat_t status = EPS_OK;
    int i = 0;
    char where[EP_SQL_REQUEST_BUF_SIZE] = {0}, print_buf[512] = {0};

    print_getall_one_row(del_row, (char*)&print_buf, sizeof(print_buf));
    DBG("Row to be deleted: %s", print_buf);

    /* Prepare WHERE clause: it is the same for all dependent objects */
    memset((char *)where, 0, sizeof(where));
    strcat_safe(where, " WHERE 1 ", sizeof(where));
    for (i = 0; i < idx_params_num; i++)
    {
        strcat_safe(where, " AND [", sizeof(where));
        strcat_safe(where, idx_params[i], sizeof(where));
        strcat_safe(where, "] = ", sizeof(where));
        snprintf(where + strlen(where), sizeof(where), "%d", del_row->db_keys[i]);
    }

    status = w_delete_rows_by_where(conn, obj_info, obj_num, where);
    if (status != EPS_OK)
    {
        ERROR("Could not delete instance of obj %s from db (stat=%d)",
              obj_info->objName, status);
        return status;
    }

    DBG("Instance of obj %s successfully deleted from db", obj_info->objName);

    return EPS_OK;
}

/* Prepare SELECT SQL request to get values of all "configuration" parameters
   of the object instance specified by the idx_values array.
   "Configuration" parameters of an object are those
     - writable,
     - and not "not saved in DB" (like triggers)
     - If input parameter "include_hidden" is TRUE, the hidden params
          (as passwords or other secured params) are included to the query,
      otherwise they are not included in the query.
    Output:
       query - buffer with the prepared request
       config_param_num - number of config parameters
*/
static ep_stat_t w_form_query_get_config_params( worker_data_t *wd, obj_info_t *obj_info,
                                                 param_info_t param_info[], int param_num,
                                                 int *idx_values,
                                                 int include_hidden,
                                                 char *query, size_t query_size,
                                                 int *config_param_num)
{
    int i;
    int idx_params_num = 0, config_param_cnt = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];

    if (config_param_num)
        *config_param_num = 0;

     /* Save names of index parameters names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    strcpy_safe(query, "SELECT ", query_size);

    for (i = 0; i < param_num; i++)
    {
        if (param_info[i].writable && !param_info[i].notSaveInDb)
        {
            if (param_info[i].hidden && !include_hidden)
                continue;

            if (config_param_cnt != 0 )
                strcat_safe(query, ", ", query_size);

            strcat_safe(query, "[", query_size);
            strcat_safe(query, param_info[i].paramName, query_size);
            strcat_safe(query, "]", query_size);
            config_param_cnt++;
        }
    }

    if (config_param_cnt == 0)
    {
        DBG("No writable parameters in obj %s", obj_info->objName);
        return EPS_NOTHING_DONE;
    }

    strcat_safe(query, " FROM ", query_size);
    strcat_safe(query, obj_info->objValuesTblName, query_size);

    strcat_safe(query, " WHERE 1", query_size);
    for (i = 0; i < idx_params_num; i++)
    {
        strcat_safe(query, " AND [", query_size);
        strcat_safe(query, idx_params[i], query_size);
        strcat_safe(query, "]=", query_size);
        sprintf(query+strlen(query), "%d", idx_values[i]);
    }

    //DBG("Query to get all writable params of obj %s\n %s", obj_info->objName, query);

    if (config_param_num)
        *config_param_num = config_param_cnt;

    return EPS_OK;
}

/*  Get array of name-value pairs of configuration parameters of the
 *  object instance specified by the keyrow
 *  Output:
 *    cfg_params    - array of name-value pairs
 *    cfg_param_num - as input - contains size of cgf_params array,
 *                    as output - number of returned parameters
 *    cfg_param_idx - array of indexes of config params in the param_info
 *    stmt - sqlite statement - must be freed by the caller!!!
 *           (stmt contains values of all requested parameters)
 */
static ep_stat_t w_get_config_params_from_db(worker_data_t *wd, obj_info_t *obj_info,
                                             sqlite3 *conn,
                                             param_info_t param_info[], int param_num,
                                             int *idx_values,
                                             nvpair_t *cfg_params, int *cfg_param_num,
                                             int *cfg_param_idx,
                                             sqlite3_stmt **p_stmt)
{
    ep_stat_t status = EPS_OK;
    int res, i, j = 0, tmp_idx = 0;
    int cfg_param_cnt = 0;
    char query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    sqlite3_stmt *stmt = NULL;

    if (!cfg_params || !cfg_param_num || !p_stmt)
    {
        ERROR("Bad input parameters");
        return EPS_INVALID_ARGUMENT;
    }

    *p_stmt = NULL;

    if (!obj_info->configurable)
    {
        DBG("Obj %s is not configurable", obj_info->objName);
        status = EPS_NOTHING_DONE;
    }

    /* Prepare SQL query for select all config params including the hidden */
    //DBG("Max number of config params = %d", *cfg_param_num);
    w_form_query_get_config_params(wd, obj_info, param_info, param_num,
                        idx_values, TRUE, query, sizeof(query), &cfg_param_cnt);
    if (cfg_param_cnt == 0)
    {
        DBG("Object %s has no config (writable) params", obj_info->objName);
        *cfg_param_num = 0;
        return EPS_OK;
    }

    if (cfg_param_cnt > *cfg_param_num)
        GOTO_RET_WITH_ERROR(EPS_NO_MORE_ROOM,"Num of config params %d more than nvpair array size %d",
                            cfg_param_cnt, *cfg_param_num);

    *cfg_param_num = 0;
    DBG("Total number of config params %d. Query for get config params:\n\t %s", cfg_param_cnt, query);

    if (sqlite3_prepare_v2(conn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s", sqlite3_errmsg(conn));

    res = sqlite3_step(stmt);

    if (res == SQLITE_ROW)
    {
        j = 0;
        for (i = 0; i < sqlite3_column_count(stmt); i++)
        {
            if ((sqlite3_column_text(stmt, i) != NULL) && (sqlite3_column_bytes(stmt, i) != 0))
            {
                strcpy_safe(cfg_params[j].name, sqlite3_column_name(stmt, i), NVP_MAX_NAME_LEN);
                cfg_params[j].pValue = (char *)sqlite3_column_text(stmt, i);

                if (cfg_param_idx != NULL)
                {
                    w_check_param_name(param_info, param_num, cfg_params[j].name, &tmp_idx);
                    cfg_param_idx[j] = tmp_idx;
                }

                /*DBG("Param %d from DB: %s = %s (param_info index %d)",
                      j, cfg_params[j].name, cfg_params[j].pValue, tmp_idx);*/
                j++;
            }
            else  // Ignore empty config parameters
            {
                //DBG("Empty value of config parameter %s (i=%d)",sqlite3_column_name(stmt, i),i);
            }
        }
    }
    else if (res == SQLITE_DONE)
    {
        DBG("no config params was  selected");
        status = EPS_NOTHING_DONE;
    }
    else
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(wd->mdb_conn));

    /*  Fill the output params */
    *cfg_param_num = j;
    if (j > 0)
       *p_stmt = stmt;

ret:
    if ((status != EPS_OK) || (j == 0))
    {
        if (stmt) sqlite3_finalize(stmt);
        *p_stmt = NULL;
    }

    return status;
}

/* Order cfg_params and cfg_param_idx arrays as follows:
   params using the specified set-style will remain at the beginning
   of the array, other params will be moved to the end.
   The output "p_cnt" contains number of parameters that use
   the specified set-style*/
static ep_stat_t w_order_params_by_set_style(param_info_t param_info[], int param_num,
                                             nvpair_t *cfg_params, int cfg_param_num,
                                             int *cfg_param_idx, int setStyle,
                                             int *p_cnt)
{
    int i, j, tmp_idx;
    nvpair_t  tmp_info;

    for (i = 0, j = 0; i < cfg_param_num; i++)
    {
        if (param_info[ cfg_param_idx[j] ].setOperStyle != setStyle)
        {
            /* keep the paramValue pair and index */
            memcpy((char *)&tmp_info, (char *)&cfg_params[j], sizeof(nvpair_t));
            tmp_idx = cfg_param_idx[j];

            /* move left cfg_params arrays and copy the
               tmp paramValue pair and tmp_idx to the right end */
            memmove (&cfg_params[j], &cfg_params[j+1], (cfg_param_num - j) * sizeof(nvpair_t));
            memcpy(&cfg_params[cfg_param_num -1], &tmp_info, sizeof(nvpair_t));

            memmove (&cfg_param_idx[j], &cfg_param_idx[j+1], (cfg_param_num - j) * sizeof(int));
            cfg_param_idx[cfg_param_num -1] = tmp_idx;
        }
        else
        {
            j++;
        }
    }

    if (p_cnt)
       *p_cnt = j;

    return EPS_OK;
}


static ep_stat_t w_getall_updobj_in_be_script( worker_data_t *wd, obj_info_t *obj_info,
                                        param_info_t param_info[], int param_num,
                                        parsed_param_name_t *pn, sqlite3 *dbconn,
                                        char *idx_params[], int idx_params_num,
                                        nvpair_t *cfg_params, int cfg_param_num,
                                        int *cfg_param_idx, int *setStatus)
{
    ep_stat_t status = EPS_OK;
    int i, cnt = 0;
    int resCode, parsedStatus;
    int idx_values[MAX_INDECES_PER_OBJECT]; // Values of the instance DB indexes
    char          buf[EP_SQL_REQUEST_BUF_SIZE] = {0};  // TODO add (#define) bufsize for add/get/set operations in the top
    char          *p_extr_results;
    char method_str[MAX_METHOD_STR_LEN] = {0};
    parsed_operation_t   parsed_script_method;
    sqlite3_stmt *stmt = NULL;

    if (strlen(obj_info->setMethod) == 0)
        GOTO_RET_WITH_ERROR(EPS_IGNORED,"Script obj set method string is empty for %s",
                                          obj_info->objName);

    memcpy((char *)method_str, obj_info->setMethod, sizeof(method_str));

    status = w_parse_operation_string(OP_SET, method_str, &parsed_script_method);

    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR,"Could not parse script set method for obj %s",
                                                          obj_info->objName);

    /* Move all params that uses the object's set method script-style to the
       beginning of the cfg_params array */
    w_order_params_by_set_style( param_info, param_num, cfg_params,cfg_param_num,
                                 cfg_param_idx, OP_STYLE_NOT_DEF, &cnt);
    DBG("%d params should be updated with obj script style", cnt);

    /* Prepare shell command (with all needed info) and perform it */
    status = w_prepare_command(wd, pn, obj_info, dbconn, &parsed_script_method,
                               idx_params, idx_values, idx_params_num,
                               (char*)&buf, sizeof(buf), &stmt);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Couldn't prepare script command (%d)", status);

    /* Complete the script cmd: insert names and values of params to be set */
    for (i = 0; i < cnt; i++)
    {
        if ((strcat_safe(buf, " -pname ", sizeof(buf)) == NULL) ||
            (strcat_safe(buf, cfg_params[i].name, sizeof(buf)) == NULL) ||
            (strcat_safe(buf, " -pvalue '", sizeof(buf)) == NULL) ||
            (strcat_safe(buf, cfg_params[i].pValue, sizeof(buf)) == NULL) ||
            (strcat_safe(buf, "'", sizeof(buf)) == NULL))
            {
                status = EPS_GENERAL_ERROR;
                DBG("Not enough buf (size: %d) - command is incomplete: \n\t%s", sizeof(buf), buf);
                GOTO_RET_WITH_ERROR(status, "Couldn't prepare parameters script command (%d)", status);
            }
    }
    DBG("Full prepared command: \n\t%s", buf);

    /* Perform prepared command and parse results. */
    p_extr_results = w_perform_prepared_command(buf, sizeof(buf), TRUE, NULL);
    if (!p_extr_results)
        GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR, "Could not read set-script results");

    DBG("Result of the command: %s ", buf);

    status = w_parse_script_res(buf, sizeof(buf), &resCode, &parsedStatus, NULL);
    if (status == EPS_OK)
    {
        DBG ("Parsed script results: resCode %d, setStatus %d",resCode,parsedStatus);
        if (parsedStatus > 0)
            *setStatus = parsedStatus;
    }

ret:
    if (stmt) sqlite3_finalize(stmt);

    return status;
}

static ep_stat_t w_getall_updobj_in_be_backend(worker_data_t *wd, obj_info_t *obj_info,
                                        param_info_t param_info[], int param_num,
                                        parsed_param_name_t *pn, sqlite3 *dbconn,
                                        char *idx_params[], int idx_params_num,
                                        nvpair_t *cfg_params, int cfg_param_num,
                                        int *cfg_param_idx, int *setStatus)
{
    ep_stat_t status = EPS_OK;
    int cnt, res = 0;
    int be_port;
    mmxba_request_t  be_ans;
    char method_str[MAX_METHOD_STR_LEN] = {0};
    parsed_backend_method_t  parsed_be_method;
    char query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    sqlite3_stmt *stmt;

    if (strlen(obj_info->setMethod) == 0)
        GOTO_RET_WITH_ERROR(EPS_IGNORED,"BE obj set method string is empty for %s",
                                                          obj_info->objName);

    DBG ("Start: cfg_param_num = %d", cfg_param_num);

    memcpy((char *)method_str, obj_info->setMethod, sizeof(method_str));

    status = w_parse_backend_method_string(OP_SET, method_str, &parsed_be_method);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not parse BE set method for obj %s",
                                                          obj_info->objName);
    /* Move all params that uses the object's set method backend-style to the
       beginning of the cfg_params array */
    w_order_params_by_set_style(param_info, param_num, cfg_params,cfg_param_num,
                                cfg_param_idx, OP_STYLE_NOT_DEF, &cnt);
    DBG("%d params should be updated with obj BE style", cnt);

    if (idx_params_num > 0)
    {
        w_form_subst_sql_select_backend(wd, pn, obj_info, query, sizeof(query),
                                        idx_params, idx_params_num, &parsed_be_method);
        DBG("Subst query:\n%s", query);

        if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR,"Could not prepare SQL statement: %s", sqlite3_errmsg(dbconn));
    }

    if (stmt)
    {
        res = sqlite3_step(stmt);
        if (res != SQLITE_ROW)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s",
                                sqlite3_errmsg(dbconn));
    }

     /* Get port number of the backend*/
    w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0);
    DBG("Backend: %s, port %d", obj_info->backEndName, be_port);

    /* Form BE API request, send it to the backend, wait for reply */
    status = form_and_send_be_request(wd, be_port, MMXBA_OP_TYPE_SET,
                                      &parsed_be_method, stmt, idx_params_num,
                                      param_num, param_info,
                                      cnt, cfg_params, &be_ans);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "BE SET request failure (%d)", status);

    if (be_ans.opResCode != 0)
    {
        if (strlen(be_ans.errMsg) > 0)
            ERROR("Backend has returned error message:\n %d: %s", be_ans.opExtErrCode, be_ans.errMsg);

        ERROR("Backend has returned error (%d) on update operation",be_ans.opResCode);
    }
    else
    {
        if (setStatus && be_ans.postOpStatus != 0)
        {
            DBG("postOpStatus for update operation = %d",  be_ans.postOpStatus);
           *setStatus = 1;
        }
    }

ret:
   if (stmt) sqlite3_finalize(stmt);

   return status;
}

/* Updates the backend of the specified object according to the
   DB content.
   param_info[] - is an array with configuration parameters,
   instkeys - array with the DB keys of the instances that should be
              updated. In case of a one-instance object (scalar obj)
              it contains 0 key row
*/
static ep_stat_t w_getall_updobj_in_be(worker_data_t *wd, obj_info_t *obj_info,
                                       param_info_t param_info[], int param_num,
                                       sqlite3 *conn, getall_keys_ref_t *instkeys,
                                       int *setStatus)
{
    ep_stat_t status = EPS_OK;
    int i, j, c, cnt = 0, cfg_param_num = MAX_PARAMS_PER_OBJECT;
    int                   inst_num = 0, beRestartNeeded = 0;
    oper_style_t          objSetStyle, paramSetStyle;
    nvpair_t              cfg_nvpairs[MAX_PARAMS_PER_OBJECT];
    int                   cfg_param_idx[MAX_PARAMS_PER_OBJECT];
    int                   idx_params_num = 0;
    char                 *idx_params[MAX_INDECES_PER_OBJECT];
    int                   idx_values[MAX_INDECES_PER_OBJECT];
    BOOL                  scalarObj = FALSE;
    getall_keys_row_t    *key_row;
    parsed_param_name_t   pn;

    sqlite3_stmt         *stmt = NULL;

    //print_getall_keys(instkeys);

    /* Save index param names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    if (idx_params_num != 0)
    {
        if (instkeys == NULL || instkeys->rows_num == 0)
        {
            //DBG("No instance should be updated in backend"); // Nothing to do
            return EPS_OK;
        }
        inst_num = instkeys->rows_num;
        DBG("%d instance(s) should be updated in backend %s",
                                       inst_num, obj_info->backEndName);
    }
    else
    {
        scalarObj = TRUE;
    }

    objSetStyle = obj_info->setOperStyle;

    i = -1;
    while ((++i < inst_num) || (scalarObj == TRUE))
    {
        /* Prepare indexes of i-th instance of multi-instance object */
        if (scalarObj == FALSE)
        {
            key_row = instkeys->rows_ptr[i];
            memset(idx_values, 0, sizeof(idx_values));
            for (j = 0; j < key_row->keys_num; j++)
            {
                idx_values[j] = key_row->db_keys[j];
            }
        }

        beRestartNeeded = 0;

        /* Get name-value pairs for all writable params of the instance */
        cfg_param_num = MAX_PARAMS_PER_OBJECT;
        status = w_get_config_params_from_db(wd, obj_info, conn, param_info, param_num,
                                             idx_values, (nvpair_t *)cfg_nvpairs, &cfg_param_num,
                                             (int *)&cfg_param_idx, &stmt);
        if (cfg_param_num == 0)
        {
            if ((status == EPS_OK) || (status == EPS_NOTHING_DONE))
                DBG("No config params values selected for inst %d (stat = %d)", i, status);
            else
                WARN("Couldn't select config params for inst %d (stat = %d)", i, status);
            goto next_inst;
        }

        /* Prepare parsed object name and instance indexes */
        pn.partial_path = FALSE;
        strcpy_safe(pn.obj_name, obj_info->objName, sizeof(pn.obj_name));
        for (j = 0; j < idx_params_num; j++)
        {
            pn.indices[j].type = REQ_IDX_TYPE_EXACT;
            pn.indices[j].exact_val.num = idx_values[j];
        }
        pn.index_num = idx_params_num-1;

        /* --------- Per-object processing ------------ */
        if (objSetStyle == OP_STYLE_BACKEND) // Update all "backend" set style params
        {
            status = w_getall_updobj_in_be_backend(wd, obj_info, param_info, param_num,
                               &pn, conn, idx_params, idx_params_num,
                               cfg_nvpairs, cfg_param_num, cfg_param_idx, &beRestartNeeded);
        }
        else if (objSetStyle == OP_STYLE_SCRIPT) // Update all "script" set style params
        {
            status = w_getall_updobj_in_be_script( wd, obj_info, param_info, param_num,
                                &pn, conn, idx_params, idx_params_num,
                                cfg_nvpairs, cfg_param_num, cfg_param_idx, &beRestartNeeded);
        }
        else
        {
            /* There is no other per-object set methods */
        }

        if (setStatus != NULL && beRestartNeeded > 0)
            *setStatus = beRestartNeeded;

        /* --------------- Per_parameter processing ------------------ */
        for (j = 0; j < cfg_param_num; j++)
        {
            beRestartNeeded = 0;

            c = cfg_param_idx[j];

            paramSetStyle = param_info[c].setOperStyle;
            strcpy_safe(pn.leaf_name, param_info[c].paramName, sizeof(pn.leaf_name));

            if (paramSetStyle == OP_STYLE_NOT_DEF || paramSetStyle == OP_STYLE_DB ||
                paramSetStyle == OP_STYLE_ERROR)
                continue;

            switch (paramSetStyle)
            {
                case OP_STYLE_SCRIPT:
                    status = w_set_value_script(wd, &pn, obj_info, conn, param_info, param_num,
                                                c, cfg_nvpairs[j].pValue, &beRestartNeeded, NULL);
                    break;
                case OP_STYLE_SHELL_SCRIPT:
                    status = w_set_value_shell(wd, &pn, obj_info, conn, param_info, param_num,
                                               c, cfg_nvpairs[j].pValue, &beRestartNeeded, NULL);
                    break;
                case OP_STYLE_BACKEND:
                    status = w_set_value_backend(wd, &pn, obj_info, conn, param_info, param_num,
                                                 c, 1, &(cfg_nvpairs[j]), &beRestartNeeded);
                    break;
                case OP_STYLE_UCI:
                    status = w_set_value_uci(wd, &pn, obj_info, conn, param_info, param_num,
                                             c, cfg_nvpairs[j].pValue, &beRestartNeeded);
                    break;
                case OP_STYLE_UBUS: /* currently not used */
                    status = w_set_value_ubus(wd, &pn, obj_info, param_info, param_num,
                                              c, cfg_nvpairs[j].pValue);
                    break;
                default:
                    break;
            }

            DBG("Param %s was updated (stat %d), param set style %d",
                param_info[c].paramName, status, param_info[c].setOperStyle);
        }

        //DBG("Update object in the backend: row %d, status %d", i, status);

        if (status == EPS_OK)
        {
            cnt++;

            if (setStatus != NULL && beRestartNeeded > 0)
                *setStatus = beRestartNeeded;
        }

next_inst:
        if (stmt) sqlite3_finalize(stmt);

        if (scalarObj == TRUE)
            break;
    }   //End of "for i" loop over instances

    DBG("%d (of %d) instances successfully updated in the backend",cnt,inst_num);

    return status;
}

/* Add object instance that is known in the DB, but is not known in the backend
   to the backend. Addobj method-style is "script  */
static ep_stat_t w_getall_addobj_to_be_script(worker_data_t *wd,
                       obj_info_t *obj_info,  sqlite3 *conn,
                       parsed_operation_t  *p_parsed_method,
                       param_info_t param_info[], int param_num,
                       getall_keys_row_t  *key_row, int *addStatus)
{
    ep_stat_t status = EPS_OK;
    int   j, res_code, parsed_status;
    int   idx_params_num = 0, config_param_num = MAX_PARAMS_PER_OBJECT;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_values[MAX_INDECES_PER_OBJECT];
    char *p_extr_results = NULL, *token, *strtok_ctx;
    parsed_param_name_t pn;
    nvpair_t cfg_nvpairs[MAX_PARAMS_PER_OBJECT];
    char buf[EP_SQL_REQUEST_BUF_SIZE]  = { 0 }; // TODO add (#define) bufsize for add/get/set operations in the top
    sqlite3_stmt *stmt = NULL;

    /* Save index param names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    memset(idx_values, 0, sizeof(idx_values));
    for (j = 0; j < key_row->keys_num; j++)
        idx_values[j] = key_row->db_keys[j];

    /* Get name-value pairs for all writable params of the instance */
    status = w_get_config_params_from_db(wd, obj_info, conn,
                            param_info, param_num, idx_values,
                            (nvpair_t *)cfg_nvpairs, &config_param_num, NULL, &stmt);

    if ((status != EPS_OK) || (config_param_num == 0))
    {
        DBG("No config params selected (stat = %d, num = %d)", status, config_param_num);
        return status;
    }

     /* Fill parsed object name needed for preparing addobj command */
    pn.partial_path = FALSE;
    strcpy_safe(pn.obj_name, obj_info->objName, sizeof(pn.obj_name));
    for (j = 0; j < key_row->keys_num-1; j++)
    {
        pn.indices[j].type = REQ_IDX_TYPE_EXACT;
        pn.indices[j].exact_val.num = key_row->db_keys[j];
    }
    pn.index_num = key_row->keys_num-1;

    status = w_prepare_addobj_command(wd, obj_info, conn,
                       idx_params, key_row->db_keys, idx_params_num,
                       (nvpair_t *)cfg_nvpairs, config_param_num,
                       &pn, p_parsed_method, (char*)&buf[0], sizeof(buf));

    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status,"Could not prepare addobj command for %s (err %d)",
                            obj_info->objName, status);

    DBG("Full prepared command: \n\t%s", buf);

    /* Perform prepared command and parse results:
       if failure:  "resCode;", if success:  "0; addStatus;key1, key2, ...",
       where addStatus is 0 or 1, key1/2/... are values of backend keys */
    p_extr_results = w_perform_prepared_command((char*)&buf[0], sizeof( buf), TRUE, NULL);
    if (!p_extr_results)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR,"Could not read addobj script results");

    DBG( "Result of the command: %s ", p_extr_results );

    status = w_parse_script_res(buf, sizeof(buf), &res_code, &parsed_status, &p_extr_results);
    if (status == EPS_OK)
    {
        DBG ("Parsed script results: resCode %d, setStatus %d", res_code, parsed_status);
        *addStatus = parsed_status;
    }

    if (res_code == 0)
    {
        /* Check if backend have returned be key parameters values */
        token = strtok_r( p_extr_results, ";", &strtok_ctx );
        if (token && ( strlen( token ) > 0 ))
        {
            DBG( "Script returned BE keys: %s", token );
        }
        else //Script didn't return key parameter values.
        {
            if (p_parsed_method->bekey_param_num > 0)
                WARN("AddObj script didn't returned expected values of %d be key params",
                     p_parsed_method->bekey_param_num );
        }
    }
    else //Script failed to add object
    {
        GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,
                    "AddObj script failed (resCode %d)", res_code );
    }

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}


/* Add object instance that is known in the DB, but is not known in the backend
   to the backend. Addobj method-style is "backend". */
static ep_stat_t w_getall_addobj_to_be_backend(
                 worker_data_t *wd, obj_info_t *obj_info, sqlite3 *conn,
                 parsed_backend_method_t  *p_parsed_method,
                 param_info_t param_info[], int param_num,
                 getall_keys_row_t *key_row, int *addStatus)
{
    ep_stat_t status = EPS_OK;
    int   j, res;
    int   be_port;
    int   idx_params_num = 0, config_param_num = MAX_PARAMS_PER_OBJECT;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_values[MAX_INDECES_PER_OBJECT];
    nvpair_t cfg_nvpairs[MAX_PARAMS_PER_OBJECT];
    mmxba_request_t be_ans;

    parsed_param_name_t pn;
    sqlite3_stmt *stmt = NULL;
    sqlite3_stmt *stmt1 = NULL;
    char query[EP_SQL_REQUEST_BUF_SIZE]  = { 0 };

    /* Save index param names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    memset(idx_values, 0, sizeof(idx_values));
    for (j = 0; j < key_row->keys_num; j++)
        idx_values[j] = key_row->db_keys[j];

    /* Get name-value pairs for all writable params of the instance */
    status = w_get_config_params_from_db(wd, obj_info, conn,
                            param_info, param_num, idx_values,
                            (nvpair_t *)cfg_nvpairs, &config_param_num, NULL, &stmt);

    if ((status != EPS_OK) || (config_param_num == 0))
    {
        DBG("No config params selected (stat = %d, num = %d)", status, config_param_num);
        return status;
    }

    /* Get port number of the backend */
    if ((w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0) != EPS_OK) ||
        (be_port <= 0) )
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s",
                            be_port, obj_info->backEndName);

    DBG("Backend '%s': port %d", obj_info->backEndName, be_port);

    /* Fill parsed object name. We need it for preparing addobj command */
    pn.partial_path = FALSE;
    strcpy_safe(pn.obj_name, obj_info->objName, sizeof(pn.obj_name));
    for (j = 0; j < key_row->keys_num-1; j++)
    {
        pn.indices[j].type = REQ_IDX_TYPE_EXACT;
        pn.indices[j].exact_val.num = key_row->db_keys[j];
    }
    pn.index_num = key_row->keys_num-1;

    if (p_parsed_method->subst_val_num > 0)
    {
        w_form_subst_sql_select_backend( wd, &pn, obj_info, query, sizeof(query),
                                         idx_params, idx_params_num, p_parsed_method);
        DBG("Subst query:\n%s", query);

        if (strlen(query) > 0)
        {
            if (sqlite3_prepare_v2(conn, query, -1, &stmt1, NULL) != SQLITE_OK)
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                                    sqlite3_errmsg(conn));

            res = sqlite3_step(stmt1);
            if (res != SQLITE_ROW)
                    GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not perform SQL query: %s",
                                        sqlite3_errmsg(conn));
        }
    }

    status = form_and_send_be_request(wd, be_port, MMXBA_OP_TYPE_ADDOBJ,
                           p_parsed_method, stmt1, idx_params_num, 0, NULL,
                           config_param_num, (nvpair_t *)cfg_nvpairs, &be_ans );
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "be request failure (%d)", status);

    if (be_ans.opResCode != 0)
        GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,"Backend returned error: %d: %d: %s",
            be_ans.opResCode, be_ans.opExtErrCode,
            strlen(be_ans.errMsg) ? be_ans.errMsg : " ");

    if (be_ans.addObj_resp.objNum < 1)
        GOTO_RET_WITH_ERROR(EPS_BACKEND_ERROR,"No objects was added (%d)",
                            be_ans.addObj_resp.objNum);

    *addStatus = be_ans.postOpStatus;

ret:
    if (stmt)  sqlite3_finalize(stmt);
    if (stmt1) sqlite3_finalize(stmt1);
    return status;
}

/* Add the specified object instances to the backend */
static ep_stat_t w_getall_addobj_to_be(worker_data_t *wd, obj_info_t *obj_info,
                                       param_info_t param_info[], int param_num,
                                       sqlite3 *conn, getall_keys_ref_t *instkeys,
                                       int *addStatus)
{
    ep_stat_t status = EPS_OK;
    int i, cnt = 0;
    int beRestartNeeded = 0;
    getall_keys_row_t       *key_row;
    parsed_operation_t       parsed_script_method;
    parsed_backend_method_t  parsed_backend_method;

    if (instkeys->rows_num < 1)
    {
        //DBG("No instance should be added to backend"); // Nothing to do
        return EPS_OK;
    }

    DBG("%d instance(s) should be added to backend %s", instkeys->rows_num,
         obj_info->backEndName);

    /* Parse Addobj method string that will be used for all instances of the object */
    if (strlen(obj_info->addObjMethod) > 0)
    {
        if (obj_info->addObjStyle == OP_STYLE_SCRIPT)
        {
            status = w_parse_operation_string(OP_ADDOBJ, obj_info->addObjMethod,
                                              &parsed_script_method);
        }
        else if (obj_info->addObjStyle == OP_STYLE_BACKEND)
        {
            status = w_parse_backend_method_string(OP_ADDOBJ, obj_info->addObjMethod,
                                                   &parsed_backend_method);
        }
        else // Impossible case
            GOTO_RET_WITH_ERROR(EPS_NOT_IMPLEMENTED, "Addobj style '%s' not supported",
                                                 obj_info->addObjStyle);

        if (status != EPS_OK)
            GOTO_RET_WITH_ERROR(status, "Could not parse addobj method staring (%d)", status);
    }
    else
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "AddObj method string is empty for object %s",
                            obj_info->objName);

    for (i = 0; i < instkeys->rows_num; i++)
    {
        key_row = instkeys->rows_ptr[i];

        beRestartNeeded = 0;

        if (obj_info->addObjStyle == OP_STYLE_SCRIPT)
        {
            status = w_getall_addobj_to_be_script(wd, obj_info, conn,
                                &parsed_script_method, param_info, param_num,
                                key_row, &beRestartNeeded);
        }
        else if (obj_info->addObjStyle == OP_STYLE_BACKEND)
        {
            status = w_getall_addobj_to_be_backend(wd, obj_info, conn,
                            &parsed_backend_method, param_info, param_num,
                            key_row, &beRestartNeeded);
        }

        DBG("Adding object to backend: row %d, status %d", i, status);

        if (status == EPS_OK)
        {
            cnt++;

            if (addStatus != NULL && beRestartNeeded > 0)
                *addStatus = beRestartNeeded;
        }

#if 0
        /* !!!TODO: Think do we need to remove row if adding to backend failed? */
        if ( status != EPS_OK )
        {
            /* Prepare WHERE clause to Delete this obj instance from the db */
            memset( where, 0, sizeof( where ) );
            strcpy_safe( where, " WHERE 1 ", sizeof( query ) );
            for ( i = 0; i < idx_params_num; i++ )
            {
                strcat_safe( where, " AND [",       sizeof( query ) );
                strcat_safe( where, idx_params[i], sizeof( query ) );
                strcat_safe( where, "] = ",         sizeof( query ) );
                sprintf( where + strlen( where ), "%d", instkeys[j]->dbkeys.);
            }

            w_delete_rows_by_where( conn, obj_info, 1, where );
        }
#endif
    } //End of for stmt over instances

    DBG("%d (of %d) instances added to backend during config discovery",
        cnt, instkeys->rows_num);
ret:
    return status;
}

static ep_stat_t w_getall_obj_backend(worker_data_t *wd, obj_info_t *obj_info,
                                      parsed_backend_method_t *parsed_backend_string,
                                      getall_keys_t *bekeys)
{
    ep_stat_t status = EPS_OK;
    int reqSeqNum = 0;
    int be_port = -1, rcvd;
    char buf[MAX_MMX_BE_REQ_LEN];
    mmxba_request_t be_ans;
    mmxba_packet_t *pkt = (mmxba_packet_t *)buf;

    memcpy(pkt->flags, mmxba_flags, sizeof(mmxba_flags));

    w_form_backend_request_getall(wd, pkt->msg, ((sizeof(buf) - sizeof(pkt->flags)) - 1), parsed_backend_string);

    /* Get port number of the backend*/
    if ((w_get_backend_info(wd, obj_info->backEndName, &be_port, NULL, 0) != EPS_OK) ||
        (be_port <= 0) )
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not get port number (%d) for backend %s",
                            be_port, obj_info->backEndName);

    DBG("Backend '%s': port %d", obj_info->backEndName, be_port);

    /* Send message to backend */
    if (w_send_pkt_to_backend(wd->udp_be_sock, be_port, pkt) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not send packet to backend");

    reqSeqNum = wd->be_req_cnt + (wd->self_w_num * (EP_MAX_BE_REQ_SEQNUM + 1));

    /* Receive answer */
    DBG("Sent: (%c) %s", pkt->flags[0], pkt->msg);
    //DBG("Waiting for backend answer (req seqNum %d)", reqSeqNum);

    if (w_rcv_answer_from_backend(wd->udp_be_sock, reqSeqNum, buf, sizeof(buf), &rcvd) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not receive answer from backend");

    DBG("Received %d bytes: %s", rcvd, buf);

    if (mmx_backapi_message_parse(buf, &be_ans) != MMXBA_OK)
        GOTO_RET_WITH_ERROR(EPS_GENERAL_ERROR, "Could not parse response from backend");

    if (be_ans.opResCode != 0)
    {
        if (strlen(be_ans.errMsg) > 0)
        {
            ERROR("Backend returned error: %d: %s", be_ans.opExtErrCode, be_ans.errMsg);
        }
        GOTO_RET_WITH_ERROR(be_ans.opResCode, "Backend returned error");
    }

    DBG("Backend returned %d object(s)", be_ans.getAll.objNum);

    /* Fill bekeys from backend's response */
    if (w_fill_bekeys_backend(wd, be_ans.getAll.objects, be_ans.getAll.objNum, bekeys) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not get keys from backend");

ret:
    return status;
}

static ep_stat_t w_form_call_str_getall(char *buf, size_t buf_len, parsed_backend_method_t *parsed_backend_string)
{
    strcpy_safe(buf, parsed_backend_string->beObjName, buf_len);
    /* TODO remove it after testing.
    for (int i = 0; i < parsed_backend_string->subst_val_num; i ++)
    {
        strcat_safe(buf, " ", buf_len);
        strcat_safe(buf, parsed_backend_string->subst_val[i].backend_key_name, buf_len);
    }
    */

    //strcat_safe(buf, "  2>/dev/null", buf_len);

    DBG("call string: '%s'", buf);
    return EPS_OK;
}

static ep_stat_t w_getall_obj_script(worker_data_t *wd, obj_info_t *obj_info,
                                     parsed_backend_method_t *parsed_backend_string,
                                     getall_keys_t *bekeys, parsed_param_name_t *pn)
{
    ep_stat_t status = EPS_OK;
    char *be_key, *p_extr_param;
    FILE *fp;
    char *strtok_ctx1, *strtok_ctx2, *subtoken, *token;
    int  i, res_code = 99;
    char *buf = wd->be_req_xml_buf;
    int bufSize = sizeof(wd->be_req_xml_buf);
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    int param_num;
    int idx_params_num = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_values[MAX_INDECES_PER_OBJECT];

    memset(bekeys, 0, sizeof(getall_keys_t));
    memset(buf, 0, bufSize);

    if (pn->index_set_num)
    {
        status = w_get_param_info(wd
                 , pn, obj_info, 0, param_info, &param_num, NULL);

        if (status != EPS_OK)
        {
            GOTO_RET_WITH_ERROR(status, "Could not w_get_param_info");
        }

        status = get_index_param_names (param_info
                 , param_num, idx_params, &idx_params_num);

        if (status != EPS_OK)
        {
            GOTO_RET_WITH_ERROR(status, "Could not get_index_param_names");
        }

        status = w_prepare_getall_command(wd
                 , pn
                 , obj_info
                 , parsed_backend_string
                 , idx_params, idx_values, idx_params_num, buf, bufSize);

        if (status != EPS_OK)
        {
            GOTO_RET_WITH_ERROR(status, "Could not w_parse_operation_string");
        }
    }
    else
    {
        w_form_call_str_getall(buf, bufSize, parsed_backend_string);
    }

    fp = popen(buf, "r");
    memset(buf, 0, bufSize);
    if (!fp)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not execute getall script");

    p_extr_param = fgets(buf, bufSize-1, fp);
    pclose(fp);
    if (!p_extr_param)
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "fget for getall script failed");

    trim(buf);
    DBG("script returned: '%s'", buf);

    /* The first token before ";" contains script result code (0 is success)*/
    token = strtok_r(buf, ";", &strtok_ctx1);
    if (!token || ( (res_code = atoi(token)) != 0) )
    {
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Script returned error: %d", res_code);
    }

    token = strtok_r(NULL, ";", &strtok_ctx1);
    trim(token);
    while (token && (strlen(token) > 0))
    {
        //DBG("getall script returned token (for row %d): '%s'", bekeys->rows_num, token);
        i = 0;  //counter of keys in each row
        for (subtoken = strtok_r(token, ",", &strtok_ctx2); subtoken;
             subtoken = strtok_r(NULL, ",", &strtok_ctx2))
        {
            trim(subtoken);
            if (strlen(subtoken) > 0)
            {
                //DBG("getall script returned subtoken: '%s'", subtoken);
                be_key = bekeys->rows[bekeys->rows_num].be_keys[i++];
                strcpy_safe(be_key, subtoken, MMXBA_MAX_STR_LEN);
            }
            else
            {
                DBG("Empty string is received in subtoken!");
            }
        }
        bekeys->rows[bekeys->rows_num].keys_num = i;
        bekeys->rows[bekeys->rows_num].row_index = bekeys->rows_num;
        bekeys->rows_num++;

        token = strtok_r(NULL, ";", &strtok_ctx1);
        trim(token);
    }

ret:
    return status;
}

/* Configuration discovery and sync (i.e. update config data in backend and DB)
   of the specified multi-instance object */
static ep_stat_t w_config_disc_object(worker_data_t *wd, const char *name,
                                      int *beRestart, char *beName, int beNameSize)
{
    ep_stat_t status = EPS_OK;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_params_num = 0, param_num = 0, obj_num;
    int addStatus = 0, updStatus = 0;
    int cmp_res, dbrow_pos = 0, berow_pos = 0;
    int remainingCnt = 0, delFromDbCnt = 0;
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    obj_info_t obj_info_arr[MAX_DEPENDED_OBJ_NUM];
    obj_info_t *obj_info = &obj_info_arr[0];

    sqlite3 *conn = NULL;
    getall_keys_t dbkeys, bekeys;
    getall_keys_ref_t refNewDbKeys, refUpdBeKeys, refAddToBeKeys;

    parsed_param_name_t pn = {{0}};
    parsed_backend_method_t parsed_method_string;

    memset(&dbkeys, 0, sizeof(getall_keys_t));
    memset(&bekeys, 0, sizeof(getall_keys_t));

    memset(&refNewDbKeys, 0, sizeof(getall_keys_ref_t));
    memset(&refUpdBeKeys, 0, sizeof(getall_keys_ref_t));
    memset(&refAddToBeKeys, 0, sizeof(getall_keys_ref_t));

    pn.partial_path = TRUE; /* DiscoverConfig works with partial paths only */
    strcpy_safe(pn.obj_name, name, sizeof(pn.obj_name));

    status = w_get_obj_info(wd, &pn, 0, 1, (obj_info_t *)obj_info_arr, MAX_DEPENDED_OBJ_NUM, &obj_num);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not get object info");

    /*DBG("Obj info for %s; got %d dependent objs (included itself)", name, obj_num);*/

    if ((strlen(obj_info->getAllMethod) == 0) ||
        ((obj_info->getAllOperStyle != OP_STYLE_BACKEND) &&
         (obj_info->getAllOperStyle != OP_STYLE_SCRIPT)))
        return  EPS_OK;  //Do nothing - ignore such object

    /* Since getall method string has the same format for both "script"  */
    /* and "backend" style we use here parse_backend_method function     */
    status = w_parse_backend_method_string(OP_GETALL, obj_info->getAllMethod,
                                           &parsed_method_string);

    if (obj_info->getAllOperStyle == OP_STYLE_BACKEND)
    {
        status = w_getall_obj_backend(wd, obj_info, &parsed_method_string, &bekeys);
        if (status != EPS_OK)
            GOTO_RET_WITH_ERROR(status, "Could not process object (backend style)");
    }
    else if (obj_info->getAllOperStyle == OP_STYLE_SCRIPT)
    {
        status = w_getall_obj_script(wd, obj_info, &parsed_method_string, &bekeys, &pn);
        if (status != EPS_OK)
            GOTO_RET_WITH_ERROR(status, "Could not process object (script style)");
    }
    else
        GOTO_RET_WITH_ERROR(EPS_NOT_IMPLEMENTED, "Getall style '%s' not supported",
                            operstyle2string(obj_info->getAllOperStyle));

    conn = wd->main_conn;

    /* Get info about writable parameters and indeces */
    if ((status = w_get_param_info(wd, &pn, obj_info, 1, param_info, &param_num, NULL)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not get parameter info");

    /* Save index names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    /* Get all keys from db */
    if (w_fill_dbkeys(wd, obj_info, &parsed_method_string, idx_params_num, idx_params, conn, &dbkeys, &pn) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not get keys from db");

    DBG("Current instances: %d in db, %d in backend ", dbkeys.rows_num, bekeys.rows_num);
    DBG("Instances in the db:");      print_getall_keys(&dbkeys);
    //DBG("Instances in the backend:"); print_getall_keys(&bekeys);

    /* Compare instances from DB and from backend; decide what to do with them*/

    qsort(&dbkeys.rows, dbkeys.rows_num, sizeof(dbkeys.rows[0]), &compare_getall_rows);
    qsort(&bekeys.rows, bekeys.rows_num, sizeof(bekeys.rows[0]), &compare_getall_rows);

    while (dbrow_pos < dbkeys.rows_num && berow_pos < bekeys.rows_num)
    {
        cmp_res = compare_getall_rows(&dbkeys.rows[dbrow_pos], &bekeys.rows[berow_pos]);

        if (cmp_res == 0) /* instance is known both in DB and backend */
        {
            if (dbkeys.rows[dbrow_pos].create_owner == EP_DATA_OWNER_USER ||
                dbkeys.rows[dbrow_pos].cfg_owner == EP_DATA_OWNER_USER)
            {
                refUpdBeKeys.rows_ptr[refUpdBeKeys.rows_num++] = &dbkeys.rows[dbrow_pos];
            }
            else
            {
                remainingCnt++;
            }
            dbrow_pos++;
            berow_pos++;
        }
        else if (cmp_res < 0) /*instance known in DB, but unknown in backend*/
        {
            if (obj_info->writable == FALSE)
            {
                /* It's read-only object: backend is "owner" of all instances.
                If the instance does not contain data configured by user,
                it should be deleted from the db.
                Otherwise we need to keep this row (!!!May cause "not-active" row*/
                if (dbkeys.rows[dbrow_pos].cfg_owner == EP_DATA_OWNER_SYSTEM)
                {
                    w_getall_del_row_from_db(wd, (obj_info_t *)obj_info_arr, obj_num,
                               idx_params_num, idx_params, conn, &dbkeys.rows[dbrow_pos]);
                    delFromDbCnt++;
                }
                else
                    remainingCnt++;
            }
            else
            {
                /* Read-write obj: instance can be created by user or by system:
                   if it is user-created instance, add it to the backend,
                   if it is system-created instance, deleted it from the DB. */
                if (dbkeys.rows[dbrow_pos].create_owner == EP_DATA_OWNER_USER)
                {
                    refAddToBeKeys.rows_ptr[refAddToBeKeys.rows_num++] = &dbkeys.rows[dbrow_pos];
                }
                else   //it is system-created instance
                {
                    if (dbkeys.rows[dbrow_pos].cfg_owner != EP_DATA_OWNER_USER)
                    {
                        //DBG("Instance of read-write object created by system is found");
                        w_getall_del_row_from_db(wd, (obj_info_t *)obj_info_arr, obj_num, idx_params_num, idx_params,
                                                 conn, &dbkeys.rows[dbrow_pos]);
                        delFromDbCnt++;
                    }
                    else  /* system-created and user-configured instance
                             Keep this instance in the DB. If the instance
                             will be re-created by the system in the backend,
                             EP will update it according to the DB config */
                    {
                        remainingCnt++;
                    }
                }
            }
            dbrow_pos++;
        }
        else /* instance known in the backend, but unknown in the DB */
        {
            /* Instances of read-only objects should always be added to the DB.
               Writable object instances are created mostly by user, but
               sometimes by backend as well (for ex, default config info),
               so we need "to merge instances", i.e. add them to DB */
            refNewDbKeys.rows_ptr[refNewDbKeys.rows_num++] = &bekeys.rows[berow_pos++];
        }
    }  //End of while over dbrows and berows

    /* Check remaining rows that are known in the DB, but unknown in backend */
    while (dbrow_pos < dbkeys.rows_num)
    {
        if (obj_info->writable == FALSE)
        {
            if (dbkeys.rows[dbrow_pos].cfg_owner == EP_DATA_OWNER_SYSTEM)
            {
                w_getall_del_row_from_db(wd, (obj_info_t *)obj_info_arr, obj_num, idx_params_num, idx_params,
                                         conn, &dbkeys.rows[dbrow_pos]);
                delFromDbCnt++;
            }
            else
            {
                remainingCnt++;
            }
        }
        else /*If writable obj inst was created by user - add it to backend,
               if it was created by system - delete it from the DB         */
        {
            if (dbkeys.rows[dbrow_pos].create_owner == EP_DATA_OWNER_USER)
            {
                refAddToBeKeys.rows_ptr[refAddToBeKeys.rows_num++] = &dbkeys.rows[dbrow_pos];
            }
            else //it is system-created instance
            {
                if (dbkeys.rows[dbrow_pos].cfg_owner != EP_DATA_OWNER_USER)
                {
                    //DBG("Instance of writable object created by system is found");
                    w_getall_del_row_from_db(wd, (obj_info_t *)obj_info_arr, obj_num, idx_params_num, idx_params,
                                             conn, &dbkeys.rows[dbrow_pos]);
                    delFromDbCnt++;
                }
                else // system-created and user-configured instance
                {
                    remainingCnt++;
                }
            }
        }

        dbrow_pos++;
    }

    /* Check remaining rows that are known in the backend, but unknown in DB */
    while (berow_pos < bekeys.rows_num)
    {
        refNewDbKeys.rows_ptr[refNewDbKeys.rows_num++] = &bekeys.rows[berow_pos++];
    }

    /* Sort the rows prepared for adding to DB as they received from backend */
    qsort(refNewDbKeys.rows_ptr, refNewDbKeys.rows_num, sizeof(refNewDbKeys.rows_ptr[0]),
                                            &compare_getall_ref_row_indexes);

    /* And now process all instances according to the above decisions */
    DBG("Instance analysis results: \n\t\t"
        "%d should be updated in be, %d should be added to DB, %d should be added to be,\n\t\t"
        "%d deleted from DB, %d kept in db as is",
         refUpdBeKeys.rows_num, refNewDbKeys.rows_num, refAddToBeKeys.rows_num,
         delFromDbCnt, remainingCnt);

    w_getall_process_new_to_db(wd, obj_info, &parsed_method_string,
                               idx_params_num, idx_params, conn, &refNewDbKeys);

    w_getall_addobj_to_be(wd, obj_info, param_info, param_num, conn,
                          &refAddToBeKeys, &addStatus);

    w_getall_updobj_in_be(wd, obj_info, param_info, param_num, conn,
                          &refUpdBeKeys, &updStatus);
ret:

    DBG("restart status of backend %s: addStatus=%d, updStatus=%d",
                                obj_info->backEndName, addStatus, updStatus);
    if (beRestart)
    {
        *beRestart = (addStatus > 0 || updStatus > 0);
    }

    if (beName != NULL && beNameSize > 0)
    {
        memset(beName, 0, beNameSize);
        strcpy_safe(beName, obj_info->backEndName, beNameSize);
    }

    return status;
}

/* Configuration discovery and sync (i.e. update config data in backend and DB)
   of the specified one-instance object */
static ep_stat_t w_config_disc_scalar_object(worker_data_t *wd, const char *name,
                                             int *beRestart, char *beName, int beNameSize)
{
    ep_stat_t status = EPS_OK;
    int res = 0, updStatus = 0;
    int param_num = 0, obj_num = 0;
    int cfg_owner = 0;
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    obj_info_t obj_info;

    parsed_param_name_t pn = {{0}};

    char query[EP_SQL_REQUEST_BUF_SIZE] = "SELECT ";
    sqlite3 *dbconn = NULL;
    sqlite3_stmt *stmt = NULL;

    pn.partial_path = FALSE;
    strcpy_safe(pn.obj_name, name, sizeof(pn.obj_name));

    status = w_get_obj_info(wd, &pn, 0, 0, &obj_info, 1, &obj_num);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not get object info");

    /* Get info about all writable parameters of the object */
    if (!obj_info.configurable)
    {
        DBG("One-instance obj %s is not configurable", obj_info.objName);
        return EPS_OK;
    }
    if ((status = w_get_param_info(wd, &pn, &obj_info, 1, param_info,
                                                &param_num, NULL)) != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not get parameter info");

    if (param_num == 0)
    {
        DBG("No action is needed for object %s", name);
        return EPS_OK;
    }

    /* Build SELECT query to get the "config-owner" flag from the values DB*/
    strcat_safe(query,  MMX_CFGOWNER_DBCOLNAME,  sizeof(query));
    strcat_safe(query, " FROM ", sizeof(query));
    strcat_safe(query, obj_info.objValuesTblName, sizeof(query));
    query[strlen(query)] = '\0';

    /*  Establish connection to the values DB of the object and execute query */
    DBG("%s", query);

    dbconn = wd->main_conn;

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL statement: %s",
                            sqlite3_errmsg(dbconn));

    res = sqlite3_step(stmt);
    /*DBG("SQL query result %d, param_num %d, col cnt %d",res, param_num,
                                             sqlite3_column_count(stmt)); */
    if (res != SQLITE_DONE && res != SQLITE_ROW)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(dbconn));

    cfg_owner = sqlite3_column_int(stmt, 0);
    if (cfg_owner == 0)
    {
        DBG("This is system configured object: %s", name);
        goto ret;
    }
    else
    {
        DBG("This is user configured object: %s", name);
    }

    w_getall_updobj_in_be(wd, &obj_info, param_info, param_num, dbconn,
                          NULL, &updStatus);

ret:
    if (stmt) sqlite3_finalize(stmt);

    if (beRestart)
        *beRestart = (updStatus > 0);

    if (beName != NULL && beNameSize > 0)
    {
        memset(beName, 0, beNameSize);
        strcpy_safe(beName, obj_info.backEndName, beNameSize);
    }

    return status;
}


/**************************************************************************/
/*! \fn static ep_stat_t w_getall_sync_db_be_keys(worker_data_t *wd
                 , obj_info_t *obj_info_arr
                 , int obj_num
                 , int idx_params_num
                 , char *idx_params[]
                 , getall_keys_t *dbkeys
                 , getall_keys_t *bekeys
                 , getall_keys_ref_t  *refNewDbKeys
                 , getall_keys_ref_t  *refUpdBeKeys
                 , getall_keys_ref_t  *refAddToBeKeys)
 *  \brief Syncronize data base with backend, fills up keys:
 *             - new
 *             - which should be updated in db
 *             - which should be added to db
 *  \param[in] worker_data_t *wd // Main structure for saving databases, sockets, etc
 *  \param[in] obj_info_t *obj_info_arr // Information about object
 *  \param[in] int obj_num // Amount of objects
 *  \param[in] int_idx_params_num // Amount of udx_param
 *  \param[in] char *idx_params[] // Index params
 *  \param[in] getall_keys_t *dbkeys // Data base keys
 *  \param[in] getall_keys_t *bekeys // Backend keys
 *  \param[in] param_info_t *param_info //  Parameter meta information from management model
 *  \param[in] int param_num // amount of param info
 *  \param[in] parsed_backend_method_t *parsed_method_string // Contains arguments for backend command
 *  \param[out] int addStatus // Status for new element in backend
 *  \param[out] int updStatus // Status for updated elements in backend
 *  \return EPS_OK
 */
/**************************************************************************/
static ep_stat_t w_getall_sync_db_be_keys(worker_data_t *wd
                 , obj_info_t *obj_info_arr
                 , int obj_num
                 , int idx_params_num
                 , char *idx_params[]
                 , getall_keys_t *dbkeys
                 , getall_keys_t *bekeys
                 , param_info_t *param_info
                 , int param_num
                 , parsed_backend_method_t *parsed_method_string
                 , int *addStatus
                 , int *updStatus)
{
    ep_stat_t          status               = EPS_OK;
    int                cmp_res              = 0;
    int                dbrow_pos            = 0;
    int                berow_pos            = 0;
    int                remainingCnt         = 0;
    int                delFromDbCnt         = 0;
    sqlite3           *conn                 = NULL;
    obj_info_t        *obj_info             = obj_info_arr;
    getall_keys_ref_t  refNewDbKeys         = {0};
    getall_keys_ref_t  refUpdBeKeys         = {0};
    getall_keys_ref_t  refAddToBeKeys       = {0};


    /* Compare instances from DB and from backend; decide what to do with them */

    conn = wd->main_conn;

    qsort(&dbkeys->rows
    , dbkeys->rows_num, sizeof(dbkeys->rows[0]), &compare_getall_rows);

    qsort(&bekeys->rows
    , bekeys->rows_num, sizeof(bekeys->rows[0]), &compare_getall_rows);

    while (dbrow_pos < dbkeys->rows_num && berow_pos < bekeys->rows_num)
    {
        cmp_res = compare_getall_rows(&dbkeys->rows[dbrow_pos]
                  , &bekeys->rows[berow_pos]);

        if (cmp_res == 0) /* instance is known both in DB and backend */
        {
            if ( dbkeys->rows[dbrow_pos].create_owner == EP_DATA_OWNER_USER ||
                 dbkeys->rows[dbrow_pos].cfg_owner == EP_DATA_OWNER_USER)
            {
                refUpdBeKeys.rows_ptr[refUpdBeKeys.rows_num++] = &dbkeys->rows[dbrow_pos];
            }
            else
            {
                remainingCnt++;
            }
            dbrow_pos++;
            berow_pos++;
        }
        else if (cmp_res < 0) /*instance known in DB, but unknown in backend*/
        {
            if (obj_info->writable == FALSE)
            {
                /* It's read-only object: backend is "owner" of all instances.
                If the instance does not contain data configured by user,
                it should be deleted from the db.
                Otherwise we need to keep this row (!!!May cause "not-active" row*/
                if (dbkeys->rows[dbrow_pos].cfg_owner == EP_DATA_OWNER_SYSTEM)
                {
                    w_getall_del_row_from_db(wd, (obj_info_t *)obj_info_arr
                    , obj_num, idx_params_num, idx_params, conn
                    , &dbkeys->rows[dbrow_pos]);

                    delFromDbCnt++;
                }
                else
                    remainingCnt++;
            }
            else
            {
                /* Read-write obj: instance can be created by user or by system:
                   if it is user-created instance, add it to the backend,
                   if it is system-created instance, deleted it from the DB. */
                if (dbkeys->rows[dbrow_pos].create_owner == EP_DATA_OWNER_USER)
                {
                    refAddToBeKeys.rows_ptr[refAddToBeKeys.rows_num++] = &dbkeys->rows[dbrow_pos];
                }
                else   /* it is system-created instance */
                {
                    if (dbkeys->rows[dbrow_pos].cfg_owner != EP_DATA_OWNER_USER)
                    {
                        w_getall_del_row_from_db(wd, (obj_info_t *)obj_info_arr
                        , obj_num, idx_params_num, idx_params, conn
                        , &dbkeys->rows[dbrow_pos]);

                        delFromDbCnt++;
                    }
                    else  /* system-created and user-configured instance
                             Keep this instance in the DB. If the instance
                             will be re-created by the system in the backend,
                             EP will update it according to the DB config */
                    {
                        remainingCnt++;
                    }
                }
            }
            dbrow_pos++;
        }
        else /* instance known in the backend, but unknown in the DB */
        {
            /* Instances of read-only objects should always be added to the DB.
               Writable object instances are created mostly by user, but
               sometimes by backend as well (for ex, default config info),
               so we need "to merge instances", i.e. add them to DB */
            refNewDbKeys.rows_ptr[refNewDbKeys.rows_num++] = &bekeys->rows[berow_pos++];
        }
    }  /* End of while over dbrows and berows */

    /* Check remaining rows that are known in the DB, but unknown in backend */
    while (dbrow_pos < dbkeys->rows_num)
    {
        if (obj_info->writable == FALSE)
        {
            if (dbkeys->rows[dbrow_pos].cfg_owner == EP_DATA_OWNER_SYSTEM)
            {
                w_getall_del_row_from_db(wd
                , (obj_info_t *)obj_info_arr
                , obj_num, idx_params_num, idx_params, conn
                , &dbkeys->rows[dbrow_pos]);

                delFromDbCnt++;
            }
            else
            {
                remainingCnt++;
            }
        }
        else /*If writable obj inst was created by user - add it to backend,
               if it was created by system - delete it from the DB         */
        {
            if (dbkeys->rows[dbrow_pos].create_owner == EP_DATA_OWNER_USER)
            {
                refAddToBeKeys.rows_ptr[refAddToBeKeys.rows_num++] = &dbkeys->rows[dbrow_pos];
            }
            else /* it is system-created instance */
            {
                if (dbkeys->rows[dbrow_pos].cfg_owner != EP_DATA_OWNER_USER)
                {
                    /* Instance of writable object created by system is found */
                    w_getall_del_row_from_db(wd
                    , (obj_info_t *)obj_info_arr, obj_num
                    , idx_params_num, idx_params, conn
                    , &dbkeys->rows[dbrow_pos]);

                    delFromDbCnt++;
                }
                else /* system-created and user-configured instance */
                {
                    remainingCnt++;
                }
            }
        }
        dbrow_pos++;
    }

    /* Check remaining rows that are known in the backend, but unknown in DB */
    while (berow_pos < bekeys->rows_num)
    {
        refNewDbKeys.rows_ptr[refNewDbKeys.rows_num++] = &bekeys->rows[berow_pos++];
    }

    /* Sort the rows prepared for adding to DB as they received from backend */
    qsort(refNewDbKeys.rows_ptr
    , refNewDbKeys.rows_num
    , sizeof(refNewDbKeys.rows_ptr[0]), &compare_getall_ref_row_indexes);

    /* And now process all instances according to the above decisions */
    DBG("Instance analysis results: \n\t\t"
        "%d should be updated in be, %d should be added to DB,"
        "%d should be added to be,\n\t\t"
        "%d deleted from DB, %d kept in db as is"
    , refUpdBeKeys.rows_num
    , refNewDbKeys.rows_num
    , refAddToBeKeys.rows_num, delFromDbCnt, remainingCnt);

    w_getall_process_new_to_db(wd
    , obj_info
    , parsed_method_string, idx_params_num, idx_params, conn, &refNewDbKeys);

    w_getall_addobj_to_be(wd
    , obj_info, param_info, param_num, conn, &refAddToBeKeys, addStatus);

    w_getall_updobj_in_be(wd
    , obj_info, param_info, param_num, conn, &refUpdBeKeys, updStatus);

ret:

    return status;
}

/**************************************************************************/
/*! \fn static ep_stat_t w_config_disc_tree(worker_data_t *wd
                        , const char *raw_name
                        , parsed_param_name_t *pn
                        , int *beRestart)
 *  \brief Discovery defined parameters in data model
 *  \param[in] worker_data_t *wd // Main structure for saving databases, sockets, etc
 *  \param[in] const char *raw_name // Full dmcli param. name without formatting
 *  \param[in] parsed_param_name_t *pn // Parsed parameter name from request string
 *  \param[out] int *beRestart // Restart backand if need it
 *  \return EPS_OK if success or error code
 */
/**************************************************************************/
static ep_stat_t w_config_disc_tree(worker_data_t *wd
                 , const char *raw_name
                 , parsed_param_name_t *pn
                 , int *beRestart)
{
    ep_stat_t          status               = EPS_OK;
    int                idx_params_num       = 0;
    int                param_num            = 0;
    int                obj_num              = 0;
    int                addStatus            = 0;
    int                updStatus            = 0;
    const int          obj_info_size        = 1;//19;
    sqlite3           *conn                 = NULL;
    char              *idx_params[MAX_INDECES_PER_OBJECT];
    param_info_t       param_info[MAX_PARAMS_PER_OBJECT];
    obj_info_t         obj_info_arr[MAX_DEPENDED_OBJ_NUM];
    obj_info_t        *obj_info = &obj_info_arr[0];
    getall_keys_t      dbkeys;
    getall_keys_t      bekeys;
    parsed_backend_method_t parsed_method_string;

    memset(&dbkeys, 0, sizeof(getall_keys_t));
    memset(&bekeys, 0, sizeof(getall_keys_t));

    /* Prepare data about object - filling up obj_info structure */
    status = w_get_obj_info(wd, pn, 0, 0, obj_info, obj_info_size, &obj_num);
    if(status != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(status, "Could not get object info");
    }

    if (obj_info->getAllOperStyle == OP_STYLE_SCRIPT)
    {
        /* Filling up parsed_method_string for w_getall_obj_script */
        status = w_parse_backend_method_string(OP_GETALL
                , obj_info->getAllMethod
                , &parsed_method_string);

        if (status != EPS_OK)
        {
            GOTO_RET_WITH_ERROR(status, "Could not w_parse_operation_string");
        }

        /* Execute backend lua script and parse result, filling up  bekeys*/
        status = w_getall_obj_script(wd
                , obj_info, &parsed_method_string, &bekeys, pn);

        if (status != EPS_OK)
        {
            GOTO_RET_WITH_ERROR(status
            , "Could not get all objects from script");
        }
    }
    else
    {
        GOTO_RET_WITH_ERROR(EPS_INVALID_FORMAT
        , "Wrong message type. Supported OP_STYLE_SCRIPT only");
    }

    conn = wd->main_conn;

    /* Get info about writable parameters and indeces */
    status = w_get_param_info(wd
             , pn, obj_info, 1, param_info, &param_num, NULL);

    if (status != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(status, "Could not get parameter info");
    }

    /* Save index names */
    status = get_index_param_names (param_info
             , param_num, idx_params, &idx_params_num);

    if (status != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not get get index param names");
    }
    /* Get all keys from db */
    status = w_fill_dbkeys(wd
             , obj_info
             , &parsed_method_string
             , idx_params_num, idx_params, conn, &dbkeys, pn);

    if (status != EPS_OK)
    {
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not get keys from db");
    }

    DBG("Current instances: %d in db, %d in backend "
    , dbkeys.rows_num, bekeys.rows_num);

    DBG("Instances in the db:");
    print_getall_keys(&dbkeys);

    w_getall_sync_db_be_keys(wd
    , &obj_info_arr[0]
    , obj_num
    , idx_params_num
    , idx_params
    , &dbkeys
    , &bekeys
    , param_info, param_num, &parsed_method_string, &addStatus, &updStatus);


ret:

    DBG("restart status of backend %s: addStatus=%d, updStatus=%d"
    , obj_info->backEndName, addStatus, updStatus);

    if (beRestart)
    {
        *beRestart = (addStatus > 0 || updStatus > 0);
    }

    return status;
}

/* Configuration discovery and sync (i.e. update config data in backend and DB)
   of the multi-instance augment objects (i.e. those having no get-all method) */
static ep_stat_t w_config_disc_augment_object(worker_data_t *wd, const char *name,
                                              parsed_backend_method_t *parsed_method,
                                              int *beRestart, char *beName, int beNameSize)
{
    ep_stat_t status = EPS_OK;
    int updStatus = 0;
    char *idx_params[MAX_INDECES_PER_OBJECT];
    int idx_params_num = 0, param_num = 0, obj_num = 0;
    int dbrow_pos = 0;
    param_info_t param_info[MAX_PARAMS_PER_OBJECT];
    obj_info_t obj_info;

    getall_keys_t dbkeys;
    getall_keys_ref_t refUpdBeKeys;
    parsed_param_name_t pn = {{0}};
    sqlite3 *conn = NULL;

    memset(&dbkeys, 0, sizeof(getall_keys_t));
    memset(&refUpdBeKeys, 0, sizeof(getall_keys_ref_t));

    pn.partial_path = FALSE;
    strcpy_safe(pn.obj_name, name, sizeof(pn.obj_name));
    status = w_get_obj_info(wd, &pn, 0, 0, &obj_info, 1, &obj_num);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not get object info");

    /* Get info about all writable parameters of the object */
    if (!obj_info.configurable)
    {
        //DBG("Object %s is not configurable", obj_info.objName);
        return EPS_OK;
    }

    status = w_get_param_info(wd, &pn, &obj_info, 1, param_info, &param_num, NULL);
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(status, "Could not get parameter info");

    if (param_num == 0)
    {
        DBG("No action is needed for object %s", name);
        return EPS_OK;
    }

    DBG("===== Processing augment obj: `%s' =====", name);
    /* Save index names */
    get_index_param_names (param_info, param_num, idx_params, &idx_params_num);

    conn = wd->main_conn;

    /* Get all keys from db */
    if (w_fill_dbkeys(wd, &obj_info, parsed_method, idx_params_num, idx_params,
                                                       conn, &dbkeys, &pn) != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not get keys from db");

    DBG("Current instances: %d in db", dbkeys.rows_num);
    //DBG("Instances in the db:");      print_getall_keys(&dbkeys);

    for (dbrow_pos = 0; dbrow_pos < dbkeys.rows_num ; dbrow_pos++)
    {
        if (dbkeys.rows[dbrow_pos].create_owner == EP_DATA_OWNER_USER ||
            dbkeys.rows[dbrow_pos].cfg_owner == EP_DATA_OWNER_USER)
        {
            refUpdBeKeys.rows_ptr[refUpdBeKeys.rows_num++] = &dbkeys.rows[dbrow_pos];
        }
    }
    DBG("%d instances should be updated in the backend", refUpdBeKeys.rows_num);
    w_getall_updobj_in_be(wd, &obj_info, param_info, param_num, conn,
                                           &refUpdBeKeys, &updStatus);
ret:
    if (beRestart)
        *beRestart = (updStatus > 0);

    if (beName != NULL && beNameSize > 0)
    {
        memset(beName, 0, beNameSize);
        strcpy_safe(beName, obj_info.backEndName, beNameSize);
    }

    return status;
}

/*  Function w_handle_discover_config
 *  Used as a handler of an external MMX request "DiscoverConfig" (from frontends)
 *   or as an internal function called during other request processing.
 *  In case of this is external request the write lock must be received,
 *   otherwise the write lock is already received by the calling function.
 */
static ep_stat_t w_handle_discover_config(worker_data_t *wd, ep_message_t *message,
                                          BOOL externalReq)
{
    ep_stat_t status = EPS_OK;
    int i, res1;
    char *backendName = (char*)message->body.discoverConfig.backendName;
    char *indexedObjName = (char*)message->body.discoverConfig.objName;

    char *objName = NULL;
    char *objNameFromDb = NULL, *augObjects = NULL;
    char *beNameFromDb = NULL;
    BOOL beSpecified = FALSE;
    BOOL objSpecified = FALSE;
    BOOL objConfigurable = FALSE;
    BOOL write_lock_received = FALSE;
    int updStatus = 0;
    int restart_be[MAX_BACKEND_NUM];
    int num_of_idx;
    char *token, *strtok_ctx;
    parsed_backend_method_t parsed_method = {0};
    unsigned int objs_num = 0;
    parsed_param_name_t pn;

    if (indexedObjName)
    {
        int resCode =  parse_param_name(indexedObjName, &pn);

        objName = pn.obj_name;
        if (resCode != EPS_OK)
        {
            DBG("Couldn't parse indexedObjName\n");
            GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Couldn't parse indexedObjName");
        }
    }


    /* SQL statement for DiscoverConfig request with
     * (1) both Backend name and Object Name
     * (2) OR only Backend name
     * (3) OR only Object name
     * (4) OR none of params - means system-wide DiscoverConfig (all Objects)
     */
    sqlite3_stmt *stmt = NULL;

    ep_message_t answer = {{0}};

    memcpy(&answer.header, &message->header, sizeof(answer.header));
    answer.header.respFlag = 1;
    answer.header.msgType = MSGTYPE_DISCOVERCONFIG_RESP;

    beSpecified = (strlen(backendName) > 0);
    objSpecified = (strlen(objName) > 0);

    memset((char *)restart_be, 0, sizeof(restart_be));

    DBG("Config discovery started (beName = %s, objName = %s, extReq flag = %d)",
         backendName, objName, externalReq);

    if ((status = w_init_mmxdb_handles(wd, MMXDBTYPE_RUNNING, message->header.msgType)) != EPS_OK)
        goto ret;

    /* --- This is write operation. EP write-lock must be received --- */
    if (externalReq)
    {
        if (ep_common_get_write_lock(message->header.msgType, message->header.txaId,
                                     message->header.callerId) == EPS_OK)
            write_lock_received = TRUE;
        else
            GOTO_RET_WITH_ERROR(EPS_RESOURCE_NOT_FREE,
                    "Could not receive write lock for DiscoverConfig operation");
    }

    /* --- Set up SQL statement to get Objects --- */
    if (beSpecified && objSpecified)
    {
        /* Single Object (Backend is also specified) */
        if (sqlite3_prepare_v2(wd->mdb_conn, SQL_QUERY_GET_OBJECT_BY_BACKEND, -1,
                               &stmt, NULL) != SQLITE_OK)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare stmt 1: %s",
                                sqlite3_errmsg(wd->mdb_conn));

        res1 = sqlite3_bind_text(stmt, 1, (const char *)backendName, -1, SQLITE_STATIC);
        if (res1 != SQLITE_OK)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not bind backend name to stmt 1: %s",
                                sqlite3_errmsg(wd->mdb_conn));
        res1 = sqlite3_bind_text(stmt, 2, (const char *)objName, -1, SQLITE_STATIC);
        if (res1 != SQLITE_OK)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not bind object name to stmt 1: %s",
                                sqlite3_errmsg(wd->mdb_conn));
    }
    else
    {
        if (objSpecified) /* Single Object */
        {
            if (sqlite3_prepare_v2(wd->mdb_conn, SQL_QUERY_GET_OBJECT, -1,
                                   &stmt, NULL) != SQLITE_OK)
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare stmt 2: %s",
                                    sqlite3_errmsg(wd->mdb_conn));

            res1 = sqlite3_bind_text(stmt, 1, (const char *)objName, -1, SQLITE_STATIC);
            if (res1 != SQLITE_OK)
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not bind object name to stmt 2: %s",
                                    sqlite3_errmsg(wd->mdb_conn));
        }
        else if (beSpecified) /* Backend's Objects */
        {
            if (sqlite3_prepare_v2(wd->mdb_conn, SQL_QUERY_GET_OBJECTS_BY_BACKEND_BY_INIT_ORDER, -1,
                                   &stmt, NULL) != SQLITE_OK)
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare stmt 3: %s",
                                    sqlite3_errmsg(wd->mdb_conn));

            res1 = sqlite3_bind_text(stmt, 1, (const char *)backendName, -1, SQLITE_STATIC);
            if (res1 != SQLITE_OK)
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not bind backend name to stmt 3: %s",
                                    sqlite3_errmsg(wd->mdb_conn));
        }
        else /* All Objects (system-wide DiscoverConfig) */
        {
            if (sqlite3_prepare_v2(wd->mdb_conn, SQL_QUERY_GET_OBJECTS_BY_INIT_ORDER, -1,
                                   &stmt, NULL) != SQLITE_OK)
                GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare stmt 4: %s",
                                    sqlite3_errmsg(wd->mdb_conn));
        }
    }

    if (stmt == NULL)
    {
        GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR, "Could not set up sql statement");
    }

    /* Go over list of Objects (it maybe just one Object).
     * Objects in list are ordered by column ObjInitOrder. */
    while ((res1 = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        status = EPS_OK;
        updStatus = FALSE;

        /* SQL query is fixed - it fetches next columns:
         *  0 - ObjName, 1 - Configurable, 2 - StyleOfGetAll,
         *  3 - GetAllMethod, 4 - AugmentObjects, 5 - backendName */
        objNameFromDb = (char *)sqlite3_column_text(stmt, 0);
        objConfigurable = (BOOL)sqlite3_column_int(stmt, 1);
        augObjects = (char *)sqlite3_column_text(stmt, 4);
        beNameFromDb = (char *)sqlite3_column_text(stmt, 5);

        if (pn.index_set_num)//"we have indexes"
        {
            status = w_config_disc_tree(wd, indexedObjName, &pn, &updStatus);
            if (status != EPS_OK)
            {
                DBG("Couldn't execute w_config_disc_tree\n");
                GOTO_RET_WITH_ERROR(EPS_SYSTEM_ERROR
                , "Couldn't execute w_config_disc_tree\n");
            }
        }
        else /* Old code*/
        {
            if ((num_of_idx = w_num_of_obj_indeces(objNameFromDb)) == 0)
            {
                if (objConfigurable == TRUE)
                {
                    DBG(" ====== Processing one-inst obj `%s' ======", objNameFromDb);
                    status = w_config_disc_scalar_object(wd, objNameFromDb, &updStatus, NULL, 0);
                }
            }
            else // This is a multi-instance Object
            {
                if (strcmp(beNameFromDb, "rsc_wifi_be")) //TODO: Workaround for fix mmx-wifi-z, need support in model
                {
                    /* If getAll method is defined for the Object,
                    process the Object and it's augmenting Objects */
                    if (sqlite3_column_text(stmt, 2) && sqlite3_column_text(stmt, 3))
                    {
                        DBG(" ===== Processing multi-inst obj `%s' =====", objNameFromDb);
                        status = w_config_disc_object(wd, objNameFromDb, &updStatus, NULL, 0);

                        /* Process augmenting Objects if they exist */
                        if (augObjects != NULL && strlen(augObjects) > 0)
                        {
                            token = strtok_r(augObjects, ",", &strtok_ctx);
                            while (token)
                            {
                                w_config_disc_augment_object(wd, trim(token),
                                        &parsed_method, &updStatus, NULL, 0);
                                token = strtok_r(NULL, ",", &strtok_ctx);
                            }
                        }
                    }
                }
            }
        } // End of multi-instance Object processing

        if (status != EPS_OK)
            WARN("Could not process object (stat %d). Ignore.", status);
        else
        {
            if (updStatus == TRUE)
            {
                if ((i = ep_common_get_beinfo_index(beNameFromDb)) >= 0)
                    restart_be[i] = TRUE;
                else
                    WARN("Could not assign backend %s to restart. Ignore.", beNameFromDb);
            }
        }

        /* Increment Objects (fetched from DB for procesing) counter */
        objs_num++;
    } // End of "while" over found Objects


    if (sqlite3_reset(stmt) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not reset sql statement: %s", sqlite3_errmsg(wd->mdb_conn));

    if (res1 != SQLITE_DONE && res1 != SQLITE_ROW)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not execute query: %s", sqlite3_errmsg(wd->mdb_conn));

    if (objs_num == 0)
    {
        /* Just warning, may it be an error ? */
        WARN("Could not find a single object in DB to process. Ignore.");
    }

ret:
    if (stmt)
        sqlite3_finalize(stmt);

    /* ------- Release EP write operation lock ------- */
    if (write_lock_received)
        ep_common_finalize_write_lock(message->header.txaId,
                                      message->header.callerId, FALSE);

    DBG("Config discovery finished: %lu", status);
    answer.header.respCode = 0; // Always return successful resCode
    w_send_answer(wd, &answer);

    /* Restart backends that were updated and required to restart them */
    w_restart_backends(MSGTYPE_DISCOVERCONFIG, restart_be);

    return status;
}

/* -------------------------------------------------------------------------------
 * ----------- Procesing of scalars on init --------------------------------------
 * -------------------------------------------------------------------------------*/
#define SQL_QUERY_GET_ALL_SCALARS "SELECT [ObjName], [ValuesDbName], [ValuesTblName] \
     FROM [MMX_Objects_InfoTbl] WHERE [ObjName] NOT LIKE '%{i}%' ORDER BY [ValuesDbName] "

#define INSERT_NEW_ROW "INSERT OR IGNORE INTO %s (rowid) VALUES (1)"

/* Function w_handle_init_actions
   Currently init actions include olny
   initialization of main DB rows for all scalar objects.
   Scalar objects are those w/o indeces (i.e w/o {i} place holders in the name)
   If a scalar table is empty, a new row (with default values) will be
   inserted, otherwise insering is ignored
*/
static ep_stat_t w_handle_init_actions(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;
    int  commonCnt = 0, successCnt = 0;
    char query[EP_SQL_REQUEST_BUF_SIZE];
    scalar_info_t  scalInfo;
    sqlite3        *vdb_conn = NULL;
    sqlite3_stmt   *selStmt  = NULL, *insStmt = NULL;

    DBG("Init scalars action started");

    if ((status = w_init_mmxdb_handles(wd, MMXDBTYPE_RUNNING,
                                       message->header.msgType)) != EPS_OK)
        return status;

    vdb_conn = wd->main_conn;

    /* Get list of all scallars. The query selects value of 3 columns */
    if (sqlite3_prepare_v2(wd->mdb_conn, SQL_QUERY_GET_ALL_SCALARS, -1,
                           &selStmt, NULL) != SQLITE_OK)
    {
        ERROR("Could not prepare select scalars stmt: %s", sqlite3_errmsg(wd->mdb_conn));
        return EPS_SQL_ERROR;
    }

    while (sqlite3_step(selStmt) == SQLITE_ROW)
    {
        commonCnt++;
        memset(&scalInfo, 0, sizeof(scalInfo));
        strcpy_safe(scalInfo.objName, (char *)sqlite3_column_text(selStmt, 0), sizeof(scalInfo.objName));
        strcpy_safe(scalInfo.objValuesDbName, (char *)sqlite3_column_text(selStmt, 1), sizeof(scalInfo.objValuesDbName));
        strcpy_safe(scalInfo.objValuesTblName, (char *)sqlite3_column_text(selStmt, 2), sizeof(scalInfo.objValuesTblName));
        //DBG("Scalar object name: %s (%s, %s)", scalInfo.objName, scalInfo.objValuesDbName, scalInfo.objValuesTblName);

        /* Create connection to the appropriated values db */
        if ((strlen(scalInfo.objValuesDbName) == 0) ||
            (strlen(scalInfo.objValuesTblName) == 0))
        {
            //DBG("Values DB or table not specified for scalar obj %s", scalInfo.objName);
            continue;
        }

        /* Insert a new row with rowid=1 in the value DB table */
        sprintf(query, INSERT_NEW_ROW, (char*)&scalInfo.objValuesTblName);
        if (sqlite3_prepare_v2(vdb_conn, query, -1, &insStmt, NULL) == SQLITE_OK)
        {
            if (sqlite3_step(insStmt) != SQLITE_DONE)
            {
                DBG("Cannot insert row to scalar tbl %s (%s)",
                     (char*)&scalInfo.objValuesTblName, sqlite3_errmsg(vdb_conn));
            }
            else
            {
                //DBG("Successful insert to %s",(char*)&scalInfo.objValuesTblName);
                successCnt++;
            }
        }
        else
        {
            DBG("Can't compile query - error: %s", sqlite3_errmsg(vdb_conn));
        }

        sqlite3_finalize(insStmt);

    }  //End of while cycle over all scalar objects

    sqlite3_finalize(selStmt);

    DBG("Init scalars completed: %d objects of %d were processed",successCnt, commonCnt);
    return EPS_OK;
}


static ep_stat_t w_handle_reboot(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;
    int delay_time;

    delay_time = message->body.reboot.delaySeconds;

    status = w_reboot(wd, delay_time, NULL);

    return status;
}

 /* TODO -Think do we need delay time in factory reset request? */
static ep_stat_t w_handle_reset(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;
    int rst_type = 0; // 0 is factory reset
    int delay_time;

    rst_type = message->body.reset.resetType;
    delay_time = message->body.reset.delaySeconds;

    status = w_reset(wd, delay_time, rst_type, NULL);

    return status;
}

static BOOL isMsgTypeCorrect(int msgType)
{
    BOOL res = FALSE;

    switch (msgType)
    {
        case MSGTYPE_GETVALUE:
        case MSGTYPE_SETVALUE:
        case MSGTYPE_GETPARAMNAMES:
        case MSGTYPE_ADDOBJECT:
        case MSGTYPE_DELOBJECT:
        case MSGTYPE_DISCOVERCONFIG:
        case MSGTYPE_INITACTIONS:
        case MSGTYPE_REBOOT:
        case MSGTYPE_RESET:
            res = TRUE;
            break;
        default:
            res = FALSE;
            break;
    }

    return res;
}

static ep_stat_t w_free_metadb_info(worker_data_t *wd)
{
    DBG("Free meta db info for db type %d", wd->mmxDbType);

    if (wd->stmt_get_obj_info) {
        sqlite3_finalize(wd->stmt_get_obj_info);
        wd->stmt_get_obj_info = NULL;
    }
    if (wd->stmt_get_obj_list) {
        sqlite3_finalize(wd->stmt_get_obj_list);
        wd->stmt_get_obj_list = NULL;
    }

    if (wd->mdb_conn) {
        sql_closeConnection(wd->mdb_conn);
        wd->mdb_conn = NULL;
    }

    wd->mmxDbType = 0;

    return EPS_OK;
}

static ep_stat_t w_free_maindb_info(worker_data_t *wd)
{
    DBG("Free main db info for db type %d", wd->mmxDbType);

    if (wd->main_conn)
    {
        sql_closeConnection(wd->main_conn);
        wd->main_conn = NULL;
    }

    return EPS_OK;
}

static ep_stat_t w_init_mmxdb_handles(worker_data_t *wd, int dbType, int msgType)
{
    ep_stat_t status = EPS_OK;
    BOOL setAddDelRequest = FALSE;

    /*DBG("Input DB type is %d (%s), existing DB type is %d (%s)", dbType,
        mmxdbtype_num2str(dbType), wd->mmxDbType, mmxdbtype_num2str(wd->mmxDbType));*/

    if ((dbType == wd->mmxDbType) &&
        (wd->mdb_conn != NULL) && (wd->main_conn != NULL))
    {
        DBG ("Connections to MMX meta and main db type %d (%s) already exist",
                                           dbType, mmxdbtype_num2str(dbType));
        return EPS_OK;
    }

    setAddDelRequest = (msgType == MSGTYPE_SETVALUE) ||
                       (msgType == MSGTYPE_ADDOBJECT) ||
                       (msgType == MSGTYPE_DELOBJECT) ;

    /* Free existing connections to the meta and main DBs */
    w_free_maindb_info(wd);
    w_free_metadb_info(wd);

    /* Create a new connection to the main and meta DB and prepare the
       neccessary often used statements */
    status = sql_getDbConnPerDbType(&(wd->main_conn),"mmx_main_db", dbType);

    if ((status != EPS_OK) && (status != EPS_CANNOT_OPEN_DB))
        GOTO_RET_WITH_ERROR(EPS_CANNOT_OPEN_DB,"Could not get connection to main DB");

    if (status == EPS_OK)
    {
        status = sql_getDbConnPerDbType(&(wd->mdb_conn),"mmx_meta_db", dbType);

        /* Almost impossible case: main db conn is OK, but meta db cannot be opened*/
        if (status != EPS_OK)
            w_free_maindb_info(wd);
    }

    /* If MMX requests like set,addobj,delobj is called for the cand db,
       but the cand DB does not exist yet, we will create it just now and
       try again to get connection */
    if ((status == EPS_CANNOT_OPEN_DB) && (dbType == MMXDBTYPE_CANDIDATE) &&
        (setAddDelRequest == TRUE))
    {
        w_save_config_to_candidate(wd,NULL);
        status = sql_getDbConnPerDbType(&(wd->mdb_conn), "mmx_meta_db", dbType);
        if (status == EPS_OK)
            status = sql_getDbConnPerDbType(&(wd->main_conn), "mmx_main_db", dbType);
    }
    if (status != EPS_OK)
        GOTO_RET_WITH_ERROR(EPS_CANNOT_OPEN_DB, "Could not get connection to MMX DB, no DB files");

    if (sqlite3_prepare_v2(wd->mdb_conn, SQL_QUERY_GET_OBJ_INFO, -1,
                           &(wd->stmt_get_obj_info), NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR,"Could not prepare stmt 1: %s",
                            sqlite3_errmsg(wd->mdb_conn));

    if (sqlite3_prepare_v2(wd->mdb_conn, SQL_QUERY_GET_OBJ_LIST_INFO, -1,
                           &(wd->stmt_get_obj_list), NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare stmt 2: %s",
                            sqlite3_errmsg(wd->mdb_conn));

    wd->mmxDbType = dbType;

ret:
    if (status != EPS_OK)
    {
        w_free_metadb_info(wd);
        w_free_maindb_info(wd);
    }

    return status;
}

static ep_stat_t w_handle_msg(worker_data_t *wd, ep_message_t *message)
{
    ep_stat_t status = EPS_OK;

    if (isMsgTypeCorrect(message->header.msgType) == FALSE)
    {
        DBG("Incorrect message type: %d. Ignore", message->header.msgType);
        return EPS_NOT_FOUND;
    }

    switch (message->header.msgType)
    {
    case MSGTYPE_GETVALUE:
        status = w_handle_getvalue(wd, message);
        break;
    case MSGTYPE_SETVALUE:
        status = w_handle_setvalue(wd, message);
        break;
    case MSGTYPE_GETPARAMNAMES:
        status = w_handle_getparamnames(wd, message);
        break;
    case MSGTYPE_ADDOBJECT:
        status = w_handle_addobject(wd, message);
        break;
    case MSGTYPE_DELOBJECT:
        status = w_handle_delobject(wd, message);
        break;
    case MSGTYPE_DISCOVERCONFIG:
        status = w_handle_discover_config(wd, message, TRUE);
        break;
    case MSGTYPE_INITACTIONS:
        status = w_handle_init_actions(wd, message);
        break;
    case MSGTYPE_REBOOT:
        status = w_handle_reboot(wd, message);
        break;
    case MSGTYPE_RESET:
        status = w_handle_reset(wd, message);
        break;
    default:
        GOTO_RET_WITH_ERROR(EPS_IGNORED,"Incorrect message type: %d", message->header.msgType);
        break;
    }

ret:
    return status;
}

static ep_stat_t w_init(worker_data_t *wd)
{
    char buf[FILENAME_BUF_LEN];
    struct timeval timeout;

    wd->be_req_cnt = 0;
    wd->self_w_num = tiddb_get_w_num();

    /* Create UDP and IPC sockets for the EP worker thread */
    wd->udp_port = EP_PORT_STARTNUM + wd->self_w_num;
    if (udp_socket_init(&(wd->udp_sock), MMX_EP_ADDR, wd->udp_port) != ING_STAT_OK)
    {
        ERROR("Could not create UDP socket for worker");
        return EPS_SYSTEM_ERROR;
    }

    wd->udp_be_port = EP_PORT_STARTNUM + wd->self_w_num + 4;
    if (udp_socket_init(&(wd->udp_be_sock), MMX_EP_BE_ADDR, wd->udp_be_port) != ING_STAT_OK)
    {
        ERROR("Could not create UDP socket for worker");
        return EPS_SYSTEM_ERROR;
    }

    DBG ("Created UDP sock for ep worker - port %d", EP_PORT_STARTNUM + wd->self_w_num);

    /* set timeout on udp socket */
    timeout.tv_sec = UDP_SOCK_TIMEOUT;
    timeout.tv_usec = 0;
    if (setsockopt(wd->udp_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                                                           sizeof(timeout)) < 0)
    {
        ERROR("Could not set timeout");
        return EPS_SYSTEM_ERROR;
    }

    if (setsockopt(wd->udp_be_sock, SOL_SOCKET, SO_RCVTIMEO, (char *)&timeout,
                                                           sizeof(timeout)) < 0)
    {
        ERROR("Could not set timeout");
        return EPS_SYSTEM_ERROR;
    }

    memset(buf, 0, sizeof(buf));
    strcpy_safe(buf, SUN_PATH, sizeof(buf));
    strcat_safe(buf, tiddb_get(), sizeof(buf));
    if (unix_socket_init_full(&(wd->ipc_sock), buf, &(wd->addr), &(wd->addr_len)) != ING_STAT_OK)
    {
        ERROR("Could not create UNIX socket");
        return EPS_SYSTEM_ERROR;
    }

    return EPS_OK;
}

static ep_stat_t w_destroy(worker_data_t *wd)
{
    w_free_metadb_info(wd);

    close(wd->udp_sock); wd->udp_sock = 0;
    close(wd->udp_sock); wd->udp_be_sock = 0;
    close(wd->ipc_sock); wd->ipc_sock = 0;

    return EPS_OK;
}

/* Prepare worker data when a new task is got */
static ep_stat_t w_task_init(worker_data_t *wd)
{
    memset(wd->answer_buf, 0, sizeof(wd->answer_buf));
    memset(wd->fe_req_values_pool, 0, sizeof(wd->fe_req_values_pool));
    memset(wd->fe_resp_values_pool, 0, sizeof(wd->fe_resp_values_pool));
    memset(wd->be_req_values_pool, 0, sizeof(wd->be_req_values_pool));

    return EPS_OK;
}

void *tp_worker(void *data)
{
    ep_stat_t status = EPS_OK;
    tp_threadpool_t *tp = (tp_threadpool_t *)data;
    tp_task_t task;
    worker_data_t wd = {0};
    ep_message_t message;

    tiddb_add(NULL);

    INFO("Thread %lu started", pthread_self());

    if (w_init(&wd) != EPS_OK)
    {
        ERROR("Could not initialize worker. Exiting thread");
        return NULL;
    }

    while (TRUE)
    {
        if (tp->stopped)
            break;

        if (tp_get_task(tp, &task) != EPS_OK)
        {
            if (!tp->stopped) ERROR("Could not get task. Trying again");
            continue;
        }

        DBG("Got task. Working on it");

        w_task_init(&wd);
        memset((char *)&message, 0, sizeof(message));
        mmx_frontapi_msg_struct_init(&message, (char *)wd.fe_req_values_pool,
                                     sizeof(wd.fe_req_values_pool));
        if (task.task_type == TASK_TYPE_RAW)
        {
            /* DBG("Raw message. Parsing..."); */
            if (mmx_frontapi_message_parse(task.raw_message, &message) != EPS_OK)
            {
                ERROR("Could not parse raw message");
                continue;
            }

            DBG("-------- Message %s parsed --------", msgtype2str(message.header.msgType));
            status = w_handle_msg(&wd, &message);
            DBG("-------- Message %s has been processed (status %d) --------",
                                msgtype2str(message.header.msgType), status);
        }
        else if (task.task_type == TASK_TYPE_SUBTASK)
        {
            DBG("Subtask. Performing...");
            // TODO do job
        }
        else
        {
            ERROR("Incorrect task type");
        }
    }

    w_destroy(&wd);

    INFO("Exiting");

    return NULL;
}
