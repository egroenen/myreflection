/* 
 * swdiag_sequence.c - SW Diagnostics Sequencer
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
 * This file contains the sequencer that runs the tests and rules, kicks
 * off root cause identification, and recovery actions.
 *
 * It also handles the health and propagation of that health through
 * the system.
 */

#ifndef __SWDIAG_SEQUENCE_H__
#define __SWDIAG_SEQUENCE_H__

#include "swdiag_types.h"

/*
 * We have up to 50 requests at a time in my testing with just the CF
 * tests - so try to keep at most 50 contexts.
 */
#define SEQUENCE_CONTEXT_LOW_WATER   50

void swdiag_seq_from_test(obj_instance_t *test_instance);

void swdiag_seq_from_test_notify(obj_instance_t *test_instance,
                                 swdiag_result_t result,
                                 long value);

void swdiag_seq_from_test_notify_rci(obj_instance_t *test_instance,
                                     swdiag_result_t result,
                                     long value);

void swdiag_seq_from_root_cause(obj_instance_t *rule_instance);

void swdiag_seq_comp_set_health(obj_comp_t *comp, uint health);

swdiag_result_t swdiag_seq_test_run(obj_instance_t *test_instance, 
                                    long *value);
void swdiag_seq_from_action_complete(obj_instance_t *action_instance,
                                     swdiag_result_t result);
void swdiag_seq_init(void);

#endif
