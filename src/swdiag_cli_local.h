/* 
 * swdiag_cli_local.h - Interface between Object DB and the CLI 
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
 * handler and the OS independent CLI transport mechanism. These
 * structures to not travel over the transport, they are internal
 * only.
 *
 * This abstraction allows the Object DB data to be grouped into 
 * structures that are more convenient for the CLIs and transport
 * to handle. It also forms a reusable API to make moving code to
 * remote locations easier.
 *
 * IMPORTANT: No Object DB types should be used in this file.
 *
 */

#ifndef __SWDIAG_CLI_LOCAL_H__
#define __SWDIAG_CLI_LOCAL_H__

#include "swdiag_cli.h"
 
unsigned int swdiag_cli_local_get_info_handle(const char *name,
                                              cli_type_t type,
                                              cli_type_filter_t filter,
                                              const char *instance_name);

void swdiag_cli_local_free_info(cli_info_t *info);
cli_info_t *swdiag_cli_local_get_info(unsigned int handle_id, 
                                      unsigned int max);
cli_info_t *swdiag_cli_local_get_instance_info(unsigned int handle_id, 
                                               unsigned int max);

/*
 * swdiag_cli_local_get_single_info can return one of cli_comp_t,
 * cli_test_t, cli_rule_t, cli_action_t
 */
void *swdiag_cli_local_get_single_info(unsigned int handle_id); 
cli_instance_t *swdiag_cli_local_get_single_instance_info(unsigned int handle_id); 

void swdiag_cli_local_config_rule_param(const char *rule_name,
                                        boolean setdefault,
                                        swdiag_rule_operator_t op,
                                        long op_n,
                                        long op_m);

void swdiag_cli_local_config_test_param(const char *test_name,
                                        boolean setdefault,
                                        unsigned int period);
void swdiag_cli_local_enable_disable_test(const char *test_name,
                                          cli_state_t state,
                                          const char *instance_name);
void swdiag_cli_local_enable_disable_rule(const char *rule_name,
                                          cli_state_t state,
                                          const char *instance_name);
void swdiag_cli_local_enable_disable_action(const char *action_name,
                                            cli_state_t state,
                                            const char *instance_name);

void swdiag_cli_local_enable_disable_comp(const char *comp_name,
                                          cli_state_t state);
cli_data_t *swdiag_cli_local_get_strucs_in_comp(unsigned int handle_id,
                                                unsigned int max_mtu);
void swdiag_cli_local_test_run(const char *test_name, 
                               const char *instance_name,
                               cli_type_t type,
                               cli_type_filter_t filter);

cli_obj_name_t *swdiag_cli_local_get_option_tbl(unsigned int handle_id, 
                                                unsigned int mtu);
const char *swdiag_cli_local_get_parent_comp(unsigned int handle_id);
boolean swdiag_cli_local_is_obj_remote(const char *name);

cli_data_t *swdiag_cli_local_get_depend_or_trigger_data(
    unsigned int handle_id, 
    unsigned int mtu);

void swdiag_cli_local_test_command_internal(
    swdiag_cli_test_cmd_t cli_cmd, 
    swdiag_cli_test_cmd_t cmd_period, 
    unsigned int period,
    const char *cli_name1, 
    const char *cli_name2, 
    const char *cli_name3);

cli_data_t *swdiag_cli_local_get_connected_instances_between_objects(
    unsigned int handle_id, 
    const char *instance_name, 
    unsigned int mtu);

void swdiag_cli_local_debug_enable(const char *name);
void swdiag_cli_local_debug_disable(const char *name);
cli_debug_t *swdiag_cli_local_debug_get(void);

#endif
