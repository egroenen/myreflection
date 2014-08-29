/*
 * Software Diagnostics Quick Start Example5
 *
 * Chaining rules, run the action if the test value is less than 20
 * 4 times in a row.
 * 
 */
#include "swdiag_client.h"

extern long get_foo(void);
extern int fix_foo(void);

static swdiag_result_t example_test (const char *instance, void *context, 
                                     long *value)
{
    *value = get_foo();
    return(SWDIAG_RESULT_VALUE);
}

static swdiag_result_t example_action (const char *instance, void *context)
{
    if (fix_foo()) {
        return(SWDIAG_RESULT_PASS);
    } else {
        return(SWDIAG_RESULT_FAIL);
    }
}

void example5_chained_rules (void)
{
    swdiag_test_create_polled("Example5Test",
                              example_test,
                              0,
                              SWDIAG_PERIOD_NORMAL);
    
    swdiag_rule_create("Example5ThresholdRule",
                       "Example5Test",
                       SWDIAG_ACTION_NOOP);

    swdiag_rule_set_type("Example5ThresholdRule",
                         SWDIAG_RULE_LESS_THAN_N, 20, 0);

    /*
     * Note that we are referencing the action prior to it being created
     * as an example of a forward reference.
     */
    swdiag_rule_create("Example5TimeRule",
                       "Example5ThresholdRule",
                       "Example5Action");
    
    swdiag_rule_set_type("Example5TimeRule",
                         SWDIAG_RULE_N_IN_ROW, 4, 0);
    
    swdiag_action_create("Example5Action",
                         example_action,
                         0);

    swdiag_test_chain_ready("ExampleTest");
}
