/* 
 * swdiag_util.h - SW Diagnostics Utility module header
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
 */

#ifndef __SWDIAG_UTIL_H__
#define __SWDIAG_UTIL_H__

#include "swdiag_client.h"
#include "swdiag_types.h"
#include "swdiag_obj.h"

typedef struct swdiag_list_element_t_ {
    struct swdiag_list_element_t_ *next;
    void *data;
} swdiag_list_element_t;

/*
 * General list element
 */
struct swdiag_list_s {
    swdiag_list_element_t *head;
    swdiag_list_element_t *tail;
    uint num_elements;
    xos_critical_section_t *lock;
};

swdiag_list_t *swdiag_list_create(void);
void swdiag_list_free(swdiag_list_t *list);
void swdiag_list_add(swdiag_list_t *list, void *element);  // add to head
boolean swdiag_list_remove(swdiag_list_t *list, void *element);
void *swdiag_list_pop(swdiag_list_t *list);                // pop head
void swdiag_list_push(swdiag_list_t *list, void *element); // push to tail
void *swdiag_list_peek(swdiag_list_t *list);
void swdiag_list_insert(swdiag_list_t *list, swdiag_list_element_t *prev, 
                        void *element);
boolean swdiag_list_find(swdiag_list_t *list, const void *element);

/*
 * obj_t specific specialisation of the general list
 */
obj_t *swdiag_obj_list_find_by_name(swdiag_list_t *list, const char *name);

const char *swdiag_util_swdiag_result_str(swdiag_result_t result);

#endif
