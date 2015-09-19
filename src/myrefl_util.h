/* 
 * myrefl_util.h - SW Diagnostics Utility module header
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

#ifndef __MYREFL_UTIL_H__
#define __MYREFL_UTIL_H__

#include "myrefl_client.h"
#include "myrefl_types.h"
#include "myrefl_obj.h"

typedef struct myrefl_list_element_t_ {
    struct myrefl_list_element_t_ *next;
    void *data;
} myrefl_list_element_t;

/*
 * General list element
 */
struct myrefl_list_s {
    myrefl_list_element_t *head;
    myrefl_list_element_t *tail;
    uint num_elements;
    xos_critical_section_t *lock;
};

myrefl_list_t *myrefl_list_create(void);
void myrefl_list_free(myrefl_list_t *list);
void myrefl_list_add(myrefl_list_t *list, void *element);  // add to head
boolean myrefl_list_remove(myrefl_list_t *list, void *element);
void *myrefl_list_pop(myrefl_list_t *list);                // pop head
void myrefl_list_push(myrefl_list_t *list, void *element); // push to tail
void *myrefl_list_peek(myrefl_list_t *list);
void myrefl_list_insert(myrefl_list_t *list, myrefl_list_element_t *prev, 
                        void *element);
boolean myrefl_list_find(myrefl_list_t *list, const void *element);

/*
 * obj_t specific specialisation of the general list
 */
obj_t *myrefl_obj_list_find_by_name(myrefl_list_t *list, const char *name);

const char *myrefl_util_myrefl_result_str(myrefl_result_t result);

#endif
