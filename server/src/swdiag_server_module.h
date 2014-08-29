/*
 * swdiag_server_module.h - SW Diagnostics Server Module interface
 *
 * Copyright (c) 2012 Edward Groenendaal
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

/**
 * The swdiag server reads its configuration and executes tests and
 * actions by calling module scripts in the module directory. Each module
 * script supports a number of functions:
 *
 * <modulename> conf
 *      Returns the configuration in JSON format
 * <modulename> test <testname> [<instancename>]
 *      Runs the test with the name <testname> against the optional instance.
 *      A single number may optionally be returned. The exit code of the
 *      test dictates whether the test was successful or not. 0 is success,
 *      >0 is failure.
 * <modulename> action <actionname> [<instancename>]
 *      Runs the action with the name <actionname> against the optional instance.
 *      The exit code of the action dictates whether the action was successful
 *      or not. 0 is success, >0 is failure.
 */

#ifndef SWDIAG_SERVER_MODULE_H_
#define SWDIAG_SERVER_MODULE_H_

extern void modules_init(char *modules_path);
extern boolean modules_process_config();
extern swdiag_result_t swdiag_server_exec_test(const char *instance, void *context, long *value);
extern swdiag_result_t swdiag_server_exec_action(const char *instance, void *context);
extern swdiag_result_t swdiag_server_email(const char *instance, void *context);

#define EMAIL_TO_MAX      (50)
#define EMAIL_SUBJECT_MAX (128)
#define EMAIL_COMMAND_MAX (128)

/**
 * Track which test to run using a test_context struct.
 */
typedef struct {
    char module_name[SWDIAG_MAX_NAME_LEN];
    char test_name[SWDIAG_MAX_NAME_LEN];
} test_context;

/**
 * Context struct used for sending an email from an action.
 */
typedef struct {
    char to[EMAIL_TO_MAX];
    char subject[EMAIL_SUBJECT_MAX];
    char command[EMAIL_COMMAND_MAX];
} email_context;

#endif /* SWDIAG_SERVER_MODULE_H_ */
