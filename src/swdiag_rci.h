/* 
 * swdiag_rci.h - SW Diagnostics Root Cause Identification header
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
 * Subsystem and functions that perform root cause analysis of failed
 * clients registered with diagnostics.
 */

#ifndef __SWDIAG_RCI_H__
#define __SWDIAG_RCI_H__

#include "swdiag_util.h"

typedef enum {
    RCI_MAP_PARENTS = 1,
    RCI_MAP_COMP_PARENTS,
    RCI_MAP_CHILDREN,
    RCI_MAP_COMP_CHILDREN,
    
} rci_map_direction_t;

typedef boolean (*rci_map_function_t) (obj_instance_t *rule, void *context);

typedef struct loop_domain_t_ {
    struct loop_domain_t_ *next;
    uint number;
    swdiag_list_t *reachable;
} loop_domain_t;

extern void swdiag_rci_run(obj_instance_t *rule_instance, 
                           swdiag_result_t result);
extern boolean swdiag_depend_found_comp(swdiag_list_t *dependencies, 
                                        obj_comp_t *comp);
extern void swdiag_rci_rule_deleted(obj_instance_t *rule_instance);
#endif
