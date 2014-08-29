/* 
 * swdiag_posix.c 
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
 * POSIX dependent function definitions for swdiag_os.h
 * A set of OS'es (but not all) would include this module in the build.
 */

//#define _POSIX_C_SOURCE 200112L

#ifdef __GNUC__
#define _XOPEN_SOURCE 500
#endif

#include <signal.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>

#ifdef __APPLE__
/*
 * Mac OS X doesn't have posix timers, so do it some other TBD way.
 *
 * We'll come back to this later, giving up on it for now.
 */
#include <sys/time.h>
#include <CoreFoundation/CFRunLoop.h>
#endif

#include "swdiag_xos.h"
#include "swdiag_obj.h"
#include "swdiag_trace.h"
#include "swdiag_sched.h"
#include "swdiag_thread.h"

/*************************************************************
 * Macros and Types
 *************************************************************/

/*
 * The POSIX specific structures for:
 * - critical sections
 * - threads
 * - timers
 */
struct xos_critical_section_t_ {
    pthread_mutex_t mutex;
};

struct xos_thread_t_ {
    pthread_t tid;
    pthread_mutex_t run_test_mutex;
    pthread_cond_t cond;
    boolean work_to_do;
};

struct xos_timer_t_ {
    struct xos_timer_t_ *next;
    boolean started;
    xos_timer_expiry_fn_t *expiry_fn;
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS - 1) >= 0L
    timer_t id;
#elif __APPLE__
    struct itimerval id;
#else
#error No timers
#endif
    void *context;
};

typedef struct {
    xos_timer_t *head;
    xos_timer_t *tail;
} timer_queue_t;

static timer_queue_t timer_queue;

/*************************************************************
 * POSIX time definitions and functions
 *************************************************************/

#define TIMESPEC_LT(t1, t2) \
	((t1)->tv_sec < (t2)->tv_sec || ((t1)->tv_sec == (t2)->tv_sec && (t1)->tv_nsec < (t2)->tv_nsec))

#define TIMESPEC_IS_ZERO(t1) ((t1)->tv_sec == 0 && (t1)->tv_nsec == 0)

/*
 * Set the current time into the structure pointed to by time_now
 */
void swdiag_xos_time_set_now (xos_time_t *time_now)
{
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS - 1) >= 0L
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        swdiag_error("SCHED clock get time failure");
        return;
    }
    time_now->sec = ts.tv_sec;
    time_now->nsec = ts.tv_nsec;
#elif __APPLE__
    CFAbsoluteTime current;

    current = (CFAbsoluteTimeGetCurrent() * 1000);
    time_now->sec = (unsigned int)(current / 1000.0);
    time_now->nsec = 0;
#else
    time_now->sec = 0;
    time_now->nsec = 0;
#endif
}

/*************************************************************
 * POSIX timer functions
 *************************************************************/

/*
 * Walk the list of timers and check which ones have expired, don't
 * bother matching up the ids for the actual timer. This may also
 * be a stray signal - that's fine we'll take what we can.
 */
#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS - 1) >= 0L
static void signal_handler (int sig, siginfo_t *si, void *uc)
{
    xos_timer_t *timer;
    struct itimerspec value;
    timer_t *tidp = si->si_value.sival_ptr;

    /*
     * Because we are only using a few timers we don't bother sorting them.
     * We don't really want to call tests out of order which can happen here
     * as multiple timers may go off together. Something to do later.
     */
    for (timer = timer_queue.head; timer; timer = timer->next) {
        if (!timer->started) {
            continue;
        }

        value.it_value.tv_sec = 1;
        if (timer_gettime(timer->id, &value) != 0) {
            swdiag_error("XOS gettime failed");
            continue;
        }

        if (TIMESPEC_IS_ZERO(&value.it_value)) {
            //swdiag_debug(NULL, "XOS timer %d expired", timer->id);
            timer->expiry_fn(timer->context);
        }
    }
}

