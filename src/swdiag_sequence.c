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

#include "swdiag_obj.h"
#include "swdiag_sequence.h"
#include "swdiag_trace.h"
#include "swdiag_util.h"
#include "swdiag_thread.h"
#include "swdiag_sched.h"
#include "swdiag_rci.h"

/*
 * seq_event_t
 *
 * Sequencer events showing where in the sequencer we are.
 */
typedef enum seq_event_t_ {
    SEQ_TEST_RUN = 1,
    SEQ_TEST_RESULT,
    SEQ_TEST_RESULT_RCI,
    SEQ_RULE_PROCESS_INPUT,
    SEQ_RULE_RUN,
    SEQ_RULE_RUN_RCI,
    SEQ_RULE_RESULT,
    SEQ_RCI_RUN,
    SEQ_RULE_ROOT_CAUSE,
    SEQ_ACTION_RUN,
    SEQ_ACTION_RESULT,
} seq_event_t;

/*
 * seq_thread_context
 *
 * All that the sequencer needs in order to run within a thread.
 */
typedef struct {
    obj_instance_t *instance;
    seq_event_t event;
    swdiag_result_t result;
    long value;
} seq_thread_context_t;

static swdiag_list_t *free_seq_contexts = NULL;

static unsigned char *bitvalues = NULL;

/*
 * bit_count()
 *
 * Fast lookup of how many bits are set in "data". We do this by generating
 * an array of the counts for all values in a byte, that allows us to do
 * an array lookup rather than counting the bits every time.
 *
 * i.e. tradeoff memory for performance.
 */
static unsigned char bit_count (unsigned char data)
{
    unsigned char count = 0;
    int i, j;

    if (!bitvalues) {
        unsigned char val;
        bitvalues = (unsigned char*)malloc(256 * sizeof(unsigned char));
        if (bitvalues) {
            for(j = 0; j <= 255; j++) {
                val = j;
                count = 0;
                for(i = 0; i < 8; i++) {
                    if (val & 1) count++;
                    val = val >> 1;
                }
                bitvalues[j] = count;
            }
        }
    }

    if (bitvalues) {
        return(bitvalues[data]);
    } else {
        return(0);
    }
}

static void seq_comp_health (obj_comp_t *comp, int health_delta)
{
    int increment;
    swdiag_list_element_t *element;
    obj_t *obj;
    int health = 1000;

    /*
     * Calculate the health of the component by taking 1000 and
     * then taking away the severity of all the failing rules
     * and components that are a member of it.
     */
    obj = swdiag_obj_get_first_rel(comp->obj, OBJ_REL_RULE);

    while(obj) {
        if (swdiag_obj_validate(obj, OBJ_TYPE_RULE) &&
            obj->i.state == OBJ_STATE_ENABLED &&
            obj->i.last_result == SWDIAG_RESULT_FAIL) {
            health -= obj->t.rule->severity;
        }
        obj = swdiag_obj_get_next_rel(obj, OBJ_REL_NEXT_IN_COMP);
    }

    obj = swdiag_obj_get_first_rel(comp->obj, OBJ_REL_COMP);

    while(obj) {
        if (swdiag_obj_validate(obj, OBJ_TYPE_COMP) &&
            obj->i.state == OBJ_STATE_ENABLED) {
            health -= (1000 - obj->t.comp->health);
        }
        obj = swdiag_obj_get_next_rel(obj, OBJ_REL_NEXT_IN_COMP);
    }

    comp->health = health;

    /*
     * Confidence is always as low as the health, and then rises
     * slowly after that with consistent test passes (over one hour).
     */
    if (comp->health < comp->confidence) {
        comp->confidence = (comp->health > 0 ? comp->health : 0);
    } else if (comp->health > comp->confidence) {
        /*
         * And now take away any lack of confidence that the sub-components
         * are having.
         * Calculating the increment to be enough to close the gap over one hour
         * assuming that there is at least one test in there that is in the fast
         * queue. If there are more than one test or rules then this will close
         * the gap quicker.
         *
         * TODO Need a better way of handling this.
         */   
        increment = (comp->health - comp->confidence) / (3600/(SWDIAG_PERIOD_FAST/1000));
        if (increment == 0) {
            increment = 1;
        }
        comp->confidence += increment;

        obj = swdiag_obj_get_first_rel(comp->obj, OBJ_REL_COMP);

        while(obj) {
            if (swdiag_obj_validate(obj, OBJ_TYPE_COMP)) {
                if (obj->i.state == OBJ_STATE_ENABLED &&
                    obj->t.comp->confidence < comp->confidence) {
                    /*
                     * Only as confident as our lowest member confidence
                     */
                    comp->confidence = obj->t.comp->confidence;
                }
            }
            obj = swdiag_obj_get_next_rel(obj, OBJ_REL_NEXT_IN_COMP);
        }
    }
    
    if (comp->confidence > 1000) {
        comp->confidence = 1000;
    }

    switch(-health_delta) {
    case SWDIAG_SEVERITY_CATASTROPHIC:
        comp->catastrophic++;
        break;
    case SWDIAG_SEVERITY_CRITICAL:
        comp->critical++;
        break;
    case SWDIAG_SEVERITY_HIGH:
        comp->high++;
        break;
    case SWDIAG_SEVERITY_MEDIUM:
        comp->medium++;
        break;
    case SWDIAG_SEVERITY_LOW:
        comp->low++;
        break;
    case SWDIAG_SEVERITY_NONE:
        /*
         * No severity - no stats.
         */
        break;
    case SWDIAG_SEVERITY_POSITIVE:
        comp->positive++;
        break;
    default:
        /*
         * Custom sevrity - no stats to update for them.
         */
        break;
    }

    swdiag_debug(comp->obj->i.name,
                 "Set Health on %s to %d (change %d)", 
                 comp->obj->i.name,
                 comp->health, health_delta);

    /*
     * Notify interested tests of the health change
     */
    if (health_delta != 0) {
        for (element = comp->interested_test_objs->head;
             element != NULL;
             element = element->next) {
            obj = element->data;
            if (swdiag_obj_validate(obj, OBJ_TYPE_TEST)) {
                swdiag_seq_from_test_notify(&obj->i, SWDIAG_RESULT_VALUE, 
                                            comp->health);
            }
        }
        /* Notify result to interested clients */
        /* TODO. To be added once component health notification is supported */
        swdiag_xos_notify_component_health(comp->obj->i.name, comp->health);
    }
                                                                   
    /* apply the change to the parent component in a recursive fashion */
    if (comp->obj->parent_comp) {
        /* later we should normalize the health_delta */
        seq_comp_health(comp->obj->parent_comp, health_delta);
    }
}

