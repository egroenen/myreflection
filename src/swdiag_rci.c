/* 
 * swdiag_rci.c - SW Diagnostics Root Cause Analysis
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
 * Subsystem and functions that perform root cause identification
 * of failed clients registered with diagnostics.
 */

#include "swdiag_obj.h"
#include "swdiag_util.h"
#include "swdiag_trace.h"
#include "swdiag_rci.h"
#include "swdiag_api.h"
#include "swdiag_sequence.h"

static boolean rci_schedule_dependent_rules(obj_instance_t *rule);
static boolean rci_map_function(obj_instance_t *instance,
                                const char *instance_name,
                                rci_map_direction_t direction,
                                rci_map_function_t function,
                                swdiag_list_t *history, 
                                boolean default_state, 
                                void *context);
static void rci_propagate_rule_change(obj_instance_t *rule_of_interest,
                                      swdiag_result_t action);

/*
 * Contain the rule and state that is being propagated, e.g. when a new
 * root cause is detected all root causes are cleared above that point.
 */
typedef struct {
    obj_instance_t *rule_of_interest;
    swdiag_result_t action;
} rci_propagate_context_t;

/*
 * List of loop domains for loop detection
 */
static loop_domain_t *domains = NULL;

/*
 * What is the next_domain to be used for cyclic dependency loop 
 * detection.
 */
static uint next_domain = 1;

/*
 * UT variables
 */
static boolean rci_ut_in_progress = FALSE;
swdiag_list_t *rci_ut_visited_rules = NULL;
swdiag_list_t *rci_ut_scheduled_rules = NULL;

/*
 * is_obj_in_tree()
 *
 * recursivly search the tree looking for "target", returns TRUE
 * if found and FALSE if not found.
 */
static boolean is_obj_in_tree (const obj_t *target, const obj_t *tree)
{
    swdiag_list_element_t *next_child;

    if (!tree || !target) {
        return(FALSE);
    }

    /*
     * Is the current head of the tree our target?
     */
    if (tree == target) {
        return(TRUE);
    } else {
        /*
         * Maybe one of the children is the target?
         */
        next_child = tree->child_depend->head;

        while(next_child) {
            if (is_obj_in_tree(target, (obj_t*)next_child->data)) {
                return(TRUE);
            }
            next_child = next_child->next;
        }
    }
    return(FALSE);
}


/*******************************************************************
 * Exported API Functions
 *******************************************************************/

/*
 * swdiag_is_domain_reachable()
 *
 * Function for finding if a domain "target" is reachable from
 * "container". It is exported because the test code needs to use
 * it, but is not in the header files as it shouldn't be used by
 * anyone else.
 */
boolean swdiag_is_domain_reachable (uint container, uint target)
{
    loop_domain_t *domain = domains;
    boolean retval = FALSE;

    while(domain) {
        if (domain->number == container) {
            retval = swdiag_list_find(domain->reachable, (void*)(uintptr_t)target);
            break;
        }
        domain = domain->next;
    }
    return(retval);
}

/*
 * swdiag_depend_found_comp()
 *
 * Given a dependency list "dependencies" return TRUE if any of the
 * dependent objects are members of component "comp".
 */
boolean swdiag_depend_found_comp (swdiag_list_t *dependencies, 
                                  obj_comp_t *comp)
{
    boolean retval = FALSE;
    swdiag_list_element_t *element;
    obj_t *obj;

    for(element = dependencies->head; element; element = element->next)
    {
        obj = element->data;
        if (obj->parent_comp == comp) {
            retval = TRUE;
            break;
        }
    }
    return(retval);
}

/*
 * swdiag_depend_create()
 *
 * Set up a dependency on child_name that should be run to determine
 * whether the parent_name is the root cause. If the child_name
 * passes the parent_name is designated the root cause.
 *
 * parent_name and child_name may be rule or component names. When a
 * component name is used it will be expanded inline to contain the set
 * of rules contained by that component during RCI not at registration
 * time.
 */