/*
 * Create the timer with given expiry function and context
 *
 * We only have one
 */
xos_timer_t *swdiag_xos_timer_create (xos_timer_expiry_fn_t *fn, void *context)
{
    xos_timer_t *timer;
    struct sigevent sev;
    struct sigaction sa;
    int posix_timer_signal = SIGRTMIN;

    timer = calloc(1, sizeof(xos_timer_t));
    if (!timer) {
        swdiag_error("XOS timer malloc failure");
        return (NULL);
    }
    timer->expiry_fn = fn;
    timer->context = context;
    timer->started = FALSE;
    timer->next = NULL;

    // Set up a signal handler for that signal
    sa.sa_flags = SA_SIGINFO;
    sa.sa_sigaction = signal_handler;
    sigemptyset(&sa.sa_mask);

    if (sigaction(posix_timer_signal, &sa, NULL) < 0) {
        swdiag_error("XOS failed to initialize signal handler");
        free(timer);
        return (NULL);
    }

    memset(&sev, 0, sizeof(sev));

    // Set up a timer that triggers that signal
    sev.sigev_notify = SIGEV_SIGNAL;
    sev.sigev_signo = posix_timer_signal;
    sev.sigev_value.sival_ptr = &timer->id;

    if (timer_create(CLOCK_REALTIME, &sev, &timer->id ) == -1) {
        swdiag_error("XOS timer_create() failed (%s)", 
                     strerror(errno));
        free(timer);
        return (NULL);
    }

    /*
     * Add the timer to the tail of the local queue (if created successfully)
     */
    if (!timer_queue.head) {
        timer_queue.head = timer;
    }
    if (timer_queue.tail) {
        timer_queue.tail->next = timer;
    }
    timer_queue.tail = timer;

    swdiag_debug(NULL, "XOS timer %d created", timer->id);
    return (timer);
}
#elif __APPLE__
xos_timer_t *swdiag_xos_timer_create (xos_timer_expiry_fn_t *fn, void *context)
{

}
#endif

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS - 1) >= 0L
/*
 * Start the given timer with the given expiry delay
 */
void swdiag_xos_timer_start (xos_timer_t *timer, 
                             long delay_sec, long delay_nsec)
{
    struct itimerspec timer_val; 

    if (!timer) {
        swdiag_debug(NULL, "XOS timer_start() bad params");
        return;
    }

    if (delay_nsec < 0) {
        delay_nsec += 1e9;
        delay_sec--;
    }
    if (delay_sec < 0) {
        delay_sec = 0;
        delay_nsec = 0;
    }

    timer_val.it_value.tv_sec = delay_sec;
    timer_val.it_value.tv_nsec = delay_nsec; 
    timer_val.it_interval.tv_sec = 0; /* zero's mean one shot */
    timer_val.it_interval.tv_nsec = 0;

    timer->started = TRUE;
    if (timer_settime(timer->id, 0, &timer_val, NULL) == -1) {
        swdiag_error("XOS timer_start(%d) (%lu,%lu) failed (%s)", 
                     timer->id, delay_sec, delay_nsec, strerror(errno));
        return;
    }
    //swdiag_debug(NULL, "XOS timer %d started with delay(%lu,%lu)",
    //             timer->id, delay_sec, delay_nsec);
}
#elif __APPLE__

/*
 * On MacOSX we will sleep instead of using a timer, when we wake up we can
 * check to see what has expired in the meantime.
 */
void swdiag_xos_timer_start (xos_timer_t *timer, 
                             long delay_sec, long delay_nsec)
{



}
#endif

/*
 * Stop the timer (opposite of create)
 */
void xos_timer_stop (xos_timer_t *timer)
{
    /* to write */
}

#if defined(_POSIX_TIMERS) && (_POSIX_TIMERS - 1) >= 0L
/*
 * Delete the timer
 */
