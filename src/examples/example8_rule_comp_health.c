/*
 * Software Diagnostics Quick Start Example8
 *
 * Create a component and then monitor the health of that component 
 * in a separate test and threshold rule. Should the health of the 
 * monitored component drop under 50% then alert the user.
 * 
 */
#include "examples.h"
#include "myrefl_client.h"

void example8_foo_failed (int failed)
{
    if (failed) {
        myrefl_test_notify("Example8Test", NULL, MYREFL_RESULT_FAIL, 0);
    } else {
        myrefl_test_notify("Example8Test", NULL, MYREFL_RESULT_PASS, 0);
    }
}

void example8_rule_comp_health (void)
{
    myrefl_test_create_notification("Example8Test");

    myrefl_rule_create("Example8Rule",
                       "Example8Test",
                       MYREFL_ACTION_NOOP);

    myrefl_comp_create("Example8Component");

    myrefl_comp_contains_many("Example8Component",
                              "Example8Test",
                              "Example8Rule",
                              NULL);

    myrefl_test_chain_ready("Example8Test"); 

    myrefl_test_create_comp_health("Example8MonitorCompTest",
                                   "Example8Component");

    myrefl_action_create_user_alert(
        "Example8AlertUser",
        "The Health of the component is low, it may be operating at a "
        "reduced capacity. It is recommended to limit use of this system "
        "until the root cause can be diagnosed.");

    myrefl_rule_create("Example8MonitorCompRule",
                       "Example8MonitorCompTest",
                       "Example8AlertUser");

    /*
     * Trigger the user alert when the health of the monitored component
     * drops under 50%.
     */
    myrefl_rule_set_type("Example8MonitorCompRule",
                         MYREFL_RULE_LESS_THAN_N, 
                         50, 0);

    myrefl_test_chain_ready("Example8MonitorCompTest"); 
}