void swdiag_depend_create (const char *parent_name,
                           const char *child_name)
{
    obj_t *parent = NULL, *child = NULL;
    obj_comp_t *comp;
    loop_domain_t *domain = NULL;
    loop_domain_t *parent_domain = NULL;

    swdiag_obj_db_lock();

    parent = swdiag_api_get_or_create(parent_name, OBJ_TYPE_ANY);
    
    if (!parent || (parent->type != OBJ_TYPE_COMP &&
                    parent->type != OBJ_TYPE_RULE &&
                    parent->type != OBJ_TYPE_NONE)) {
        swdiag_error("Parent %s has incorrect type %s for depend create",
                     parent_name, swdiag_obj_type_str(parent->type));
        swdiag_obj_db_unlock();
        return;
    }

    child = swdiag_api_get_or_create(child_name, OBJ_TYPE_ANY);
    if (!child || (child->type != OBJ_TYPE_COMP &&
                   child->type != OBJ_TYPE_RULE &&
                   child->type != OBJ_TYPE_NONE)) {
        swdiag_error("Child %s has type %s for depend_create()",
                     child_name, swdiag_obj_type_str(child->type));
        swdiag_obj_db_unlock();
        return;
    }

    if (!parent || !child || parent == child) { 
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Are they already linked?
     */
    if (swdiag_list_find(parent->child_depend, child) ||
        swdiag_list_find(child->parent_depend, parent)) {
        swdiag_trace(parent_name, "Parent '%s' already depends on child '%s', ignoring",
                     parent_name, child_name); 
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Check the domains of the parent and child objects, 
     */
    if (parent->domain == 0) {
        if (child->domain == 0) {
            /*
             * Neither are in a dependency tree, add them to the
             * same new tree, no need to check for loops.
             */
            domain = malloc(sizeof(loop_domain_t));
            if (domain) {
                domain->number = next_domain++;
                domain->reachable = swdiag_list_create();
                if (!domain->reachable) {
                    swdiag_error("Memory allocation failure");
                    free(domain);
                    swdiag_obj_db_unlock();
                    return;
                }
                domain->next = domains;
                domains = domain;
            } else {
                swdiag_error("Memory allocation failure"); 
                swdiag_obj_db_unlock();
                return;
            }
            parent->domain = child->domain = domain->number;
        } else {
            /*
             * Parent is not in a domain, but child is, add the parent
             * to the childs domain, no need for a loop check since
             * the parent was not already connected.
             */
            parent->domain = child->domain;
        }
    } else if (child->domain == 0) {
        /*
         * Parent is in a domain but child isn't, add the child to the
         * parents domain, no need for a loop check since the child
         * was not already connected.
         */
        child->domain = parent->domain;
    } else if (child->domain == parent->domain) {
        /*
         * Same domain, there may be a loop within this domain. So search
         * down through the children looking for the parent.
         */
        if (is_obj_in_tree(parent, child)) {
            /*
             * This parent is already referenced by one of the children
             * that would create a loop. So don't make this connection.
             */
            swdiag_error("Loop detected creating a dependency between "
                         "'%s' and '%s'", parent->i.name, child->i.name);
            swdiag_obj_db_unlock();
            return;
        } 
    } else {
        /*
         * Different domains. Check to see whether the domains intersect
         * eachother already. If they do then we have a possible loop.
         */
        domain = domains;

        while(domain) {
            if (domain->number == parent->domain) {
                /*
                 * Found the parent domain, add the child domain to
                 * it as it is now reachable.
                 */
                swdiag_list_add(domain->reachable,
                                (void*)(uintptr_t)child->domain);
                parent_domain = domain;

            } else if (domain->number == child->domain) {
                /*
                 * Can the child contact the parent, if so we have a possible
                 * loop and need to walk the tree to find out.
                 */
                if (swdiag_list_find(domain->reachable, 
                                     (void*)(uintptr_t)parent->domain)) {
                    if (is_obj_in_tree(parent, child)) {
                        /*
                         * Need to go back through the domains and find the
                         * parent and remove the child from the reachable
                         * domains since we are not allowing this connection
                         */
                        if (parent_domain) {
                            swdiag_list_remove(parent_domain->reachable,
                                               (void*)(uintptr_t)child->domain);
                        }

                        swdiag_error("Loop detected creating a dependency between "
                                     "'%s' to '%s'", parent->i.name, 
                                     child->i.name);
                        swdiag_obj_db_unlock();
                        return;
                    }
                }
            } else if (swdiag_list_find(domain->reachable, 
                                        (void*)(uintptr_t)parent->domain)) {
                /*
                 * This non child and non parent domain can reach our parent
                 * which means that it can now also reach the child.
                 */
                swdiag_list_add(domain->reachable, (void*)(uintptr_t)child->domain);
            }
            
            domain = domain->next;
        }

    }

    /*
     * No loops detected, so connect the parent and child together.
     */
    swdiag_list_add(parent->child_depend, child);
    swdiag_list_add(child->parent_depend, parent);

    /*
     * Check whether the parent or child are within a component, and
     * if so update that components top_depend and bottom_depend as
     * appropriate. 
     *
     * If either parent or child are in different components then there
     * will be no change.
     */
    if (parent->parent_comp && (parent->parent_comp == child->parent_comp)) {

        comp = parent->parent_comp;

        if (comp && swdiag_obj_validate(comp->obj, OBJ_TYPE_COMP)) {
            /*
             * child can no longer be in the top_depend, and the
             * parent can no longer be in the bottom.
             */
            swdiag_list_remove(comp->top_depend, child);
            swdiag_list_remove(comp->bottom_depend, parent);
        }
    }

    swdiag_trace(parent->i.name, 
                 "Connected '%s(%d)' to '%s(%d)'", parent->i.name,
                 parent->domain, child->i.name, child->domain);
    swdiag_obj_db_unlock();
}

/*
 * rci_schedule_dependent_rules_guts()
 *
 * We are applied to all children of a rule, and need to determine 
 * whether to retest any rules and/or set the root cause candidate
 * flag.
 *
 * The return value lets the caller know whether or not we have
 * determined that its a root cause candidate.
 */
static boolean rci_schedule_dependent_rules_guts (obj_instance_t *rule_instance, 
                                                  void *context)
{
    boolean mark_rule_rc_candidate = TRUE;

    if (rule_instance->last_result == SWDIAG_RESULT_PASS) {
        if (rule_instance->root_cause != RULE_ROOT_CAUSE_CANDIDATE) {
            /*
             * This child passed the last time it ran, and it isn't
             * currently a root cause recurse downwards through our
             * dependencies to mark and schedule for resesting all of 
             * those that are currently passing.
             */
            mark_rule_rc_candidate = rci_schedule_dependent_rules(rule_instance);

            /*
             * Ideally we would now check if any of the children are
             * already the root cause, in which case we don't need
             * to bother scheduling the tests for this rule. However
             * determining that has a cost, so for now I'll simply
             * retest all rules that are currently passing and
             * are not currently a root cause candidate.
             *
             * If there are no runnable tests for this rule then
             * this can't be a RCC at the moment.
             */
            swdiag_debug(rule_instance->obj->i.name,
                         "RCI: Rule '%s' is passing and is not current a RCC, so schedule dependent tests",
                         swdiag_obj_instance_name(rule_instance));

            swdiag_sched_rule_immediate(rule_instance);
        }
    } else {
        /*
         * Child is failing, therefore the parent can't be a root
         * cause candidate. We would have recursed downwards past 
         * this failure when it failed, so no need to retest objects
         * under this point right now.
         */
        mark_rule_rc_candidate = FALSE;
    }

    swdiag_debug(rule_instance->obj->i.name,
                 "RCI: Map evaluated '%s' for root cause, %s", 
                 swdiag_obj_instance_name(rule_instance), 
                 mark_rule_rc_candidate == TRUE ? "Root Cause Candidate" : "Not");
    return(mark_rule_rc_candidate);
}

/*
 * rci_schedule_dependent_rules()
 *
 * For all of the child rules of this rule schedule any polled tests
 * that feed into those rules.
 */
static boolean rci_schedule_dependent_rules (
    obj_instance_t *rule_instance)
{
    boolean mark_rule_rc_candidate;
    const char *instance_name = NULL;

    if (&rule_instance->obj->i != rule_instance) {
        /*
         * Only use the instance name if this is not the static
         * instance name.
         */
        instance_name = rule_instance->name;
    }

    mark_rule_rc_candidate = rci_map_function(
        rule_instance, 
        instance_name,
        RCI_MAP_CHILDREN,
        rci_schedule_dependent_rules_guts,
        NULL, TRUE, NULL);
    
    if (mark_rule_rc_candidate) {
        rule_instance->root_cause = RULE_ROOT_CAUSE_CANDIDATE;
    } else {
        /*
         * There must be a rule under this one that is failing, no
         * need to retest this rule in this case since it can't
         * possibly be the root cause.
         * 
         * If it is already in the test queue we could remove it?
         */
        rule_instance->root_cause = RULE_ROOT_CAUSE_NOT;
    }

    swdiag_debug(rule_instance->obj->i.name,
                 "RCI: Evaluated '%s' for root cause, %s", 
                 swdiag_obj_instance_name(rule_instance), 
                 mark_rule_rc_candidate == TRUE ? "Root Cause Candidate" : "Not");
    return(mark_rule_rc_candidate);
}

/*
 * rci_apply_propagate_rule_change()
 *
 */
static boolean rci_apply_propagate_rule_change (obj_instance_t *current_rule,
                                                void *context)
{
    rci_propagate_context_t *propagate_context = context;
    obj_instance_t *rule_of_interest;
    swdiag_result_t action;

    rule_of_interest = propagate_context->rule_of_interest;
    action = propagate_context->action;

    swdiag_debug(current_rule->obj->i.name,
                 "%s: checking '%s' for change in '%s' to %s", 
                 __FUNCTION__, 
                 swdiag_obj_instance_name(current_rule),
                 swdiag_obj_instance_name(rule_of_interest),
                 action == SWDIAG_RESULT_PASS ? "Pass" : "Fail");

    if (rci_ut_in_progress) {
        swdiag_list_add(rci_ut_visited_rules, current_rule);
    }

    if (action == SWDIAG_RESULT_PASS) {
        if (current_rule->last_result == SWDIAG_RESULT_FAIL &&
            current_rule->root_cause == RULE_ROOT_CAUSE_NOT) {
            /*
             * A rule under this point has changed from fail to pass,
             * there is a chance that this current_rule will now also
             * pass, so schedule it for retesting.
             *
             * Mark it as a RCC since it may be a new RC now that the
             * RC under it has been cleared.
             *
             * Don't rerun tests for rules that are already an RC since
             * that can cause the action to be executed twice, and anyway
             * they shouldn't be an RC if something was failing beneath
             * them.
             */
            swdiag_debug(current_rule->obj->i.name,
                         "RCI: Rerunning '%s' since an object under it has changed from fail to pass",
                         swdiag_obj_instance_name(current_rule));

            current_rule->root_cause = RULE_ROOT_CAUSE_CANDIDATE;

            if (rci_ut_in_progress) {
                swdiag_list_add(rci_ut_scheduled_rules, 
                                current_rule);
            } else {
                swdiag_sched_rule_immediate(current_rule);
            }
        }
    } else if (action == SWDIAG_RESULT_FAIL) {
        /*
         * The original rule failed, so clear all the root causes above
         * this point as it is now the new root cause. And retest any
         * rules that require it.
         */
        if (current_rule != rule_of_interest) {
            
            if (current_rule->root_cause == RULE_ROOT_CAUSE) {
                swdiag_debug(current_rule->obj->i.name,
                             "RCI: Cleared RC on '%s', found lower RC '%s'",
                             swdiag_obj_instance_name(current_rule),
                             swdiag_obj_instance_name(rule_of_interest));
                current_rule->root_cause = RULE_ROOT_CAUSE_NOT;
            }

            if (current_rule->last_result == SWDIAG_RESULT_PASS) {
                /*
                 * This rule is passing, and yet one of its dependents
                 * has failed. There is a good chance that this rule
                 * needs to be retested and it will fail as well.
                 */
                swdiag_debug(current_rule->obj->i.name,
                             "RCI: Rerunning '%s' since a RC under it is failing",
                             swdiag_obj_instance_name(current_rule)); 

                if (rci_ut_in_progress) {
                    swdiag_list_add(rci_ut_scheduled_rules, 
                                    current_rule);
                } else {
                    swdiag_sched_rule_immediate(current_rule);
                }
            } else {
                /*
                 * The current rule is failing, as is expected since
                 * it is above another one that is failing. Therefore
                 * this rule also can't be a root cause candidate since
                 * one of its children is failing.
                 *
                 * Should we yank any pending tests from the queue?
                 *
                 * The root cause candidate flag will be cleared
                 * anyway when this test fails, so leave it in place
                 * else we will simply start a new RCI session.
                 */
            }

        }
    }

    return(TRUE);
}

/*
 * rci_propagate_rule_change()
 *
 * Given a change in the status of a rule propagate any changes due
 * to that status change through the dependencies clearing root causes
 * where appropriate.
 */
static void rci_propagate_rule_change (obj_instance_t *rule_of_interest,
                                       swdiag_result_t action)
{
    rci_propagate_context_t *context = NULL;
    const char *instance_name = NULL;

    if (swdiag_obj_is_member_instance(rule_of_interest)) {
        instance_name = rule_of_interest->name;
    }

    context = malloc(sizeof(rci_propagate_context_t));
    
    if (context) {
        context->rule_of_interest = rule_of_interest;
        context->action = action;
        (void)rci_map_function(
            rule_of_interest, 
            instance_name,
            RCI_MAP_PARENTS,
            rci_apply_propagate_rule_change, 
            NULL, TRUE, context);
        free(context);
    }
}

/*
 * swdiag_rci_ut_propagate_rule_change()
 *
 * UT harness to test the propagation of a pass/fail through an
 * object dependency tree.
 *
 * Uses the boolean "rci_ut_in_progress" to block the calls
 * to the schedular.
 */
void swdiag_rci_ut_propagate_rule_change(
    obj_instance_t *rule_of_interest,
    swdiag_result_t action,
    swdiag_list_t *visited_rules,
    swdiag_list_t *scheduled_rules)
{
    swdiag_debug(NULL,
                 "%s: Starting UT for '%s' action=%s", 
                 __FUNCTION__, rule_of_interest->name, 
                 action == SWDIAG_RESULT_PASS ? "Pass" : "Fail");
    rci_ut_in_progress = TRUE;

    rci_ut_visited_rules = visited_rules;
    rci_ut_scheduled_rules = scheduled_rules;
    
    rci_propagate_rule_change(rule_of_interest, action);

    rci_ut_in_progress = FALSE;
    rci_ut_visited_rules = NULL;
    rci_ut_scheduled_rules = NULL;
}

/*
 * rci_is_passed()
 *
 * Function used for the map function to return TRUE if this rule
 * is passing (if the rule is not enabled then it counts as passing).
 */
static boolean rci_is_passed (obj_instance_t *rule_instance, void *context)
{
    if (rci_ut_in_progress) {
        swdiag_list_add(rci_ut_visited_rules, rule_instance);
    }

    if (rule_instance->last_result == SWDIAG_RESULT_PASS || 
        rule_instance->state != OBJ_STATE_ENABLED) {
        swdiag_debug(rule_instance->obj->i.name, "%s: %s passed", __FUNCTION__, swdiag_obj_instance_name(rule_instance));
        return(TRUE);
    } else {   
        swdiag_debug(rule_instance->obj->i.name, "%s: %s failed", __FUNCTION__, swdiag_obj_instance_name(rule_instance));
        return(FALSE);
    }
}

/*
 * rci_is_enabled()
 *
 * Is this instance is enabled
 */
static boolean rci_is_enabled (obj_instance_t *rule_instance, 
                               void *context)
{
    return(rule_instance->state == OBJ_STATE_ENABLED);
}

void swdiag_rci_ut_map_is_passed(obj_instance_t *rule_instance,
                                 const char *instance_name,
                                 swdiag_list_t *visited_rules)
{
    swdiag_debug(NULL, 
                 "%s: Starting UT for '%s'", 
                 __FUNCTION__, rule_instance->name);

    rci_ut_in_progress = TRUE;
    rci_ut_visited_rules = visited_rules;

    rci_map_function(rule_instance, 
                     instance_name, 
                     RCI_MAP_CHILDREN,
                     rci_is_passed, 
                     NULL, TRUE, NULL);

    rci_ut_in_progress = FALSE;
    rci_ut_visited_rules = NULL;
}

/*
 * rci_not_rcc()
 *
 * Function used for the map function to return TRUE if this rule
 * is not a root cause candidate or state is not enabled (since RCC
 * doesn't count for them).
 */
static boolean rci_not_rcc (obj_instance_t *rule_instance, void *context)
{
    if (rule_instance->root_cause != RULE_ROOT_CAUSE_CANDIDATE ||
        rule_instance->state != OBJ_STATE_ENABLED) {
        swdiag_debug(rule_instance->obj->i.name, "%s: %s not RCC", __FUNCTION__, swdiag_obj_instance_name(rule_instance));
        return(TRUE);
    } else {
        swdiag_debug(rule_instance->obj->i.name, "%s: %s is RCC", __FUNCTION__, swdiag_obj_instance_name(rule_instance));
        return(FALSE);
    }
}

/*
 * rci_map_function()
 *
 * Map the function "function" across all the rules in "dependencies", 
 * expanding components where necessary. Return the "default_state" if
 * "function" returned "default_state" for all objects, else !default_state
 *
 *
 * When expanding components use either the top or bottom depend lists
 * as the immediate neighbors. 
 *
 * Recurse through components until all the rules have been 
 * located. I.e. if a rule dependency contains a component, and that
 * component contains another component on its boundary, then we recurse
 * into that second component to find its rules.
 *
 * Where objects have an instance, only apply this function to the
 * matching instance, where no instance, apply to all the instances.
 */
static boolean rci_map_function (obj_instance_t *instance,
                                 const char *instance_name,
                                 rci_map_direction_t direction,
                                 rci_map_function_t function,
                                 swdiag_list_t *history,
                                 boolean default_state,
                                 void *context)
{
    swdiag_list_t *dependencies = NULL;
    swdiag_list_element_t *current;
    obj_t *element_obj;
    obj_t *obj;
    obj_comp_t *parent_comp;
    obj_instance_t *rule_instance;
    boolean retval = default_state;
    boolean free_history = FALSE;
    
    if (instance && instance->state != OBJ_STATE_ENABLED) {
        /*
         * Only traverse enabled objects
         */
        swdiag_debug(instance->obj->i.name, "RCI: %s: '%s' not enabled, skipping",
                     __FUNCTION__, swdiag_obj_instance_name(instance));
        return(retval);
    }

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_ANY)) {
        swdiag_error("Root Cause Identification aborted due to invalid object");
        return(retval);
    }

    //swdiag_debug("RCI: mapping fn across object instance '%s'", 
    //             swdiag_obj_instance_name(instance));

    if (!history) {
        /*
         * First call, allocate some history so that we can avoid 
         * calling function for objects more than once when there 
         * are multiple paths through the dependencies.
         */
        history = swdiag_list_create();
        free_history = TRUE;
    }

    obj = instance->obj;

    switch (direction) {
    case RCI_MAP_PARENTS:
        switch(obj->type) {
        case OBJ_TYPE_RULE:
        case OBJ_TYPE_NONE:
            dependencies = obj->parent_depend;
            break;
        case OBJ_TYPE_COMP:
            dependencies = obj->t.comp->bottom_depend;
            break;
        default:
            swdiag_error("Internal error, found unexpected obj type "
                         "in dependencies");
			break;
        }
        break;
    case RCI_MAP_COMP_PARENTS:
        /*
         * Use the components dependencies.
         */
        switch(obj->type) {
        case OBJ_TYPE_COMP:
            dependencies = obj->parent_depend;
            direction = RCI_MAP_PARENTS;
            break;
        default:
            swdiag_error("Internal error, found unexpected obj type "
                         "in dependencies");
			break;
        }
        break;
    case RCI_MAP_CHILDREN:
        switch(obj->type) {
        case OBJ_TYPE_RULE:
        case OBJ_TYPE_NONE:
            dependencies = obj->child_depend;
            break;
        case OBJ_TYPE_COMP:
            dependencies = obj->t.comp->top_depend;
            break;
        default:
            swdiag_error("Internal error, found unexpected obj type "
                         "in dependencies");
			break;
        }
        break;
    case RCI_MAP_COMP_CHILDREN:
        /*
         * Use the components dependencies.
         */
        switch(obj->type) {
        case OBJ_TYPE_COMP:
            dependencies = obj->child_depend;
            direction = RCI_MAP_CHILDREN;
            break;
        default:
            swdiag_error("Internal error, found unexpected obj type "
                         "in dependencies");
			break;
        }
        break;
    }

    /*
     * No more dependencies, start unwinding the recursion
     */
    if (!dependencies) {
        if (free_history) {
            swdiag_list_free(history);
        }
        return(!retval);
    }

    /*
     * If this object was in its parent components top dependencies 
     * then we must also map the function to the parent_depend of 
     * the component itself as well since that is one level
     * above all top depends.
     *
     * i.e.
     *
     * Rule1     Rule2       Rule3
     *    \______|             |
     *       ___Comp1__        |  
     *       |         |       |
     *       |  Rule4--|-------|
     *       |         |
     *       -----------
     *
     * Where Rule1 and RUle2 both depend on Comp1, and Comp1 contains
     * Rule4. Rule3 has a dependency on Rule4 directly.
     *
     * So when this this mapping function is applied to Rule4 and we
     * are being applied to its parents, it should be applied to Rule3
     * first since it is a direct parent of the Rule4. Then since Rule4
     * is in the top_depend of Comp1 it should also be applied to Rule1
     * and Rule2 which are parents of Comp1.
     */
    if (direction == RCI_MAP_PARENTS) {
        parent_comp = obj->parent_comp;
        if (parent_comp && swdiag_list_find(parent_comp->top_depend, obj)) {
            /*
             * This object is in the top depend list of its component,
             * so apply it to the parent depend of that component.
             */
            if (default_state != rci_map_function(&parent_comp->obj->i, 
                                                  instance_name, 
                                                  RCI_MAP_COMP_PARENTS, 
                                                  function, 
                                                  history,
                                                  default_state,
                                                  context)) {
                /*
                 * Rewrite the retval if any of these objects returned
                 * the opposite of default_state.
                 */
                retval = !default_state;
            }
        }
    }

    if (direction == RCI_MAP_CHILDREN) {
        parent_comp = obj->parent_comp;
        if (parent_comp && swdiag_list_find(parent_comp->bottom_depend, obj)) {
            /*
             * This object is in the bootom depend list of its 
             * component, so apply it to the child depend of
             * that component.
             */
            if (default_state != rci_map_function(&parent_comp->obj->i, 
                                                  instance_name, 
                                                  RCI_MAP_COMP_CHILDREN, 
                                                  function, 
                                                  history,
                                                  default_state,
                                                  context)) {
                retval = !default_state;
            }
        }
    }

    /*
     * Walk the dependencies applying function to them.
     */
    current = dependencies->head;
    while (current) {
        element_obj = current->data;

        if (!swdiag_list_find(history, element_obj)) {
            /*
             * Keep track of where we have been to prevent going 
             * the same way more than once in the same traversal, which
             * can be caused by dependencies that diverge and then join 
             * again.
             */
            swdiag_list_add(history, element_obj);

            if (element_obj->type == OBJ_TYPE_RULE) {
                /*
                 * If an instance was specified AND if the rule
                 * contains instances, then apply to that one else
                 * apply to *all* instances on the object.
                 */
                if (instance_name && element_obj->i.next) {
                    rule_instance = swdiag_obj_instance_by_name(element_obj, 
                                                                instance_name);
                    swdiag_debug(rule_instance->obj->i.name,
                                 "RCI: Looking for instance '%s', got %p",
                                 instance_name, rule_instance);
                    
                    if (rule_instance && 
                        rule_instance->state == OBJ_STATE_ENABLED &&
                        default_state != function(rule_instance, context)) {
                        retval = !default_state;
                    }
                } else {
                    /*
                     * Apply to all instances
                     */
                    for (rule_instance = &element_obj->i;
                         rule_instance;
                         rule_instance = rule_instance->next) {
                        if (rule_instance->state == OBJ_STATE_ENABLED &&
                            default_state != function(rule_instance, context)) {
                            retval = !default_state;
                        }
                    }
                }
            }
            
            /*
             * Continue walking dependencies, expanding components
             * as they are found.
             */
            if (default_state != rci_map_function(&element_obj->i, 
                                                  instance_name, 
                                                  direction, 
                                                  function, 
                                                  history,
                                                  default_state,
                                                  context)) {
                retval = !default_state;
            }
        }
        current = current->next;
    }

    if (free_history) {
        swdiag_list_free(history);
    }

    return(retval);
}

