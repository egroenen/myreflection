/* 
 * swdiag_sched.c - SW Diagnostics Scheduler
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
 * Schedule periodic tests to run.
 */

#include "swdiag_client.h"

#include "swdiag_types.h"
#include "swdiag_xos.h"
#include "swdiag_sched.h"
#include "swdiag_obj.h"
#include "swdiag_sequence.h"
#include "swdiag_api.h"
#include "swdiag_trace.h"
#include "swdiag_thread.h"
#include "swdiag_util.h"

/*
 * The scheduler test and thread queues
 */
static sched_test_queue_t test_queues[NBR_TEST_QUEUES] =
{{TEST_QUEUE_IMMEDIATE, "Immediate", NULL},
 {TEST_QUEUE_FAST,      "Fast",      NULL},
 {TEST_QUEUE_NORMAL,    "Normal",    NULL},
 {TEST_QUEUE_SLOW,      "Slow",      NULL},
 {TEST_QUEUE_USER,      "User",      NULL}
};

static swdiag_thread_t *sched_thread = NULL;
static xos_timer_t *test_start_timer = NULL;

static void check_test_start_timer(void);

static boolean queues_blocked = FALSE;

/*******************************************************************
 * Local functions
 *******************************************************************/

/*
 * Dequeue a test in preparation for running it
 */
static void dequeue_test_for_start (sched_test_queue_t *test_queue)
{
    sched_test_t *sched_test;
    obj_test_t *test;
    obj_instance_t *instance;
    
    sched_test = swdiag_list_pop(test_queue->queue);

    if (!sched_test) {
        swdiag_error("SCHED no scheduled test");
        return;
    }

    instance = sched_test->instance;

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_TEST)) {
        swdiag_error("SCHED invalid scheduled test object");
        return;
    }

    swdiag_debug(instance->obj->i.name, 
                 "SCHED dequeue test '%s' for start from %s queue", 
                 swdiag_obj_instance_name(instance), test_queue->name);

    test = instance->obj->t.test;

    /*
     * Ask the sequencer to start the test.
     */
    if (test->type == OBJ_TEST_TYPE_POLLED) {  
        /*
         * Test is no longer on a queue.
         */
        sched_test->queued = TEST_QUEUE_NONE;
        swdiag_seq_from_test(instance);
    } else {
        /*
         * A notification, we could be here because auto-pass was set at
         * a user configured time, or we could be here for a root cause
         * notification. We can tell the difference based on which queue 
         * the test was in.
         */
        if (sched_test->queued == TEST_QUEUE_USER) {
            sched_test->queued = TEST_QUEUE_NONE;
            if (test->autopass != AUTOPASS_UNSET) {
                swdiag_seq_from_test_notify(instance, SWDIAG_RESULT_PASS, 0);
            }
        } else {
            /*
             * Must be immediate, and therefore an RCI feedback, feeding
             * back the previews notified test result and value.
             */
            sched_test->queued = TEST_QUEUE_NONE;
            swdiag_debug(instance->obj->i.name,
                         "SCHED: Immediate Notify for '%s'", 
                         swdiag_obj_instance_name(instance));
            swdiag_seq_from_test_notify_rci(instance,
                                            instance->last_result,
                                            instance->last_value);
        }
    }
}

/*
 * Check the scheduled test times at the head of each scheduler queue
 * and if expired, dequeue the test and trigger the thread to execute it.
 * Finally we call a function that restarts any timers if necessary.
 */
static void check_queue_test_times (void)
{
    int q;
    sched_test_queue_t *test_queue;
    sched_test_t *sched_test;
    xos_time_t time_now;
    boolean found = FALSE;

    if (queues_blocked) {
        /*
         * Don't do anything if the queues are blocked.
         */
        swdiag_debug(NULL, "%s:Queues Blocked", __FUNCTION__);
        return;
    }

    //swdiag_debug(NULL, "SCHED checking queue times");

    swdiag_xos_time_set_now(&time_now);

    /*
     * Take the first from each queue and send it off to be scheduled, continue
     * looping across the threads, interleaving the requests, until there
     * are no more tests that are due to be scheduled at this time.
     */
    do {
        found = FALSE;
        for (q = TEST_QUEUE_FIRST; q < NBR_TEST_QUEUES; q++) {
            test_queue = &test_queues[q];
            
            sched_test = swdiag_list_peek(test_queue->queue);
            
            if (sched_test && XOS_TIME_LT(sched_test->next_time, time_now)) {
                //swdiag_debug(NULL, "SCHED check queues (detect) %s starting test %s",
                //             test_queue->name, sched_test->instance->name);
                dequeue_test_for_start(test_queue);
                found = TRUE;
            }
        }
    } while (found);

    /*
     * Restart the timer for the next test.
     */
    check_test_start_timer();
}

