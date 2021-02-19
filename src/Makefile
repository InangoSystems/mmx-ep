################################################################################
#
# Makefile
#
# Copyright (c) 2013-2021 Inango Systems LTD.
#
# Author: Inango Systems LTD. <support@inango-systems.com>
# Creation Date: 01 Jan 2013
#
# The author may be reached at support@inango-systems.com
# 
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
# 
# 1. Redistributions of source code must retain the above copyright notice,
# this list of conditions and the following disclaimer.
# 
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation
# and/or other materials provided with the distribution.
# 
# Subject to the terms and conditions of this license, each copyright holder
# and contributor hereby grants to those receiving rights under this license
# a perpetual, worldwide, non-exclusive, no-charge, royalty-free, irrevocable
# (except for failure to satisfy the conditions of this license) patent license
# to make, have made, use, offer to sell, sell, import, and otherwise transfer
# this software, where such license applies only to those patent claims, already
# acquired or hereafter acquired, licensable by such copyright holder or contributor
# that are necessarily infringed by:
# 
# (a) their Contribution(s) (the licensed copyrights of copyright holders and
# non-copyrightable additions of contributors, in source or binary form) alone;
# or
# 
# (b) combination of their Contribution(s) with the work of authorship to which
# such Contribution(s) was added by such copyright holder or contributor, if,
# at the time the Contribution is added, such addition causes such combination
# to be necessarily infringed. The patent license shall not apply to any other
# combinations which include the Contribution.
# 
# Except as expressly stated above, no rights or licenses from any copyright
# holder or contributor is granted under this license, whether expressly, by
# implication, estoppel or otherwise.
# 
# DISCLAIMER
# 
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE
# LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
# USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
# 
# NOTE
# 
# This is part of a management middleware software package called MMX that was developed by Inango Systems Ltd.
# 
# This version of MMX provides web and command-line management interfaces.
# 
# Please contact us at Inango at support@inango-systems.com if you would like to hear more about
# - other management packages, such as SNMP, TR-069 or Netconf
# - how we can extend the data model to support all parts of your system
# - professional sub-contract and customization services
# 
################################################################################

ifeq ($(PREFIX),)
    PREFIX := /usr
endif

override CC ?= gcc
override CFLAGS += -c -Wall -std=gnu99 -D _XOPEN_SOURCE=600
		#  -Wunused-parameter
		#  -Wsign-compare
		#  -Wimplicit-function-declaration
		#  -Wmissing-field-initializers
		#  -Wpedantic -Wextra
override LDFLAGS += -lmmx-frontapi -lmmx-backapi -lpthread -lmicroxml -lsqlite3 -ling-gen-utils -lconfig

# "Threshold"
override CONFIG_MMX_EP_EXT_THRESHOLD ?= 0

# "Query timeout (in seconds)"
override CONFIG_MMX_EP_EXT_QUERY_TIMEOUT ?= 60

# "Backend IP address"
override CONFIG_MMX_BE_IPADDR ?= "127.0.0.1"

# "Max object num"
override CONFIG_EP_MAX_OBJECTS_NUM ?= 128

# "Max params per object"
override CONFIG_EP_MAX_PARAMS_PER_OBJECT ?= 48

# "Max indeces per object"
override CONFIG_EP_MAX_INDECES_PER_OBJECT ?= 4

# "Subst values per object"
override CONFIG_EP_SUBST_VALUES_NUMBER ?= 5

# "Max method string lenght"
override CONFIG_EP_MAX_METHOD_STR_LEN ?= 256

# "Max leaf name lenght"
override CONFIG_EP_MAX_LEAF_NAME_LEN ?= 64

# "Max parameter type lenght"
override CONFIG_EP_MAX_PARAM_TYPE_LEN ?= 32

# "Max units string lenght"
override CONFIG_EP_MAX_UNITS_STR_LEN ?= 32

# "Max backend name lenght"
override CONFIG_EP_MAX_BENAME_STR_LEN ?= 32
                                                        
# "Max buffer lenght for SQL requests"
override CONFIG_EP_SQL_REQUEST_BUF_SIZE ?= 2048

# "Number worker threads"
override CONFIG_EP_TP_WORKER_THREADS_NUM ?= 4

# "Worker thread stack size"
override CONFIG_EP_PTHREAD_STACK_SIZE ?= 1048576

# "Max instances per object"
override CONFIG_EP_MAX_INSTANCES_PER_OBJECT ?= 312

# "EP Syslog output"
override CONFIG_EP_USE_SYSLOG ?=

# Enable code with reaction to threshold overflow
override CONFIG_WITH_MMX_EP_EXT ?=

SOURCES := $(wildcard *.c)
OBJECTS := $(SOURCES:.c=.o)
ifneq ($(CONFIG_WITH_MMX_EP_EXT),y)
OBJECTS := $(filter-out ep_ext.o,$(OBJECTS))
endif
EXECUTABLE=mmx-ep

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

%.o: %.c ep_config.h
	$(CC) $(CFLAGS) $< -o $@

ep_config.h: ep_config.h.in
	sed -e "s/@MMX_BE_IPADDR@/${CONFIG_MMX_BE_IPADDR}/" \
	    -e "s/@MAX_OBJECTS_NUM@/${CONFIG_EP_MAX_OBJECTS_NUM}/" \
	    -e "s/@MAX_PARAMS_PER_OBJECT@/${CONFIG_EP_MAX_PARAMS_PER_OBJECT}/" \
	    -e "s/@MAX_INDECES_PER_OBJECT@/${CONFIG_EP_MAX_INDECES_PER_OBJECT}/" \
	    -e "s/@MAX_INSTANCES_PER_OBJECT@/${CONFIG_EP_MAX_INSTANCES_PER_OBJECT}/" \
	    -e "s/@EP_SUBST_VALUES_NUMBER@/${CONFIG_EP_SUBST_VALUES_NUMBER}/" \
	    -e "s/@MAX_METHOD_STR_LEN@/${CONFIG_EP_MAX_METHOD_STR_LEN}/" \
	    -e "s/@MAX_LEAF_NAME_LEN@/${CONFIG_EP_MAX_LEAF_NAME_LEN}/" \
	    -e "s/@MAX_PARAM_TYPE_LEN@/${CONFIG_EP_MAX_PARAM_TYPE_LEN}/" \
	    -e "s/@MAX_UNITS_STR_LEN@/${CONFIG_EP_MAX_UNITS_STR_LEN}/" \
	    -e "s/@MAX_BENAME_STR_LEN@/${CONFIG_EP_MAX_BENAME_STR_LEN}/" \
	    -e "s/@EP_SQL_REQUEST_BUF_SIZE@/${CONFIG_EP_SQL_REQUEST_BUF_SIZE}/" \
	    -e "s/@EP_TP_WORKER_THREADS_NUM@/${CONFIG_EP_TP_WORKER_THREADS_NUM}/" \
	    -e "s/@EP_PTHREAD_STACK_SIZE@/${CONFIG_EP_PTHREAD_STACK_SIZE}/" \
	    ep_config.h.in > ep_config.h

install:
	install -d $(DESTDIR)$(PREFIX)/sbin
	install -m 0755 $(EXECUTABLE) $(DESTDIR)$(PREFIX)/sbin

clean:
	rm -f *.o $(EXECUTABLE)
