/*
 * Software Diagnostics Quick Start Example6
 *
 * Combining test and rule inputs using logical operators
 * 
 */
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

void example6_combined_rules (void)
{
    swdiag_test_create_polled("Example6TestFoo",
                              example_test,
                              0,
                              SWDIAG_PERIOD_NORMAL);

    swdiag_action_create("Example6Action",
                         example_action,
                         0);

    swdiag_rule_create("Example6ThresholdRuleFoo",
                       "Example6Test",
                       SWDIAG_ACTION_NOOP);

    swdiag_rule_set_type("Example6ThresholdRuleFoo",
                         SWDIAG_RULE_LESS_THAN_N, 20, 0);

    swdiag_rule_create("Example6TimeRuleFoo",
                       "Example6ThresholdRuleFoo",
                       SWDIAG_ACTION_NOOP);
    
    swdiag_rule_set_type("Example6TimeRuleFoo",
                         SWDIAG_RULE_N_IN_ROW, 4, 0);
    

    swdiag_test_create_polled("Example6TestBar",
                              example_test2,
                              0,
                              SWDIAG_PERIOD_FAST);
    
    swdiag_rule_create("Example6RuleBar",
                       "Example6TestBar",
                       SWDIAG_ACTION_NOOP);

    /*
     * Note that we could connect "Example6TestBar" directly to
     * "Example6RuleAnd" without using "Example6RuleBar. However
     * by using the additional rule you leave the option open for the
     * user to reconfigure "Example6RuleBar" to interpret the results
     * from "Example6TestBar" (e.g. using a time filter) should it
     * be required in the future.
     */
    swdiag_rule_create("Example6RuleAnd",
                       "Example6TimeRuleFoo",
                       "Example6Action");

    swdiag_rule_add_input("Example6RuleAnd",
                          "Example6RuleBar");

    swdiag_rule_set_type("Example6RuleAnd",
                         SWDIAG_RULE_AND, 0, 0);
                         
    swdiag_test_chain_ready("Example6TestFoo");
    swdiag_test_chain_ready("Example6TestBar");
}
