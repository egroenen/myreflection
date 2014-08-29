/* 
 * swdiag_client.h - Software Diagnostics Client API
 *
 * Copyright (c) 2007-2009 Cisco Systems Inc.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use, copy,
 * modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * Public API for scheduling online diagnostic tests to detect
 * and recover from software faults. Also see the CLI API for
 * access to the status of Software Diagnsotics.
 */

/**
 * @file
 * Software Diagnostics Client API. To be used by Software Diagnostic clients
 * to register and configure their components, tests, rules and actions.
 */
   
#ifndef __SWDIAG_CLIENT_H__
#define __SWDIAG_CLIENT_H__

/*******************************************************************
 * Tests
 *******************************************************************/

/**
 * @defgroup test_apis API for Test Creation, Configuration, and Deletion
 *
 * @brief
 * API used for creating, configuring and deleting software
 * diagnostic tests. Create a test via swdiag_test_create_polled() or 
 * swdiag_test_create_notification().
 *
 * @note Once a test has been created it must be linked to a rule before
 * it will affect the health of the system, and that rule connected to a
 * recovery action in order for it to be an effective diagnostic.
 *
 * @note All tests must be enabled prior to use, see @ref object_states
 * 
 * @codeexample
 * Example client test callback function with bogus variable and function 
 * names using test and rule instances and no action function.
 *
 * @code
 * swdiag_result_t foo_test (const char *instance, 
 *                           void *context,
 *                           int *retval)
 * {
 *     hwidbtype *hwidb;
 *
 *     if (!instance || !context) {
 *         return(SWDIAG_RESULT_ABORT);
 *     }
 *
 *     hwidb = (hwidbtype*)context;
 *
 *     if (hwidb->bar) {
 *         *retval = hwidb->counter;
 *         return(SWDIAG_RESULT_VALUE);
 *     } else {
 *         return(SWDIAG_RESULT_ABORT);
 *     }
 * }
 * 
 * void foo_init (void)
 * { 
 *     swdiag_test_create_polled("Test", foo_test, NULL, SWDIAG_FREQ_NORMAL);
 *     swdiag_rule_create("Rule", "Test", SWDIAG_ACTION_NOOP);
 *     swdiag_rule_set_type("Rule", SWDIAG_RULE_GREATER_THAN_N, 80, 0);
 *     
 *     FOR_ALL_HWIDBS(hwidb) {
 *         swdiag_instance_create("Test", hwidb->name, hwidb);
 *         swdiag_instance_create("Rule", hwidb->name, hwidb);
 *     }
 *
 *     swdiag_test_chain_ready("Test");
 * }
 * @endcode
 */

/* @{ */

/**
 * @name Maximum object name length
 *
 * Maximum length of any object name or instance name used by swdiag.
 * Should a name be longer than this it will be truncated to fit. This
 * limit is imposed by some OS's limitations (31). Don't change it without
 * taking that into account.
 */
#define SWDIAG_MAX_NAME_LEN 31

/**
 * @name Maximum object description length
 *
 * Maximum length of object description used by swdiag.
 * Should the description be longer than this it will be truncated to fit. 
 */
#define SWDIAG_MAX_DESC_LEN 1024

/**
 * @name Internal Polling Periods
 *
 * Built in polling periods that should be used by clients polled tests.
 *
 * @note The schedular changes the frequencies based on system load, also
 * please use these defines since the values may be changed without notice 
 * in the future.
 *
 * You can also use any user defined period as well, but then it wont fit in
 * a queue with other tests.
 */
/* @{ */
/**
 * Use for tests that should run as quickly as possible, each test should 
 * run quickly so as to not block other tests from running.
 * Default is 1 minute.
 */
#define SWDIAG_PERIOD_FAST   (1000 * 60)
/**
 * Use for tests that should run every so often.
 * Default is 5 minutes.
 */
#define SWDIAG_PERIOD_NORMAL (1000 * 60 * 5)
/**
 * Use for tests that should not be run frequently, possibly because the 
 * test takes a while to complete.
 * Default is 30 mins.
 */
#define SWDIAG_PERIOD_SLOW   (1000 * 60 * 30)
/* @} */

/** 
 * Result of an test, rule or action
 *
 * @bug SWDIAG_RESULT_IN_PROGRESS not implemented
 */
typedef enum swdiag_result_e {
    SWDIAG_RESULT_INVALID = 0, /**< Invalid result, used to identify errors */
    SWDIAG_RESULT_PASS,        /**< test, rule or action passed */
    SWDIAG_RESULT_FAIL,        /**< test, rule or action failed */
    SWDIAG_RESULT_VALUE,       /**< test returned a value rather than pass/fail */
    SWDIAG_RESULT_IN_PROGRESS, /**< test or action is still in progress */
    SWDIAG_RESULT_ABORT,       /**< test or action was aborted prior or during */
    SWDIAG_RESULT_IGNORE,      /**< test or action result should be ignored, e.g. for a test applied to a base object */
    SWDIAG_RESULT_LAST         /**< not to be used. */
} swdiag_result_t;

/** Polled test callback
 *
 * Prototype for the client provided diagnostic test function
 *
 * @param[in] instance Instance name (if applicable) for this test, may 
 *                     be NULL if this test does not support instances
 * @param[in] context  Context as provided by the client when the test 
 *                     or instance was created.
 * @param[out] value   Optional integer that the client can assign a value
 *                     to to be interpreted by any associated rule
 *
 * @result Status of the test
 * @retval SWDIAG_RESULT_PASS   The test passed
 * @retval SWDIAG_RESULT_FAIL   The test failed
 * @retval SWDIAG_RESULT_VALUE  THe test is returning a value to be interpreted by a rule
 * @retval SWDIAG_RESULT_ABORT  The test could not be run
 * @retval SWDIAG_RESULT_IN_PROGRESS The test is still in progress, use swdiag_test_notify() when complete.
 * 
 * @note Where a test has instances the test function will be called for 
 *       each instance as well as the owning test. Therefore it is the 
 *       responsibility of the test function to ignore tests that have
 *       no valid instance (i.e. instance is NULL).
 */
typedef swdiag_result_t swdiag_test_t(const char *instance, 
                                      void *context, 
                                      long *value);

