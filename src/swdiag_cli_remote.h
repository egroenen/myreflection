/* 
 * swdiag_cli_remote.h - Internal interface to core CLI handlers.  
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
 * Declare the API used between the OS dependent SW Diags CLI
 * handler and the OS dependent CLI transport mechanism.
 *
 * NOTE: To be replaced with a new reusable transport API.
 */

#ifndef __SWDIAG_CLI_REMOTE_H__
#define __SWDIAG_CLI_REMOTE_H__

#include "swdiag_cli.h"

unsigned int swdiag_cli_remote_get_info_handle(const char *comp_name,
                                               cli_type_t type,
                                               cli_type_filter_t filter,
                                               const char *instance_name);
cli_info_t *swdiag_cli_remote_get_info(unsigned int handle_id); 
cli_info_t *swdiag_cli_remote_get_instance_info(unsigned int handle_id); 
void *swdiag_cli_remote_get_single_info(unsigned int handle_id); 
cli_instance_t *swdiag_cli_remote_get_single_instance_info(unsigned int handle_id);

void swdiag_cli_remote_config_rule_param(const char *name,
                                         boolean setdefault,
                                         swdiag_rule_operator_t op,
                                         long op_n,
                                         long op_m);

void swdiag_cli_remote_config_test_param(const char *name,
                                         boolean setdefault,
                                         unsigned int period);
void swdiag_cli_remote_enable_disable_obj(const char *name,
                                          cli_state_t state,
                                          cli_type_t type,
                                          const char *instance_name);

cli_obj_name_t *swdiag_cli_remote_get_option_tbl(unsigned int handle_id);

cli_data_t *swdiag_cli_remote_get_strucs_in_comp(unsigned int handle_id);
cli_data_t *swdiag_cli_remote_get_depend_or_trigger_data(unsigned int handle_id); 
const char* swdiag_cli_remote_get_parent_comp(unsigned int handle);

void swdiag_cli_remote_test_command_internal(
    swdiag_cli_test_cmd_t cli_cmd, 
    swdiag_cli_test_cmd_t cmd_period, 
    unsigned int period,
    const char *cli_name1, 
    const char *cli_name2, 
    const char *cli_name3,
    const char *remote_name);

cli_data_t *swdiag_cli_remote_get_connected_instances_between_objects(
    unsigned int handle_id, 
    const char *instance_name); 

#endif
