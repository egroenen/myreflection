/* 
 * myrefl_thread.c - SW Diagnostics Thread Queue
 *
 * Copyright (c) 2007-2009 Cisco Systems Inc.
 * Copyright (c) 2010-2015 Edward Groenendaal
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

#include "myrefl_thread.h"
#include "myrefl_util.h"
#include "myrefl_trace.h"

/*
 * Keep at least 50 free job requests spare for moments of activity
 * when out of memory.
 */
#define THREAD_REQUEST_LOW_WATER 50

/*
 * Keep track of the threads and jobs
 */
static myrefl_list_t *thread_free_queue = NULL;
static myrefl_list_t *thread_executing_queue = NULL;
static myrefl_list_t *job_pending_queue = NULL;

static myrefl_list_t *free_job_requests = NULL;

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

long myrefl_thread_ut_get_delay (void)
{
    return(throttle_delay);
}

/*
 * myrefl_thread_cpu()
 *
 * Return how much CPU swdiags has used over the last minute. 
 *
 * Note that all
 * CPU % down to integers.
 */
long myrefl_thread_cpu (void)
{
    myrefl_list_element_t *element;
    myrefl_thread_t *thread;
    long cpu = 0;

    if (!thread_free_queue || !thread_executing_queue) {
    	return 0;
    }
    /*
     * Check running processes first
     */
    for(element = thread_free_queue->head; element; element = element->next) {
        thread = element->data;
        cpu += myrefl_xos_thread_cpu_last_min(thread->xos);
    }

    for(element = thread_executing_queue->head; element; element = element->next) {
        thread = element->data;
        cpu += myrefl_xos_thread_cpu_last_min(thread->xos);
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
        cpu = myrefl_thread_cpu();

        if (cpu > warn_threshold) {
            throttle_delay = (MYREFL_THREAD_HIGH_DELAY / range) * (cpu - warn_threshold);
            if (throttle_delay > MYREFL_THREAD_MAX_DELAY) {
                throttle_delay = MYREFL_THREAD_MAX_DELAY;
            }
        } else {
            throttle_delay = 0;
        }
    }   
}

static myrefl_result_t myrefl_thread_cpu_monitor (const char *instance,
                                                  void *context,
                                                  long *cpu)
{
    *cpu = myrefl_thread_cpu();
    return(MYREFL_RESULT_VALUE);
}

static myrefl_result_t myrefl_thread_throttle (const char *instance,
                                               void *context)
{
    calculate_throttle_delay();

    return(MYREFL_RESULT_PASS);
}

/*
 * thread_main()
 *
 * Main thread that waits until a job is available.
 */
static void thread_main (myrefl_thread_t *thread)
{
    thread->id = myrefl_xos_thread_get_id(thread->xos);

    myrefl_debug(NULL, "Work thread %s(%d) created", thread->name, thread->id);

    while(!thread->quit) {
        if (myrefl_xos_thread_wait(thread->xos)) {

        	if (thread->quit) {
        		continue;
        	}
            /*
             * We have a job to do.
             */
            while(thread->job) {

                /*
                 * Throttle the threads if required.
                 */
                if (throttle_delay) {
                    myrefl_xos_sleep(throttle_delay);
                    calculate_throttle_delay();
                }

                /*
                 * Run the job.
                 */
                myrefl_debug(NULL, "Thread %s(%d) starting job %p", thread->name, thread->id, thread->job);
                thread->job->execute(thread, thread->job->context);
                myrefl_debug(NULL, "Thread %s(%d) completed job", thread->name, thread->id);

                /*
                 * Finished, free or recycle the job
                 */
                if (free_job_requests && 
                    free_job_requests->num_elements < THREAD_REQUEST_LOW_WATER) {
                	// Recyle the job.
                    myrefl_list_push(free_job_requests, thread->job);
                } else {
                	// Free it, we have enough job in the free queue.
                    free(thread->job);
                }
                thread->job = NULL;

                /*
                 * Any other jobs pending? Lets do them now.
                 */
                thread->job = myrefl_list_pop(job_pending_queue);
            }

            /*
             * No more work to do, Remove the thread from the executing queue
             */
            myrefl_list_remove(thread_executing_queue, thread);
            myrefl_list_push(thread_free_queue, thread);
        } else {
            myrefl_thread_kill(thread);
        }
    }

    /*
     * Thread is quitting, free memory for the thread.
     */    
    myrefl_debug(NULL, "Thread %s(%d) killed", thread->name, thread->id);
    myrefl_xos_thread_destroy(thread);
    free(thread);
}

