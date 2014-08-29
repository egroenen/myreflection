/* 
 * swdiag_xos.h - SW Diagnostics Cross-OS Layer
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
 * Define any missing types from this OS's standard headers, and include
 * those standard headers if required.
 */

#ifndef __SWDIAG_XOS_H__
#define __SWDIAG_XOS_H__

#include "swdiag_client.h"

#ifdef __IOS__
#include "swdiag_ios.h"
#elif IOS_ON_NEUTRINO
#include "swdiag_ion.h"
#else
/*
 * All other OS include swdiag_linux.h until we figure out a better way of doing the
 * platform independence using autoconf.
 */
#include "config.h"
#include "swdiag_linux.h"
#endif

#include "swdiag_types.h"

/*
 * Incomplete structure types that the OS specific module completes
 */
typedef struct xos_thread_t_ xos_thread_t;
typedef struct xos_time_t_ xos_time_t;
typedef struct xos_timer_t_ xos_timer_t;
typedef struct xos_critical_section_t_ xos_critical_section_t;

/*******************************************************************
 * Thread functions
 *******************************************************************/

typedef void (xos_thread_start_fn_t)(swdiag_thread_t *thread);
xos_thread_t *swdiag_xos_thread_create(const char *name,
                                       xos_thread_start_fn_t *start_routine,
                                 swdiag_thread_t *swdiag_thread);
boolean swdiag_xos_thread_destroy(xos_thread_t *thread);
boolean swdiag_xos_thread_wait(xos_thread_t *thread);
boolean swdiag_xos_thread_release(xos_thread_t *thread);
void swdiag_xos_thread_suspend(xos_thread_t *thread);
int swdiag_xos_thread_get_id(xos_thread_t *thread);
long swdiag_xos_thread_cpu_last_min(xos_thread_t *thread);

void swdiag_xos_register_with_master(const char *component_name);
void swdiag_xos_register_as_master(void);
void swdiag_xos_slave_to_master(void);

void swdiag_xos_notify_user(const char *instance, const char *string);
void swdiag_xos_notify_test_result(const char *test_name, const char *instance_name,
                                   boolean result, long value);
void swdiag_xos_notify_rule_result(const char *rule_name, const char *instance_name,
                                   boolean result, long value);
void swdiag_xos_notify_action_result(const char *action_name, const char *instance_name,
                                   boolean result, long value);
void swdiag_xos_notify_component_health(const char *comp_name, int health);
void swdiag_xos_recovery_in_progress(obj_instance_t *rule_instance,
                                     obj_instance_t *action_instance);

/*******************************************************************
 * Time definitions and functions
 *******************************************************************/

struct xos_time_t_ {
    ulong sec;  /* Integer seconds since 0000 UTC 1/1/1970 */
    ulong nsec; /* Nanosecond residual */
};

#define XOS_TIME_LT(t1, t2) \
	((t1).sec < (t2).sec || ((t1).sec == (t2).sec && (t1).nsec < (t2).nsec))

#define XOS_TIME_IS_ZERO(t1) ((t1)->sec == 0 && (t1)->nsec == 0)

void swdiag_xos_time_set_now(xos_time_t *time_now);
void swdiag_xos_time_diff(xos_time_t *start, 
                          xos_time_t *end, 
                          xos_time_t *diff);

/*******************************************************************
 * Timer functions
 *******************************************************************/

typedef void (xos_timer_expiry_fn_t)(void *context);
xos_timer_t *swdiag_xos_timer_create(xos_timer_expiry_fn_t *fn, void *context);
void swdiag_xos_timer_start(xos_timer_t *timer, long delay_sec, long delay_nsec);
void swdiag_xos_timer_stop(xos_timer_t *timer);
void swdiag_xos_timer_delete(xos_timer_t *timer);

/*******************************************************************
 * Process definitions and functions
 *******************************************************************/

typedef enum xos_event_e {
    XOS_EVENT_TEST_START,
    XOS_EVENT_GUARD_TIMEOUT,
} xos_event_t;

void swdiag_xos_sleep(uint seconds);
xos_event_t swdiag_xos_wait_on_event(int *detail);

//void xos_wake_on_event_timer(xos_timer_t *timer);

/*******************************************************************
 * Critical Regions
 *******************************************************************/
xos_critical_section_t *swdiag_xos_critical_section_create(void);
void swdiag_xos_critical_section_delete(xos_critical_section_t *cs);
void swdiag_xos_critical_section_enter(xos_critical_section_t *cs);
void swdiag_xos_critical_section_exit(xos_critical_section_t *cs);


/*******************************************************************
 * Miscellaneous functions
 *******************************************************************/

const char *swdiag_xos_errmsg_to_name(const void *msgsym);
swdiag_result_t swdiag_xos_reload(void);
swdiag_result_t swdiag_xos_scheduled_reload(void);
swdiag_result_t swdiag_xos_switchover(void);
swdiag_result_t swdiag_xos_scheduled_switchover(void);
swdiag_result_t swdiag_xos_reload_standby(void);

void swdiag_xos_trace(trace_event_t *event);
void swdiag_xos_running_in_terminal(void);
char *swdiag_xos_sstrncpy(char *s1, char const *s2, unsigned long max);
char *swdiag_xos_sstrncat(char *s1, char const *s2, unsigned long max);

#endif /* __SWDIAG_XOS_H__ */
