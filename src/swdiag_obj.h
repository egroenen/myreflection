/* 
 * swdiag_obj.h - SW Diagnostics Object module header
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
 * Internal Object DB declarations, not to be used outside of swdiag.
 */

#ifndef __SWDIAG_OBJ_H__
#define __SWDIAG_OBJ_H__

#include "swdiag_client.h"
#include "swdiag_types.h"
#include "swdiag_sched.h"
#include "swdiag_xos.h"

#define SWDIAG_COMPONENT "Software Diagnostics"

/*
 * Extend the test/rule/action flags with the upper word of 
 * flags that apply to all types. The user is not exposed to
 * this type, although they can see the flags themselves.
 */
typedef enum obj_flags_t_ {
    OBJ_FLAG_BUILT_IN      = 0x00010000,
    OBJ_FLAG_CONFIGURED    = 0x00020000,  // Not used yet
    OBJ_FLAG_SILENT        = 0x00040000,  // No report on fail
    OBJ_FLAG_NOTIFY        = 0x00060000,  // Notify to interested clients.
    OBJ_FLAG_TEST_CREATED  = 0x00080000,  // Objs created via internal test cmd
    OBJ_FLAG_RESERVED      = 0xFFFF0000,
} obj_flag_t;

typedef struct obj_history_s {
    xos_time_t time;
    swdiag_result_t result;
    unsigned int count;
    long value;
} obj_history_t;

#define OBJ_HISTORY_SIZE 5

typedef struct obj_stats_s {
    unsigned int runs;
    unsigned int passes;
    unsigned int failures;
    unsigned int aborts;
    obj_history_t history[OBJ_HISTORY_SIZE];
    unsigned int history_head;
} obj_stats_t;

/* identifies an object type */
typedef enum obj_type_e {
    OBJ_TYPE_ANY = 0,
    OBJ_TYPE_NONE = 1, /* object without type */
    OBJ_TYPE_TEST,
    OBJ_TYPE_RULE,
    OBJ_TYPE_ACTION,
    OBJ_TYPE_COMP,
} obj_type_t;

/* identifies a linked object relative to another object */
typedef enum obj_rel_e {
    OBJ_REL_NONE = 1, /* values aligned with obj_type_t */
    OBJ_REL_TEST,            // Start off getting tests
    OBJ_REL_RULE,            // Start off getting rules
    OBJ_REL_ACTION,          // Start off getting actions
    OBJ_REL_COMP,            // Start off getting components
    OBJ_REL_NEXT_IN_SYS,     // Get the next "whatever" constrained to system
    OBJ_REL_NEXT_IN_COMP,    // Get the next "whatever" constrained to comp
    OBJ_REL_NEXT_IN_TEST,    // Get the next "whatever" constrained to test
    OBJ_REL_PARENT_COMP,
    OBJ_REL_CHILD_COMP,
} obj_rel_t;

/*
 * State that the test/action/rule/component may be in.
 */
typedef enum obj_state_e {
    OBJ_STATE_ALLOCATED = 1,  /* object allocated but not yet initialized */
    OBJ_STATE_INITIALIZED,    /* object initialized but not yet created */
    OBJ_STATE_CREATED,        /* object created, not enabled or disabled yet */
    OBJ_STATE_ENABLED,        /* object fully created and enabled */
    OBJ_STATE_DISABLED,       /* object fully created but disabled */
    OBJ_STATE_DELETED,        /* object (partially/fully created) is deleted */
    OBJ_STATE_INVALID,        /* object is in error */
} obj_state_t;

typedef enum rule_root_cause_e {
    RULE_ROOT_CAUSE_NOT,
    RULE_ROOT_CAUSE_CANDIDATE,
    RULE_ROOT_CAUSE,
} rule_root_cause_t;

/*************************************************************************
 * Generic header object for Tests, Actions, Rules and Components
 */

typedef struct obj_rule_data_t_ {
    unsigned char *history;
    long history_size;
    long position;
} obj_rule_data_t;

/*
 * Object instance, where there may be one or more instances per object.
 */
struct obj_instance_s {
    obj_t *obj;
    char *name;

    void *context;

    obj_state_t state;               // Current state
    obj_state_t default_state;       // State requested by application
    obj_state_t cli_state;           // State requested by the CLI

