/* 
 * swdiag_api.c - SW Diagnostics client API module
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

#include "swdiag_client.h"
#include "swdiag_xos.h"
#include "swdiag_obj.h"
#include "swdiag_trace.h"
#include "swdiag_api.h"
#include "swdiag_sequence.h"
#include "swdiag_util.h"
#include "swdiag_sched.h"
#include "swdiag_rci.h"

#define BADSTR(s) (!(s) || *(s)=='\0')

/*
 * Limit of number of rules chained together, do not make this greater
 * than 99 or you will run out of stack when doing recursion.
 */
#define RULE_DEPTH_LIMIT 25

#define SWDIAG_NAME_SEPERATOR '@'

obj_state_t default_obj_state = OBJ_STATE_ENABLED;

/*******************************************************************
 * Local functions
 *******************************************************************/

/*
 * reload()
 *
 * Perform a reload of the system now
 */
static swdiag_result_t reload (const char *instance, void *context)
{
    return(swdiag_xos_reload());
}

/*
 * scheduled_reload()
 *
 * Perform a reload of the system in the next maintenance period.
 */
static swdiag_result_t scheduled_reload (const char *instance, void *context)
{
    return(swdiag_xos_scheduled_reload());
}

/*
 * switchover()
 *
 * Perform a switchover of the system now
 */
static swdiag_result_t switchover (const char *instance, void *context)
{
    return(swdiag_xos_switchover());
}

/*
 * reload_standby()
 *
 * Perform a switchover of the system now
 */
static swdiag_result_t reload_standby (const char *instance, void *context)
{
    return(swdiag_xos_reload_standby());
}

/*
 * scheduled_switchover()
 *
 * Perform a reload of the system in the next maintenance period.
 */
static swdiag_result_t scheduled_switchover (const char *instance, 
                                             void *context)
{
    return(swdiag_xos_scheduled_reload());
}

/*
 * noop()
 *
 * Do nothing, dummy action.
 */
static swdiag_result_t noop (const char *instance, void *context)
{
    return(SWDIAG_RESULT_PASS);
}

/*
 * Duplicate one string by freeing (if necessary) and allocating memory
 * for another string (to_str) and copying in from another (from_str).
 * Returns TRUE if successful, FALSE upon memory error.
 */
static boolean dup_string (char **to_str, const char *from_str)
{
    if (*to_str) {
        free(*to_str);
    }

    if (BADSTR(from_str)) {
        /* this is allowed, it just means we clear the string */
        *to_str = NULL;
        return (TRUE);
    } else {
        *to_str = strdup(from_str);
    }

    if (!*to_str) {
        return (FALSE);
    }
    return (TRUE);
}

/*
 * Return the address of the pointer to the rule at the
 * head of the given object's output rule list.
 */
static obj_rule_t **get_head_output_rule (obj_t *obj)
{
    obj_rule_t **head_rule = NULL;

    switch (obj->type) {
      case OBJ_TYPE_TEST:
        head_rule = &obj->t.test->rule;
        break;
      case OBJ_TYPE_RULE:
        head_rule = &obj->t.rule->output;
        break;
      case OBJ_TYPE_NONE:
        head_rule = &obj->ref_rule;
        break;
      default:
        swdiag_error("Get object head rule - bad type");
        break;
    }
    return (head_rule);
}

static obj_instance_t *swdiag_api_instance_create (obj_t *obj,
                                                   const char *name)
{
    obj_instance_t *instance = NULL;

    instance = swdiag_obj_instance_create(obj, name);
    
    if (instance) {
        instance->last_result = SWDIAG_RESULT_PASS;
        instance->last_result_count = 1;
        instance->sched_test.queued = TEST_QUEUE_NONE;
        if (obj->i.state == OBJ_STATE_ENABLED || 
            obj->i.state == OBJ_STATE_DISABLED) {
            instance->state = obj->i.state;
        } else {
            instance->state = OBJ_STATE_INITIALIZED;
        }
        instance->default_state = obj->i.default_state;
    }
    
    return(instance);
}

/*
 * Get (or create) an object of given name and type from the database.
 * If this function results in the object being allocated, then this function
 * will initialize the type-specific values to their defaults.
 *
 * Note that after return and prior to the type-specific create API
 * being called, some of these defaults may be overridden by other
 * type-specific API functions.  For example the following combination
 * is possible and will result in the description being set in the
 * same way as if the calls are reversed:
 * swdiag_test_set_description() swdiag_test_create_polled()
 */
obj_t *swdiag_api_get_or_create (const char *name, obj_type_t type)
{
    obj_t *obj;
    obj_rule_t *rule = NULL;
    obj_comp_t *comp = NULL;
    obj_test_t *test = NULL;
    obj_action_t *action;
    char *name_copy;
    
    name_copy = swdiag_api_convert_name(name);
    if (!name_copy) {
        swdiag_error("Failed allocation of name for %s '%s'", swdiag_obj_type_str(type), 
                     name);
        return (NULL);
    }

    obj = swdiag_obj_get_or_create(name_copy, type);
    if (!obj || obj->i.state != OBJ_STATE_ALLOCATED) {
        /*
         * We only continue on below and set type-specific defaults if the
         * object has just been allocated.
         *
         * IF it was just allocated then it consumed our name_copy, if not
         * then we need to free it.
         */
        free(name_copy);
        return (obj);
    }

    switch (obj->type) {
    case OBJ_TYPE_TEST:
        /*
         * Because these can be overwritten by swdiag_test_notify() we
         * have moved these here from swdiag_test_create_polled().
         */
        test = obj->t.test;
        if (obj->i.flags.test == SWDIAG_TEST_NONE) {
            /*
             * Only set the default flags if none are already set.
             */
            obj->i.flags.test = SWDIAG_TEST_LOCATION_ALL;
        }
        test->autopass = AUTOPASS_UNSET;
        obj->i.last_result = SWDIAG_RESULT_PASS;
        obj->i.last_result_count = 1;
        break;
        
    case OBJ_TYPE_ACTION:
        action = obj->t.action;        
        if (obj->i.flags.action == SWDIAG_ACTION_NONE) {
            /*
             * Only set the default flags if none are already set.
             */
            obj->i.flags.action = SWDIAG_ACTION_LOCATION_ALL;
        }
        obj->i.last_result = SWDIAG_RESULT_PASS;
        obj->i.last_result_count = 1;

        break;
        
    case OBJ_TYPE_RULE:
        /*
         * Because these can be overwritten by swdiag_rule_set_type() we
         * have moved these here from swdiag_rule_create().
         */
        rule = obj->t.rule;
        rule->operator = SWDIAG_RULE_ON_FAIL;
        rule->default_operator = rule->operator;
        rule->op_n = 0;
        rule->op_m = 0;
        rule->default_op_n = 0;
        rule->default_op_m = 0;
        rule->severity = SWDIAG_SEVERITY_MEDIUM;

        if (obj->i.flags.rule == SWDIAG_RULE_NONE) {
            /*
             * Only set the default flags if none are already set.
             */
            obj->i.flags.rule = SWDIAG_RULE_LOCATION_ALL;
        }  

        obj->i.fail_count = 0;
        obj->i.last_result = SWDIAG_RESULT_PASS;
        obj->i.last_result_count = 1;
        obj->i.rule_data = NULL;
        obj->i.root_cause = RULE_ROOT_CAUSE_NOT;
        obj->i.action_run = FALSE;
        rule->next_in_input = NULL;

        break;
        
    case OBJ_TYPE_COMP:
        comp = obj->t.comp;
        break;
        
    case OBJ_TYPE_NONE:
        break;
        
    default:
        return (obj);
    }
    
    /*
     * Defaults common to all test, action, rule and component types
     */
    obj->description = NULL; /* May be overwritten by swdiag_rule_set_description() */

    /*
     * All objects start off in the initialised state - and stay that way 
     * for forward references. Should the state be fiddled prior to creation
     * then the default_state or cli_state should be changed - not the actual
     * state.
     */
    obj->i.state = OBJ_STATE_INITIALIZED;
    obj->i.default_state = OBJ_STATE_INITIALIZED;
    obj->i.cli_state = OBJ_STATE_INITIALIZED;

    obj->remote_location = FALSE; /* May be overwritten by master_ipc_rx() */

    /*
     * Before returning this object to any of the API functions, validate 
     * that it is internally consistent, so that the API functions don't need
     * to do any checking.
     */
    if (!swdiag_obj_validate(obj, type)) {
        return(NULL);
    } else {
        return (obj);
    }
}

/*
 * user_notify_action()
 *
 * Notify the user using whatever notification mechanism at our disposal.
 */
static swdiag_result_t user_notify_action (const char *instance_name,
                                           void *context)
{
    const char *notification = context;
    
    swdiag_xos_notify_user(instance_name, notification);

    return(SWDIAG_RESULT_PASS);
}

/*******************************************************************
 * External (non-API) utilities
 *******************************************************************/

/*
 * Copy the name and replace all spaces with underscores, and our
 * seperator for remote/local namespace also goes to underscores.
 */
char *swdiag_api_convert_name (const char *from_to_name)
{
    char tmp_name[SWDIAG_MAX_NAME_LEN + 1];
    const char *name = from_to_name;
    unsigned int count = 0;

    for (; *name && count < SWDIAG_MAX_NAME_LEN; name++) {
        if (*name == ' ' || *name == SWDIAG_NAME_SEPERATOR) {
            tmp_name[count] = '_';
        } else {
            tmp_name[count] = *name;
        }
        count++;
    }

    tmp_name[count] = '\0';

    if (*name) {
        swdiag_error("Object name too long '%s', truncated to '%s'",
                     from_to_name, tmp_name);
    }

    return(strdup(tmp_name));
}

/*******************************************************************
 * External API for tests
 *******************************************************************/

void swdiag_test_create_polled (const char *test_name,
                                swdiag_test_t test_func,
                                void *context,
                                unsigned int period)
{
    obj_test_t *test;
    obj_t *obj;
    const char fnstr[] = "Create polled test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }
    if (!test_func) {
        swdiag_error("%s - bad test_func", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    /*
     * Get or create test and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, test_name);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Override defaults
     */
    test = obj->t.test;
    test->type = OBJ_TEST_TYPE_POLLED;
    test->function = test_func;
    test->period = period;
    test->default_period = period;
    if (obj->ref_rule) {
        /*
         * Connect the rule up if it has been set prior to creation,
         * else leave it alone in the case of a reused test.
         */
        test->rule = obj->ref_rule;
    }
    obj->i.sched_test.queued = TEST_QUEUE_NONE;
    obj->i.context = context;

    obj->i.state = OBJ_STATE_CREATED;

    swdiag_obj_db_unlock();
}

void swdiag_test_create_notification (const char *test_name)
{
    obj_test_t *test;
    obj_t *obj;
    const char fnstr[] = "Create notification test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    /*
     * Get or create test and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, test_name);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Override defaults
     */
    test = obj->t.test;
    test->type = OBJ_TEST_TYPE_NOTIFICATION;
    obj->i.sched_test.queued = TEST_QUEUE_NONE;
    obj->i.state = OBJ_STATE_CREATED;

    if (obj->ref_rule) {
        /*
         * Connect the rule up if it has been set prior to creation,
         * else leave it alone in the case of a reused test.
         */
        test->rule = obj->ref_rule;
    }
    swdiag_obj_db_unlock();
}

/*
 * swdiag_test_notify()
 *
 * The client is informing us of a test result. If the object doesn't
 * exist then it is created, but if the instance_name is used, and that
 * instance is not located then the result is ignored.
 */
void swdiag_test_notify (const char *test_name,
                         const char *instance_name,
                         swdiag_result_t result,
                         long value)
{
    obj_t *obj;
    obj_instance_t *instance;
    char *instance_copy = NULL;
    const char fnstr[] = "Notify test";

    /*
     * Sanity check client params, instance *is* allowed to be NULL
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    if (result <= SWDIAG_RESULT_INVALID || result >= SWDIAG_RESULT_LAST ||
        result == SWDIAG_RESULT_IN_PROGRESS) {
        swdiag_error("%s - '%s' bad result value", fnstr, test_name);
        return;
    }

    swdiag_obj_db_lock();

    /*
     * Get or create test and set defaults if applicable
     */
    obj = swdiag_obj_get_by_name_unconverted(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_obj_db_unlock();
        swdiag_error("%s No test with name '%s' found", fnstr, test_name);
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, test_name);
            swdiag_obj_db_unlock();
            return;
        }
        instance = swdiag_obj_instance_by_name(obj, instance_copy);
    } else {
        instance = &obj->i;
    }

    if (instance) { 
        if (instance->state == OBJ_STATE_ENABLED) {
            swdiag_seq_from_test_notify(instance, result, value);
        }
    } else {
        swdiag_error("Test '%s' %s %s does not exist",
                     obj->i.name, 
                     instance_name ? "instance" : "",
                     instance_name ? instance_name : "");
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

/*
 * swdiag_test_set_autopass()
 *
 * Set a timer on this test such that "seconds" after a failure it
 * will automatically be marked as a pass again. This can be useful
 * in situations where we only receive failure notifications and have
 * no easy way of knowing when it is no longer a failure condition.
 *
 * For example with error messages, it may be reasonable to say that
 * the error is no longer relevent after 10 minutes.
 *
 * Every time we get the notification the timer is extended, so constant
 * error messages will result in a constant failure.
 */
void swdiag_test_set_autopass (const char *test_name,
                               unsigned int ms)
{
    obj_t *obj;
    const char fnstr[] = "Set test autopass";   

     /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_obj_db_unlock();
        swdiag_error("%s '%s'", fnstr, test_name);
        return;
    }
    
    obj->t.test->autopass = ms;

    swdiag_obj_db_unlock();
}