/*
 * Timer callback that signals the event thread to properly handle the expiry.
 */
static void test_start_timer_expired (void *context)
{
    swdiag_debug(NULL, "SCHED start timer expired");

    if (!sched_thread) {
        return;
    }
    
    /*
     * Signal the event thread to wake up
     */
    swdiag_debug(NULL, "SCHED releasing event thread %d", sched_thread->id);
    if (!swdiag_xos_thread_release(sched_thread->xos)) {
        swdiag_error("SCHED failed to release thread");
    }
}

/*
 * Infinite loop that processes timeouts, checks queues for tests, and
 * awakens test threads when required.
 */
static void sched_thread_main (swdiag_thread_t *thread)
{
    xos_event_t event = XOS_EVENT_TEST_START; // TEMP until supported by XOS

    if (!sched_thread) {
        /*
         * Make sure everyone is up to speed on who we are.
         */
        sched_thread = thread;
    }

    swdiag_debug(NULL, "Schedular thread started");

    /*
     * Create the test start timer that wakes the main
     */
    if (test_start_timer) {
        swdiag_xos_timer_delete(test_start_timer);
    }
    test_start_timer = swdiag_xos_timer_create(test_start_timer_expired, NULL);

    check_queue_test_times();

    while (!sched_thread->quit) {
        swdiag_debug(NULL, "SCHED event thread about to wait");
        swdiag_xos_thread_wait(sched_thread->xos);
        swdiag_debug(NULL, "SCHED event thread woken");
        switch (event) {
        case XOS_EVENT_TEST_START:
            swdiag_obj_db_lock();
            check_queue_test_times();
            swdiag_obj_db_unlock();
            break;

        case XOS_EVENT_GUARD_TIMEOUT:
            break;

        default:
            break;
        }
    }
    swdiag_debug(NULL, "Schedular thread exited");

    swdiag_xos_thread_destroy(sched_thread);
    free(sched_thread);
    sched_thread = NULL;
}

/*
 * Create and initialize the scheduler queues
 */
static void create_queues (void)
{
    test_queues[TEST_QUEUE_IMMEDIATE].queue = swdiag_list_create();
    test_queues[TEST_QUEUE_FAST].queue = swdiag_list_create();
    test_queues[TEST_QUEUE_NORMAL].queue = swdiag_list_create();
    test_queues[TEST_QUEUE_SLOW].queue = swdiag_list_create();
    test_queues[TEST_QUEUE_USER].queue = swdiag_list_create();
}

static void destroy_queues (void)
{
	sched_test_t *sched_test;
	int q;
	sched_test_queue_t *test_queue;

	queues_blocked = TRUE;

	for (q = TEST_QUEUE_FIRST; q < NBR_TEST_QUEUES; q++) {
		test_queue = &test_queues[q];
		while ((sched_test = swdiag_list_pop(test_queue->queue)) != NULL) {
			sched_test->queued = TEST_QUEUE_NONE;
		}
		swdiag_list_free(test_queue->queue);
	}
}
/*
 * Check all the queues and start/restart the test start timer if necessary
 */
