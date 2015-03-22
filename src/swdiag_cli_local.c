/* 
 * swdiag_cli_local.c - Internal interface to core CLI handlers.
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
 * Declare the API for OS independent CLI code to use when obtaining 
 * SW Diags CLI info from local SW Diags instances.
 *
 * This is all a bit quirky because of the way that some router OSs
 * handle their CLI. It really needs better documentation and examples.
 */

#include "swdiag_cli.h"
#include "swdiag_obj.h"
#include "swdiag_api.h"
#include "swdiag_util.h"
#include "swdiag_trace.h"
#include "swdiag_sequence.h"
#include "swdiag_cli_local.h"
#include "swdiag_cli_handle.h"
#include "swdiag_xos.h"

#define CLI_DAY_SEC 43200

/* How many handle to be freed in one attempt so that CPU is not consumed to free
 * huge count of handles
 */ 
#define CLI_HANDLE_FREE_COUNT 100 

typedef struct {
    int counter;
    swdiag_result_t result;
    long value;
} polled_test_context;    

static swdiag_list_t *handles_in_use = NULL;
static unsigned int next_handle_id = 1;   // MUST be randomised to make this unpredictable, so it can be used as a session id.

static unsigned int get_new_handle_id (void)
{
    return (next_handle_id++);  
}    

/* Wrapper for swdiag_list_find */
boolean swdiag_cli_local_handle_valid (cli_handle_t *handle) 
{
    return (swdiag_list_find(handles_in_use, handle));
}    

static cli_type_t swdiag_cli_instance_type_to_type (cli_type_t inst_type)
{
    cli_type_t type;
    if (inst_type == CLI_TEST_INSTANCE) {
        type = CLI_TEST;
    } else if (inst_type == CLI_RULE_INSTANCE) {
        type = CLI_RULE;
    } else if (inst_type == CLI_ACTION_INSTANCE) {
        type = CLI_ACTION;
    } else {
        type = CLI_UNKNOWN;
    }    
    return (type);
}    

void swdiag_cli_local_handle_set_in_use_flag (cli_handle_t *handle)
{
    if (handle) {
        handle->in_use = TRUE;
    }    
}    

void swdiag_cli_local_handle_reset_in_use_flag (cli_handle_t *handle)
{
    if (handle) {
        handle->in_use = FALSE;
    }    
}    

/*
 * Should the caller have an error and need to free this handle, they can
 * call this function. Note that other parties may still have references
 * to the handle, so this is dangerous. It may be better to leave it in 
 * place and leak the memory?
 *
 * Callers should only use this function when it is safe to do so, i.e. 
 * haven't communicated this handle with anyone as yet.
 */
boolean swdiag_cli_local_handle_free (cli_handle_t *handle)
{
    boolean val = FALSE;
    if (!handle || handle->in_use) {
        return (val);
    }

    /* Check if List is empty */ 
    if (!handles_in_use) {
        return (val);
    }

    /* Reset in_use flag before freeing cli handle. This can reset flags for
     * the object which is stored in handle. But the code also replaces
     * these objects with new objects as it traverses the list or object
     * database to display the contents. If those are not properly updated then
     * its a bug in the code.
     */
    if (handle->instance && swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        handle->instance->in_use = 0;
    }    
    if (handle->last_obj && swdiag_obj_validate(handle->last_obj, OBJ_TYPE_ANY)) {
        handle->last_obj->i.in_use = 0;
    }    
    if (handle->last_remote_obj && swdiag_obj_validate(handle->last_remote_obj, OBJ_TYPE_ANY)) {
        handle->last_remote_obj->i.in_use = 0;
    }    

    swdiag_xos_critical_section_enter(handles_in_use->lock);
    if (swdiag_list_remove(handles_in_use, handle)) {
        val = TRUE;
        free(handle);
    }
    swdiag_xos_critical_section_exit(handles_in_use->lock);
    return (val);
}

/*
 * Set and reset in_use flag as well assign instance to handle->instance
 */ 
static void handle_set_instance (cli_handle_t *handle, 
                                 obj_instance_t *instance)
{
    obj_instance_t *existing_instance;
    if (!handle) {
        return;
    }    
    existing_instance = handle->instance;
    if (existing_instance && 
        swdiag_obj_instance_validate(existing_instance, OBJ_TYPE_ANY) && 
        existing_instance->in_use) {
        existing_instance->in_use--;
    }
    handle->instance = instance;
    if (instance && swdiag_obj_instance_validate(instance, OBJ_TYPE_ANY)) {
        instance->in_use++;
    }
}    

static void handle_set_last_remote_obj (cli_handle_t *handle,
                                        obj_t *obj)
{
    obj_t *existing_obj;
    if (!handle) {
        return;
    }    
    existing_obj = handle->last_remote_obj;
    if (existing_obj && 
        swdiag_obj_validate(existing_obj, OBJ_TYPE_ANY) && 
        existing_obj->i.in_use) {
        existing_obj->i.in_use--;
    }
    handle->last_remote_obj = obj;
    if (obj && swdiag_obj_validate(obj, OBJ_TYPE_ANY)) {
        obj->i.in_use++;
    }
}    

static void handle_set_last_obj (cli_handle_t *handle,
                                 obj_t *obj)
{
    obj_t *existing_obj;
    if (!handle) {
        return;
    }    
    existing_obj = handle->last_obj;
    if (existing_obj && existing_obj->i.in_use) {
        existing_obj->i.in_use--;
    }
    handle->last_obj = obj;
    if (obj) {
        obj->i.in_use++;
    }
}    

void swdiag_cli_local_handle_set_remote_comp_obj (cli_handle_t *handle,
                                                  obj_t *obj)
{
    obj_t *existing_obj;
    if (!handle) {
        return;
    }    
    existing_obj = handle->remote_comp;
    if (existing_obj && existing_obj->i.in_use) {
        existing_obj->i.in_use--;
    }
    handle->remote_comp = obj;
    if (obj) {
        obj->i.in_use++;
    }
}    

static void handle_clean_up (cli_handle_t *handle)
{
    handle_set_instance(handle, NULL); 
    handle_set_last_obj(handle, NULL); 
    handle_set_last_remote_obj(handle, NULL); 
    swdiag_cli_local_handle_set_remote_comp_obj(handle, NULL); 
    swdiag_cli_local_handle_free(handle);
}    

/*
 * Check if the given object is local or remote. 
 */ 
boolean swdiag_cli_local_is_obj_remote (const char *name) 
{
    char *strptr;
    char location_name[SWDIAG_MAX_NAME_LEN];
    char s1[SWDIAG_MAX_NAME_LEN]; 
    obj_t *obj;
    boolean ret_flag = FALSE;
    
    if (!name || *name == '\0') {
        /* NULL name is a valid parameter, 
         * don't log an error 
         */
        return (ret_flag);
    }    
    
    swdiag_xos_sstrncpy(s1, name, SWDIAG_MAX_NAME_LEN);
    strptr = strtok(s1, delimiter); 

    /* if strptr is NULL, means the name could be a remote component name
     * without a path. EX: show diagnostic component name StandyRP. 
     */ 
    if (!strptr) {
       swdiag_xos_sstrncpy(location_name, name, SWDIAG_MAX_NAME_LEN);
    } else {
       swdiag_xos_sstrncpy(location_name, strptr, SWDIAG_MAX_NAME_LEN);
    }
  
    swdiag_obj_db_lock();
    obj = swdiag_obj_get_by_name_unconverted(location_name, OBJ_TYPE_COMP); 
    if (obj && obj->remote_location) {
        ret_flag = TRUE;
    } else {
        ret_flag = FALSE;
    }
    swdiag_debug(NULL, "Looked up %s, %s", location_name, ret_flag == TRUE ? "Remote" : "Local");
    swdiag_obj_db_unlock();
    return (ret_flag);
}    

/*
 * cli_to_rel_type()
 *
 * Convert a cli_type_t to a obj_rel_e
 */
static obj_rel_t cli_to_rel_type (cli_type_t type)
{
    switch (type) {
      case CLI_TEST:      return (OBJ_REL_TEST);
      case CLI_ACTION:    return (OBJ_REL_ACTION);
      case CLI_RULE:      return (OBJ_REL_RULE);
      case CLI_COMPONENT: return (OBJ_REL_COMP);
      default: /*errmsg*/ return (OBJ_REL_NONE);
    }
}

/*
 * cli_to_obj_type()
 *
 * Convert a cli_type_t to object type.
 */
static obj_type_t cli_to_obj_type (cli_type_t type)
{
    switch (type) {
      case CLI_TEST:      return (OBJ_TYPE_TEST);
      case CLI_ACTION:    return (OBJ_TYPE_ACTION);
      case CLI_RULE:      return (OBJ_TYPE_RULE);
      case CLI_COMPONENT: return (OBJ_TYPE_COMP);
      default: /*errmsg*/ return (OBJ_TYPE_NONE);
    }
}

/*
 * obj_type_to_cli()
 *
 * Convert a object type to cli type 
 */
static cli_type_t obj_type_to_cli (obj_type_t type)
{
    switch (type) {
      case OBJ_TYPE_TEST:      return (CLI_TEST);
      case OBJ_TYPE_ACTION:    return (CLI_ACTION);
      case OBJ_TYPE_RULE:      return (CLI_RULE);
      case OBJ_TYPE_COMP: return (CLI_COMPONENT);
      default: /*errmsg*/ return (CLI_UNKNOWN);
    }
}

/*
 * obj_state_to_cli()
 *
 * Convert a object state to cli state 
 */
static cli_state_t obj_state_to_cli (obj_state_t state)
{
    switch (state) {
    case OBJ_STATE_ALLOCATED:   return (CLI_STATE_ALLOCATED);
    case OBJ_STATE_INITIALIZED: return (CLI_STATE_INITIALIZED);
    case OBJ_STATE_CREATED:     return (CLI_STATE_CREATED);
    case OBJ_STATE_ENABLED:     return (CLI_STATE_ENABLED);
    case OBJ_STATE_DISABLED:    return (CLI_STATE_DISABLED);
    case OBJ_STATE_DELETED:     return (CLI_STATE_DELETED);
    case OBJ_STATE_INVALID:     return (CLI_STATE_INVALID);
    }
    return(CLI_STATE_INVALID);                            
}

/*
 * obj_test_type_to_cli_type()
 * Convert obj type test to Cli test type.
 */
static cli_test_type_t obj_test_type_to_cli_type (obj_test_type_t obj_type)
{
    switch (obj_type) {
        case OBJ_TEST_TYPE_POLLED: return (CLI_TEST_TYPE_POLLED);
        case OBJ_TEST_TYPE_NOTIFICATION: return (CLI_TEST_TYPE_NOTIFICATION);
        case OBJ_TEST_TYPE_ERRMSG: return (CLI_TEST_TYPE_ERRMSG);
    }    
    return (CLI_TEST_TYPE_INVALID);
}    



/* 
 * swdiag_check_nvgen()
 * 
 * Verify if the element info to be filtered.
 */ 
static boolean swdiag_check_nvgen (cli_type_t type, 
                                   cli_state_t cli_state, 
                                   cli_state_t def_state,
                                   unsigned int period, 
                                   unsigned int def_period,
                                   swdiag_rule_operator_t op,
                                   swdiag_rule_operator_t def_op)
{
    boolean nvgen_generate = FALSE;
    switch (type) {
    case CLI_TEST:
        if ((def_period != period) ||
            ((cli_state != CLI_STATE_INITIALIZED) && 
             (cli_state != def_state))) {
            nvgen_generate = TRUE;
        }    
        break;
    case CLI_RULE:
        if ((def_op != op) ||
            ((cli_state != CLI_STATE_INITIALIZED) && 
             (cli_state != def_state))) {
            nvgen_generate = TRUE;
        }    
        break;
    case CLI_ACTION:
    case CLI_ACTION_INSTANCE:
    case CLI_TEST_INSTANCE:
    case CLI_RULE_INSTANCE:
    case CLI_COMPONENT:
        if ((cli_state != CLI_STATE_INITIALIZED) && 
            (cli_state != def_state)) {
            nvgen_generate = TRUE;
        }
        break;
    default:
        break;
    }    
    return (nvgen_generate);
}    

