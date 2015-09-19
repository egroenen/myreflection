/*
 * Software Diagnostics Quick Start Example5
 *
 * Chaining rules, run the action if the test value is less than 20
 * 4 times in a row.
 * 
 */
#include "myrefl_client.h"

extern long get_foo(void);
extern int fix_foo(void);

static myrefl_result_t example_test (const char *instance, void *context, 
                                     long *value)
{
    *value = get_foo();
    return(MYREFL_RESULT_VALUE);
}

static myrefl_result_t example_action (const char *instance, void *context)
{
    if (fix_foo()) {
        return(MYREFL_RESULT_PASS);
    } else {
        return(MYREFL_RESULT_FAIL);
    }
}

void example5_chained_rules (void)
{
    myrefl_test_create_polled("Example5Test",
                              example_test,
                              0,
                              MYREFL_PERIOD_NORMAL);
    
    myrefl_rule_create("Example5ThresholdRule",
                       "Example5Test",
                       MYREFL_ACTION_NOOP);

    myrefl_rule_set_type("Example5ThresholdRule",
                         MYREFL_RULE_LESS_THAN_N, 20, 0);

    /*
     * Note that we are referencing the action prior to it being created
     * as an example of a forward reference.
     */
    myrefl_rule_create("Example5TimeRule",
                       "Example5ThresholdRule",
                       "Example5Action");
    
    myrefl_rule_set_type("Example5TimeRule",
                         MYREFL_RULE_N_IN_ROW, 4, 0);
    
    myrefl_action_create("Example5Action",
                         example_action,
                         0);

    myrefl_test_chain_ready("ExampleTest");
}
