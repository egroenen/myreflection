/* 
 * swdiag_cli.h - interface to core CLI handlers.
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
 * Declare the API and structure used between the OS dependent SW Diags CLI
 * handler and the OS dependent CLI transport mechanism.
 *
 * IMPORTANT: All functions declared in this file are available
 * everywhere in the system and do *not* depend on having the Object
 * DB local (or CLI local which interfaces to it). I.e. Do not declare
 * a function in this file that is only implemented in swdiag_cli_local.c
 * since it will not work where the CLI is remote.
 *
 *
 * TODO: Rewrite the whole CLI interacts with swdiag so that no internal
 *       headers are being exposed and so that the modularity is clearer.
 */

#ifndef __SWDIAG_CLI_H__
#define __SWDIAG_CLI_H__

#include "swdiag_xos.h"

#define MIN_LOCAL 1
#define MAX_LOCAL 20
#define delimiter "/"
#define NULLSTR(s) (!(s) || *(s) == '\0')

/* Conctenated string of double the size of max string + two characters
 * to store "/" and null terminated character
 */ 
#define SWDIAG_MAX_CONCAT_STRING_LEN 2*SWDIAG_MAX_NAME_LEN+2

/* 
 * enum for internal test commands 
 */
typedef enum {
    SWDIAG_EXEC_DEPEND_CREATE, 
    SWDIAG_EXEC_RULE_CREATE, 
    SWDIAG_EXEC_ACTION_CREATE, 
    SWDIAG_EXEC_COMP_CREATE, 
    SWDIAG_EXEC_TEST_NOTIFY_CREATE, 
    SWDIAG_EXEC_TEST_POLL_CREATE, 
    SWDIAG_EXEC_INSTANCE_CREATE, 
    SWDIAG_EXEC_COMP_CONTAINS, 
    SWDIAG_EXEC_PERIOD_NORMAL, 
    SWDIAG_EXEC_PERIOD_FAST, 
    SWDIAG_EXEC_PERIOD_SLOW, 
    SWDIAG_EXEC_TEST_NOTIFY_FAIL,
    SWDIAG_EXEC_TEST_NOTIFY_PASS,
    SWDIAG_EXEC_TEST_POLLED_FAIL,
    SWDIAG_EXEC_TEST_POLLED_PASS,
    SWDIAG_EXEC_TEST_POLLED_ABORT,
    SWDIAG_EXEC_TEST_POLLED_VALUE,
    SWDIAG_EXEC_DELETE,
} swdiag_cli_test_cmd_t;    

typedef enum cli_type_t_ {
    CLI_UNKNOWN = 0,
    CLI_TEST = 1,
    CLI_ACTION = 2,
    CLI_RULE = 3,
    CLI_COMPONENT = 4,
    CLI_TEST_POLLED = 5,
    CLI_TEST_INSTANCE = 6,
    CLI_ACTION_INSTANCE = 7,
    CLI_RULE_INSTANCE = 8,
} cli_type_t;

typedef enum cli_type_filter_t_ {
    CLI_FILTER_UNKNOWN = 0,   /* Not to be used */
    CLI_FILTER_NONE,
    CLI_DATA_FAILURE,         /* Get failure data */
    CLI_DATA_FAILURE_CURRENT, /* Get current failure data */
    CLI_RUN,                  /* Use by test command, Run cli test */
    CLI_RUN_NO_RULES,         /* Use by test command, run no rule */
    CLI_SHOW_COMP,            /* Show strucs in Comp */
    CLI_TEST_RULE,            /* Get rule from test obj */
    CLI_RULE_ACTION,          /* Get action from rule obj */
    CLI_RULE_INPUT,           /* Get all input to this rule */
    CLI_RULE_OUTPUT,          /* Get a list of all output triggers */
    CLI_PARENT_DEPEND,        /* Get a list of all parent */
    CLI_CHILD_DEPEND,         /* Get a list of dependent children */
    CLI_OPT_TBL,              /* Get option table */
    CLI_NVGEN                 /* filter set during nvgen operation */
} cli_type_filter_t;    

