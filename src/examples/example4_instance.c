/*
 * Software Diagnostics Quick Start Example4
 *
 * Multiple instances of a test, rule and action. As foo's are created
 * the client will call foo_created() and when they are deleted foo_deleted()
 * is called.
 * 
 */
#include "examples.h"
#include "swdiag_client.h"

typedef struct foo_s {
    char *name;
    int id;
} foo_t;

extern int check_foo(foo_t *foo);
extern int fix_foo(foo_t *foo);

static swdiag_result_t example_test (const char *instance, 
                                     void *context, long *value)
{
    foo_t *foo = (foo_t *)context;

    if (!instance) {
        /*
         * Don't test the base/non-instance.
         */
        return(SWDIAG_RESULT_IGNORE);
    }

    if (!foo) {
        /*
         * Can't test without the foo context.
         */
        return(SWDIAG_RESULT_ABORT);
    }

    if (check_foo(foo)) {
        return(SWDIAG_RESULT_PASS);
    } else {
        return(SWDIAG_RESULT_FAIL);
    }
}

static swdiag_result_t example_action (const char *instance, void *context)
{
    foo_t *foo = (foo_t *)context;

    if (!instance) {
        /*
         * Don't recover the base/non-instance.
         */
        return(SWDIAG_RESULT_IGNORE);
    }

    if (!foo) {
        /*
         * Can't perform action without the foo context.
         */
        return(SWDIAG_RESULT_ABORT);
    }

    if (fix_foo(foo)) {
        return(SWDIAG_RESULT_PASS);
    } else {
        return(SWDIAG_RESULT_FAIL);
    }
}

void foo_created (const foo_t *foo)
{
    /*
     * All tests, rules and actions have instances so that we can
     * carry an instance specific "foo" as the context to the tests
     * and actions and not have to lookup the foo by the instance
     * name.
     */
    swdiag_instance_create("Example4Test", foo->name, (void*)foo);
    swdiag_instance_create("Example4Rule", foo->name, NULL);
    swdiag_instance_create("Example4Action", foo->name, (void*)foo);
}

void foo_deleted (const foo_t *foo)
{
    swdiag_instance_delete("Example4Test", foo->name);
    swdiag_instance_delete("Example4Rule", foo->name);
    swdiag_instance_delete("Example4Action", foo->name);
}

void example4_instance_init (void)
{
    swdiag_test_create_polled("Example4Test",
                              example_test,
                              0,
                              SWDIAG_PERIOD_NORMAL);
    
    swdiag_action_create("Example4Action",
                         example_action,
                         0);

    swdiag_rule_create("Example4Rule",
                       "Example4Test",
                       "Example4Action");

    swdiag_test_chain_ready("Example4Test");

}

void example4_instance_deinit (void)
{
    swdiag_test_delete("Example4Test");
    swdiag_rule_delete("Example4Rule");
    swdiag_action_delete("Example4Action");
}
