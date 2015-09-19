/*
 * check_thread.c - Unit Test Harness for myrefl_thread.c
 *
 * Copyright (c) 2014 Edward Groenendaal.
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
 *
 * Test the contents of myrefl_xos.h using the "check" UT framework.
 * Use "make check" to run these tests.
 *
 * This will confirm that the linux xos functions for threading and
 * critical sections is working properly.
 * *
 * Use ck_assert_msg() for things being tested in that test, and ck_assert()
 * where it is not core  to the test at hand.
 *
 * April 2014, Edward Groenendaal
 */
#include <check.h>
#include "../src/myrefl_thread.h"
#include "../src/myrefl_trace.h"
#include "../src/myrefl_xos.h"

struct xos_thread_t_ {
    pthread_t tid;
    pthread_mutex_t run_test_mutex;
    pthread_cond_t cond;
    boolean work_to_do;
};
/*
 * Wake up the kill thread when the timer goes off
 */
static void thread_timer_expired (void *context)
{
	myrefl_thread_t *thread = (myrefl_thread_t*)context;
    myrefl_xos_thread_release(thread->xos);
    myrefl_debug(NULL, "Test Timer Expired %p", thread);
}

static void thread_10s(myrefl_thread_t *thread)
{
	xos_timer_t *timer;
	xos_time_t time1;
	xos_time_t time2;

	myrefl_xos_time_set_now(&time1);

	timer = myrefl_xos_timer_create(thread_timer_expired, thread);
	myrefl_xos_timer_start(timer, 10, 0);
	myrefl_debug(NULL, "Test Timer Started %p", thread);
	// Waiting here for the timer to go off.
	myrefl_xos_thread_wait(thread->xos);
	myrefl_xos_time_set_now(&time2);

	myrefl_debug(NULL, "Test Timer Exited %p", thread);
}

/**
 * Check that we can init and destroy the thread pool.
 */
START_TEST (test_myrefl_thread_init)
{
	// This will start the thread pool.
	myrefl_thread_init();

	// Need a timer thread to kill off the pools in 5s.
	myrefl_thread_t *thread = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
	thread->quit = FALSE;
	thread->xos = myrefl_xos_thread_create("Test Kill Thread",
			thread_10s,
			thread);
	ck_assert(thread->xos != NULL);

	// Let the threads run, they should all exit after 2s
	// when we request that the pool be killed off.
	void *status;
	pthread_join(thread->xos->tid, &status);
	myrefl_thread_kill_threads();
	myrefl_debug(NULL, "completed init test");
}
END_TEST

static void test_func (myrefl_thread_t *thread, void *context)
{
	boolean *status = (boolean*)context;
	//myrefl_debug(NULL, "job run %p", context);
	*status = TRUE;
	myrefl_xos_sleep(10);
}

#define MAX_JOBS 1000

void test_myrefl_thread_exec_num (int num_jobs)
{
	// This will start the thread pool.
		myrefl_thread_init();

		// Create a job to run, and record whether it ran.
		static boolean jobs[MAX_JOBS];

		int i;
		for (i=0; i < num_jobs; i++) {
			jobs[i] = FALSE;
			myrefl_thread_request(test_func, NULL, &jobs[i]);
		}

		// Need a timer thread to kill off the pools in 5s.
		myrefl_thread_t *thread = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
		thread->quit = FALSE;
		thread->xos = myrefl_xos_thread_create("Test Kill Thread",
				thread_10s,
				thread);
		ck_assert(thread->xos != NULL);

		void *status;
		/* wait for the guard thread to exit, when it does check that all
		 * of our jobs actually ran whilst we were waiting.
		 */
		pthread_join(thread->xos->tid, &status);

		for (i=0; i < num_jobs; i++) {
			if (!jobs[i]) {
				myrefl_debug(NULL, "Job %d is FALSE", i);
			}
			//ck_assert(jobs[i] == TRUE);
		}
		myrefl_thread_kill_threads();
}
/*
 * Check that we can schedule jobs and that they get executed in the thread pool.
 */
START_TEST (test_myrefl_thread_exec1)
{
	test_myrefl_thread_exec_num(1);
}
END_TEST

START_TEST (test_myrefl_thread_exec4)
{
	test_myrefl_thread_exec_num(4);
}
END_TEST

START_TEST (test_myrefl_thread_exec5)
{
	test_myrefl_thread_exec_num(5);
}
END_TEST

START_TEST (test_myrefl_thread_exec10)
{
	test_myrefl_thread_exec_num(10);
}
END_TEST

START_TEST (test_myrefl_thread_exec1000)
{
	test_myrefl_thread_exec_num(1000);
}
END_TEST

/*
 * Register the above unit tests.
 */
Suite *
swdaig_thread_test_suite (void)
{
  Suite *s = suite_create ("myrefl_thread");

  /* Core test case */
  TCase *tc_core = tcase_create ("Threads");
  tcase_add_test(tc_core, test_myrefl_thread_init);
  tcase_add_test(tc_core, test_myrefl_thread_exec1);
  tcase_add_test(tc_core, test_myrefl_thread_exec4);
  tcase_add_test(tc_core, test_myrefl_thread_exec5);
  tcase_add_test(tc_core, test_myrefl_thread_exec10);
  tcase_add_test(tc_core, test_myrefl_thread_exec1000);
  tcase_set_timeout(tc_core, 120);
  suite_add_tcase (s, tc_core);

  return s;
}
int
main (void)
{
	int number_failed;

	// We want to see debugging messages on the console, not in syslog.
	myrefl_xos_running_in_terminal();
	myrefl_debug_enable();

	Suite *s = swdaig_thread_test_suite();
	SRunner *sr = srunner_create(s);
	// When using gdb set to nofork
	//srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all (sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

