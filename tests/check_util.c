/*
 * check_until.c - Unit Test Harness for myrefl_util.c
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
#include "../src/myrefl_xos.h"
#include "../src/myrefl_util.h"
#include "../src/myrefl_trace.h"


extern myrefl_list_element_t *myrefl_ut_expose_free_list (void);

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

static int count_free_elements (myrefl_list_element_t *head)
{
	int num = 0;
	myrefl_list_element_t *element = head;
	while (element != NULL) {
		num++;
		element = element->next;
	}
	return num;
}

/*
 * Test the lists.
 */
START_TEST (test_myrefl_util_list)
{
	myrefl_list_t *list = NULL;

	list = myrefl_list_create();

	ck_assert(list != NULL);

	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(list->lock != NULL);

	myrefl_list_element_t *elements = myrefl_ut_expose_free_list();

	ck_assert(elements == NULL);

	myrefl_list_add(list, NULL);

	elements = myrefl_ut_expose_free_list();

	ck_assert(elements != NULL);
	ck_assert(count_free_elements(elements) == 49);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head->next == NULL);
	ck_assert(list->head == list->tail);
	ck_assert(list->num_elements == 1);

	/* Try and insert a duplicate element */
	myrefl_list_add(list, NULL);
	ck_assert(list->num_elements == 1);

	/* find it */
	ck_assert(myrefl_list_find(list, NULL));

	ck_assert(myrefl_list_peek(list) == NULL);

	/* remove it */
	ck_assert(myrefl_list_remove(list, NULL));

	elements = myrefl_ut_expose_free_list();

	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 50);

	static int data[1000];
	int i;

	data[0] = 0;
	data[1] = 1;
	data[2] = 2;

	myrefl_list_push(list, &data[0]);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head == list->tail);
	ck_assert(list->num_elements == 1);

	myrefl_list_element_t *head = list->head;

	myrefl_list_push(list, &data[1]);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head != list->tail);
	ck_assert(list->head == head);
	ck_assert(list->num_elements == 2);

	myrefl_list_push(list, &data[2]);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->head != list->tail);
	ck_assert(list->head == head);
	ck_assert(list->num_elements == 3);

	int *peek = (int*)myrefl_list_peek(list);
	ck_assert(*peek == 0);

	int *pop = myrefl_list_pop(list);
	ck_assert(*pop == 0);
	ck_assert(list->num_elements == 2);

	pop = myrefl_list_pop(list);
	ck_assert(*pop == 1);
	ck_assert(list->num_elements == 1);

	pop = myrefl_list_pop(list);
	ck_assert(*pop == 2);
	ck_assert(list->num_elements == 0);


	/* The lists element wrappers are allocated in blocks of 50
	 * so we should test those boundaries.
	 */


	/**/
	for (i=0; i < 10; i++) {
		data[i] = i;
		myrefl_list_add(list, &data[i]);
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 40);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 10);
	for (i=0; i < 10; i++) {
		ck_assert(myrefl_list_remove(list, &data[i]));
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 50);
	/**/

	/**/
	for (i=0; i < 50; i++) {
		data[i] = i;
		myrefl_list_add(list, &data[i]);
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 0);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 50);
	for (i=0; i < 50; i++) {
		ck_assert(myrefl_list_remove(list, &data[i]));
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 50);
	/**/

	/**/
	for (i=0; i < 51; i++) {
		data[i] = i;
		myrefl_list_add(list, &data[i]);
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 49);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 51);
	for (i=0; i < 51; i++) {
		ck_assert(myrefl_list_remove(list, &data[i]));
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 100);
	/**/

	/**/
	for (i=0; i < 1000; i++) {
		data[i] = i;
		myrefl_list_add(list, &data[i]);
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(count_free_elements(elements) == 0);
	ck_assert(list->head != NULL);
	ck_assert(list->tail != NULL);
	ck_assert(list->num_elements == 1000);
	for (i=0; i < 1000; i++) {
		ck_assert(myrefl_list_remove(list, &data[i]));
	}
	elements = myrefl_ut_expose_free_list();
	ck_assert(list->head == NULL);
	ck_assert(list->tail == NULL);
	ck_assert(list->num_elements == 0);
	ck_assert(count_free_elements(elements) == 1000);
	/**/
}
END_TEST

static void thread_consume (myrefl_thread_t *thread)
{
	int i;

	myrefl_debug(NULL, "Started consume thread");

	myrefl_list_t *list = (myrefl_list_t*)thread->job;
	for (i = 0; i < 1000; i++) {
		//myrefl_debug(NULL, "Iteration %d", i);
		int *data = NULL;
		data = myrefl_list_pop(list);
		if (data != NULL) {
			//myrefl_debug(NULL, "Pushed %d", *data);
			myrefl_list_push(list, data);
		} else {
			// Run out of elements, let the other thread put some back
			//myrefl_debug(NULL, "No more elements, sleeping");
			myrefl_xos_sleep(10);
		}
		// Give up CPU
		myrefl_xos_sleep(1);

	}
	myrefl_debug(NULL, "Completed consume thread");
}

static void test_myrefl_list_locking_n(int num_elements)
{
	static int data[1000];
	int i;

	myrefl_list_t *list = NULL;

	list = myrefl_list_create();

	ck_assert(list != NULL);

	/* init data*/
	for (i=0; i < num_elements; i++) {
		data[i] = i;
		myrefl_list_add(list, &data[i]);
	}

	myrefl_thread_t *thread1 = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
	thread1->quit = FALSE;
	thread1->job = (void*)list;
	thread1->xos = myrefl_xos_thread_create("Consume Thread",
			thread_consume,
			thread1);
	ck_assert(thread1->xos != NULL);

	myrefl_thread_t *thread2 = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
	thread2->quit = FALSE;
	thread2->job = (void*)list;
	thread2->xos = myrefl_xos_thread_create("Consume Thread",
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
START_TEST (test_myrefl_util_list_locking)
{
	test_myrefl_list_locking_n(1);
	test_myrefl_list_locking_n(2);
	test_myrefl_list_locking_n(10);
	test_myrefl_list_locking_n(1000);
}
END_TEST

/*
 * Register the above unit tests.
 */
Suite *
swdaig_xos_test_suite (void)
{
  Suite *s = suite_create ("myrefl_util");

  /* Core test case */
  TCase *tc_core = tcase_create ("Util lists");
  tcase_add_test(tc_core, test_myrefl_util_list);
  //tcase_add_test(tc_core, test_myrefl_util_list_locking);
  tcase_set_timeout(tc_core, 10);
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
	// When using gdb set to nofork
	//srunner_set_fork_status(sr, CK_NOFORK);
	srunner_run_all (sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

