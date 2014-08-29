/* 
 * swdiag_cli_handle.h - Structure and APIs for the CLI Handle 
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
 * API for managing the CLI Handles
 */

#ifndef __SWDIAG_CLI_HANDLE_H__
#define __SWDIAG_CLI_HANDLE_H__

#include "swdiag_obj.h"
#include "swdiag_cli.h"

/*
 * swdiag_cli_handle_t
 *
 * Handle used for a CLI request for a large amount of data, note that the
 * handle contents are not to be accessed outside of swdiag_cli_local.c 
 * (possible exception of the remote code for getting data from remotes).
 */
typedef struct cli_handle_t_ {
    unsigned int handle_id;
    cli_type_t type;
    cli_type_filter_t filter;
    obj_instance_t *instance; 
    obj_t *last_obj; /* The last data from swdiag list or rule ouput obj*/
    obj_t *remote_comp;
    unsigned int remote_handle_id;
    obj_t *last_remote_obj; /* keeps track of last remote location */
    xos_time_t handle_used_last_time; /* time stamp when handle was created */
    boolean in_use;    /* Flag to indicate handle in use */
} cli_handle_t;

void swdiag_cli_local_handle_free_garbage(void);
boolean swdiag_cli_local_handle_valid(cli_handle_t *handle);
cli_handle_t *swdiag_cli_local_handle_get(unsigned int handle_id);
boolean swdiag_cli_local_handle_free(cli_handle_t *handle);
void swdiag_cli_local_handle_set_remote_comp_obj(cli_handle_t *handle, obj_t *comp_obj);
void swdiag_cli_local_handle_set_in_use_flag(cli_handle_t *handle);
void swdiag_cli_local_handle_reset_in_use_flag(cli_handle_t *handle);
cli_handle_t *swdiag_cli_local_handle_allocate(cli_type_t type, 
                                               cli_type_filter_t filter);
#endif