/*
 * Allocate and initialise a new history record.
 */
static void seq_new_history_record (obj_stats_t *stats,
                                    swdiag_result_t result, 
                                    long value)
{
    obj_history_t *record = NULL;

    if (stats) {
        if (++stats->history_head == OBJ_HISTORY_SIZE) {
            stats->history_head = 0;
        }
        record = &stats->history[stats->history_head];

        if (record) {
            swdiag_xos_time_set_now(&record->time);
            record->result = result;
            record->count = 1;
            record->value = value;
        } 
    }
    return;
}

/*
 * Update the existing history records count.
 */
static void seq_update_history_record (obj_stats_t *stats,
                                       swdiag_result_t result,
                                       long value)
{
    obj_history_t *record = NULL;

    if (stats) {
        record = &stats->history[stats->history_head];

        if (record->count == 0) {
            /*
             * Special case the first time the history is updated
             */
            seq_new_history_record(stats, result, value);
        } else {
            if (record->result == result) {
                record->count++;
            }
            /*
             * Track the record even for Pass/Fail rules since it
             * is interesting. We track the latest value rather than
             * the first - since that is in general more interesting.
             */
            record->value = value;
        }
    }
}

/*
 * Update the stats for the test, rule or action or component.
 * When rules are run, the result affects the test stats as well.
 * Component statistics are updated in a recursive fashion.
 */
static void seq_stats_update (obj_instance_t *instance, 
                              swdiag_result_t result,
                              long value)
{
    obj_stats_t *stats = &instance->stats;
    obj_stats_t *base_stats = NULL;
    obj_instance_t *base = NULL;
    boolean result_changed = FALSE;
    obj_instance_t *instance_local;

    if (swdiag_obj_is_member_instance(instance)) {
        base = &instance->obj->i;
        base_stats = &base->stats;
    }

    stats->runs++;

    if (base_stats) base_stats->runs++;

    switch (result) {
    case SWDIAG_RESULT_PASS:
        stats->passes++;
        if (base_stats) base_stats->passes++;
        swdiag_debug(instance->obj->i.name, "SEQ: Stats.passes++ for %s", instance->name);
        break;
    case SWDIAG_RESULT_FAIL:
        stats->failures++;
        if (base_stats) base_stats->failures++;
        swdiag_debug(instance->obj->i.name, "SEQ: Stats.failed++ for %s", instance->name);
        break;
    case SWDIAG_RESULT_ABORT:
        stats->aborts++;
        if (base_stats) base_stats->aborts++;
        swdiag_debug(instance->obj->i.name, "SEQ: Stats.aborted++ %s", instance->name);
        break;
    case SWDIAG_RESULT_VALUE:
        /* result incremented from rules */ 
        if (instance->last_value == value) {
            seq_update_history_record(&instance->stats,
                                      result,
                                      value); 
        } else {
            result_changed = TRUE;
            seq_new_history_record(&instance->stats,
                                   result,
                                   value);
        }

        instance->last_value = value;

        break;
    case SWDIAG_RESULT_IN_PROGRESS:
        /* shouldn't be here whilst the test is still in progress */
        swdiag_error("SEQ: Stats.in_progress, should not get here");
        break;
    case SWDIAG_RESULT_INVALID:
    case SWDIAG_RESULT_IGNORE:
    case SWDIAG_RESULT_LAST:
        swdiag_debug(instance->obj->i.name, "SEQ: Stats.invalid! %s", instance->name);
        break;
    }

    if (result == SWDIAG_RESULT_PASS || result == SWDIAG_RESULT_FAIL) {
        if (instance->last_result == result) {
            instance->last_result_count++;
            if (instance->obj->type == OBJ_TYPE_ACTION) {
                /*
                 * Don't collate action records
                 */
                seq_new_history_record(&instance->stats,
                                       result, 
                                       value);
            } else {
                seq_update_history_record(&instance->stats,
                                          result,
                                          value);
            }
        } else {
            result_changed = TRUE;
            instance->last_result = result;
            instance->last_result_count = 1;
            
            seq_new_history_record(&instance->stats,
                                   result, 
                                   value);
        }

        if (base) {
            /*
             * Update the base instance state
             */
            if (base->last_result == result) {
                base->last_result_count++;
            } else {
                /*
                 * Possibly changed state, we'll have to look through
                 * the instances.
                 */
                result_changed = TRUE;
                if (result == SWDIAG_RESULT_FAIL) {
                    base->last_result = result;
                    base->last_result_count = 1;
                } else {
                    /*
                     * A pass when we are failing is only a pass if all
                     * the instances are also not failing.
                     */
                    for (instance_local = base->next; 
                         instance_local != NULL; 
                         instance_local = instance_local->next) {
                        if (instance_local->last_result == SWDIAG_RESULT_FAIL) {
                            break;
                        }
                    }
                    if (!instance_local) {
                        /*
                         * None were failing, so that means all are either
                         * passing or in a state which we will accept as a 
                         * pass.
                         */
                        base->last_result = result;
                        base->last_result_count = 1;
                    }
                }
            }
        }
    }

    /* Notify results to interested clients if they have registered to receive
     * notification.
     */ 
    if (result_changed && (instance->obj->i.flags.any & OBJ_FLAG_NOTIFY)) {
        switch(instance->obj->type) {
        case OBJ_TYPE_TEST:
            if (swdiag_obj_is_member_instance(instance)) {    
                swdiag_xos_notify_test_result(instance->obj->i.name, NULL, 
                    result, value);
            } else {
                swdiag_xos_notify_test_result(instance->obj->i.name, 
                    instance->name, result, value);    
            }    
            break;
        case OBJ_TYPE_RULE:
            if (swdiag_obj_is_member_instance(instance)) {    
                swdiag_xos_notify_rule_result
                    (instance->obj->i.name, NULL, result, value);    
            } else {
                swdiag_xos_notify_rule_result
                    (instance->obj->i.name, instance->name, result, value);    
            }    
            break;
        case OBJ_TYPE_ACTION:
            if (swdiag_obj_is_member_instance(instance)) {    
                swdiag_xos_notify_action_result
                    (instance->obj->i.name, NULL, result, value);    
            } else {
                swdiag_xos_notify_action_result
                    (instance->obj->i.name, instance->name, result, value);    
            }    
            break;
        case OBJ_TYPE_COMP:
        case OBJ_TYPE_NONE:
        case OBJ_TYPE_ANY:
            break;    
        }        
    }    
}