/*
 * myrefl_thread_init()
 *
 * Create the threads and add then to the free queue.
 */
void myrefl_thread_init (void)
{
    int i;
    myrefl_thread_t *thread;
    obj_t *obj;

    thread_free_queue = myrefl_list_create();
    thread_executing_queue = myrefl_list_create();
    job_pending_queue = myrefl_list_create();
    free_job_requests = myrefl_list_create();

    if (!thread_free_queue || !thread_executing_queue || !job_pending_queue ||
        !free_job_requests) {
        myrefl_error("Could not initialise threads");
        return;

    }

    for (i=0; i<THREAD_REQUEST_LOW_WATER; i++) {
        thread_job_t *job;
        job = malloc(sizeof(thread_job_t));
        if (job) {
            myrefl_list_push(free_job_requests, job);
        }
    }

    for (i=0; i<NBR_THREADS; i++) {
        thread = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
        if (!thread) {
            myrefl_error("Failed to alloc thread");
            continue;
        }
        thread->quit = FALSE;
        thread->xos = myrefl_xos_thread_create("SWDiag Work Thread",
                                               thread_main, thread);
        //thread->guard_timer = xos_timer_create(guard_timer_expired, thread);
        thread->job = NULL;

        if (thread->xos) {
            myrefl_list_add(thread_free_queue, thread);
        } else {
            myrefl_error("Failed to create xos thread");
            if (thread->name) {
                free(thread->name);
            }
            free(thread);
        }
    }

    /*
     * Create a diagnostic that monitors our CPU and throttles it when
     * it exceeds a threshold.
     */
    myrefl_test_create_polled(MYREFL_THREAD_CPU_USAGE,
                              myrefl_thread_cpu_monitor,
                              NULL,
                              MYREFL_PERIOD_FAST);

    myrefl_action_create(MYREFL_THREAD_THROTTLE_WARN,
                         myrefl_thread_throttle,
                         (void*)MYREFL_CPU_THROTTLE_WARN);


    obj = myrefl_obj_get_by_name_unconverted(MYREFL_THREAD_THROTTLE_WARN,
                                             OBJ_TYPE_ACTION);

    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_SILENT;
    }

    myrefl_action_create(MYREFL_THREAD_THROTTLE_HIGH,
                         myrefl_thread_throttle,
                         (void*)MYREFL_CPU_THROTTLE_HIGH);

    obj = myrefl_obj_get_by_name_unconverted(MYREFL_THREAD_THROTTLE_HIGH,
                                             OBJ_TYPE_ACTION);

    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_SILENT;
    }

    myrefl_rule_create(MYREFL_THREAD_CPU_WARN,
                       MYREFL_THREAD_CPU_USAGE,
                       MYREFL_THREAD_THROTTLE_WARN);

    myrefl_rule_set_type(MYREFL_THREAD_CPU_WARN,
                         MYREFL_RULE_GREATER_THAN_N,
                         MYREFL_CPU_THROTTLE_WARN, 0);

    myrefl_rule_set_severity(MYREFL_THREAD_CPU_WARN,
                             MYREFL_SEVERITY_LOW);

    obj = myrefl_obj_get_by_name_unconverted(MYREFL_THREAD_CPU_WARN,
                                             OBJ_TYPE_RULE);
    
    if (obj) {
        throttle_warn = obj->t.rule;
    }

    myrefl_rule_create(MYREFL_THREAD_CPU_HIGH,
                       MYREFL_THREAD_CPU_USAGE,
                       MYREFL_THREAD_THROTTLE_HIGH);

    myrefl_rule_set_type(MYREFL_THREAD_CPU_HIGH,
                         MYREFL_RULE_GREATER_THAN_N,
                         MYREFL_CPU_THROTTLE_HIGH, 0);

    myrefl_rule_set_severity(MYREFL_THREAD_CPU_HIGH,
                             MYREFL_SEVERITY_MEDIUM);

    obj = myrefl_obj_get_by_name_unconverted(MYREFL_THREAD_CPU_HIGH,
                                             OBJ_TYPE_RULE);

    if (obj) {
        throttle_high = obj->t.rule;
    }

    /*
     * Don't trigger the low when the high is already triggering
     */
    myrefl_depend_create(MYREFL_THREAD_CPU_WARN, MYREFL_THREAD_CPU_HIGH);

    myrefl_comp_create(MYREFL_COMPONENT);

    myrefl_comp_contains_many(MYREFL_COMPONENT,
                              MYREFL_THREAD_CPU_USAGE,
                              MYREFL_THREAD_THROTTLE_WARN,
                              MYREFL_THREAD_THROTTLE_HIGH,
                              MYREFL_THREAD_CPU_WARN,
                              MYREFL_THREAD_CPU_HIGH,
                              NULL);
    
    myrefl_test_chain_ready(MYREFL_THREAD_CPU_USAGE);

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
        job = myrefl_list_pop(free_job_requests);
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

