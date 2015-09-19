/*
 * Software Diagnostics Quick Start Example11
 *
 * Dependencies between rules and components for Root Cause Identification
 * 
 */
#include "examples.h"
#include "myrefl_client.h"

/*
 * Foo subsystem
 */
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

static myrefl_result_t example_foo_action (const char *instance, void *context)
{
    if (fix_foo()) {
        return(MYREFL_RESULT_PASS);
    } else {
        return(MYREFL_RESULT_FAIL);
    }
}

void example11_dependencies_foo (void)
{
    myrefl_test_create_notification("Example11FooErrorMsg");

    myrefl_rule_create("Example11FooErrorRule",
                       "Example11FooErrorMsg",
                       MYREFL_ACTION_NOOP);

    myrefl_action_create("Example11FooAction",
                         example_foo_action,
                         NULL);

    myrefl_test_create_polled("Example11FooTest",
                              example_test,
                              0,
                              MYREFL_PERIOD_SLOW);

    myrefl_rule_create("Example11FooRule",
                       "Example11FooTest",
                       "Example11FooAction");

    /*
     * The error message depends on the polled test, so if the polled test
     * is failing then ignore the error message. If we get an error message
     * then immediately as part of Root Cause Identification run the polled
     * test to see whether Foo is failed.
     *
     * So in this case we are using the error message as a quick way of 
     * checking that Foo is working at the first sign of trouble rather than
     * waiting for the polled test to run.
     */
    myrefl_depend_create("Example11FooErrorRule",
                         "Example11FooRule");

    myrefl_comp_create("Example11FooComp");
    myrefl_comp_contains_many("Example11FooComp",
                              "Example11FooErrorMsg",
                              "Example11FooErrorRule",
                              "Example11FooAction",
                              "Example11FooTest",
                              "Example11FooRule",
                              NULL);
    myrefl_test_chain_ready("Example11FooErrorMsg");
    myrefl_test_chain_ready("Example11FooTest");
}

/*
 * Bar subsystem
 */
extern int fix_bar(void);

void example11_bar_failed (int failed)
{
    if (failed) {
        myrefl_test_notify("Example11BarTest", NULL, MYREFL_RESULT_FAIL, 0);
    } else {
        myrefl_test_notify("Example11BarTest", NULL, MYREFL_RESULT_PASS, 0);
    }
}

static myrefl_result_t example_bar_action (const char *instance, void *context)
{
    if (fix_bar()) {
        return(MYREFL_RESULT_PASS);
    } else {
        return(MYREFL_RESULT_FAIL);
    }
}

void example11_dependencies_bar (void)
{
    myrefl_test_create_notification("Example11BarTest");
    
    myrefl_action_create("Example11BarAction",
                         example_bar_action,
                         NULL);

    myrefl_rule_create("Example11BarRule",
                       "Example11BarTest",
                       "Example11BarAction");

    /*
     * Bar has a dependency on the Foo component. If there is a root cause 
     * in the Foo component then it is deemed the root cause of Bar failing.
     * IF Foo has any further dependencies then those are followed until a 
     * root cause is found.
     */
    myrefl_depend_create("Example11BarRule",
                         "Example11FooComp");

    myrefl_test_chain_ready("Example11BarTest");
}