/** Create a Polled Test
 *
 * Create a polled test with the name test_name that will execute the function
 * test_func every period milli-seconds, passing it the provided context.
 *
 * Test results are passed to the associated rule, execution of the test_func
 * will not start until the test is ready and enabled when it has completed 
 * configuration.
 *
 * If the test function is going to take a long time it should run in
 * its own process or thread and return from the test_func immediately
 * with the return value of SWDIAG_RESULT_IN_PROGRESS. Once the test
 * is complete the swdiag_test_notify() API should be used to notify
 * the results of the test.
 *
 * If your test should not be run everywhere then use the 
 * swdiag_test_location() to override the default.
 *
 * @param test_name Test name for refering to this test by rules, dependencies
 *                  and the CLI.
 * @param test_func Pointer to the test function to be invoked
 * @param context   Opaque Context to be passed to test_func when invoked
 * @param period    Polling interval in milli-seconds (ms)
 *
 * @pre A test with test_name may already exist
 * @post A test with test_name will exist with these parameters set and swdiag_test_chain_ready() will have to be called
 *
 * @see swdiag_rule_create(), swdiag_test_chain_ready(), swdiag_test_notify()
 *
 */
void swdiag_test_create_polled(const char *test_name,
                               swdiag_test_t test_func,
                               void *context,
                               unsigned int period);

/** Create a Notification Test
 *
 * A test which is fully contained in the instrumented module, and
 * will inform the SW diags via the swdiag_test_notify() API when
 * it is triggered. Test results are passed to the associated rule
 * to be interpreted and acted upon.
 *
 * @param test_name Name of the Notification test.
 *
 * @pre test_name may already exist, if it does then swdiag_test_chain_ready() will have to be called
 *
 * @see swdiag_test_notify(), swdiag_rule_create()
 */
void swdiag_test_create_notification(const char *test_name);

/** Notification of the result of a test
 *
 * Notify swdiag of the status for a notification test instance, or 
 * an asynchronous polled test result.
 *
 * If this test has had instances created on it then the instance_name
 * parameter should be used to specify which instance this notification
 * pertains to. If there are no instances then NULL should be used to 
 * refer to the actual test.
 *
 * @param[in] test_name Test Name for the notification, if it doesn't 
 *                      exist then a new object is created as a forward
 *                      reference, and the result saved
 * @param[in] instance_name Optional instance name, may be NULL. If the 
 *                      instance doesn't exist then the result is discarded
 * @param[in] result    Result for this notification (e.g. pass or fail)
 * @param[in] value     Optional test value that may be used by a rule
 *
 * @pre test_name has been created and is enabled
 *
 * @see swdiag_test_create_notification(), swdiag_rule_create()
 */
void swdiag_test_notify(const char *test_name,
                        const char *instance_name,
                        swdiag_result_t result,
                        long value);

/** Test Flags
 * 
 * Flags that modify the behaviour of a test, including location flags
 * that dictate on what physical entities the test should be run.
 *
 * The client should always get the flags and OR any changes into the
 * received flags
 *
 * @codeexample
 *  Set the test location to be only on the Active RP for "Test"
 * @code
 *   flags = swdiag_test_get_flags("Test");
 *   swdiag_test_set_flags("Test", (flags & ~SWDIAG_TEST_LOCATION_ALL) | 
 *                                  SWDIAG_TEST_LOCATION_ACTIVE_RP);
 * @endcode
 *
 * @note Notifications for tests will be ignored unless the test
 *       is configured to receive them at that location
 *
 * @bug SWDIAG_TEST_LOCATION_STANDBY_RP not implemented
 * @bug SWDIAG_TEST_LOCATION_LC not implemented
 * 
 * @see swdiag_test_get_flags(), swdiag_test_set_flags()
 */
typedef enum swdiag_test_flags_e {
    SWDIAG_TEST_NONE                = 0x0000, /**< No flags defined */
    SWDIAG_TEST_LOCATION_ACTIVE_RP  = 0x0001, /**< Execute polled test on Active RP */
    SWDIAG_TEST_LOCATION_STANDBY_RP = 0x0002, /**< Execute polled test on Standby RP */
    SWDIAG_TEST_LOCATION_LC         = 0x0004, /**< Execute polled test on Line Card */
    SWDIAG_TEST_LOCATION_ALL        = 0x0007, /**< Execute polled test in all locations */
    SWDIAG_TEST_FLAG_ALL            = (SWDIAG_TEST_LOCATION_ALL),
} swdiag_test_flags_t;



/** Create a test that monitors a components health
 *
 * Create a combination polled and notification test that returns the
 * named components health, which can then be interpreted by a
 * rule. The test returns values via a notification whenever the
 * health of the component changes. The test will return the health
 * at the normal polling period, and also as a notification whenever
 * it changes.
 *
 * The test returns the health as an integer from 0 to 1000, where 
 * 1000 is 100.0%.
 *
 * @note The user could replicate this functionality using a polled 
 *       test that returned the health of the component. However the
 *       test created by swdiag_test_create_comp_health() has private
 *       access to the infra code that performs the health calculations
 *       and so will return changes in health as a notification immediately
 *       rather than having to wait for a polling period.
 *
 * @param[in] test_name Test name to create
 * @param[in] comp_name Component to monitor
 */
void swdiag_test_create_comp_health(const char *test_name,
                                    const char *comp_name);

/** Set a tests flags
 *
 * Set flags that modify the behaviour of this test from the defaults.
 *
 * @param[in] test_name Test name to modify the flags for
 * @param[in] flags     New flags for this test
 *
 * @pre Test with test_name may exist
 * @post Test with test_name will exist with these flags
 *
 * @codeexample
 *  Set the test location to be only on the Active RP for "Test"
 * @code
 *   flags = swdiag_test_get_flags("Test");
 *   swdiag_test_set_flags("Test", (flags & ~SWDIAG_TEST_LOCATION_ALL) | 
 *                                  SWDIAG_TEST_LOCATION_ACTIVE_RP);
 * @endcode
 * @see swdiag_test_get_flags()
 */
void swdiag_test_set_flags(const char *test_name,
                           swdiag_test_flags_t flags);

/** Get a tests flags
 *
 * Get the flags that modify the behaviour of this test from the defaults.
 *
 * @param[in] test_name Test name to get the flags for
 * @return The flags for this test, SWDIAG_TEST_NONE if the test_name 
 *         was not found.
 *
 * @pre Test with test_name exists
 *
 * @codeexample
 *  Set the test location to be only on the Active RP for "Test"
 * @code
 *   flags = swdiag_test_get_flags("Test");
 *   swdiag_test_set_flags("Test", (flags & ~SWDIAG_TEST_LOCATION_ALL) | 
 *                                  SWDIAG_TEST_LOCATION_ACTIVE_RP);
 * @endcode
 * @see swdiag_test_set_flags()
 */
swdiag_test_flags_t swdiag_test_get_flags(const char *test_name);