/*
 * Update the statistics for a test, rule or action - it is called for each.
 * In addition all container component stats are updated for rules.
 */
static void seq_result_stats_update (obj_instance_t *instance, 
                                     swdiag_result_t result,
                                     long value)
{
    obj_comp_t *comp;
    obj_t *obj;

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_ANY)) {
        return;
    }

    seq_stats_update(instance, result, value);

    obj = instance->obj;

    /*
     * For rules when we have a real result (pass,fail,abort) update the
     * immediate parent component.
     */
    if (obj->type != OBJ_TYPE_RULE || result == SWDIAG_RESULT_VALUE) {
        return;
    }
    comp = obj->parent_comp;
    if (comp && swdiag_obj_validate(comp->obj, OBJ_TYPE_COMP)) {
        seq_stats_update(&comp->obj->i, result, 0);
    }
}

/*
 * seq_rule_result_on_health()
 *
 * Track health for the component based on the rule results,
 * Note that a rule should only have once instance worth of
 * severity, not multipled by the number of instances that 
 * are failing.
 */
static void seq_rule_result_on_health (obj_instance_t *instance,
                                       swdiag_result_t result)
{
    obj_comp_t *comp;
    obj_rule_t *rule;

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_ANY)) {
        return;
    }

    rule = instance->obj->t.rule;
    comp = instance->obj->parent_comp;

    /*
     * if the test result is the same as the previous test result
     * then there is no change to the health of the component, but
     * there maybe change in the confidence.
     */
    if (instance->last_result_count > 1) {
        seq_comp_health(comp, 0);
        return;
    }

    switch(result) {
    case  SWDIAG_RESULT_PASS:
        /*
         * We've changed from met to not met, so increment the health.
         */
        seq_comp_health(comp, rule->severity);
        break;
    case SWDIAG_RESULT_FAIL:
        /*
         * We've changed from not met, to met the conditions for this 
         * rule, so decrement the health.
         */
        seq_comp_health(comp, -rule->severity);
        break;
    default:
        break;
    }
}

/*
 * Runs the actual test function - releases the DB lock whilst the
 * test is actually running allowing other threads access to the DB.
 */
swdiag_result_t swdiag_seq_test_run (obj_instance_t *instance, long *value)
{
    swdiag_result_t result = SWDIAG_RESULT_INVALID;
    obj_test_t *test;

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_TEST)) {
        swdiag_error("Failed to validate object '%s'", 
                     instance ? instance->name : "unknown");
        return(SWDIAG_RESULT_INVALID);
    } 

    test = instance->obj->t.test;

    if (test->type == OBJ_TEST_TYPE_POLLED) {
        if (test->function) {
            instance->in_use++;
            if (swdiag_obj_is_member_instance(instance)) {
                swdiag_obj_db_unlock();
                result = (test->function)(instance->name, 
                                          instance->context, 
                                          value);
                swdiag_obj_db_lock();
            } else {
                swdiag_obj_db_unlock();
                result = (test->function)(NULL,
                                          instance->context, 
                                          value); 
                swdiag_obj_db_lock();
            }

            // Important that the in_use is only decremented *after*
            // locking the obj DB so that we don't free it before
            // detecting that it has been deleted.
            instance->in_use--;
        } else {
            swdiag_error("No function registered for polled test '%s'",
                         instance->name);
        }
    }

    return (result);
}

