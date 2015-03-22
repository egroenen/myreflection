/* 
 * swdiag_obj.c - SW Diagnostics Object module 
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
 * Internal API to the Object DB. 
 *
 * Note that all object names are in the internal converted format unless
 * otherwise noted.
 */

#include "swdiag_xos.h"
#include "swdiag_trace.h"
#include "swdiag_util.h"
#include "swdiag_obj.h"
#include "swdiag_api.h"
#include "swdiag_rci.h"
#include "swdiag_thread.h"
#include "swdiag_cli_handle.h"

/*******************************************************************
 * Structures and Types
 *******************************************************************/

/*
 * The following defines the maximum pointer walk iterations
 * to avoid lock ups in case of bugs. There is no imposed client limit,
 * so we use a large number.
 */
#define MAX_SERIAL_RULES 24
#define MAX_COMP_NESTING 255
#define MAX_NEXT_COMPS   255

obj_comp_t *swdiag_obj_system_comp = NULL;

static unsigned int obj_idents[] = {
    0,            // OBJ_TYPE_ANY
    0x0B20B20B,   // OBJ_TYPE_NONE (used for obj_t )
    0xBEEFFEED,   // OBJ_TYPE_TEST
    0xFEEDBEEF,   // OBJ_TYPE_RULE
    0xAC710511,   // OBJ_TYPE_ACTION
    0xC0DEC04E,   // OBJ_TYPE_COMP
};

/*
 * Use a counter for the obj DB lock so that it is safe for internal
 * code to use the external API should they wish to and handle the
 * nested lock/unlock safely.
 */
static xos_critical_section_t *obj_db_lock = NULL;
static int obj_db_count = 0;

/*
 * We have a "freeme" queue which a garbage collector thread uses to
 * periodically clean up deleted objects.
 */
static swdiag_list_t *freeme = NULL;
static swdiag_thread_t *garbage_collector = NULL;
static xos_timer_t *start_garbage_collect;

/*
 * GARBAGE_PERIOD_SEC
 *
 * How many seconds between invocations of the garbage collector,
 * as well as the ondemand invocations.
 */
#define GARBAGE_PERIOD_SEC 12

/*
 * GARBAGE_QUEUE_RATE
 * GARBAGE_QUEUE_FLOOR
 *
 * To avoid using too much CPU the garbage collector will only
 * process GARBAGE_QUEUE_RATE percent of the queue at a time before
 * pausing for GARBAGE_QUEUE_SLEEP seconds. If the result of 
 * GARBAGE_QUEUE_RATE of the queue is less than GARBAGE_QUEUE_FLOOR, then
 * process GARBAGE_QUEUE_FLOOR.
 */
#define GARBAGE_QUEUE_RATE 300
#define GARBAGE_QUEUE_FLOOR 30
#define GARBAGE_QUEUE_SLEEP 5

/*******************************************************************
 * Local Functions
 *******************************************************************/

/*
 * Convert a subset of relative types to an object type
 */
static obj_type_t rel_to_type (obj_rel_t rel)
{
    obj_type_t type = OBJ_TYPE_NONE;

    switch (rel) {
      case OBJ_REL_NONE: type = OBJ_TYPE_NONE; break;
      case OBJ_REL_TEST: type = OBJ_TYPE_TEST; break;
      case OBJ_REL_RULE: type = OBJ_TYPE_RULE; break;
      case OBJ_REL_ACTION: type = OBJ_TYPE_ACTION; break;
      case OBJ_REL_COMP: type = OBJ_TYPE_COMP; break;
      default: /*errmsg();*/ break;
    }
    return (type);
}

/*
 * Convert a known object type to a relative type
 */
static obj_rel_t type_to_rel (obj_type_t type)
{
    obj_rel_t rel = OBJ_REL_TEST;

    switch (type) {
      case OBJ_TYPE_NONE: rel = OBJ_REL_NONE; break;
      case OBJ_TYPE_TEST: rel = OBJ_REL_TEST; break;
      case OBJ_TYPE_RULE: rel = OBJ_REL_RULE; break;
      case OBJ_TYPE_ACTION: rel = OBJ_REL_ACTION; break;
      case OBJ_TYPE_COMP: rel = OBJ_REL_COMP; break;
      default: /*errmsg();*/ break;
    }
    return (rel);
}

/*
 * Get the next component in the system after the one given
 */
static obj_comp_t *comp_get_next (obj_comp_t *comp)
{
    obj_comp_t *next = NULL;
    int i = 0;

    if (!comp) {
        /*
         * System Component
         */
        next = swdiag_obj_system_comp;
    } else if (comp->comps) {
        /*
         * Next child component in our component
         */
        next = comp->comps->t.comp;
    } else if (comp->obj->next_in_comp) {
        /*
         * Next peer component in our component
         */
        next = comp->obj->next_in_comp->t.comp;
    } else do {
        /*
         * Parents next component
         */
        if (i++ > MAX_COMP_NESTING) {
            swdiag_error("%s: Too much comp nesting", __FUNCTION__);
            return (NULL);
        }
        comp = comp->obj->parent_comp;
        if (!comp) {
            break; /* reached top of tree */
        }
        if (comp->obj->next_in_comp) {
            next = comp->obj->next_in_comp->t.comp;
            break;
        }
    } while (TRUE);

    return (next);
}

/*
 * comp_get_next_type()
 *
 * Return the first object of given type containing the given component.
 *
 * If the component does not contain objects of the given type, return the next
 * object of that type in the system (from subsequent component), or NULL.
 */
static obj_t *comp_get_next_type (obj_comp_t *comp, obj_type_t type)
{
    obj_t *obj = NULL;

    switch (type) {
      case OBJ_TYPE_NONE:
        while (comp && !(obj=comp->nones)) {
            comp = comp_get_next(comp);
        }
        break;

      case OBJ_TYPE_TEST:
        while (comp && !(obj=comp->tests)) {
            comp = comp_get_next(comp);
        }
        break;

      case OBJ_TYPE_RULE:
        while (comp && !(obj=comp->rules)) {
            comp = comp_get_next(comp);
        }
        break;
       
      case OBJ_TYPE_ACTION:
        while (comp && !(obj=comp->actions)) {
            comp = comp_get_next(comp);
        }
        break;

      default:
        break;
    }
    return (obj);
}

/*
 * comp_get_next_contained()
 *
 * Return the next component contained either directly or indirectly
 * in the tree of components given by top_comp, and after the one
 * given by last_comp.
 * If there are no more components in top_comp then NULL is returned.
 *
 * The next comp is defined as any child first, then peer, then parent/s peer.
 * For example this function will return 1,2,3,4,5,6 for the following tree:
 *
 *            1
 *          2   5
 *         3 4   6
 */
static obj_comp_t *comp_get_next_contained (
    obj_comp_t *top_comp, obj_comp_t *last_comp)
{
    obj_comp_t *comp, *next_comp = NULL;
    int i = 0;

    if (!top_comp) {
        top_comp = swdiag_obj_system_comp;
    }
    if (!last_comp) {
        last_comp = top_comp;
    }

    if (last_comp->comps) {
        /*
         * Next child component in given component
         */
        next_comp = last_comp->comps->t.comp;
    } else if (top_comp == last_comp) {
        /* break - no more components in the tree */
    } else if (last_comp->obj->next_in_comp) {
        /*
         * Next peer component of given component
         */
        next_comp = last_comp->obj->next_in_comp->t.comp;
    } else do {
        /*
         * Parents next component
         */
        if (i++ > MAX_COMP_NESTING) {
            swdiag_error("%s: Too much comp nesting", __FUNCTION__);
            return (NULL);
        }
        comp = last_comp->obj->parent_comp;
        if (comp == top_comp || !comp) {
            break;
        }
        next_comp = comp->obj->next_in_comp->t.comp;
    } while (!next_comp);

    return (next_comp);
}

/*
 * comp_get_next_type_contained()
 *
 * Return the next object of given type (including any type) contained
 * either directly or indirectly in the tree of components given by top_comp,
 * starting with the one given by last_comp. If last_comp does not directly
 * contain objects of the given type, return the next object of the given
 * type from the tree of contained components.
 * If no objects of that type are found, NULL is returned.
 */