void swdiag_xos_timer_delete (xos_timer_t *timer)
{
    if (timer) {
        swdiag_debug(NULL, "XOS timer %d deleted", timer->id);
        xos_timer_stop(timer);
        free(timer);
    }
}
#else
void swdiag_xos_timer_delete (xos_timer_t *timer)
{

}

#endif

/*************************************************************
 * POSIX critical section functions
 *************************************************************/

/*
 * Create a critical section, the pointer to which is returned.
 */
xos_critical_section_t *swdiag_xos_critical_section_create (void)
{
    xos_critical_section_t * cs;
    pthread_mutexattr_t attr;
    boolean rc;

    cs = (xos_critical_section_t *)
        malloc(sizeof(xos_critical_section_t));
    if (!cs) {
        swdiag_error("POSIX malloc");
        return (NULL);
    }

    rc = pthread_mutexattr_init(&attr);
    if (rc) {
        swdiag_error("POSIX critical section attr failed");
        free(cs);
        return (NULL);
    }
    
    rc = pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);

    if (rc) {
        swdiag_error("POSIX critical section settype failed");
        free(cs);
        return (NULL);
    }

    rc = pthread_mutex_init(&cs->mutex, &attr);
    if (rc) {
        swdiag_error("POSIX critical section init failed");
        free(cs);
        return (NULL);
    }
    return (cs);
}

/*
 * Enter the given critical section
 */
void swdiag_xos_critical_section_enter (xos_critical_section_t *cs)
{
    int rc;
    if ((rc = pthread_mutex_trylock(&cs->mutex)) != 0) {
        //swdiag_debug("*** BLOCKING ON MUTEX %s ***", strerror(rc));
        pthread_mutex_lock(&cs->mutex);
    }
}

/*
 * Exit the given critical section
 */
void swdiag_xos_critical_section_exit (xos_critical_section_t *cs)
{
    pthread_mutex_unlock(&cs->mutex);
}

/*
 * Delete a critical section
 */
void swdiag_xos_critical_section_delete (xos_critical_section_t *cs)
{
    pthread_mutex_destroy(&cs->mutex);
    free(cs);
}

/*************************************************************
 * POSIX thread functions
 *************************************************************/

/*
 * Create a new thread and mutex, and lock it's mutex immediately before
 * it is ready to run at the function given by start_routine.
 * The data for the new thread is stored in parameter.
 */
xos_thread_t *swdiag_xos_thread_create (const char *name,
                                        xos_thread_start_fn_t *start_fn, 
                                        swdiag_thread_t *swdiag_thread)
{
    boolean rc;
    xos_thread_t *thread;
    pthread_t tid;

    if (!swdiag_thread || !start_fn) {
        return (NULL);
    }
    thread = (xos_thread_t *)malloc(sizeof(xos_thread_t));
    if (!thread) {
        swdiag_error("POSIX malloc");
        return (NULL);
    }
    thread->tid = 0;
    thread->work_to_do = FALSE;
    swdiag_thread->xos = thread;
    swdiag_thread->name = strdup(name);

    rc = pthread_mutex_init(&thread->run_test_mutex, NULL);
    if (rc) {
        swdiag_error("POSIX mutex init failed with %d", rc);
        free(thread);
        return (NULL);
    }
    rc = pthread_cond_init(&thread->cond, NULL);
    if (rc) {
        swdiag_error("POSIX cond init failed");
        free(thread);
        return (NULL);
    }

    rc = pthread_create(&tid, NULL, (void*)start_fn, swdiag_thread);
    if (rc) {
        swdiag_error("POSIX thread create failed");
        free(thread);
        return (NULL);
    }
    if (thread->tid == 0) {
        thread->tid = tid;
        swdiag_thread->id = (int)tid;
    }

    swdiag_debug(NULL, "POSIX thread %p created", thread->tid);
    return (thread);
}

/*
 * Destroy another thread given by the parameter.
 */