static swdiag_result_t seq_rule_run (obj_instance_t *instance,
                                     swdiag_result_t result,
                                     long value)
{
    swdiag_result_t rule_result = SWDIAG_RESULT_PASS;
    obj_rule_t *rule;
    obj_t *obj;
    obj_instance_t *input_instance;
    swdiag_list_element_t *element;

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_RULE)) {
        return(SWDIAG_RESULT_ABORT);
    }

    obj = instance->obj;
    rule = obj->t.rule;

    switch (rule->operator) {
    case SWDIAG_RULE_ON_FAIL:
        /* we only get here if the test returns failure, so trigger rule */
        if (result == SWDIAG_RESULT_FAIL) {
            rule_result = SWDIAG_RESULT_FAIL;
        }
        break;
        
    case SWDIAG_RULE_EQUAL_TO_N:
        if (result == SWDIAG_RESULT_VALUE) {
            if (value == rule->op_n) {
                rule_result = SWDIAG_RESULT_FAIL;
            }
        }
        break;
        
    case SWDIAG_RULE_NOT_EQUAL_TO_N:
        if (result == SWDIAG_RESULT_VALUE) {
            if (value != rule->op_n) {
                rule_result = SWDIAG_RESULT_FAIL;
            }
        }
        break;
        
    case SWDIAG_RULE_LESS_THAN_N: 
        if (result == SWDIAG_RESULT_VALUE) {
            if (value < rule->op_n) {
                rule_result = SWDIAG_RESULT_FAIL;
            }
        }
        break;
        
    case SWDIAG_RULE_GREATER_THAN_N:
        if (result == SWDIAG_RESULT_VALUE) {
            if (value > rule->op_n) {
                rule_result = SWDIAG_RESULT_FAIL;
            }
        }
        break;
    case SWDIAG_RULE_RANGE_N_TO_M:
        /*
         * Pass if inside the range, else fail.
         */
        if (result == SWDIAG_RESULT_VALUE) {
            if (value < rule->op_n ||
                value > rule->op_m) {
                rule_result = SWDIAG_RESULT_FAIL;
            }
        }
        break;

    case SWDIAG_RULE_DISABLE:
        rule_result = SWDIAG_RESULT_ABORT;
        break;

    case SWDIAG_RULE_N_EVER:
        if (result == SWDIAG_RESULT_FAIL) {
            instance->fail_count++;
            if (instance->fail_count >= rule->op_n) {
                rule_result = SWDIAG_RESULT_FAIL;
                instance->fail_count = 0;
            }
        }
        break;
    case SWDIAG_RULE_N_IN_ROW:
        if (result == SWDIAG_RESULT_FAIL) {
            instance->fail_count++;
            if (instance->fail_count >= rule->op_n) {
                rule_result = SWDIAG_RESULT_FAIL;
            }
        } else {
            instance->fail_count = 0;
        }
        break;
    case SWDIAG_RULE_N_IN_M:
        /*
         * For tracking N in M we use 1 bit per rule invocation in a 
         * block of memory sized to 1 bit per invocation. For every 
         * invocation we move our insertion bit along one, which overwrites
         * old values.
         *
         * To evaluate whether the rule passes or fails we simply have to
         * count how many bits are fail (1) versus pass (0), so N is a count
         * of failures (1), and M is the total count.
         */
        if (result != SWDIAG_RESULT_PASS &&
            result != SWDIAG_RESULT_FAIL) {
            /*
             * Not a valid input to an N in M rule, we only
             * want pass or fail;
             */
            swdiag_error("Rule '%s' not pass or fail, got %s, ignoring", 
                         swdiag_obj_instance_name(instance), 
                         swdiag_util_swdiag_result_str(result));
            rule_result = SWDIAG_RESULT_ABORT;
            break;
        }

        if (!instance->rule_data) {
            /*
             * Allocate it - must be the first time through or the
             * rule type was changed.
             */
            instance->rule_data = calloc(1, sizeof(obj_rule_data_t));
            if (instance->rule_data) {
                /*
                 * Allocate the history bits
                 */
                int history_size = (rule->op_m / 8) + 1;
                instance->rule_data->history = calloc(1, history_size);
                instance->rule_data->history_size = history_size;
            }
        }

        if (instance->rule_data) {
            if (instance->rule_data->history) {
                int byte = instance->rule_data->position / 8;
                int bit = instance->rule_data->position % 8;
                uchar mask = (1 << bit);
                long count_n = 0;

                if (byte >= instance->rule_data->history_size) {
                    /*
                     * We'd be off the end of the data, how did that 
                     * happen?
                     */
                    break;
                }

                if (result == SWDIAG_RESULT_PASS) {
                    /*
                     * Clear the bit for a pass
                     */
                    instance->rule_data->history[byte] &= ~mask;
                } else {
                    /*
                     * Set the bit for a fail
                     */
                    instance->rule_data->history[byte] |= mask;
                }
                
                /*
                 * Count the bits, by walking through the history
                 * a byte at a time and looking up how many bits in
                 * each byte (faster than shifting and counting).
                 */
                for(byte = 0; 
                    byte < instance->rule_data->history_size; 
                    byte++) {
                    count_n += bit_count(instance->rule_data->history[byte]);
                }
                if (count_n >= rule->op_n) {
                    rule_result = SWDIAG_RESULT_FAIL;
                }
                     
                instance->rule_data->position++;

                if (instance->rule_data->position >= rule->op_m) {
                    instance->rule_data->position = 0;
                }
                swdiag_debug(instance->obj->i.name, "%s Fail Count = %ld",
                             swdiag_obj_instance_name(instance), count_n);

                
            } else {
                swdiag_error("No rule history data for '%s'",
                             swdiag_obj_instance_name(instance));
                rule_result = SWDIAG_RESULT_ABORT;
            }
        } else {
            swdiag_error("No rule data for '%s'", 
                         swdiag_obj_instance_name(instance));
            rule_result = SWDIAG_RESULT_ABORT;
        }
        break;
    case SWDIAG_RULE_N_IN_TIME_M:
        /*
         * I need to know the time of each entry so that I can expire
         * them.
         */
        swdiag_error("Not supported Rule N in time M yet");
        rule_result = SWDIAG_RESULT_ABORT;
        break;
    case SWDIAG_RULE_FAIL_FOR_TIME_N:
        swdiag_error("Not supported Rule Fail for time N yet");
        rule_result = SWDIAG_RESULT_ABORT;
        break;
    case SWDIAG_RULE_OR:
        /*
         * Find matching input rule instances and if any are failing
         * then we are failing.
         */
        for(element = rule->inputs->head; 
            element != NULL; 
            element = element->next) 
        {
            obj = element->data;
            input_instance = swdiag_obj_instance(obj, instance);

            if (input_instance && 
                input_instance->last_result == SWDIAG_RESULT_FAIL &&
                input_instance->state == OBJ_STATE_ENABLED) {
                /*
                 * Any inputs failing means the whole rule fails
                 */
                rule_result = SWDIAG_RESULT_FAIL;
                break;
            }
        }
        break;
    case SWDIAG_RULE_AND:
        /*
         * Find matching input rule instances and if all are failing
         * then we are failing. An abort counts as a failure.
         */
        rule_result = SWDIAG_RESULT_FAIL;

        for(element = rule->inputs->head; 
            element != NULL; 
            element = element->next) 
        {
            obj = element->data;
            input_instance = swdiag_obj_instance(obj, instance);

            if (input_instance && 
                input_instance->last_result == SWDIAG_RESULT_PASS &&
                input_instance->state == OBJ_STATE_ENABLED) {
                /*
                 * Any inputs passing means the whole rule passes
                 */
                rule_result = SWDIAG_RESULT_PASS;
                break;
            }
        }
        
        break;
    case SWDIAG_RULE_INVALID:
    case SWDIAG_RULE_LAST:
        swdiag_error("Invalid rule type for rule '%s'", 
                     swdiag_obj_instance_name(instance));
        rule_result = SWDIAG_RESULT_FAIL;
        break;
    }

    seq_result_stats_update(instance, rule_result, value);

    /*
     * Update health now that we've updated the stats, health is only
     * calculated on the base instance, not the member instances. The
     * stats update will have updated the base wrt the member instance 
     * state.
     */
    seq_rule_result_on_health(&instance->obj->i, rule_result);

    swdiag_debug(instance->obj->i.name,
                 "Ran rule '%s' result '%s' criteria for (%ld in n:%ld m:%ld)",  
                 swdiag_obj_instance_name(instance),
                 swdiag_util_swdiag_result_str(rule_result), 
                 value, rule->op_n, rule->op_m);

    return (rule_result);
}