static obj_t *comp_get_next_type_contained (
    obj_comp_t *top_comp, obj_comp_t *last_comp, obj_type_t type)
{
    obj_t *obj = NULL;
    obj_comp_t *next_comp;
    int i = 0;

    if (!top_comp) {
        top_comp = swdiag_obj_system_comp;
    }
    if (!last_comp) {
        last_comp = top_comp;
    }
    next_comp = last_comp;

    switch (type) {
      case OBJ_TYPE_NONE:
        while (next_comp && !(obj=next_comp->nones)) {
            next_comp = comp_get_next_contained(top_comp, next_comp);
        }
        break;

      case OBJ_TYPE_TEST:
        while (next_comp && !(obj=next_comp->tests)) {
            next_comp = comp_get_next_contained(top_comp, next_comp);
        }
        break;

      case OBJ_TYPE_RULE:
        while (next_comp && !(obj=next_comp->rules)) {
            next_comp = comp_get_next_contained(top_comp, next_comp);
        }
        break;
       
      case OBJ_TYPE_ACTION:
        while (next_comp && !(obj=next_comp->actions)) {
            next_comp = comp_get_next_contained(top_comp, next_comp);
        }
        break;
       
      case OBJ_TYPE_COMP:
        while (next_comp && !(obj=next_comp->comps)) {
            next_comp = comp_get_next_contained(top_comp, next_comp);
        }
        break;

      case OBJ_TYPE_ANY:
        while (next_comp && !obj) {
            if (i++ > MAX_NEXT_COMPS) {
                swdiag_error("%s: Too many empty next comps", __FUNCTION__);
                return (NULL);
            }
            if (next_comp->nones) {
                obj = next_comp->nones;
            } else if (next_comp->tests) {
                obj = next_comp->tests;
            } else if (next_comp->rules) {
                obj = next_comp->rules;
            } else if (next_comp->actions) {
                obj = next_comp->actions;
            } else if (next_comp->comps) {
                obj = next_comp->comps;
            } else {
                next_comp = comp_get_next_contained(top_comp, next_comp);
            }
        }
        break;

      default:
        break;
    }
    return (obj);
}

/*
 * Find the first object of type contained within the complete system
 * The type must be Test, Rule, Action or Comp.
 */
static obj_t *sys_get_first_type (obj_type_t type)
{
    if (type == OBJ_TYPE_COMP) {
        return (swdiag_obj_system_comp->obj);
    }
    return (comp_get_next_type(swdiag_obj_system_comp, type));
}

/*
 * Get the next object of the given type contained within the complete system.
 * The type must be Test, Rule, Action or Comp.
 */
static obj_t *sys_get_next_type (obj_t *obj, obj_type_t type)
{
    obj_comp_t *comp;

    if (obj->type == OBJ_TYPE_COMP) {
        comp = comp_get_next(obj->t.comp);
        if (comp) {
            return (comp->obj);
        } else {
            return (NULL);
        }
    }
    if (obj->next_in_comp) {
        return (obj->next_in_comp);
    }
    
    /*
     * Find the next sibling component, and look in there for objects
     * of this type
     */
    comp = comp_get_next(obj->parent_comp);
    return (comp_get_next_type(comp, type));
}  

/*
 * Return the object with given name and relative type.
 * Returns NULL if a object does not exist with that name and type.
 */
static obj_t *get_by_name_rel (const char *name, obj_rel_t rel)
{
    obj_t *obj;

    if (!swdiag_obj_system_comp) {
        swdiag_error("Call to get_by_name_rel() before System component created");
        return (NULL);
    }

    obj = swdiag_obj_get_first_rel(NULL, rel);

    while (obj) {
        if (!strcmp(name, obj->i.name)) {
            return (obj); /* found it */
        }
        obj = swdiag_obj_get_next_rel(obj, OBJ_REL_NEXT_IN_SYS);
    }

    return (obj);
}

/*
 * From a generic object (of OBJ_TYPE_NONE), allocate the specific
 * object of given type.
 * Returns TRUE upon success, FALSE otherwise.
 */
static boolean allocate_object_type (obj_t *obj, obj_type_t type)
{
    boolean success = FALSE;

    if (obj->type != OBJ_TYPE_NONE) {
        swdiag_error("Failed to create object type as type not NONE");
        return (FALSE);
    }

    switch (type) {
    case OBJ_TYPE_TEST: 
        obj->t.test = calloc(1, sizeof(obj_test_t));
        if (obj->t.test) {
            obj->t.test->ident = obj_idents[OBJ_TYPE_TEST];
            obj->t.test->obj = obj;
            obj->type = OBJ_TYPE_TEST;
            success = TRUE;
        }
        break;
    case OBJ_TYPE_RULE: 
        obj->t.rule = calloc(1, sizeof(obj_rule_t));
        if (obj->t.rule) {
            obj->t.rule->ident = obj_idents[OBJ_TYPE_RULE];
            obj->t.rule->obj = obj;
            obj->type = OBJ_TYPE_RULE;
            obj->t.rule->inputs = swdiag_list_create();
            obj->t.rule->action_list = swdiag_list_create();
            success = TRUE;
        }
        break;
    case OBJ_TYPE_ACTION: 
        obj->t.action = calloc(1, sizeof(obj_action_t));
        if (obj->t.action) {
            obj->t.action->ident = obj_idents[OBJ_TYPE_ACTION];
            obj->t.action->obj = obj;
            obj->type = OBJ_TYPE_ACTION;
            obj->t.action->rule_list = swdiag_list_create();
            success = TRUE;
        }
        break;
    case OBJ_TYPE_COMP:
        obj->t.comp = calloc(1, sizeof(obj_comp_t));
        if (obj->t.comp) {
            obj->t.comp->ident = obj_idents[OBJ_TYPE_COMP];
            obj->t.comp->obj = obj;
            obj->type = OBJ_TYPE_COMP;
            obj->t.comp->top_depend = swdiag_list_create();
            obj->t.comp->bottom_depend = swdiag_list_create();
            obj->t.comp->interested_test_objs = swdiag_list_create();
            success = TRUE;
        }
        break;
    case OBJ_TYPE_ANY:
    case OBJ_TYPE_NONE:
      success = TRUE; /* already NONE so we can ignore */
      break;
    default:
        break;
    }

    if (success) {
        obj->i.state = OBJ_STATE_ALLOCATED;
    } else {
        obj->i.state = OBJ_STATE_INVALID;
        obj->type = OBJ_TYPE_NONE;
     }
    return (success);
}

/*******************************************************************
 * Exported Generic Object Functions
 *******************************************************************/

/*
 * swdiag_comp_get_first_contained()
 *
 * Given a component (or NULL for system), find and return the first object
 * of the given type (including OBJ_TYPE_ANY) contained in the component tree.
 */
obj_t *swdiag_comp_get_first_contained (obj_comp_t *top_comp, obj_type_t type)
{
    return (comp_get_next_type_contained(top_comp, top_comp, type));
}

/*
 * comp_get_next_any()
 */
static obj_t *comp_get_next_any (obj_t *obj, obj_comp_t *comp)
{
    switch (obj->type) {
    case OBJ_TYPE_COMP:
        if (comp->nones) {
            return (comp->nones);
        } /* FALLTHRU */
        /* no break */
    case OBJ_TYPE_NONE:
        if (comp->tests) {
            return (comp->tests);
        } /* FALLTHRU */
        /* no break */
    case OBJ_TYPE_TEST:
        if (comp->rules) {
            return (comp->rules);
        } /* FALLTHRU */
        /* no break */
    case OBJ_TYPE_RULE:
        if (comp->actions) {
            return (comp->actions);
        } /* FALLTHRU */
        /* no break */
    case OBJ_TYPE_ACTION:
        /* FALLTHRU - no more objects in this comp */
    default:
        break;
    }
    return(NULL);
}

/*
 * swdiag_comp_get_next_contained()
 *
 * Given a component (or NULL for system), and a previously found object,
 * find and return the next object of given type (including OBJ_TYPE_ANY)
 * contained in the component tree. This function can be iterated to find
 * all objects of certain type (or of any type) contained in a component tree.
 */
obj_t *swdiag_comp_get_next_contained (
    obj_comp_t *top_comp, obj_t *obj, obj_type_t type)
{
    obj_comp_t *comp;

    /*
     * Sanity check params
     */
    if (!obj || !obj->parent_comp) {
        return (NULL);
    }

    if (type == OBJ_TYPE_COMP) {
        comp = obj->t.comp;
    } else {
        if (obj->type != OBJ_TYPE_COMP && obj->next_in_comp) {
            return (obj->next_in_comp);
        }
        if (obj->type == OBJ_TYPE_COMP && type == OBJ_TYPE_ANY) {
            comp = obj->t.comp;
        } else {
            comp = obj->parent_comp;
        }
    }

    /*
     * We only reach the following switch statement for OBJ_TYPE_ANY
     * when we have finished returning the type given by obj->type,
     * and need to move to the next type in the same component.
     *
     * So if the last object returned was a COMP, we then return the
     * NONES in that comp, if there are any. If and when the nones are
     * exhausted, we move on to the tests etc...
     */
    if (type == OBJ_TYPE_ANY) {
        if ((obj = comp_get_next_any(obj, comp)) != NULL) {
            return(obj);
        }
    }

    /*
     * Were done with this component, lets move to the next component
     * in the tree.
     */
    comp = comp_get_next_contained(top_comp, comp);
    if (!comp) {
        return (NULL);
    } else if (type == OBJ_TYPE_COMP || type == OBJ_TYPE_ANY) {
        return (comp->obj);
    }

    /*
     * Otherwise if present, find the object in the next component
     */
    return (comp_get_next_type_contained(
                top_comp, /* top component - don't look outside this */
                comp, /* starting component */
                type));
}