/* 
 * swdiag_cli_allocate_handle()
 * Malloc memory for handle, initialize with given parameter and return
 * pointer.
 */
cli_handle_t *swdiag_cli_local_handle_allocate (cli_type_t type,
                                                cli_type_filter_t filter)
{
    cli_handle_t *handle;
    
    /* Create a list if it is the first time allocation */
    if (!handles_in_use) {
        handles_in_use = swdiag_list_create();    
    }

    handle = calloc(1, sizeof(cli_handle_t));
    if (!handle) {
        swdiag_error("Not enough memory to allocate handle");
        return(0);
    }
    handle->handle_id = get_new_handle_id();
    handle->type = type;
    handle->filter = filter;
    handle->instance = NULL;
    handle->last_obj = NULL;
    handle->last_remote_obj = NULL;
    handle->remote_comp = NULL;
    swdiag_list_add(handles_in_use, handle);
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    return (handle);
}    

/*
 * swdiag_cli_local_get_info_handle_local()
 *
 * Allocate a CLI handle for this CLI request.
 */
unsigned int swdiag_cli_local_get_info_handle (const char *name,
                                               cli_type_t type,
                                               cli_type_filter_t filter,
                                               const char *instance_name)
{
    cli_handle_t *handle;
    cli_type_t local_type = type;
    obj_t *obj, *last_remote_obj;
    obj_instance_t *instance;
    const char fnstr[] = "Local handle"; 

    switch (type) {
    case CLI_TEST_INSTANCE:
    case CLI_TEST:
        local_type = CLI_TEST;
        break;
    case CLI_RULE_INSTANCE:
    case CLI_RULE:
        local_type = CLI_RULE;
        break;
    case CLI_ACTION_INSTANCE:
    case CLI_ACTION:
        local_type = CLI_ACTION;
        break;
    case CLI_COMPONENT:
        break;
    case CLI_UNKNOWN:
    default:
        return (0);
    }
    
    handle = swdiag_cli_local_handle_allocate(type, filter);
    if (!handle) {
        /* error is already logged in called function */
        return (0);
    }

    swdiag_debug(NULL, "%s - type (%d) filter (%d)", fnstr, type, filter);
    
    /* If object name is specified then retrieve specific object
     * from object database. The object name is NON NULL to retrieve
     * single object from object database.
     */
    swdiag_obj_db_lock();
    if (!NULLSTR(name)) {
        /* If asked to get handle for different types but filter is set to 
         * CLI_SHOW_COMP then change local type. This is used to show all
         * children of a component.
         */ 
        if (filter == CLI_SHOW_COMP) {
            local_type = CLI_COMPONENT;
        }    
        obj = swdiag_obj_get_by_name_unconverted(name, 
              cli_to_obj_type(local_type));
        if (!swdiag_obj_validate(obj, cli_to_obj_type(local_type))) {
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            return (0);
        }    
        handle_set_instance(handle, &obj->i);
        if (handle->type == CLI_TEST_INSTANCE ||
            handle->type == CLI_RULE_INSTANCE ||
            handle->type == CLI_ACTION_INSTANCE) {
            /* If instance name is given then locate an instance obj */
            if (!NULLSTR(instance_name)) {
                instance = obj->i.next;
                while (instance) {
                    if (!strcmp(instance_name, instance->name)) { 
                        /* found it */
                        if (!swdiag_obj_instance_validate(instance, 
                                cli_to_obj_type(local_type))) {
                            /* not a valid instance, terminate processing */
                            handle->instance = NULL;
                            break;
                        }    
                        handle_set_instance(handle, instance);
                        break;
                    }
                    instance = instance->next; 
                }    
            } else {    
                /* Get the 1st obj instance */
                instance = obj->i.next;
                if (!instance || !swdiag_obj_instance_validate(instance, 
                        cli_to_obj_type(local_type))) {
                    /* not a valid instance, terminate processing */
                    handle->instance = NULL;
                } else {    
                    handle_set_instance(handle, instance);
                }
            }    
        }
        handle->remote_comp = NULL;
        handle->remote_handle_id = 0;
        handle->filter = filter;
    } else {
        obj = swdiag_obj_get_first_rel(NULL, cli_to_rel_type(local_type));
        if (!swdiag_obj_validate(obj, cli_to_obj_type(local_type))) {
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            return (0);
        }    
        handle_set_instance(handle, &obj->i);
        handle->remote_comp = NULL;
        handle->remote_handle_id = 0;
        handle->filter = filter;
        last_remote_obj = swdiag_obj_get_first_rel(NULL, OBJ_REL_COMP);
        if (last_remote_obj && 
            swdiag_obj_validate(last_remote_obj, OBJ_TYPE_COMP)) {
            handle_set_last_remote_obj(handle, last_remote_obj);
        }
    }
    swdiag_obj_db_unlock();
    return(handle->handle_id);
}

/*
 * In the real code do not use a memory address for the handle, as we can't
 * check that it is valid. Instead use a reference into an array that contains
 * the memory address - that way we can validate.
 */
void *swdiag_cli_local_get_single_info (unsigned int handle_id) 
{
    obj_instance_t *instance;
    obj_t *obj;
    obj_stats_t *stats;
    obj_state_t state;
    cli_handle_t *handle;
    const char fnstr[] = "Local single info"; 
    void *return_ptr = NULL;
    int i, j;
    
    handle = swdiag_cli_local_handle_get(handle_id); 
    if (!handle || !handle->instance ||
        !swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        return (NULL);
    }
   
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance;
    obj = instance->obj;
    stats = &obj->i.stats;
    state = obj->i.state;

    swdiag_debug(NULL, "%s - obj name '%s' type (%d) filter (%d)", 
                 fnstr, obj->i.name, handle->type, handle->filter);
    
    switch (handle->type) {
    case CLI_COMPONENT:
    {
        cli_comp_t *cli_comp = NULL;
        
        if (state == OBJ_STATE_ENABLED ||
            state == OBJ_STATE_DISABLED ||
            state == OBJ_STATE_CREATED ||
            state == OBJ_STATE_INITIALIZED) {
            cli_comp = calloc(1, sizeof(cli_comp_t));
            if (cli_comp == NULL) {
                swdiag_error("%s - No memory to allocate cli comp obj", 
                    fnstr);
                swdiag_cli_local_handle_reset_in_use_flag(handle);
                handle_clean_up(handle);
                swdiag_obj_db_unlock();
                return (cli_comp);
            }
            cli_comp->description = obj->description;
            cli_comp->context = obj->i.context;
            cli_comp->name = obj->i.name;
            cli_comp->stats.failures = stats->failures;
            cli_comp->stats.aborts = stats->aborts;
            cli_comp->stats.passes = stats->passes;
            cli_comp->stats.runs = stats->runs;
            cli_comp->health = (unsigned int)(obj->t.comp->health > 0 ? 
                                              obj->t.comp->health : 0);
            cli_comp->confidence = obj->t.comp->confidence;
            cli_comp->state = obj_state_to_cli(state);
            cli_comp->default_state = obj_state_to_cli(obj->i.default_state);
            cli_comp->catastrophic = obj->t.comp->catastrophic;
            cli_comp->critical = obj->t.comp->critical;
            cli_comp->high = obj->t.comp->high;
            cli_comp->medium = obj->t.comp->medium;
            cli_comp->low = obj->t.comp->low;
            cli_comp->positive = obj->t.comp->positive;
        }
        return_ptr = (void *)cli_comp;
    }        
    break;
    
    case CLI_TEST:
    {
        cli_test_t *cli_test = NULL;

        if (state == OBJ_STATE_ENABLED ||
            state == OBJ_STATE_DISABLED ||
            state == OBJ_STATE_CREATED ||
            state == OBJ_STATE_INITIALIZED) {
            cli_test = calloc(1, sizeof(cli_test_t));
            if (cli_test == NULL) {
                swdiag_error("No memory to allocate cli test obj");
                swdiag_cli_local_handle_reset_in_use_flag(handle);
                handle_clean_up(handle);
                swdiag_obj_db_unlock();
                return (cli_test);
            }
            cli_test->description = obj->description;
            cli_test->context = obj->i.context;
            cli_test->name = obj->i.name;
            cli_test->type = obj_test_type_to_cli_type(obj->t.test->type);
            cli_test->stats.failures = stats->failures;
            cli_test->stats.aborts = stats->aborts;
            cli_test->stats.passes = stats->passes;
            cli_test->stats.runs = stats->runs;
            for(i = 0; i < CLI_HISTORY_SIZE && i < OBJ_HISTORY_SIZE; i++) {
                j = i + (stats->history_head+1);
                if (j >= OBJ_HISTORY_SIZE) {
                    j -= OBJ_HISTORY_SIZE;
                }
                cli_test->stats.history[i].time = stats->history[j].time;
                cli_test->stats.history[i].result = stats->history[j].result;
                cli_test->stats.history[i].count = stats->history[j].count;
                cli_test->stats.history[i].value = stats->history[j].value;
            }
            cli_test->state = obj_state_to_cli(state);
            cli_test->default_state = obj_state_to_cli(obj->i.default_state);
            cli_test->period = obj->t.test->period;
            cli_test->default_period = obj->t.test->default_period;
            cli_test->last_ran = obj->i.sched_test.last_time;
            cli_test->next_run = obj->i.sched_test.next_time;
            cli_test->last_result_count = obj->i.last_result_count;       
            cli_test->last_result = obj->i.last_result;  
            cli_test->last_value = obj->i.last_value;
        }
        return_ptr = (void *)cli_test;
    }    
    break;
    
    case CLI_ACTION:
    {
        cli_action_t *cli_action = NULL;

        if (state == OBJ_STATE_ENABLED ||
            state == OBJ_STATE_DISABLED ||
            state == OBJ_STATE_CREATED ||
            state == OBJ_STATE_INITIALIZED) {
            cli_action = calloc(1, sizeof(cli_action_t));
            if (cli_action == NULL) {
                swdiag_error("No memory to allocate cli action obj");
                swdiag_cli_local_handle_reset_in_use_flag(handle);
                handle_clean_up(handle);
                swdiag_obj_db_unlock();
                return (cli_action);
            }
            cli_action->description = obj->description;
            cli_action->context = obj->i.context;
            cli_action->name = obj->i.name;
            cli_action->stats.failures = stats->failures;
            cli_action->stats.aborts = stats->aborts;
            cli_action->stats.passes = stats->passes;
            cli_action->stats.runs = stats->runs;
            for(i = 0; i < CLI_HISTORY_SIZE && i < OBJ_HISTORY_SIZE; i++) {
                j = i + (stats->history_head+1);
                if (j >= OBJ_HISTORY_SIZE) {
                    j -= OBJ_HISTORY_SIZE;
                }
                cli_action->stats.history[i].time = stats->history[j].time;
                cli_action->stats.history[i].result = stats->history[j].result;
                cli_action->stats.history[i].count = stats->history[j].count;
                cli_action->stats.history[i].value = stats->history[j].value;
            }
            cli_action->state = obj_state_to_cli(state);
            cli_action->default_state = obj_state_to_cli(obj->i.default_state);
            cli_action->last_result_count = obj->i.last_result_count;       
            cli_action->last_result = obj->i.last_result;       
        }
        return_ptr = (void *)cli_action;
    }    
    break;

    case CLI_RULE:
    {
        cli_rule_t *cli_rule = NULL;

        if (state == OBJ_STATE_ENABLED ||
            state == OBJ_STATE_DISABLED ||
            state == OBJ_STATE_CREATED ||
            state == OBJ_STATE_INITIALIZED) {
            cli_rule = calloc(1, sizeof(cli_rule_t));
            if (cli_rule == NULL) {
                swdiag_error("No memory to allocate cli rule obj");
                swdiag_cli_local_handle_reset_in_use_flag(handle);
                handle_clean_up(handle);
                swdiag_obj_db_unlock();
                return (cli_rule);
            }
            cli_rule->description = obj->description;
            cli_rule->context = obj->i.context;
            cli_rule->name = obj->i.name;
            cli_rule->stats.failures = stats->failures;
            cli_rule->stats.aborts = stats->aborts;
            cli_rule->stats.passes = stats->passes;
            cli_rule->stats.runs = stats->runs;
            for(i = 0; i < CLI_HISTORY_SIZE && i < OBJ_HISTORY_SIZE; i++) {
                j = i + (stats->history_head+1);
                if (j >= OBJ_HISTORY_SIZE) {
                    j -= OBJ_HISTORY_SIZE;
                }
                cli_rule->stats.history[i].time = stats->history[j].time;
                cli_rule->stats.history[i].result = stats->history[j].result;
                cli_rule->stats.history[i].count = stats->history[j].count;
                cli_rule->stats.history[i].value = stats->history[j].value;
            }
            cli_rule->state = obj_state_to_cli(state);
            cli_rule->default_state = obj_state_to_cli(obj->i.default_state);
            cli_rule->operator =  obj->t.rule->operator;
            cli_rule->default_operator =  obj->t.rule->default_operator;
            cli_rule->op_n =  obj->t.rule->op_n;
            cli_rule->op_m =  obj->t.rule->op_m;
            cli_rule->fail_count = obj->i.fail_count;       
            cli_rule->last_result_count = obj->i.last_result_count;       
            cli_rule->last_result = obj->i.last_result;
            cli_rule->last_value = obj->i.last_value;
        }
        return_ptr = (void *)cli_rule;
    }    
    break;
    default:
        break;
    } /* end of switch (handle->type) */     

    swdiag_cli_local_handle_reset_in_use_flag(handle);
    handle_clean_up(handle);
    swdiag_obj_db_unlock();
    return (return_ptr);
} /* end of swdiag_cli_get_single_info_local() */     