boolean swdiag_xos_thread_destroy (xos_thread_t *thread)
{
    int rc;
    int tid;

    if (!thread) {
        swdiag_error("POSIX thread destroy");
        return (FALSE);
    }

    tid = thread->tid;
    rc = pthread_cancel(tid);
    if (rc) {
        swdiag_debug(NULL, "POSIX destroy %d failed with %d", tid, rc);
        return (FALSE);
    }

    free(thread);
    swdiag_debug(NULL, "POSIX thread %d destroyed", tid);
    return (TRUE);
}

/*
 * Wait the current thread until a test is ready to execute.
 * Specifically the current thread waits on a lock.
 * The current thread data is given by the parameter.
 */
boolean swdiag_xos_thread_wait (xos_thread_t *thread)
{
    int rc;

    if (!thread) {
        swdiag_error("POSIX thread wait");
        return(FALSE);
    }

    rc = pthread_mutex_lock(&thread->run_test_mutex);
    if (rc) {
        swdiag_debug(NULL, "POSIX wait %d lock failed with %d", (int)thread->tid, rc);
        return(FALSE);
    }

    while (!thread->work_to_do) {
        /*
         * The pthread_cont_wait() will unlock the mutex and wait for
         * it to be signalled again by xos_thread_release().
         */
        rc = pthread_cond_wait(&thread->cond, &thread->run_test_mutex);
        if (rc) {
            swdiag_debug(NULL, "POSIX wait %d condvar failed with %d", (int)thread->tid, rc);
        }
    }
    thread->work_to_do = FALSE;

    rc = pthread_mutex_unlock(&thread->run_test_mutex);
    if (rc) {
        swdiag_debug(NULL, "POSIX wait %d unlock failed with %d", (int)thread->tid, rc);
        return (FALSE);
    }

    //swdiag_debug(NULL, "POSIX thread %d started", (int)thread->tid);
    return (TRUE);
}

/*
 * Release another thread that has been held pending a test to execute.
 * Specifically unlocks the given mutex in the thread data given by the param.
 * The current thread has been holding this mutex until now.
 */
boolean swdiag_xos_thread_release (xos_thread_t *thread)
{
    int rc;

    if (!thread) {
        swdiag_error("POSIX thread release");
        return (FALSE);
    }

    rc = pthread_mutex_lock(&thread->run_test_mutex);
    if (rc) {
        swdiag_debug(NULL, "POSIX lock %d failed with %d", (int)thread->tid, rc);
        return (FALSE);
    }

    if (thread->work_to_do) {
        swdiag_error("POSIX thread already running");
        pthread_mutex_unlock(&thread->run_test_mutex);
        return (FALSE);
    }
    thread->work_to_do = TRUE;
    rc = pthread_cond_signal(&thread->cond);
    if (rc) {
        swdiag_debug(NULL, "POSIX release %d condvar failed with %d", (int)thread->tid, rc);
        pthread_mutex_unlock(&thread->run_test_mutex);
        return (FALSE);
    }
    rc = pthread_mutex_unlock(&thread->run_test_mutex);
    if (rc) {
        swdiag_debug(NULL, "POSIX release %d unlock failed with %d", (int)thread->tid, rc);
        return (FALSE);
    }

    // swdiag_debug(NULL, "POSIX thread %d released", (int)thread->tid);
    return (TRUE);
}

/*
 * Return the OS specific thread id of the current thread.
 * For POSIX we also set the value internally as pthread_create() may have
 * kicked off the thread before it has a chance to return with the TID.
 */
int swdiag_xos_thread_get_id (xos_thread_t *thread)
{
    if (!thread) {
        swdiag_error("POSIX thread set id");
        return (0);
    }

    thread->tid = pthread_self();
    return ((int)thread->tid);
}

/*
 * Return the CPU percentage this thread has used over the last 
 * minute. TBD - not even sure if we can do this properly on
 * posix threads - depends on the thread implementation. It's
 * safe to assume 0 for now.
 */
long swdiag_xos_thread_cpu_last_min (xos_thread_t *thread)
{
    long cpu = 0;

    return(cpu);
}
