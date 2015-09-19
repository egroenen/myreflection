/*
 * Software Diagnostics Quick Start Example1
 *
 * A clients polled test that when it fails triggers a clients recovery 
 * action.
 *
 */
#include "myrefl_client.h"

extern int check_foo(void);
extern int fix_foo(void);

static myrefl_result_t example_test (const char *instance, void *context, 
                                     long *value)
{
    if (check_foo()) {
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

void example1_polledtest (void)
{
    myrefl_test_create_polled("ExampleTest",
                              example_test,
                              0,
                              MYREFL_PERIOD_NORMAL);
    
    myrefl_action_create("ExampleAction",
                         example_action,
                         0);

    myrefl_rule_create("ExampleRule",
                       "ExampleTest",
                       "ExampleAction");

    myrefl_test_chain_ready("ExampleTest");

}