/*
 * Given a handle (that we allocated earlier) fill up a new cli_instance_t
 */
cli_instance_t *swdiag_cli_local_get_single_instance_info (unsigned int handle_id) 
{
    cli_instance_t *cli_inst = NULL;
    obj_instance_t *instance;
    obj_state_t state;
    obj_stats_t *stats;
    cli_type_t type;
    const char fnstr[] = "Local single instance"; 
    cli_handle_t *handle;
    int i, j;
    
    handle = swdiag_cli_local_handle_get(handle_id); 
    if (!handle || !handle->instance || 
        !swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        return (NULL);
    }

    switch (handle->type) {
        case CLI_TEST_INSTANCE:
            type = CLI_TEST;
            break;    
        case CLI_RULE_INSTANCE:
            type = CLI_RULE;
            break;    
        case CLI_ACTION_INSTANCE:
            type = CLI_ACTION;
            break;    
        default:
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_error("%s - Invalid type (%d) is passed for get instance info",
                    fnstr, handle->type);
        return (NULL);
    }   

    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance;
    
    state = instance->state;

    swdiag_debug(NULL, "%s - instance name '%s' type (%d) filter (%d)", 
                 fnstr, instance->name, handle->type, handle->filter);

    if (state == OBJ_STATE_ENABLED ||
        state == OBJ_STATE_DISABLED ||
        state == OBJ_STATE_CREATED ||
        state == OBJ_STATE_INITIALIZED) {
        stats = &instance->stats;
        cli_inst = calloc(1, sizeof(cli_instance_t));
        if (!cli_inst) {
            swdiag_error("%s -Not enough memory", fnstr);
            swdiag_cli_local_handle_reset_in_use_flag(handle);
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            return (NULL);
        }
        /*
         * Setup contents of element
         */
        cli_inst->name = instance->name;
        cli_inst->stats.failures = stats->failures;
        cli_inst->stats.aborts = stats->aborts;
        cli_inst->stats.passes = stats->passes;
        cli_inst->stats.runs = stats->runs;
        for(i = 0; i < CLI_HISTORY_SIZE && i < OBJ_HISTORY_SIZE; i++) {
            j = i + (stats->history_head+1);
            if (j >= OBJ_HISTORY_SIZE) {
                j -= OBJ_HISTORY_SIZE;
            }
            cli_inst->stats.history[i].time = stats->history[j].time;
            cli_inst->stats.history[i].result = stats->history[j].result;
            cli_inst->stats.history[i].count = stats->history[j].count;
            cli_inst->stats.history[i].value = stats->history[j].value;
        }
        cli_inst->state = obj_state_to_cli(state);
        cli_inst->default_state = obj_state_to_cli(instance->default_state);
        cli_inst->last_result = instance->last_result;       
        cli_inst->last_result_count = instance->last_result_count;     
        cli_inst->fail_count = instance->fail_count;       
    } else {
        swdiag_debug(NULL, "%s - instance '%s' invalid state %d", fnstr, 
                     instance->name, state);
    }
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    handle_clean_up(handle);
    swdiag_obj_db_unlock();
    return (cli_inst);
}

/*
 * swdiag_cli_get_instance_info_local()
 *
 * Given a handle (that we allocated earlier) fill up a new cli_info_t
 * with data up to the mtu provided. The caller is to free the allocated
 * memory so that we are multi-thread safe. Given handle should have the 
 * name of a obect for which instances informaton need to be fetched. 
 *
 */
cli_info_t *swdiag_cli_local_get_instance_info (unsigned int handle_id, 
                                                unsigned int mtu)
{
    cli_info_t *cli_info = NULL;
    unsigned int count = 0;
    cli_type_filter_t filter;
    obj_stats_t *stats;
    obj_state_t state;
    boolean process;
    obj_instance_t *instance;
    cli_type_t type;
    cli_info_element_t *element = NULL; 
    cli_info_element_t *last_element;
    const char fnstr[] = "Local instance info"; 
    cli_handle_t *handle;
    
    handle = swdiag_cli_local_handle_get(handle_id); 
    
    if (!handle || !handle->instance ||
        !swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        return (NULL);
    }
    
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    
    switch (handle->type) {
    case CLI_TEST_INSTANCE:
        type = CLI_TEST;
        break;    
    case CLI_RULE_INSTANCE:
        type = CLI_RULE;
        break;    
    case CLI_ACTION_INSTANCE:
        type = CLI_ACTION;
        break;    
    default:
        swdiag_error("%s - Invalid type (%d) is passed for get instance info",
            fnstr, handle->type);
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        return (NULL);
    }   

    swdiag_debug(NULL, "%s - type (%d) filter (%d)", fnstr, handle->type, 
                 handle->filter);

    cli_info = calloc(1, sizeof(cli_info_t));

    if (cli_info == NULL) {
        swdiag_error("%s - No memory to allocate cli instance info", fnstr);
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        return(NULL);
    }
    count = 0;
    cli_info->elements = NULL;
    last_element = NULL;
    filter = handle->filter;

    swdiag_obj_db_lock();
    instance = handle->instance;

    while (instance && count < mtu) {
        state = instance->state;
        if (state == OBJ_STATE_ENABLED ||
            state == OBJ_STATE_DISABLED ||
            state == OBJ_STATE_CREATED ||
            state == OBJ_STATE_INITIALIZED) {
            process = TRUE;
            stats = &instance->stats;
            switch (filter) {
            case CLI_DATA_FAILURE:
                if (stats->failures == 0) {
                    process = FALSE;
                }    
                break;
            case CLI_DATA_FAILURE_CURRENT:
                if (instance->last_result != SWDIAG_RESULT_FAIL) {
                    process = FALSE;
                }    
                break;
            default:
                process = TRUE;
                break;   
            }    
            
            if (process) {
                element = calloc(1, sizeof(cli_info_element_t));
                if (!element) {
                    swdiag_cli_local_handle_reset_in_use_flag(handle);
                    handle_clean_up(handle);
                    swdiag_obj_db_unlock();
                    return(cli_info);
                }
                /*
                 * Setup contents of element
                 */
                element->type = type;
                element->name = instance->name;
                element->stats.failures = stats->failures;
                element->stats.aborts = stats->aborts;
                element->stats.passes = stats->passes;
                element->stats.runs = stats->runs;
                element->state = obj_state_to_cli(state);
                element->default_state = 
                               obj_state_to_cli(instance->default_state);

                count++;
                
                element->last_result = instance->last_result;
                element->last_result_count = instance->last_result_count;

                element->next = NULL;
                
                if (!cli_info->elements) {
                    cli_info->elements = element;
                }
                if (last_element) {
                    last_element->next = element;
                }
                last_element = element;
            } /* end of process */
        }
        instance = instance->next;
        if (!instance || 
            !swdiag_obj_instance_validate(instance, cli_to_obj_type(type))) {
            swdiag_cli_local_handle_reset_in_use_flag(handle);
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            return(cli_info);
        }
        handle_set_instance(handle, instance);
    } /* end of while */
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}

/**
 * Free up the cli_info when finished with it.
 */
void swdiag_cli_local_free_info(cli_info_t *info) {
    cli_info_element_t *element = info->elements;
    cli_info_element_t *element_free = NULL;

    while (element != NULL) {
        element_free = element;
        element = element->next;
        free(element_free);
    }
    free(info);
}
/*
 * swdiag_cli_get_info_local()
 *
 * Given a handle (that we allocated earlier) fill up a new cli_info_t
 * with data up to the mtu provided. The caller is to free the allocated
 * memory so that we are multi-thread safe.
 * This function should be used only to get all test/rule/action/component
 * objects. For instances use swdiag_cli_get_instance_info_local(). 
 *
 * In the real code do not use a memory address for the handle, as we can't
 * check that it is valid. Instead use a reference into an array that contains
 * the memory address - that way we can validate.
 */
