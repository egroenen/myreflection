/* -*- Mode: C -*-
 * 
 * swdiag_unix_rpc.x - HA Diagnostics Sun RPC interface specification
 *
 * Copyright (c) 2007-2009 Cisco Systems Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/* 
 * Define the RPC data types for communication between HA Diags
 * instances.
 *
 * TODO: Extend messages to cover the full set of messages required.
 */

typedef string swdiag_name_t<>;
typedef string host_name_t<>;
typedef struct swdiag_register_t_ swdiag_register_t;

struct swdiag_register_t_ {
    swdiag_name_t entity;
    host_name_t hostname;
    int instance;
};

/*
 * When the CLI wishes to obtain information from the HA diags master it
 * first uses the SWDIAG_GET_INFO_HANDLE with the type of info it wants,
 * a handle is returned. Then it uses SWDIAG_GET_INFO with that handle to
 * obtain the rest of the info.
 */
struct swdiag_cli_info_element_rpc_t {
    int type;
    string name<>;
    int level;
    int remote;
    int last_result;
    int last_result_count;
    int severity;
    int failures;
    int aborts;
    int passes;
    int runs;
    int state;
    int default_state;
    int health;
    int confidence;
};

typedef struct swdiag_cli_info_element_rpc_t swdiag_cli_info_rsp_t<20>;

/*
 * The master instance registers the following service that listens for
 * register requests. It then uses this information for contacting the
 * RPC server on that HA Diags instance that implements the slave
 * protocol.
 */
program SWDIAG_REMOTE_MASTER_PROTOCOL {
    version SWDIAG_MASTER_PROTOCOL_V1 {
        int SWDIAG_REGISTER(swdiag_register_t) = 1;
        unsigned SWDIAG_GET_INFO_HANDLE(int) = 2;
        swdiag_cli_info_rsp_t SWDIAG_GET_INFO(unsigned) = 3;
    } = 1;
} = 0x3000F002;

/*
 * The slaves instances register the following service that allows the
 * master instance to contact them for information. The program number
 * is overridden by the slave instance depending on the package so that
 * more than one slave may exist on the same host.
 */
program SWDIAG_REMOTE_SLAVE_PROTOCOL {
    version SWDIAG_SLAVE_PROTOCOL_V1 {
        int SWDIAG_GET_HEALTH(void) = 1;
        unsigned SWDIAG_GET_SLAVE_INFO_HANDLE(int) = 2;
        swdiag_cli_info_rsp_t SWDIAG_GET_SLAVE_INFO(unsigned) = 3;
    } = 1;
} = 0x3000F003;     