typedef enum cli_state_t_ {
    CLI_STATE_ALLOCATED,
    CLI_STATE_INITIALIZED,
    CLI_STATE_CREATED,
    CLI_STATE_ENABLED,
    CLI_STATE_DISABLED,
    CLI_STATE_DELETED,
    CLI_STATE_INVALID
} cli_state_t;        

typedef enum cli_test_type_t_  {
     CLI_TEST_TYPE_POLLED,
     CLI_TEST_TYPE_NOTIFICATION,
     CLI_TEST_TYPE_ERRMSG,
     CLI_TEST_TYPE_INVALID
} cli_test_type_t;

/*
 * Should keep in sync with what the obj DB is keeping.
 */
#define CLI_HISTORY_SIZE 5

typedef struct cli_history_s {
    xos_time_t time;
    swdiag_result_t result;
    unsigned int count;
    long value;
} cli_history_t;

typedef struct cli_stats_t_ {
    unsigned int failures;
    unsigned int aborts;
    unsigned int passes;
    unsigned int runs;
    cli_history_t history[5];  // Last five results
} cli_stats_t;    

typedef struct cli_test_info_t_ {
    cli_test_type_t type;
    const char *name;
    const char *description;
    const char *context;
    int poll_frequency;
    xos_time_t last_ran;
    xos_time_t next_run;
    swdiag_result_t last_result;
    unsigned int last_result_count;
    long last_value;
    cli_stats_t stats;
    cli_state_t state;
    cli_state_t default_state;
    unsigned int period;
    unsigned int default_period;
} cli_test_t;    

typedef struct cli_rule_info_t_ {
    const char *name;
    const char *description;
    const char *context;
    swdiag_rule_operator_t operator;
    swdiag_rule_operator_t default_operator;
    long op_n;
    long op_m;
    cli_stats_t stats;
    cli_state_t state;
    cli_state_t default_state;
    swdiag_result_t last_result;
    long last_value;
    unsigned int last_result_count;
    unsigned int fail_count;
    swdiag_severity_t severity;
} cli_rule_t;

typedef struct cli_comp_info_t_ {
    const char *name;
    const char *description;
    const char *context;
    unsigned int health;
    unsigned int confidence;
    cli_stats_t stats;
    cli_state_t state;
    cli_state_t default_state;
    unsigned int catastrophic;
    unsigned int critical;
    unsigned int high;
    unsigned int medium;
    unsigned int low;
    unsigned int positive;
    /*
     * MIB notify + thresholds TODO
     */
} cli_comp_t;    


typedef struct cli_action_info_t_ {
    const char *name;
    const char *description;
    const char *context;
    cli_stats_t stats;
    cli_state_t state;
    cli_state_t default_state;
    swdiag_result_t last_result;
    unsigned int last_result_count;
} cli_action_t;    

typedef struct cli_instance_t_ {
    const char *name;
    const void *context;
    cli_state_t state;
    cli_state_t default_state;     /* default state */
    cli_stats_t stats;
    swdiag_result_t last_result;
    unsigned int last_result_count;
    unsigned int fail_count;
} cli_instance_t;

/*
 * info_element_t
 */
typedef struct cli_info_element_t_ {
    struct cli_info_element_t_ *next;
    cli_type_t type;
    const char *name;
    const char *description;
    const char *context;
    swdiag_result_t last_result;
    unsigned int last_result_count;
    unsigned int health;
    unsigned int confidence;
    cli_stats_t stats;
    cli_state_t state;
    cli_state_t default_state;
    cli_state_t cli_state;
    swdiag_rule_operator_t operator;
    swdiag_rule_operator_t default_operator;
    long op_n;
    long op_m;
    unsigned int period;
    unsigned int default_period;
    swdiag_severity_t severity;
} cli_info_element_t;