/** Delete a test and or instances
 *
 * Delete from the DB this test and any instances.
 *
 * @param[in] test_name Name of the test to delete
 *
 * @pre A test with test_name exists
 *
 * @see swdiag_instance_delete()
 */
void swdiag_test_delete(const char *test_name);

/** Test and all chained rules and actions are configured and ready
 *
 * All tests have states, these states are not visible from the API,
 * but are visible from the CLI. The three states of interest to a
 * client developer are, Created, Enabled and Disabled.
 *
 * All objects when they are first created are in the Created state, 
 * when in this state they are will be ignored by Software Diagnostics.
 *
 * After the objects have been configured they may be enabled or
 * disabled explicitly (via swdiag_test_enable(),
 * swdiag_rule_enable(), swdiag_action_enable(), swdiag_comp_enable())
 * or swdiag_test_chain_ready() may be used to set a test and all of
 * the rules and actions that are connected to this test to the
 * default state for the system.
 *
 * See the section @ref object_states in the user guide for more 
 * on states.
 *
 * @param[in] test_name Test that is ready
 *
 * @pre Test with test_name must exist, and should be configured.
 */ 
void swdiag_test_chain_ready(const char *test_name);

/** Explicitly Enable a test
 *
 * Change the state of this test to enabled. If it is a polled test then
 * it will start being called.
 *
 * @param[in] test_name Name of the test to be enabled
 * @param[in] instance_name Name of the instance to be enabled, NULL for all
 *
 * @pre Test with test_name must exist, and should be configured.
 *
 * @warning swdiag_test_chain_ready() should be used to signal that the test
 *          is ready to be used.
 *
 * @see swdiag_test_disable(), swdiag_test_chain_ready()
 */
void swdiag_test_enable(const char *test_name, const char *instance_name);

/** Explicitly Disable a test
 * 
 * Disable a test that is either enabled by default or has been explicitly
 * enabled.
 *
 * @param[in] test_name Name of the test to be disabled
 * @param[in] instance_name Name of the instance to be enabled, NULL for all
 *
 * @pre Test with test_name must exist, and should be configured.
 *
 * @see swdiag_test_enable(), swdiag_test_chain_ready()
 */
void swdiag_test_disable(const char *test_name, const char *instance_name);

/** Set the description for a test
 *
 * Set a description for a test that is displayed to the user should they request
 * more information on this test via the user interface.
 *
 * @param[in] test_name Name of the test
 * @param[in] description Description in plain text including new lines at appropriate intervals
 *
 * @pre Test with test_name may exist.
 */
void swdiag_test_set_description(const char *test_name,
                                 const char *description);

/** Set autopass for notification tests
 *
 * Notification tests sometimes only notify swdiags on failure, and not
 * when the failure has been corrected. For example memory allocation
 * failures. In these cases it is advised to use swdiag_test_set_autopass()
 * to signal that the failure is no longer of concern at a set delay after
 * a failure. 
 *
 * @note If the test fails again within the delay period then the delay 
 *       period is reset. Therefore a constant stream of failure notifications
 *       will result in the test staying in the failed state.
 *
 * @param[in] test_name Name of the test
 * @param[in] delay Delay in milli-seconds after which the test should pass
 *
 * @pre Test with test_name may exist
 */
void swdiag_test_set_autopass(const char *test_name,
                              unsigned int delay);
                          
/* @} */

/*******************************************************************
 * Actions
 *******************************************************************/
/**
 * @defgroup action_apis API for Action Creation, Configuration, and Deletion 
 *
 * @usage
 * API used to create, delete and configure diagnostic recovery actions to be
 * triggered when a diagnostic rule fails based on a test result.
 *
 * @see swdiag_rule_create(), swdiag_test_create_polled(), swdiag_test_create_notification()
 */

/* @{ */

/**
 * @name Built-in diagnostic infrastructure provided recovery actions
 */
/* @{ */
/**
 * Action to reload the system immediately
 *
 * @note The implementation of this build-in action is OS dependent. On 
 *       some OS's it may not be implemented.
 *
 * @codeexample
 * Using the SWDIAG_ACTION_RELOAD to trigger a system reload when processor
 * memory is exhausted for a 10 minutes.
 * @code
 * swdiag_rule_create("NoMemory", DIAG_MEMORY_TEST, SWDIAG_ACTION_NOOP);
 * swdiag_rule_set_type("NoMemory", SWDIAG_RULE_EQUAL_TO, 0, 0);
 * swdiag_rule_create("NoMemoryOverTime", "NoMemory",  SWDIAG_ACTION_RELOAD);
 * swdiag_rule_set_type("NoMemoryOverTime", SWDIAG_RULE_FAIL_FOR_TIME_N, (ONEMIN*10), 0);
 * @endcode
 */
#define SWDIAG_ACTION_RELOAD               "Built-in-reload"
/**
 * Action to switchover to a redundant processor immediately. On a non
 * HA system this action will display a message and return.
 *
 * @codeexample
 * Using the SWDIAG_ACTION_SWITCHOVER to trigger a system switchover 
 * when processor memory is exhausted on the Active for a 10 minutes and
 * the Standby is at 100% health.
 * @note The severity of the "StandbyHealthy" rule is set to 
 *       SWDIAG_SEVERITY_NONE since the Standby being at 100% health is not
 *       a fault.
 * @code
 * swdiag_rule_create("NoMemory", DIAG_MEMORY_TEST, SWDIAG_ACTION_NOOP);
 * swdiag_rule_set_type("NoMemory", SWDIAG_RULE_EQUAL_TO, 0, 0);
 * swdiag_rule_create("NoMemoryOverTime", "NoMemory",  SWDIAG_ACTION_NOOP);
 * swdiag_rule_set_type("NoMemoryOverTime", SWDIAG_RULE_FAIL_FOR_TIME_N, (ONEMIN*10), 0);
 * swdiag_test_create_comp_health("StandbyHealth", SWDIAG_STANDBY_COMP);
 * swdiag_rule_create("StandbyHealthy", "StandbyHealth", SWDIAG_ACTION_NOOP);
 * swdiag_rule_set_type("StandbyHealthy", SWDIAG_RULE_EQUAL_TO, 1000, 0);
 * swdiag_rule_set_severity("StandbyHealthy", SWDIAG_SEVERITY_NONE);
 * swdiag_rule_create("NoMemorySwitchover", "NoMemoryOverTime", SWDIAG_ACTION_SWITCHOVER);
 * swdiag_rule_add_input("NoMemorySwitchover", "StandbyHealthy");
 * swdiag_rule_set_type("NoMemorySwitchover", SWDIAG_RULE_AND, 0, 0);
 * @endcode
 */
