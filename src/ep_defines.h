/* ep_defines.h
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
 * These values can be redefined on compilation
 */

#ifndef DEFINES_H_
#define DEFINES_H_

/*
 * DB
 */
#ifndef MAX_OBJECTS_NUM
#   define MAX_OBJECTS_NUM 128
#endif

#ifndef MAX_PARAMS_PER_OBJECT
#   define MAX_PARAMS_PER_OBJECT     48 //80
#endif

/* Indices are numeric values inside param name */
#ifndef MAX_INDECES_PER_OBJECT
#   define MAX_INDECES_PER_OBJECT    4 //5
#endif

/*
 * MMX Objects dependencies
 */

/* Max count of dependencies of one dependency type per object */
#ifndef MAX_DEPCOUNT_PER_OBJECT
#   define MAX_DEPCOUNT_PER_OBJECT   16               /* TODO optimal value ? */
#endif

/* Max depth (inheritance) of the object's dependency */
#ifndef MAX_DEPDEPTH_PER_OBJECT
#   define MAX_DEPDEPTH_PER_OBJECT   4                /* TODO optimal value ? */
#endif


/* Max number of backends supported by MMX */
#ifndef MAX_BACKEND_NUM
#   define MAX_BACKEND_NUM           40
#endif

/* Max number of symbols in a backend name */
#ifndef MAX_BACKEND_NAMELEN
#   define MAX_BACKEND_NAMELEN       32
#endif


/* Path to directory where all MMX DBs are located during runtime */
#ifndef DB_PATH
#   define DB_PATH getenv("INANGOBASEPATH")
#endif

/* Path to directory where saved MMX DBs (startup DB) are located */
#ifndef SAVEDDB_PATH
#   define SAVEDDB_PATH getenv("MMXSAVEDDBPATH")
#endif

/* Path to directory where the "candidate" MMX DBs are located */
#ifndef CANDDB_PATH
#   define CANDDB_PATH getenv("MMXCANDDBPATH")
#endif

/* Path to directory with UCI config files */
#ifndef MMXCONFIG_PATH
#   define MMXCONFIG_PATH getenv("MMXUCICONFIGPATH")
#endif

/* Path to directory where saved config files are located */
#ifndef SAVEDCONFIG_PATH
#   define SAVEDCONFIG_PATH getenv("MMXSAVEDCONFIGPATH")
#endif

/* Name of file containing type of DB used for EP start */
#ifndef DBSTARTTYPE_FILE
#   define DBSTARTTYPE_FILE getenv("MMX_START_DBTYPE_FILE")
#endif

/* DB journal mode */
#ifndef MMX_DB_JOURNAL_MODE
#   define MMX_DB_JOURNAL_MODE getenv("MMX_DB_JOURNAL_MODE")
#endif

/* DB synchronous */
#ifndef MMX_DB_SYNCHRONOUS
#   define MMX_DB_SYNCHRONOUS getenv("MMX_DB_SYNCHRONOUS")
#endif

/* timeout of all sql operations */
#ifndef SQL_TIMEOUT
#   define SQL_TIMEOUT (5*1000) /* (sec*1000) */
#endif


#ifndef USE_SYSLOG
#   define USE_SYSLOG 0
#endif

#ifndef DEBUG
#   define DEBUG 1
#endif

#ifndef TP_MAX_TASK_QUEUE_SIZE
#   define TP_MAX_TASK_QUEUE_SIZE 32 //128
#endif

#endif /* DEFINES_H_ */
