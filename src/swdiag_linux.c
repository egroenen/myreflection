/* 
 * swdiag_linux.c - SW Diagnostics OS specific interface
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
 * NOTE: To be replaced with a more generic unix interface rather than 
 * separate linux and sunos since they are identical.
 */

#include "swdiag_xos.h"
#include "swdiag_obj.h"

/*
 * swdiag_xos_get_time()
 *
 * Get the unix time, secs since 1970
 */
time_t swdiag_xos_get_time (void)
{
    time_t time_now;

    time_now = time(NULL);

    if (time_now == (time_t)-1) {
        /*
         * An error, die.
         *
         * TBD.
         */
    }
    return(time_now);
}

/*
 * swdiag_xos_sleep()
 *
 * Sleep for 'time' milliseconds.
 */
void swdiag_xos_sleep (uint milliseconds)
{
    struct timespec req;

    req.tv_sec = milliseconds / 1000;
    req.tv_nsec = (milliseconds % 1000) * 1E6;

    nanosleep(&req, NULL);
}

/*
 * swdiag_xos_errmsg_to_name()
 *
 * linux can't trigger off of error messages (yet).
 */
const char *swdiag_xos_errmsg_to_name (const void *msgsym)
{
    return("invalid errmsg");
}

char *swdiag_xos_sstrncpy (char *s1, const char *s2, unsigned long max)
{
    return (strncpy(s1, s2, max));
}

char *swdiag_xos_sstrncat (char *s1, const char *s2, unsigned long max)
{
   return (strncat(s1, s2, max));
}

/*
 * Need a better home for this! I'm not even convinced that we need it at all.
 * TODO
 */
void swdiag_xos_recovery_in_progress (obj_instance_t *rule_instance,
                                      obj_instance_t *action_instance)
{
//    printf("Software Diagnostics Identified Fault\n"
//           " Rule Name Failed:          %s\n"
//           " Recovery Action Initiated: %s\n"
//           " Enter 'swdiag -r %s%s%s' for further details\n",
//           swdiag_obj_instance_name(rule_instance),
//           swdiag_obj_instance_name(action_instance),
//           rule_instance->obj->i.name,
//           swdiag_obj_is_member_instance(rule_instance) ? " -i " : "",
//           swdiag_obj_is_member_instance(rule_instance) ? rule_instance->name : "");
}
