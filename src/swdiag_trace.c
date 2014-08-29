/* 
 * swdiag_trace.c - SW Diagnostics Traceability module
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

#include "swdiag_types.h"
#include "swdiag_xos.h"
#include "swdiag_trace.h"
#include "swdiag_util.h"
#include "swdiag_api.h"

static uint error_count = 0;

#define TRACE_BUF_SIZE 160
#define ERROR_BUF_SIZE 160

/*
 * List of objects being debugged so that we can filter on just those
 * objects.
 */
static swdiag_list_t *debug_list = NULL;

static boolean debug_enabled = FALSE;

/*
 * swdiag_trace()
 *
 * Format a string trace buffer for sending to the OS dependent tracing
 */
void swdiag_trace (const char *name, const char *fmt, ...)
{
    va_list args;
    char buffer[TRACE_BUF_SIZE];
    int count = 0;
    trace_event_t event;

    va_start(args, fmt);
    count += vsnprintf(buffer, TRACE_BUF_SIZE, fmt, args);
    va_end(args);
    
    event.type = TRACE_STRING;
    swdiag_xos_sstrncpy(event.string, buffer, TRACE_MAX_STRING);
    event.string[TRACE_MAX_STRING-1] = '\0';

    /*
     * Send it off to the OS specific tracing file for processing.
     */
    swdiag_xos_trace(&event);

    /*
     * All event tracing goes into debugging as well.
     */
    swdiag_debug(name, "%s", event.string);
}

/*
 * swdiag_error()
 *
 * Format a string trace buffer for sending to the OS dependent tracing.
 * The number of swdiag errors is incremented.
 */
void swdiag_error (const char *fmt, ...)
{
    va_list args;
    char buffer[ERROR_BUF_SIZE];
    int count = 0;
    trace_event_t event;

    error_count++;

    va_start(args, fmt);
    count += vsnprintf(buffer, ERROR_BUF_SIZE, fmt, args);
    va_end(args);

    event.type = TRACE_ERROR;
    swdiag_xos_sstrncpy(event.string, buffer, TRACE_MAX_STRING);
    event.string[TRACE_MAX_STRING-1] = '\0';

    /*
     * Send it off to the OS specific tracing file for processing.
     */
    swdiag_xos_trace(&event);
}

/*
 * swdiag_error()
 *
 * Format a string trace buffer for sending to the OS dependent tracing.
 */
void swdiag_debug_guts (const char *name, const char *fmt, ...)
{
    va_list args;
    char buffer[ERROR_BUF_SIZE];
    int count = 0;
    trace_event_t event;

    /*
     * Is "name" in our debug_list? We can get away with comparing 
     * pointers since the name is always a pointer to the instance->name
     * which doesn't change for the life of the object.
     *
     * Note that name may be NULL, in which case it shouldn't be filtered,
     * and also debug_list may be NULL in which case no filters have been
     * setup.
     *
     * At some point we may decide to not display debugs for non NULL when
     * a filter is in place - we'll try it and see.
     */
    if (name && debug_list && !swdiag_list_find(debug_list, (char*)name)) {
        return;
    }

    va_start(args, fmt);
    count += vsnprintf(buffer, ERROR_BUF_SIZE, fmt, args);
    va_end(args);

    event.type = TRACE_DEBUG;
    swdiag_xos_sstrncpy(event.string, buffer, TRACE_MAX_STRING);
    event.string[TRACE_MAX_STRING-1] = '\0';

    /*
     * Send it off to the OS specific tracing file for processing.
     */
    swdiag_xos_trace(&event);
}

/*
 * swdiag_error_count()
 *
 * Return the number of swdiag errors recorded since initialization
 */
uint swdiag_error_count (void)
{
    return (error_count);
}

/*
 * Add a debug filter for the object with this name, if no object found
 * then create it as a forward reference.
 */
void swdiag_debug_add_filter (const char *name)
{
    obj_t *obj;

    obj = swdiag_api_get_or_create(name, OBJ_TYPE_ANY);

    if (!debug_list) {
        debug_list = swdiag_list_create();
    }

    if (debug_list && obj) {
        swdiag_list_add(debug_list, obj->i.name);
    }
}

void swdiag_debug_remove_filter (const char *name)
{
    obj_t *obj;
    
    obj = swdiag_obj_get_by_name(name, OBJ_TYPE_ANY);
    
    if (obj && debug_list) {
        if (!swdiag_list_remove(debug_list, obj->i.name)) {
            swdiag_debug(NULL, "Debug filter for '%s' not found", name);
        }

        if (debug_list->num_elements == 0) {
            swdiag_list_free(debug_list);
            debug_list = NULL;
            swdiag_debug_disable();
        }
    }
}

const swdiag_list_t *swdiag_debug_filters_get (void)
{
    return(debug_list);
}

void swdiag_debug_enable (void)
{
    debug_enabled = TRUE;
}

void swdiag_debug_disable (void)
{
    swdiag_list_free(debug_list);
    debug_list = NULL;
    debug_enabled = FALSE;
}

boolean swdiag_debug_enabled (void)
{
    return(debug_enabled);
}