static swdiag_result_t poll_for_comp_health (const char *instance,
                                             void *context,
                                             long *value)
{
    obj_t *obj = context;

    if (swdiag_obj_validate(obj, OBJ_TYPE_COMP)) {
        *value = obj->t.comp->health;
        return(SWDIAG_RESULT_VALUE);
    }
    return(SWDIAG_RESULT_ABORT);
}

void swdiag_test_create_comp_health (const char *test_name,
                                     const char *comp_name)
{
    obj_test_t *test;
    obj_comp_t *comp;
    obj_t *test_obj;
    obj_t *comp_obj;
    const char fnstr[] = "Create component health test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }
    if (BADSTR(comp_name)) {
        swdiag_error("%s - bad comp name", fnstr);
        return;
    }
    
    swdiag_obj_db_lock();

    /*
     * Get or create test and set defaults if applicable
     */
    test_obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!test_obj) {
        swdiag_obj_db_unlock();
        swdiag_error("%s could not find or create '%s'", fnstr, test_name);
        return;
    }

    comp_obj = swdiag_api_get_or_create(comp_name, OBJ_TYPE_COMP);
    if (!comp_obj) {
        swdiag_obj_db_unlock();
        swdiag_error("%s could not find or create '%s'", fnstr, comp_name);
        return;
    }

    /*
     * It's a polled test which also gets notifications.
     */
    test = test_obj->t.test;
    test->type = OBJ_TEST_TYPE_POLLED;
    test->function = poll_for_comp_health;
    test->period = SWDIAG_PERIOD_NORMAL;
    test->default_period = SWDIAG_PERIOD_NORMAL;
    test_obj->i.sched_test.queued = TEST_QUEUE_NONE;
    test_obj->i.state = OBJ_STATE_CREATED;
    test_obj->i.context = comp_obj;

    if (test_obj->ref_rule) {
        /*
         * Connect the rule up if it has been set prior to creation,
         * else leave it alone in the case of a reused test.
         */
        test->rule = test_obj->ref_rule;
    }

    comp = comp_obj->t.comp;
    if (!swdiag_list_find(comp->interested_test_objs, test_obj)) {
        swdiag_list_add(comp->interested_test_objs, test_obj);
    }
    swdiag_obj_db_unlock();
}

/*
 * swdiag_test_set_flags()
 *
 * Only set the flags on the base object, and not instances at this time.
 */
void swdiag_test_set_flags (const char *test_name,
                            swdiag_test_flags_t flags)
{
    obj_t *obj;
    const char fnstr[] = "Set flags for test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_obj_db_unlock();
        swdiag_error("%s '%s'", fnstr, test_name);
        return;
    }
    obj->i.flags.test = (flags & ~OBJ_FLAG_RESERVED) | 
                         (obj->i.flags.test & OBJ_FLAG_RESERVED);
    swdiag_obj_db_unlock();
}

swdiag_test_flags_t swdiag_test_get_flags (const char *test_name)
{
    obj_t *obj;
    swdiag_test_flags_t flags = SWDIAG_TEST_NONE;
    const char fnstr[] = "Get flags for test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return (SWDIAG_TEST_NONE);
    }

    swdiag_obj_db_lock();

    /*
     * Get the test and return flags. It is not an error to get flags
     * for unknown test, we just return nothing in this case.
     */
    obj = swdiag_obj_get_by_name_unconverted(test_name, OBJ_TYPE_TEST);
    if (obj) {
        flags = obj->i.flags.test;
    }
    swdiag_obj_db_unlock();
    return(flags & ~OBJ_FLAG_RESERVED);
}

void swdiag_test_delete (const char *test_name)
{
    obj_t *obj;
    obj_instance_t *instance; 
    const char fnstr[] = "Delete test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    obj = swdiag_obj_get_by_name_unconverted(test_name, OBJ_TYPE_ANY);
    if (!obj) {
        swdiag_obj_db_unlock();
        return; /* no object to delete is not an error */
    }
    if (obj->type != OBJ_TYPE_TEST) {
        swdiag_error("%s '%s' - bad type (%s)",
            fnstr, test_name, swdiag_obj_type_str(obj->type));
        swdiag_obj_db_unlock();
        return;
    }

    if (obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
        for(instance = &obj->i; instance != NULL; instance = instance->next) {
            swdiag_sched_remove_test(instance);
        }
    }

    swdiag_obj_delete(obj);

    swdiag_obj_db_unlock();
}

void *swdiag_api_test_get_context (const char *test_name)
{
    obj_t *obj;
    void *context = NULL;
    const char fnstr[] = "Get context for test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return (NULL);
    }

    swdiag_obj_db_lock();
    
    /*
     * Get the test and return context. It is not an error to get context
     * for unknown test, we just return NULL in this case.
     */
    obj = swdiag_obj_get_by_name_unconverted(test_name, OBJ_TYPE_TEST);
    if (obj) {
        context = obj->i.context;
    }
    swdiag_obj_db_unlock();
   return (context);
}

void swdiag_test_chain_ready (const char *test_name)
{
    obj_t *obj;
    obj_instance_t *instance;
    const char fnstr[] = "Test Ready";
    obj_state_t state;

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    obj = swdiag_obj_get_by_name_unconverted(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, test_name);
         swdiag_obj_db_unlock();
        return;
    }

    switch (obj->i.state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
        /*
         * We don't care what the current test state is, we still need
         * to set the state on the rest of the chained objects anyway.
         */

        /* FALLTHRU */
    case OBJ_STATE_CREATED:
        /*
         * Set all the objects and instances connected to this test to the 
         * default state for the system (enabled/disabled) unless the owning
         * component is explicitly enabled or disabled, in which case use that,
         * or if the CLI has specified a specific state.
         */
        if (obj->parent_comp && 
            obj->parent_comp->obj->i.state != obj->parent_comp->obj->i.default_state) {
            state = obj->parent_comp->obj->i.state;
        } else {
            state = default_obj_state;
        }

        swdiag_obj_chain_update_state(obj, state);

        if (obj->i.cli_state != OBJ_STATE_INITIALIZED) {
            /*
             * The test may have been explicitrly configured to be enabled
             * so grab the state here (swdiag_obj_chain_set_state()) handles
             * this already.
             */
            obj->i.state = obj->i.cli_state;
        } 

        if (obj->i.state == OBJ_STATE_ENABLED && 
            obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
            /*
             * Add all the instances for this test to the schedular
             */
            for (instance = &obj->i; instance != NULL; instance = instance->next) {
                swdiag_sched_add_test(instance, FALSE);
            }
        }
        swdiag_trace(test_name, "Test '%s' %s", test_name, 
                     swdiag_obj_state_str(obj->i.state));
        break;
    default:
        swdiag_error("%s '%s'", fnstr, test_name);
        break;
    } 
    swdiag_obj_db_unlock();
}

void swdiag_test_enable (const char *test_name,
                         const char *instance_name)
{
    swdiag_api_test_enable_guts(test_name, instance_name, FALSE); 
}