/*
 * Return an object relative (given by rel) to the object given.
 * Specifically can be used to:
 *  - Return the test that feeds directly or indirectly (via other rules)
 *    into the given rule.
 *    i.e. obj_test = swdiag_obj_get_rel(obj_rule, OBJ_REL_RULE);
 *
 *  - Futher expansion of this function is as required.
 */
obj_t *swdiag_obj_get_rel (obj_t *obj, obj_rel_t rel)
{
    int i = 0;

    if (!obj) {
        return (NULL);
    }

    if (obj->type == OBJ_TYPE_RULE) {
        if (rel == OBJ_REL_TEST) {
            /*
             * Walk backwards thru the input rules looking for the first test.
             * Use a loop with a sanity maximum to avoid any chance of lock-up.
             *
             * Note that this is broken really, since it is supposed to
             * return *all* the tests for a given rule, which may be more than
             * one now that we support multiple inputs.
             */
            do {
                if (i++ > MAX_SERIAL_RULES) {
                    swdiag_error("%s: too many serial rules", __FUNCTION__);
                    return (NULL);
                }
                obj = (obj_t*)(obj->t.rule->inputs->head ? obj->t.rule->inputs->head->data : NULL);
            } while (obj && obj->type != OBJ_TYPE_TEST);
            return (obj);
        }
    }

    /* types referenced from the obj */
    switch (rel) {
      case OBJ_REL_PARENT_COMP:
        /* all but the System component will have a super_comp */
        if (obj->parent_comp) {
            return (obj->parent_comp->obj);
        }
        /* else returns NULL */
        break;
      default:
        break;
    }
    return (NULL);
}

/*
 * Return the first object of a given type relative to the given object
 */
obj_t *swdiag_obj_get_first_rel (obj_t *obj, obj_rel_t rel)
{
    if (!obj) {
        return (sys_get_first_type(rel_to_type(rel)));
    }
    
    switch (obj->type) {
    case OBJ_TYPE_COMP: {
        obj_comp_t *comp = obj->t.comp;
        switch (rel) {
        case OBJ_REL_NONE:
            return (comp->nones);
        case OBJ_REL_TEST:
            return (comp->tests);
        case OBJ_REL_RULE:
            return (comp->rules);
        case OBJ_REL_ACTION:
            return (comp->actions);
        case OBJ_REL_COMP:
            return (comp->comps);
        default:
            break;
        }
        break;
    }
    case OBJ_TYPE_TEST: {
        obj_test_t *test = obj->t.test;
        switch (rel) {
        case OBJ_REL_RULE:
            return (test->rule ? test->rule->obj : NULL);
        default:
            break;
        }
        break;
    }
        
    default:
        break;      
    }
    return (NULL);
}

/*
 * swdiag_obj_get_next_rel()
 *
 * Given an object, find and return the next one of its type in the tree.
 */
obj_t *swdiag_obj_get_next_rel (obj_t *obj, obj_rel_t rel)
{
    obj_rule_t *rule;
    
    if (!obj) {
        return (swdiag_obj_get_first_rel(NULL, rel));
    }
    
    /*
     * Getting the objects of this type in the system
     */
    if (obj->type == rel_to_type(rel) || rel == OBJ_REL_NEXT_IN_SYS) {
        return (sys_get_next_type(obj, obj->type));
    }
    
    /*
     * Getting the next object of the same type in this component
     */
    if (rel == OBJ_REL_NEXT_IN_COMP) {
        return (obj->next_in_comp);
    }
    
    /*
     * Getting the rules associated with the same root test
     */
    if (obj->type == OBJ_TYPE_RULE && rel == OBJ_REL_NEXT_IN_TEST) {
        rule = obj->t.rule;
        if (rule->next_in_input) {
            return (rule->next_in_input->obj);
        } else {
            return (NULL);
        }
    }
    
    /* other combinations not yet handled */
    //printf("\nget_obj_get_next(not handled %s)", obj->i.name);
    
    return (NULL);
}

/*
 * Return the object with given name and type (including OBJ_TYPE_NONE).
 * If the type is unknown then the value OBJ_TYPE_ANY should be used
 * and this function will scan the object list of nones, components, tests,
 * rules and actions (in that order) looking for the first name that matches.
 */
obj_t *swdiag_obj_get_by_name (const char *name, obj_type_t type)
{
    obj_t *obj = NULL;

    if (!name) {
        return (NULL);
    }

    /* return the system component if specified */
    if (type == OBJ_TYPE_COMP && 
        !strcmp(name, swdiag_obj_system_comp->obj->i.name)) {
        return (swdiag_obj_system_comp->obj);
    }

    //swdiag_trace("'%s' name '%s' type '%d' typetorel '%d'", 
    //    __FUNCTION__, name, type, type_to_rel(type));

    if (type == OBJ_TYPE_ANY) {
        if ((obj = get_by_name_rel(name, OBJ_REL_NONE))) {
            return (obj);
        }
        if ((obj = get_by_name_rel(name, OBJ_REL_COMP))) {
            return (obj);
        }
        if ((obj = get_by_name_rel(name, OBJ_REL_TEST))) {
            return (obj);
        }
        if ((obj = get_by_name_rel(name, OBJ_REL_RULE))) {
            return (obj);
        }
        obj = get_by_name_rel(name, OBJ_REL_ACTION);
        return (obj);
    }

    return (get_by_name_rel(name, type_to_rel(type)));
}

/*
 * Return the object with the given name and type.
 * If the type is unknown the the value OBJ_TYPE_ANY should be used.
 * The name has not yet been copied and converted.
 *
 * Note that the name is unconverted, and so must go back to the API to
 * convert it before looking it up.
 */
obj_t *swdiag_obj_get_by_name_unconverted (const char *name, obj_type_t type)
{
    char *name_copy;
    obj_t *obj = NULL;

    name_copy = swdiag_api_convert_name(name);
    if (name_copy) {
        obj = swdiag_obj_get_by_name(name_copy, type);
        free(name_copy);
    }

    return (obj);
}

/*
 * Unlink an object from it's component, including from the system component.
 */
void swdiag_obj_unlink_from_comp (obj_t *obj)
{
    obj_t *tmp = NULL;
    obj_t **list = NULL;
    obj_comp_t *comp = obj->parent_comp;

    if (!comp) {
        return; /* no component to remove from */
    }

    switch (obj->type) {
    case OBJ_TYPE_NONE: 
        list = &comp->nones;
        break;
    case OBJ_TYPE_TEST: 
        list = &comp->tests; 
        break;
    case OBJ_TYPE_RULE: 
        list = &comp->rules; 
        break;
    case OBJ_TYPE_ACTION: 
        list = &comp->actions; 
        break;
    case OBJ_TYPE_COMP: 
        list = &comp->comps; 
        break;
      default:
        //ttyprintf(CONTTY, "\nunlink_from_comp(%s) BAD obj->type=%d",
        //    obj->name, obj->type);
        return;
    }

    /* remove from anywhere in the comp list */
    if (*list == obj) {
        *list = obj->next_in_comp;
    } else for (tmp=*list; tmp; tmp=tmp->next_in_comp) {
        if (tmp->next_in_comp == obj) {
            tmp->next_in_comp = obj->next_in_comp;
            break;
        }
    }
    obj->next_in_comp = NULL;
    obj->parent_comp = NULL;
    
    swdiag_list_remove(comp->top_depend, obj);
    swdiag_list_remove(comp->bottom_depend, obj);

    //ttyprintf(CONTTY, "\nunlinked %s from comp", obj->name);
}


/*
 * swdiag_obj_comp_link_obj()
 *
 * The provided object "obj" should be made a member of component "comp",
 * by linking it in as the head of the approprate list.
 *
 * We use the "next_in_comp" pointer to chain objects of the same type
 * within a componen This works well because an object may only be a 
 * member of one component at a time.
 */
void swdiag_obj_comp_link_obj (obj_comp_t *comp, obj_t *obj)
{
    obj_t *tmp = NULL;
    obj_t **list = NULL;

    switch (obj->type) {
    case OBJ_TYPE_NONE:
        list = &comp->nones;
        break;
    case OBJ_TYPE_TEST: 
        list = &comp->tests; 
        break;
    case OBJ_TYPE_RULE: 
        list = &comp->rules; 
        break;
    case OBJ_TYPE_ACTION: 
        list = &comp->actions; 
        break;
    case OBJ_TYPE_COMP: 
        list = &comp->comps; 
        break;
    default:
        //ttyprintf(CONTTY, "\ncomp_link_obj(%s) BAD obj->type=%d", obj->name, obj->type);
        return;
    }

    /* add to the front of the comp list */
    tmp = *list;
    *list = obj;
    obj->next_in_comp = tmp;
    obj->parent_comp = comp;

    swdiag_debug(obj->i.name, "%s linked to comp %s", obj->i.name, obj->parent_comp->obj->i.name);

    return;
}