#define SWDIAG_ACTION_SWITCHOVER           "Built-in-switchover"
/**
 * Action to reload the redundant processor immediately. On a non
 * HA system this action will display a message and return. 
 * 
 * @codeexample
 * Reload the Standby processor whenever it drops to under 50% health
 * for more than 10mins
 * @code
 * swdiag_test_create_comp_health("StandbyHealth", SWDIAG_STANDBY_COMP);
 * swdiag_rule_create("StandbyUnhealthy", "StandbyHealth", SWDIAG_ACTION_NOOP);
 * swdiag_rule_set_type("StandbyUnhealthy", SWDIAG_RULE_LESS_THAN_N, 500, 0);
 * swdiag_rule_create("StandbyUnhealthOverTime", "StandbyUnhealthy", SWDIAG_ACTION_SWITCHOVER);
 * swdiag_rule_set_type("StandbyUnhealthyOverTime", SWDIAG_RULE_FAIL_FOR_TIME_N, (ONEMIN*10), 0);
 * @endcode
 */

#define SWDIAG_ACTION_RELOAD_STANDBY        "Built-in-reload-standby"
/*
 * Action to reload the system during the next maintenance interval,
 * if configured. If no maintainence interval is configured then this
 * action will display a message and return.
 *
 * @codeexample
 * Reload the Standby processor at the next maintenance period whenever 
 * it drops to under 50% health for more than 10mins
 * @code
 * swdiag_test_create_comp_health("StandbyHealth", SWDIAG_STANDBY_COMP);
 * swdiag_rule_create("StandbyUnhealthy", "StandbyHealth", SWDIAG_ACTION_NOOP);
 * swdiag_rule_set_type("StandbyUnhealthy", SWDIAG_RULE_LESS_THAN_N, 500, 0);
 * swdiag_rule_create("StandbyUnhealthOverTime", "StandbyUnhealthy", SWDIAG_ACTION_SCHEDULED_SWITCHOVER);
 * swdiag_rule_set_type("StandbyUnhealthyOverTime", SWDIAG_RULE_FAIL_FOR_TIME_N, (ONEMIN*10), 0);
 * @endcode
 */
#define SWDIAG_ACTION_SCHEDULED_RELOAD     "Built-in-scheduled-reload"
/**
 * Action to switchover to a redundant processor during the next maintenance interval,
 * if configured. If no maintainence interval is configured then this
 * action will display a message and return.
 */
#define SWDIAG_ACTION_SCHEDULED_SWITCHOVER "Built-in-scheduled-switchover"
/**
 * Action that does nothing, should be used when chaining rules together and no actions are required during the intermediate stages.
 * 
 * @codeexample
 * Using the SWDIAG_ACTION_NOOP built in action to trigger only if a threshold is exceeded more than 4 out of the last 5 times,
 * @code
 * swdiag_test_create_polled("Test", test_func, NULL, SWDIAG_PERIOD_NORMAL);
 * swdiag_rule_create("Rule1", "Test", SWDIAG_ACTION_NOOP);
 * swdiag_rule_set_type("Rule1", SWDIAG_RULE_GREATER_THAN_N, 5, 0);
 * swdiag_rule_create("Rule2", "Rule1", "Action");
 * swdiag_rule_set_type("Rule2", SWDIAG_RULE_N_IN_M, 4, 5);
 * swdiag_action_create("Action", action_func, NULL);
 * @endcode
 */
#define SWDIAG_ACTION_NOOP                 "Built-in-No-op"
/* @} */

/** Recovery action callback
 *
 * Prototype for the client provided diagnostic recovery action function
 *
 * @param[in] instance Instance name (if applicable) for this action, may 
 *                     be NULL if this action does not support instances.
 * @param[in] context Context as provided by the client when the action  
 *                    or instance was created.
 * @result Status of the recovery action
 * @retval SWDIAG_RESULT_PASS   The recovery action passed
 * @retval SWDIAG_RESULT_FAIL   The recovery action failed
 * @retval SWDIAG_RESULT_ABORT  The recovery action could not be run
 * @retval SWDIAG_RESULT_IN_PROGRESS The recovery action is still in progress, use swdiag_action_complete() when complete.
 */
typedef swdiag_result_t swdiag_action_t(const char *instance, 
                                        void *context);

/** Create a Recovery Action
 *
 * Execute the function provided with the context when called. If the
 * action can not complete in a reasonable time then it may need to 
 * return SWDIAG_ACTION_IN_PROGRESS and call swdiag_action_complete()
 * when complete.
 *
 * @param[in] action_name Name of the action to create
 * @param[in] action_func Client recovery action function to call
 * @param[in] context     Client provided opaque context to provide recovery action when called
 */
void swdiag_action_create(const char *action_name,
                          swdiag_action_t action_func,
                          void *context);

/** Asynchronous Recovery Action Complete
 *
 * A recovery action which returned a value of
 * SWDIAG_RESULT_IN_PROGRESS when originally called uses
 * swdiag_action_complete() when it has completed the recovery action.
 *
 * @param[in] action_name Name of the action that has completed
 * @param[in] instance_name Optional instance name, NULL if not using instances
 * @param[in] result Result of the recovery action
 * @retval SWDIAG_RESULT_PASS   The recovery action passed
 * @retval SWDIAG_RESULT_FAIL   The recovery action failed
 * @retval SWDIAG_RESULT_ABORT  The recovery action could not be run
 */
void swdiag_action_complete(const char *action_name,
                            const char *instance_name,
                            swdiag_result_t result);

/** Create a user alert action
 * 
 * Notify the user via what ever methods are available on this 
 * system. E.g. EEM, logger, syslog
 *
 * @param[in] action_name Name of the action to create
 * @param[in] notification_string String to display to the user when this action is triggered
 */
void swdiag_action_create_user_alert(const char *action_name,
                                     const char *notification_string);

/** Delete a recovery action
 * 
 * @param[in] action_name Name of the action to delete
 * 
 * @pre An action with the name action_name exists
 */
void swdiag_action_delete(const char *action_name);

/** Set the description for a recovery action
 * 
 * Should the user wish to view further information about a recovery
 * action the contents of the description is displayed. By default
 * there is no description for actions.
 *
 * @param[in] action_name Name of the action
 * @param[in] description Description to attach to this action
 */
void swdiag_action_set_description(const char *action_name,
                                   const char *description);