static void check_test_start_timer (void)
{
    int q;
    sched_test_queue_t *soonest_queue = NULL;
    xos_time_t soonest_time;
    sched_test_t *sched_test;

    if (queues_blocked) {
        /*
         * Don't start the timer when the queues are blocked.
         */
        return;
    }

    if (!test_start_timer) {
        return;
    }

    /*
     * Find the next expiry time across all queues
     * (which may be in the past in some conditions)
     */
    for (q = TEST_QUEUE_FIRST; q < NBR_TEST_QUEUES; q++) {
        sched_test = swdiag_list_peek(test_queues[q].queue);

        if (!sched_test) {
            continue;
        }

        if (!soonest_queue || 
            XOS_TIME_LT(sched_test->next_time, soonest_time)) {
            soonest_queue = &test_queues[q];
            soonest_time = sched_test->next_time;
        }
    }

    if (soonest_queue) {
        xos_time_t time_now, delay;

        swdiag_xos_time_set_now(&time_now);
        sched_test = swdiag_list_peek(soonest_queue->queue);

        if (XOS_TIME_LT(soonest_time, time_now)) {
            /*
             * The soonest test should have been started already! 
             * no need to start the timer, just inform the schedular
             * thread to check the queues and run this test.
             *
             * Ignore any errors if it is already running.
             */
            //swdiag_debug(NULL, "SCHED check test start - immediately "
            //             "releasing thread to run %s",
            //             sched_test->instance->name);

            swdiag_xos_thread_release(sched_thread->xos);

        } else {
            //swdiag_debug(NULL, "SCHED check test start - '%s', now=%lu, then=%lu",
            //             sched_test->instance->name, time_now.sec, 
            //             sched_test->next_time.sec);

            swdiag_xos_time_diff(&time_now, &sched_test->next_time, &delay);
            /*
             * An extra bit of padding so that we expire just a bit later 
             * (1s) than the test is due to start.
             * 
             * This ensures that when we search for the expired test that
             * it is less than the current time.
             */     
            if ((delay.nsec + 1e8) > 1e9) {
                delay.sec++;
                delay.nsec = (1e9 - delay.nsec) + 1e8;
            } else {
                delay.nsec += 1e8;
            }

            //swdiag_debug(NULL, "SCHED check test start - starting timer for "
            //             "expiry of %s in (%lu.%lu)",
            //             sched_test->instance->name, delay.sec, delay.nsec);
            swdiag_xos_timer_start(test_start_timer, delay.sec, delay.nsec);
        }
    } else {
        swdiag_debug(NULL, "SCHED check test start - queues empty");
    }
}

/*
 * Add a test to the tail of the specified queue
 */
void swdiag_sched_add_test (obj_instance_t *instance, boolean force)
{
    test_queue_t queue_e;
    sched_test_queue_t *test_queue = NULL;
    sched_test_t *sched_test, *list_sched_test;
    obj_test_t *test;
    swdiag_list_element_t *curr_element, *prev_list_element;
    ulong period = 0;
    ulong period_sec = 0;
    ulong period_nsec = 0;

    if (!instance || !swdiag_obj_validate(instance->obj, OBJ_TYPE_TEST) ||
        instance->state != OBJ_STATE_ENABLED) {
        /*
         * Only add enabled tests to the schedular.
         */
        swdiag_debug(NULL, "Ignoring test '%s' addition to schedular", 
                     instance ? instance->name : "unknown");
        return;
    }

    if (queues_blocked && !force) {
        /*
         * queues are blocked at the moment, don't add this test
         * back to the queues, something else will have to do that.
         */
        swdiag_debug(instance->obj->i.name,
                     "Ignoring test '%s' addition to schedular, blocked", 
                     swdiag_obj_instance_name(instance));
        return;
    }

    test = instance->obj->t.test;

    /*
     * This may be a polled test, or a notification test with autopass
     * set.
     */
    if (test->type == OBJ_TEST_TYPE_POLLED) {
        /*
         * Identify the queue to use
         */
        period = test->period;
        switch (period) {
        case SWDIAG_PERIOD_SLOW:
            queue_e = TEST_QUEUE_SLOW;
            break;
        case SWDIAG_PERIOD_NORMAL:
            queue_e = TEST_QUEUE_NORMAL;
            break;
        case SWDIAG_PERIOD_FAST:
            queue_e = TEST_QUEUE_FAST;
            break;
        default:
            queue_e = TEST_QUEUE_USER;
            break;
        }
    } else {
        if (test->autopass > AUTOPASS_UNSET &&
            instance->last_result == SWDIAG_RESULT_FAIL) {
            /*
             * Schedule to autopass
             */
            queue_e = TEST_QUEUE_USER;
            period = test->autopass;
        } else {
            return;
        }
    }
    sched_test = &instance->sched_test;
    sched_test->instance = instance; // Should go into the test creation

    test_queue = &test_queues[queue_e];

    if (!(test_queue->queue)) {
        /*
         * Queues aren't initialised as yet, we will add this test later
         * when the queues are initialised.
         */
        swdiag_debug(instance->obj->i.name,
                     "Ignoring test '%s' addition to schedular, no queues", 
                     swdiag_obj_instance_name(instance));
        return;
    }

    if (sched_test->queued == TEST_QUEUE_NONE) {
        sched_test->queued = queue_e;
    } else if (sched_test->queued != queue_e || 
               queue_e == TEST_QUEUE_USER) {
        /*
         * remove from the current queue
         */
        if (swdiag_list_remove(test_queues[sched_test->queued].queue, 
                               sched_test)) {
            sched_test->queued = queue_e;
        }
    }  else {
        swdiag_debug(instance->obj->i.name,
                     "SCHED Ignoring double add of test '%s' to queue %s when it is already in queue %s",
                     swdiag_obj_instance_name(instance), test_queue->name, 
                     test_queues[sched_test->queued].name);
        return;
    }

    /*
     * Set the run time, and if applicable restart the event timer
     */
    period_sec = period / 1000;
    period_nsec = (period % 1000) * 1e6;
    swdiag_xos_time_set_now(&sched_test->next_time);
    sched_test->next_time.sec += period_sec;
    sched_test->next_time.nsec += period_nsec;

    if (queue_e == TEST_QUEUE_USER) {
        /*
         * Search forwards through the queue and insert this test 
         * at the appropriate point. Search forwards from the head
         * to improve the performance on tests at high frequencies.
         */
        curr_element = test_queue->queue->head;
        prev_list_element = NULL;
        while(curr_element) {
            list_sched_test = curr_element->data;
            if (XOS_TIME_LT(sched_test->next_time,
                            list_sched_test->next_time)) {
                break;
            }
            prev_list_element = curr_element;
            curr_element = curr_element->next;
        }
        swdiag_list_insert(test_queue->queue, prev_list_element, sched_test);
    } else {
        swdiag_list_push(test_queue->queue, sched_test);
    }
    swdiag_debug(instance->obj->i.name,
                 "SCHED %s queue added test '%s' to run in %lus %luns",
                 test_queue->name, swdiag_obj_instance_name(instance), 
                 period_sec, period_nsec);

    check_test_start_timer();
}