/*
 * Find the object with the given name and type (or of any type for OBJ_TYPE_ANY).
 * If not found then the object is created and is set to enabled state.
 * The type specific portion (if applicable) is also created, but is unitialized.
 *  OBJ_TYPE_NONE will be grown into an object of type
 * A pointer to the object is returned.
 *
 * When a real type (TEST,RULE,ACTION,COMP) is given and where an object exists
 * with the same name and is of OBJ_TYPE_NONE, then that object will grow into
 * the given real object, with the type specific portion is created. In this way
 * a partially created object can be referenced before it being fully created.
 *
 * It is the responsibility of the caller to:
 * 1. Complete the initialization of the specialized portion of a created object.
 * 2. Link the object into the diagnostics database using swdiag_obj_link().
 * 3. Delete the object in case of errors.
 */
obj_t *swdiag_obj_get_or_create (char *obj_name, obj_type_t type)
{
    obj_t *obj = NULL;
    obj_comp_t *comp;
    
    /*
     * If there is no swdiag_obj_system_comp yet then create it first before
     * creating this object.
     */
    if (!swdiag_obj_system_comp && strcmp(obj_name, SWDIAG_SYSTEM_COMP) != 0) {
        obj = swdiag_obj_get_or_create(SWDIAG_SYSTEM_COMP, OBJ_TYPE_COMP);
        if (!obj) {
            swdiag_error("Could not create system component");
            return (NULL);
        }
        obj->t.comp->health = 1000;
        obj->t.comp->confidence = 1000;
        obj->i.state = OBJ_STATE_ENABLED;
        obj->i.default_state = OBJ_STATE_ENABLED;
        obj->i.cli_state = OBJ_STATE_INITIALIZED;  // CLI not set.
        obj = NULL;
    }

    if (swdiag_obj_system_comp) {
        /*
         * It is at this point where we ensure there is only one name space
         * for all object types. The underlying functions do support separate
         * name spaces, however we are placing the restriction on behalf
         * of the greater swdiag component to avoid API issues.
         * So we look for an existing name of ANY type.
         */
        obj = swdiag_obj_get_by_name(obj_name, OBJ_TYPE_ANY);
    }

    if (obj) {
        /*
         * We have an existing entry, make sure that it is of the correct 
         * type, if not then convert it if possible.
         */
        if (type == OBJ_TYPE_ANY) {
            /* found it and we don't care what type */
        } else if (type == obj->type) {
            /* found it with matching type */
        } else if (type != OBJ_TYPE_NONE && obj->type == OBJ_TYPE_NONE) {
            /*
             * Unlink the object from the component's list of NONE types,
             * migrate to a real type, and then link the object back into
             * the same component on the correct type list.
             */
            comp = obj->parent_comp;
            swdiag_obj_unlink_from_comp(obj);
            if (!allocate_object_type(obj, type)) {
                swdiag_error("Can't create object type from NONE type");
                /* fall thru and relink the failed NONE type anyway */
            }
            swdiag_obj_comp_link_obj(comp, obj);
        } else {
            swdiag_error("Can't change types of existing object %s from %s to %s",
                obj_name, swdiag_obj_type_str(obj->type), swdiag_obj_type_str(type));
            return (NULL);
        }

        /*
         * There is no way we should be finding deleted objects still
         * in the tree, don't return them if we do.
         */
        if (obj->i.state == OBJ_STATE_DELETED) {
            swdiag_error("Failed to create duplicate of deleted object '%s' type '%s'",
                         swdiag_obj_type_str(obj->type), obj_name);
            obj = NULL;
        } 

        return (obj);
    }

    /*
     * This object is unknown. Lets allocate and zero the base object.
     */
    obj = calloc(1, sizeof(obj_t));
    if (!obj) {
        swdiag_error("Alloc of %s object '%s'", swdiag_obj_type_str(type), obj_name);
        return (NULL);
    }

    /*
     * Now set the base object defaults.
     */
    obj->ident = obj_idents[OBJ_TYPE_NONE];
    obj->type = OBJ_TYPE_NONE; /* Overridden by allocate_object_type() */
    obj->description = NULL;

    /*
     * Only the API module can move the state from ALLOCATED to INITIALIZED
     * as only the API module has knowledge of how to initialize the fields
     * of the specific type.
     */
    obj->i.obj = obj;
    obj->i.state = OBJ_STATE_ALLOCATED;
    obj->i.name = obj_name;
    obj->i.context = NULL;
    obj->i.next = NULL;
    obj->i.prev = NULL;
    obj->i.sched_test.queued = TEST_QUEUE_NONE;
    obj->i.sched_test.instance = &obj->i;

    obj->parent_depend = swdiag_list_create();
    obj->child_depend = swdiag_list_create();
    obj->ref_rule = NULL;
    obj->domain = 0;

    /*
     * Now allocate the type specific portion of the object.
     * No specific portion is created for unknown types.
     */
    if (!allocate_object_type(obj, type)) {
        swdiag_error("Could not create  %s object '%s'", swdiag_obj_type_str(type), obj_name);
        /* object still stored, but has INVALID state */
        free(obj);
        return (NULL);
    }

    /*
     * The object is first linked into the System component to keep track
     * of it.
     */
    if (swdiag_obj_system_comp) { 
        swdiag_debug(NULL, "%s: Added '%s' to '%s'", 
                     __FUNCTION__, obj->i.name, 
                     swdiag_obj_system_comp->obj->i.name);
        swdiag_obj_comp_link_obj(swdiag_obj_system_comp, obj);
        if (obj->type == OBJ_TYPE_RULE || obj->type == OBJ_TYPE_COMP) {
            swdiag_list_add(swdiag_obj_system_comp->top_depend, obj);
            swdiag_list_add(swdiag_obj_system_comp->bottom_depend, obj);
        }
    }
    
    if (!swdiag_obj_system_comp && strcmp(obj_name, SWDIAG_SYSTEM_COMP) == 0) {
        swdiag_obj_system_comp = obj->t.comp;
    }

    swdiag_trace(obj->i.name, "Created obj '%s'", obj->i.name);

    return (obj);
}

/*
 * Link an object into the System component.
 */
void swdiag_obj_link (obj_t *obj)
{
    swdiag_obj_comp_link_obj(swdiag_obj_system_comp, obj);
}

