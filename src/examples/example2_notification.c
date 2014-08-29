/*
 * Software Diagnostics Quick Start Example2
 *
 * A clients notification into example2_foo_failed() triggers a
 * recovery action.
 * 
 */
#include "examples.h"
#include "swdiag_client.h"

extern int fix_foo(void);

void example2_foo_failed (int failed)
{
    if (failed) {
        swdiag_test_notify("Example2Test", NULL, SWDIAG_RESULT_FAIL, 0);
    } else {
        swdiag_test_notify("Example2Test", NULL, SWDIAG_RESULT_PASS, 0);
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

void example2_notification (void)
{
    swdiag_test_create_notification("Example2Test");
    
    swdiag_action_create("Example2Action",
                         example_action,
                         NULL);

    swdiag_rule_create("Example2Rule",
                       "Example2Test",
                       "Example2Action");

    swdiag_test_chain_ready("Example2Test");

}