/*
 * Runs an action by calling the action function.
 * Actions stats are incremented depending on the result of the action.
 */
static swdiag_result_t seq_action_run (obj_instance_t *instance)
{
    swdiag_result_t result = SWDIAG_RESULT_ABORT;
    obj_action_t *action;

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_ACTION)) {
        return(SWDIAG_RESULT_ABORT);
    }

    action = instance->obj->t.action;

    if (action->function) {
        instance->in_use++;;
        swdiag_obj_db_unlock();
        result = (action->function)(instance->name, instance->context);
        swdiag_obj_db_lock();
        instance->in_use--;
    }

    seq_result_stats_update(instance, result, 0);

    swdiag_trace(instance->obj->i.name, "Action %s %s", 
                 swdiag_obj_instance_name(instance),
                 swdiag_util_swdiag_result_str(result));

    return (result);
}

/*
 * seq_sequencer()
 *
 * Given an object instance and event walk through the sequence of
 * test/rule/action
 */
static void seq_sequencer (obj_instance_t *instance, 
                           seq_event_t event, 
                           swdiag_result_t result,
                           long value)
{ 
    swdiag_result_t rule_result, test_result, action_result;
    swdiag_list_element_t *element;
    obj_t *action_obj, *rule_obj;
    obj_rule_t *rule = NULL;
    obj_test_t *test = NULL;
    obj_action_t *action = NULL;
    obj_instance_t *rule_instance = NULL;
    obj_instance_t *action_instance = NULL;

    rule_result = test_result = action_result = result;

    if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_ANY)) {
        return;
    }
    
    swdiag_debug(instance->obj->i.name, 
                 "SEQ: processing event %d for '%s'", event,
                 swdiag_obj_instance_name(instance));

    switch(event) {
    case SEQ_TEST_RUN:
        if (instance->obj->type != OBJ_TYPE_TEST) {
            return;
        } 
        test = instance->obj->t.test;

        test_result = swdiag_seq_test_run(instance, &value);

        if (!swdiag_obj_instance_validate(instance, OBJ_TYPE_TEST)) {
            /*
             * Whilst the test was running the object may have been 
             * deleted or corrupted, don't look at this object any
             * further.
             */
            return;
        }

        if (test_result == SWDIAG_RESULT_IN_PROGRESS) {
            /*
             * The test will inform us when the result is available,
             * don't do anything now other than start a watchdog timer
             * in case it doesn't get back to us so that we can put
             * the test back onto the schedular.
             *
             * TODO
             */
            swdiag_debug(NULL, "SEQ: TODO: Need a watchdog timer in case test "
                         "doesn't notify us of the result");
            return;
        }
        /* no break */
    case SEQ_TEST_RESULT: 
        swdiag_xos_time_set_now(&instance->sched_test.last_time);
        /* no break */
    case SEQ_TEST_RESULT_RCI:
        if (instance->obj->type != OBJ_TYPE_TEST) {
            swdiag_error("SEQ: Wrong object type to test result");
            return;
        }
        test = instance->obj->t.test;

        if (test_result == SWDIAG_RESULT_IGNORE) {
            /*
             * Test wants us to ignore this result, e.g. a test that
             * works on instances uses this when it is run on a base
             * object instance.
             *
             * Don't update any stats, totally ignore.
             */

            swdiag_debug(instance->obj->i.name,
                         "Test result returned ignored for '%s'",
                         swdiag_obj_instance_name(instance));

            if (test->type == OBJ_TEST_TYPE_POLLED) {
                swdiag_sched_add_test(instance, FALSE);
            }
            return;
        }

        if (event != SEQ_TEST_RESULT_RCI) {
            /*
             * Don't update stats for fake notification results, these 
             * just exist to feed RCI with notification results.
             */
            seq_result_stats_update(instance, test_result, value);
        }

        /*
         * Reschedule test if polled, or autopass is set.
         */
        if (test->type == OBJ_TEST_TYPE_POLLED || 
            test->autopass != AUTOPASS_UNSET) {
            swdiag_sched_add_test(instance, FALSE);
        }

        /*
         * First rule triggering from this test.
         */
        rule = test->rule;
        /* no break */

    case SEQ_RULE_PROCESS_INPUT:
        /*
         * Process the input result sending it to all interested
         * rules. Allows rule->multiple_rule processing.
         */
        if (!rule && instance->obj->type == OBJ_TYPE_RULE) {
            /*
             * Got a rule as input
             */
            rule = instance->obj->t.rule;
        }

        if (!rule) {
            swdiag_debug(instance->obj->i.name,
                         "SEQ: No rule found for instance %s", 
                         swdiag_obj_instance_name(instance));
            break;
        }

        for (; rule; rule = rule->next_in_input) {
            boolean map_instances = FALSE;
            /*
             * apply each rule instance to the result/value.
             *
             * First locate the same instance on the rule as was used
             * for this test, if none found then use the rule instance.
             */
            rule_instance = swdiag_obj_instance(rule->obj, instance);

            if (!rule_instance) {
                swdiag_error("SEQ: No rule instance found for instance %s", 
                             swdiag_obj_instance_name(instance));
                continue;
            }

            /*
             * If the rule_instance is the base instance then map the 
             * result to all instances.
             */
            if (!swdiag_obj_is_member_instance(rule_instance)) {
                map_instances = TRUE;
            }

            while (rule_instance != NULL) {
                if (test_result == SWDIAG_RESULT_ABORT) {
                    /*
                     * An aborted test may be of importance to the root
                     * cause identification, since it could block the
                     * proper identfication of the root cause. Let RCI
                     * know about this abort.
                     */
                    swdiag_rci_run(rule_instance, test_result);
                } else {
                    if (event != SEQ_TEST_RESULT_RCI) {
                        /*
                         * Normal test process the results in a rule
                         */
                        seq_sequencer(rule_instance, SEQ_RULE_RUN, 
                                      test_result, value);
                    } else {
                        /*
                         * This is an RCI run, don't run the actual rule
                         * again since that can stuff up the history.
                         */
                        seq_sequencer(rule_instance, SEQ_RULE_RUN_RCI, 
                                      test_result, value);
                    }
                }
                
                if (map_instances) {
                    rule_instance = rule_instance->next;
                } else {
                    rule_instance = NULL;
                }
            }
        }
        break;
    case SEQ_RULE_RUN_RCI:
        rule_result = instance->last_result;
        /* no break */
    case SEQ_RULE_RUN:
        if (event != SEQ_RULE_RUN_RCI) {
            rule_result = seq_rule_run(instance, test_result, value);
        }
        /* no break */
    case SEQ_RULE_RESULT:
        /*
         * If this rule feeds its results to any other rules kick
         * the processing off for those in the background.
         */
        if (instance->obj->type != OBJ_TYPE_RULE) {
            swdiag_error("SEQ: Wrong object type to rule result");
            return;
        }

        rule = instance->obj->t.rule;

        if (rule_result == SWDIAG_RESULT_PASS && instance->action_run) {
            /*
             * We're passing, so clear action_run since we are OK to
             * run that action again should we need to.
             */
            instance->action_run = FALSE;
        }

        if (rule->output && 
            swdiag_obj_validate(rule->output->obj, OBJ_TYPE_RULE)) {
            rule_instance = swdiag_obj_instance(rule->output->obj, instance);

            if (!rule_instance) {
                swdiag_error("SEQ: No rule instance for instance %s from rule output",
                             swdiag_obj_instance_name(instance));             
                break;
            }

            /*
             * If the rule_instance is the base instance then map the 
             * result to all instances.
             */
            if (!swdiag_obj_is_member_instance(rule_instance)) {
                while (rule_instance != NULL) {
                    seq_sequencer(rule_instance, SEQ_RULE_PROCESS_INPUT, 
                                  rule_result, 0);
                    rule_instance = rule_instance->next;
                }
            } else {
                /*
                 * Found a matching instance on the output, apply 
                 * only to that matching instance.
                 */
                seq_sequencer(rule_instance, SEQ_RULE_PROCESS_INPUT, 
                              rule_result, 0);
            }
        }
        /* no break */
    case SEQ_RCI_RUN:
        /*
         * Pass all rule passes and initial rule failures through to
         * Root Cause Identification. RCI will call us back with the
         * root cause.
         */
        if (instance->obj->type != OBJ_TYPE_RULE) {
            return;
        }
        swdiag_rci_run(instance, rule_result);
        break;
    case SEQ_RULE_ROOT_CAUSE:
        
        if (instance->obj->type != OBJ_TYPE_RULE) {
            return;
        }

        rule = instance->obj->t.rule;

        if (instance->action_run) {
            /*
             * Don't rerun actions just because a rule has been
             * determined a RC when it wasn't before.
             */
            swdiag_debug(instance->obj->i.name,
                         "SEQ: Ignore action for '%s' since already run and the rule hasn't passed since then",
                         swdiag_obj_instance_name(instance));
            return;
        } else {
            instance->action_run = TRUE;
        }

        for(element = rule->action_list->head; 
            element != NULL; 
            element = element->next) 
        {
            action_obj = (obj_t*)element->data;

            /*
             * For each action, find the appropriate instance.
             */
            action_instance = swdiag_obj_instance(action_obj, 
                                                  instance);

            /*
             * Before we lose the context of which rule is triggering
             * the action we need to notify the user of what is going on.
             *
             * Don't inform the user of NO-OP's though!
             */
            if ((action_instance->obj->i.flags.action & OBJ_FLAG_SILENT))
            {
                /*
                 * Ignore silent recovery actions
                 */
            } else {
                swdiag_xos_recovery_in_progress(instance, action_instance);
            }

            seq_sequencer(action_instance, SEQ_ACTION_RUN, 
                          rule_result, 0);
        }
        break;
    case SEQ_ACTION_RUN:
        action_result = seq_action_run(instance);

        if (!swdiag_obj_validate(instance->obj, OBJ_TYPE_ACTION) ||
            instance->state == OBJ_STATE_DELETED) {
            /*
             * Whilst the test was running the object may have been 
             * deleted or corrupted, don't look at this object any
             * further.
             */
            return;
        }

        if (action_result == SWDIAG_RESULT_IN_PROGRESS) {
            /*
             * The action will inform us when the result is available, don't
             * do anything now other than start a watchdog timer.
             *
             * TODO
             */ 
            swdiag_debug(NULL, "SEQ: TODO: Need a watchdog timer in case action "
                    "doesn't notify us of the result");
            return;
        }
        /* no break */
    case SEQ_ACTION_RESULT:  
        /*
         * If the action passed then we should retest the rule to check
         * whether it is now working (assuming it is polled). If it is
         * a notification then hopefully the client will tell us that it
         * has passed soon.
         */
        if (instance->obj->type != OBJ_TYPE_ACTION) {
            return;
        }

        if (action_result == SWDIAG_RESULT_PASS) {
            action = instance->obj->t.action;
            
            /*
             * Don't run rules for built in's, there could be a lot of 
             * those, and most don't apply.
             *
             * Note that at this time these flags are only set on the 
             * base object.
             */
            if (instance->obj->i.flags.action & OBJ_FLAG_BUILT_IN) {
                return;
            }

            if (action->rule_list) {
                for (element = action->rule_list->head;
                     element != NULL;
                     element = element->next)
                {
                    rule_obj = (obj_t*)element->data;
                    rule_instance = swdiag_obj_instance(rule_obj, 
                                                        instance);
                    swdiag_debug(rule_instance->obj->i.name,
                                 "SEQ: Scheduling tests for rule '%s' to confirm action", 
                                 swdiag_obj_instance_name(rule_instance));
                    swdiag_sched_rule_immediate(rule_instance);
                }
            }
        }
        break;
    }
}