/** Action Flags
 * 
 * Flags that modify the behaviour of an action, including location flags
 * that dictate on what physical entities the action should be run.
 *
 * The client should always get the flags and OR any changes into the
 * received flags
 *
 * @codeexample
 *  Set the test location to be only on the Active RP for "Action"
 * @code
 *   flags = swdiag_action_get_flags("Action");
 *   swdiag_action_set_flags("Action", (flags & ~SWDIAG_ACTION_LOCATION_ALL) | 
 *                                      SWDIAG_ACTION_LOCATION_ACTIVE_RP);
 * @endcode
 *
 * @bug SWDIAG_ACTION_LOCATION_STANDBY_RP not implemented
 * @bug SWDIAG_ACTION_LOCATION_LC not implemented
 
 * @see swdiag_action_get_flags(), swdiag_action_set_flags()
 */
typedef enum swdiag_action_flags_e {
    SWDIAG_ACTION_NONE                = 0x0000, /**< No flags defined */
    SWDIAG_ACTION_LOCATION_ACTIVE_RP  = 0x0001, /**< Execute action on Active RP */
    SWDIAG_ACTION_LOCATION_STANDBY_RP = 0x0002, /**< Execute action on Standby RP */
    SWDIAG_ACTION_LOCATION_LC         = 0x0004, /**< Execute action on Line Card */
    SWDIAG_ACTION_LOCATION_ALL        = 0x0007, /**< Execute action in all locations */
} swdiag_action_flags_t;

/** Set an actions flags
 *
 * Set flags that modify the behaviour of this action from the defaults.
 *
 * @param[in] action_name Action name to modify the flags for
 * @param[in] flags       New flags for this action
 *
 * @pre Action with action_name may exist
 * @post Action with action_name will exist with these flags
 *
 * @codeexample
 *  Set the action location to be only on the Active RP for "Action"
 * @code
 *   flags = swdiag_action_get_flags("Action");
 *   swdiag_action_set_flags("Action", (flags & ~SWDIAG_ACTION_LOCATION_ALL) | 
 *                                      SWDIAG_ACTION_LOCATION_ACTIVE_RP);
 * @endcode
 * @see swdiag_action_get_flags()
 */
void swdiag_action_set_flags(const char *action_name,
                             swdiag_action_flags_t flags);
/** Get an actions flags
 *
 * Get the flags that modify the behaviour of this action
 *
 * @param[in] action_name Action name to get the flags for
 * @return The flags for this action, SWDIAG_ACTION_NONE if the action_name 
 *         was not found.
 *
 * @pre Action with action_name exists
 *
 * @codeexample
 *  Set the action location to be only on the Active RP for "Action"
 * @code
 *   flags = swdiag_action_get_flags("Action");
 *   swdiag_action_set_flags("Action", (flags & ~SWDIAG_ACTION_LOCATION_ALL) | 
 *                                      SWDIAG_ACTION_LOCATION_ACTIVE_RP);
 * @endcode
 * @see swdiag_action_set_flags()
 */
swdiag_action_flags_t swdiag_action_get_flags(const char *action_name);

/** Explicitly Enable an action
 *
 * Enable an action that is either disabled by default or has been explicitly 
 * disabled.
 *
 * @param[in] action_name Name of the action to be enabled
 * @param[in] instance_name Name of the instance to be enabled, NULL for all
 *
 * @pre Action with action_name must exist, and should be configured.
 *
 * @note swdiag_test_chain_ready() should be used to signal that the test
 *       chain, including this action, is ready to be used.
 *
 * @see swdiag_action_disable(), swdiag_test_chain_ready()
 */
void swdiag_action_enable(const char *action_name, const char *instance_name);

/** Explicitly Disable an action
 * 
 * Disable an action that is either enabled by default or has been explicitly
 * enabled.
 *
 * @param[in] action_name Name of the action to be disabled
 * @param[in] instance_name Name of the instance to be enabled, NULL for all
 *
 * @pre Action with action_name must exist, and should be configured.
 *
 * @see swdiag_action_enable(), swdiag_test_chain_ready()
 */
void swdiag_action_disable(const char *action_name, const char *instance_name);

/* @} */

/*******************************************************************
 * Rules
 *******************************************************************/

/**
 * @defgroup rule_apis API for Rule Creation, and Deletion
 *
 * API used to create, delete and configure diagnostic recovery actions to be
 * triggered when a diagnostic rule fails based on a test result.
 *
 * @see swdiag_action_create(), swdiag_test_create_polled(), swdiag_test_create_notification()
 */ 

/* @{ */

/** Create a rule
 * Create a rule with the default behaviour of triggering the specified
 * Action when the specified Test fails (SWDIAG_RULE_ON_FAIL).
 *
 * @param[in] rule_name               New rule name for this rule
 * @param[in] input_test_or_rule_name Input trigger for the rule, may be a test or another rule
 * @param[in] action_name             Name of the recovery action to trigger should this rule fail
 *
 * @note Be aware of the sequence in which the test chain is made
 * ready and when the type of the rule is configured. If the test is
 * created and enabled, fails, and is then attached to a rule then
 * that rulke will immediately trigger causing the recovery action to
 * be called. This may not be the desired effect if for example the
 * type is to be changed to SWDIAG_RULE_N_EVER.
 */
void swdiag_rule_create(const char *rule_name,
                        const char *input_test_or_rule_name,
                        const char *action_name);

/** Type of rule
 */
typedef enum swdiag_rule_operator_e {
    SWDIAG_RULE_INVALID = 0,   /**< invalid value, not to be used */
    SWDIAG_RULE_ON_FAIL = 1,   /**< trigger action upon test fail (default) */
    SWDIAG_RULE_DISABLE,       /**< disable trigger action */
    SWDIAG_RULE_EQUAL_TO_N,    /**< trigger action when value == op1 */
    SWDIAG_RULE_NOT_EQUAL_TO_N,/**< trigger action when value != op1 */
    SWDIAG_RULE_LESS_THAN_N,   /**< trigger action when value < op1 */
    SWDIAG_RULE_GREATER_THAN_N,/**< trigger action when value > op1 */
    SWDIAG_RULE_N_EVER,        /**< After N occurrences */
    SWDIAG_RULE_N_IN_ROW,      /**< After N occurences in a row */
    SWDIAG_RULE_N_IN_M,        /**< N fails out of M runs */
    SWDIAG_RULE_RANGE_N_TO_M,  /**< Fail if within the specific range */
    SWDIAG_RULE_N_IN_TIME_M,   /**< N occurrences in M milliseconds */ 
    SWDIAG_RULE_FAIL_FOR_TIME_N, /**< Failed for M milliseconds */
    SWDIAG_RULE_OR,            /**< Any input is passing */
    SWDIAG_RULE_AND,           /**< All inputs are passing */
    SWDIAG_RULE_LAST,          /**< Last value - not to be used  */
} swdiag_rule_operator_t;