cli_info_t *swdiag_cli_local_get_info (unsigned int handle_id, 
                                       unsigned int max)
{
    cli_info_t *cli_info = NULL;
    cli_info_element_t *element = NULL, *tmp_element, *last_element;
    unsigned int count = 0;
    cli_type_filter_t filter;
    obj_stats_t *stats;
    obj_state_t state;
    boolean process;
    obj_t *obj;
    obj_instance_t *instance;
    const char fnstr[] = "Local info"; 
    cli_handle_t *handle;
    
    handle = swdiag_cli_local_handle_get(handle_id); 

    if (!handle || !handle->instance || 
        !swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        return (NULL);
    }
    
    cli_info = calloc(1, sizeof(cli_info_t));
    if (cli_info == NULL) {
        swdiag_error("%s - No memory to allocate cli data", fnstr);
        handle_clean_up(handle);
        return(NULL);
    }
    
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    count = 0;
    cli_info->elements = NULL;
    last_element = NULL;
    filter = handle->filter;
    instance = handle->instance;
    obj = instance->obj;
    while (obj && count < max) {
        state = obj->i.state;
        if (state == OBJ_STATE_ENABLED ||
            state == OBJ_STATE_DISABLED ||
            state == OBJ_STATE_CREATED ||
            state == OBJ_STATE_INITIALIZED) {
            process = TRUE;
            stats = &obj->i.stats;
            switch (filter) {
            case CLI_DATA_FAILURE:
                if (stats->failures == 0) {
                    process = FALSE;
                }    
                break;
            case CLI_DATA_FAILURE_CURRENT:
                if (obj->i.last_result != SWDIAG_RESULT_FAIL) {
                    process = FALSE;
                }    
                break;
            case CLI_NVGEN:
                /*
                 * Has anything other than state changed?
                 */
                process = swdiag_check_nvgen(handle->type, 
                                             obj_state_to_cli(obj->i.cli_state), 
                                             obj_state_to_cli(obj->i.default_state),
                                             obj->t.test->period,
                                             obj->t.test->default_period,
                                             obj->t.rule->operator,
                                             obj->t.rule->default_operator);
                
                if (process) {
                    /*
                     * Nvgenning of objects because of state alone should
                     * be summarised by the owning component where possible
                     *
                     * The following matrix shows when we should nvgen
                     * an object based on its cli_state and the
                     * parents cli_state compared to their respective
                     * default states.
                     *
                     * We also know whether the CLI has configured the state
                     * and so can compare that against what the default state
                     * is.
                     *
                     * We should nvgen anything that has a different default
                     * state than cli_state - unless summarised by the parent.
                     *
                     * CLI/Default
                     *
                     * +--------------------------------+
                     * | Parent>| Enb | Enb | Dis | Dis |
                     * | Obj    | Enb | Dis | Enb | Dis |
                     * |   v    |     |     |     |     |
                     * +--------+-----+-----+-----+-----+
                     * | Enb/Enb|     |     | NV  |     |
                     * | Enb/Dis| NV  | Sum | NV  | NV  |
                     * | Dis/Enb| NV  | NV  | Sum | NV  |
                     * | Dis/Dis|     | NV  |     |     |
                     * +--------+-----+-----+-----+-----+ 
                     * NV = Nvgen, Sum = Summarise
                     *
                     * Of particular note is the fact that we should
                     * nvgen the state for an object that has been
                     * configured to be its default state whilst its
                     * parent has been configured to not be its default 
                     * state.
                     *
                     * Also we need to check the state of the instance vs
                     * the state of the base instance, and if they differ
                     * then we need to nvgen that instance state TODO.
                     */
                    if (obj->i.cli_state != OBJ_STATE_INITIALIZED &&
                        obj->i.cli_state != obj->i.default_state) {
                        /*
                         * +--------------------------------+
                         * | Parent>| Enb | Enb | Dis | Dis |
                         * | Obj    | Enb | Dis | Enb | Dis |
                         * |   v    |     |     |     |     |
                         * +--------+-----+-----+-----+-----+
                         * | Enb/Dis| NV  | Sum | NV  | NV  |
                         * | Dis/Enb| NV  | NV  | Sum | NV  |
                         * +--------+-----+-----+-----+-----+
                         *
                         * Decide whether to summarise.
                         */
                        if (obj->parent_comp) {
                            if ((obj->parent_comp->obj->i.cli_state != 
                                 obj->parent_comp->obj->i.default_state) &&
                                obj->parent_comp->obj->i.cli_state == obj->i.cli_state) {
                                /*
                                 * Summarise
                                 */
                                process = FALSE;
                            } else {
                                process = TRUE;
                            }
                        } else {
                            /*
                             * Must be the system component if there is 
                             * no parent.
                             */
                            process = TRUE;
                        }
                    } else {
                        /*
                         * There are a couple of scenarios where we would
                         * like to nvgen an objects state even though it
                         * is the default state already.
                         */
                        if (obj->parent_comp &&
                            obj->parent_comp->obj->i.state != 
                            obj->parent_comp->obj->i.default_state &&
                            obj->parent_comp->obj->i.default_state == state) {
                            process = TRUE;
                        }
                    }
                }
                break;
            default:
                process = TRUE;
                break;   
            }    
            
            if (process) {
                element = calloc(1, sizeof(cli_info_element_t));
                if (!element) {
                    swdiag_error("No memory to allocate element");
                    break;
                }
                /*
                 * Setup contents of element
                 */
                element->type = handle->type;
                element->description = obj->description;
                element->name = obj->i.name;
                element->last_result = obj->i.last_result;
                element->stats.failures = stats->failures;
                element->stats.aborts = stats->aborts;
                element->stats.passes = stats->passes;
                element->stats.runs = stats->runs;
                element->state = obj_state_to_cli(state); 
                element->default_state = obj_state_to_cli(obj->i.default_state);
                element->cli_state = obj_state_to_cli(obj->i.cli_state);
                
                count++;
                
                switch (handle->type) {
                case CLI_COMPONENT:
                    element->health = (unsigned int)(obj->t.comp->health > 0 ? 
                                                     obj->t.comp->health : 0) ;
                    element->confidence = obj->t.comp->confidence;
                    break;   
                case CLI_TEST:
                    element->period = obj->t.test->period;
                    element->default_period = obj->t.test->default_period;
                    element->last_result_count = obj->i.last_result_count;
                    break;
                case CLI_RULE:
                    element->operator =  obj->t.rule->operator;
                    element->default_operator = obj->t.rule->default_operator;
                    element->op_n = obj->t.rule->op_n;
                    element->op_m = obj->t.rule->op_m;
                    element->last_result_count = obj->i.last_result_count;
                    element->severity = obj->t.rule->severity;
                    break;
                case CLI_ACTION:
                    break;
                default:
                    /*
                     * This can't/won't occur, but if it does it 
                     * will happen on the first pass through - 
                     * so only one element has been
                     * allocated, so free that and the cli_info.
                     */
                    swdiag_error("%s - Invalid CLI Request for handle type (%d) "
                        "aborting", fnstr, handle->type);
                    if (element) {
                        free(element);
                    }    
                    free(cli_info);
                    swdiag_cli_local_handle_reset_in_use_flag(handle);
                    handle_clean_up(handle);
                    swdiag_obj_db_unlock();
                    return(NULL);
                } /* end of switch (handle->type) */
                
                element->next = NULL;
                
                if (!cli_info->elements) {
                    cli_info->elements = element;
                }
                if (last_element) {
                    last_element->next = element;
                }
                last_element = element;
            } /* end of process */
        }
        switch(handle->type) { 
        case CLI_TEST:
        case CLI_ACTION:
        case CLI_RULE:
        case CLI_COMPONENT:
            obj = swdiag_obj_get_next_rel(obj, cli_to_rel_type(handle->type));
            if (!obj || 
                !swdiag_obj_validate(obj, cli_to_obj_type(handle->type))) {
                swdiag_cli_local_handle_reset_in_use_flag(handle);
                handle_clean_up(handle);
                swdiag_obj_db_unlock();
                return(cli_info);
            } else { 
                handle_set_instance(handle, &obj->i);
            }
            break; 
        default:
            /*
             * Invalid - what are they asking for?
             *
             * Free up the elements so far, and the cli_info and return.
             * (NB: there won't be any elements since we would have exited
             * earlier - this is to keep SA happy, and in case the design
             * changes).
             */
            swdiag_error("%s - Invalid CLI Request for handle type (%d) aborting", 
                fnstr, handle->type);
            element = cli_info->elements;
            while (element) {
                tmp_element = element->next;
                free(element);
                element = tmp_element;
            }
            count = 0;
            swdiag_cli_local_handle_reset_in_use_flag(handle);
            handle_clean_up(handle);
            obj = NULL;
            swdiag_obj_db_unlock();
            return(cli_info);
        }
    }
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}    


/*
 * swdiag_cli_get_parent_comp_local()
 * Get the name of parent component
 */ 
const char *swdiag_cli_local_get_parent_comp (unsigned int handle_id)
{
    obj_t *parent_comp = NULL;
    obj_instance_t *instance;
    const char *parent_comp_name = NULL;
    cli_handle_t *handle;
    
    handle = swdiag_cli_local_handle_get(handle_id); 
    
    if (!handle || !handle->instance ||
        !swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        return (NULL);
    }
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance;
    parent_comp = swdiag_obj_get_rel(instance->obj, OBJ_REL_PARENT_COMP);
    if (parent_comp) {
        parent_comp_name = parent_comp->i.name;
    }
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    handle_clean_up(handle);
    swdiag_obj_db_unlock();
    return (parent_comp_name);
}

static void swdiag_cli_copy_data_element (cli_data_element_t *cli_element,
                                          obj_t *obj)
{
    cli_element->name = obj->i.name;
    cli_element->stats.failures = obj->i.stats.failures;
    cli_element->stats.aborts = obj->i.stats.aborts;
    cli_element->stats.passes = obj->i.stats.passes;
    cli_element->stats.runs = obj->i.stats.runs;
    cli_element->state = obj_state_to_cli(obj->i.state);
    cli_element->type = obj_type_to_cli(obj->type);
    if (obj->type == OBJ_TYPE_RULE) {
        cli_element->severity = obj->t.rule->severity;
    }
    cli_element->last_result = obj->i.last_result;
}            
            
/*
 * swdiag_cli_get_strucs_in_comp_local()
 */ 
cli_data_t *swdiag_cli_local_get_strucs_in_comp (unsigned int handle_id,
                                                 unsigned int max_mtu) 
{
    obj_t *last_obj = NULL;
    obj_instance_t *instance;
    obj_rel_t rel;
    cli_data_element_t *cli_element = NULL, *last_element, *tmp_element;
    cli_data_t *cli_info = NULL;
    unsigned int count = 0;
    const char fnstr[] = "Local strucs in comp"; 
    cli_handle_t *handle;
    
    handle = swdiag_cli_local_handle_get(handle_id); 
    
    if (!handle || !handle->instance ||
        !swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        return (NULL);
    }

    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    rel = cli_to_rel_type(handle->type);

    instance = handle->instance;
    last_obj = handle->last_obj;
    if (last_obj == NULL) {
        last_obj = swdiag_obj_get_first_rel(instance->obj, rel);
        if (!last_obj || !swdiag_obj_validate(last_obj, last_obj->type)) {
            swdiag_cli_local_handle_reset_in_use_flag(handle);
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            return (NULL);
        }    
        handle_set_last_obj(handle, last_obj);
    } else {
        last_obj = handle->last_obj;
    }    

    cli_info = calloc(1, sizeof(cli_data_t));
    if (cli_info == NULL) {
        swdiag_error("%s - No memory to allocate cli info in get strucs", fnstr);
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }
    count = 0;
    cli_info->elements = NULL;
    last_element = NULL;
    while (last_obj && count < max_mtu) {
        cli_element = calloc(1, sizeof(cli_data_element_t));
        if (!cli_element) {
            swdiag_error("No memory to allocate cli element");
            break;
        }
        swdiag_cli_copy_data_element(cli_element, last_obj);
        cli_element->next = NULL;
        count++;

        if (!cli_info->elements) {
            cli_info->elements = cli_element;
        }
        if (last_element) {
            last_element->next = cli_element;
        }
        last_element = cli_element;
        
        switch(handle->type) { 
        case CLI_TEST:
        case CLI_ACTION:
        case CLI_RULE:
        case CLI_COMPONENT:
            last_obj = swdiag_obj_get_next_rel(last_obj, OBJ_REL_NEXT_IN_COMP);
            if (!last_obj || !swdiag_obj_validate(last_obj, last_obj->type)) {
                swdiag_cli_local_handle_reset_in_use_flag(handle);
                handle_clean_up(handle);
                swdiag_obj_db_unlock();
                return(cli_info);
            } else {    
                handle_set_last_obj(handle, last_obj);
            }
            break; 
        default:
            /*
             * Invalid - what are they asking for?
             * Free up the elements so far, and the cli_info and return.
             */
            swdiag_error("%s - Invalid CLI Request, aborting", fnstr);
            cli_element = cli_info->elements;
            while (cli_element) {
                tmp_element = cli_element->next;
                free(cli_element);
                cli_element = tmp_element;
            }
            swdiag_cli_local_handle_reset_in_use_flag(handle);
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            last_obj = NULL;
            return(cli_info);
        }
    } /* end of while */

    swdiag_cli_local_handle_reset_in_use_flag(handle);
    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}

/* 
 * Get all rules for a given test to be displayed through CLI
 */ 