/*
 * info_t
 */
typedef struct cli_info_t_ {
    unsigned int num_elements;
    cli_info_element_t *elements;
} cli_info_t;

/* Use it to get all object names locally and remotely 
 * to show it for user convenience.
 */
typedef struct cli_obj_name_element_t_ {
    struct cli_obj_name_element_t_ *next;
    const char *name;
    const char *help;
} cli_obj_name_element_t;    

typedef struct cli_obj_name_t_ {
    unsigned int num_elements;
    cli_obj_name_element_t *elements;
} cli_obj_name_t;    

/* Use it to get either all rules for a given test, all actions 
 * for a given rule or a test for a given rule. 
 */
typedef struct cli_data_element_t_ {
    struct cli_data_element_t_ *next;
    const char *name;
    cli_stats_t stats;
    cli_state_t state;
    cli_type_t type;
    swdiag_result_t last_result;
    swdiag_severity_t severity;
} cli_data_element_t;

typedef struct cli_data_t {
    unsigned int num_elements;
    cli_data_element_t *elements;
} cli_data_t;    

typedef struct cli_debug_t {
    unsigned int num_filters;
    char *filters;
} cli_debug_t;
    
void *swdiag_cli_get_option_tbl(const char *cli_name, cli_type_t type);

unsigned int swdiag_cli_get_info_handle(const char *name,
                               cli_type_t type,
                               cli_type_filter_t filter,
                               const char *instance_name);

cli_info_t *swdiag_cli_get_info(unsigned int handle_id);
cli_info_t *swdiag_cli_get_instance_info(unsigned int handle_id);

void *swdiag_cli_get_single_info(unsigned int handle_id); 
cli_instance_t *swdiag_cli_get_single_instance_info(unsigned int handle_id);

void swdiag_cli_test_run(const char *test_name, 
                         const char *instance_name,
                         cli_type_t type,
                         cli_type_filter_t filter);

void swdiag_cli_config_rule_param(const char *rule_name,
                                  boolean setdefault,
                                  swdiag_rule_operator_t op,
                                  long op_n,
                                  long op_m);
void swdiag_cli_config_test_param(const char *test_name,
                                  boolean setdefault,
                                  unsigned int period);
void swdiag_cli_enable_disable_test(const char *test_name, 
                                    cli_state_t state,
                                    const char *instance_name);
void swdiag_cli_enable_disable_action(const char *action_name,
                                      cli_state_t state,
                                      const char *instance_name);
void swdiag_cli_enable_disable_rule(const char *rule_name, 
                                    cli_state_t state,
                                    const char *instance_name);
void swdiag_cli_enable_disable_comp(const char *comp_name,
                                    cli_state_t state);
const char *swdiag_cli_get_parent_comp(unsigned int handle_id);
cli_data_t *swdiag_cli_get_strucs_in_comp(unsigned int handle_id);
cli_data_t *swdiag_cli_get_depend_or_trigger_data(unsigned int handle_id);
cli_data_t *swdiag_cli_get_strucs_in_comp(unsigned int handle_id);
boolean swdiag_cli_is_obj_remote(const char *name);
const char *swdiag_cli_append_token(const char *name, const char *token);
void swdiag_cli_test_command_internal(swdiag_cli_test_cmd_t cli_cmd, 
                                      swdiag_cli_test_cmd_t cmd_period, 
                                      unsigned int period,
                                      const char *cli_name1, 
                                      const char *cli_name2, 
                                      const char *cli_name3,
                                      const char *remote_name);
cli_data_t *swdiag_cli_get_connected_instances_between_objects(
    unsigned int handle_id,
    const char *instance_name);

char *swdiag_cli_entity_to_name(unsigned int slot);

void swdiag_cli_debug_enable(const char *name);
void swdiag_cli_debug_disable(const char *name);
cli_debug_t *swdiag_cli_debug_get(void);

const char *swdiag_cli_state_to_str(cli_state_t state);
#endif