/** Change the type of the rule
 *
 * Change the rule type to one of swdiag_rule_operator_t, providing the
 * N and M operands where appropriate (as indicated by the name of the rule
 * type).
 *
 * e.g.@n
 *       SWDIAG_RULE_LESS_THAN_N requires that operand_n have a value@n
 *       SWDIAG_RULE_AND does not use either operand@n
 *       SWDIAG_RULE_RANGE_N_TO_M uses both N and M@n
 *
 * The default rule type when the rule is created is SWDIAG_RULE_ON_FAIL.
 *
 * @param[in] rule_name Name of the rule to change the type on
 * @param[in] operator  Rule type to change the rule to
 * @param[in] operand_n Optional operand for value N in the rule, 0 if not required
 * @param[in] operand_m Optional operand for value M in the rule, 0 if not required
 */
void swdiag_rule_set_type(const char *rule_name,
                          swdiag_rule_operator_t operator,
                          long operand_n,
                          long operand_m);

/** Add additional inputs for logic rules
 *
 * Some rule types work by applying logical operators to all the inputs
 * for a rule. This API is used to add the additional inputs to the rule.
 *
 * @param[in] rule_name Name of the rule to add the input to
 * @param[in] input_test_or_rule_name Name of the Rule or Test to add as an input
 */
void swdiag_rule_add_input(const char *rule_name,
                           const char *input_test_or_rule_name);

/** Rule Flags
 * 
 * Flags that modify the behaviour of a rule, including location flags
 * that dictate on what physical entities the rule should be run.
 *
 * The client should always get the flags and OR any changes into the
 * received flags
 *
 * @codeexample
 *  Set the rule location to be only on the Active RP for "Rule"
 * @code
 *   flags = swdiag_rule_get_flags("Rule");
 *   swdiag_rule_set_flags("Rule", (flags & ~SWDIAG_RULE_LOCATION_ALL) | 
 *                                  SWDIAG_ACTION_RULE_ACTIVE_RP);
 * @endcode
 *
 * @bug SWDIAG_RULE_LOCATION_STANDBY_RP not implemented
 * @bug SWDIAG_RULE_LOCATION_LC not implemented
 *
 * @see swdiag_rule_get_flags(), swdiag_rule_set_flags()
 */
typedef enum swdiag_rule_flags_e {
    SWDIAG_RULE_NONE                = 0x0000, /**< No flags defined */
    SWDIAG_RULE_LOCATION_ACTIVE_RP  = 0x0001, /**< Process rule on Active RP */
    SWDIAG_RULE_LOCATION_STANDBY_RP = 0x0002, /**< Process rule on Standby RP */
    SWDIAG_RULE_LOCATION_LC         = 0x0004, /**< Process rule on Line Card */
    SWDIAG_RULE_LOCATION_ALL        = 0x0007, /**< Process rule in all locations (default) */
    SWDIAG_RULE_TRIGGER_ROOT_CAUSE  = 0x0010, /**< Only trigger recovery action if rule is the root cause (default) */
    SWDIAG_RULE_TRIGGER_ALWAYS      = 0x0020, /**< Trigger the recovery action whenever this rule fails */
    SWDIAG_RULE_NO_RESULT_STATS     = 0x0040, /**< Don't keep stats on this rule since it doesn't indicate anything on its own */
} swdiag_rule_flags_t;

/** Set a rules flags
 *
 * Set flags that modify the behaviour of this rule from the defaults.
 *
 * @param[in] rule_name   Rule name to modify the flags for
 * @param[in] flags       New flags for this rule
 *
 * @pre Rule with rule_name may exist
 * @post Rule with rule_name will exist with these flags
 *
 * @codeexample
 *  Set the rule location to be only on the Active RP for "Rule"
 * @code
 *   flags = swdiag_rule_get_flags("Rule");
 *   swdiag_rule_set_flags("Rule", (flags & ~SWDIAG_RULE_LOCATION_ALL) | 
 *                                  SWDIAG_RULE_LOCATION_ACTIVE_RP);
 * @endcode
 * @see swdiag_rule_get_flags()
 */
void swdiag_rule_set_flags(const char *rule_name,
                           swdiag_rule_flags_t flags);

/** Get a rules flags
 *
 * Get the flags that modify the behaviour of this rule
 *
 * @param[in] rule_name Rule name to get the flags for
 * @return The flags for this rule, SWDIAG_RULE_NONE if the rule_name 
 *         was not found.
 *
 * @pre Rule with rule_name exists
 *
 * @codeexample
 *  Set the rule location to be only on the Active RP for "Rule"
 * @code
 *   flags = swdiag_rule_get_flags("Rule");
 *   swdiag_rule_set_flags("Rule", (flags & ~SWDIAG_RULE_LOCATION_ALL) | 
 *                                  SWDIAG_RULE_LOCATION_ACTIVE_RP);
 * @endcode
 * @see swdiag_rule_set_flags()
 */
swdiag_rule_flags_t swdiag_rule_get_flags(const char *rule_name);

/** Delete a rule
 * 
 * @param[in] rule_name Name of the rule to delete
 * 
 * @pre A rule with the name rule_name exists
 */
void swdiag_rule_delete(const char *rule_name);

/** Set the description for a rule
 * 
 * Should the user wish to view further information about a rule
 * the contents of the description is displayed. By default
 * there is no description for rules.
 *
 * @param[in] rule_name Name of the rule
 * @param[in] description Description to attach to this rule
 */
void swdiag_rule_set_description(const char *rule_name,
                                 const char *description);

/** Rule Severity
 *
 * The severity determines how much the health of the system
 * is effected whenever this rule fails. 
 *
 * @note There is also a positive severity that may be used for when a
 * rule is passing to offset the health impact from when other rules
 * are failing. This could be used by a platform to keep the health of
 * the system high whenever it is proven to be still performing its
 * core work regardless of failures within the system.
 */
typedef enum swdiag_severity_e {
    SWDIAG_SEVERITY_CATASTROPHIC = 1000, /**< 100% health impact */
    SWDIAG_SEVERITY_CRITICAL     =  500, /**<  50% health impact */
    SWDIAG_SEVERITY_HIGH         =  200, /**<  20% health impact */
    SWDIAG_SEVERITY_MEDIUM       =  100, /**<  10% health impact */
    SWDIAG_SEVERITY_LOW          =   50, /**<   5% health impact */
    SWDIAG_SEVERITY_NONE         =    0, /**<   No health impact */
    SWDIAG_SEVERITY_POSITIVE     = -200, /**< -20% health impact */
} swdiag_severity_t;