    union {
        swdiag_test_flags_t   test;
        swdiag_action_flags_t action;
        swdiag_rule_flags_t   rule;
        obj_flag_t            any;
    } flags;

    obj_stats_t stats;

    swdiag_result_t  last_result;     // Last result
    long             last_value;      // Last value, if appropriate
    unsigned int     last_result_count; // How many times in a row
    unsigned int     fail_count;      // How many failures (rule based)
    obj_rule_data_t *rule_data;       // Data that the rule needs to evaluate

    sched_test_t    sched_test;

    rule_root_cause_t root_cause;
    boolean action_run;              // Action has been run for root cause.
    unsigned int in_use;             // Instance is being referenced

    obj_instance_t *next;
    obj_instance_t *prev;
};

struct obj_s {
    unsigned int ident;
    obj_type_t type;
    char *description;

    boolean remote_location; /* Set for distributed model */

    obj_t *next_in_comp;   /* next of the same type of struc in component */
    obj_comp_t *parent_comp; /* what component is this a member of */
    
    swdiag_list_t *parent_depend; /* for rules and comps */
    swdiag_list_t *child_depend;  /* for rules and comps */

    /*
     * Temporary reference to a newly created rule during the pending
     * creation of this object, whilst the type is not yet known.
     */
    obj_rule_t *ref_rule;

    /*
     * which domain is this object in for cyclic loop detection
     * of dependencies. 0 is no domain.
     */
    uint domain;

    /*
     * Zero or one of these pointers will point to the
     * specific object data, according to the object type
     * (this could also be a union)
     */
    union {
        obj_test_t *test;
        obj_action_t *action;
        obj_rule_t *rule;
        obj_comp_t *comp;
    } t;

    obj_instance_t i;   // Parent instance
};

/*************************************************************************
 * Types and structures for Test objects
 */
typedef enum obj_test_type_e  {
    OBJ_TEST_TYPE_POLLED,
    OBJ_TEST_TYPE_NOTIFICATION,
    OBJ_TEST_TYPE_ERRMSG,
} obj_test_type_t;

#define AUTOPASS_UNSET -1

struct obj_test_s {
    unsigned int ident;
    obj_t *obj;
    obj_test_type_t type;
    obj_rule_t *rule;
    swdiag_test_t *function;
    unsigned long period;          /* configured period */
    unsigned long default_period;  /* default period */
    long          autopass;        /* Auto pass delay */
};

/*************************************************************************
 * Types and structures for Action objects
 *
 * There is not currently an action instance since there is no action 
 * specific variant data at this time.
 */
struct obj_action_s {
    unsigned int ident;
    obj_t *obj;
    swdiag_action_t *function;
    swdiag_list_t *rule_list;   // List of rule_objs pointing at this action 
};


/*************************************************************************
 * Types and structures for Rule objects
 *
 * Rules are triggered whenever an "input" changes, at which point they 
 * execute the "operator" with its operands "op_n" and "op_m" (if required).
 * 
 * The rule results are passed to Root Cause Identification, and if this
 * rule is the root cause then the "action" is executed.
 *
 * The rule result is also passed to the "output" if present so that any
 * rules that have this rule as input may be re-evaluated.
 *
 * Once this rule has been evaluated we process the "next_in_input" rule
 * as well since it is dependent on the same "input" as this. Note that the
 * "input" only records one "output" so we are dependent on this rule to
 * chain to subsequent ones.
 */

struct obj_rule_s {
    unsigned int ident;
    obj_t *obj;
    
    swdiag_rule_operator_t operator;
    swdiag_rule_operator_t default_operator;
    long op_n;
    long op_m;
    long default_op_n;
    long default_op_m;
    
    swdiag_list_t *inputs;      /* input trigger is a test or rule */
    swdiag_list_t *action_list; /* list of recovery actions */
    obj_rule_t *output;  /* subsequent dependent rules */  
    
    obj_rule_t *next_in_input; /* next (parallel) rule from our input */
    
    swdiag_severity_t severity;
};

/*
 * Types and structures for Component objects
 */
struct obj_comp_s {
    unsigned int ident;
    obj_t *obj;
     