/*
 * swdiag_sched_rule_immediate()
 *
 * Add all the tests that feed into this rule for immediate testing.
 *
 * Check our input
 */
void swdiag_sched_rule_immediate (obj_instance_t *rule_instance)
{
    swdiag_list_element_t *element;
    obj_t *rule_obj, *input_obj;
    obj_instance_t *test_instance, *parent_instance;

    if (!swdiag_obj_instance_validate(rule_instance, OBJ_TYPE_RULE)) {
        swdiag_error("Scheduler passed invalid rule instance");
        return;
    }

    swdiag_trace(rule_instance->obj->i.name,
                 "SCHED: Run all tests for rule '%s'", 
                 swdiag_obj_instance_name(rule_instance));

    rule_obj = rule_instance->obj;

    /*
     * For each input from this rule check whether it is a test, if so
     * schedule it, else recurse into that next rule down.
     */
    for(element = rule_obj->t.rule->inputs->head;
        element != NULL;
        element = element->next) {

        input_obj = (obj_t*)element->data;

        if (input_obj->type != OBJ_TYPE_TEST) {
            parent_instance = swdiag_obj_instance(input_obj, rule_instance);
            swdiag_sched_rule_immediate(parent_instance);
        } else if (input_obj->type == OBJ_TYPE_TEST) {
            test_instance = swdiag_obj_instance(input_obj, rule_instance);

            /* 
             * Schedule the test to run ASAP.
             */
            swdiag_trace(NULL, "SCHED: Immediate test '%s' being queued", 
                         swdiag_obj_instance_name(test_instance));
            swdiag_sched_test_immediate(test_instance);
        } 
    }
}

/*
 * swdiag_sched_test_immediate()
 *
 * Remove this test from whatever queue it is currently on, and put it
 * on the immediate queue for testing now. 
 */