static void thread_free_jobs (void)
{
	thread_job_t *job = NULL;

	if (free_job_requests) {
		while ((job = myrefl_list_pop(free_job_requests)) != NULL) {
			free(job);
		}
		myrefl_list_free(free_job_requests);
		free_job_requests = NULL;
	}

	if (job_pending_queue) {
		while ((job = myrefl_list_pop(job_pending_queue)) != NULL) {
			free(job);
		}
		myrefl_list_free(job_pending_queue);
		job_pending_queue = NULL;
	}
}

void myrefl_thread_terminate (void)
{
	myrefl_thread_kill_threads();
	thread_free_jobs();
}

/*
 * myrefl_thread_request()
 *
 * Accept a request to run a thread calling this function with the
 * supplied context. If there are no threads available then defer the
 * request to the pending queue until one becomes available.
 */
void myrefl_thread_request (thread_function_exe_t execute, 
                            thread_function_dsp_t display,
                            void *context)
{
    myrefl_thread_t *thread;
    thread_job_t *job;

    
    job = thread_alloc_job(execute, display, context); 

    if (!job) {
        /*
         * Could not allocate the job, we'll have to discard it
         */
        myrefl_error("Could not execute job, discarded");
        return;
    }

    //myrefl_trace(NULL, "thread free queue %d, executing %d", thread_free_queue->num_elements, thread_executing_queue->num_elements);
    thread = myrefl_list_pop(thread_free_queue);

    if (thread && !thread->quit) {
        /*
         * Got a thread, tell it to run the supplied function with the
         * context.
         */
        thread->job = job;

        if (!myrefl_xos_thread_release(thread->xos)) {
            myrefl_error("Failed to release thread");
            myrefl_thread_kill(thread);
            return;
        }
        myrefl_list_push(thread_executing_queue, thread);
    } else {
        /*
         * No threads free, add to the pending queue.
         */
        myrefl_list_push(job_pending_queue, job);
    }
}

void myrefl_thread_kill (myrefl_thread_t *thread)
{
    myrefl_debug(NULL, "Requesting thread %p to quit", thread);

    if (thread && thread->xos) {
    	thread->job = NULL;
        thread->quit = TRUE;
        myrefl_debug(NULL, "Requesting thread %p to quit", thread);
        myrefl_xos_thread_release(thread->xos);
        /*
         * Remove this thread from the free and executing queues
         */
    }
}

/*
 * myrefl_thread_ut_clear_pending()
 *
 * Remove all pending jobs for this function. Used during UTs to ensure
 * that there are no lingering jobs that can effect scheduling for other
 * UTs.
 */
void myrefl_thread_ut_clear_pending (thread_function_exe_t function)
{
    myrefl_list_element_t *element;
    thread_job_t *job;
    
    for(element = job_pending_queue->head; element; element = element->next) {
        job = element->data;

        if (job->execute == function) {
            myrefl_list_remove(job_pending_queue, job);
            /*
             * Back to the head.
             */
            element = job_pending_queue->head;
        }
    }

    throttle_delay = 0;

    return;
}

/***
 * When shutting down (or during UT) we need to be able to close all
 * our threads.
 */
void myrefl_thread_kill_threads ()
{
	myrefl_debug(NULL, "killing all threads in thread pool");

	if (thread_free_queue != NULL) {
		myrefl_debug(NULL, "killing all threads in thread free pool %p", thread_free_queue);

		// kill off the free threads and pop them from
		// the queue.
		myrefl_thread_t *thread;
		while ((thread = (myrefl_thread_t*)myrefl_list_pop(thread_free_queue)) != NULL) {
			myrefl_thread_kill(thread);
		}
		myrefl_list_free(thread_free_queue);
		thread_free_queue = NULL;
	}

	if (thread_executing_queue != NULL) {
		myrefl_debug(NULL, "killing all threads in thread executing pool %p", thread_executing_queue);

		// kill off the executing threads and pop them from
		// the queue.
		myrefl_thread_t *thread;
		while ((thread = (myrefl_thread_t*)myrefl_list_pop(thread_executing_queue)) != NULL) {
			myrefl_thread_kill(thread);
		}
		myrefl_list_free(thread_executing_queue);
		thread_executing_queue = NULL;
	}
}
