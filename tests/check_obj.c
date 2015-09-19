/*
 * check_obj.c - Unit Test Harness for myrefl_obj.c
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
 * Test the contents of myrefl_obj.c using the "check" UT framework.
 * Use "make check" to run these tests.
 *
 * The tests depend on using hidden functions within myrefl_obj.c, and also
 * including private header files to peek inside the opaque structures.
 *
 * Use ck_assert_msg() for things being tested in that test, and ck_assert()
 * where it is not core  to the test at hand.
 *
 * April 2014, Edward Groenendaal
 */
#include <unistd.h>
#include <check.h>
#include "../src/myrefl_obj.h"
#include "../src/myrefl_util.h"
#include "../src/myrefl_thread.h"

extern myrefl_list_t *myrefl_obj_test_get_freeme();
extern struct myrefl_thread_s *myrefl_obj_test_get_garbage_collector();
extern void myrefl_obj_test_run_garbage_collector();
/*
 * Test the initialisation, inits the garbage collector. Don't
 * bother testing the failures, they are trivial.
 */
START_TEST (test_myrefl_obj_init)
{
	myrefl_obj_init();
	myrefl_list_t *freeme = myrefl_obj_test_get_freeme();
	ck_assert_msg(freeme != NULL,
			"freeme queue not created on init");
	ck_assert_int_eq(freeme->num_elements, 0);
	ck_assert_msg(myrefl_obj_test_get_garbage_collector() != NULL,
			"garbage collector structure not created");
	ck_assert_msg(myrefl_obj_test_get_garbage_collector()->xos != NULL,
			"garbage collector thread not created");
}
END_TEST

/*
 * Test that the garbage collector is running, and actually cleans up objects.
 *
 * We create an instance and then free it, it should go on the freeme queue, and
 * then be freed by the garbage collector when run.
 */
START_TEST (test_myrefl_garbage_collector_manual)
{
	myrefl_obj_init();
	obj_t *obj = myrefl_obj_get_or_create(strdup("test object"), OBJ_TYPE_NONE);
	ck_assert(obj != NULL);
	myrefl_list_t *freeme = myrefl_obj_test_get_freeme();
	ck_assert(freeme != NULL);
	ck_assert(freeme->num_elements == 0);
	ck_assert(((struct obj_instance_s)obj->i).state == OBJ_STATE_ALLOCATED);
	myrefl_obj_delete(obj);
	ck_assert_msg(freeme->num_elements == 1, "Not added to free queue");
	ck_assert(((struct obj_instance_s)obj->i).state == OBJ_STATE_DELETED);
	myrefl_obj_test_run_garbage_collector();
	ck_assert_msg(freeme->num_elements == 0, "Not removed from free queue");
}
END_TEST

/*
 * Access freed object
 */
START_TEST (test_myrefl_garbage_collector_invalid)
{
	myrefl_obj_init();
	obj_t *obj = myrefl_obj_get_or_create(strdup("test object"), OBJ_TYPE_NONE);
	ck_assert(obj != NULL);
	myrefl_list_t *freeme = myrefl_obj_test_get_freeme();
	ck_assert(freeme != NULL);
	ck_assert(freeme->num_elements == 0);
	ck_assert(((struct obj_instance_s)obj->i).state == OBJ_STATE_ALLOCATED);
	myrefl_obj_delete(obj);
	ck_assert_msg(freeme->num_elements == 1, "Not added to free queue");
	ck_assert(((struct obj_instance_s)obj->i).state == OBJ_STATE_DELETED);
	myrefl_obj_test_run_garbage_collector();
	ck_assert_msg(freeme->num_elements == 0, "Not removed from free queue");

	// Now access the deleted object which has been freed.

	ck_assert_msg(myrefl_obj_validate(obj, OBJ_TYPE_NONE) == FALSE,
				"Freed object passed validation");

	// It would be nice to force a sigsegv at this point. Can't do it though.
}
END_TEST