static void unlink_obj_instance (obj_instance_t *delete_instance)
{
    obj_t *delete_obj, *obj;
    swdiag_list_element_t *current, *element;
    obj_test_t *test;
    obj_action_t *action;
    obj_rule_t *rule, *rule2;
    obj_comp_t *comp;

    if (delete_instance) {
        delete_obj = delete_instance->obj;
        if (delete_instance == &delete_obj->i) {
            /*
             * Deleting the primary instance, remove all links
             * from this object to other ones, and also remove this
             * object from dependency trees and its parent component.
             */

            /*
             * Start off by removing this object from the owning 
             * component.
             */
            swdiag_obj_unlink_from_comp(delete_obj);
            
            /*
             * Remove this object from any dependency tree that it
             * is part of. This is a matter of looking through the
             * parent_depend and child_depend and removing any trace
             * of our object in those objects parent/child depends.
             */
            if (delete_obj->parent_depend) {
                current = delete_obj->parent_depend->head;
                while(current) {
                    obj = current->data;
                    swdiag_list_remove(obj->child_depend, delete_obj);
                    current = current->next;
                }
            }

            if (delete_obj->child_depend) {
                current = delete_obj->child_depend->head;
                while(current) {
                    obj = current->data;
                    swdiag_list_remove(obj->parent_depend, delete_obj);
                    current = current->next;
                }
            }
            
            /*
             * Removing the domain is quite hard, so given that this type
             * of deletion is not likely to occur often the domains are left
             * in place (recreation of the object could reuse the old domain
             * anyway) we will leave the domains in place.
             */

            /*
             * Type specific deletion
             */
            switch(delete_obj->type) {
            case OBJ_TYPE_TEST:
                test = delete_obj->t.test;
                
                /*
                 * Remove from any schedular queues.
                 */
                swdiag_sched_remove_test(delete_instance);

                /*
                 * remove our link to any rule, and all those rules links
                 * back to us.
                 */
                rule = test->rule;
                while(rule) {
                    swdiag_list_remove(rule->inputs, delete_obj);
                    rule = rule->next_in_input;
                }
                test->rule = NULL;
                break;
            case OBJ_TYPE_ACTION:
                action = delete_obj->t.action;
                /*
                 * Remove all references to rules that point to us, and remove
                 * ourself from all rules that point to us.
                 */
                while ((obj = swdiag_list_pop(action->rule_list)) != NULL) {
                    if (obj->type == OBJ_TYPE_RULE) {
                        rule = obj->t.rule;
                        swdiag_list_remove(rule->action_list, delete_obj);
                    }
                }
                break;
            case OBJ_TYPE_RULE:
                rule = delete_obj->t.rule;
                /*
                 * input, action_list, output, next_in_input 
                 */

                /*
                 * Remove reference to the inputs that triggers this
                 * rule. Also remove any reference (if any) of those
                 * inputs to us.
                 */
                if (rule->inputs) {
                    for(element = rule->inputs->head;
                        element != NULL;
                        element = element->next) {
                        obj = (obj_t*)element->data;
                        
                        rule2 = NULL;
                        
                        switch (obj->type) {
                        case OBJ_TYPE_RULE:
                            /*
                             * Our input was a rule, and it was pointing
                             * at us, so we are the first (maybe only) rule
                             * triggering from that rule. Point that rule at
                             * the next rule also triggering from that rule.
                             */
                            if ((rule2 = obj->t.rule->output) == rule) {
                                obj->t.rule->output = rule->next_in_input;
                                rule->next_in_input = NULL;
                            }
                            break;
                        case OBJ_TYPE_TEST:
                            /*
                             * Our input was a test, and it was pointing
                             * at us, so we are the first (maybe only) rule
                             * triggering from that test. Point that test at
                             * the next rule also triggering from that test.
                             */
                            if ((rule2 = obj->t.test->rule) == rule) {
                                obj->t.test->rule = rule->next_in_input;
                                rule->next_in_input = NULL;
                            }
                            break;
                        case OBJ_TYPE_NONE:
                            if ((rule2 = obj->ref_rule) == rule) {
                                obj->ref_rule = rule->next_in_input;
                                rule->next_in_input = NULL;
                            }
                            break;
                        default:
                            swdiag_error("Unexpected input type to '%s' when deleting",
                                         swdiag_obj_instance_name(delete_instance));
                            break;
                        }
                        
                        /*
                         * Remove ourselves from the middle/end of a list of 
                         * next_in_input rules for our input. If we were the
                         * head then we've already done this.
                         *
                         * rule2 will already be set to the first rule in the 
                         * list of rules with this input, so just walk along 
                         * them until this rule is found, and remove it.
                         */
                        while (rule2 && rule2->next_in_input) {
                            if (rule2->next_in_input == rule) {
                                rule2->next_in_input = rule->next_in_input;
                                break;
                            }
                            rule2 = rule2->next_in_input;
                        }
                    }

                    /*
                     * Clear all inputs
                     */
                    while((obj = swdiag_list_pop(rule->inputs)) != NULL) {
                        /*
                         * Discard obj
                         */
                    }
                }
                /*
                 * Rules can be chained, so this rule could be the input
                 * for other rules, they need to be identified and ourself
                 * removed.
                 */
                rule2 = rule->output;
                while(rule2) {
                    swdiag_list_remove(rule2->inputs, rule);
                    rule2 = rule2->next_in_input;
                }
                rule->output = NULL;

                /*
                 * Remove our reference to all actions, and ourself from 
                 * all of those actions.
                 */
                while ((obj = swdiag_list_pop(rule->action_list)) != NULL) {
                    action = obj->t.action;
                    swdiag_list_remove(action->rule_list, delete_obj);
                }
                
                break;
            case OBJ_TYPE_COMP:
                comp = delete_obj->t.comp;
                /* remove all members from the component, so that they
                 * end up in the system component.
                 *
                 * Rather than iterate across the contents of the component
                 * just get the first object, over and over on the assumption
                 * that it is removed and therefore changes each time.
                 */
                while((obj = comp_get_next_any(delete_obj, comp)) != NULL) {
                    swdiag_api_comp_contains(swdiag_obj_system_comp->obj, obj);
                }

                /*
                 * Delete all interested test objects since they reference
                 * this component in their context.
                 */
                while((obj = swdiag_list_pop(comp->interested_test_objs)) != NULL) {
                    swdiag_obj_delete(obj);
                }
                break;
            case OBJ_TYPE_NONE:   
                /*
                 * Remove any reference to ourselves from the referenced
                 * rule.
                 */
                rule = delete_obj->ref_rule;
                while(rule) {
                    swdiag_list_remove(rule->inputs, delete_obj);
                    rule = rule->next_in_input;
                }
                delete_obj->ref_rule = NULL;
                break;
            case OBJ_TYPE_ANY:
                break;
            }
        } else {
            if (delete_instance == delete_obj->i.next) {
                /*
                 * Head.
                 */
                delete_obj->i.next = delete_instance->next;
            } else {
                /*
                 * In the middle or end
                 */
                if (delete_instance->prev) {
                    delete_instance->prev->next = delete_instance->next;
                } else {
                    swdiag_error("Invalid prev pointer");
                }
            }
            
            if (delete_instance->next) {
                delete_instance->next->prev = delete_instance->prev;
            }
            
            /*
             * remove all links from this instance
             */
            delete_instance->next = NULL;
            delete_instance->prev = NULL;
        }
    }
}

/*
 * Mark the given object as deleted.
 * We don't free the memory, rather we set the state to DELETED so that
 * so that pointers to this object can remain valid.
 *
 * The object is then added to our garbage detection queue waiting for
 * safe deletion. 
 */
void swdiag_obj_delete (obj_t *obj)
{
    obj_instance_t *instance;

    if (obj) {
        obj->i.state = OBJ_STATE_DELETED;

        /*
         * Remove all instances (apart from the first static one).
         */
        while (obj->i.next != NULL) {
            instance = obj->i.next;
            swdiag_obj_instance_delete(instance);
        }  

        instance = &obj->i;

        /*
         * Make sure that any RCI is reevaluated in the context
         * of this deletion.
         */
        if (instance->obj->type == OBJ_TYPE_RULE) {
            swdiag_rci_rule_deleted(instance);
        }
        
        /*
         * Remove this object from the obj DB
         */
        unlink_obj_instance(instance);

        /*
         * Free the object once we are 100% sure that noone is 
         * referencing it.
         */
        swdiag_list_push(freeme, instance);

        swdiag_trace(obj->i.name, "DELETED %s '%s'",
                     swdiag_obj_type_str(obj->type), obj->i.name);   
    }

}

/*
 * Mark an object of given name and type as deleted.
 * If the type is not known then the value OBJ_TYPE_ANY should be used.
 * Returns TRUE upon success, FALSE otherwise.
 * Otherwise the same as swdiag_obj_delete()
 */
boolean swdiag_obj_delete_by_name (const char *name, obj_type_t type)
{
    obj_t *obj;
    boolean rc = FALSE;
    
    obj = swdiag_obj_get_by_name(name, type);
    if (obj) {
        swdiag_obj_delete(obj);
        rc = TRUE;
    }
    return (rc);
}

boolean swdiag_obj_delete_by_name_unconverted (const char *name, 
                                               obj_type_t type)
{
    obj_t *obj;
    boolean rc = FALSE;
    
    obj = swdiag_obj_get_by_name_unconverted(name, type);
    if (obj) {
        swdiag_obj_delete(obj);
        rc = TRUE;
    }
    return (rc);
}

/*
 * Recursive function to change the state of all the objects 
 * connected in a chain under this object to the desired state
 * 
 * state is the desired state unless default_state or cli_state 
 * in the object says otherwise.
 */
void swdiag_obj_chain_update_state (obj_t *obj, obj_state_t state)
{
    obj_instance_t *instance;

    if (!swdiag_obj_validate(obj, OBJ_TYPE_ANY)) {
        return;
    }

    /*
     * Ignore objects that are deleted.
     */
    if (obj->i.state == OBJ_STATE_DELETED) {
        return;
    }

    switch(obj->type) {
    case OBJ_TYPE_TEST:
    {
        obj_test_t *test = obj->t.test;
        /*
         * Locate the connected rule
         */
        if (test->rule) {
            swdiag_obj_chain_update_state(test->rule->obj, state);
        }
        break;
    }
    case OBJ_TYPE_RULE:
    {
        obj_rule_t *rule = obj->t.rule;
        obj_t *action_obj = NULL;
        swdiag_list_element_t *element;

        /*
         * Recurse along all the peer rules
         */
        if (rule->next_in_input) {
            swdiag_obj_chain_update_state(rule->next_in_input->obj, state);
        }
        
        /*
         * Now do every action that this rule triggers
         */
        for (element = rule->action_list->head;
             element != NULL;
             element = element->next) {
            action_obj = (obj_t*)element->data;
            swdiag_obj_chain_update_state(action_obj, state);
        }

        /*
         * And if there was an output rule - go there.
         */
        if (rule->output) {
            swdiag_obj_chain_update_state(rule->output->obj, state);
        }
        break;
    }
    case OBJ_TYPE_ACTION:
        /*
         * Nothing explicit to do for actions
         */
        break;
    case OBJ_TYPE_COMP:  
    default:
        /*
         * Not a valid object type in a test chain.
         */
        swdiag_error("Invalid object type in a test chain");
        return;
    }
    
    /*
     * And finally change the state of this object and all of its 
     * instances
     */
    for (instance = &obj->i; instance != NULL; instance = instance->next) {
        if (instance->cli_state != OBJ_STATE_INITIALIZED) {
            /*
             * Allow the CLI to override the state that we are setting.
             */
            instance->state = instance->cli_state;
        } else { 
            /*
             * There is no CLI override, so we should be setting the
             * state on the objects to the desired state, unless there
             * is a pre-existing default_state on that object - which
             * will act as an override.
             */
            if (instance->state != OBJ_STATE_INITIALIZED) {
                /*
                 * Only set the state for objects that have been
                 * created, i.e. don't change forward references.
                 */
                if (instance->default_state == OBJ_STATE_INITIALIZED) {
                    /*
                     * No existing default_state so use the state
                     * that we've been instructed to use.
                     */
                    instance->state = instance->default_state = state;
                } else {
                    /*
                     * There is an existing default_state for this
                     * object - s we should be using that instead of
                     * whatever state we've been told to use.
                     *
                     * This allows us to keep the state that has been
                     * set from the API.
                     */
                    instance->state = instance->default_state;
                }
            }
        }
    }
}