/*
 * rci_determine_if_root_cause()
 *
 * Evaluate this rules children to figure out whether this rule ought
 * to be the root cause.
 */
static boolean rci_determine_if_root_cause (
    obj_instance_t *rule_instance, 
    void *context)
{
    const char *instance_name = NULL;

    swdiag_debug(rule_instance->obj->i.name, 
                 "Determine if root cause for %s",
                 swdiag_obj_instance_name(rule_instance));

    if (rule_instance->state != OBJ_STATE_ENABLED) {
        /*
         * Ignore objects that are not enabled.
         */
        return(TRUE);
    }

    if (rule_instance->root_cause == RULE_ROOT_CAUSE_CANDIDATE &&
        rule_instance->last_result == SWDIAG_RESULT_FAIL) {
        swdiag_trace(rule_instance->obj->i.name,
                     "'%s' is a RCC and is failing.",
                     swdiag_obj_instance_name(rule_instance) );
        /*
         * If all of the child rules are passing and none are marked
         * as root cause candidates (i.e. awaiting retest) then this
         * rule is the root cause.
         */
        if (&rule_instance->obj->i != rule_instance) {
            /*
             * Only use the instance name if this is not the static
             * instance name.
             */
            instance_name = rule_instance->name;
        }
        if (rci_map_function(rule_instance, 
                             instance_name, 
                             RCI_MAP_CHILDREN,
                             rci_is_passed, 
                             NULL, TRUE, NULL)) {
            
            swdiag_trace(rule_instance->obj->i.name,
                         "RCI: All children of '%s' passing", 
                         swdiag_obj_instance_name(rule_instance));
            /*
             * All children pass
             */
            if (rci_map_function(rule_instance, 
                                 instance_name,
                                 RCI_MAP_CHILDREN,
                                 rci_not_rcc, 
                                 NULL, TRUE, NULL)) {
                /*
                 * No children are root cause candidates.. so this is
                 * the root cause, let the sequencer know so that
                 * recovery actions may be run.
                 */
                rule_instance->root_cause = RULE_ROOT_CAUSE;
                rci_propagate_rule_change(rule_instance, 
                                          SWDIAG_RESULT_FAIL); 
                /*
                 * FOUND ROOT CAUSE
                 */
                swdiag_trace(rule_instance->obj->i.name,
                             "RCI: Root Cause '%s'", 
                             swdiag_obj_instance_name(rule_instance)); 
                swdiag_seq_from_root_cause(rule_instance); 
            } else {
                swdiag_trace(rule_instance->obj->i.name,
                             "RCI: Some children of '%s' are still RCC, results pending, wait.", 
                             swdiag_obj_instance_name(rule_instance));
            }
        } else {
            /*
             * One of the children are failing, so I can't be the 
             * root cause, so am no longer a candidate.
             */
            rule_instance->root_cause = RULE_ROOT_CAUSE_NOT;
            swdiag_trace(rule_instance->obj->i.name,
                         "RCI: Some children of '%s' are failing, clearing RCC", 
                         swdiag_obj_instance_name(rule_instance));
        }
    }
    return(TRUE);
}

