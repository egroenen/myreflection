/* 
 * swdiag_trace.h - SW Diagnostics Trace header
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
 * Traceability (logging) for diagnostics
 */
#ifndef __SWDIAG_TRACE_H__
#define __SWDIAG_TRACE_H__

#include "swdiag_types.h"
#include "swdiag_xos.h"

typedef enum trace_type_ {
    TRACE_STRING,
    TRACE_ERROR,
    TRACE_DEBUG,
    TRACE_ADD,
    TRACE_DEL,
    TRACE_TEST_PASS,
    TRACE_TEST_FAIL,
} trace_type_t;

#define TRACE_MAX_STRING 120

struct trace_event_s {
    trace_type_t type;
    char string[TRACE_MAX_STRING];
    int value1;
};

/*
 * Trace using a string, avoid this form if you can because it is
 * wasteful of memory in the event tracing. Instead use trace
 * events that contain the raw info, and then format it on display.
 */
void swdiag_trace(const char *name, const char *fmt, ...) __attribute__ ((format ( __printf__, 2, 3)));
void swdiag_error(const char *fmt, ...) __attribute__ ((format ( __printf__, 1, 2)));
void swdiag_debug_guts(const char *name, const char *fmt, ...) __attribute__ ((format ( __printf__, 2, 3)));
void swdiag_trace_test_pass(const char *testname, int pass);
uint swdiag_error_count(void);
void swdiag_debug_add_filter(const char *name);
void swdiag_debug_remove_filter(const char *name);
void swdiag_debug_enable(void);
void swdiag_debug_disable(void);
boolean swdiag_debug_enabled(void);
const swdiag_list_t *swdiag_debug_filters_get(void);

/*
 * For debugging use only. Can be used verbosely, unlike trace.
 */
#define swdiag_debug(name, args...) if (swdiag_debug_enabled()) swdiag_debug_guts(name, args);

#endif /* __SWDIAG_TRACE_H__ */
