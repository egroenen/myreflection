/*
 * Software Diagnostics Quick Start Example2
 *
 * A clients notification into example2_foo_failed() triggers a
 * recovery action.
 * 
 */
#include "examples.h"
#include "myrefl_client.h"

extern int fix_foo(void);

void example2_foo_failed (int failed)
{
    if (failed) {
        myrefl_test_notify("Example2Test", NULL, MYREFL_RESULT_FAIL, 0);
    } else {
        myrefl_test_notify("Example2Test", NULL, MYREFL_RESULT_PASS, 0);
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

void example2_notification (void)
{
    myrefl_test_create_notification("Example2Test");
    
    myrefl_action_create("Example2Action",
                         example_action,
                         NULL);

    myrefl_rule_create("Example2Rule",
                       "Example2Test",
                       "Example2Action");

    myrefl_test_chain_ready("Example2Test");

}
