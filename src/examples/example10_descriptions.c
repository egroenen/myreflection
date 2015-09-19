/*
 * Software Diagnostics Quick Start Example10
 *
 * Adding descriptions to improve usability.
 * 
 */

#include "examples.h"
#include "myrefl_client.h"

extern int fix_foo(void);

void example10_foo_failed (int failed)
{
    if (failed) {
        myrefl_test_notify("Example10Test", NULL, MYREFL_RESULT_FAIL, 0);
    } else {
        myrefl_test_notify("Example10Test", NULL, MYREFL_RESULT_PASS, 0);
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

void example10_descriptions (void)
{
    myrefl_test_create_notification("Example10Test");

    /*
     * Note that descriptions are word wrapped automatically upon 
     * display, and that newlines are honored.
     */
    myrefl_test_set_description(
        "Example10Test",
        "A Notification from the Foo subsystem that Foo is no longer"
        "functional. Issue the following command in order to"
        "diagnose whether Foo is functional:\n"
        "\n"
        "show foo status\n");

    myrefl_action_create("Example10Action",
                         example_action,
                         NULL);

    myrefl_action_set_description(
        "Example10Action",
        "Reinitialise the Foo database and restart the Foo processes"
        "Use the following command in order to check the status of the"
        "Foo subsystem:\n"
        "\n"
        "show foo status\n");

    myrefl_rule_create("Example10Rule",
                       "Example10Test",
                       "Example10Action");

    myrefl_rule_set_description(
        "Example10Rule",        
        "A Notification from the Foo subsystem that Foo is no longer"
        "functional. Issue the following command in order to"
        "diagnose whether Foo is functional:\n"
        "\n"
        "show foo status\n"
        "\n"
        "The Foo database and processes should have been automatically"
        "recovered.");

    myrefl_comp_create("Example10Component");

    myrefl_comp_set_description(
        "Example10Component",
        "Set of diagnostic tests, rules and actions for monitoring the"
        "Foo subsystem that provides Foo services to Bar interfaces. The"
        "health of the component should always be at 100%, and drop in"
        "health is an important event that should be investigated.");

    myrefl_comp_contains_many("Example10Component",
                              "Example10Test",
                              "Example10Rule",
                              NULL);

    myrefl_test_chain_ready("Example10Test"); 
}