/*
 * seq_thread_fn()
 *
 * Thread function, take the context from the thread and extract the
 * necessary information for the sequencer, then kick off the sequencer
 * at the correct point.
 */
static void seq_thread_fn (swdiag_thread_t *thread, void *context_v)
{
    seq_thread_context_t *context = context_v;

    swdiag_obj_db_lock();
    /*
     * The DB is now locked, so the in_use flag can be cleared.
     */
    context->instance->in_use--;

    seq_sequencer(context->instance, context->event, context->result, 
                  context->value);

    if (!free_seq_contexts) {
        free_seq_contexts = swdiag_list_create();
    }
    
    if (free_seq_contexts && 
        free_seq_contexts->num_elements < SEQUENCE_CONTEXT_LOW_WATER) {
        swdiag_list_push(free_seq_contexts, context);
    } else {
        free(context);
    }
    swdiag_obj_db_unlock();
}

/*
 * swdiag_seq_from_test()
 *
 * Kick off the sequencer from the start of a test within a new thread.
 */
void swdiag_seq_from_test (obj_instance_t *instance)
{
    seq_thread_context_t *context = NULL;
    seq_thread_context_t no_memory;

    if (free_seq_contexts) {
        context = swdiag_list_pop(free_seq_contexts);
    }

    if (!context) {
        context = malloc(sizeof(seq_thread_context_t));
    }

    if (!context) {
        context = &no_memory;
    }

    context->instance = instance;
    context->event = SEQ_TEST_RUN;
    context->result = SWDIAG_RESULT_INVALID;
    context->value = 0;
    
    if (context != &no_memory) {
        instance->in_use++;
        swdiag_thread_request(seq_thread_fn, 
                              NULL, 
                              context);
    } else {
        seq_sequencer(context->instance, context->event, context->result, 
                      context->value);
    }
}

