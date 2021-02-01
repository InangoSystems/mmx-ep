/* ep_db_utils.h
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

#ifndef EP_DB_UTILS_H_
#define EP_DB_UTILS_H_

#include <sqlite3.h>

#include "ep_common.h"

/*
 * Opens connection to SQLite db and sets timeout and journal mode = truncate
 */
ep_stat_t sql_getDbConn(sqlite3 **xo_conn, char *xi_db_name);

ep_stat_t sql_getDbConnPerDbType(sqlite3 **xo_conn, char *xi_db_name, int xi_db_type);

/*
 * Closes SQLite connection
 */
ep_stat_t sql_closeConnection(sqlite3 *conn);


/*
 * This is "wrapper" on sqlite3 api for performing SQL write operation:
 *   UPDATE, INSERT, DELETE
 *  (Note. This function is workaround.
 *         It replaces sqlite3_exec function, because in our version of
 *         sqlite3 library the sqlite3_exec does not work correctly.
 *         When any "write" request is not successful, the db remains locked.)
 *
 * Output parameter 'modified_rowNum' returns the number of rows modified,
 * inserted or deleted by the completed UPDATE, INSERT or DELETE statement.
 */
ep_stat_t ep_db_exec_write_query(sqlite3 *dbconn, char *query, int *modified_rowNum);

/*
 *  Helper function that returns number of entries in the
 *  specified table of the specified DB
 */
ep_stat_t ep_db_get_tbl_row_count(sqlite3 *dbconn, char *tbl_name, int *rowNum);

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
                    char *where_cond, exact_indexvalues_set_t *indexvalues_set);

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
                    char *column_name, char *column_value, int column_value_maxlen);

#endif /* EP_DB_UTILS_H_ */
