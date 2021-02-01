/* ep_db_utils.c
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
#include <sqlite3.h>
#include <string.h>

#include "ep_common.h"

static const char DEFAULT_JOURNAL_MODE[] = "truncate";
static const char DEFAULT_SYNCHRONOUS[]  = "full";

#define MAX_COMMAND_SIZE  256

static ep_stat_t sql_openConn(sqlite3 **xo_conn, char *xi_db_name)
{
    const char *setting;
    char command[MAX_COMMAND_SIZE] = {0};

    int res = sqlite3_open_v2(xi_db_name, xo_conn, SQLITE_OPEN_READWRITE, NULL);

    if (res)
    {
        ERROR("Can't open db %s (err %d)", xi_db_name, res);
        return EPS_CANNOT_OPEN_DB;
    }

    /* set timeout */
    if (sqlite3_busy_timeout(*xo_conn, SQL_TIMEOUT))
    {
        ERROR("Can't set timeout. Proceed anyway");
    }

    /* set journal_mode */
    setting = MMX_DB_JOURNAL_MODE;
    if (!setting)
    {
        WARN("No environment setting for MMX_DB_JOURNAL_MODE. Using default: %s.", DEFAULT_JOURNAL_MODE);
        setting = DEFAULT_JOURNAL_MODE;
    }

    snprintf(command, sizeof(command), "PRAGMA journal_mode=%s;", setting);
    if (sqlite3_exec(*xo_conn, command, NULL, 0, NULL))
    {
        ERROR("Can't set journal_mode pragma. Proceed anyway.");
    }

    /* set synchronous */
    setting = MMX_DB_SYNCHRONOUS;
    if (!setting)
    {
        WARN("No environment setting for MMX_DB_SYNCHRONOUS. Using default: %s.", DEFAULT_SYNCHRONOUS);
        setting = DEFAULT_SYNCHRONOUS;
    }

    snprintf(command, sizeof(command), "PRAGMA synchronous=%s;", setting);
    if (sqlite3_exec(*xo_conn, command, NULL, 0, NULL))
    {
        ERROR("Can't set synchronous pragma. Proceed anyway.");
    }

    return EPS_OK;
}

/*
 * Opens connection to specified db and sets timeout
 *  and journal mode = truncate
 */
ep_stat_t sql_getDbConn(sqlite3 **xo_conn, char *xi_db_name)
{
    ep_stat_t stat = sql_openConn(xo_conn, xi_db_name);

    if (stat == EPS_OK)
    {
        DBG("DB connection to %s successfully established", xi_db_name);
    }

    return stat;
}

/* Create connection with the specified data base of the specified
 * DB type (running, startup, candidate)
 *    xo_conn  - connection to the DB
 *    xi_db_name - name of the DB file without path
 *    xi_db_type - type of the DB: running, startup, candidate
 *                 the path to the DB will be set according to the DB type
 */
ep_stat_t sql_getDbConnPerDbType(sqlite3 **xo_conn, char *xi_db_name, int xi_db_type)
{
    ep_stat_t stat;
    char buf[FILENAME_BUF_LEN] = {0};

    if ( !xi_db_name || strlen(xi_db_name) == 0 ||
         strlen(xi_db_name) >= FILENAME_BUF_LEN - 32)
    {
        ERROR("Cannot get DB conn. Bad DB name %s", xi_db_name ? " " : xi_db_name);
        return EPS_GENERAL_ERROR;
    }

    get_db_path_by_dbtype(buf, sizeof(buf), xi_db_type);
    strcat_safe(buf, xi_db_name, sizeof(buf));

    stat = sql_openConn(xo_conn, buf);
    if (stat == EPS_OK)
    {
        DBG("DB connection to %s (type %d) successfully established", buf, xi_db_type);
    }

    return stat;
}

ep_stat_t sql_closeConnection(sqlite3 *conn)
{
    return sqlite3_close(conn) == 0 ? EPS_OK : EPS_SQL_ERROR;
}


/*
 * This is "wrapper" on sqlite3 api for performing SQL write operation:
 *   UPDATE, INSERT, DELETE
 *  (Note. This function is workaround.
 *         It replaces sqlite3_exec function, because in our version of
 *         sqlite3 library the sqlite3_exec does not work correctly.
 *         When any "write" request is not successful, the db remains locked.)
 * Output parameter 'modified_rowNum' returns the number of rows modified,
 * inserted or deleted by the completed UPDATE, INSERT or DELETE statement.
 *
 */

 /* TODO !!!! Thing about parallel access to DB from different worker-threads
               May be SQLITE mutex usage is needed here !!!  */