/** Set the impact of this rule
 *
 * Each rule has an impact on the health of the system, change the
 * impact of this rule.
 *
 * @param[in] rule_name Name of the rule
 * @param[in] severity    Severity of this rule
 */
void swdiag_rule_set_severity(const char *rule_name,
                              swdiag_severity_t severity);

/** Add subsequent actions to a rule
 * 
 * Sometimes it is desirable to have more than one action triggered
 * from a single rule. Using swdiag_rule_add_action() many actions
 * may be triggered in sequence
 *
 * @param[in] rule_name Name of the rule to add the action to
 * @param[in] action_name Name of the action to add
 *
 * @note There is no guarentee of which sequence the actions will be
 *       run. There is no way of removing an action from a rule at 
 *       this time.
 */
void swdiag_rule_add_action(const char *rule_name,
                            const char *action_name);

/** Explicitly Enable a Rule
 *
 * Enable a rule that is either disabled by default or has been explicitly 
 * disabled.
 *
 * @param[in] rule_name Name of the rule to be enabled
 * @param[in] instance_name Name of the instance to be enabled, NULL for all
 *
 * @pre Rule with rule_name must exist, and should be configured.
 *
 * @note swdiag_test_chain_ready() should be used to signal that the rule
 *       is ready to be used.
 *
 * @see swdiag_rule_disable(), swdiag_test_chain_ready()
 */
void swdiag_rule_enable(const char *rule_name, const char *instance_name);

/** Explicitly Disable a Rule
 * 
 * Disable a rule that is either enabled by default or has been explicitly
 * enabled.
 *
 * @param[in] rule_name Name of the rule to be disabled
 * @param[in] instance_name Name of the instance to be enabled, NULL for all
 *
 * @pre Rule with rule_name must exist, and should be configured.
 *
 * @see swdiag_rule_enable(), swdiag_test_chain_ready()
 */
void swdiag_rule_disable(const char *rule_name, const char *instance_name);

/* @} */
/*******************************************************************
 * Dependencies
 *******************************************************************/
/**
 * @defgroup depend_apis API for Root Cause Identification Dependency Creation and Deletion
 *
 * API to create and delete dependencies between rules (or
 * components) to be used by Root Cause Identification.
 */

/* @{ */

/** Create a dependency
 * 
 * Set up a dependency on child_name that should be run to determine
 * whether the parent_name is the root cause. 
 *
 * Both the parent or child may be components or rules. Where a
 * component is used then all the rules within that component will
 * have an implicit dependency.  
 *
 * If a dependency loop is detected then
 * the dependency will be ignored.
 *
 * @param[in] parent_rule_or_comp Name of the parent component or rule
 * @param[in] child_rule_or_comp  Name of the child component or rule
 */
void swdiag_depend_create(const char *parent_rule_or_comp,
                          const char *child_rule_or_comp);

/** Delete a dependency
 *
 * Delete an existing dependency between a parent and a child.
 *
 * @param[in] parent_rule_or_comp Name of the parent component or rule
 * @param[in] child_rule_or_comp  Name of the child component or rule
 *
 * @bug Not implemented (not even defined).
 * @todo Not implemented since it is quite hard, considering not 
 *       ever implementing instead and removing this declaration.
 */
void swdiag_depend_delete(const char *parent_rule_or_comp,
                          const char *child_rule_or_comp);

/* @} */
/*******************************************************************
 * Component
 *******************************************************************/

/**
 * @defgroup comp_apis API for Component Creation and Deletion
 *
 * API used to create and delete components as well as add and remove
 * objects from those components. A component is an object that contains
 * other objects. It is used to group objects so that they may be enabled
 * and disabled as a group, and also provides a way of collecting the 
 * results from all of the member objects and presenting them as a health
 * metric.
 *
 * Every software diagnostics client should create a component for their
 * functional area. All of their tests, rules and actions should be made
 * a member of that component. If they wish they may make sub-components
 * for further sub-grouping of functionality.
 *
 * Components may also be used when creating dependencies, so that one
 * rule in a client may create a dependency on an entire component, so 
 * that they need not know the detail of how that component is implemented.
 *
 * For example, a High Availability client that is using the
 * Redundancy Facility (RF) and Checkpointing Facility (CF) would
 * create a dependency on the software diagnostic components for RF
 * and CF, then should there be any failures within the HA client any
 * faults in RF or CF would be identified as the root cause.
 */

/* @{ */

/**
 * @name Predefined Component Names
 */
/* @{ */
/**
 * System component that contains all rules, tests, actions and components 
 * within this instance of swdiag.
 */
#define SWDIAG_SYSTEM_COMP "System"
/**
 * Component used to represent the Standby RP on the swdiag Master
 */
#define SWDIAG_STANDBY_COMP "StandbyRP"

/* @} */

/** Create a component
 *
 * Create a component for a functional area. This component will have a
 * health associated with it calculated from the results and severity of
 * all of its member objects.
 *
 * Objects should be added to the component using the swdiag_comp_contains()
 * and swdiag_comp_contains_many() APIs.
 *
 * @param[in] component_name Name of the component to create
 */
void swdiag_comp_create(const char *component_name);

/** Add object to component
 * 
 * Add an object of any type (test, action, rule, or component) to this
 * component.
 *
 * @note Neither the parent_component and child_object_name need to exist at
 *       the time of this API being called. If they don't exist then a forward
 *       reference is made, and when they are created the forward reference 
 *       object will be reused.
 *
 * @param[in] parent_component_name Name of the component to add the object to
 * @param[in] child_object_name Name of the object to add
 */
void swdiag_comp_contains(const char *parent_component_name,
                          const char *child_object_name);

/** Add objects to component
 * 
 * Add objects of any type (test, action, rule, or component) to this component
 *
 * @param[in] parent_component_name Name of the component to add the objects to
 * @param[in] child_object_name Name of the first object to add
 * @param[in] ... Name of the subsequent objects to add, ending with NULL
 *
 * @codeexample
 * Example of adding some objects to a component
 * @code
 * {
 *     swdiag_test_create_notification("Test1");
 *     swdiag_rule_create("Rule1", "Test1", "Action1");
 *     swdiag_action_create("Action1", action_func, NULL);
 *     swdiag_comp_create("Comp1");
 *
 *     swdiag_comp_contains_many("Comp1", "Test1", "Rule1", "Action1", NULL);
 * }
 * @endcode
 */
void swdiag_comp_contains_many(const char *parent_component_name,
                               const char *child_object_name,
                               ...);

