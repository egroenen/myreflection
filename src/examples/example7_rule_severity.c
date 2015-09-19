/*
 * Software Diagnostics Quick Start Example7
 *
 * Using myrefl_rule_set_sevrity() to adjust the impact of a failing
 * rule on the owning component.
 * 
 */
#include "examples.h"
#include "myrefl_client.h"

extern long get_foo(void);
extern int check_bar(void);
extern int fix_foo(void);

static myrefl_result_t example_test (const char *instance, void *context, 
                                     long *value)
{
    *value = get_foo();
    return(MYREFL_RESULT_VALUE);
}

static myrefl_result_t example_test2 (const char *instance, void *context, 
                                      long *value)
{
    if (check_bar()) {
        return(MYREFL_RESULT_PASS);
    } else {
        return(MYREFL_RESULT_FAIL);
    }
}

static myrefl_result_t example_action (const char *instance, void *context)
{
    if (fix_foo()) {
        return(MYREFL_RESULT_PASS);
    } else {
        return(MYREFL_RESULT_FAIL);
    }
}

void example7_rule_severity (void)
{

    myrefl_comp_create("Example7Comp");

    myrefl_test_create_polled("Example7TestFoo",
                              example_test,
                              0,
                              MYREFL_PERIOD_NORMAL);

    myrefl_action_create("Example7Action",
                         example_action,
                         0);

    myrefl_rule_create("Example7ThresholdRuleFoo",
                       "Example7Test",
                       MYREFL_ACTION_NOOP);

    myrefl_rule_set_type("Example7ThresholdRuleFoo",
                         MYREFL_RULE_LESS_THAN_N, 20, 0);

    /*
     * "Example7ThresholdRuleFoo" failing should not affect the
     * health of the component.
     */
    myrefl_rule_set_severity("Example7ThresholdRuleFoo",
                             MYREFL_SEVERITY_NONE);

    myrefl_rule_create("Example7TimeRuleFoo",
                       "Example7ThresholdRuleFoo",
                       MYREFL_ACTION_NOOP);
    
    myrefl_rule_set_type("Example7TimeRuleFoo",
                         MYREFL_RULE_N_IN_ROW, 4, 0);
    
    /*
     * "Example7TimeRuleFoo" failing is a defect and should
     * have an impact on the health of the component.
     */
    myrefl_rule_set_severity("Example7TimeRuleFoo",
                             MYREFL_SEVERITY_HIGH);

    myrefl_test_create_polled("Example7TestBar",
                              example_test2,
                              0,
                              MYREFL_PERIOD_FAST);
    
    myrefl_rule_create("Example7RuleBar",
                       "Example7TestBar",
                       MYREFL_ACTION_NOOP);

    /*
     * "Example7RuleBar" failing is a defect and so should affect the
     * health of the component.
     */
    myrefl_rule_set_severity("Example7RuleBar",
                             MYREFL_SEVERITY_HIGH);

    myrefl_rule_create("Example7RuleAnd",
                       "Example7TimeRuleFoo",
                       "Example7Action");

    myrefl_rule_add_input("Example7RuleAnd",
                          "Example7RuleBar");

    myrefl_rule_set_type("Example7RuleAnd",
                         MYREFL_RULE_AND, 0, 0);

    /*
     * When both "Example7TimeRuleFoo" and "Example7RuleBar" are failing
     * this is a critical event. Note that the health of the component
     * has already been decremented by the fact that these two input
     * rules are failing. However both failing at the same time is 
     * more important than just one failing at a time.
     */
    myrefl_rule_set_severity("Example7RuleAnd",
                             MYREFL_SEVERITY_CRITICAL);
                         
    myrefl_comp_contains_many("Example7Comp",
                              "Example7TestFoo",
                              "Example7Action",
                              "Example7ThresholdRuleFoo",
                              "Example7TimeRuleFoo",
                              "Example7TestBar",
                              "Example7RuleBar",
                              "Example7RuleAnd",
                              NULL);

    myrefl_test_chain_ready("Example7TestFoo");
    myrefl_test_chain_ready("Example7TestBar");
}
