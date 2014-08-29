/*
 * swdiag_server_libinterface.c - SW Diagnostics Server libswdiag compulsory functions
 *
 * Copyright (c) 2012 Edward Groenendaal
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

/**
 * libswdiag has some functions that must be implemented by the user. In the
 * majority of cases these are just stubs. Odds are that these will be removed
 * at some point, or use a registration API.
 */

#include "swdiag_xos.h"
#include "smtpfuncs.h"
#include "swdiag_server_config.h"

/*
 * xos_notify_user()
 *
 * Use whatever notification scheme is at our disposal syslog/printf etc.
 * Which in the case of a daemonised server is just the email system right now.
 */
void swdiag_xos_notify_user (const char *instance, const char *message)
{
    send_mail(server_config.smtp_hostname, "swdiag-server", server_config.alert_email_from, server_config.alert_email_to, message, NULL, message);
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