void swdiag_sched_test_immediate (obj_instance_t *test_instance)
{
    sched_test_queue_t *test_queue = NULL;
    sched_test_t *sched_test;

    if (!swdiag_obj_instance_validate(test_instance, OBJ_TYPE_TEST)) {
        swdiag_error("Scheduler passed invalid test instance");
        return;
    }

    if (test_instance->sched_test.queued == TEST_QUEUE_IMMEDIATE) {
        /*
         * Already queued to run immediately, do nothing.
         */
        swdiag_debug(test_instance->obj->i.name,
                     "SCHED: Ignoring request to run '%s' since it is already queued to run immediately",
                     swdiag_obj_instance_name(test_instance));
        return;
    }
    
    if (test_instance->sched_test.queued != TEST_QUEUE_NONE) {
        swdiag_sched_remove_test(test_instance);
    } else {
        if (test_instance->obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
            /*
             * Must be currently running since it isn't enqueued. So don't
             * execute it again.
             */
            swdiag_debug(test_instance->obj->i.name,
                         "SCHED: Ignoring request to run '%s', already running",
                         swdiag_obj_instance_name(test_instance));
            return;
        }
    }

    if (queues_blocked) {
        /*
         * Ignore requests to run tests when queues blocked.
         */
        swdiag_debug(test_instance->obj->i.name,
                     "Ignoring test '%s' addition to schedular, blocked", 
                     swdiag_obj_instance_name(test_instance));
        return;
    }

    sched_test = &test_instance->sched_test;

    test_queue = &test_queues[TEST_QUEUE_IMMEDIATE];

    swdiag_xos_time_set_now(&sched_test->next_time);

    sched_test->queued = TEST_QUEUE_IMMEDIATE;

    swdiag_list_push(test_queue->queue, sched_test);

    swdiag_debug(test_instance->obj->i.name,
                 "SCHED %s queue added test %s to run immediately",
                 test_queue->name, swdiag_obj_instance_name(test_instance));

    check_test_start_timer();
}

/*
 * swdiag_sched_remove_test()
 *
 * Remove this test from the schedular if it is queued.
 */
void swdiag_sched_remove_test (obj_instance_t *instance)
{
    if (instance && instance->sched_test.queued != TEST_QUEUE_NONE) {
        if (swdiag_list_remove(test_queues[instance->sched_test.queued].queue, 
                               &instance->sched_test)) {
            instance->sched_test.queued = TEST_QUEUE_NONE;
        }
    } 
    check_test_start_timer();
}

/*
 * validate_schedular()
 *
 * Internal test to validate that all is well within the schedular itself.
 */
static swdiag_result_t validate_schedular (const char *instance_name,
                                           void *context, long *retval)
{
    swdiag_obj_db_lock();
    swdiag_obj_db_unlock();
    return(SWDIAG_RESULT_PASS);
}

static void swdiag_sched_start (void)
{
    if (sched_thread) {
        /*
         * Schedular already running!
         */
        swdiag_error("Schedular already running when started");
        return;
    }

    /*
     * Create the main schedular thread.
     */
    sched_thread = (swdiag_thread_t *)malloc(sizeof(swdiag_thread_t));
    if (!sched_thread) {
        swdiag_error("Failed to alloc schedular thread");
        return;
    }
    sched_thread->quit = FALSE;
    sched_thread->xos = swdiag_xos_thread_create("SWDiag Schedular",
                                                 sched_thread_main, 
                                                 sched_thread);
    sched_thread->job = NULL;

    if (!sched_thread->xos) {
        swdiag_error("Failed to create schedular thread");
        return;
    } 
}

/*
 * recover_schedular()
 *
 * If we find a problem within the schedular then clear the schedular queues
 * and walk the entire DB looking for test instances, and enqueue them all
 * to be polled.
 */
