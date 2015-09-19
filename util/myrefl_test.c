/* 
 * myrefl_test.c 
 *
 * Copyright (c) 2007-2009 Cisco Systems Inc.
 * Copyright (c) 2010-2015 Edward Groenendaal
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
 * Standalone test process that monitors for the running of test_proc using
 * software diags, and starts it if it dies.
 */

#include "myrefl_client.h"
#include "myrefl_unix_clients.h"
#include "myrefl_unix_rpc.h"

static char *my_instance_name = "SWDiag Test Package";

static const char *test_proc = "/tmp/cisco_test_proc";
static const char *test_proc_pid_file = "/tmp/cisco_test_proc2.pid";

/*
 * check_for_testproc()
 *
 * Check for the existence of the cisco test proc. This is not production code
 * and is not safe for production without beefing up security.
 */
static myrefl_result_t check_for_testproc (const char *instance,
                                           void *context, long *retval)
{
    myrefl_result_t result = MYREFL_RESULT_ABORT;
    FILE *pidfile, *pscmd;
    char pidbuf[10];
    char command[80];
    int rc;
    char *end,  *p;
    
    pidfile = fopen(test_proc_pid_file, "r");
    if (pidfile) {
        fread(pidbuf, sizeof(pidbuf), 1, pidfile);

        end = strchr(pidbuf, '\n');
        if (end) {
            *end = '\0';
        }

        /*
         * Confirm that pidbug only contains numeric.
         */
        for(p=pidbuf; *p && isdigit(*p); p++);
        
        if (!*p && !ferror(pidfile)) {
            snprintf(command, 80, "/usr/bin/ps -p %s", pidbuf);
            pscmd = popen(command, "r");
            if (pscmd) { 
                /*
                 * Gobble the output from ps to avoid a broken pipe.
                 */
                while (fgets(command, 80, pscmd) != NULL);

                if (pclose(pscmd) == 0) {
                    result = MYREFL_RESULT_PASS;
                } else {
                    result = MYREFL_RESULT_FAIL;
                }
            } 
        }
        fclose(pidfile);
    } else {
        result = MYREFL_RESULT_FAIL;
    }

    return(result);
}

static myrefl_result_t restart_testproc (const char *instance,
                                         void *context)
{
    system(test_proc);
    return(MYREFL_RESULT_PASS);
}

boolean myrefl_create_instance (myrefl_unix_info_t *myrefl_info)
{
    myrefl_info->rpc_instance = MYREFL_TEST_CLIENT_ID;
    myrefl_info->my_name = my_instance_name;

    myrefl_comp_create("Test Process Component");

    /*
     * Now create my tests
     */
    myrefl_test_create_polled("Check Process Test",
                                check_for_testproc,
                                NULL,
                                MYREFL_PERIOD_FAST);

    myrefl_action_create("Restart Process",
                           restart_testproc,
                           NULL);

    myrefl_rule_create("Check Process Rule",
                         "Check Process Test",
                         "Restart Process");

    myrefl_rule_set_severity("Check Process Rule",
                             MYREFL_SEVERITY_CRITICAL);

    myrefl_comp_contains_many("Test Process Component",
                              "Check Process Rule",
                              "Check Process Test",
                              "Restart Process", 
                              NULL);

    myrefl_test_chain_ready("Check Process Test");

    return(TRUE);
}