static cli_data_t  *get_test_rules (cli_handle_t *handle,
                                    unsigned int max_mtu)
{
    cli_data_t *cli_info = NULL;
    cli_data_element_t *cli_element, *last_element, *tmp_element;
    unsigned int count = 0;
    obj_instance_t *instance;
    obj_t *last_obj = NULL; 
    const char fnstr[] = "Local test rules"; 

    if (!handle || !handle->instance) { 
        return (NULL);
    }
    
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance;
    last_obj = handle->last_obj;

    if (last_obj == NULL) {
        last_obj = swdiag_obj_get_first_rel(instance->obj, OBJ_REL_RULE);  
        if (!last_obj || !swdiag_obj_validate(last_obj, OBJ_TYPE_RULE)) {
            swdiag_cli_local_handle_reset_in_use_flag(handle);
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            return (NULL);
        }    
        handle_set_last_obj(handle, last_obj);
    } else {
        last_obj = handle->last_obj;
    }    

    cli_info = calloc(1, sizeof(cli_data_t));
    if (cli_info == NULL) {
        swdiag_error("%s - No memory to allocate cli data", fnstr);
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }

    count = 0;
    cli_info->elements = NULL;
    last_element = NULL;

    while (last_obj && count < max_mtu) {
        cli_element = calloc(1, sizeof(cli_data_element_t));
        if (!cli_element) {
            swdiag_error("%s - No memory to allocate cli element", fnstr);
            break;
        }
        swdiag_cli_copy_data_element(cli_element, last_obj);
        cli_element->next = NULL;
        count++;

        if (!cli_info->elements) {
            cli_info->elements = cli_element;
        }
        if (last_element) {
            last_element->next = cli_element;
        }
        last_element = cli_element;
        switch(handle->type) { 
        case CLI_TEST:
        case CLI_ACTION:
        case CLI_RULE:
        case CLI_COMPONENT:
            last_obj = swdiag_obj_get_next_rel(last_obj, OBJ_REL_NEXT_IN_TEST);
            if (!last_obj || !swdiag_obj_validate(last_obj, last_obj->type)) {
                swdiag_cli_local_handle_reset_in_use_flag(handle);
                handle_clean_up(handle);
                swdiag_obj_db_unlock();
                return(NULL);
            } else {    
                handle_set_last_obj(handle, last_obj);
            }
            break; 
        default:
            /*
             * Invalid - what are they asking for?
             * Free up the elements so far, and the cli_info and return.
             */
            swdiag_error("%s - Invalid CLI Request, aborting", fnstr);
            cli_element = cli_info->elements;
            while (cli_element) {
                tmp_element = cli_element->next;
                free(cli_element);
                cli_element = tmp_element;
            }
            swdiag_cli_local_handle_reset_in_use_flag(handle);
            handle_clean_up(handle);
            swdiag_obj_db_unlock();
            return(NULL);
        }
    }
    cli_info->num_elements = count;
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    swdiag_obj_db_unlock();
    return(cli_info);
}

/* 
 * swdiag_cli_get_rule_actions_or_inputs_local()
 * Get all actions or inputs for a given rule to be displayed through CLI
 * This is common function as inputs and actions traverses through swdiag_list.
 */ 
static cli_data_t *get_rule_actions_or_inputs (cli_handle_t *handle,
                                               unsigned int max_mtu,
                                               cli_type_filter_t rule_type)
{
    obj_t *rule_obj, *last_obj;
    obj_instance_t *instance;
    obj_t *element_obj ;
    swdiag_list_element_t *element = NULL;
    cli_data_t *cli_info = NULL;
    cli_data_element_t *cli_element;
    cli_data_element_t *last_element = NULL;
    unsigned int count = 0;
    const char fnstr[] = "Local rule types"; 
    boolean clean_up_handle = FALSE;

    if (!handle || !handle->instance) {
        return (cli_info);
    }
    
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance; 
    rule_obj = instance->obj;

    if (rule_type == CLI_RULE_ACTION) {
        element = rule_obj->t.rule->action_list->head;
    } else if (rule_type == CLI_RULE_INPUT) {
        element = rule_obj->t.rule->inputs->head;
    } else {
        /* This should not happen so return */
        element = NULL;
    }    
    if (element == NULL) {
        /* object might have been deleted */
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }    

    last_obj = handle->last_obj;
    if (last_obj == NULL) {
        element_obj = (obj_t *)element->data;
        if (!element_obj || 
            !swdiag_obj_validate(element_obj, element_obj->type)) {
            clean_up_handle = TRUE;
        } else {    
            handle_set_last_obj(handle, element_obj);
        }
    } else {    
        element_obj = last_obj;
        while (element->data != last_obj) {
            element = element->next;
        }    
        if (element == NULL) {
            /* object might have been deleted */
            clean_up_handle = TRUE;
        }    
    }    

    if (clean_up_handle) {
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }        

    cli_info = calloc(1, sizeof(cli_data_t));
    if (cli_info == NULL) {
        swdiag_error("No memory to allocate cli data");
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }

    count = 0;
    cli_info->elements = NULL;
    while (element_obj && count < max_mtu) {
        cli_element = calloc(1, sizeof(cli_data_element_t));
        if (!cli_element) {
            swdiag_error("%s - No memory to allocate cli element", fnstr);
            clean_up_handle = TRUE;
            break;
        }
        swdiag_cli_copy_data_element(cli_element, element_obj);
        count ++;

        cli_element->next = NULL;

        if (!cli_info->elements) {
            cli_info->elements = cli_element;
        }
        if (last_element) {
            last_element->next = cli_element;
        }
        last_element = cli_element;
        element = element->next; 
        if (element) {
            element_obj = (obj_t *)element->data;
            if (!element_obj || 
                !swdiag_obj_validate(element_obj, element_obj->type)) {
                clean_up_handle = TRUE;
            } else {    
                handle_set_last_obj(handle, element_obj);
            }
        } else {
            clean_up_handle = TRUE;
            element_obj = NULL;
        }    
    }
    
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    if (clean_up_handle) {
        handle_clean_up(handle);
    }        

    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}

/* 
 * Get all output trigger for a given rule to be displayed through CLI
 */ 
static cli_data_t *get_rule_output (cli_handle_t *handle,
                                    unsigned int max_mtu)
{
    obj_t *rule_obj, *output_obj = NULL, *last_obj;
    cli_data_t *cli_info = NULL;
    obj_rule_t *output_rule = NULL;
    cli_data_element_t *cli_element;
    cli_data_element_t *last_element = NULL;
    unsigned int count = 0;
    obj_instance_t *instance;
    boolean clean_up_handle = FALSE;

    if (!handle || !handle->instance) {
        return (cli_info);
    }

    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance; 
    rule_obj = instance->obj;
    last_obj = handle->last_obj;

    /* Data in handle is NULL then it is first time iteration, otherwise 
     * start iterating from previous saved data pointer in handle->last_obj.
     */
    if (last_obj == NULL) {
        output_rule = rule_obj->t.rule->output;
    } else {
        output_rule = last_obj->t.rule;
    }    

    if (output_rule == NULL) {
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (NULL);
    }    

    cli_info = calloc(1, sizeof(cli_data_t));
    if (cli_info == NULL) {
        swdiag_error("No memory to allocate cli data");
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }

    count = 0;
    cli_info->elements = NULL;

    output_obj = output_rule->obj;
    while (output_obj && count < max_mtu) {
        if (!output_obj || !swdiag_obj_validate(output_obj, OBJ_TYPE_RULE)) {
            clean_up_handle = TRUE;
            break;
        }
        cli_element = calloc(1, sizeof(cli_data_element_t));
        if (!cli_element) {
            swdiag_error("No memory to allocate cli element");
            clean_up_handle = TRUE;
            break;
        }
        swdiag_cli_copy_data_element(cli_element, output_obj);
        count ++;

        cli_element->next = NULL;

        if (!cli_info->elements) {
            cli_info->elements = cli_element;
        }
        if (last_element) {
            last_element->next = cli_element;
        }
        last_element = cli_element;
        output_rule = output_rule->next_in_input; 
        if (output_rule) {
            output_obj = output_rule->obj;
            handle_set_last_obj(handle, output_obj);
        } else {
            clean_up_handle = TRUE;
            output_obj = NULL;
        }    
    }
    
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    if (clean_up_handle) {
        handle_clean_up(handle);
    }

    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}

/*
 * swdiag_cli_get_parent_depend_local()
 * Get all parent dependencies for a given object.
 */ 
static cli_data_t *get_parent_depend (cli_handle_t *handle, 
                                      unsigned int mtu)
{
    obj_t *obj, *parent_obj, *last_parent;
    obj_instance_t *instance;
    swdiag_list_element_t *element = NULL;
    cli_data_t *cli_info = NULL;
    cli_data_element_t *cli_element;
    cli_data_element_t *last_element = NULL;
    unsigned int count = 0;
    boolean clean_up_handle = FALSE;
    
    if (!handle || !handle->instance) {
        return (cli_info);
    }
   
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance;
    obj = instance->obj;
    element = obj->parent_depend->head;
    if (element == NULL) {
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }    
    last_parent = handle->last_obj;

    if (last_parent == NULL) {
        parent_obj = (obj_t *)element->data;
        if (!parent_obj || 
            !swdiag_obj_validate(parent_obj, parent_obj->type)) {
            clean_up_handle = TRUE;
        } else {    
            handle_set_last_obj(handle, parent_obj);
        }
    } else {
        parent_obj = last_parent; 
        while (element->data != last_parent) {
            element = element->next;
        }    
        if (element == NULL) {
            /* object might have been deleted */
            clean_up_handle = TRUE;
        }    
    }

    if (clean_up_handle) {
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }        
    
    cli_info = calloc(1, sizeof(cli_data_t));
    if (cli_info == NULL) {
        swdiag_error("No memory to allocate cli data");
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }
    
    count = 0;
    cli_info->elements = NULL;
    while (parent_obj && count < mtu) {
        cli_element = calloc(1, sizeof(cli_data_element_t));
        if (!cli_element) {
            swdiag_error("No memory to allocate cli element");
            clean_up_handle = TRUE;
            break;
        }
        swdiag_cli_copy_data_element(cli_element, parent_obj);
        count ++;

        cli_element->next = NULL;

        if (!cli_info->elements) {
            cli_info->elements = cli_element;
        }
        if (last_element) {
            last_element->next = cli_element;
        }
        last_element = cli_element;
        element = element->next; 
        if (element) {
            parent_obj = (obj_t *)element->data;
            if (!parent_obj || 
                !swdiag_obj_validate(parent_obj, parent_obj->type)) {
                clean_up_handle = TRUE;
            } else {    
                handle_set_last_obj(handle, parent_obj);
            }
        } else {
            parent_obj = NULL;
            clean_up_handle = TRUE;
        }    
    }
    
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    if (clean_up_handle) {
        handle_clean_up(handle);
    }
    
    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}    


/*
 * swdiag_cli_get_child_depend_local()
 * Get all child dependencies for a given object.
 */ 
static cli_data_t *get_child_depend (cli_handle_t *handle, 
                                     unsigned int mtu)
{
    obj_t *child_obj, *obj, *last_child;
    obj_instance_t *instance;
    swdiag_list_element_t *element = NULL;
    cli_data_t *cli_info = NULL;
    cli_data_element_t *cli_element;
    cli_data_element_t *last_element = NULL;
    unsigned int count = 0;
    boolean clean_up_handle = FALSE;

    if (!handle || !handle->instance) {
        return (cli_info);
    }
    
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance;
    obj = instance->obj;
    element = obj->child_depend->head;
    if (element == NULL) {
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }    

    last_child = handle->last_obj;
    if (last_child == NULL) {
        child_obj = (obj_t *)element->data;
        if (!child_obj || 
            !swdiag_obj_validate(child_obj, child_obj->type)) {
            clean_up_handle = TRUE;
        } else {    
            handle_set_last_obj(handle, child_obj);
        }
    } else {
        child_obj = last_child; 
        while (element->data != last_child) {
            element = element->next;
        }    
        if (element == NULL) {
            /* object might have been deleted */
            clean_up_handle = TRUE;
        }    
    }

    if (clean_up_handle) {
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }        

    cli_info = calloc(1, sizeof(cli_data_t));
    if (cli_info == NULL) {
        swdiag_error("No memory to allocate cli data");
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }
    
    count = 0;
    cli_info->elements = NULL;
    while (child_obj && count < mtu) {
        cli_element = calloc(1, sizeof(cli_data_element_t));
        if (!cli_element) {
            swdiag_error("No memory to allocate cli element");
            clean_up_handle = TRUE;
            break;
        }
        swdiag_cli_copy_data_element(cli_element, child_obj);
        count ++;
        child_obj->i.in_use--;

        cli_element->next = NULL;

        if (!cli_info->elements) {
            cli_info->elements = cli_element;
        }
        if (last_element) {
            last_element->next = cli_element;
        }
        last_element = cli_element;
        element = element->next; 
        if (element) {
            child_obj = (obj_t *)element->data;
            if (!child_obj || 
                !swdiag_obj_validate(child_obj, child_obj->type)) {
                clean_up_handle = TRUE;
            } else {    
                handle_set_last_obj(handle, child_obj);
            }
        } else {
            clean_up_handle = TRUE;
            child_obj = NULL;
        }    
    }
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    if (clean_up_handle) {
        handle_clean_up(handle);
    }
    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}    