    /* element lists contained within this component */
    obj_t *nones;
    obj_t *tests;
    obj_t *actions;
    obj_t *rules;
    obj_t *comps;
    
    /*
     * Keep some stats on which category of rules have failed in 
     * this component
     */
    unsigned int catastrophic;
    unsigned int critical;
    unsigned int high;
    unsigned int medium;
    unsigned int low;
    unsigned int positive;
    
    /*
     * Maintaining lists of which rules/comps are at the top and bottom of a
     * dependency tree allows the RCI code to effieciently expand
     * dependencies on components.
     *
     * rules/comps that have no dependencies are in both, rules/comps that have
     * children within the component are removed from the bottom list,
     * and rules/comps that have parents within the component are removed
     * from the top list.
     *
     * Note that components can and do contain other components and
     * dependencies can be created between components.
     */
    swdiag_list_t *top_depend;
    swdiag_list_t *bottom_depend;

    /*
     * Test objects that want to be notified when there is a change in 
     * health of this component.
     */
    swdiag_list_t *interested_test_objs;

    int health;
    int confidence;  

    /*
     * Threshold notifications used by SNMP.
     */
    unsigned int health_high_threshold;
    unsigned int health_low_threshold;
};

extern obj_comp_t *swdiag_obj_system_comp;

/*******************************************************************
 * Useful object related functions
 *******************************************************************/

const char *swdiag_obj_state_str(obj_state_t state);
const char *swdiag_obj_type_str(obj_type_t type);
const char *swdiag_obj_rel_str(obj_rel_t relative);

/*******************************************************************
 * Objects (common to tests, actions, rules and comps)
 *******************************************************************/

obj_t *swdiag_obj_get_or_create (char *name, obj_type_t type);
obj_t *swdiag_obj_get_by_name(const char *name, obj_type_t type);
obj_t *swdiag_obj_get_by_name_unconverted(const char *name,
                                          obj_type_t type);
boolean swdiag_obj_delete_by_name(const char *name, obj_type_t type);
boolean swdiag_obj_delete_by_name_unconverted(const char *name, 
                                              obj_type_t type);

obj_t *swdiag_obj_get_rel(obj_t *obj, obj_rel_t rel);
obj_t *swdiag_obj_get_first_rel(obj_t *obj, obj_rel_t rel);
obj_t *swdiag_obj_get_next_rel(obj_t *obj, obj_rel_t rel);
obj_t *swdiag_comp_get_first_contained(obj_comp_t *top_comp, obj_type_t type);
obj_t *swdiag_comp_get_next_contained(
    obj_comp_t *top_comp, obj_t *obj, obj_type_t type);

void swdiag_obj_set_desc(obj_t *obj, char *desc);

void swdiag_obj_link(obj_t *obj);
void swdiag_obj_delete(obj_t *obj);
void swdiag_obj_comp_link_obj(obj_comp_t *comp, obj_t *obj);
void swdiag_obj_unlink_from_comp(obj_t *obj);

void swdiag_obj_init(void);
void swdiag_obj_terminate(void);

boolean swdiag_obj_validate(obj_t *obj, obj_type_t type);
boolean swdiag_obj_instance_validate(obj_instance_t *instance, 
                                     obj_type_t type);
boolean swdiag_obj_write_lock(obj_t *obj);
boolean swdiag_obj_write_unlock(obj_t *obj);
boolean swdiag_obj_read_lock(obj_t *obj);
boolean swdiag_obj_read_unlock(obj_t *obj);

void swdiag_obj_chain_update_state(obj_t *obj, obj_state_t state);
obj_instance_t *swdiag_obj_instance_create(obj_t *obj, const char *instance_name);
void swdiag_obj_instance_delete(obj_instance_t *instance);
const char *swdiag_obj_instance_name(obj_instance_t *instance);
obj_instance_t *swdiag_obj_instance(obj_t *obj, obj_instance_t *ref_instance);
obj_instance_t *swdiag_obj_instance_by_name(obj_t *obj, const char *instance_name);
boolean swdiag_obj_is_member_instance(obj_instance_t *instance);
boolean swdiag_obj_has_member_instances(obj_t *obj);
void swdiag_obj_db_lock(void);
void swdiag_obj_db_unlock(void);
#endif /* __SWDIAG_OBJ_H__ */
