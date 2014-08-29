/*
 * Software Diagnostics Quick Start Example7
 *
 * Using swdiag_rule_set_sevrity() to adjust the impact of a failing
 * rule on the owning component.
 * 
 */
#include "examples.h"
#include "swdiag_client.h"

extern long get_foo(void);
extern int check_bar(void);
extern int fix_foo(void);

static swdiag_result_t example_test (const char *instance, void *context, 
                                     long *value)
{
    *value = get_foo();
    return(SWDIAG_RESULT_VALUE);
}

static swdiag_result_t example_test2 (const char *instance, void *context, 
                                      long *value)
{
    if (check_bar()) {
        return(SWDIAG_RESULT_PASS);
    } else {
        return(SWDIAG_RESULT_FAIL);
    }
}

static swdiag_result_t example_action (const char *instance, void *context)
{
    if (fix_foo()) {
        return(SWDIAG_RESULT_PASS);
    } else {
        return(SWDIAG_RESULT_FAIL);
    }
}

void example7_rule_severity (void)
{

    swdiag_comp_create("Example7Comp");

    swdiag_test_create_polled("Example7TestFoo",
                              example_test,
                              0,
                              SWDIAG_PERIOD_NORMAL);

    swdiag_action_create("Example7Action",
                         example_action,
                         0);

    swdiag_rule_create("Example7ThresholdRuleFoo",
                       "Example7Test",
                       SWDIAG_ACTION_NOOP);

    swdiag_rule_set_type("Example7ThresholdRuleFoo",
                         SWDIAG_RULE_LESS_THAN_N, 20, 0);

    /*
     * "Example7ThresholdRuleFoo" failing should not affect the
     * health of the component.
     */
    swdiag_rule_set_severity("Example7ThresholdRuleFoo",
                             SWDIAG_SEVERITY_NONE);

    swdiag_rule_create("Example7TimeRuleFoo",
                       "Example7ThresholdRuleFoo",
                       SWDIAG_ACTION_NOOP);
    
    swdiag_rule_set_type("Example7TimeRuleFoo",
                         SWDIAG_RULE_N_IN_ROW, 4, 0);
    
    /*
     * "Example7TimeRuleFoo" failing is a defect and should
     * have an impact on the health of the component.
     */
    swdiag_rule_set_severity("Example7TimeRuleFoo",
                             SWDIAG_SEVERITY_HIGH);

    swdiag_test_create_polled("Example7TestBar",
                              example_test2,
                              0,
                              SWDIAG_PERIOD_FAST);
    
    swdiag_rule_create("Example7RuleBar",
                       "Example7TestBar",
                       SWDIAG_ACTION_NOOP);

    /*
     * "Example7RuleBar" failing is a defect and so should affect the
     * health of the component.
     */
    swdiag_rule_set_severity("Example7RuleBar",
                             SWDIAG_SEVERITY_HIGH);

    swdiag_rule_create("Example7RuleAnd",
                       "Example7TimeRuleFoo",
                       "Example7Action");

    swdiag_rule_add_input("Example7RuleAnd",
                          "Example7RuleBar");

    swdiag_rule_set_type("Example7RuleAnd",
                         SWDIAG_RULE_AND, 0, 0);

    /*
     * When both "Example7TimeRuleFoo" and "Example7RuleBar" are failing
     * this is a critical event. Note that the health of the component
     * has already been decremented by the fact that these two input
     * rules are failing. However both failing at the same time is 
     * more important than just one failing at a time.
     */
    swdiag_rule_set_severity("Example7RuleAnd",
                             SWDIAG_SEVERITY_CRITICAL);
                         
    swdiag_comp_contains_many("Example7Comp",
                              "Example7TestFoo",
                              "Example7Action",
                              "Example7ThresholdRuleFoo",
                              "Example7TimeRuleFoo",
                              "Example7TestBar",
                              "Example7RuleBar",
                              "Example7RuleAnd",
                              NULL);

    swdiag_test_chain_ready("Example7TestFoo");
    swdiag_test_chain_ready("Example7TestBar");
}