void swdiag_api_test_enable_guts (const char *test_name,
                                  const char *instance_name,
                                  boolean cli)
{
    obj_t *obj;
    const char fnstr[] = "Enable test";
    char *instance_copy = NULL;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();

    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, test_name);
         swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, test_name); 
            swdiag_obj_db_unlock();
            return;
        }

        instance = swdiag_obj_instance_by_name(obj, instance_copy);

        if (!instance) {
            /*
             * Create a forward reference for the instance.
             */
            instance = swdiag_api_instance_create(obj, instance_copy);
            if (!instance) {
                swdiag_error("Invalid instance name '%s:%s'", test_name, instance_copy);
                if (instance_copy) {
                    free(instance_copy);
                }
                swdiag_obj_db_unlock();
                return;
            }
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_CREATED:

        if (!instance_name) {
            /*
             * No instance name given, enable all the instances if any
             * on this test adding them into the schedular if polled.
             */
            do {
                if (!cli) {
                    instance->default_state = OBJ_STATE_ENABLED;
                } else {
                    instance->cli_state = OBJ_STATE_ENABLED;
                }

                if (instance->cli_state != OBJ_STATE_DISABLED) {
                    /*
                     * If the CLI has set the state to disabled then
                     * Don't allow it to be enabled from the API.
                     */
                    instance->state = OBJ_STATE_ENABLED;
                }

                if (instance->state == OBJ_STATE_ENABLED &&
                    obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
                    swdiag_sched_add_test(instance, FALSE);
                }
            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just enable the named instance
             */    
            if (!cli) {
                instance->default_state = OBJ_STATE_ENABLED;
            } else {
                instance->cli_state = OBJ_STATE_ENABLED;
            }
            
            if (instance->cli_state != OBJ_STATE_DISABLED) {
                /*
                 * If the CLI has set the state to disabled then
                 * Don't allow it to be enabled from the API.
                 */
                instance->state = OBJ_STATE_ENABLED;
            }
            
            if (instance->state == OBJ_STATE_ENABLED &&
                obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
                swdiag_sched_add_test(instance, FALSE);
            }
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_ENABLED;
        } else {
            instance->cli_state = OBJ_STATE_ENABLED;
        }
        break;
    default:
        swdiag_error("%s '%s' in the wrong state", fnstr, test_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

/*
 * Revert to default state.
 */
void swdiag_api_test_default (const char *test_name,
                              const char *instance_name)

{
    obj_t *obj;
    const char fnstr[] = "Default test";
    char *instance_copy = NULL;
    obj_instance_t *instance;
    obj_test_t *test;

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();

    obj = swdiag_obj_get_by_name_unconverted(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, test_name);
        swdiag_obj_db_unlock();
        return;
    }

    test = obj->t.test;

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, test_name); 
            swdiag_obj_db_unlock();
            return;
        }

        instance = swdiag_obj_instance_by_name(obj, instance_copy);

        if (!instance) {
            swdiag_error("Invalid instance name '%s:%s'", test_name, instance_copy);
            if (instance_copy) {
                free(instance_copy);
            }
            swdiag_obj_db_unlock();
            return;
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_CREATED:

        if (!instance_name) {
            /*
             * No instance name given, default all the instances if any
             * on this test adding them into the schedular if polled and 
             * enabled.
             */
            test->period = test->default_period;

            do {
                if (instance->state == OBJ_STATE_ENABLED ||
                    instance->state == OBJ_STATE_DISABLED) {
                    instance->state = instance->default_state;
                }
                instance->cli_state = OBJ_STATE_INITIALIZED;
                
                if (obj->t.test->type == OBJ_TEST_TYPE_POLLED &&
                    instance->state == OBJ_STATE_ENABLED) {
                    swdiag_sched_add_test(instance, FALSE);
                }
            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just default the named instance
             */
            if (instance->state == OBJ_STATE_ENABLED ||
                instance->state == OBJ_STATE_DISABLED) {
                instance->state = instance->default_state;
            }
            instance->cli_state = OBJ_STATE_INITIALIZED;

            if (obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
                swdiag_sched_add_test(instance, FALSE);
            }
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference. If we knew whether
         * there was anything else configured, and if there wasn't we could
         * delete this object altogether now.
         */
        if (test->period == test->default_period) {
            /*
             * The state has been defaulted, and the period is set to the
             * defaults, and the object hasn't actually been created. So
             * it should be safe to delete the test.
             */
            swdiag_obj_delete(obj);
        } else {
            instance->cli_state = OBJ_STATE_INITIALIZED;
        }
        break;
    default:
        swdiag_error("%s '%s' in the wrong state", fnstr, test_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

void swdiag_test_disable (const char *test_name, const char *instance_name)
{
    swdiag_api_test_disable_guts(test_name, instance_name, FALSE);
}

void swdiag_api_test_disable_guts (const char *test_name, 
                                   const char *instance_name,
                                   boolean cli)
{
    obj_t *obj;
    const char fnstr[] = "Disable test";
    char *instance_copy = NULL;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();

    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, test_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, test_name);
            swdiag_obj_db_unlock();
            return;
        }
        
        instance = swdiag_obj_instance_by_name(obj, instance_copy);

        if (!instance) {
            /*
             * Create a forward reference.
             */
            instance = swdiag_api_instance_create(obj, instance_copy);
            
            if (!instance) {
                swdiag_error("Invalid instance name '%s:%s'", test_name, instance_copy);
                if (instance_copy) {
                    free(instance_copy);
                }
                swdiag_obj_db_unlock();
                return;
            }
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_CREATED:
        if (!instance_name) {
            /*
             * No instance name given, disable all the instances if any
             * on this test removing them from the schedular if polled.
             */
            do {
                if (!cli) {
                    instance->default_state = OBJ_STATE_DISABLED;
                } else {
                    instance->cli_state = OBJ_STATE_DISABLED;
                }

                if (instance->cli_state != OBJ_STATE_ENABLED) {
                    /*
                     * If the CLI has set the state to enabled then
                     * Don't allow it to be disabled from the API.
                     */
                    instance->state = OBJ_STATE_DISABLED;
                }
                
                if (obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
                    swdiag_sched_remove_test(instance);
                }
            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just disable the named instance
             */    
            if (!cli) {
                instance->default_state = OBJ_STATE_DISABLED;
            } else {
                instance->cli_state = OBJ_STATE_DISABLED;
            }

            if (instance->cli_state != OBJ_STATE_ENABLED) {
                /*
                 * If the CLI has set the state to enabled then
                 * Don't allow it to be disabled from the API.
                 */
                instance->state = OBJ_STATE_DISABLED;
            }

            if (obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
                swdiag_sched_remove_test(instance);
            }
        }
        break; 
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_DISABLED;
        } else {
            instance->cli_state = OBJ_STATE_DISABLED;
        }
        break;
    default:
        swdiag_error("%s '%s'", fnstr, test_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

void swdiag_test_set_description (const char *test_name,
                                  const char *description)
{
    obj_t *obj;
    const char fnstr[] = "Set description for test";

    /*
     * Sanity check client params
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test_name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    /*
     * Get or create test and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, test_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (!dup_string(&obj->description, description)) {
        swdiag_error("%s '%s' - alloc '%s'", fnstr, test_name, description);
    }
    swdiag_obj_db_unlock();
}

/*******************************************************************
 * External API for actions
 *******************************************************************/

void swdiag_action_create (const char *action_name,
                           swdiag_action_t action_func,
                           void *context)
{
    obj_action_t *action;
    obj_t *obj;
    const char fnstr[] = "Create action";

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }
    if (!action_func) {
        swdiag_error("%s '%s' - bad action_func", fnstr, action_name);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create action and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION); 
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Override defaults
     */
    action = obj->t.action;
    if (action->function == user_notify_action) {
        free((void *)obj->i.context);
    }
    action->function = action_func;
    obj->i.context = context;

    obj->i.state = OBJ_STATE_CREATED;

    swdiag_obj_db_unlock();
}

void swdiag_action_complete (const char *action_name,
                             const char *instance_name,
                             swdiag_result_t result)
{
    obj_t *obj;
    obj_instance_t *instance;
    char *instance_copy = NULL;
    const char fnstr[] = "Complete action";

    /*
     * Sanity check client params, instance can be NULL.
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get error object, which must exust.
     */
    obj = swdiag_obj_get_by_name_unconverted(action_name, OBJ_TYPE_ACTION);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, action_name);
            swdiag_obj_db_unlock();
            return;
        }
    }

    instance = swdiag_obj_instance_by_name(obj, instance_copy);

    if (instance && instance->state == OBJ_STATE_ENABLED) {
        swdiag_seq_from_action_complete(instance, result);
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

/*
 * swdiag_action_create_user_alert()
 *
 * Create a new action with the notification string as context
 * that will generate this user notification when called.
 *
 * The string is freed when the action is deleted or if a different
 * string is applied.
 */
void swdiag_action_create_user_alert (const char *action_name,
                                      const char *notification_string)
{
    char *notification;
    const char fnstr[] = "Create user alert action";

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }
    if (BADSTR(notification_string)) {
        swdiag_error("%s - bad notification_string", fnstr);
        return;
    }

    notification = strdup(notification_string);
    if (!notification) {
        swdiag_error("%s '%s' - alloc '%s'",
            fnstr, action_name, notification_string);
        return;
    }

    swdiag_action_create(action_name, 
                         user_notify_action,
                         notification);
}

void swdiag_action_delete (const char *action_name)
{
    const char fnstr[] = "Delete action";
    obj_action_t *action;
    obj_t *obj;

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    obj = swdiag_obj_get_by_name_unconverted(action_name, OBJ_TYPE_ANY);
    if (!obj) {
        swdiag_obj_db_unlock();
        return; /* no object to delete is not an error */
    }
    if (obj->type != OBJ_TYPE_ACTION) {
        swdiag_error("%s '%s' - bad type (%s)",
            fnstr, action_name, swdiag_obj_type_str(obj->type));
        swdiag_obj_db_unlock();
        return;
    }
    action = obj->t.action;

    /*
     * Free the user notification string
     */
    if (action->function == user_notify_action) {
        free((void *)obj->i.context);
    }

    swdiag_obj_delete(obj);
    swdiag_obj_db_unlock();
}

void swdiag_action_enable (const char *action_name,
                           const char *instance_name)
{
    swdiag_api_action_enable_guts(action_name, instance_name, FALSE);
}

void swdiag_api_action_enable_guts (const char *action_name,
                                    const char *instance_name,
                                    boolean cli)
{
    obj_t *obj;
    const char fnstr[] = "Enable action";
    char *instance_copy = NULL;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);

        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, action_name);
            swdiag_obj_db_unlock();
            return;
        }

        instance = swdiag_obj_instance_by_name(obj, instance_copy);
        
        if (!instance) {
            instance = swdiag_api_instance_create(obj, instance_copy);
            if (!instance) {
                swdiag_error("Invalid instance name '%s:%s'", action_name, instance_copy);
                if (instance_copy) {
                    free(instance_copy);
                }
                swdiag_obj_db_unlock();
                return;
            }
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_CREATED:

        if (!instance_name) {
            /*
             * No instance name given, enable all the instances if any
             * on this action
             */
            do {
                if (!cli) {
                    instance->default_state = OBJ_STATE_ENABLED;
                } else {
                    instance->cli_state = OBJ_STATE_ENABLED;
                }
                
                if (instance->cli_state != OBJ_STATE_DISABLED) {
                    /*
                     * If the CLI has set the state to disabled then
                     * Don't allow it to be enabled from the API.
                     */
                    instance->state = OBJ_STATE_ENABLED;
                }
            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just enable the named instance
             */    
            if (!cli) {
                instance->default_state = OBJ_STATE_ENABLED;
            } else {
                instance->cli_state = OBJ_STATE_ENABLED;
            }

            if (instance->cli_state != OBJ_STATE_DISABLED) {
                /*
                 * If the CLI has set the state to disabled then
                 * Don't allow it to be enabled from the API.
                 */
                instance->state = OBJ_STATE_ENABLED;
            }
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_ENABLED;
        } else {
            instance->cli_state = OBJ_STATE_ENABLED;
        }
        break; 
    default:
        swdiag_error("%s '%s'", fnstr, action_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

/*
 * Revert to default state.
 */
void swdiag_api_action_default (const char *action_name,
                                const char *instance_name)

{
    obj_t *obj;
    const char fnstr[] = "Default action";
    char *instance_copy = NULL;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();

    obj = swdiag_obj_get_by_name_unconverted(action_name, OBJ_TYPE_ACTION);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, action_name); 
            swdiag_obj_db_unlock();
            return;
        }

        instance = swdiag_obj_instance_by_name(obj, instance_copy);

        if (!instance) {
            swdiag_error("Invalid instance name '%s:%s'", action_name, instance_copy);
            if (instance_copy) {
                free(instance_copy);
            }
            swdiag_obj_db_unlock();
            return;
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_CREATED:

        if (!instance_name) {
            /*
             * No instance name given, default all the instances if any
             * on this action
             */
            do {
                if (instance->state == OBJ_STATE_ENABLED ||
                    instance->state == OBJ_STATE_DISABLED) {
                    instance->state = instance->default_state;
                }
                instance->cli_state = OBJ_STATE_INITIALIZED;
            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just default the named instance
             */
            if (instance->state == OBJ_STATE_ENABLED ||
                instance->state == OBJ_STATE_DISABLED) {
                instance->state = instance->default_state;
            }
            instance->cli_state = OBJ_STATE_INITIALIZED;
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference, and if it is
         * being defaulted but not yet created it should be safe to
         * delete it - it will be created again later.
         */
        swdiag_obj_delete(obj);
        break;
    default:
        swdiag_error("%s '%s' in the wrong state", fnstr, action_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

void swdiag_action_disable (const char *action_name, 
                            const char *instance_name)
{
    swdiag_api_action_disable_guts(action_name, instance_name, FALSE);
}

void swdiag_api_action_disable_guts (const char *action_name, 
                                     const char *instance_name,
                                     boolean cli)
{
    obj_t *obj;
    const char fnstr[] = "Disable action";
    char *instance_copy = NULL;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }


    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, action_name);
            swdiag_obj_db_unlock();
            return;
        }
    
        instance = swdiag_obj_instance_by_name(obj, instance_copy);
        
        if (!instance) {
            instance = swdiag_api_instance_create(obj, instance_copy);
            if (!instance) {
                swdiag_error("Invalid instance name '%s:%s'", action_name, instance_copy);
                if (instance_copy) {
                    free(instance_copy);
                }
                swdiag_obj_db_unlock();
                return;
            }
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_CREATED:
        if (!instance_name) {
            /*
             * No instance name given, disable all the instances if any
             * on this action
             */
            do {
                if (!cli) {
                    instance->default_state = OBJ_STATE_DISABLED;
                } else {
                    instance->cli_state = OBJ_STATE_DISABLED;
                }

                if (instance->cli_state != OBJ_STATE_ENABLED) {
                    /*
                     * If the CLI has set the state to enabled then
                     * Don't allow it to be disabled from the API.
                     */
                    instance->state = OBJ_STATE_DISABLED;
                }

            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just disable the named instance
             */    
            if (!cli) {
                instance->default_state = OBJ_STATE_DISABLED;
            } else {
                instance->cli_state = OBJ_STATE_DISABLED;
            }

            if (instance->cli_state != OBJ_STATE_ENABLED) {
                /*
                 * If the CLI has set the state to enabled then
                 * Don't allow it to be disabled from the API.
                 */
                instance->state = OBJ_STATE_DISABLED;
            }

        }
        break;

    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_DISABLED;
        } else {
            instance->cli_state = OBJ_STATE_DISABLED;
        }
        break;
    default:
        swdiag_error("%s '%s'", fnstr, action_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

void swdiag_action_set_flags (const char *action_name,
                              swdiag_action_flags_t flags)
{
    obj_t *obj;
    const char fnstr[] = "Set flags for action";

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, action_name);
        swdiag_obj_db_unlock();
        return;
    }
    obj->i.flags.action = (flags & ~OBJ_FLAG_RESERVED) | 
                           (obj->i.flags.action & OBJ_FLAG_RESERVED);
    swdiag_obj_db_unlock();
}

swdiag_action_flags_t swdiag_action_get_flags (const char *action_name)
{
    obj_t *obj;
    swdiag_action_flags_t flags = SWDIAG_ACTION_NONE;
    const char fnstr[] = "Get flags for action";

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return (SWDIAG_ACTION_NONE);
    }
    swdiag_obj_db_lock();
    /*
     * Get the action and return flags. It is not an error to get flags
     * for unknown action, we just return nothing in this case.
     */
    obj = swdiag_obj_get_by_name_unconverted(action_name, OBJ_TYPE_ACTION);
    if (obj) {
        flags = obj->i.flags.action;
    }
    swdiag_obj_db_unlock();
    return(flags & ~OBJ_FLAG_RESERVED);
}

void *swdiag_get_context (const char *obj_name)
{
    obj_t *obj;
    const char fnstr[] = "Get the context";
    void *context = NULL;

    /*
     * Sanity check client params
     */
    if (BADSTR(obj_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return (NULL);
    }
    swdiag_obj_db_lock();
    /*
     * Get the action and return flags. It is not an error to get flags
     * for unknown action, we just return nothing in this case.
     */
    obj = swdiag_obj_get_by_name_unconverted(obj_name, OBJ_TYPE_ANY);
    if (obj) {
        context = obj->i.context;
    }
    swdiag_obj_db_unlock();
    return context;
}

void swdiag_action_set_description (const char *action_name,
                                    const char *description)
{
    obj_t *obj;
    const char fnstr[] = "Set description for action";

    /*
     * Sanity check client params
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create action and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (!dup_string(&obj->description, description)) {
        swdiag_error("%s '%s' - alloc '%s'", fnstr, action_name, description);
    }
    swdiag_obj_db_unlock();
}

/*******************************************************************
 * External API for rules
 *******************************************************************/

/*
 * rule_input_search()
 *
 * Starting from obj look through the inputs looking for target,
 * TRUE if it is found, FALSE if not.
 */
static boolean rule_input_search (obj_t *obj, obj_t *target, unsigned int depth)
{ 
    swdiag_list_element_t *element;
    obj_rule_t *rule;
    obj_t *input;
    boolean retval = FALSE;

    /*
     * Don't recurse more than a set limit else we will run out of stack.
     * It is an error to have more than this level, so return TRUE to block
     * the addition of this new rule.
     */
    if (++depth > RULE_DEPTH_LIMIT) {
        swdiag_error("Adding rule '%s' failed, too many rules %d chained together, maximum %d",
                     target->i.name, depth, RULE_DEPTH_LIMIT);
        return(TRUE);
    }

    if (obj && obj->type == OBJ_TYPE_RULE) {
        rule = obj->t.rule;
        for(element = rule->inputs->head;
            element != NULL;
            element = element->next) {
            /*
             * For every input in the inputs list for this rule...
             */
            input = (obj_t*)element->data;
            if (input == target) {
                retval = TRUE;
                break;
            } else {
                if (rule_input_search(input, target, depth)) {
                    retval = TRUE;
                    break;
                }
            }
        }
    }
    return(retval);
}

void swdiag_rule_create (const char *rule_name,
                         const char *test_or_rule_name,
                         const char *action_name)
{
    swdiag_list_element_t *element;
    obj_rule_t *rule;
    obj_action_t *action;
    obj_t *rule_obj, *action_obj, *obj, *input_obj = NULL;
    const char fnstr[] = "Create rule";

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }
    if (BADSTR(test_or_rule_name)) {
        swdiag_error("%s - bad test_or_rule_name", fnstr);
        return;
    }
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create rule and set defaults if applicable
     */
    rule_obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!rule_obj) {
        swdiag_error("%s '%s'", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }
    rule = rule_obj->t.rule;

    /*
     * Get or create the input object (test or rule) and validate it's
     * type
     */
    input_obj = swdiag_api_get_or_create(test_or_rule_name, OBJ_TYPE_ANY);
    if (!input_obj) {
        swdiag_error("%s '%s', - creating test_or_rule_name '%s'",
            fnstr, rule_name, test_or_rule_name);
        swdiag_obj_delete(rule_obj);
        swdiag_obj_db_unlock();
        return;
    }
    switch (input_obj->type) {
      case OBJ_TYPE_TEST:
      case OBJ_TYPE_RULE:
      case OBJ_TYPE_NONE:
        /* input type is ok */
        break;
      default:
        swdiag_error("%s '%s', bad type (%s) for test_or_rule_name '%s'",
            fnstr, rule_name, swdiag_obj_type_str(input_obj->type),
            test_or_rule_name);
        swdiag_obj_delete(rule_obj);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Can't have the same input more than once.
     */
    if (rule_input_search(rule_obj, input_obj, 0)) {
        swdiag_error("%s '%s', '%s' already an input",
                     fnstr, rule_name, test_or_rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Check to see whether the input_obj has an existing dependency
     * on this rule, if it does then we can not connect the two.
     */
    if (rule_input_search(input_obj, rule_obj, 0)) {
        /*
         * Invalid linkage since the input is referencing this
         * rule, which would be a loop.
         */
        swdiag_error("%s - Can not create '%s' since it would create a loop or there are too many rules connected",
                     fnstr, rule_name);
        /*
         * Delete the rule - since it can't have this input rather than
         * leaving it around.
         */
        swdiag_obj_delete(rule_obj);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * We may be changing the input for an existing rule, to do this
     * we have to:
     *
     * 1. Remove this rule from the chain of rules that are linked to
     *    each of the inputs:
     *
     *    swdiag_rule_create(rule2, test, action);
     *    swdiag_rule_create(rule2, test2, action);
     * 
     *    In the example below we are removing the input "test" from 
     *    rule2. So this will involve starting at rule1, and walking
     *    the chain of rules until we find "rule2", then linking
     *    rule1 to rule3.
     *
     *    test
     *      |
     *    rule1 -> rule2 -> rule3
     *
     * 2. Remove the rules reference to the old inputs.
     */
    for(element = rule->inputs->head;
        element != NULL;
        element = element->next) {
        obj = (obj_t*)element->data;
        obj_rule_t **head_output_rule = get_head_output_rule(obj);
        if (head_output_rule) {
            if (*head_output_rule == rule) {
                /*
                 * The input is pointing at us, that makes it easy, just
                 * change the inputs head to be our next input.
                 */
                *head_output_rule = rule->next_in_input;
            } else {
                /* 
                 * Maybe our rule is in the next_in_input chain off of the
                 * head, so loop through this chain until we find
                 * ourselves, then remove ourselves from the chain.
                 */ 
                obj_rule_t *prev_rule;
                for (prev_rule = *head_output_rule; 
                     prev_rule != NULL;
                     prev_rule = prev_rule->next_in_input) {
                    if (prev_rule->next_in_input == rule) {
                        /*
                         * Matched our rule, link the previous rule
                         * to the next of our rule.
                         */
                        prev_rule->next_in_input = rule->next_in_input;
                        break;
                    }
                }
                if (!prev_rule) {
                    /*
                     * This objects linkages are wrong, it had an
                     * existing input, however that input doesn't
                     * include this rule.
                     */
                    swdiag_error("%s '%s', can't remove rule from input list",
                                 fnstr, rule_name);
                }
            }
        }
    }
    
    /*
     * We start off at creation time not linked to anything, which may mean
     * throwing away any existing forward referenced inputs, but so be it.
     */
    while((obj = swdiag_list_pop(rule->inputs)) != NULL)

    rule->next_in_input = NULL;

    /*
     * Get or create the action object and add it to the rules action list
     */
    action_obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION);
    if (!action_obj) {
        swdiag_error("%s '%s', - creating action '%s'",
            fnstr, rule_name, action_name);
        swdiag_obj_delete(rule_obj);
        swdiag_obj_db_unlock();
        return;
    }

    action = action_obj->t.action;

    if (rule->action_list) {
        /*
         * Is this action already listed for this rule? If not add it.
         */
        if (!swdiag_list_find(rule->action_list, action_obj)) {
            swdiag_list_add(rule->action_list, action_obj);
        }
        if (!swdiag_list_find(action->rule_list, rule_obj)) {
            swdiag_list_add(action->rule_list, rule_obj);
        }
    } else {
        swdiag_error("%s '%s', - bad action_list rule", fnstr, rule_name);
    }

    /*
     * If appropriate link the input to this rule,
     * and push this rule to the start of the input list.
     */
    if (input_obj) {
        obj_rule_t **head_output_rule;

        /*
         * Link our rule to this input
         */
        swdiag_list_add(rule->inputs, input_obj);

        /*
         * Link the inputs output to our rule, which for a rule is the output
         * for a test is the rule, and for an unknown type is the ref_rule.
         */
        head_output_rule = get_head_output_rule(input_obj);
        if (head_output_rule) {
            rule->next_in_input = *head_output_rule;
            *head_output_rule = rule;
        }
    }

    /*
     * There may be an existing ref_rule set for this object, so check
     * that and use it as the output if found. The ref_rule will
     * already be referencing us, so just connect us to them.
     *
     * Occurs where another rule has forward referenced this one as an input.
     */
    if (rule_obj->ref_rule) {
        rule->output = rule_obj->ref_rule;
    }

    rule_obj->i.state = OBJ_STATE_CREATED;
    swdiag_obj_db_unlock();
}

void swdiag_rule_add_input (const char *rule_name,
                            const char *test_or_rule_name)
{ 
    obj_rule_t *rule;
    obj_t *rule_obj, *input_obj = NULL;
    const char fnstr[] = "Add rule input";

    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }
    if (BADSTR(test_or_rule_name)) {
        swdiag_error("%s - bad test_or_rule_name", fnstr);
        return;
    }  

    swdiag_obj_db_lock();

    /*
     * Get rule - don't create since a later creation will lose the
     * input list anyway.
     */
    rule_obj = swdiag_obj_get_by_name_unconverted(rule_name, OBJ_TYPE_RULE);
    if (!rule_obj) {
        swdiag_error("%s '%s'", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    rule = rule_obj->t.rule;

    input_obj = swdiag_api_get_or_create(test_or_rule_name, OBJ_TYPE_ANY);

    if (!input_obj) {
        swdiag_error("%s '%s', - creating test_or_rule_name '%s'",
            fnstr, rule_name, test_or_rule_name);
        swdiag_obj_db_unlock();
        return;
    }
    switch (input_obj->type) {
    case OBJ_TYPE_TEST:
    case OBJ_TYPE_RULE:
    case OBJ_TYPE_NONE:
        /* input type is ok */
        break;
    case OBJ_TYPE_ANY:
    case OBJ_TYPE_COMP:
    case OBJ_TYPE_ACTION:
        swdiag_error("%s '%s', bad type (%s) for test_or_rule_name '%s'",
            fnstr, rule_name, swdiag_obj_type_str(input_obj->type),
            test_or_rule_name);
        swdiag_obj_db_unlock();
        return;
    }

     if (rule_input_search(rule_obj, input_obj, 0)) {
        swdiag_error("%s '%s', '%s' already an input",
                     fnstr, rule_name, test_or_rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (rule_input_search(input_obj, rule_obj, 0)) {
        /*
         * Invalid linkage since the input is referencing this
         * rule, which would be a loop.
         */
        swdiag_error("%s - Can not add '%s' since it would cause a loop or there are too many rules connected.",
                     fnstr, test_or_rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (input_obj) {
        obj_rule_t **head_output_rule;

        /*
         * Link our rule to this input
         */
        swdiag_list_add(rule->inputs, input_obj);

        /*
         * Link the inputs output to our rule
         */
        head_output_rule = get_head_output_rule(input_obj);
        if (head_output_rule) {
            rule->next_in_input = *head_output_rule;
            *head_output_rule = rule;
        }
    }

    swdiag_obj_db_unlock();
}

/*
 * swdiag_rule_set_type()
 *
 * For a rule with given name, set the rule operator and operands.
 * The rule should already be created, otherwise this function does nothing
 * except internally record an error.
 */
void swdiag_rule_set_type (const char *rule_name,
                           swdiag_rule_operator_t operator,
                           long operand_n,
                           long operand_m)
{
    obj_rule_t *rule;
    obj_t *obj;
    const char fnstr[] = "Set type for rule";

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule name", fnstr);
        return;
    }

    if (!(operator > SWDIAG_RULE_INVALID && operator < SWDIAG_RULE_LAST)) {
        swdiag_error("%s - rule '%s' invalid rule operator value %d", 
                     fnstr, rule_name, operator);
        return;
    }

    switch(operator) {
    case SWDIAG_RULE_ON_FAIL:
    case SWDIAG_RULE_DISABLE:
        if (operand_n) {
            swdiag_error("%s - rule '%s' N operand specified (%ld) when not expected", 
                         fnstr, rule_name, operand_n);
            return;
        }
        if (operand_m) {
            swdiag_error("%s - rule '%s' M operand specified (%ld) when not expected", 
                         fnstr, rule_name, operand_m);
            return;
        }
        break;
    case SWDIAG_RULE_EQUAL_TO_N:
    case SWDIAG_RULE_NOT_EQUAL_TO_N:
    case SWDIAG_RULE_LESS_THAN_N:
    case SWDIAG_RULE_GREATER_THAN_N:
        if (operand_m) {
            swdiag_error("%s - rule '%s' M operand specified (%ld) when not expected", 
                         fnstr, rule_name, operand_m);
            return;
        }
        break;
    case SWDIAG_RULE_N_EVER:
    case SWDIAG_RULE_N_IN_ROW:
        if (operand_m) {
            swdiag_error("%s - rule '%s' M operand specified (%ld) when not expected", 
                         fnstr, rule_name, operand_m);
            return;
        }

        if (operand_n < 1) {
            swdiag_error("%s - rule '%s' N operand less than 1 (%ld)", 
                         fnstr, rule_name, operand_n);
            return;
        }
        break;
    case SWDIAG_RULE_RANGE_N_TO_M:
        if (operand_n > operand_m) {
            swdiag_error("%s - rule '%s' N operand (%ld) greater than M (%ld)", 
                         fnstr, rule_name, operand_n, operand_m);
            return;
        }
        break;
    case SWDIAG_RULE_N_IN_M:
        if (operand_n > operand_m) {
            swdiag_error("%s - rule '%s' N operand (%ld) greater than M (%ld)", 
                         fnstr, rule_name, operand_n, operand_m);
            return;
        }
        /* FALLTHRU */
    case SWDIAG_RULE_N_IN_TIME_M:
        if (operand_n < 1) {
            swdiag_error("%s - rule '%s' N operand less than 1 (%ld)", 
                         fnstr, rule_name, operand_n);
            return;
        }
        if (operand_m < 1) {
            swdiag_error("%s - rule '%s' M operand less than 1 (%ld)", 
                         fnstr, rule_name, operand_m);
            return;
        }
        break;
    default:
        break;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create rule and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    rule = obj->t.rule;
    rule->operator = operator;
    rule->default_operator = operator;
    rule->op_n = operand_n;
    rule->op_m = operand_m;
    rule->default_op_n = operand_n;
    rule->default_op_m = operand_m;
    swdiag_obj_db_unlock();
}

/*
 * swdiag_rule_add_action()
 *
 * Add subsequent actions to an existing rule so that a single rule
 * may trigger more than one recovery action.
 */
void swdiag_rule_add_action (const char *rule_name,
                             const char *action_name)
{   
    obj_rule_t *rule;
    obj_t *rule_obj, *action_obj;
    obj_action_t *action;
    const char fnstr[] = "Add action to rule";

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create rule and set defaults if applicable
     */
    rule_obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!rule_obj) {
        swdiag_error("%s '%s' - rule", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Get or create action and set defaults if applicable
     */
    action_obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION);
    if (!action_obj) {
        swdiag_error("%s '%s' - action '%s'", fnstr, rule_name, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    rule = rule_obj->t.rule;
    action = action_obj->t.action;
    
    if (!swdiag_list_find(rule->action_list, action_obj)) {
        swdiag_list_add(rule->action_list, action_obj);
    } else {
        swdiag_error("%s '%s' - action '%s' already present", 
                     fnstr, rule_name, action_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (!swdiag_list_find(action->rule_list, rule_obj)) {
        swdiag_list_add(action->rule_list, rule_obj);
    } else {
        swdiag_error("%s '%s' - rule '%s' already present", 
                     fnstr, rule_name, action_name);
    }
    swdiag_obj_db_unlock();
}

void swdiag_rule_set_flags (const char *rule_name,
                            swdiag_rule_flags_t flags)
{
    obj_t *obj;
    const char fnstr[] = "Set flags for rule";

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }
    obj->i.flags.rule = (flags & ~OBJ_FLAG_RESERVED) | 
                         (obj->i.flags.rule & OBJ_FLAG_RESERVED);
    swdiag_obj_db_unlock();
}

swdiag_rule_flags_t swdiag_rule_get_flags (const char *rule_name)
{
    obj_t *obj;
    swdiag_rule_flags_t flags = SWDIAG_RULE_NONE;
    const char fnstr[] = "Get flags for rule";

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return (SWDIAG_RULE_NONE);
    }
    swdiag_obj_db_lock();
    /*
     * Get the rule and return flags. It is not an error to get flags
     * for unknown rule, we just return nothing in this case.
     */
    obj = swdiag_obj_get_by_name_unconverted(rule_name, OBJ_TYPE_RULE);
    if (obj) {
        flags = obj->i.flags.rule;
    }
    swdiag_obj_db_unlock();
    return (flags & ~OBJ_FLAG_RESERVED);
}

void swdiag_rule_delete (const char *rule_name) 
{
    const char fnstr[] = "Delete rule";
    obj_t *obj;

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_ANY);
    if (!obj) {
        swdiag_error("%s '%s' - does not exist", fnstr,  rule_name);
        swdiag_obj_db_unlock();
        return;
    }
    if (obj->type != OBJ_TYPE_RULE) {
        swdiag_error("%s '%s' - bad type (%s)",
            fnstr, rule_name, swdiag_obj_type_str(obj->type));
        swdiag_obj_db_unlock();
        return;
    }

    swdiag_obj_delete(obj);
    swdiag_obj_db_unlock();
}

void swdiag_rule_enable (const char *rule_name,
                         const char *instance_name)
{
    swdiag_api_rule_enable_guts(rule_name, instance_name, FALSE); 
}

void swdiag_api_rule_enable_guts (const char *rule_name,
                                  const char *instance_name,
                                  boolean cli)
{
    obj_t *obj;
    const char fnstr[] = "Enable rule";
    char *instance_copy = NULL;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_error("%s '%s' - unknown rule", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, rule_name);
            swdiag_obj_db_unlock();
            return;
        }

        instance = swdiag_obj_instance_by_name(obj, instance_copy);
        
        if (!instance) {
            instance = swdiag_api_instance_create(obj, instance_copy);
            if (!instance) {
                swdiag_error("Invalid instance name '%s:%s'", rule_name, instance_copy);
                if (instance_copy) {
                    free(instance_copy);
                }
                swdiag_obj_db_unlock();
                return;
            }
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_CREATED:

        if (!instance_name) {
            /*
             * No instance name given, enable all the instances if any
             * on this action
             */
            do {
                if (!cli) {
                    instance->default_state = OBJ_STATE_ENABLED;
                } else {
                    instance->cli_state = OBJ_STATE_ENABLED;
                }

                if (instance->cli_state != OBJ_STATE_DISABLED) {
                    /*
                     * If the CLI has set the state to disabled then
                     * Don't allow it to be enabled from the API.
                     */
                    instance->state = OBJ_STATE_ENABLED;
                }

            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just enable the named instance
             */    
            if (!cli) {
                instance->default_state = OBJ_STATE_ENABLED;
            } else {
                instance->cli_state = OBJ_STATE_ENABLED;
            }

            if (instance->cli_state != OBJ_STATE_DISABLED) {
                /*
                 * If the CLI has set the state to disabled then
                 * Don't allow it to be enabled from the API.
                 */
                instance->state = OBJ_STATE_ENABLED;
            }
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_ENABLED;
        } else {
            instance->cli_state = OBJ_STATE_ENABLED;
        }
        break;
    default:
        swdiag_error("%s '%s' in the wrong state", fnstr, rule_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

/*
 * Revert to default state.
 */
void swdiag_api_rule_default (const char *rule_name,
                              const char *instance_name)

{
    obj_t *obj;
    const char fnstr[] = "Default rule";
    char *instance_copy = NULL;
    obj_instance_t *instance;
    obj_rule_t *rule;

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();

    obj = swdiag_obj_get_by_name_unconverted(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    rule = obj->t.rule;

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, rule_name); 
            swdiag_obj_db_unlock();
            return;
        }

        instance = swdiag_obj_instance_by_name(obj, instance_copy);

        if (!instance) {
            swdiag_error("Invalid instance name '%s:%s'", rule_name, instance_copy);
            if (instance_copy) {
                free(instance_copy);
            }
            swdiag_obj_db_unlock();
            return;
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_CREATED:

        if (!instance_name) {
            /*
             * No instance name given, default all the instances if any
             * on this rule
             */
            rule->operator = rule->default_operator;
            rule->op_n = rule->default_op_n;
            rule->op_m = rule->default_op_m;

            do {
                if (instance->state == OBJ_STATE_ENABLED ||
                    instance->state == OBJ_STATE_DISABLED) {
                    instance->state = instance->default_state;
                }
                instance->cli_state = OBJ_STATE_INITIALIZED;
            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just default the named instance
             */  
            if (instance->state == OBJ_STATE_ENABLED ||
                instance->state == OBJ_STATE_DISABLED) {
                instance->state = instance->default_state;
            }
            instance->cli_state = OBJ_STATE_INITIALIZED;
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference, and if it is
         * being defaulted but not yet created it should be safe to
         * delete it - it will be created again later.
         */
        if (rule->operator == rule->default_operator &&
            rule->op_n == rule->default_op_n &&
            rule->op_m == rule->default_op_m) {
            swdiag_obj_delete(obj);
        } else {
            instance->cli_state = OBJ_STATE_INITIALIZED;
        }
        break;
    default:
        swdiag_error("%s '%s' in the wrong state", fnstr, rule_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}


void swdiag_rule_disable (const char *rule_name, 
                          const char *instance_name)
{
    swdiag_api_rule_disable_guts(rule_name, instance_name, FALSE); 
}

void swdiag_api_rule_disable_guts (const char *rule_name, 
                                   const char *instance_name,
                                   boolean cli)
{
    obj_t *obj;
    const char fnstr[] = "Disable rule";
    char *instance_copy = NULL;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }

    if (instance_name && *instance_name == '\0') {
        swdiag_error("%s - bad instance_name", fnstr);
        return; 
    }

    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_error("%s '%s' - unknown rule", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, rule_name);
            swdiag_obj_db_unlock();
            return;
        }
    
        instance = swdiag_obj_instance_by_name(obj, instance_copy);
        
        if (!instance) {
            instance = swdiag_api_instance_create(obj, instance_copy);
            if (!instance) {
                swdiag_error("Invalid instance name '%s:%s'", rule_name, instance_copy);
                if (instance_copy) {
                    free(instance_copy);
                }
                swdiag_obj_db_unlock();
                return;
            }
        }
    } else {
        instance = &obj->i;
    }

    switch (instance->state) {
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_CREATED:
        if (!instance_name) {
            /*
             * No instance name given, disable all the instances if any
             * on this action
             */
            do {
                if (!cli) {
                    instance->default_state = OBJ_STATE_DISABLED;
                } else {
                    instance->cli_state = OBJ_STATE_DISABLED;
                }

                if (instance->cli_state != OBJ_STATE_ENABLED) {
                    /*
                     * If the CLI has set the state to enabled then
                     * Don't allow it to be disabled from the API.
                     */
                    instance->state = OBJ_STATE_DISABLED;
                }
            } while ((instance = instance->next) != NULL);
        } else {
            /*
             * Just disable the named instance
             */    
            if (!cli) {
                instance->default_state = OBJ_STATE_DISABLED;
            } else {
                instance->cli_state = OBJ_STATE_DISABLED;
            }

            if (instance->cli_state != OBJ_STATE_ENABLED) {
                /*
                 * If the CLI has set the state to enabled then
                 * Don't allow it to be disabled from the API.
                 */
                instance->state = OBJ_STATE_DISABLED;
            }
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_DISABLED;
        } else {
            instance->cli_state = OBJ_STATE_DISABLED;
        }
        break;
    default:
        swdiag_error("%s '%s'", fnstr, rule_name);
        break;
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
}

void swdiag_rule_set_description (const char *rule_name,
                                  const char *description)
{
    obj_t *obj;
    const char fnstr[] = "Set description for rule";

    /*
     * Sanity check client params
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create rule and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (!dup_string(&obj->description, description)) {
        swdiag_error("%s '%s' - alloc '%s'", fnstr, rule_name, description);
    }
    swdiag_obj_db_unlock();
}

void swdiag_rule_set_severity (const char *rule_name,
                               swdiag_severity_t severity)
{ 
    obj_rule_t *rule;
    obj_t *obj;
    const char fnstr[] = "Set severity for rule";

    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create rule and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * If changing the severity on an existing rule then make sure that
     * if the rule is currently failing that the health is updated to
     * reflect this change.
     *
     * The same is true of adding and removing objects from a component.
     * ALl of which comes under the banner of health propagation.
     *
     * TODO
     */
    rule = obj->t.rule;
    rule->severity = severity;
    swdiag_obj_db_unlock();
}

/*******************************************************************
 * External API for components
 *******************************************************************/

void swdiag_comp_create (const char *component_name) 
{   
    obj_comp_t *comp;
    obj_t *obj;
    const char fnstr[] = "Create component";

    /*
     * Sanity check client params
     */
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad component_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create component and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(component_name, OBJ_TYPE_COMP);
    if (!obj) { 
        swdiag_error("%s '%s'", fnstr, component_name);
        swdiag_obj_db_unlock();
        return;
    }

    comp = obj->t.comp;
    comp->health = 1000;
    comp->confidence = 1000;

    /*
     * We should inherit the state of our parent object if it has been
     * configured to be something different, which will most of the
     * time be the System component. That way if diags are disabled it
     * "sticks" with new components.
     */
    if (obj->parent_comp && 
        obj->parent_comp->obj->i.state != obj->parent_comp->obj->i.default_state) {
        obj->i.state = obj->parent_comp->obj->i.state;
        
    } else {
        obj->i.state = default_obj_state;
    }
    obj->i.default_state = default_obj_state;

    /*
     * Also check whether this component has been explicitly configured to 
     * another state - and that should take priority.
     */
    if (obj->i.cli_state != OBJ_STATE_INITIALIZED) {
        obj->i.state = obj->i.cli_state;
    }

    swdiag_obj_db_unlock();
}

/*
 * swdiag_api_comp_contains()
 *
 * Add an object to a component.
 */
void swdiag_api_comp_contains (obj_t *parent, obj_t *child)
{
    obj_t *obj;

    /*
     * Loop detection.
     *
     * Search the child containment for any reference to the parent,
     * if found then linking the two would create a loop.
     */
    if (child->type == OBJ_TYPE_COMP) {
        obj = swdiag_comp_get_first_contained(child->t.comp, OBJ_TYPE_COMP);
        do {
            if (obj == parent) {
                swdiag_error("%s - '%s' references parent '%s', containment would create a loop",
                             "comp contains", child->i.name, parent->i.name);
                return;
            }
            obj = swdiag_comp_get_next_contained(child->t.comp, obj, 
                                                 OBJ_TYPE_COMP);
        } while (obj);
    }

    /* 
     * remove child component from a parent component if necessary,
     * including from the components top and bottom lists since it
     * is no longer in that component.
     */
    if (child->parent_comp) {
        swdiag_list_remove(child->parent_comp->top_depend, child);
        swdiag_list_remove(child->parent_comp->bottom_depend, child);
        swdiag_obj_unlink_from_comp(child);
    }

    /* add child component to the parent component */
    swdiag_obj_comp_link_obj(parent->t.comp, child);

    if (child->type == OBJ_TYPE_RULE || child->type == OBJ_TYPE_COMP) {
        /* 
         * Each component tracks the object dependencies within it so
         * that should another object depend on the component we know
         * which objects it should really depend on.
         *
         * To this end we maintain the "top_depend" and "bottom_depend" 
         * attributes of the component.
         *
         * Should this child have *any* parent dependencies within this
         * component then it is not at the top of the component tree.
         *
         * Should this child have *any* child dependencies within this
         * component then it is not at the bottom of the component tree.
         *
         * nb. I'm structured the code the odd way below because
         * that's how my mind works, I find negative logic confuses
         * me.
         */
        if (swdiag_depend_found_comp(child->parent_depend, parent->t.comp)) {
            /*
             * The child object has a parent dependency that is also within
             * this component. So no need to add it to the parent top_depend
             */
        } else {
            /*
             * This child is currently accessable from the top of the
             * component (should you draw a tree with the component a
             * circle around part of the tree).
             */
            swdiag_list_add(parent->t.comp->top_depend, child);
        }
        
        if (swdiag_depend_found_comp(child->child_depend, parent->t.comp)) {
            /*
             * The child object has a child dependency that is also within
             * this component. So no need to add it to the parent child_depend
             */
        } else {
            /*
             * This child is currently accessable from the bottom of
             * the component (should you draw a tree with the
             * component a circle around part of the tree).
             */
            swdiag_list_add(parent->t.comp->bottom_depend, child);
        }
    }
}

void swdiag_comp_contains (const char *parent_component_name,
                           const char *child_object_name)
{
    obj_t *parent, *child;
    const char fnstr[] = "Contains for component";

    /*
     * Sanity check client params
     */
    if (BADSTR(parent_component_name)) {
        swdiag_error("%s - bad parent_component_name", fnstr);
        return;
    }
    if (BADSTR(child_object_name)) {
        swdiag_error("%s '%s' - bad child_object_name",
            fnstr, parent_component_name);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create components and set defaults if applicable
     */  
    parent = swdiag_api_get_or_create(parent_component_name, OBJ_TYPE_COMP);

    if (!parent) {
        swdiag_error("%s '%s' - creating parent", fnstr, parent_component_name);
        swdiag_obj_db_unlock();
        return;
    }
    if (parent->type != OBJ_TYPE_COMP) {
        swdiag_error("%s '%s'- wrong parent type %s",
                     fnstr, parent_component_name, 
                     swdiag_obj_type_str(parent->type));
        swdiag_obj_db_unlock();
        return;
    }    

    child = swdiag_api_get_or_create(child_object_name, OBJ_TYPE_ANY);

    if (!child) {
        swdiag_error("%s '%s' - creating child '%s'",
                     fnstr, parent_component_name, child_object_name);
        swdiag_obj_db_unlock();
        return;
    }

    swdiag_api_comp_contains(parent, child);

    swdiag_obj_db_unlock();
}

void swdiag_comp_contains_many (const char *parent_component_name,
                                const char *child_object_name,
                                ...)
{
    va_list argp;
    const char fnstr[] = "Set many contains for component";

    /*
     * Sanity check client params
     */
    if (BADSTR(parent_component_name)) {
        swdiag_error("%s - bad parent_component_name", fnstr);
        return;
    }
    if (BADSTR(child_object_name)) {
        swdiag_error("%s '%s'- bad child_object_name",
            fnstr, parent_component_name);
        return;
    }

    swdiag_comp_contains(parent_component_name, child_object_name);

    va_start(argp, child_object_name);
    child_object_name = va_arg(argp, char *);
    while (child_object_name) {
        swdiag_comp_contains(parent_component_name, child_object_name);

        child_object_name = va_arg(argp, char *);
    }
    va_end(argp);
}

void swdiag_comp_delete (const char *component_name)
{
    const char fnstr[] = "Delete component";
    obj_t *obj;

    /*
     * Sanity check client params
     */
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad component_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    obj = swdiag_obj_get_by_name_unconverted(component_name, OBJ_TYPE_ANY);
    if (!obj) {
        swdiag_obj_db_unlock();
        return; /* no object to delete is not an error */
    }
    if (obj->type != OBJ_TYPE_COMP) {
        swdiag_error("%s '%s' - bad type (%s)",
            fnstr, component_name, swdiag_obj_type_str(obj->type));
        swdiag_obj_db_unlock();
        return;
    }
    /*
     * Deleting a component does not delete all of its member objects,
     * they are created individually, they should be deleted individually
     */
    swdiag_obj_delete(obj);
    swdiag_obj_db_unlock();
}

void swdiag_api_comp_set_context (const char *component_name,
                                  void *context)
{
    obj_t *obj;
    const char fnstr[] = "Set context for component";

    /*
     * Sanity check client params
     */
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad component_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create component and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(component_name, OBJ_TYPE_COMP);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, component_name); 
        swdiag_obj_db_unlock();
        return;
    }

    obj->i.context = context; 
    swdiag_obj_db_unlock();
}

void *swdiag_api_comp_get_context (const char *component_name)
{
    obj_t *obj;
    void *context = NULL;
    const char fnstr[] = "Get context for component";

    /*
     * Sanity check client params
     */
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad component_name", fnstr);
        return (NULL);
    }
    swdiag_obj_db_lock();
    /*
     * Get the component and return context. It is not an error to get context
     * for unknown component, we just return NULL in this case.
     */
    obj = swdiag_obj_get_by_name_unconverted(component_name, OBJ_TYPE_COMP);
    if (obj) {
        context = obj->i.context;
    }
    swdiag_obj_db_unlock();
    return (context);
}

void swdiag_comp_enable (const char *comp_name)
{
    swdiag_api_comp_enable_guts(comp_name, FALSE);
}

/*
 * swdiag_comp_enable()
 *
 * Partial implementation - need to enable all objects within the 
 * component as well
 */
void swdiag_api_comp_enable_guts (const char *comp_name,
                                  boolean cli)
{
    obj_t *comp_obj, *obj;
    const char fnstr[] = "Enable comp";
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(comp_name)) {
        swdiag_error("%s - bad comp_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    comp_obj = swdiag_api_get_or_create(comp_name, OBJ_TYPE_COMP);
    if (!swdiag_obj_validate(comp_obj, OBJ_TYPE_COMP)) {
        swdiag_error("%s '%s' - unknown", fnstr, comp_name); 
        swdiag_obj_db_unlock();
        return;
    }

    instance = &comp_obj->i;

    switch (instance->state) {
    case OBJ_STATE_DISABLED:
    case OBJ_STATE_CREATED:
        if (!cli) {
            instance->default_state = OBJ_STATE_ENABLED;
        } else {
            instance->cli_state = OBJ_STATE_ENABLED;
        }

        if (instance->cli_state != OBJ_STATE_DISABLED) {
            /*
             * If the CLI has set the state to disabled then
             * Don't allow it to be enabled from the API.
             */
            instance->state = OBJ_STATE_ENABLED;
        }
        
        /* FALLTHRU */
    case OBJ_STATE_ENABLED:
        /*
         * Ensure that all member objects are enabled as well
         */
        obj = swdiag_comp_get_first_contained(comp_obj->t.comp, OBJ_TYPE_ANY);

        while (obj) {
            instance = &obj->i;
            while (instance) {
                if (instance->state == OBJ_STATE_DISABLED ||
                    instance->state == OBJ_STATE_CREATED) {
                    if (cli) {
                        /*
                         * Wipe out the cli_state on sub objects
                         */
                        instance->cli_state = OBJ_STATE_INITIALIZED;
                    }
                    
                    if (instance->cli_state != OBJ_STATE_DISABLED) {
                        /*
                         * Only set the state on sub-objects if they
                         * are not explicitly configured to be
                         * disabled from the CLI and if the parent
                         * component is enabled (this allows the
                         * parent component to have a CLI override on
                         * subobjects).
                         */
                        if (obj->parent_comp) {
                            if (obj->parent_comp->obj->i.state == OBJ_STATE_ENABLED) { 
                                instance->state = OBJ_STATE_ENABLED;
                            }
                        } else {
                            instance->state = OBJ_STATE_ENABLED;
                        }
                    }
                }

                if (!cli) {
                    instance->default_state = OBJ_STATE_ENABLED;
                } 

                if (obj->type == OBJ_TYPE_TEST &&
                    obj->t.test->type == OBJ_TEST_TYPE_POLLED &&
                    instance->state == OBJ_STATE_ENABLED) {
                    swdiag_sched_add_test(instance, FALSE);
                }
                instance = instance->next;
            } 
            obj = swdiag_comp_get_next_contained(comp_obj->t.comp, obj, OBJ_TYPE_ANY);
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This component must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_ENABLED;
        } else {
            instance->cli_state = OBJ_STATE_ENABLED;
        }
        break;
    default:
        swdiag_error("%s '%s'", fnstr, comp_name);
        break;
    } 
    swdiag_obj_db_unlock();
}

/*
 * Revert to default state.
 */
void swdiag_api_comp_default (const char *comp_name)
{
    obj_t *obj;
    const char fnstr[] = "Default comp";
    obj_comp_t *comp;
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(comp_name)) {
        swdiag_error("%s - bad comp name", fnstr);
        return;
    }

    swdiag_obj_db_lock();

    obj = swdiag_obj_get_by_name_unconverted(comp_name, OBJ_TYPE_COMP);
    if (!obj) {
        swdiag_error("%s '%s' - unknown", fnstr, comp_name);
        swdiag_obj_db_unlock();
        return;
    }

    comp = obj->t.comp;

    instance = &obj->i;

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_DISABLED:
        instance->state = instance->default_state;
        /* FALLTHRU */
    case OBJ_STATE_CREATED:
        /*
         * Clear any configured CLI state
         */
        instance->cli_state = OBJ_STATE_INITIALIZED;
        
        /*
         * For each object within this component revert its state back to
         * its default from any configured state.
         */
        obj = swdiag_comp_get_first_contained(comp, OBJ_TYPE_ANY);

        while (obj) {
            /*
             * default any configuration
             */
            switch(obj->type) {
            case OBJ_TYPE_TEST:
                swdiag_api_test_default(obj->i.name, NULL);
                break;
            case OBJ_TYPE_RULE:
                swdiag_api_rule_default(obj->i.name, NULL);
                break;    
            case OBJ_TYPE_ACTION:
                swdiag_api_action_default(obj->i.name, NULL);
                break;
            case OBJ_TYPE_COMP:
                if (swdiag_obj_validate(obj, OBJ_TYPE_COMP)) {
                    obj_comp_t *comp2 = obj->t.comp;
                    if (!comp2->nones &&
                        !comp2->tests &&
                        !comp2->actions &&
                        !comp2->rules &&
                        !comp2->comps) {
                        /*
                         * Doesn't contain anything, and not yet
                         * created, delete it.
                         */
                        swdiag_obj_delete(obj);
                    }
                    /*
                     * Clear the sub-components CLI configuration and
                     * revert it to the default state from the API.
                     */
                    if (obj->i.state == OBJ_STATE_ENABLED ||
                        obj->i.state == OBJ_STATE_DISABLED) {
                        obj->i.state = obj->i.default_state;
                    }
                    obj->i.cli_state = OBJ_STATE_INITIALIZED;
                }
                break;
            case OBJ_TYPE_NONE:
            case OBJ_TYPE_ANY:
                break;
            }

            if (swdiag_obj_validate(obj, OBJ_TYPE_ANY)) {
                obj = swdiag_comp_get_next_contained(comp, obj, OBJ_TYPE_ANY);
            } else {
                /*
                 * Must have deleted the prior object, we need to start at 
                 * the beginning now, just in case. This is pretty rare, and
                 * does mean that we will be shuffling everything in the 
                 * schedulars again.
                 */
                obj = swdiag_comp_get_first_contained(comp, OBJ_TYPE_ANY);
            }
        }

        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This instance must be a forward reference, and if it is
         * being defaulted but not yet created it should be safe to
         * delete it - it will be created again later.
         */
        if (!comp->nones &&
            !comp->tests &&
            !comp->actions &&
            !comp->rules &&
            !comp->comps) {
            /*
             * Doesn't contain anything, and not yet created, delete it.
             */
            swdiag_obj_delete(obj);
        } else {
            instance->cli_state = OBJ_STATE_INITIALIZED;
        }
        break;
    default:
        swdiag_error("%s '%s' in the wrong state", fnstr, comp_name);
        break;
    }

    swdiag_obj_db_unlock();
}


void swdiag_comp_disable (const char *comp_name)
{
    swdiag_api_comp_disable_guts(comp_name, FALSE); 
}

void swdiag_api_comp_disable_guts (const char *comp_name, boolean cli)
{
    obj_t *comp_obj, *obj;
    const char fnstr[] = "Disable comp";
    obj_instance_t *instance;

    /*
     * Sanity check client params
     */
    if (BADSTR(comp_name)) {
        swdiag_error("%s - bad comp_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    comp_obj = swdiag_api_get_or_create(comp_name, OBJ_TYPE_COMP);
    if (!swdiag_obj_validate(comp_obj, OBJ_TYPE_COMP)) {
        swdiag_error("%s '%s' - unknown", fnstr, comp_name); 
        swdiag_obj_db_unlock();
        return;
    }

    instance = &comp_obj->i;

    switch (instance->state) {
    case OBJ_STATE_ENABLED:
    case OBJ_STATE_CREATED:
        if (!cli) {
            instance->default_state = OBJ_STATE_DISABLED;
        } else {
            instance->cli_state = OBJ_STATE_DISABLED;
        }

        if (instance->cli_state != OBJ_STATE_ENABLED) {
            /*
             * If the CLI has set the state to enabled then
             * Don't allow it to be disabled from the API.
             */
            instance->state = OBJ_STATE_DISABLED;
        }

        /* FALLTHRU */
    case OBJ_STATE_DISABLED:
        /*
         * Ensure that all member objects are disabled as well
         */
        obj = swdiag_comp_get_first_contained(comp_obj->t.comp, OBJ_TYPE_ANY);

        while (obj) {
            instance = &obj->i;
            while (instance) {
                if (instance->state == OBJ_STATE_ENABLED ||
                    instance->state == OBJ_STATE_CREATED) {
                    if (cli) {
                        /*
                         * Wipe out any existing CLI state within the 
                         * component
                         */
                        instance->cli_state = OBJ_STATE_INITIALIZED;
                    }
                    if (instance->cli_state != OBJ_STATE_ENABLED) {
                        /*
                         * Only disable a sub-object if not explicitly
                         * enabled from the CLI and the parent comp is
                         * disabled.
                         */
                        if (obj->parent_comp) {
                            if (obj->parent_comp->obj->i.state == OBJ_STATE_DISABLED) { 
                                instance->state = OBJ_STATE_DISABLED; 
                            }
                        } else {
                            instance->state = OBJ_STATE_DISABLED;
                        }
                    }
                }
                if (!cli) {
                    instance->default_state = OBJ_STATE_DISABLED;
                } 

                if (obj->type == OBJ_TYPE_TEST &&
                    obj->t.test->type == OBJ_TEST_TYPE_POLLED &&
                    instance->state == OBJ_STATE_DISABLED) {
                    swdiag_sched_remove_test(instance);
                }
                instance = instance->next;
            } 
            obj = swdiag_comp_get_next_contained(comp_obj->t.comp, obj, OBJ_TYPE_ANY);
        }
        break;
    case OBJ_STATE_INITIALIZED:
        /*
         * This component must be a forward reference. Don't update the
         * actual state since it hasn't actually been created as yet.
         */
        if (!cli) {
            instance->default_state = OBJ_STATE_DISABLED;
        } else {
            instance->cli_state = OBJ_STATE_DISABLED;
        }
        break;
    default:
        swdiag_error("%s '%s'", fnstr, comp_name);
        break;
    } 
    swdiag_obj_db_unlock();
}

void swdiag_comp_set_description (const char *component_name,
                                  const char *description)
{
    obj_t *obj;
    const char fnstr[] = "Set description for component";

    /*
     * Sanity check client params
     */
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad component_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create test and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(component_name, OBJ_TYPE_COMP);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, component_name); 
        swdiag_obj_db_unlock();
        return;
    }

    if (!dup_string(&obj->description, description)) {
        swdiag_error("%s '%s' - alloc '%s'", fnstr, component_name, description);
    }
    swdiag_obj_db_unlock();
}

/*******************************************************************
 * External API for health
 *******************************************************************/

void swdiag_health_set (const char *component_name,
                        unsigned int health)
{
    obj_comp_t *comp;
    obj_t *obj;
    const char fnstr[] = "Set health for component";

    /*
     * Sanity check client params
     */
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad component_name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    /*
     * Get or create component and set defaults if applicable
     */
    obj = swdiag_api_get_or_create(component_name, OBJ_TYPE_COMP);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, component_name); 
        swdiag_obj_db_unlock();
        return;
    }

    comp = obj->t.comp;
    swdiag_seq_comp_set_health(comp, (health*10));
    swdiag_trace(obj->i.name, "Health '%d' on component '%s'", health, 
                 component_name);  
    swdiag_obj_db_unlock();
}

unsigned int swdiag_health_get (const char *component_name)
{
    obj_comp_t *comp;
    obj_t *obj;
    unsigned int health = 0;
    const char fnstr[] = "Get health for component";

    /*
     * Sanity check client params
     */
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad component_name", fnstr);
        return (0);
    }
    swdiag_obj_db_lock();
    obj = swdiag_obj_get_by_name_unconverted(component_name, OBJ_TYPE_COMP);
    if (!obj) {
        /* not an error to get health for unknown component - return 0 */ 
        swdiag_obj_db_unlock();
        return (0);
    }

    comp = obj->t.comp;
    health = comp->health;
    swdiag_obj_db_unlock();
    return (health/10);
}

/*******************************************************************
 * Instance
 *******************************************************************/
void swdiag_instance_create (const char *object_name, 
                             const char *instance_name,
                             void *context)
{
    obj_t *obj;
    obj_instance_t *instance;
    char *instance_copy;

    const char fnstr[] = "Create an instance of an object";

    if (BADSTR(object_name)) {
        swdiag_error("%s - bad object name", fnstr);
        return;
    }

    if (BADSTR(instance_name)) {
        swdiag_error("%s - bad instance name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(object_name, OBJ_TYPE_ANY);
    if (!obj) {
        swdiag_error("%s '%s'", fnstr, object_name);  
        swdiag_obj_db_unlock();
        return;
    }

    instance_copy = swdiag_api_convert_name(instance_name);
    if (!instance_copy) {
        swdiag_error("%s memory allocation failure for '%s'", 
                     fnstr, object_name);
        swdiag_obj_db_unlock();
        return;
    }

    instance = swdiag_obj_instance_by_name(obj, instance_copy);

    if (instance) {
        swdiag_error("%s '%s' instance '%s' already exists", fnstr, 
                     instance_name, object_name);
        free(instance_copy);
        swdiag_obj_db_unlock();
        return;
    }

    /*
     * Instance doesn't already exist, create it and attatch it to
     * the object - it should inherit the initial state of the
     * owning object.
     */
    instance = swdiag_api_instance_create(obj, instance_copy);

    instance->context = context;

    if (obj->type == OBJ_TYPE_TEST &&
        obj->t.test->type == OBJ_TEST_TYPE_POLLED) {
        swdiag_sched_add_test(instance, FALSE);
    }

    if (instance->state == OBJ_STATE_INITIALIZED) {
        instance->state = OBJ_STATE_CREATED;
    }

    free(instance_copy);

    swdiag_obj_db_unlock();
}

/*
 * swdiag_instance_delete()
 *
 * Delete an instance of an object if found.
 */
void swdiag_instance_delete (const char *object_name, 
                             const char *instance_name)
{
    obj_t *obj;
    obj_instance_t *instance;
    char *instance_copy;

    const char fnstr[] = "Create an instance of an object";

    if (BADSTR(object_name)) {
        swdiag_error("%s - bad object name", fnstr);
        return;
    }

    if (BADSTR(instance_name)) {
        swdiag_error("%s - bad instance name", fnstr);
        return;
    }
    swdiag_obj_db_lock();
    obj = swdiag_obj_get_by_name_unconverted(object_name, OBJ_TYPE_ANY);
    if (!obj) { 
        swdiag_obj_db_unlock();
        return;
    }

    instance_copy = swdiag_api_convert_name(instance_name);
    if (!instance_copy) {
        swdiag_error("%s memory allocation failure for '%s'", 
                     fnstr, object_name);
        swdiag_obj_db_unlock();
        return;
    }

    instance = swdiag_obj_instance_by_name(obj, instance_copy);

    if (!instance) {
        /*
         * No instance with that name on that object
         */ 
        free(instance_copy);
        swdiag_obj_db_unlock();
        return;
    }

    if (instance->obj && instance->obj->type == OBJ_TYPE_TEST) {
        /*
         * remove from schedular if a polled test
         */
        swdiag_sched_remove_test(instance);
    } 
    swdiag_obj_instance_delete(instance);  
    free(instance_copy);
    swdiag_obj_db_unlock();
}
/*******************************************************************
 * External API for miscellaneous utilities
 *******************************************************************/

static boolean i_am_slave = FALSE;

/*
 * I want to look at this again - soon since it will wrap since the
 * tracing is using this and using up all the entries in the background
 */
#define NBR_MAKE_NAME_BUFFERS 400
#define MAKE_NAME_BUFFER_SIZE 200
const char *swdiag_api_make_name (const char *prefix, const char *suffix)
{
    static char *buffers[NBR_MAKE_NAME_BUFFERS];
    static int next = 0;
    static int firsttime = 1;
    char *retval = 0;
    int i;
    static xos_critical_section_t *lock = NULL;

    if (!lock) {
        lock = swdiag_xos_critical_section_create();
    }
    swdiag_xos_critical_section_enter(lock);

    if (firsttime) {
        for (i=0; i< NBR_MAKE_NAME_BUFFERS; i++) {
            buffers[i] = malloc(MAKE_NAME_BUFFER_SIZE);
        }
        firsttime = 0;
    }

    if (prefix && suffix) {
        snprintf(buffers[next], MAKE_NAME_BUFFER_SIZE, "%s:%s",
                 prefix, suffix);
        
        retval = buffers[next];
        
        next++;
    
        if (next == NBR_MAKE_NAME_BUFFERS) {
            next = 0;
        }
    }
    swdiag_xos_critical_section_exit(lock);
    return(retval);
}

void swdiag_set_master (void)
{
    swdiag_obj_db_lock();
    if (i_am_slave) {
        swdiag_trace(NULL, "Changing from SW Diagnostics Slave to Master");
        swdiag_xos_slave_to_master();
    } else {
        swdiag_trace(NULL, "This is the SW Diagnostics Master"); 
        swdiag_xos_register_as_master();
    }
    swdiag_obj_db_unlock();
}

void swdiag_set_slave (const char *component_name)
{
    if (BADSTR(component_name)) {
        swdiag_error("set slave - bad component_name");
        return;
    }
    swdiag_obj_db_lock();
    if (!i_am_slave) {
        swdiag_trace(component_name, "This is a SW Diagonstics Slave '%s'", 
                     component_name);
        i_am_slave = TRUE;
        
        swdiag_xos_register_with_master(component_name);
    } else {
        swdiag_trace(component_name, "Ignoring double registration for '%s'", 
                     component_name);
    }
    swdiag_obj_db_unlock();
}


/* Enable or Disable test notification.
 * 
 * API which can be used if user wishes to receive notification from swdiag if
 * result changes either for test.
 * 
 */
boolean swdiag_notify_test_result (const char *test_name, 
                                   const char *instance_name,
                                   boolean enable_notification)
{
    obj_t *obj;
    obj_instance_t *instance;
    char *instance_copy = NULL;
    const char fnstr[] = "Notify Test Result";

    /*
     * Sanity check client params, instance *is* allowed to be NULL
     */
    if (BADSTR(test_name)) {
        swdiag_error("%s - bad test name", fnstr);
        return (FALSE);
    }

    swdiag_obj_db_lock();

    /*
     * Get or create test.
     */
    obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!obj) {
        swdiag_error("%s - Object not created or found for '%s'", 
                    fnstr, test_name);
        swdiag_obj_db_unlock();
        return (FALSE);
    }

    if (BADSTR(instance_name)) {
        /* Instance name is NULL, turn on object flag */  
        obj->i.flags.any |= OBJ_FLAG_NOTIFY;
        swdiag_obj_db_unlock();
        return (TRUE);
    }
    
    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, test_name);
            swdiag_obj_db_unlock();
            return (FALSE);
        }
    }

    instance = swdiag_obj_instance_by_name(obj, instance_copy);
    if (instance) { 
        instance->flags.any |= OBJ_FLAG_NOTIFY;
    } else {
        /* create forward reference instance */
        instance = swdiag_api_instance_create(obj, instance_copy);
        
        if (instance) {
            instance->flags.any |= OBJ_FLAG_NOTIFY;
        }    
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
    return (TRUE);
}    

/* Enable or Disable rule notification.
 * 
 * API which can be used if user wishes to receive notification from swdiag if
 * result changes either for rule.
 * 
 */
boolean swdiag_notify_rule_result (const char *rule_name, 
                                   const char *instance_name,
                                   boolean enable_notification)
{
    obj_t *obj;
    obj_instance_t *instance;
    char *instance_copy = NULL;
    const char fnstr[] = "Notify Rule Result";

    /*
     * Sanity check client params, instance *is* allowed to be NULL
     */
    if (BADSTR(rule_name)) {
        swdiag_error("%s - bad rule name", fnstr);
        return (FALSE);
    }

    swdiag_obj_db_lock();

    /*
     * Get or create rule.
     */
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!obj) {
        swdiag_obj_db_unlock();
        swdiag_error("%s - Object not created or found for '%s'", 
            fnstr, rule_name);
        return (FALSE);
    }
    
    if (BADSTR(instance_name)) {
        /* Instance name is NULL, turn on object flag */  
        obj->i.flags.any |= OBJ_FLAG_NOTIFY;
        swdiag_obj_db_unlock();
        return (TRUE);
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, rule_name);
            swdiag_obj_db_unlock();
            return (FALSE);
        }
    }

    instance = swdiag_obj_instance_by_name(obj, instance_copy);
    if (instance) { 
        instance->flags.any |= OBJ_FLAG_NOTIFY;
    } else {
        /* create forward reference instance */
        instance = swdiag_api_instance_create(obj, instance_copy);
        if (instance) {
            instance->flags.any |= OBJ_FLAG_NOTIFY;
        }    
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
    return (TRUE);
}    

/* Enable or Disable action notification.
 * 
 * API which can be used if user wishes to receive notification from swdiag if
 * result changes either for action.
 * 
 */
boolean swdiag_notify_action_result (const char *action_name, 
                                    const char *instance_name,
                                    boolean enable_notification)
{
    obj_t *obj;
    obj_instance_t *instance;
    char *instance_copy = NULL;
    const char fnstr[] = "Notify Action Result";

    /*
     * Sanity check client params, instance *is* allowed to be NULL
     */
    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action name", fnstr);
        return (FALSE);
    }

    swdiag_obj_db_lock();

    /*
     * Get or create action.
     */
    obj = swdiag_api_get_or_create(action_name, OBJ_TYPE_ACTION);
    if (!obj) {
        swdiag_obj_db_unlock();
        swdiag_error("%s - Object not created for found for '%s'", 
            fnstr, action_name);
        return (FALSE);
    }
    
    if (BADSTR(instance_name)) {
        /* Instance name is NULL, turn on object flag */  
        obj->i.flags.any |= OBJ_FLAG_NOTIFY;
        swdiag_obj_db_unlock();
        return (TRUE);
    }

    if (instance_name) {
        instance_copy = swdiag_api_convert_name(instance_name);
        if (!instance_copy) {
            swdiag_error("%s memory allocation failure for '%s'", 
                         fnstr, action_name);
            swdiag_obj_db_unlock();
            return (FALSE);
        }
    }

    instance = swdiag_obj_instance_by_name(obj, instance_copy);
    if (instance) { 
        instance->flags.any |= OBJ_FLAG_NOTIFY;
    } else {
        /* create forward reference instance */
        instance = swdiag_api_instance_create(obj, instance_copy);
        if (instance) {
            instance->flags.any |= OBJ_FLAG_NOTIFY;
        }    
    }

    if (instance_copy) {
        free(instance_copy);
    }
    swdiag_obj_db_unlock();
    return (TRUE);
}    

/* Component health notification.
 * 
 * API which can be used if user wishes to receive notification from swdiag if
 * health of the component falls below certain threshold. 
 *
 */
boolean swdiag_component_health_notify (const char *component_name, 
                                        unsigned int lower_threshold,
                                        unsigned int upper_threshold)

{
    const char fnstr[] = "Comp Health notify";
    
    if (BADSTR(component_name)) {
        swdiag_error("%s - bad comp name", fnstr);
        return (FALSE);
    }

    swdiag_error("%s - Not implemented yet. Comp name (%s) Lower Threshold (%d)"
        " Upper threshold (%d)", fnstr, component_name, lower_threshold, 
        upper_threshold);
    return (TRUE);
}    

/* Execute the given action
 *
 *  This API should be called by client if it needs to take certain action which
 *  is internal to swdiag.
 */  
boolean swdiag_execute_action(const char *action_name,
                              const char *instance_name)
{
    const char fnstr[] = "Execute Action";
   
    /* Instance name can be NULL */

    if (BADSTR(action_name)) {
        swdiag_error("%s - bad action name", fnstr);
        return (FALSE);
    }

    swdiag_error("%s - Not implemented. Action Name (%s) Instance name (%s)", 
        fnstr, action_name, BADSTR(instance_name) ? "Null" : instance_name);

    return (TRUE);

}    


/*******************************************************************
 * Initialization
 *******************************************************************/

/*
 * swdiag_api_init()
 *
 * Initialise everything required for the API to work.
 */
void swdiag_api_init (void)
{
    obj_t *obj;

    swdiag_action_create(SWDIAG_ACTION_RELOAD,
                         reload,
                         NULL);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_ACTION_RELOAD, 
                                             OBJ_TYPE_ACTION);
    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_BUILT_IN;
    }

    swdiag_action_enable(SWDIAG_ACTION_RELOAD, NULL);
    
    swdiag_action_create(SWDIAG_ACTION_SWITCHOVER,
                         switchover,
                         NULL);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_ACTION_SWITCHOVER, 
                                             OBJ_TYPE_ACTION);
    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_BUILT_IN;
    }

    swdiag_action_enable(SWDIAG_ACTION_SWITCHOVER, NULL);

    swdiag_action_create(SWDIAG_ACTION_RELOAD_STANDBY,
                         reload_standby,
                         NULL);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_ACTION_RELOAD_STANDBY, 
                                             OBJ_TYPE_ACTION);
    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_BUILT_IN;
    }

    swdiag_action_enable(SWDIAG_ACTION_RELOAD_STANDBY, NULL);

    
    swdiag_action_create(SWDIAG_ACTION_SCHEDULED_RELOAD,
                         scheduled_reload,
                         NULL);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_ACTION_SCHEDULED_RELOAD, 
                                             OBJ_TYPE_ACTION);
    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_BUILT_IN;
    }

    swdiag_action_enable(SWDIAG_ACTION_SCHEDULED_RELOAD, NULL);
    
    swdiag_action_create(SWDIAG_ACTION_SCHEDULED_SWITCHOVER,
                         scheduled_switchover,
                         NULL);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_ACTION_SCHEDULED_SWITCHOVER, 
                                             OBJ_TYPE_ACTION);
    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_BUILT_IN;
    }


    swdiag_action_enable(SWDIAG_ACTION_SCHEDULED_SWITCHOVER, NULL);
    
    swdiag_action_create(SWDIAG_ACTION_NOOP,
                         noop,
                         NULL);

    obj = swdiag_obj_get_by_name_unconverted(SWDIAG_ACTION_NOOP,
                                             OBJ_TYPE_ACTION);
    if (obj) {
        obj->i.flags.any |= OBJ_FLAG_BUILT_IN;
        obj->i.flags.any |= OBJ_FLAG_SILENT;
    }

    swdiag_action_enable(SWDIAG_ACTION_NOOP, NULL);
}
