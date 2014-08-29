/* 
 * swdiag_thread.c - SW Diagnostics Thread Queue
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
 * Provide a service to obtain a thread for performing some job.
 */

#include "swdiag_thread.h"
#include "swdiag_util.h"
#include "swdiag_trace.h"

/*
 * Keep at least 50 free job requests spare for moments of activity
 * when out of memory.
 */
#define THREAD_REQUEST_LOW_WATER 50

/*
 * Keep track of the threads and jobs
 */
static swdiag_list_t *thread_free_queue = NULL;
static swdiag_list_t *thread_executing_queue = NULL;
static swdiag_list_t *job_pending_queue = NULL;

static swdiag_list_t *free_job_requests = NULL;

/*
 * How many ms to delay the scheduling of tasks due to the
 * CPU thresholds being exceeded.
 */
static int throttle_delay = 0;

/*
 * Keep a reference to the actual objects since the user could
 * change the thresholds on us - and we want to know what they
 * are.
 */
obj_rule_t *throttle_warn = NULL;
obj_rule_t *throttle_high = NULL;

long swdiag_thread_ut_get_delay (void)
{
    return(throttle_delay);
}

/*
 * swdiag_thread_cpu()
 *
 * Return how much CPU swdiags has used over the last minute. 
 *
 * Note that all
 * CPU % down to integers.
 */
long swdiag_thread_cpu (void)
{
    swdiag_list_element_t *element;
    swdiag_thread_t *thread;
    long cpu = 0;

    /*
     * Check running processes first
     */
    for(element = thread_free_queue->head; element; element = element->next) {
        thread = element->data;
        cpu += swdiag_xos_thread_cpu_last_min(thread->xos);
    }

    for(element = thread_executing_queue->head; element; element = element->next) {
        thread = element->data;
        cpu += swdiag_xos_thread_cpu_last_min(thread->xos);
    }
    return(cpu);
}

static void calculate_throttle_delay (void)
{
    long warn_threshold = 0;
    long high_threshold = 0;
    long cpu;
    
    if (throttle_warn) {
        warn_threshold = throttle_warn->op_n;
    }
    if (throttle_high) {
        high_threshold = throttle_high->op_n;
    }
    
    if (warn_threshold && high_threshold) {
        /*
         * update the throttle_delay if required 
         * based on the contents of the thresholds.
         */
        long range = high_threshold - warn_threshold;
        cpu = swdiag_thread_cpu();

        if (cpu > warn_threshold) {
            throttle_delay = (SWDIAG_THREAD_HIGH_DELAY / range) * (cpu - warn_threshold);
            if (throttle_delay > SWDIAG_THREAD_MAX_DELAY) {
                throttle_delay = SWDIAG_THREAD_MAX_DELAY;
            }
        } else {
            throttle_delay = 0;
        }
    }   
}

static swdiag_result_t swdiag_thread_cpu_monitor (const char *instance,
                                                  void *context,
                                                  long *cpu)
{
    *cpu = swdiag_thread_cpu();
    return(SWDIAG_RESULT_VALUE);
}

static swdiag_result_t swdiag_thread_throttle (const char *instance,
                                               void *context)
{
    calculate_throttle_delay();

    return(SWDIAG_RESULT_PASS);
}

/*
 * thread_main()
 *
 * Main thread that waits until a job is available.
 */
static void thread_main (swdiag_thread_t *thread)
{
    thread->id = swdiag_xos_thread_get_id(thread->xos);

    swdiag_debug(NULL, "Work thread %s(%d) created", thread->name, thread->id);

    while(!thread->quit) {
        if (swdiag_xos_thread_wait(thread->xos)) {
            /*
             * We have a job to do.
             */
            while(thread->job) {

                /*
                 * Throttle the threads if required.
                 */
                if (throttle_delay) {
                    swdiag_xos_sleep(throttle_delay);
                    calculate_throttle_delay();
                }

                // swdiag_debug(NULL, "Thread %s(%d) starting job 0x%x", thread->name, thread->id, thread->job);
                thread->job->execute(thread, thread->job->context);

                //swdiag_debug(NULL, "Thread %s(%d) completed job", thread->name, thread->id);
                /*
                 * Finished, free the job.
                 */
                if (free_job_requests && 
                    free_job_requests->num_elements < THREAD_REQUEST_LOW_WATER) {
                    swdiag_list_push(free_job_requests, thread->job);
                } else {
                    free(thread->job);
                }
                thread->job = NULL;

                /*
                 * Any other jobs pending?
                 */
                thread->job = swdiag_list_pop(job_pending_queue);
            }

            /*
             * Remove the thread from the executing queue
             */
            swdiag_list_remove(thread_executing_queue, thread);
            swdiag_list_add(thread_free_queue, thread);
        } else {
            swdiag_thread_kill(thread);
        }
    }

    /*
     * Thread is quitting, free memory for the thread.
     */    
    swdiag_debug(NULL, "Thread %s(%d) killed", thread->name, thread->id);
    free(thread);
}