START_TEST (test_myrefl_garbage_collector_multiple)
{
	myrefl_obj_init();

	myrefl_list_t *freeme = myrefl_obj_test_get_freeme();
	ck_assert_msg(freeme->num_elements == 0, "Not removed from free queue");

	char buffer[20];
	int i;
	int size = 100;
	for (i=0; i<size; i++) {
		snprintf(buffer, sizeof(buffer), "Test %d", i);
		obj_t *obj = myrefl_obj_get_or_create(strdup(buffer), OBJ_TYPE_NONE);
		myrefl_obj_delete(obj);
	}

	ck_assert_msg(freeme->num_elements == size, "Not added to free queue");
	myrefl_obj_test_run_garbage_collector();
	ck_assert_msg(freeme->num_elements == (size-(size/3)), "Not removed from free queue, %d left", freeme->num_elements);
	// This will bring us down to the floor
	myrefl_obj_test_run_garbage_collector();
	ck_assert_msg(freeme->num_elements == ((size-(size/3)) - 30), "Not removed from free queue, %d left", freeme->num_elements);
	myrefl_obj_test_run_garbage_collector();
	ck_assert_msg(freeme->num_elements == ((size-(size/3)) - 60), "Not removed from free queue, %d left", freeme->num_elements);
	myrefl_obj_test_run_garbage_collector();
	ck_assert_msg(freeme->num_elements == 0, "Not removed from free queue, %d left", freeme->num_elements);

}
END_TEST

/*
 * Wake up the kill thread when the timer goes off
 */
static void kill_thread_timer_expired (void *context)
{
	myrefl_thread_t *thread = (myrefl_thread_t*)context;
    myrefl_xos_thread_release(thread->xos);
}

/* wait for 15sec then exit the thread, killing off the
 * garbage collector whilst we are at it
 */
static void kill_thread_15s(myrefl_thread_t *thread)
{
	xos_timer_t *timer;

	timer = myrefl_xos_timer_create(kill_thread_timer_expired, thread);
	myrefl_xos_timer_start(timer, 15, 0);

	// Waiting here for the timer to go off.
	myrefl_xos_thread_wait(thread->xos);

	myrefl_thread_t *gc = myrefl_obj_test_get_garbage_collector();
	ck_assert(gc != NULL);
	gc->quit = TRUE;

}

/*
 * Check that the garbage collector does run and free our deleted
 * object. The garbage collector runs every 12s, so we wait 15s
 * for it to do its thing. The guard timer on the test needs to wait
 * at least 24s to give the garbage collector thread time to exit as
 * well.
 */
START_TEST (test_myrefl_garbage_collector_auto)
{
	myrefl_obj_init();
	obj_t *obj = myrefl_obj_get_or_create(strdup("test object"), OBJ_TYPE_NONE);
	ck_assert(obj != NULL);
	myrefl_list_t *freeme = myrefl_obj_test_get_freeme();
	ck_assert(freeme != NULL);
	ck_assert(freeme->num_elements == 0);
	ck_assert(((struct obj_instance_s)obj->i).state == OBJ_STATE_ALLOCATED);
	myrefl_obj_delete(obj);
	ck_assert_msg(freeme->num_elements == 1, "Not added to free queue");
	ck_assert(((struct obj_instance_s)obj->i).state == OBJ_STATE_DELETED);

	myrefl_thread_t *kill_thread = (myrefl_thread_t *)malloc(sizeof(myrefl_thread_t));
	kill_thread->quit = FALSE;
	kill_thread->xos = myrefl_xos_thread_create("Test Kill Thread",
			kill_thread_15s,
			kill_thread);
	ck_assert(kill_thread->xos != NULL);

	// This is where the threads start, and we don't return from here, until
	// the threads exit.
	pthread_exit(NULL);

	ck_assert_msg(freeme->num_elements == 0, "Not removed from free queue");
}
END_TEST


/*
 * Register the above unit tests.
 */
Suite *
swdaig_obj_test_suite (void)
{
  Suite *s = suite_create ("myrefl_obj");

  /* Core test case */
  TCase *tc_core = tcase_create ("Init");
  tcase_add_test(tc_core, test_myrefl_obj_init);
  suite_add_tcase (s, tc_core);

  TCase *tc_gc = tcase_create ("GC Tests");
  tcase_add_test(tc_gc, test_myrefl_garbage_collector_manual);
  tcase_add_test(tc_gc, test_myrefl_garbage_collector_invalid);
  tcase_add_test(tc_gc, test_myrefl_garbage_collector_multiple);
  tcase_add_test(tc_gc, test_myrefl_garbage_collector_auto);
  tcase_set_timeout(tc_gc, 25);
  suite_add_tcase (s, tc_gc);

  return s;
}

/*
 * Run the above unit tests
 */
int
main (void)
{
	int number_failed;

	// We want to see debugging messages on the console, not in syslog.
	myrefl_xos_running_in_terminal();
	//myrefl_debug_enable();

	Suite *s = swdaig_obj_test_suite();
	SRunner *sr = srunner_create(s);
	srunner_run_all (sr, CK_VERBOSE);
	number_failed = srunner_ntests_failed(sr);
	srunner_free (sr);
	return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}