/*
 * swdiag_cli_get_depend_or_trigger_data_local()
 *
 * TODO
 *
 * This shall get all actions, input rules, output triggers for rule.
 * If it is test it shows all underneath rules. Also it would get data of 
 * child and parent dependencies.
 */ 
cli_data_t *swdiag_cli_local_get_depend_or_trigger_data (unsigned int handle_id,
                                                         unsigned int mtu)
{
    cli_handle_t *handle;

    handle = swdiag_cli_local_handle_get(handle_id); 
    if (!handle) {
        return (NULL);
    }

    switch (handle->filter) {
        case CLI_TEST_RULE:
            return (get_test_rules(handle, mtu));
        case CLI_RULE_ACTION:
            return (get_rule_actions_or_inputs(handle, 
                    mtu, CLI_RULE_ACTION));
        case CLI_RULE_INPUT: 
            return (get_rule_actions_or_inputs(handle, 
                    mtu, CLI_RULE_INPUT));
        case CLI_RULE_OUTPUT:
            return (get_rule_output(handle, mtu));
        case CLI_PARENT_DEPEND:
            return (get_parent_depend(handle, mtu));
        case CLI_CHILD_DEPEND:
            return (get_child_depend(handle, mtu));
        case CLI_FILTER_UNKNOWN: 
        case CLI_FILTER_NONE:
        case CLI_DATA_FAILURE:
        case CLI_DATA_FAILURE_CURRENT:
        case CLI_RUN:
        case CLI_RUN_NO_RULES:
        case CLI_SHOW_COMP:
        case CLI_OPT_TBL:
        case CLI_NVGEN:
            /* no action */
            return (NULL);    
    }

    // TODO
    return (NULL);
}    

/*
 * swdiag_get_connected_instances_between_objects_local() 
 * There can be common instances between rule and test which are called         
 * connected instances. 
 */ 
cli_data_t *swdiag_cli_local_get_connected_instances_between_objects 
                                               (unsigned int handle_id, 
                                               const char *instance_name, 
                                               unsigned int mtu)
{
    obj_t *last_obj = NULL; 
    cli_data_t *cli_info = NULL;
    cli_data_element_t *cli_element, *tmp_element;
    cli_data_element_t *last_element = NULL;
    unsigned int count = 0;
    const char fnstr[] = "Local Connected Instances:"; 
    obj_type_t obj_type = OBJ_TYPE_NONE;
    obj_instance_t *instance, *found_instance = NULL;
    boolean is_matching_instance = FALSE;;
    cli_handle_t *handle;
    swdiag_list_element_t *element = NULL;
    boolean clean_up_handle = FALSE;
    
    handle = swdiag_cli_local_handle_get(handle_id); 
    if (!handle || !handle->instance || 
        !swdiag_obj_instance_validate(handle->instance, OBJ_TYPE_ANY)) {
        return (cli_info);
    }
   
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    swdiag_obj_db_lock();
    instance = handle->instance;
    last_obj = handle->last_obj;

    if (handle->type == CLI_TEST) {
        obj_type = OBJ_TYPE_RULE; /* Dependent obj */
        if (!last_obj) {
            last_obj = swdiag_obj_get_first_rel(instance->obj, OBJ_REL_RULE);  
            if (!last_obj || 
                !swdiag_obj_validate(last_obj, last_obj->type)) {
                clean_up_handle = TRUE;
            } else {        
                handle_set_last_obj(handle, last_obj);
            }
        }
    } else if (handle->type == CLI_RULE) {
        obj_type = OBJ_TYPE_TEST; /* Dependent Obj */
        element = instance->obj->t.rule->inputs->head;  
        if (element == NULL) {
            clean_up_handle = TRUE;
        } else {    
            if (!last_obj) {
                last_obj = (obj_t *)element->data;
                if (!last_obj || 
                    !swdiag_obj_validate(last_obj, last_obj->type)) {
                    clean_up_handle = TRUE;
                } else {    
                    handle_set_last_obj(handle, last_obj);
                }
            } else {    
                while (element->data != last_obj) {
                    element = element->next;
                }    
                if (element == NULL) {
                    /* object might have been deleted */
                    clean_up_handle = TRUE;
                }    
            }
        }
    }    

    if (clean_up_handle) {
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }        
    cli_info = calloc(1, sizeof(cli_data_t));
    if (cli_info == NULL) {
        swdiag_error("No memory to allocate cli data");
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        swdiag_obj_db_unlock();
        return (cli_info);
    }
    
    count = 0;
    cli_info->elements = NULL;
    while (last_obj && count < mtu) {
        instance = last_obj->i.next;
        while (instance) {
            if (strcmp(instance_name, instance->name) == 0) { 
                is_matching_instance = TRUE;
                found_instance = instance;
                break;  /* There can be only one matching instance */
            }
            instance = instance->next;
        }
        
        /* No matching instance, then get a base instance */
        if (!is_matching_instance) {
            found_instance = &last_obj->i;
        }

        if (found_instance) {
            cli_element = calloc(1, sizeof(cli_data_element_t));
            if (!cli_element) {
                swdiag_error("%s - No memory to allocate cli element", 
                    fnstr);
                clean_up_handle = TRUE;
                break;
            }
            cli_element->name = swdiag_obj_instance_name(found_instance);
            cli_element->stats.failures = found_instance->stats.failures;
            cli_element->stats.aborts = found_instance->stats.aborts;
            cli_element->stats.passes = found_instance->stats.passes;
            cli_element->stats.runs = found_instance->stats.runs;
            cli_element->state = obj_state_to_cli(found_instance->state); 
            cli_element->type = obj_type_to_cli(last_obj->type);
            cli_element->last_result = last_obj->i.last_result;
            cli_element->next = NULL;
            count++;

            if (!cli_info->elements) {
                cli_info->elements = cli_element;
            }
            if (last_element) {
                last_element->next = cli_element;
            }
            last_element = cli_element;
            found_instance = NULL;
            is_matching_instance = FALSE;
        }        

        switch(handle->type) { 
        case CLI_TEST:
            last_obj = (void *)swdiag_obj_get_next_rel(last_obj, 
                                                       OBJ_REL_NEXT_IN_TEST);
            if (!last_obj || 
                !swdiag_obj_validate(last_obj, last_obj->type)) {
                clean_up_handle = TRUE;
            } else {        
                handle_set_instance(handle, &last_obj->i);
                handle_set_last_obj(handle, last_obj);
            }
            break; 
        case CLI_RULE:
            element = element->next; 
            if (element) {
                last_obj = (obj_t *)element->data;
                if (!last_obj || 
                    !swdiag_obj_validate(last_obj, last_obj->type)) {
                    clean_up_handle = TRUE;
                } else {    
                    handle_set_last_obj(handle, last_obj);
                }
            } else {
                clean_up_handle = TRUE;
                last_obj = NULL;
            }    
            break;
        default:
            /*
             * Invalid - what are they asking for?
             * Free up the elements so far, and the cli_info and return.
             */
            swdiag_error("%s - Invalid CLI Request, aborting", fnstr);
            cli_element = cli_info->elements;
            while (cli_element) {
                tmp_element = cli_element->next;
                free(cli_element);
                cli_element = tmp_element;
            }
            clean_up_handle = TRUE;
            last_obj = NULL;
            break;
        }
    }
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    if (clean_up_handle) {
        handle_clean_up(handle);
    }
    cli_info->num_elements = count;
    swdiag_obj_db_unlock();
    return(cli_info);
}    

void swdiag_cli_local_test_run (const char *test_name, 
                                const char *instance_name,
                                cli_type_t type,
                                cli_type_filter_t filter)
{
    obj_t *obj;
    obj_instance_t *instance;
    long retval;
    swdiag_result_t result;

    swdiag_obj_db_lock();
    obj = swdiag_obj_get_by_name_unconverted(test_name, cli_to_obj_type(type)); 

    if (!swdiag_obj_validate(obj, cli_to_obj_type(type))) {
        swdiag_error("No test obj found with name '%s'", test_name);
        swdiag_obj_db_unlock();
        return;
    }
    
    instance = swdiag_obj_instance_by_name(obj, instance_name);
    
    if (!swdiag_obj_instance_validate(instance, cli_to_obj_type(type))) {
        swdiag_error("No test obj instance with name '%s'", instance_name);
        swdiag_obj_db_unlock();
        return;
    }

    if (filter == CLI_RUN_NO_RULES) {
        printf("Run (no rules) %s\n", swdiag_obj_instance_name(instance));
        result = swdiag_seq_test_run(instance, &retval);
        if (result == SWDIAG_RESULT_VALUE) {
            printf("Test result was %s-%ld for %s\n",
                swdiag_util_swdiag_result_str(result), retval,
                swdiag_obj_instance_name(instance));
        } else {
            printf("Test result was %s for %s\n",
                swdiag_util_swdiag_result_str(result),
                swdiag_obj_instance_name(instance));
        }
    } else {
        printf("Run %s\n", swdiag_obj_instance_name(instance));
        swdiag_seq_from_test(instance);
    }
    swdiag_obj_db_unlock();
}  

void swdiag_cli_local_config_rule_param (const char *rule_name,
                                         boolean setdefault,
                                         swdiag_rule_operator_t op,
                                         long op_n,
                                         long op_m) 
{
    obj_t *obj;
    obj_rule_t *rule;
    const char fnstr[] = "Local rule config";

    swdiag_obj_db_lock();
    obj = swdiag_api_get_or_create(rule_name, OBJ_TYPE_RULE);
    if (!swdiag_obj_validate(obj, OBJ_TYPE_RULE)) {
        swdiag_error("%s '%s' - unknown", fnstr, rule_name);
        swdiag_obj_db_unlock();
        return;
    }

    rule = obj->t.rule;
    if (setdefault) {
       rule->operator = rule->default_operator; /* Reset the configured value
                                                 * to default
                                                 */ 
       rule->op_n = rule->default_op_n;
       rule->op_m = rule->default_op_m;
    } else {
       rule->operator = op; /* Reset the configured value */
       rule->op_n = op_n;
       rule->op_m = op_m;
    }    
    swdiag_obj_db_unlock();
}

void swdiag_cli_local_config_test_param (const char *test_name,
                                         boolean setdefault,
                                         unsigned int period)
{ 
    obj_t *test_obj;
    obj_instance_t *instance; 
    const char fnstr[] = "Local test config";

    swdiag_obj_db_lock();
    test_obj = swdiag_api_get_or_create(test_name, OBJ_TYPE_TEST);
    if (!swdiag_obj_validate(test_obj, OBJ_TYPE_TEST)) {
        swdiag_error("%s '%s' - unknown", fnstr, test_name);
        swdiag_obj_db_unlock();
        return;
    }

    for (instance = &test_obj->i; instance != NULL; instance = instance->next) {
        swdiag_sched_remove_test(instance);
    }
    
    if (setdefault) {
        test_obj->t.test->period = test_obj->t.test->default_period;
    } else {
        test_obj->t.test->period = period;
    }
    
    for (instance = &test_obj->i; instance != NULL; instance = instance->next) {
        swdiag_sched_add_test(instance, FALSE);
    }
    swdiag_obj_db_unlock();
}

void swdiag_cli_local_enable_disable_test (const char *test_name,
                                           cli_state_t state,
                                           const char *instance_name) 
{
    swdiag_obj_db_lock();
    switch(state) {
    case CLI_STATE_ENABLED:
        swdiag_api_test_enable_guts(test_name, instance_name, TRUE);
        break;
    case CLI_STATE_DISABLED:
        swdiag_api_test_disable_guts(test_name, instance_name, TRUE);
        break;
    case CLI_STATE_INITIALIZED:
        /*
         * Revert to the defaults
         */
        swdiag_api_test_default(test_name, instance_name);
        break;
    default:
        /* Invalid */
        break;
    }   
    swdiag_obj_db_unlock();
}

