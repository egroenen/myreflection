/*
 * swdiag_types.h - SW Diagnostics Types
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
 * Headers should be self-compiling, and those that need to reference
 * local structure types without requiring the full definitions should
 * include this module.
 *
 * Headers and Modules are still free to include those headers it needs
 * for complete type definitions.
 *
 */

#ifndef __SWDIAG_TYPES_H__
#define __SWDIAG_TYPES_H__

typedef struct trace_event_s trace_event_t;
typedef struct swdiag_list_s swdiag_list_t;
typedef struct sched_test_s sched_test_t;
typedef struct swdiag_thread_s swdiag_thread_t;

typedef struct obj_s obj_t;
typedef struct obj_instance_s obj_instance_t;
typedef struct obj_test_s obj_test_t;
typedef struct obj_test_instance_s obj_test_instance_t;
typedef struct obj_action_s obj_action_t;
typedef struct obj_rule_s obj_rule_t;
typedef struct obj_comp_s obj_comp_t;

/*
 * Get the standard types like uint after we have declared our own types
 */
#include "swdiag_xos.h"

#endif /* __SWDIAG_TYPES_H__ */