/** Delete a component
 * 
 * Delete a component, which will also delete all the objects within this 
 * component. If you don't want to delete the objects within this component
 * then move them to a different component first using swdiag_comp_contains()
 *
 * @param[in] component_name Name of component to delete
 */
void swdiag_comp_delete(const char *component_name);

/** Explicitly Enable a Component
 *
 * Enable a component that is either disabled by default or has been
 * explicitly disabled. Will implicitly enable all member objects of
 * this component.
 *
 * @param[in] comp_name Name of the component to be enabled
 *
 * @see swdiag_comp_disable()
 */
void swdiag_comp_enable(const char *comp_name);

/** Explicitly Disable a component
 * 
 * Disable a component that is either enabled by default or has been explicitly
 * enabled. Will implicitly disable all objects contained within this 
 * component.
 *
 * @param[in] comp_name Name of the test to be disabled
 *
 * @see swdiag_comp_enable()
 */
void swdiag_comp_disable(const char *comp_name);

/** Set the description for a component
 * 
 * Should the user wish to view further information about a component
 * the contents of the description is displayed. By default
 * there is no description for component.
 *
 * @param[in] component_name Name of the component
 * @param[in] description Description to attach to this component
 */
void swdiag_comp_set_description(const char *component_name,
                                 const char *description);

/*@}*/
/*******************************************************************
 * Instance
 *******************************************************************/
/**
 * @defgroup instance_apis API for Instance Creation, and Deletion
 *
 * API used to create and delete test, rule and action instances.
 */

/* @{ */

/** Create an instance of a test, rule, or action
 *
 * An instance of an object inherits the majority of the attributes
 * of its owning object. Therefore it uses less memory to store, and should
 * be used in preference to creating full objects when there are many of the
 * same type of diagnostic being created, e.g. the same test for all interfaces
 *
 * Where an object with no instances is an input to an object with
 * instances all of the instances will receive that input. Where an
 * instance is the input to an object with no instances then that
 * object is triggered.
 *
 * @param[in] object Name of the test, rule or action that the instance is to be created for
 * @param[in] instance Name of the instance to create
 * @param[in] context Opaque context to pass to any test or action function when called for this instance
 *
 * @codeexample
 * Example showing how to use instances to improve performance when creating
 * lots of instances of the same test, rule and action.
 * @code
 * 
 * swdiag_result_t hwidb_check_consistency (const char *instance, 
 *                                          void *context,
 *                                          long *value)
 * {
 *     hwidbtype *hwidb;
 *
 *     if (!instance || !context) {
 *         return(SWDIAG_RESULT_ABORT);
 *     }
 *
 *     hwidb = (hwidbtype*)context;
 *
 *     if (check_consistency(hwidb)) {
 *         return(SWDIAG_RESULT_PASS);
 *     } else {
 *         return(SWDIAG_RESULT_FAIL);
 *     }
 * }
 * 
 * swdiag_result_t hwidb_reset (const char *instance, void *context)
 * {
 *     hwidbtype *hwidb;
 * 
 *     if (!instance || !context) {
 *         return(SWDIAG_RESULT_ABORT);
 *     }
 *     
 *     hwidb = (hwidbtype*)context;
 *   
 *     (*hwidb->reset)(hwidb);
 *
 *     return(SWDIAG_RESULT_PASS);
 * }
 * 
 * {
 *     hwidbtype *hwidb;
 *     
 *     swdiag_test_create_polled("HWIDBConsistencyCheck", 
 *                               hwidb_check_consistency,
 *                               NULL,
 *                               SWDIAG_PERIOD_SLOW);
 *     swdiag_action_create("HWIDBReset",
 *                           hwidb_reset,
 *                           NULL);
 *     swdiag_rule_create("HWIDBConsistency", 
 *                        "HWIDBConsistencyCheck",
 *                        "HWIDBReset");
 *     FOR_ALL_HWIDBS(hwidb) {
 *         swdiag_instance_create("HWIDBConsistencyCheck", hwidb->name, hwidb);
 *         swdiag_instance_create("HWIDBConsistency", hwidb->name, hwidb);
 *         swdiag_instance_create("HWIDBReset", hwidb->name, hwidb);
 *     }
 *     swdiag_test_chain_ready("HWIDBConsistencyCheck");
 * }
 * @endcode
 *
 * @codeexample
 * Example showing how to use instances to improve performance when creating
 * lots of instances of the same test, rule and triggering the same action to
 * reload the system. 
 * @code
 * {
 *     hwidbtype *hwidb;
 *     
 *     swdiag_test_create_polled("HWIDBConsistencyCheck", 
 *                               hwidb_check_consistency,
 *                               NULL,
 *                               SWDIAG_PERIOD_SLOW);
 *     swdiag_rule_create("HWIDBConsistency", 
 *                        "HWIDBConsistencyCheck",
 *                        SWDIAG_ACTION_RELOAD);
 *     FOR_ALL_HWIDBS(hwidb) {
 *         swdiag_instance_create("HWIDBConsistencyCheck", hwidb->name, hwidb);
 *         swdiag_instance_create("HWIDBConsistency", hwidb->name, hwidb);
 *     }
 *     swdiag_test_chain_ready("HWIDBConsistencyCheck");
 * }
 * @endcode
 */
void swdiag_instance_create(const char *object, 
                            const char *instance, 
                            void *context);

/** Delete an object instance
 *
 * @param[in] object Object name to delete the instance from
 * @param[in] instance Instance name to delete
 */
void swdiag_instance_delete(const char *object, 
                            const char *instance);
/*@}*/
/*******************************************************************
 * Utilities
 *******************************************************************/
/**
 * @defgroup misc_apis Miscellaneous API
 */

/*@{*/

/** Predefined health values.
 *
 */
/* @{ */
#define SWDIAG_HEALTH_FULL  1000
#define SWDIAG_HEALTH_EMPTY 0
/* @} */

/** Set the absolute health of a component
 * 
 * Set the absolute health on a component ignoring the health of
 * the rules and component objects within that component.
 *
 * @param[in] component_name Name of the component to set the health on
 * @param[in] health Value to set the health to
 */
void swdiag_health_set(const char *component_name,
                       unsigned int health);

/** Get the absolute health of a component
 * 
 * @param[in] component_name Name of the component
 * @returns The health of the component as an integer between 0 and 1000, i.e. to convert to a percentage divde by 10
 */
unsigned int swdiag_health_get(const char *component_name);

/** Get the context for an existing object
 *
 */
void *swdiag_get_context(const char *obj_name);

/*@}*/
#endif 