/*
 * swdiag_seq_from_test_notify()
 *
 * Kick off of the sequencer from the start of the test results within
 * a new thread.
 */
void swdiag_seq_from_test_notify (obj_instance_t *instance,
                                  swdiag_result_t result,
                                  long value)
{
    seq_thread_context_t *context = NULL;
    seq_thread_context_t no_memory;

    if (free_seq_contexts) {
        context = swdiag_list_pop(free_seq_contexts);
    }

    if (!context) {
        context = malloc(sizeof(seq_thread_context_t));
    }
    
    if (!context) {
        context = &no_memory;
    }

    context->instance = instance;
    context->event = SEQ_TEST_RESULT;
    context->result = result;
    context->value = value;
    
    if (context != &no_memory) {
        instance->in_use++;
        swdiag_thread_request(seq_thread_fn, 
                              NULL, 
                              context);
    } else {
        seq_sequencer(context->instance, context->event, context->result, 
                      context->value);
    }
}

/*
 * swdiag_seq_from_test_notify_rci()
 *
 * Kick off of the sequencer from the start of the test results within
 * a new thread.
 */
void swdiag_seq_from_test_notify_rci (obj_instance_t *instance,
                                      swdiag_result_t result,
                                      long value)
{
    seq_thread_context_t *context = NULL;
    seq_thread_context_t no_memory;

    if (free_seq_contexts) {
        context = swdiag_list_pop(free_seq_contexts);
    }

    if (!context) {
        context = malloc(sizeof(seq_thread_context_t));
    }

    if (!context) {
        context = &no_memory;
    }

    context->instance = instance;
    context->event = SEQ_TEST_RESULT_RCI;
    context->result = result;
    context->value = value;
    
    if (context != &no_memory) {
        instance->in_use++;
        swdiag_thread_request(seq_thread_fn, 
                              NULL, 
                              context);
    } else {
        seq_sequencer(context->instance, context->event, context->result, 
                      context->value);
    }
}

