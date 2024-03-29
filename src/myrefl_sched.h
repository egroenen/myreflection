/* 
 * myrefl_sched.h - SW Diagnostics Scheduler header
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
 */

#ifndef __MYREFL_SCHED_H__
#define __MYREFL_SCHED_H__

#include "myrefl_xos.h"

#define MYREFL_SCHEDULAR_TEST "SWDiags Schedular Test"
#define MYREFL_SCHEDULAR_RULE "SWDiags Schedular"
#define MYREFL_SCHEDULAR_RECOVER "SWDiags Schedular Recover"

typedef enum test_queue_e {
    TEST_QUEUE_FIRST,
    TEST_QUEUE_IMMEDIATE = TEST_QUEUE_FIRST, /* higest priority */
    TEST_QUEUE_FAST,
    TEST_QUEUE_NORMAL,
    TEST_QUEUE_SLOW,      /* lowest priority */
    TEST_QUEUE_USER,
    NBR_TEST_QUEUES,
    TEST_QUEUE_NONE,
} test_queue_t;

struct sched_test_s {
    obj_instance_t *instance;  
    test_queue_t queued; 
    xos_time_t last_time;
    xos_time_t next_time;
};

typedef struct sched_test_queue_s {
    test_queue_t type;
    const char *name;
    myrefl_list_t *queue;
} sched_test_queue_t;

void myrefl_sched_init(void);
void myrefl_sched_terminate(void);
void myrefl_sched_add_test(obj_instance_t *test_instance, boolean force);
void myrefl_sched_remove_test(obj_instance_t *test_instance);
void myrefl_sched_kill(void);
void myrefl_sched_rule_immediate(obj_instance_t *rule_instance);
void myrefl_sched_test_immediate(obj_instance_t *test_instance);

#endif