/*
 * swdiag_obj_instance_validate()
 *
 * Validate an object instance.
 */
boolean swdiag_obj_instance_validate (obj_instance_t *instance, 
                                      obj_type_t type)
{
    boolean retval = TRUE;
    
    if (!instance || instance == (void*)0x0d0d0d0d || !instance->obj) {
        swdiag_error("Validate:%s: Invalid instance=%p",
                     (instance && instance != (void*)0x0d0d0d0d) ? instance->name : "", instance);
        retval = FALSE;
    } else {
        if (instance->state == OBJ_STATE_DELETED) {
            /*
             * Deleted instances are not valid.
             */
            retval = FALSE;
        }
    }
    
    return(retval && swdiag_obj_validate(instance->obj, type));
}

/*
 * swdiag_obj_validate()
 *
 * Validate that the contents of the object are internally consistent,
 * this API is called a lot, so don't do too much here.
 */
boolean swdiag_obj_validate (obj_t *obj, obj_type_t type)
{
    boolean retval = TRUE;

    if (!obj || obj == (void*)0x0d0d0d0d || obj->ident != obj_idents[OBJ_TYPE_NONE]) {
        swdiag_error("Validate: Invalid object magic obj=%p ident=0x%x",
                     obj, (obj && obj != (void*)0x0d0d0d0d) ? obj->ident : 0);
        retval = FALSE;
        return(retval);
    }
    
    /*
     * The ident is in the same place for all object types.
     */
    if (obj->type != OBJ_TYPE_NONE && (!obj->t.test || obj->t.test->ident != obj_idents[obj->type])) {
        swdiag_error("Validate: Invalid object type magic %p:0x%x",
                     obj->t.test, obj->t.test ? obj->t.test->ident : 0);
        retval = FALSE;
        return(retval);
    }

    if (obj->i.state == OBJ_STATE_DELETED) {
        /*
         * Deleted objects are invalid.
         */
        retval = FALSE;
        return(retval);
    }

    if (type != OBJ_TYPE_ANY && type != obj->type) {
        /*
         * Wrong type!
         */
        swdiag_error("Validate:%s: Invalid type %d, wanted %d",
                     obj->i.name, obj->type, type);
        retval = FALSE;
        return(retval);
    }

    switch(obj->type) {
    case OBJ_TYPE_TEST:
    {
        obj_test_t *test = obj->t.test;

        if (test->obj != obj) {
            swdiag_error("Validate:%s: Incorrect Test Linkage, cprrected",
                         obj->i.name);
            /*
             * Fix it
             */
            test->obj = obj;
        }

        if (test->rule && test->rule->ident != obj_idents[OBJ_TYPE_RULE]) {
            /*
             * Not a rule!
             */
            swdiag_error("Validate:%s: Test not connected to a valid rule, cleared",
                         obj->i.name);
            /*
             * Clear it.
             */
            test->rule = NULL;
        }

        if (obj->i.state != OBJ_STATE_ALLOCATED &&
            obj->i.state != OBJ_STATE_INITIALIZED) {
            /*
             * The following attributes of a test may not be valid 
             * until creation is complete.
             */
            switch(test->type) {
            case  OBJ_TEST_TYPE_POLLED:
                if (test->period == 0 || test->function == 0) {
                    swdiag_error("Validate:%s: Polled test with no period or no function",
                                 obj->i.name);
                    retval = FALSE;
                }
                break;     
            case OBJ_TEST_TYPE_ERRMSG:
                if (obj->i.context == 0) {
                    swdiag_error("Validate:%s: Error message test with no msgsym",
                                 obj->i.name);
                    retval = FALSE;
                }
                /* FALLTHRU */
            case OBJ_TEST_TYPE_NOTIFICATION:
                if (test->period != 0 || test->function != 0) {
                    swdiag_error("Validate:%s: Period or test function set on notification test", 
                                 obj->i.name);
                    retval = FALSE;
                }
                break;
            }
        }
        /*
         * Test the flags
         */
        if ((obj->i.flags.test & ~OBJ_FLAG_RESERVED) > SWDIAG_TEST_FLAG_ALL) {
            /*
             * Flags corrupted!
             */
            swdiag_error("Validate:%s: Invalid test flags 0x%x, cleared", 
                         obj->i.name, 
                         obj->i.flags.test & ~OBJ_FLAG_RESERVED);

            /*
             * Clear the lot - including the reserved ones.
             */
            obj->i.flags.test = 0;
        }
    }
    break;
    case OBJ_TYPE_RULE:
    {
        obj_rule_t *rule = obj->t.rule, *rule2;
        swdiag_list_element_t *element;
        obj_action_t *action;
        obj_t *input_obj;

        if (!rule->inputs) {
            swdiag_error("Validate:%s: No inputs in rule, corrected", obj->i.name);
            rule->inputs = swdiag_list_create();
        }

        /*
         * Check that all inputs point back to this rule.
         */
        element = rule->inputs->head;
        while (element != NULL) {
            boolean matched = FALSE;
            input_obj = element->data;
            
            switch(input_obj->type) {
            case OBJ_TYPE_RULE:
                rule2 = input_obj->t.rule->output;
                break;
            case OBJ_TYPE_TEST:
                rule2 = input_obj->t.test->rule;
                break;
            case OBJ_TYPE_NONE:
                rule2 = input_obj->ref_rule;
                break;
            default:
                swdiag_error("Validate:%s: Unexpected input type, removing '%s'",
                             obj->i.name, input_obj->i.name);
                swdiag_list_remove(rule->inputs, input_obj);
                /*
                 * Start back at the new head
                 */
                element = rule->inputs->head;
                continue;
            }
            
            while (rule2) {
                if (rule2 == rule) {
                    /*
                     * Matched
                     */
                    matched = TRUE;
                    break;
                }
                rule2 = rule2->next_in_input;
            }

            if (!matched) {
                swdiag_error("Validate:%s: Badly connected object, removing '%s'",
                             obj->i.name, input_obj->i.name);
                swdiag_list_remove(rule->inputs, input_obj);
                /*
                 * Start back at the new head
                 */
                element = rule->inputs->head;
                continue;
            }
            element = element->next;
        }
            
        if (!rule->action_list) {
            swdiag_error("Validate:%s: No action_list in rule, corrected", obj->i.name);
            rule->action_list = swdiag_list_create();
        }
        
        /*
         * Confirm that all the actions that this rule triggers point
         * back to this rule.
         */
        for (element = rule->action_list->head;
             element != NULL;
             element = element->next) {
            obj_t *action_obj = (obj_t*)element->data;
            
            /*
             * Don't call validate on the action to avoid a loop, just
             * do a quick sanity check here
             */
            if (action_obj &&
                action_obj->type == OBJ_TYPE_ACTION &&
                action_obj->t.action &&
                action_obj->t.action->ident == obj_idents[OBJ_TYPE_ACTION]) {
                action = action_obj->t.action;
                if (!swdiag_list_find(action->rule_list, obj)) {
                    /*
                     * Action has no link back to this rule, fix it 
                     */
                    swdiag_list_add(action->rule_list, obj);
                    swdiag_error("Validate:%s: Rule refers to action that has no link back to this rule, corrected.", 
                                 obj->i.name);
                }
            }

        }
        break;
    }
    case OBJ_TYPE_ACTION:
    {
        obj_action_t *action = obj->t.action;
        swdiag_list_element_t *element;
        obj_rule_t *rule;

        if (!action->rule_list) {
            swdiag_error("Validate:%s: No rule_list in action", obj->i.name);
            action->rule_list = swdiag_list_create();
        }

        /*
         * Confirm that all the rules that this action is linked to also
         * point back to this action.
         */
        for (element = action->rule_list->head;
             element != NULL;
             element = element->next) {
            obj_t *rule_obj = (obj_t*)element->data;
            
            /*
             * Don't call validate on the action to avoid a loop, just
             * do a quick sanity check here
             */
            if (rule_obj &&
                rule_obj->type == OBJ_TYPE_RULE &&
                rule_obj->t.rule &&
                rule_obj->t.rule->ident == obj_idents[OBJ_TYPE_RULE]) {
                rule = rule_obj->t.rule;
                if (!swdiag_list_find(rule->action_list, obj)) {
                    /*
                     * Rule has no link back to this action, fix it 
                     */
                    swdiag_list_add(rule->action_list, obj);
                    swdiag_error("Validate:%s: Action refers to rule that has no link back to this action, corrected", 
                                 obj->i.name);
                }
            }

        }

        break;
    }
    case OBJ_TYPE_COMP:
    {
        obj_comp_t *comp = obj->t.comp;
        
        if (!comp->top_depend) {
            /*
             * Try to fix the problem by creating the list,
             * we should really go through all the objects within
             * this component and rebuild the list, but the odds of
             * this occuring are less than the effort involved in
             * coding this.
             */
            swdiag_error("Validate:%s: No component top dependency list",
                         obj->i.name);
            comp->top_depend = swdiag_list_create();
            if (!comp->top_depend) {
                /*
                 * Couldn't fix
                 */
                swdiag_error("Validate:%s: Component top dependency list could not be created",
                             obj->i.name);
                retval = FALSE;
            }
        }
        if (!comp->bottom_depend) {
            /*
             * Try to fix the problem by creating the list,
             * we should really go through all the objects within
             * this component and rebuild the list, but the odds of
             * this occuring are less than the effort involved in
             * coding this.
             */
            swdiag_error("Validate:%s: No component bottom dependency list",
                         obj->i.name);
            comp->bottom_depend = swdiag_list_create();
            if (!comp->bottom_depend) {
                /*
                 * Couldn't fix
                 */
                swdiag_error("Validate:%s: Component bottom dependency list could not be created",
                             obj->i.name);
                retval = FALSE;
            }
        }
        if (!comp->interested_test_objs) {
            /*
             * Try to fix the problem by creating the list,
             */
            swdiag_error("Validate:%s: No interested test objects list",
                         obj->i.name);
            comp->interested_test_objs = swdiag_list_create();
            if (!comp->interested_test_objs) {
                /*
                 * Couldn't fix
                 */
                swdiag_error("Validate:%s: Component interested test objects list could not be created",
                             obj->i.name);
                retval = FALSE;
            }
        }
        break;
    }
    case OBJ_TYPE_NONE:
    {
        if (obj->t.test != NULL) {
            /*
             * Type structure set, but the type is unknown!
             */
             retval = FALSE;
             swdiag_error("Validate:%s: Type block set when type is none",
                          obj->i.name);
        }
        break;
    }
    default:
        /*
         * Invalid object to validate.
         */
        swdiag_error("Invalid object type to validate");
        retval = FALSE;
        break;
    }

    /*
     * Check that the object is within a component, unless it
     * is the system component (which is not within a component)
     */
    if (!obj->parent_comp) {
        /* 
         * No parent component, we can fix this, put this
         * object in the system component.
         */
        if (strcmp(obj->i.name, SWDIAG_SYSTEM_COMP) != 0) {
            /*
             * Fix up the component - finish off the containment by
             * using the public API.
             */
            obj->parent_comp = swdiag_obj_system_comp;
            swdiag_comp_contains(SWDIAG_SYSTEM_COMP, obj->i.name);
            
            if (!obj->parent_comp) {
                /*
                 * Couldn't fix it - sorry.
                 */
                swdiag_error("Validate:%s: Object could not be added to a component",
                             obj->i.name);
                retval = FALSE;
            }
        }
    }
    return(retval);
}

