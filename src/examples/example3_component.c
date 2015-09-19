/*
 * Software Diagnostics Quick Start Example3
 *
 * Grouping tests, rules and actions into a component
 * 
 */
#include "examples.h"
#include "myrefl_client.h"

void example3_foo_failed (int failed)
{
    if (failed) {
        myrefl_test_notify("Example3Test", NULL, MYREFL_RESULT_FAIL, 0);
    } else {
        myrefl_test_notify("Example3Test", NULL, MYREFL_RESULT_PASS, 0);
    }
}

void example3_component (void)
{
    myrefl_test_create_notification("Example3Test");

    myrefl_rule_create("Example3Rule",
                       "Example3Test",
                       MYREFL_ACTION_NOOP);

    myrefl_comp_create("Example3Component");

    myrefl_comp_contains_many("Example3Component",
                              "Example3Test",
                              "Example3Rule",
                              NULL);

    myrefl_test_chain_ready("Example3Test"); 
}

