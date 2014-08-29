/*
 * Software Diagnostics Quick Start Example1
 *
 * A clients polled test that when it fails triggers a clients recovery 
 * action.
 *
 */
#include "swdiag_client.h"

extern int check_foo(void);
extern int fix_foo(void);

static swdiag_result_t example_test (const char *instance, void *context, 
                                     long *value)
{
    if (check_foo()) {
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

void example1_polledtest (void)
{
    swdiag_test_create_polled("ExampleTest",
                              example_test,
                              0,
                              SWDIAG_PERIOD_NORMAL);
    
    swdiag_action_create("ExampleAction",
                         example_action,
                         0);

    swdiag_rule_create("ExampleRule",
                       "ExampleTest",
                       "ExampleAction");

    swdiag_test_chain_ready("ExampleTest");

}