/*
 * swdiag_obj_instance_create()
 *
 * Create an instance on this object and return it.
 */
obj_instance_t *swdiag_obj_instance_create (obj_t *obj,
                                            const char *instance_name)
{
    obj_instance_t *instance;

    instance = calloc(1, sizeof(obj_instance_t));

    if (!instance) {
        swdiag_error("Failed allocation of instance '%s'", instance_name);
        return (NULL);
    }

    instance->name = strdup(instance_name);
    instance->state = OBJ_STATE_ALLOCATED;
    instance->cli_state = OBJ_STATE_INITIALIZED;
    instance->obj = obj;

    instance->sched_test.instance = instance;

    /*
     * Now link it into the instance list for this object
     */
    instance->next = obj->i.next;
    if (instance->next) {
        instance->next->prev = instance;
    }
    obj->i.next = instance;
    instance->prev = &obj->i;
    return(instance);
}

/*
 * swdiag_obj_instance_delete()
 *
 * Delete the instance from its owning object
 */
void swdiag_obj_instance_delete (obj_instance_t *instance)
{
    obj_t *obj;

    if (instance) {
        obj = instance->obj;
        if (instance == &obj->i) {
            /*
             * Don't try and delete the static instance
             */
            swdiag_error("Attempt to delete base instance");
        } else { 
            /*
             * Always mark the state as deleted first!
             */
            instance->state = OBJ_STATE_DELETED;

            if (instance->obj->type == OBJ_TYPE_RULE) {
                swdiag_rci_rule_deleted(instance);
            }

            /*
             * And now remove it from the tree.
             */
            unlink_obj_instance(instance);

            /*
             * And free it when we get a chance to.
             */
            swdiag_list_push(freeme, instance);
            swdiag_debug(NULL, "Deleted instance '%s'", 
                         swdiag_obj_instance_name(instance));
        }
    }
}

/*
 * swdiag_obj_instance()
 *
 * Look for an instance with the name "instance_name" within this object,
 * if no instances are found with this name then return NULL. If the 
 * instance_name is NULL, then return the first instance.
 */
obj_instance_t *swdiag_obj_instance_by_name (obj_t *obj, const char *instance_name)
{
    obj_instance_t *instance;

    if (!swdiag_obj_validate(obj, OBJ_TYPE_ANY)) {
        return(NULL);
    }

    instance = &obj->i;

    if (instance_name) {
        /*
         * Just do a linear search for now to profile the performance
         * improvements should this change to a hash table.
         */
        while ((instance = instance->next) != NULL) {
            if (strcmp(instance->name, instance_name) == 0) {
                break;
            }
        }
    }

    return(instance);
}

/*
 * Using the "red_instance" name, try and find a matching instance in
 * the "obj". 
 */
obj_instance_t *swdiag_obj_instance (obj_t *obj, obj_instance_t *ref_instance)
{
    obj_instance_t *instance, *retval;
    const char *instance_name = NULL;

    if (&ref_instance->obj->i != ref_instance) {
        /* 
         * The reference instance is not the object instance, so we need
         * to search for it.
         */
        instance_name = ref_instance->name;
    }

    instance = retval = &obj->i;

    if (instance_name) {
        /*
         * Just do a linear search for now to profile the performance
         * improvements should this change to a hash table.
         */
        while ((instance = instance->next) != NULL) {
            if (strcmp(instance->name, instance_name) == 0) {
                retval = instance;
                break;
            }
        }
    }
    return(retval);
}

/*
 * swdiag_obj_is_member_instance()
 *
 * There are two types of instances, the instance within the object,
 * which is used when there are no instances, and when there are instances
 * it summarises the results for all the member instances. And then there
 * are the member instances which are chained from the leader.
 *
 * This API returns TRUE if the instance is a member instance and FALSE if
 * it is the leader.
 */
boolean swdiag_obj_is_member_instance (obj_instance_t *instance)
{
    if (&instance->obj->i == instance) {
        return(FALSE);
    } else {
        return(TRUE);
    }
}

boolean swdiag_obj_has_member_instances (obj_t *obj)
{
    return(obj->i.next != NULL);
}

/*
 * swdiag_obj_instance_name()
 *
 * Return a string for this object instance, formed out of the object
 * name and the instance name concatenated to it.
 *
 * Use the name quickly since it may be recycled from under you.
 */
const char *swdiag_obj_instance_name (obj_instance_t *instance)
{
    const char *name = NULL;

    if (instance) {
        if (swdiag_obj_is_member_instance(instance)) {
            name = swdiag_api_make_name(instance->obj->i.name, instance->name);
        } else {
            /*
             * This is the instance for the object itself, so just return
             * that.
             */
            name = instance->name;
        } 
    }
    return(name);
}

/*******************************************************************
 * Exported String Functions
 *******************************************************************/

const char *swdiag_obj_state_str (obj_state_t state)
{
    switch(state) {
    case OBJ_STATE_ALLOCATED:
        return("Allocated");
        break;

    case OBJ_STATE_INITIALIZED:
        return("Initialized");
        break;

    case OBJ_STATE_CREATED:
        return("Created");
        break;

    case OBJ_STATE_ENABLED:
        return ("Enabled");
        break;
        
    case OBJ_STATE_DISABLED:
        return ("Disabled");
        break;

    case OBJ_STATE_DELETED:
        return ("Deleted");
        break;

    case OBJ_STATE_INVALID:
        return("Invalid");
        break;
    }

    return("Unknown");
}

const char *swdiag_obj_type_str (obj_type_t type)
{
    switch (type) {
      case OBJ_TYPE_ANY:
        return ("Any");
        break;

      case OBJ_TYPE_NONE:
        return ("None");
        break;

      case OBJ_TYPE_TEST:
        return ("Test");
        break;

      case OBJ_TYPE_RULE:
        return ("Rule");
        break;

      case OBJ_TYPE_ACTION:
        return ("Action");
        break;

      case OBJ_TYPE_COMP:
        return ("Comp");
        break;
    }
    return("Bad-type");
}

