/*
 * Software Diagnostics Quick Start Example12
 *
 * In this example the Foo test and recovery action are only applicable
 * to the Standby RP in redundant systems. Whenever the rule fails we
 * want the recovery action run, regardless of whether this rule is 
 * determined to be the root cause or not.
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

void example12_flags (void)
{
    myrefl_action_create("Example12FooAction",
                         example_foo_action,
                         NULL);

    /*
     * If you aren't sure what flags are already set, then get the existing
     * flags, modify them and set them again.
     */
    myrefl_action_set_flags("Example12FooAction", 
                            (myrefl_action_get_flags("Example12FooAction") & 
                             ~MYREFL_ACTION_LOCATION_ALL) |
                            MYREFL_ACTION_LOCATION_STANDBY_RP);

    myrefl_test_create_polled("Example12FooTest",
                              example_test,
                              0,
                              MYREFL_PERIOD_SLOW);

    myrefl_test_set_flags("Example12FooTest", 
                          (myrefl_test_get_flags("Example12FooTest") & 
                           ~MYREFL_TEST_LOCATION_ALL) |
                          MYREFL_TEST_LOCATION_STANDBY_RP);
    
    myrefl_rule_create("Example12FooRule",
                       "Example12FooTest",
                       "Example12FooAction");

    myrefl_rule_set_flags("Example12FooRule",
                          (myrefl_rule_get_flags("Example12FooRule") & 
                           ~MYREFL_RULE_LOCATION_ALL) |
                          (MYREFL_RULE_LOCATION_STANDBY_RP | 
                           MYREFL_RULE_TRIGGER_ALWAYS));

    myrefl_test_chain_ready("Example12FooTest");
}