/*
 * rci_handle_passed_rule_status_report()
 *
 * A rule has passed, check whether we care, and whether it impacts
 * any prior root cause failure.
 */
static void rci_handle_passed_rule_status_report (
    obj_instance_t *rule_instance,
    boolean change_occurred)
{
    const char *instance_name = NULL;

    if (&rule_instance->obj->i != rule_instance) {
        /*
         * Only use the instance name if this is not the static
         * instance name.
         */
        instance_name = rule_instance->name;
    }
    
    if (rule_instance->root_cause == RULE_ROOT_CAUSE_CANDIDATE ||
        rule_instance->root_cause == RULE_ROOT_CAUSE) {
        /*
         * Can't be a candidate or a root cause if we are passing
         */
        rule_instance->root_cause = RULE_ROOT_CAUSE_NOT;
        swdiag_trace(rule_instance->obj->i.name,
                     "RCI: '%s' is passing, was RC/RCC, clearing RC/RCC", 
                     swdiag_obj_instance_name(rule_instance));
    }

    /*
     * Are any of our parents the root cause?
     */
    (void)rci_map_function(rule_instance, 
                           instance_name,
                           RCI_MAP_PARENTS, 
                           rci_determine_if_root_cause, 
                           NULL, TRUE, NULL);

    if (change_occurred) {
        /*
         * This rule changed from failed to passed, propagate this
         * change through the dependencies clearing any root causes
         * and rescheduling tests where appropriate.
         */
        rci_propagate_rule_change(rule_instance, 
                                  SWDIAG_RESULT_PASS);
    }
}

