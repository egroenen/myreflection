/*
 * check_base_xos_stubs.c
 *
 * Stubs for swdiag unit testing
 *
 *  Created on: 8/04/2014
 *      Author: eddyg
 */

#include <swdiag_client.h>
#include "../src/swdiag_linux.h"

void swdiag_xos_notify_user (const char *instance, const char *message)
{
}

/*
 * swdiag_xos_notify_test_result()
 * Notify the results of a test.
 *
 */
void swdiag_xos_notify_test_result (const char *test_name,
                                    const char *instance_name,
                                    boolean result,
                                    long value)

{
    //printf("\n** Software Diagnostics notify test result for test (%s) "
    //    "instance name (%s)", test_name, instance_name);
}

/*
 * swdiag_xos_notify_rule_result()
 * Notify the results of a rule.
 *
 */
void swdiag_xos_notify_rule_result (const char *rule_name,
                                    const char *instance_name,
                                    boolean result,
                                    long value)

{
    //printf("\n** Software Diagnostics notify rule result for rule (%s) "
    //    "instance name (%s)", rule_name, instance_name);
}

/*
 * swdiag_xos_notify_action_result()
 * Notify the results of an action.
 *
 */
void swdiag_xos_notify_action_result (const char *action_name,
                                      const char *instance_name,
                                      boolean result,
                                      long value)

{
    //printf("\n** Software Diagnostics notify action result for action (%s) "
    //    "instance name (%s)", action_name, instance_name);
}

/*
 * swdiag_xos_notify_action_result()
 * Notify of changes to a components health.
 *
 */
void swdiag_xos_notify_component_health (const char *comp_name,
                                         int health)

{
    //printf("****************** Software Diagnostics notify action result for comp (%s)\n",
    //       comp_name);
}

swdiag_result_t swdiag_xos_reload (void)
{
    return(SWDIAG_RESULT_ABORT);
}

swdiag_result_t swdiag_xos_scheduled_reload (void)
{
    return(SWDIAG_RESULT_ABORT);
}

swdiag_result_t swdiag_xos_switchover (void)
{
    return(SWDIAG_RESULT_ABORT);
}

swdiag_result_t swdiag_xos_scheduled_switchover (void)
{
    return(SWDIAG_RESULT_ABORT);
}

swdiag_result_t swdiag_xos_reload_standby (void)
{
    return(SWDIAG_RESULT_ABORT);
}

/*
 * Using the swdiag library requires that some functions be implemented, we will
 * leave these as stubs for now.
 */
void swdiag_xos_register_with_master (const char *component)
{

}

void swdiag_xos_register_as_master (void)
{

}

void swdiag_xos_slave_to_master (void)
{

}


