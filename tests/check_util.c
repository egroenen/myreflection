/*
 * check_until.c - Unit Test Harness for swdiag_util.c
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
 * Test the contents of swdiag_xos.h using the "check" UT framework.
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
#include "../src/swdiag_thread.h"
#include "../src/swdiag_xos.h"
#include "../src/swdiag_util.h"
#include "../src/swdiag_trace.h"


extern swdiag_list_element_t *swdiag_ut_expose_free_list (void);

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
	swdiag_thread_t *thread = (swdiag_thread_t*)context;
    swdiag_xos_thread_release(thread->xos);
    swdiag_debug(NULL, "Timer Expired %p", thread);
}

static void thread_2s(swdiag_thread_t *thread)
{
	xos_timer_t *timer;
	xos_time_t time1;
	xos_time_t time2;

	swdiag_xos_time_set_now(&time1);

	timer = swdiag_xos_timer_create(thread_timer_expired, thread);
	swdiag_xos_timer_start(timer, 2, 0);
	swdiag_debug(NULL, "Timer Started %p", thread);
	// Waiting here for the timer to go off.
	swdiag_xos_thread_wait(thread->xos);
	swdiag_xos_time_set_now(&time2);

	swdiag_debug(NULL, "Timer Exited %p", thread);

	swdiag_debug(NULL, "Before %lu , After %lu", time1.sec, time2.sec)

	ck_assert(time2.sec == time1.sec + 2);

}

static int count_free_elements (swdiag_list_element_t *head)
{
	int num = 0;
	swdiag_list_element_t *element = head;
	while (element != NULL) {
		num++;
		element = element->next;
	}
	return num;
}

/*
 * Test the lists.
 */
START_TEST (test_swdiag_util_list)
{
	swdiag_list_t *list = NULL;

	list = swdiag_list_create();

	ck_assert(list != NULL);

	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(list->lock != NULL);

	swdiag_list_element_t *elements = swdiag_ut_expose_free_list();

	ck_assert(elements == NULL);

	swdiag_list_add(list, NULL);

	elements = swdiag_ut_expose_free_list();

	ck_assert(elements != NULL);
	ck_assert(count_free_elements(elements) == 49);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head->next == NULL);
	ck_assert(list->head == list->tail);
	ck_assert(list->num_elements == 1);

	/* Try and insert a duplicate element */
	swdiag_list_add(list, NULL);
	ck_assert(list->num_elements == 1);

	/* find it */
	ck_assert(swdiag_list_find(list, NULL));

	ck_assert(swdiag_list_peek(list) == NULL);

	/* remove it */
	ck_assert(swdiag_list_remove(list, NULL));

	elements = swdiag_ut_expose_free_list();

	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 50);

	static int data[1000];
	int i;

	data[0] = 0;
	data[1] = 1;
	data[2] = 2;

	swdiag_list_push(list, &data[0]);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head == list->tail);
	ck_assert(list->num_elements == 1);

	swdiag_list_element_t *head = list->head;

	swdiag_list_push(list, &data[1]);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head != list->tail);
	ck_assert(list->head == head);
	ck_assert(list->num_elements == 2);

	swdiag_list_push(list, &data[2]);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head != list->tail);
	ck_assert(list->head == head);
	ck_assert(list->num_elements == 3);

	int *peek = (int*)swdiag_list_peek(list);
	ck_assert(*peek == 0);

	int *pop = swdiag_list_pop(list);
	ck_assert(*pop == 0);
	ck_assert(list->num_elements == 2);

	pop = swdiag_list_pop(list);
	ck_assert(*pop == 1);
	ck_assert(list->num_elements == 1);

	pop = swdiag_list_pop(list);
	ck_assert(*pop == 2);
	ck_assert(list->num_elements == 0);


	/* The lists element wrappers are allocated in blocks of 50
	 * so we should test those boundaries.
	 */


	/**/
	for (i=0; i < 10; i++) {
		data[i] = i;
		swdiag_list_add(list, &data[i]);
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 40);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 10);
	for (i=0; i < 10; i++) {
		ck_assert(swdiag_list_remove(list, &data[i]));
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 50);
	/**/

	/**/
	for (i=0; i < 50; i++) {
		data[i] = i;
		swdiag_list_add(list, &data[i]);
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 0);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 50);
	for (i=0; i < 50; i++) {
		ck_assert(swdiag_list_remove(list, &data[i]));
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 50);
	/**/

	/**/
	for (i=0; i < 51; i++) {
		data[i] = i;
		swdiag_list_add(list, &data[i]);
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 49);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 51);
	for (i=0; i < 51; i++) {
		ck_assert(swdiag_list_remove(list, &data[i]));
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 100);
	/**/

	/**/
	for (i=0; i < 1000; i++) {
		data[i] = i;
		swdiag_list_add(list, &data[i]);
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 0);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 1000);
	for (i=0; i < 1000; i++) {
		ck_assert(swdiag_list_remove(list, &data[i]));
	}
	elements = swdiag_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 1000);
	/**/
}
END_TEST