/*
 * rci_handle_failed_rule_status_report()
 *
 * This rule has failed, schedule all rules to be reevaluated under this
 * rule and/or update the root cause candidates.
 */
static void rci_handle_failed_rule_status_report (
    obj_instance_t *rule_instance,
    boolean change_occurred)
{
    if (rule_instance->root_cause == RULE_ROOT_CAUSE) {
        /*
         * Got a failure for an existing root cause, nothing to do
         * since it is already the root cause.
         */
        swdiag_debug(rule_instance->obj->i.name,
                     "RCI: Rule '%s' is already a root cause, ignore failure notification",
                     swdiag_obj_instance_name(rule_instance));  
    } else if (rule_instance->root_cause == RULE_ROOT_CAUSE_CANDIDATE) {
        /*
         * This rule was already a candidate for root cause, check 
         * whether it is the actual root cause.
         */
        rci_determine_if_root_cause(rule_instance, NULL);
    } else if (rci_map_function(rule_instance, 
                                NULL, 
                                RCI_MAP_CHILDREN,
                                rci_is_enabled, 
                                NULL, FALSE, NULL)) {
        /*
         * There are child dependencies and this rule was not taking
         * part of a prior root cause identification (else it would have
         * been a root cause candidate). So schedule all child rules to
         * have their tests rerun to see whether they are a root cause.
         */
        swdiag_debug(rule_instance->obj->i.name,
                     "RCI: Rule failing, has children, start new RCI for '%s'",
                     swdiag_obj_instance_name(rule_instance));
        (void)rci_schedule_dependent_rules(rule_instance);
    } else {
        /*
         * This rule failed, and it had no children, it must be the root 
         * cause itself.
         */
        swdiag_debug(rule_instance->obj->i.name,
                     "RCI: Root Cause '%s' due to failing with no children", 
                     swdiag_obj_instance_name(rule_instance)); 

        rule_instance->root_cause = RULE_ROOT_CAUSE;
        rci_propagate_rule_change(rule_instance, 
                                  SWDIAG_RESULT_FAIL); 
        swdiag_seq_from_root_cause(rule_instance); 
    }
}


