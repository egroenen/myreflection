/* 
 * swdiag_util.c - SW Diagnostics Utility module header
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
 * Miscellaneous utility functions that are used throughout the
 * SW diagnostics core component, and may also be useful for optional
 * components - such as the SW diagnostics CLI component.
 */

#include "swdiag_xos.h"
#include "swdiag_obj.h"
#include "swdiag_util.h"
#include "swdiag_trace.h"

/*
 * To reduce the effect of the malloc memory overhead allocate memory
 * blocks at multiples of util_list_element_t.
 */
#define HEADER_MALLOC_MULTIPLIER 50

/*
 * List of free list elements
 */
static swdiag_list_element_t *free_elements = NULL;

/*******************************************************************
 * Local Functions
 *******************************************************************/

static swdiag_list_element_t *new_list_element (void)
{
    swdiag_list_element_t *element = NULL;
    char *memory_block;
    int i;
    static xos_critical_section_t *lock = NULL;

    if (!lock) {
        lock = swdiag_xos_critical_section_create();
    }

    swdiag_xos_critical_section_enter(lock);

    if (free_elements == NULL) {
        memory_block = calloc(HEADER_MALLOC_MULTIPLIER, 
                              sizeof(swdiag_list_element_t));
        
        if (memory_block) {
            for(i = 0; i < HEADER_MALLOC_MULTIPLIER; i++) {
                element = (swdiag_list_element_t*)(memory_block + 
                                       (i * sizeof(swdiag_list_element_t)));
                element->next = free_elements;
                free_elements = element;
            }
        }
    }

    if (free_elements) {
        element = free_elements;
        free_elements = free_elements->next;
        element->next = NULL;
        element->data = NULL;
    } 

    swdiag_xos_critical_section_exit(lock);
    
    return(element);
}

/*
 * free_list_element()
 *
 * Put the element back on the free list.
 */
static void free_list_element (swdiag_list_element_t *element)
{
    if (element) {
        element->next = free_elements;
        free_elements = element;
    }
}

/*******************************************************************
 * Exported Functions
 *******************************************************************/

/*
 * swdiag_test_expose_free_list()
 *
 * Function to be used by the unit test code to access the free list.
 */
swdiag_list_element_t *swdiag_ut_expose_free_list (void)
{
    return(free_elements);
}

/*
 * swdiag_list_create()
 *
 * Allocate a new empty list.
 */
swdiag_list_t *swdiag_list_create (void)
{
    swdiag_list_t *list;

    list = calloc(1, sizeof(swdiag_list_t));
    if (list) {
        list->head = NULL;
        list->tail = NULL;
        list->num_elements = 0;
        list->lock = swdiag_xos_critical_section_create();
        if (!list->lock) {
            free(list);
            list = NULL;
        }
    }
    return(list);
}

/*
 * swdiag_list_free()
 *
 * Free the list
 */
void swdiag_list_free (swdiag_list_t *list)
{
    swdiag_list_element_t *current;
    
    if (list) {
        swdiag_xos_critical_section_enter(list->lock);
        while(list->head) {
            current = list->head;
            list->head = current->next;
            free_list_element(current);
        }
        swdiag_xos_critical_section_exit(list->lock);
        swdiag_xos_critical_section_delete(list->lock);
        free(list);
    }
}

/*
 * swdiag_list_add()
 *
 * Add the new_element to the head of the list.
 */
void swdiag_list_add (swdiag_list_t *list, void *data)
{
    swdiag_list_insert(list, NULL, data);
}
                      
/*
 * swdiag_list_insert()
 *
 * Insert an element after the "prev" if "prev" is NULL then insert
 * at the head.
 */
void swdiag_list_insert (swdiag_list_t *list, 
                         swdiag_list_element_t *prev,
                         void *data)
{
    swdiag_list_element_t *element, *head, *next;

    if (!list) {
        swdiag_error("%s: bad parameters", __FUNCTION__);
        return;
    }

    /*
     * This check should be removed for performance reasons prior
     * to release.
     */
    if (swdiag_list_find(list, data)) {
        swdiag_error("%s: duplicate element", __FUNCTION__);
        return;
    }

    element = new_list_element();

    swdiag_xos_critical_section_enter(list->lock);

    head = list->head;

    if (element) {
        element->data = data;

        if (prev) {
            next = prev->next;
            prev->next = element;
        } else {
            next = head;
            head = element;
        }
        element->next = next;
    }
    
    list->head = head;

    if (!list->tail) {
        /*
         * Only one element, so the head is the tail.
         */
        list->tail = head;
    } else {
        /*
         * We may have a new tail if we inserted at the end
         */
        if (list->tail == prev) {
            list->tail = element;
        }
    }
    list->num_elements++;
    swdiag_xos_critical_section_exit(list->lock);
}

/*
 * swdiag_list_remove()
 *
 * Remove the element from the list.
 */