/*
 * swdiag_seq_root_cause()
 *
 * This rule has been identified as the root cause, re-enter the sequencer
 * to process the actions. Do this in a new thread so as to not block
 * the caller (which may be EEM or the RCI code).
 */
void swdiag_seq_from_root_cause (obj_instance_t *instance)
{
    seq_thread_context_t *context = NULL;
    seq_thread_context_t no_memory;

    if (free_seq_contexts) {
        context = swdiag_list_pop(free_seq_contexts);
    }

    if (!context) {
        context = malloc(sizeof(seq_thread_context_t));
    }

    if (!context) {
        context = &no_memory;
    }

    context->instance = instance;
    context->event = SEQ_RULE_ROOT_CAUSE;
        
    if (context != &no_memory) {
        instance->in_use++;
        swdiag_thread_request(seq_thread_fn, 
                              NULL, 
                              context);
    } else {
        seq_sequencer(context->instance, context->event, context->result, 
                      context->value);
    }
}


/*
 * swdiag_seq_from_action_complete()
 *
 * Kick off of the sequencer from the action results
 */
void swdiag_seq_from_action_complete (obj_instance_t *instance,
                                      swdiag_result_t result)
{
    seq_thread_context_t *context = NULL;
    seq_thread_context_t no_memory;

    if (free_seq_contexts) {
        context = swdiag_list_pop(free_seq_contexts);
    }

    if (!context) {
        context = malloc(sizeof(seq_thread_context_t));
    }

    if (!context) {
        context = &no_memory;
    }

    context->instance = instance;
    context->event = SEQ_ACTION_RESULT;
    context->result = result;
    
    if (context != &no_memory) {
        instance->in_use++;
        swdiag_thread_request(seq_thread_fn, 
                              NULL, 
                              context);
    } else {
        seq_sequencer(context->instance, context->event, context->result, 
                      context->value);
    }
}

void swdiag_seq_comp_set_health (obj_comp_t *comp, uint health)
{
    int delta;

    if (!comp) {
        return;
    }

    delta = health - comp->health;

    seq_comp_health(comp, delta);
}

void swdiag_seq_init (void)
{
    int i;
    seq_thread_context_t *context;

    if (!free_seq_contexts) {
        free_seq_contexts = swdiag_list_create();
    }

    if (free_seq_contexts) {
        for (i=0; i < SEQUENCE_CONTEXT_LOW_WATER; i++) {
            context = malloc(sizeof(seq_thread_context_t));
            if (context) {
                swdiag_list_push(free_seq_contexts, context); 
            }
        }
    }
}

void swdiag_seq_terminate (void)
{
	// Free up the contexts
	seq_thread_context_t *context;

	while ((context = swdiag_list_pop(free_seq_contexts)) != NULL) {
		free(context);
	}
	swdiag_list_free(free_seq_contexts);
	free_seq_contexts = NULL;
}