/*
 * swdiag_rci_run()
 *
 * This "rule" has passed or failed (for the first time), run through 
 * the root cause identification.
 *
 * If the rule has passed then we are interested in knowing whether it
 * was an existing Root Cause Candidate, if not then it is of no further
 * interest to us.
 *
 * If the rule has failed, then we either start a new root cause 
 * identification or continue an existing one based on whether this
 * rule was already a root cause candidate.
 *
 * If the rule aborted and was a root cause candidate then we must 
 * reschedule it else it will hang RCI. Should it abort more than 
 * twice then we will consider.
 */
void swdiag_rci_run (obj_instance_t *rule_instance, swdiag_result_t result)
{
    boolean change_occurred;

    if (!rule_instance || 
        !swdiag_obj_validate(rule_instance->obj, OBJ_TYPE_RULE)) {
        /*
         * Don't touch this rule - it's invalid.
         */
        return;
    }

    /*
     * Loop detector, try to detect and break out of loops caused
     * by bugs in root cause identification.
     *
     * TODO.
     */

    if (result == SWDIAG_RESULT_ABORT) {
        if (rule_instance->last_result_count > 3) {
            /*
             * Continuous aborts. If this rule was a RCC then
             * think of it as passed so that we can continue with
             * RCI - else we could be blocking finding the real
             * culprit.
             */
            if (rule_instance->root_cause == RULE_ROOT_CAUSE_CANDIDATE) {
                change_occurred = TRUE;
                rci_handle_passed_rule_status_report(rule_instance, 
                                                     change_occurred);
            }
            return;
        }
        /*
         * Reschedule all tests that feed into this rule to prevent
         * a deadlock in the RCI.
         */
        if (rule_instance->root_cause == RULE_ROOT_CAUSE_CANDIDATE) {
            swdiag_debug(rule_instance->obj->i.name,
                         "Requesting rerun of aborting test '%s' to avoid Root Cause deadlock", swdiag_obj_instance_name(rule_instance));
            swdiag_sched_rule_immediate(rule_instance);
        }
        return;
    }

    if (rule_instance->last_result_count == 1) {
        /*
         * First time we have had this result, so it is a change.
         */
        change_occurred = TRUE;
    } else {
        change_occurred = FALSE;
    }

    /*
     * Only interested in changes in state or if this rule is currently
     * a root cause candidate.
     */
    if (change_occurred || 
        rule_instance->root_cause == RULE_ROOT_CAUSE_CANDIDATE) {
        if (result == SWDIAG_RESULT_PASS) {
            rci_handle_passed_rule_status_report(rule_instance, 
                                                 change_occurred);
        } else {
            rci_handle_failed_rule_status_report(rule_instance, 
                                                 change_occurred);
        }
    }
}

/*
 * This object is being deleted, removing it may affect which object
 * is a root cause.
 */
void swdiag_rci_rule_deleted (obj_instance_t *rule_instance)
{
    const char *instance_name = NULL;

    if (!rule_instance) {
        return;
    }

    if (swdiag_obj_is_member_instance(rule_instance)) {
        instance_name = rule_instance->name;
    }
    
    if (rule_instance->root_cause == RULE_ROOT_CAUSE ||
        /*
         * deleting the current root cause means that we should reevaluate
         * any parents of this object which may now become the new root 
         * cause.
         */

        rule_instance->root_cause == RULE_ROOT_CAUSE_CANDIDATE ||
        /*
         * The result from this rule could be what we are waiting for 
         * to determine a root cause. Reevaluate the dependency tree
         * from our parent dependency.
         */
        rule_instance->last_result == SWDIAG_RESULT_FAIL) {
        /*
         * Deleting an object which is failing can affect RCI since it
         * fragments the dependency tree if deleted from the middle we 
         * could get an additional root cause.
         */ 
        (void)rci_map_function(rule_instance, 
                               instance_name,
                               RCI_MAP_PARENTS, 
                               rci_determine_if_root_cause, 
                               NULL, TRUE, NULL);
    }
}
