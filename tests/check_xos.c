/*
 * check_xos.c - Unit Test Harness for myrefl_xos.c
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
#include "../src/myrefl_xos.h"
#include "../src/myrefl_thread.h"
#include "../src/myrefl_trace.h"

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
    myrefl_debug(NULL, "Timer Expired %p", thread);
}

static void thread_2s(myrefl_thread_t *thread)
{
	xos_timer_t *timer;
	xos_time_t time1;
	xos_time_t time2;

	myrefl_xos_time_set_now(&time1);

	timer = myrefl_xos_timer_create(thread_timer_expired, thread);
	myrefl_xos_timer_start(timer, 2, 0);
	myrefl_debug(NULL, "Timer Started %p", thread);
	// Waiting here for the timer to go off.
	myrefl_xos_thread_wait(thread->xos);
	myrefl_xos_time_set_now(&time2);

	myrefl_debug(NULL, "Timer Exited %p", thread);

	myrefl_debug(NULL, "Before %lu , After %lu", time1.sec, time2.sec)

	ck_assert(time2.sec == time1.sec + 2);

}
/*
 * Check thread creation, we create a thread that simply waits for 5sec and then
 * exits.
 */
START_TEST (test_myrefl_xos_thread_create)
{
	myrefl_thread_t *thread = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
	thread->quit = FALSE;
	thread->xos = myrefl_xos_thread_create("Test Kill Thread",
			thread_2s,
			thread);
	ck_assert(thread->xos != NULL);

	/* will now start the above thread and wait until threads exit */
	myrefl_debug(NULL, "Suspending now");
	pthread_exit(NULL);
	/* Nothing happens after this */
}
END_TEST

/*
 * Test the sleep, its in ms, unsigned so dont test negv
 */
START_TEST (test_myrefl_xos_sleep)
{
	xos_time_t time1;
	xos_time_t time2;
	xos_time_t diff;
	myrefl_xos_time_set_now(&time1);
	myrefl_xos_sleep(3000);
	myrefl_xos_time_set_now(&time2);
	myrefl_debug(NULL, "Sleep Before %lu , After %lu", time1.sec, time2.sec)
	ck_assert(time2.sec == time1.sec + 3);

	myrefl_xos_time_set_now(&time1);
	myrefl_xos_sleep(0);
	myrefl_xos_time_set_now(&time2);
	myrefl_debug(NULL, "Sleep Before %lu , After %lu", time1.sec, time2.sec)
	ck_assert(time2.sec == time1.sec);

	int i=0;
	for (i=0; i < 100; i++) {
		myrefl_xos_time_set_now(&time1);
		myrefl_xos_sleep(100);
		myrefl_xos_time_set_now(&time2);
		myrefl_xos_time_diff(&time1, &time2, &diff);
		myrefl_debug(NULL, "Sleep Before %lu , After %lu, Diff %lu", time1.nsec, time2.nsec, diff.nsec)
		unsigned long diff_msec = diff.nsec / 1E6;
		ck_assert(diff_msec >= 100 && diff_msec <= 200);
	}
}
END_TEST

static int cs_order[2];

static void cs_thread1(myrefl_thread_t *thread)
{
	xos_critical_section_t *cs = (void*)thread->job;
	myrefl_debug(NULL, "thread 1 about to enter");
	myrefl_xos_critical_section_enter(cs);
	myrefl_debug(NULL, "thread 1 entered");
	myrefl_xos_sleep(5000);
	if (cs_order[0] == 0) {
		cs_order[0] = 1;
	}
	myrefl_xos_critical_section_exit(cs);
	myrefl_debug(NULL, "thread 1 exited");
}

static void cs_thread2(myrefl_thread_t *thread)
{
	xos_critical_section_t *cs = (void*)thread->job;
	myrefl_debug(NULL, "thread 2 about to enter");
	myrefl_xos_critical_section_enter(cs);
	myrefl_debug(NULL, "thread 2 entered");
	myrefl_xos_sleep(2000);
	if (cs_order[0] == 0) {
		cs_order[0] = 2;
	}
	myrefl_xos_critical_section_exit(cs);
	myrefl_debug(NULL, "thread 2 exited");
}

/*
 * Test that the critical sections are working between threads. Have two threads one locks
 * the lock, and then sleeps for 5sec. The second sleeps 2s and then tries to get the lock
 * record the order that things are run. Make sure that they are in the correct order.
 */
START_TEST (test_myrefl_xos_critical_section)
{
	static xos_critical_section_t *cs = NULL;

	cs_order[0] = 0;

	cs = myrefl_xos_critical_section_create();

	myrefl_thread_t *thread1 = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
	thread1->quit = FALSE;
	thread1->job = (void*)cs;
	thread1->xos = myrefl_xos_thread_create("CS thread1",
			cs_thread1,
			thread1);

	myrefl_thread_t *thread2 = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
	thread2->quit = FALSE;
	thread2->job = (void*)cs;
	thread2->xos = myrefl_xos_thread_create("CS thread2",
			cs_thread2,
			thread2);

	void *status;

	pthread_join(thread1->xos->tid, &status); // these do NOT wait for the thread to finish!
	pthread_join(thread2->xos->tid, &status);

	pthread_exit(NULL);

	myrefl_debug(NULL, "Apparently both threads have finished, %d ran first", cs_order[0]);

	// Check the order that the threads were run, the first thread
	// with the 5sec delay should have been first, the second should be
	// blocked on the critical section.
	ck_assert(cs_order[0] == 1);


}
END_TEST

/*
 * Register the above unit tests.
 */
Suite *
swdaig_xos_test_suite (void)
{
  Suite *s = suite_create ("myrefl_xos");

  /* Core test case */
  TCase *tc_core = tcase_create ("XOS Main");
  tcase_add_test(tc_core, test_myrefl_xos_thread_create);
  tcase_add_test(tc_core, test_myrefl_xos_sleep);
  tcase_add_test(tc_core, test_myrefl_xos_critical_section);
  tcase_set_timeout(tc_core, 25);
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

	Suite *s = swdaig_xos_test_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all (sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