/*
 * swdiag_thread_init()
 *
 * Create the threads and add then to the free queue.
 */
void swdiag_thread_init (void)
{
    int i;
    swdiag_thread_t *thread;
    obj_t *obj;

    thread_free_queue = swdiag_list_create();
    thread_executing_queue = swdiag_list_create();
    job_pending_queue = swdiag_list_create();
    free_job_requests = swdiag_list_create();

    if (!thread_free_queue || !thread_executing_queue || !job_pending_queue ||
        !free_job_requests) {
        swdiag_error("Could not initialise threads");
        return;

    }

    for (i=0; i<THREAD_REQUEST_LOW_WATER; i++) {
        thread_job_t *job;
        job = malloc(sizeof(thread_job_t));
        if (job) {
            swdiag_list_push(free_job_requests, job);
        }
    }

    for (i=0; i<NBR_THREADS; i++) {
        thread = (swdiag_thread_t *)malloc(sizeof(swdiag_thread_t));
        if (!thread) {
            swdiag_error("Failed to alloc thread");
            continue;
        }
        thread->quit = FALSE;
        thread->xos = swdiag_xos_thread_create("SWDiag Work Thread",
                                               thread_main, thread);
        //thread->guard_timer = xos_timer_create(guard_timer_expired, thread);
        thread->job = NULL;

        if (thread->xos) {
            swdiag_list_add(thread_free_queue, thread);
        } else {
            swdiag_error("Failed to create xos thread");
            if (thread->name) {
                free(thread->name);
            }
            free(thread);
        }
    }

    /*
     * Create a diagnostic that monitors our CPU and throttles it when
     * it exceeds a threshold.
     *
    swdiag_test_create_polled(SWDIAG_THREAD_CPU_USAGE,
                              swdiag_thread_cpu_monitor,
                              NULL,
                              SWDIAG_PERIOD_FAST);

    swdiag_action_create(SWDIAG_THREAD_THROTTLE_WARN,
                         swdiag_thread_throttle,
                         (void*)SWDIAG_CPU_THROTTLE_WARN);


    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_THREAD_THROTTLE_WARN,
                                             OBJ_TYPE_ACTION);

    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_SILENT;
    }

    swdiag_action_create(SWDIAG_THREAD_THROTTLE_HIGH,
                         swdiag_thread_throttle,
                         (void*)SWDIAG_CPU_THROTTLE_HIGH);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_THREAD_THROTTLE_HIGH,
                                             OBJ_TYPE_ACTION);

    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_SILENT;
    }

    swdiag_rule_create(SWDIAG_THREAD_CPU_WARN,
                       SWDIAG_THREAD_CPU_USAGE,
                       SWDIAG_THREAD_THROTTLE_WARN);

    swdiag_rule_set_type(SWDIAG_THREAD_CPU_WARN,
                         SWDIAG_RULE_GREATER_THAN_N,
                         SWDIAG_CPU_THROTTLE_WARN, 0);

    swdiag_rule_set_severity(SWDIAG_THREAD_CPU_WARN,
                             SWDIAG_SEVERITY_LOW);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_THREAD_CPU_WARN,
                                             OBJ_TYPE_RULE);
    
    if (obj) {
        throttle_warn = obj->t.rule;
    }

    swdiag_rule_create(SWDIAG_THREAD_CPU_HIGH,
                       SWDIAG_THREAD_CPU_USAGE,
                       SWDIAG_THREAD_THROTTLE_HIGH);

    swdiag_rule_set_type(SWDIAG_THREAD_CPU_HIGH,
                         SWDIAG_RULE_GREATER_THAN_N,
                         SWDIAG_CPU_THROTTLE_HIGH, 0);

    swdiag_rule_set_severity(SWDIAG_THREAD_CPU_HIGH,
                             SWDIAG_SEVERITY_MEDIUM);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_THREAD_CPU_HIGH,
                                             OBJ_TYPE_RULE);

    if (obj) {
        throttle_high = obj->t.rule;
    }

    *
     * Don't trigger the low when the high is already triggering
     *
    swdiag_depend_create(SWDIAG_THREAD_CPU_WARN, SWDIAG_THREAD_CPU_HIGH);

    swdiag_comp_create(SWDIAG_COMPONENT);

    swdiag_comp_contains_many(SWDIAG_COMPONENT,
                              SWDIAG_THREAD_CPU_USAGE,
                              SWDIAG_THREAD_THROTTLE_WARN,
                              SWDIAG_THREAD_THROTTLE_HIGH,
                              SWDIAG_THREAD_CPU_WARN,
                              SWDIAG_THREAD_CPU_HIGH,
                              NULL);
    
    swdiag_test_chain_ready(SWDIAG_THREAD_CPU_USAGE);
    */
}

