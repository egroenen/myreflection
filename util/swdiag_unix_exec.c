/*
 * swdiag_unix_exec.c
 *
 * Copyright (c) 2012 Edward Groenendaal.
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
 * libswdiag provides the ability to execute tests as C functions. Should
 * we want to execute an external executable in Unix we need "something else".
 * This file contains a helper function that can be used by tests to execute these
 * external programs.
 *
 * If a polled test must set multiple values then you should register a single test
 * and then call void swdiag_test_notify() for each value that you extract from that.
 * The tricky thing is how to parse the output from an executed command, so that's
 * TBD at the moment. Obviously we can hard code some parsers in here for particular
 * tests (e.g. Java VM stats). Maybe we can have a library of C functions that do this
 * for particular applications? But that isn't very extensible. So instead I'm thinking
 * that it would be better to work on the IPC with swdiag, and then put in discrete
 * executables for swdiag_test_notify as a standalone Unix executable, and or python
 * bindings.
 *
 * e.g.
 *
 *    test_java_vm <--------- swdiag_unix_exec()  <--------- swdiag
 *          |
 *          v
 * swdiag_test_notify --------swdiag_test_notify() --------> swdiag
 *
 * The question then becomes what stops just anyone calling swdiag_test_notify
 * with garbage values? Obviously the CLI API is going to have to authenticate.
 */
#include "swdiag_client.h"

/**
 * The context contains the unix executable to run with the status
 * returned from the child being put in the value.
 */
swdiag_result_t swdiag_unix_exec_retval_status(const char *instance,
                                               void *context,
                                               long *value)
{
    *value = 0;
    return SWDIAG_RESULT_PASS;
}

/**
 * The context contains the unix executable to run with the output
 * parsed as a number and put in value.
 */
swdiag_result_t swdiag_unix_exec_retval_output(const char *instance,
                                               void *context,
                                               long *value)
{
    *value = 0;
    return SWDIAG_RESULT_PASS;
}