boolean swdiag_list_remove (swdiag_list_t *list, void *data)
{
    swdiag_list_element_t *current, *previous;
    boolean retval = FALSE;

    if (!list) {
        swdiag_error("%s: bad parameters", __FUNCTION__);
        return(retval);
    }

    swdiag_xos_critical_section_enter(list->lock);

    previous = NULL;
    current = list->head;

    while (current != NULL) {
        if (current->data == data) {
            /*
             * Got a match, remove this element from the list
             */
            if (current == list->head) {
                /*
                 * At the head of the list., Move the head to the
                 * next element.
                 */
                list->head = current->next;
            } else {
                /*
                 * Have a previous element, make it's next
                 * point our next.
                 */
                previous->next = current->next;
            }

            if (current == list->tail) {
                /*
                 * At tail of the list, so new tail is our previous
                 * element.
                 */
                list->tail = previous;
            }

            free_list_element(current);

            list->num_elements--;

            /*
             * Assume that there is only one match in the list and bail.
             */
            retval = TRUE;
            break;
        }
        previous = current;
        current = current->next;
    }
    swdiag_xos_critical_section_exit(list->lock);
    return(retval);
}

/*
 * swdiag_list_push()
 *
 * Push the element to the tail of the list
 */
void swdiag_list_push (swdiag_list_t *list, void *data)
{
    swdiag_list_element_t *element;

    if (!list) {
        swdiag_error("%s: bad parameters", __FUNCTION__);
        return;
    }

    element = new_list_element();

    if (element) {
        element->data = data;
        element->next = NULL;

        swdiag_xos_critical_section_enter(list->lock);

        if (!list->head) {
            list->head = element;
        }
        
        if (list->tail) {
            list->tail->next = element;
        }

        list->tail = element;
        
        list->num_elements++;
        swdiag_xos_critical_section_exit(list->lock);
    } else {
        swdiag_error("%s: could not allocate element", __FUNCTION__);
    }
}

/*
 * swdiag_list_pop()
 *
 * Pop off and return the head of the list.
 */
void *swdiag_list_pop (swdiag_list_t *list)
{
    swdiag_list_element_t *head;
    void *data;

    if (!list || !list->head) {
        return(NULL);
    }
    swdiag_xos_critical_section_enter(list->lock);
    head = list->head;

    list->head = head->next;

    if (list->tail == head) {
        /*
         * Popped the last element
         */
        list->tail = NULL;
    }

    data = head->data;
    free_list_element(head);

    list->num_elements--;
    swdiag_xos_critical_section_exit(list->lock);

    return(data);
}

/*
 * swdiag_list_peek()
 *
 * Return the head of the queue, but leave it on the queue.
 */
void *swdiag_list_peek (swdiag_list_t *list)
{
    void *data = NULL;

    if (list && list->head) {
        data = list->head->data;
    }
    return(data);
}

/*
 * swdiag_list_find()
 *
 * Is the "data" in the "list".
 */
boolean swdiag_list_find (swdiag_list_t *list, const void *data)
{
    swdiag_list_element_t *current = NULL;
    boolean retval = FALSE;

    if (list) {
        current = list->head;

        swdiag_xos_critical_section_enter(list->lock);
        while(current) {
            if (current->data == data) {
                retval = TRUE;
                break;
            }
            current = current->next;
        }
        swdiag_xos_critical_section_exit(list->lock);
    }
    return(retval);
}

/*
 * swdiag_obj_list_find_by_name()
 *
 * Return the object with the same name in the list, the "list" *must*
 * contain objects, if it doesn't then we'll crash, sorry.
 */
obj_t *swdiag_obj_list_find_by_name (swdiag_list_t *list, const char *name)
{
    swdiag_list_element_t *current;
    obj_t *obj = NULL;

    if (!list) {
        return(NULL);
    }

    swdiag_xos_critical_section_enter(list->lock);

    current = list->head;

    while(current) {
        if (current->data) {
            obj = current->data;
            if (swdiag_obj_validate(obj, OBJ_TYPE_ANY) && 
                obj->i.name && 
                strcmp(obj->i.name, name) == 0) {
                swdiag_xos_critical_section_exit(list->lock);
                return(obj);
            }
        }
        current = current->next;
    }
    swdiag_xos_critical_section_exit(list->lock);
    return(NULL); 
}

/*
 * swdiag_util_swdiag_result_str()
 *
 * Return textual representation of the result string
 */
const char *swdiag_util_swdiag_result_str (swdiag_result_t result)
{
    switch (result) {
    case SWDIAG_RESULT_PASS:
        return ("Pass");
        break;
        
    case SWDIAG_RESULT_FAIL:
        return ("Fail");
        break;
        
    case SWDIAG_RESULT_VALUE:
        return ("Value");
        break;
        
    case SWDIAG_RESULT_IN_PROGRESS:
        return ("InProgr");
        break;
        
    case SWDIAG_RESULT_ABORT:
        return ("Abort");
        break;
    case SWDIAG_RESULT_INVALID:
        return ("Invalid");
        break;
    case SWDIAG_RESULT_IGNORE:
        return ("Ignore");
        break;
    case SWDIAG_RESULT_LAST:
        return ("Last");
        break;
    }
    
    return("Unknown");
}