static swdiag_result_t recover_schedular (const char *instance_name,
                                          void *context)
{
    sched_test_t *sched_test;
    obj_t *obj;
    obj_test_t *test;
    obj_instance_t *instance;
    int q;
    sched_test_queue_t *test_queue;

    swdiag_obj_db_lock();

    if (!sched_thread) {
         swdiag_sched_start();
    }

    /*
     * Clear all the test queues. There may be some tests in progress
     * so block any new additions to the queues in the meantime, they
     * can get added in a moment.
     */
    queues_blocked = TRUE;

    for (q = TEST_QUEUE_FIRST; q < NBR_TEST_QUEUES; q++) {
        test_queue = &test_queues[q];
        while ((sched_test = swdiag_list_pop(test_queue->queue)) != NULL) {
            sched_test->queued = TEST_QUEUE_NONE;
        }
    }

    /* 
     * For all tests in the DB...
     */
    obj = swdiag_obj_get_first_rel(NULL, OBJ_REL_TEST);
    while(obj) {
        /*
         * Now for all the instances, connected to this test.
         */
        for(instance = &obj->i; 
            instance != NULL; 
            instance = instance->next) {

            test = instance->obj->t.test;

            if (test->type == OBJ_TEST_TYPE_POLLED) {
                sched_test = &instance->sched_test;
                sched_test->next_time.sec = 0;
                sched_test->next_time.nsec = 0;
                
                swdiag_sched_add_test(instance, TRUE);
            }
        }
        obj = swdiag_obj_get_next_rel(obj, OBJ_REL_NEXT_IN_SYS);
    }
    queues_blocked = FALSE;

    /*
     * Kick off the schedular timer again in case it has expired
     * in the mean time.
     */
    check_test_start_timer();

    swdiag_obj_db_unlock();

    return(SWDIAG_RESULT_PASS);
}

/*
 * swdiag_sched_init()
 *
 * Initialise the scheduler.
 */
void swdiag_sched_init (void)
{
    obj_t *obj;
    obj_test_t *test;
    obj_instance_t *instance;

    swdiag_obj_db_lock();

    swdiag_sched_start();

/*    swdiag_test_create_polled(SWDIAG_SCHEDULAR_TEST,
                              validate_schedular,
                              NULL,
                              SWDIAG_PERIOD_SLOW);

    swdiag_action_create(SWDIAG_SCHEDULAR_RECOVER,
                         recover_schedular,
                         NULL);
    
    swdiag_rule_create(SWDIAG_SCHEDULAR_RULE,
                       SWDIAG_SCHEDULAR_TEST,
                       SWDIAG_SCHEDULAR_RECOVER);

    swdiag_comp_create(SWDIAG_COMPONENT);
    
    swdiag_comp_contains_many(SWDIAG_COMPONENT,
                              SWDIAG_SCHEDULAR_RULE,
                              SWDIAG_SCHEDULAR_TEST,
                              SWDIAG_SCHEDULAR_RECOVER,
                              NULL);

    swdiag_test_chain_ready(SWDIAG_SCHEDULAR_TEST);*/

    create_queues();

    /*
     * Queue all existing tests to the test queues, rest will be added
     * as they are created.
     */
    obj = swdiag_obj_get_first_rel(NULL, OBJ_REL_TEST);
    while (obj) {
        /*
         * Now for all the instances, connected to this test.
         */
        swdiag_debug(obj->i.name,
                     "Evaluating test %s for schedular", obj->i.name);
        for(instance = &obj->i; 
            instance != NULL; 
            instance = instance->next) {

            if(!swdiag_obj_instance_validate(instance, OBJ_TYPE_TEST)) {
                swdiag_error("Failed to validate test instance %s, skipping", 
                             instance->name);
                continue;
            }

            test = instance->obj->t.test;

            if (test->type == OBJ_TEST_TYPE_POLLED) {
                swdiag_debug(instance->obj->i.name,
                             "Adding polled test %s to schedular", 
                             instance->name);
                swdiag_sched_add_test(instance, FALSE);
            }
        }
        obj = swdiag_obj_get_next_rel(obj, OBJ_REL_NEXT_IN_SYS);
    }
    swdiag_obj_db_unlock();
}

void swdiag_sched_terminate()
{
	swdiag_sched_kill();
	destroy_queues();
}
/*
 * swdiag_sched_kill()
 *
 * Next time through our schedular loop we should die.
 */
void swdiag_sched_kill (void)
{
    if (sched_thread) {
        sched_thread->quit = TRUE;
        swdiag_xos_thread_release(sched_thread->xos);
        swdiag_xos_sleep(1); // Give it time to run and exit
    }
}

sched_test_queue_t *swdiag_sched_ut_get_queues (void)
{
    return(test_queues);
}

void swdiag_sched_ut_recover (void)
{
    (void)recover_schedular(NULL, NULL);
}

void swdiag_sched_ut_start (void)
{
    swdiag_sched_start();
}