const char *swdiag_obj_rel_str (obj_rel_t rel)
{
    switch (rel) {
       case OBJ_REL_NONE:
        return ("None");
        break;

       case OBJ_REL_TEST:
        return ("Test");
        break;

      case OBJ_REL_RULE:
        return ("Rule");
        break;

      case OBJ_REL_ACTION:
        return ("Action");
        break;

      case OBJ_REL_COMP:
        return ("Comp");
        break;

      case OBJ_REL_NEXT_IN_SYS:
        return ("Next-in-sys");
        break;

      case OBJ_REL_NEXT_IN_COMP:
        return ("Next-in-comp");
        break;

      case OBJ_REL_NEXT_IN_TEST:
        return ("Next-in-test");
        break;

      case OBJ_REL_PARENT_COMP:
        return ("Parent-comp");
        break;

      case OBJ_REL_CHILD_COMP:
        return ("Child-comp");
        break;
    }

    return("Bad-relative");
}

void swdiag_obj_db_lock (void)
{
    /*
     * Attempt to get the mutex for this thread, if it already has
     * it this will fall through.
     */
    if (!obj_db_lock) {
        obj_db_lock = swdiag_xos_critical_section_create();
    }
    swdiag_debug(NULL, "Entering Mutux %p (%d)", obj_db_lock, obj_db_count);
    swdiag_xos_critical_section_enter(obj_db_lock);
    obj_db_count++;
    swdiag_debug(NULL, "Obtained Mutux %p (%d)", obj_db_lock, obj_db_count);

}

void swdiag_obj_db_unlock (void)
{
    obj_db_count--;  
    swdiag_xos_critical_section_exit(obj_db_lock);
    swdiag_debug(NULL, "Exited Mutux %p (%d)", obj_db_lock, obj_db_count);

}

int swdiag_obj_ut_get_lock_count (void)
{
    return(obj_db_count);
}

/*******************************************************************
 * Module Initialization
 *******************************************************************/

/*
 * Signal that the garbage collector should run now.
 */
static void garbage_collector_timer_expired (void *context)
{
    if (!garbage_collector || !swdiag_xos_thread_release(garbage_collector->xos)) {
        swdiag_error("Garbage collector thread not running");
    }
}

/*
 * Process a few of the objects on the freeme queue, check whether it
 * is safe to free them yet.
 */
static boolean process_freeme_queue (void)
{
    obj_instance_t *instance;
    obj_t *obj;
    obj_test_t *test;
    obj_rule_t *rule;
    obj_action_t *action;
    obj_comp_t *comp;
    unsigned int counter = 1, process_count;
    
    process_count = ((freeme->num_elements * 100) / GARBAGE_QUEUE_RATE);


    if (process_count < GARBAGE_QUEUE_FLOOR) {
        process_count = GARBAGE_QUEUE_FLOOR;
    }

    swdiag_debug(NULL, 
                 "Garbage collector starting, %d instances in freeme queue (processing=%d in this pass)",
                 freeme->num_elements, process_count);

    while ((instance = swdiag_list_pop(freeme)) != NULL) {
        /*
         * Try not to lock the DB for too long to allow other 
         * processing to take place.
         */
        swdiag_obj_db_lock();

        if (instance->state == OBJ_STATE_DELETED) {
            /*
             * If the object is in_use then there is a client function
             * running for it, which we have to wait for it to complete.
             */
            if (instance->in_use) {
                swdiag_list_push(freeme, instance);
            } else {
                /*
                 * Not in use, do a quick search of all the places
                 * that may be keeping a reference to this object, if
                 * not found then free it.
                 */
                //swdiag_trace(swdiag_obj_instance_name(instance), "Freeing memory in garbage collector '%s'", swdiag_obj_instance_name(instance));

                free(instance->name);
                if (swdiag_obj_is_member_instance(instance)) {
                	free(instance);
                } else {
                    obj = instance->obj;
                    if (obj->description) {
                        free(obj->description);
                    }
                    
                    switch(obj->type) {
                    case OBJ_TYPE_TEST:
                        test = obj->t.test;
                        free(test);
                        obj->t.test = NULL;
                        break;
                    case OBJ_TYPE_ACTION:
                        action = obj->t.action;
                        swdiag_list_free(action->rule_list);
                        free(action);
                        obj->t.action = NULL;
                        break;
                    case OBJ_TYPE_RULE:
                        rule = obj->t.rule;
                        swdiag_list_free(rule->inputs);
                        swdiag_list_free(rule->action_list);
                        free(rule);
                        obj->t.rule = NULL;
                        break;
                    case OBJ_TYPE_COMP:
                        comp = obj->t.comp;
                        swdiag_list_free(comp->top_depend);
                        swdiag_list_free(comp->bottom_depend);
                        swdiag_list_free(comp->interested_test_objs);
                        free(comp);
                        obj->t.comp = NULL;
                        break;
                    case OBJ_TYPE_NONE:
                        /*
                         * Nothing to free for type none.
                         */
                        break;
                    case OBJ_TYPE_ANY:
                        break;
                    }
                    free(obj);
                }
            }
        } else {
            /*
             * What is it doing on this queue if it isn't deleted?
             */
            swdiag_error("Garbage Collection: Invalid state for object '%s'",
                         swdiag_obj_instance_name(instance));
        }
        swdiag_obj_db_unlock();

        if (++counter > process_count) {
            swdiag_debug(NULL, 
                         "Garbage collector suspending, %d instances left in freeme queue",
                         freeme->num_elements);
            return(FALSE);
        }
    }
    swdiag_debug(NULL, "Garbage collection complete");
    return(TRUE);
}


/*
 * garbage_collector_main()
 *
 * The garbage collector searches the entire obj_db looking for all
 * references to objects in the freeme list. If none are found then
 * the memory for that object is freed.
 */
static void garbage_collector_main (swdiag_thread_t *thread)
{
    /*
     * We have a timer that goes off once in a while so that we can
     * do some garbage collection, when that timer goes off it signals
     * this process to run.
     */
    if (!garbage_collector) {
        garbage_collector = thread;
    }

    if (start_garbage_collect) {
        swdiag_xos_timer_delete(start_garbage_collect);
    }

    start_garbage_collect = swdiag_xos_timer_create(
        garbage_collector_timer_expired, NULL);

    if (!start_garbage_collect) {
        swdiag_error("Garbage collector failed to start, could not create timer");
        return;
    }

    swdiag_xos_timer_start(start_garbage_collect, GARBAGE_PERIOD_SEC, 0);

    while (!thread->quit) {
        swdiag_xos_thread_wait(thread->xos);

        if (thread->quit) {
        	continue;
        }
        if (process_freeme_queue()) {
            /*
             * Finished all queue processing, we can wait a while before 
             * doing any more. 
             */
            swdiag_xos_timer_start(start_garbage_collect, 
                                   GARBAGE_PERIOD_SEC, 0);
        } else {
            /*
             * Still got more to process, but we don't want to be too
             * greedy, so wait a while before doing any more.
             */
            swdiag_xos_timer_start(start_garbage_collect, 
                                   GARBAGE_QUEUE_SLEEP, 0);
        }
        swdiag_cli_local_handle_free_garbage();
    }

    swdiag_xos_thread_destroy(garbage_collector);
    free(garbage_collector);
    garbage_collector = NULL;
}

/*
 * swdiag_obj_init()
 */
void swdiag_obj_init (void)
{
    freeme = swdiag_list_create();

    if (!freeme) {
        swdiag_error("Failed to allocate garbage detector");
        return;
    }

    /*
     * Create and start the garbage detector thread.
     */
    garbage_collector = (swdiag_thread_t *)malloc(sizeof(swdiag_thread_t));
    if (!garbage_collector) {
        swdiag_error("Failed to allocate garbage detector thread");
        return;
    }

    garbage_collector->quit = FALSE;
    garbage_collector->xos = swdiag_xos_thread_create("SWDiag Garbage Collector",
                                                      garbage_collector_main,
                                                      garbage_collector);
    if (!garbage_collector->xos) {
         swdiag_error("Failed to create garbage detector thread");
         return;
    }
}

/*
 * Run the garbage collector and terminate it
 */
void swdiag_obj_terminate (void)
{
	if (garbage_collector) {
		swdiag_thread_kill(garbage_collector);
		swdiag_xos_sleep(1);
	}


	swdiag_list_free(freeme);
	freeme = NULL;
}
/*
 * The following functions are for accessing static (private) contents of this module for the
 * purpose of black box testing this module. They are not to be used outside of unit testing.
 */
swdiag_list_t *swdiag_obj_test_get_freeme()
{
	return freeme;
}

swdiag_thread_t *swdiag_obj_test_get_garbage_collector()
{
	return garbage_collector;
}

void swdiag_obj_test_run_garbage_collector()
{
	process_freeme_queue();
}