void swdiag_cli_local_enable_disable_action (const char *action_name,
                                             cli_state_t state,
                                             const char *instance_name) 
{
    swdiag_obj_db_lock();
    switch(state) {
    case CLI_STATE_ENABLED:
        swdiag_api_action_enable_guts(action_name, instance_name, TRUE);
        break;
    case CLI_STATE_DISABLED:
        swdiag_api_action_disable_guts(action_name, instance_name, TRUE);
        break;
    case CLI_STATE_INITIALIZED:
        /*
         * Revert to the defaults
         */
        swdiag_api_action_default(action_name, instance_name);
        break;
    default:
        /* Invalid */
        break;
    }    
    swdiag_obj_db_unlock();
}    

void swdiag_cli_local_enable_disable_comp (const char *comp_name,
                                           cli_state_t state) 
{
    swdiag_obj_db_lock();
    switch(state) {
    case CLI_STATE_ENABLED:
        swdiag_api_comp_enable_guts(comp_name, TRUE);
        break;
    case CLI_STATE_DISABLED:
        swdiag_api_comp_disable_guts(comp_name, TRUE);
        break;
    case CLI_STATE_INITIALIZED:
        /*
         * Revert to the defaults
         */
        swdiag_api_comp_default(comp_name);
        break;
    default:
        break;
    }
    swdiag_obj_db_unlock();
}    

void swdiag_cli_local_enable_disable_rule (const char *rule_name,
                                           cli_state_t state,
                                           const char *instance_name) 
{
    swdiag_obj_db_lock();
    switch(state) {
    case CLI_STATE_ENABLED:
        swdiag_api_rule_enable_guts(rule_name, instance_name, TRUE);
        break;
    case CLI_STATE_DISABLED:
        swdiag_api_rule_disable_guts(rule_name, instance_name, TRUE);
        break;
    case CLI_STATE_INITIALIZED:
        /*
         * Revert to the defaults
         */
        swdiag_api_rule_default(rule_name, instance_name);
        break;
    default:
        break;
    }    
    swdiag_obj_db_unlock();
}    

/*
 * swdiag_get_instance_option_tbl_local
 * This function gets a list of all object instances. 
 */ 
static cli_obj_name_t *
swdiag_cli_local_get_instance_option_tbl (cli_handle_t *handle, 
                                          unsigned int max_mtu)
{
    cli_obj_name_t *cli_obj_name;
    unsigned int count;
    obj_rel_t rel;
    cli_obj_name_element_t *element, *last_element;
    obj_instance_t *instance;
    cli_type_t type;
    boolean clean_up_handle = FALSE;

    if (!handle || !handle->instance) {
        return (NULL);
    }
    
    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);
    
    type = swdiag_cli_instance_type_to_type(handle->type);
    if (type == CLI_UNKNOWN) {
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        return (NULL);
    }    

    cli_obj_name = calloc(1, sizeof(cli_obj_name_t));

    if (!cli_obj_name) {
        swdiag_error("Invalid CLI handle");
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        return (NULL);
    }    


    swdiag_obj_db_lock();
    rel = cli_to_rel_type (handle->type);
    count = 0;
    cli_obj_name->elements = NULL;
    last_element = NULL;
    instance = handle->instance;
    while (instance && count < max_mtu) {
        element = calloc(1, sizeof(cli_obj_name_element_t));
        if (!element) {
            swdiag_error("No memory to allocate an element in optiontbl local");
            clean_up_handle = TRUE;
            break;
        }    

        element->name = instance->name;
        element->help = NULL;
        element->next = NULL;

        count++;

        if (!cli_obj_name->elements) {
            cli_obj_name->elements = element;
        }
        if (last_element) {
            last_element->next = element;
        }
        last_element = element;

        switch (handle->type) {
        case CLI_TEST_INSTANCE:
        case CLI_ACTION_INSTANCE:
        case CLI_RULE_INSTANCE:
            instance = (void *)instance->next;
            if (!instance || 
                !swdiag_obj_instance_validate(instance, 
                                              cli_to_obj_type(type))) {
                clean_up_handle = TRUE;
            } else {
                handle_set_instance(handle, instance);
            }
            break;
        default:
            swdiag_error("Wrong type (%d) is received to build local "
                         "option table", handle->type);
            clean_up_handle = TRUE;
            break;
        }    
    } /* end of while */    
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    if (clean_up_handle) {
        handle_clean_up(handle);
    }    
    cli_obj_name->num_elements = count;
    swdiag_obj_db_unlock();
    return (cli_obj_name);
}    
   
/*
 * swdiag_get_option_tbl_local
 * This function gets a list of all object names. 
 */ 
cli_obj_name_t *swdiag_cli_local_get_option_tbl (unsigned int handle_id, 
                                                 unsigned int max_mtu)
{
    cli_obj_name_t *cli_obj_name;
    unsigned int count;
    obj_t *obj = NULL, *remote_obj = NULL; /* Keeps track of remote obj.*/
    obj_rel_t rel;
    cli_obj_name_element_t *element, *last_element;
    char path[SWDIAG_MAX_NAME_LEN];
    obj_instance_t *instance;
    cli_handle_t *handle;
    boolean clean_up_handle = FALSE;
    
    handle = swdiag_cli_local_handle_get(handle_id); 
    if (!handle) {
        return (NULL);
    }

    swdiag_xos_time_set_now(&handle->handle_used_last_time);
    swdiag_cli_local_handle_set_in_use_flag(handle);

    if (handle->type == CLI_TEST_INSTANCE ||
        handle->type == CLI_ACTION_INSTANCE ||
        handle->type == CLI_RULE_INSTANCE) {
        cli_obj_name = 
            swdiag_cli_local_get_instance_option_tbl(handle, max_mtu);
        /* handle_in_use_flag is taken care in called function so no need 
         * to reset
         */ 
        return (cli_obj_name);
    }    

    cli_obj_name = calloc(1, sizeof(cli_obj_name_t));
    if (!cli_obj_name) {
        swdiag_error("Not enough memory to allocate cli names");
        swdiag_cli_local_handle_reset_in_use_flag(handle);
        handle_clean_up(handle);
        return (NULL);
    }    

    swdiag_obj_db_lock();
    instance = handle->instance;
    if (instance) {
        obj = instance->obj;
    }
    rel = cli_to_rel_type(handle->type);
    
    count = 0;
    cli_obj_name->elements = NULL;
    last_element = NULL;

    while (obj && count < max_mtu) {
        element = calloc(1, sizeof(cli_obj_name_element_t));
        if (!element) {
            swdiag_error("No memory to allocate an element in optiontbl local");
            clean_up_handle = TRUE;
            break;
        }    

        swdiag_debug(NULL, "Table: name=%s rel=%d, remote=%d obj=%p",
                     obj->i.name, rel == OBJ_REL_COMP, 
                     obj->remote_location, obj);

        if (rel == OBJ_REL_COMP && obj->remote_location) { 
            swdiag_xos_sstrncpy(path, obj->i.name, SWDIAG_MAX_NAME_LEN);
            element->name = strdup(swdiag_xos_sstrncat(path, "/", 
                                                       SWDIAG_MAX_NAME_LEN)); 
            element->help = "Remote Location";
        } else {        
            element->name = obj->i.name;
            element->help = NULL;
        }
        element->next = NULL;

        count++;

        if (!cli_obj_name->elements) {
            cli_obj_name->elements = element;
        }
        if (last_element) {
            last_element->next = element;
        }
        last_element = element;

        switch (handle->type) {
        case CLI_TEST:
        case CLI_ACTION:
        case CLI_RULE:
        case CLI_COMPONENT:
            obj = swdiag_obj_get_next_rel(obj, rel);
            if (!obj || 
                !swdiag_obj_validate(obj, cli_to_obj_type(handle->type))) {
                handle_set_instance(handle, NULL);
            } else {
                handle_set_instance(handle, &obj->i);
            }
            break;    
        default:
            swdiag_error("\n Wrong type (%d) is received to build local "
                "option table", handle->type);
            clean_up_handle = TRUE;
            /* NULL obj to terminate while loop */
            obj = NULL;
            break;
        }    
    }    

    /* if it is not component object then get a list of all remote component
     * so that it can be displayed along with non component objects for user 
     * convenience. If instance then don't get it.
     */
    if (rel != OBJ_REL_COMP && count < max_mtu) {
        remote_obj = handle->last_remote_obj;
        while (remote_obj && count < max_mtu) {
            /* This handle has remote entries so reinit flag to FALSE */
            clean_up_handle = FALSE;
            if (remote_obj->remote_location) {
                element = calloc(1, sizeof(cli_obj_name_element_t));
                if (!element) {
                    swdiag_error("No memory to allocate an element in "
                        "option table local");
                    clean_up_handle = TRUE;
                    break;
                }    
                swdiag_xos_sstrncpy(path, remote_obj->i.name, 
                    SWDIAG_MAX_NAME_LEN);
                element->name = strdup(swdiag_xos_sstrncat(path, "/", 
                        SWDIAG_MAX_NAME_LEN)); 
                element->help = "Remote Location";
                element->next = NULL;

                count++;

                if (!cli_obj_name->elements) {
                    cli_obj_name->elements = element;
                }
                if (last_element) {
                    last_element->next = element;
                }
                last_element = element;
            }    
            remote_obj = swdiag_obj_get_next_rel(remote_obj, 
                OBJ_REL_NEXT_IN_SYS);
            if (!remote_obj || 
                !swdiag_obj_validate(remote_obj, OBJ_TYPE_COMP)) {
                clean_up_handle = TRUE;
            } else {    
                handle_set_last_remote_obj(handle, remote_obj);
            }
        } /* end of while (remote_obj) */        
    }
    
    swdiag_cli_local_handle_reset_in_use_flag(handle);
    if (clean_up_handle) {
        handle_clean_up(handle);
    }    
    
    cli_obj_name->num_elements = count; 
    swdiag_obj_db_unlock();
    return (cli_obj_name);
}    

/*
 * action_fn()
 * To be used only for internal testing purpose.
 *
 * Do nothing, dummy action.
 */
static swdiag_result_t action_fn (const char *instance_name, void *context)
{
    swdiag_error("\nAction function got executed for %s", 
                 instance_name ? instance_name : "");
    return(SWDIAG_RESULT_PASS);
}


/*
 * test_fn()
 * To be used only for internal testing purpose.
 *
 * Do nothing, dummy action.
 */
static swdiag_result_t test_fn (const char *instance_name, 
                                void *context, long *retval)
{
    polled_test_context *p_context = (polled_test_context *)context;
    if (p_context == NULL) {
        swdiag_error("NULL context for test callback '%s'", 
                     instance_name ? instance_name : ""); 
        return(SWDIAG_RESULT_FAIL);
    }    
    p_context->counter++;
    swdiag_error("%s : Number of times called %d", 
         instance_name ? instance_name : "", p_context->counter);
    return(p_context->result);
}

/*
 * swdiag_cli_local_update_polled_test_context()
 *
 *
 */ 
static void swdiag_cli_local_update_polled_test_context
                                     (const char *test_name, 
                                     const char *instance_name, 
                                     swdiag_result_t result,
                                     long value)
{
    polled_test_context *context;
    context = (polled_test_context *)swdiag_api_test_get_context(test_name);
    if (context == NULL) {
        swdiag_error("NULL context for test '%s'", 
                     test_name ? test_name : ""); 
        return; 
    }    
    context->result = result;
    context->value = value;
}    

/*
 * swdiag_cli_local_delete_created_obj();
 *
 */ 
static void swdiag_cli_local_delete_created_obj (const char *obj_name, 
                                             const char *instance_name)
{
    obj_t *obj;
    const char fnstr[] = "Delete Created OBJ";
    

    if (NULLSTR(obj_name)) {
        swdiag_error("%s - Bad object name", fnstr);
        return;
    }    


    obj = swdiag_obj_get_by_name_unconverted(obj_name, OBJ_TYPE_ANY);
    if (!obj) {
        swdiag_error("%s - Object does not exist for '%s'", fnstr, obj_name); 
        return;
    }    

    if (!(obj->i.flags.any & OBJ_FLAG_TEST_CREATED)) {
        swdiag_error("%s - Object can't be deleted as it is not "
            "internally created by user", fnstr); 
        return;
    }    

    if (!NULLSTR(instance_name)) {
        /* delete an instance and return */
        swdiag_instance_delete(obj_name, instance_name);
        return;
    }    

    switch (obj->type) {
    case OBJ_TYPE_TEST:
        swdiag_test_delete(obj_name);
        break;    
    case OBJ_TYPE_RULE:
        swdiag_rule_delete(obj_name);
        break;    
    case OBJ_TYPE_ACTION:
        swdiag_action_delete(obj_name);
        break;    
    case OBJ_TYPE_COMP:
        swdiag_comp_delete(obj_name);
        break;    
    case OBJ_TYPE_ANY:
    case OBJ_TYPE_NONE:
        break;
    }    
}

