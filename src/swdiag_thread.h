/* 
 * swdiag_thread.h - SW Diagnostics Threads header
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
 * Define the types for general purpose diagnostics threads. These 
 * can be used to execute tests, rules, rci and actions.
 */

#ifndef __SWDIAG_THREAD_H__
#define __SWDIAG_THREAD_H__

#include "swdiag_xos.h"
#include "swdiag_types.h"

/*
 * Number of seconds that a test, rule or action has to complete before we
 * give up and terminate that thread.
 */
#define GUARD_TIMEOUT_SEC 30

/*
 * Number of concurrent threads that we will support at one time
 */
#define NBR_THREADS       4

typedef enum {
    THREAD_QUEUE_FREE,
    THREAD_QUEUE_EXECUTING,
    NBR_THREAD_QUEUES
} thread_queue_e;

/*
 * thread_function_t
 *
 * User supplied function to be used for the thread.
 */
typedef void (*thread_function_exe_t)(swdiag_thread_t*, void*);
typedef void (*thread_function_dsp_t)(swdiag_thread_t*, void*);

typedef struct {
    thread_function_exe_t execute;
    thread_function_dsp_t display;
    void *context;
} thread_job_t;

/*
 * swdiag_thread_t
 *
 * Thread for running a job, the type of job is implied by the type
 * of the "obj" put into the thread structure before the thread is 
 * released.
 */
struct swdiag_thread_s {
    char *name;
    int id;
    boolean quit;
    xos_timer_t *guard_timer; /* timer guards against slow tests/actions */
    xos_thread_t *xos;
    thread_job_t *job;
};

extern void swdiag_thread_init(void);
extern void swdiag_thread_terminate(void);
extern void swdiag_thread_request(thread_function_exe_t execute, 
                                  thread_function_dsp_t display,
                                  void *context);
extern void swdiag_thread_kill(swdiag_thread_t *thread);
extern void swdiag_thread_kill_threads(void);

#define SWDIAG_THREAD_CPU_USAGE "SWDiag CPU Util"
#define SWDIAG_THREAD_THROTTLE_WARN "SWDiag Throttle CPU Warning"
#define SWDIAG_THREAD_THROTTLE_HIGH "SWDiag Throttle CPU High"
#define SWDIAG_THREAD_CPU_WARN "SWDiag CPU Warning"
#define SWDIAG_THREAD_CPU_HIGH "SWDiag CPU High"

/*
 * Default warning threshold is 5%, and high is at 10%
 */
#define SWDIAG_CPU_THROTTLE_WARN 50
#define SWDIAG_CPU_THROTTLE_HIGH 100

#define SWDIAG_THREAD_HIGH_DELAY 1000
#define SWDIAG_THREAD_MAX_DELAY  5000

#endif
