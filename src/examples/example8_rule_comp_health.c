/*
 * Software Diagnostics Quick Start Example8
 *
 * Create a component and then monitor the health of that component 
 * in a separate test and threshold rule. Should the health of the 
 * monitored component drop under 50% then alert the user.
 * 
 */
#include "examples.h"
#include "swdiag_client.h"

void example8_foo_failed (int failed)
{
    if (failed) {
        swdiag_test_notify("Example8Test", NULL, SWDIAG_RESULT_FAIL, 0);
    } else {
        swdiag_test_notify("Example8Test", NULL, SWDIAG_RESULT_PASS, 0);
    }
}

void example8_rule_comp_health (void)
{
    swdiag_test_create_notification("Example8Test");

    swdiag_rule_create("Example8Rule",
                       "Example8Test",
                       SWDIAG_ACTION_NOOP);

    swdiag_comp_create("Example8Component");

    swdiag_comp_contains_many("Example8Component",
                              "Example8Test",
                              "Example8Rule",
                              NULL);

    swdiag_test_chain_ready("Example8Test"); 

    swdiag_test_create_comp_health("Example8MonitorCompTest",
                                   "Example8Component");

    swdiag_action_create_user_alert(
        "Example8AlertUser",
        "The Health of the component is low, it may be operating at a "
        "reduced capacity. It is recommended to limit use of this system "
        "until the root cause can be diagnosed.");

    swdiag_rule_create("Example8MonitorCompRule",
                       "Example8MonitorCompTest",
                       "Example8AlertUser");

    /*
     * Trigger the user alert when the health of the monitored component
     * drops under 50%.
     */
    swdiag_rule_set_type("Example8MonitorCompRule",
                         SWDIAG_RULE_LESS_THAN_N, 
                         50, 0);

    swdiag_test_chain_ready("Example8MonitorCompTest"); 
}