/*
 * thread_alloc_job()
 *
 * Allocate a thread job, private utility function.
 */
static thread_job_t *thread_alloc_job (thread_function_exe_t execute,
                                       thread_function_dsp_t display,
                                       void *context)
{
    thread_job_t *job = NULL;

    if (free_job_requests) {
        job = swdiag_list_pop(free_job_requests);
    }
    
    if (!job) {
        job = malloc(sizeof(thread_job_t));
    }

    if (job) {
        job->execute = execute;
        job->display = display;
        job->context = context;
    }
    return(job);
}

/*
 * swdiag_thread_request()
 *
 * Accept a request to run a thread calling this function with the
 * supplied context. If there are no threads available then defer the
 * request to the pending queue until one becomes available.
 */
void swdiag_thread_request (thread_function_exe_t execute, 
                            thread_function_dsp_t display,
                            void *context)
{
    swdiag_thread_t *thread;
    thread_job_t *job;

    
    job = thread_alloc_job(execute, display, context); 

    if (!job) {
        /*
         * Could not allocate the job, we'll have to discard it
         */
        swdiag_error("Could not execute job, discarded");
        return;
    }

    thread = swdiag_list_pop(thread_free_queue);

    if (thread) {
        /*
         * Got a thread, tell it to run the supplied function with the
         * context.
         */
        thread->job = job;

        if (!swdiag_xos_thread_release(thread->xos)) {
            swdiag_error("Failed to release thread");
            swdiag_thread_kill(thread);
            return;
        }
        swdiag_list_add(thread_executing_queue, thread);
    } else {
        /*
         * No threads free, add to the pending queue.
         */
        swdiag_list_add(job_pending_queue, job);
    }
}

void swdiag_thread_kill (swdiag_thread_t *thread)
{
    if (thread && thread->xos) {
        thread->quit = TRUE;
        swdiag_xos_thread_release(thread->xos);
        /*
         * Remove this thread from the free and executing queues
         */

        /*
         * Need to clean up the xos thread?
         */

        /*
         * Can't free the memory for the thread since it may still
         * be in use.
         */
    }
}

/*
 * swdiag_thread_ut_clear_pending()
 *
 * Remove all pending jobs for this function. Used during UTs to ensure
 * that there are no lingering jobs that can effect scheduling for other
 * UTs.
 */
void swdiag_thread_ut_clear_pending (thread_function_exe_t function)
{
    swdiag_list_element_t *element;
    thread_job_t *job;
    
    for(element = job_pending_queue->head; element; element = element->next) {
        job = element->data;

        if (job->execute == function) {
            swdiag_list_remove(job_pending_queue, job);
            /*
             * Back to the head.
             */
            element = job_pending_queue->head;
        }
    }

    throttle_delay = 0;

    return;
}
