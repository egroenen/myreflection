/*
 * Software Diagnostics Quick Start Example3
 *
 * Grouping tests, rules and actions into a component
 * 
 */
#include "examples.h"
#include "swdiag_client.h"

void example3_foo_failed (int failed)
{
    if (failed) {
        swdiag_test_notify("Example3Test", NULL, SWDIAG_RESULT_FAIL, 0);
    } else {
        swdiag_test_notify("Example3Test", NULL, SWDIAG_RESULT_PASS, 0);
    }
}

void example3_component (void)
{
    swdiag_test_create_notification("Example3Test");

    swdiag_rule_create("Example3Rule",
                       "Example3Test",
                       SWDIAG_ACTION_NOOP);

    swdiag_comp_create("Example3Component");

    swdiag_comp_contains_many("Example3Component",
                              "Example3Test",
                              "Example3Rule",
                              NULL);

    swdiag_test_chain_ready("Example3Test"); 
}