void swdiag_cli_local_set_obj_flag (const char *obj_name, obj_type_t type,
                                obj_flag_t flag)
{
    obj_t *obj;
    const char fnstr[] = "Local set flag";
    
    if (NULLSTR(obj_name)) {
        swdiag_error("%s - Bad object name", fnstr);
        return;
    }    
    
    if ((flag != OBJ_FLAG_BUILT_IN)     &&
        (flag != OBJ_FLAG_CONFIGURED)   &&
        (flag != OBJ_FLAG_SILENT)       &&
        (flag != OBJ_FLAG_NOTIFY)       &&
        (flag != OBJ_FLAG_TEST_CREATED) &&
        (flag != OBJ_FLAG_RESERVED)) {
        return ;
    }    

    obj = swdiag_obj_get_by_name_unconverted(obj_name, type);
    if (!obj) {
        return;
    }    

    obj->i.flags.any |= flag;
}    

/*
 * Create various objects as requested from internal CLI test command
 * For rule create, cli_name1, cli_name2, cli_name3 indicates rule, test,
 * and action name. 
 * For depend create, cli_name1, cli_name2 are parent and child names.
 * For test and action create, cli_name1 is test or action name.
 */ 
void swdiag_cli_local_test_command_internal (
    swdiag_cli_test_cmd_t cli_cmd, 
    swdiag_cli_test_cmd_t cmd_period, 
    unsigned int value,
    const char *cli_name1, 
    const char *cli_name2, 
    const char *cli_name3)
{
    const char fnstr[] = "create test cmd local";
    unsigned int period = 0;
    polled_test_context *context = NULL; 

    switch (cli_cmd) {
    case SWDIAG_EXEC_DEPEND_CREATE:
        swdiag_depend_create(cli_name1, cli_name2);
        break;
    case SWDIAG_EXEC_RULE_CREATE:
        swdiag_rule_create(cli_name1, cli_name2, cli_name3);
        swdiag_rule_enable(cli_name1, NULL);
        swdiag_cli_local_set_obj_flag(cli_name1, OBJ_TYPE_RULE,
                                  OBJ_FLAG_TEST_CREATED);
        break;
    case SWDIAG_EXEC_ACTION_CREATE: 
        swdiag_action_create(cli_name1, action_fn, NULL);
        swdiag_action_enable(cli_name1, NULL);
        swdiag_cli_local_set_obj_flag(cli_name1, OBJ_TYPE_ACTION,
                                  OBJ_FLAG_TEST_CREATED);
        break;
    case SWDIAG_EXEC_COMP_CREATE: 
        swdiag_comp_create(cli_name1);
        swdiag_comp_enable(cli_name1);
        swdiag_cli_local_set_obj_flag(cli_name1, OBJ_TYPE_COMP,
                                  OBJ_FLAG_TEST_CREATED);
        break;
    case SWDIAG_EXEC_TEST_NOTIFY_CREATE: 
        swdiag_test_create_notification(cli_name1);
        swdiag_test_enable(cli_name1, NULL);
        swdiag_cli_local_set_obj_flag(cli_name1, OBJ_TYPE_TEST,
                                  OBJ_FLAG_TEST_CREATED);
        break;
    case SWDIAG_EXEC_TEST_POLL_CREATE: 
        if (cmd_period == SWDIAG_EXEC_PERIOD_NORMAL) {
            period = SWDIAG_PERIOD_NORMAL;
        } else if (cmd_period == SWDIAG_EXEC_PERIOD_FAST) {
            period = SWDIAG_PERIOD_FAST;
        } else if (cmd_period == SWDIAG_EXEC_PERIOD_SLOW) {
            period = SWDIAG_PERIOD_SLOW;
        } else {
            period = value;
        }    
        /* Leaking memory. This will be called by 
         * test guys for internal test only.
         */ 
        context = calloc(1, sizeof(polled_test_context));
        swdiag_test_create_polled(cli_name1, test_fn, 
                                 (void *)context, period);
        swdiag_test_enable(cli_name1, NULL);
        swdiag_cli_local_set_obj_flag(cli_name1, OBJ_TYPE_TEST,
                                  OBJ_FLAG_TEST_CREATED);
        break;
    case SWDIAG_EXEC_INSTANCE_CREATE: 
        /* Leaking memory. This will be called by 
         * test guys for internal test only.
         * I am not verifying if it is polled or notification test.
         * Just pass a context
         */ 
        context = calloc(1, sizeof(polled_test_context));
        swdiag_instance_create(cli_name1, cli_name2, (void *)context);
        break;
    case SWDIAG_EXEC_COMP_CONTAINS: 
        swdiag_comp_contains(cli_name1, cli_name2);
        break;
    case SWDIAG_EXEC_PERIOD_NORMAL:
    case SWDIAG_EXEC_PERIOD_FAST:
    case SWDIAG_EXEC_PERIOD_SLOW:    
        break;
    case SWDIAG_EXEC_TEST_NOTIFY_FAIL:    
        if (NULLSTR(cli_name2)) {
            swdiag_test_notify(cli_name1, NULL, SWDIAG_RESULT_FAIL, value);
        } else {
            swdiag_test_notify(cli_name1, cli_name2, SWDIAG_RESULT_FAIL, value);

        }
        break;
    case SWDIAG_EXEC_TEST_NOTIFY_PASS:    
        if (NULLSTR(cli_name2)) {
            swdiag_test_notify(cli_name1, NULL, SWDIAG_RESULT_PASS, value);
        } else {
            swdiag_test_notify(cli_name1, cli_name2, SWDIAG_RESULT_PASS, value);

        }
        break;
    case SWDIAG_EXEC_TEST_POLLED_FAIL:    
        swdiag_cli_local_update_polled_test_context(cli_name1, cli_name2, 
            SWDIAG_RESULT_FAIL, value);
        break;
    case SWDIAG_EXEC_TEST_POLLED_PASS:    
        swdiag_cli_local_update_polled_test_context(cli_name1, cli_name2, 
            SWDIAG_RESULT_PASS, value);
        break;
    case SWDIAG_EXEC_TEST_POLLED_ABORT:    
        swdiag_cli_local_update_polled_test_context(cli_name1, cli_name2, 
            SWDIAG_RESULT_ABORT, value);
        break;
    case SWDIAG_EXEC_TEST_POLLED_VALUE:    
        swdiag_cli_local_update_polled_test_context(cli_name1, cli_name2, 
            SWDIAG_RESULT_VALUE, value);
        break;
    case SWDIAG_EXEC_DELETE:
        swdiag_cli_local_delete_created_obj(cli_name1, cli_name2);
        break;
        
    } /* switch(cli_cmd) */    
    swdiag_debug(NULL, "%s - cmd %d period %d", fnstr, cli_cmd, period);
}    


cli_handle_t 
*swdiag_cli_local_handle_get (unsigned int handle_id)
{
    swdiag_list_element_t *current;
    cli_handle_t *handle = NULL;
    boolean found = FALSE;

    if (handles_in_use == NULL || handle_id == 0) {
        /* List does not exist */
        return (handle);
    }    
    swdiag_xos_critical_section_enter(handles_in_use->lock);
    current = handles_in_use->head; 
    while (current) {
        handle = (cli_handle_t *)current->data;
        if (!handle) {
            break;
        }    
        if (handle->handle_id == handle_id) {
            found = TRUE;
            break;
        } else {
            current = current->next;
        }    
    }    
    swdiag_xos_critical_section_exit(handles_in_use->lock);
    if (found) {
        return (handle);
    } else {
        return (NULL);
    }    
}    

/*
 * Check if handle can be freed if lying in queue. 
 */
static boolean swdiag_cli_handle_free_check (cli_handle_t *handle)
{
    xos_time_t time_now, delay;

    if (handle == NULL) {
        return (FALSE);
    }    
    swdiag_xos_time_set_now(&time_now);
    swdiag_xos_time_diff(&handle->handle_used_last_time, &time_now, &delay);
    if (delay.sec >= CLI_DAY_SEC) {
        return (TRUE);
    } else {
        return (FALSE);       
    }
}

/*
 * During CLI display user can quit display in the middle or may not hit space
 * bar to scroll the entire display. This can leave handle allocated in the
 * list. Also object database elements in the handle may have references to
 * objects which no longer needed or deleted. If such handles are not freed then
 * those can exist forever. This function should check such and free if not
 * needed. How do we decide if they no longer needed ? If this function finds
 * out that handle is in use then it can take a time stamp and if it finds out a
 * diff of 12hrs then handle can be deleted.
 */ 
void swdiag_cli_local_handle_free_garbage (void)
{
    cli_handle_t *handle;
    cli_type_t type;
    unsigned int count = 0;
    swdiag_list_element_t *current, *temp_current;

    if (handles_in_use == NULL) {
        /* List does not exist */
        return;
    }    
    current = handles_in_use->head; 
    if (current) {
        handle = (cli_handle_t *)current->data;
    } else {
        return;
    }    
    swdiag_xos_critical_section_enter(handles_in_use->lock);
    while (handle && (count < CLI_HANDLE_FREE_COUNT)) {
        /* if sanity checks is TRUE then free handle */
        temp_current = current->next; 
        if (swdiag_cli_handle_free_check(handle)) {
            type = handle->type;
            if (swdiag_cli_local_handle_free(handle)) {
                /* if handle is freed then walk list from head as the
                 * pointers may be pointing to garbage
                 */
                temp_current = handles_in_use->head;
            }    
        }
        count++;
        current = temp_current;
        if (current) {
            handle = (cli_handle_t *)current->data;
        } else {
            break;
        }    
    } /* While (handle) */    
    swdiag_xos_critical_section_exit(handles_in_use->lock);
}    

void swdiag_cli_local_debug_enable (const char *name)
{
    swdiag_debug_enable();
    if (name) {
        swdiag_debug_add_filter(name);
    }
}

void swdiag_cli_local_debug_disable (const char *name)
{
    if (name) {
        swdiag_debug_remove_filter(name);
    } else {
        swdiag_debug_disable();
    }
}

cli_debug_t *swdiag_cli_local_debug_get (void)
{
    cli_debug_t *debugs = NULL;
    const swdiag_list_t *filters = NULL;
    int i;
    swdiag_list_element_t *element;
    char *ptr;

    if (swdiag_debug_enabled()) {
        debugs = calloc(1, sizeof(cli_debug_t));
        filters = swdiag_debug_filters_get();
        if (debugs && filters && filters->num_elements > 0) {
            debugs->filters = calloc(filters->num_elements, (sizeof(char*) * SWDIAG_MAX_NAME_LEN));
            if (debugs->filters) {
                for(element = filters->head, i = 0; element && i < filters->num_elements; i++) {
                    ptr = debugs->filters + (i * SWDIAG_MAX_NAME_LEN);
                    swdiag_xos_sstrncpy(ptr, (char*)element->data, SWDIAG_MAX_NAME_LEN);
                    element = element->next;
                }
                debugs->num_filters = i;
            }
        }
    }
    return(debugs);
}

const char *swdiag_cli_state_to_str(cli_state_t state) {
    switch(state) {
    case CLI_STATE_ALLOCATED:
        return "Allocated";
    case CLI_STATE_INITIALIZED:
        return "Initialised";
    case CLI_STATE_CREATED:
        return "Created";
    case CLI_STATE_ENABLED:
        return "Enabled";
    case CLI_STATE_DISABLED:
        return "Disabled";
    case CLI_STATE_DELETED:
        return "Deleted";
    case CLI_STATE_INVALID:
        return "Invalid";
    }
}