ep_stat_t ep_db_exec_write_query(sqlite3 *dbconn, char *query, int *modified_rowNum)
{
    ep_stat_t status = EPS_OK;
    int res = 0;
    sqlite3_stmt *stmt = NULL;

    if (!query || (strlen(query) == 0))
    {
        status = EPS_NOTHING_DONE;
        DBG("Null or null-length SQL query string was passed - nothing done");
    }
    else /* Perform the input query */
    {
        //DBG("Write SQL query:\n   %s", query);
        if ((res = sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL)) != SQLITE_OK)
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Couldn't prepare write SQL stmt: err %d - %s",
                                                res, sqlite3_errmsg(dbconn));

        res = sqlite3_step(stmt);
        if ((res != SQLITE_OK) && (res != SQLITE_DONE))
            GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not perform SQLite step: err %d - %s",
                                                res, sqlite3_errmsg(dbconn));
        *modified_rowNum = sqlite3_changes(dbconn);
        /* DBG("Result of write SQL query: %d (success, modified %d row(s))", res,
            *modified_rowNum); */
    }

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}


/*
 *  Helper function that returns number of entries in the specified table
 */
ep_stat_t ep_db_get_tbl_row_count(sqlite3 *dbconn, char *tbl_name, int *rowNum)
{
    ep_stat_t status = EPS_OK;
    int       res = 0;
    char      query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    sqlite3_stmt *stmt = NULL;

    sprintf(query, "SELECT COUNT(*) FROM %s", tbl_name);
    //DBG("Query:   %s", query);

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not prepare SQL stmt: %s",
                                            sqlite3_errmsg(dbconn));

    res = sqlite3_step(stmt);
    if (res == SQLITE_ROW)
        *rowNum = sqlite3_column_int(stmt, 0);
    else
        GOTO_RET_WITH_ERROR(EPS_SQL_ERROR, "Could not get row count from table %s (err %s)",
                                            tbl_name, sqlite3_errmsg(dbconn));

    //DBG ("Table %s contains %d entries (res code = %d)", tbl_name, *rowNum, res);

ret:
    if (stmt) sqlite3_finalize(stmt);
    return status;
}

/*
 *  Returns (within the output arg *indexvalues_set) the array of Object instances
 *   - just index values of each instance. Information is fetched from Values DB
 *   by running SQL query formed accordingly to input args:
 *     tbl_name            - table of the Object in the Values DB
 *     index_num           - number of Object indexes
 *     index_params        - names of index parameters
 *     index_param_values  - exact/ranged/wildcard values of index parameters
 *                           (needed to add WHERE condition to SELECT query)
 *     where_cond (optional) - ready WHERE condition for SELECT query
 */
ep_stat_t ep_db_get_tbl_row_indexes(sqlite3 *dbconn, char *tbl_name, int index_num,
                    char *index_params[], param_name_index_t index_param_values[],
                    char *where_cond, exact_indexvalues_set_t *indexvalues_set)
{
    sqlite3_stmt *stmt = NULL;

    char query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    int i, size, res, query_size = EP_SQL_REQUEST_BUF_SIZE;

    if (index_num < 0 || index_num > MAX_INDECES_PER_OBJECT)
    {
        ERROR("Invalid input - indexes number (=%d) must be: 1..%d",
               index_num, MAX_INDECES_PER_OBJECT);
        return EPS_INVALID_ARGUMENT;
    }

    /* Begin preparation of the query */
    strcpy_safe(query, "SELECT ", query_size);
    for (i = 0; i < index_num; i++)
    {
        if (i + 1 == index_num)
            snprintf(query+strlen(query), query_size, "%s ", index_params[i]);
        else
            snprintf(query+strlen(query), query_size, "%s, ", index_params[i]);
    }
    strcat_safe(query, " FROM ", query_size);
    strcat_safe(query, tbl_name, query_size);

    /* Add WHERE condition to the formed query ( SELECT ... FROM <TABLE> ) */
    if (where_cond && strlen(where_cond))
    {
        /* WHERE condition was passed in function args */
        strcat_safe(query, " ", query_size);
        strcat_safe(query, where_cond, query_size);
    }
    else
    {
        /* WHERE condition to be made of index-param = index-value */
        strcat_safe(query, " WHERE 1 ", query_size);

        for (i = 0; i < index_num; i++)
        {
            if (index_param_values[i].type == REQ_IDX_TYPE_PLACEHOLDER)
            {
                /* Placeholder - no WHERE criterion added to SQL query */
                continue;
            }

            if (i + 1 == index_num)
            {
                /* Only last index-param is promoted to support any of:
                 *  REQ_IDX_TYPE_EXACT, REQ_IDX_TYPE_RANGE, REQ_IDX_TYPE_ALL */
                if (index_param_values[i].type == REQ_IDX_TYPE_EXACT)
                {
                    snprintf(query+strlen(query), query_size, "AND %s = %d ",
                             index_params[i], index_param_values[i].exact_val.num);
                }
                else if (index_param_values[i].type == REQ_IDX_TYPE_RANGE)
                {
                    snprintf(query+strlen(query), query_size, "AND %s >= %d ",
                             index_params[i], index_param_values[i].range_val.begin);
                    snprintf(query+strlen(query), query_size, "AND %s <= %d ",
                             index_params[i], index_param_values[i].range_val.end);
                }
                else /* REQ_IDX_TYPE_ALL */
                {
                    /* Wildcard - no WHERE criterion added to SQL query */
                }
            }
            else
            {
                /* Not last index-param is promoted to support only REQ_IDX_TYPE_EXACT */
                if (index_param_values[i].type != REQ_IDX_TYPE_EXACT)
                {
                    ERROR("Invalid input - index-param (%d) must be an EXACT Index",
                           index_params[i]);
                    return EPS_INVALID_ARGUMENT;
                }

                snprintf(query+strlen(query), query_size, "AND %s = %d ",
                         index_params[i], index_param_values[i].exact_val.num);
            }
        }
    }

    DBG("======== Prepared SQL query (len %d) ========\n\t %s", strlen(query), query);

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
    {
        ERROR("Could not prepare SQL statement: %s", sqlite3_errmsg(dbconn));
        return EPS_SQL_ERROR;
    }

    indexvalues_set->inst_num = 0;
    indexvalues_set->index_num = index_num;

    /* Run query and save each instance indexes to index-values set */
    while (TRUE)
    {
        res = sqlite3_step(stmt);

        if (res == SQLITE_ROW)
        {
            if (indexvalues_set->inst_num >= MAX_INSTANCES_PER_OBJECT)
            {
                WARN("Max Object instances limit reached (stopping the query)");
                break;
            }

            /* Save the indexes of the Object instance */
            size = indexvalues_set->inst_num;
            for (i = 0; i < index_num; i++)
            {
                if (index_param_values[i].type == REQ_IDX_TYPE_EXACT)
                    indexvalues_set->indexvalues[size][i] = index_param_values[i].exact_val.num;
                else
                    indexvalues_set->indexvalues[size][i] = sqlite3_column_int(stmt, i);
            }
            /* Increment number of saved instances */
            indexvalues_set->inst_num++;
        }
        else if (res == SQLITE_DONE)
        {
            /* Statement is complete */
            break;
        }
        else
        {
            if (stmt) sqlite3_finalize(stmt);
            ERROR("Could not execute query: %s", sqlite3_errmsg(dbconn));
            return EPS_SQL_ERROR;
        }
    }

    if (stmt) sqlite3_finalize(stmt);

    return EPS_OK;
}

