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
#include "swdiag_client.h"

/*
 * Foo subsystem
 */
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

static swdiag_result_t example_foo_action (const char *instance, void *context)
{
    if (fix_foo()) {
        return(SWDIAG_RESULT_PASS);
    } else {
        return(SWDIAG_RESULT_FAIL);
    }
}

void example12_flags (void)
{
    swdiag_action_create("Example12FooAction",
                         example_foo_action,
                         NULL);

    /*
     * If you aren't sure what flags are already set, then get the existing
     * flags, modify them and set them again.
     */
    swdiag_action_set_flags("Example12FooAction", 
                            (swdiag_action_get_flags("Example12FooAction") & 
                             ~SWDIAG_ACTION_LOCATION_ALL) |
                            SWDIAG_ACTION_LOCATION_STANDBY_RP);

    swdiag_test_create_polled("Example12FooTest",
                              example_test,
                              0,
                              SWDIAG_PERIOD_SLOW);

    swdiag_test_set_flags("Example12FooTest", 
                          (swdiag_test_get_flags("Example12FooTest") & 
                           ~SWDIAG_TEST_LOCATION_ALL) |
                          SWDIAG_TEST_LOCATION_STANDBY_RP);
    
    swdiag_rule_create("Example12FooRule",
                       "Example12FooTest",
                       "Example12FooAction");

    swdiag_rule_set_flags("Example12FooRule",
                          (swdiag_rule_get_flags("Example12FooRule") & 
                           ~SWDIAG_RULE_LOCATION_ALL) |
                          (SWDIAG_RULE_LOCATION_STANDBY_RP | 
                           SWDIAG_RULE_TRIGGER_ALWAYS));

    swdiag_test_chain_ready("Example12FooTest");
}