static void thread_consume (swdiag_thread_t *thread)
{
	int i;

	swdiag_debug(NULL, "Started consume thread");

	swdiag_list_t *list = (swdiag_list_t*)thread->job;
	for (i = 0; i < 1000; i++) {
		//swdiag_debug(NULL, "Iteration %d", i);
		int *data = NULL;
		data = swdiag_list_pop(list);
		if (data != NULL) {
			//swdiag_debug(NULL, "Pushed %d", *data);
			swdiag_list_push(list, data);
		} else {
			// Run out of elements, let the other thread put some back
			//swdiag_debug(NULL, "No more elements, sleeping");
			swdiag_xos_sleep(10);
		}
		// Give up CPU
		swdiag_xos_sleep(1);

	}
	swdiag_debug(NULL, "Completed consume thread");
}

static void test_swdiag_list_locking_n(int num_elements)
{
	static int data[1000];
	int i;

	swdiag_list_t *list = NULL;

	list = swdiag_list_create();

	ck_assert(list != NULL);

	/* init data*/
	for (i=0; i < num_elements; i++) {
		data[i] = i;
		swdiag_list_add(list, &data[i]);
	}

	swdiag_thread_t *thread1 = (swdiag_thread_t *)malloc(sizeof(swdiag_thread_t));
	thread1->quit = FALSE;
	thread1->job = (void*)list;
	thread1->xos = swdiag_xos_thread_create("Consume Thread",
			thread_consume,
			thread1);
	ck_assert(thread1->xos != NULL);

	swdiag_thread_t *thread2 = (swdiag_thread_t *)malloc(sizeof(swdiag_thread_t));
	thread2->quit = FALSE;
	thread2->job = (void*)list;
	thread2->xos = swdiag_xos_thread_create("Consume Thread",
			thread_consume,
			thread2);
	ck_assert(thread2->xos != NULL);

	// Let the threads run, they should all exit after 2s
	// when we request that the pool be killed off.
	void *status1;
	void *status2;
	pthread_join(thread1->xos->tid, &status1);
	pthread_join(thread2->xos->tid, &status2);
}
/*
 * Test that the reentrancy of the list is OK, and that we
 * lock it at the right times.
 *
 * Allocate data, put in a list, then have two threads that push and pop
 * the data from the list. They will pop then push back on as fast as they can.
 *
 * There should be no duplicates.
 *
 * Each thread will do a number of iterations and then exit.
 *
 */
START_TEST (test_swdiag_util_list_locking)
{
	test_swdiag_list_locking_n(1);
	test_swdiag_list_locking_n(2);
	test_swdiag_list_locking_n(10);
	test_swdiag_list_locking_n(1000);
}
END_TEST

/*
 * Register the above unit tests.
 */
Suite *
swdaig_xos_test_suite (void)
{
  Suite *s = suite_create ("swdiag_util");

  /* Core test case */
  TCase *tc_core = tcase_create ("Util lists");
  tcase_add_test(tc_core, test_swdiag_util_list);
  //tcase_add_test(tc_core, test_swdiag_util_list_locking);
  tcase_set_timeout(tc_core, 10);
  suite_add_tcase (s, tc_core);

  return s;
}
int
main (void)
{
	int number_failed;

	// We want to see debugging messages on the console, not in syslog.
	swdiag_xos_running_in_terminal();
	swdiag_debug_enable();

	Suite *s = swdaig_xos_test_suite();
	SRunner *sr = srunner_create(s);
	// When using gdb set to nofork
	//srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all (sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