/*
 *  Returns (within the output arg *column_value) the value of requested column
 *   (column_name) for specified table's (tbl_name) row (row is defined with
 *   index columns names (index_params) and values (index_param_values)).
 *   Information is fetched from Values DB by running SQL query:
 *
 *      SELECT <column_name> FROM <tbl_name> WHERE 1 AND \
 *       <index_params[0]> = <index_param_values[0]> AND \
 *       ...
 *       <index_params[index_num-1]> = <index_param_values[index_num-1]>
 *
 *  Regardless of the actual column data-type (in db table schema) - column value
 *   is returned in string format (char *column_value).
 */
ep_stat_t ep_db_get_tbl_row_column(sqlite3 *dbconn, char *tbl_name, int index_num,
                    char *index_params[], int index_param_values[],
                    char *column_name, char *column_value, int column_value_maxlen)
{
    sqlite3_stmt *stmt = NULL;

    char query[EP_SQL_REQUEST_BUF_SIZE] = {0};
    int i, res, query_size = EP_SQL_REQUEST_BUF_SIZE;

    if (index_num < 0 || index_num > MAX_INDECES_PER_OBJECT)
    {
        ERROR("Invalid input - indexes number (=%d) must be: 1..%d",
               index_num, MAX_INDECES_PER_OBJECT);
        return EPS_INVALID_ARGUMENT;
    }

    /* Begin preparation of the query */
    snprintf(query, query_size, "SELECT %s FROM %s ", column_name, tbl_name);

    /* WHERE condition to be made of index-param = index-value */
    strcat_safe(query, " WHERE 1 ", query_size);
    for (i = 0; i < index_num; i++)
    {
        snprintf(query+strlen(query), query_size, "AND %s = %d ",
                 index_params[i], index_param_values[i]);
    }


    DBG("======== Prepared SQL query (len %d) ========\n\t %s", strlen(query), query);

    if (sqlite3_prepare_v2(dbconn, query, -1, &stmt, NULL) != SQLITE_OK)
    {
        ERROR("Could not prepare SQL statement: %s", sqlite3_errmsg(dbconn));
        return EPS_SQL_ERROR;
    }

    i = 0;
    /* Run query and save fetched column value */
    while (TRUE)
    {
        res = sqlite3_step(stmt);

        if (res == SQLITE_ROW)
        {
            if (i)
            {
                WARN("Expected single row - got multiple >> break");
                break;
            }

            strcpy_safe(column_value, (char *)sqlite3_column_text(stmt, 0),
                        column_value_maxlen);

            i++;
        }
        else if (res == SQLITE_DONE)
        {
            /* Statement is complete */
            break;
        }
        else
        {
            if (stmt) sqlite3_finalize(stmt);
            ERROR("Could not execute query: %s", sqlite3_errmsg(dbconn));
            return EPS_SQL_ERROR;
        }
    }

    if (stmt) sqlite3_finalize(stmt);

    return EPS_OK;
}
