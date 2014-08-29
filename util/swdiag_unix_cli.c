/* 
 * swdiag_unix_cli.c
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
 * Unix client CLI functions
 */
#include <stdlib.h>
#include "swdiag_unix_rpc.h"
#include "swdiag_cli.h"

/* 
 * Decode the state and print it as a string
 * TBD to be moved in comman repository
 */ 
const char *swdiag_unix_cli_state_str_compressed (cli_state_t state)
{
    switch (state) {
    case CLI_STATE_ALLOCATED:   return ("Alloc");
    case CLI_STATE_INITIALIZED: return ("Init");
    case CLI_STATE_CREATED:     return ("Create");
    case CLI_STATE_ENABLED:     return ("Enbld");
    case CLI_STATE_DISABLED:    return ("Dsbld");
    case CLI_STATE_DELETED:     return ("Delete");
    case CLI_STATE_INVALID:     return ("None");
    }    
    return ("None");
}  

/* 
 * swdiag_cli_severity_str_compressed()
 * Decode the severity and return it as a compressed 6chr string
 */ 
const char *swdiag_cli_severity_str_compressed (swdiag_severity_t severity)
{
    switch (severity) {
    case SWDIAG_SEVERITY_CATASTROPHIC: return "Ctphic";
    case SWDIAG_SEVERITY_CRITICAL:     return "Critcl";
    case SWDIAG_SEVERITY_HIGH:         return "High";
    case SWDIAG_SEVERITY_MEDIUM:       return "Medium";
    case SWDIAG_SEVERITY_LOW:          return "Low";
    case SWDIAG_SEVERITY_NONE:         return "None";
    case SWDIAG_SEVERITY_POSITIVE:     return "Positv";
    }    
    return("NA");
}

const char *swdiag_util_swdiag_result_str (swdiag_result_t result)
{
    switch (result) {
    case SWDIAG_RESULT_PASS:
        return ("Pass");
        break;
        
    case SWDIAG_RESULT_FAIL:
        return ("Fail");
        break;
        
    case SWDIAG_RESULT_VALUE:
        return ("Value");
        break;
        
    case SWDIAG_RESULT_IN_PROGRESS:
        return ("InProgr");
        break;
        
    case SWDIAG_RESULT_ABORT:
        return ("Abort");
        break;
    case SWDIAG_RESULT_INVALID:
        return ("Invalid");
        break;
    case SWDIAG_RESULT_IGNORE:
        return ("Ignore");
        break;
    case SWDIAG_RESULT_LAST:
        return ("Last");
        break;
    }
    
    return("Unknown");
}

/*
 * main()
 *
 * Parse the parameters and show whatever it is they are asking for from
 * the SW Diags master on the localhost.
 */
int main (int argc, char *argv[])
{
    CLIENT *clnt;
    int handle;
    swdiag_cli_info_rsp_t rsp;
    swdiag_cli_info_element_rpc_t *rpc_element;
    cli_type_t type = CLI_UNKNOWN;
    int c, errflg;
    char *hostname = "localhost";
    
    errflg = 0;
    while ((c = getopt(argc, argv, "tarcs:")) != -1) {
        switch (c) {
        case 't':
            if (type == CLI_UNKNOWN) {
                type = CLI_TEST;
            } else {
                errflg++;
            }
            break;
        case 'a':
            if (type == CLI_UNKNOWN) {
                type = CLI_ACTION;
            } else {
                errflg++;
            }
            break;
        case 'r':
            if (type == CLI_UNKNOWN) {
                type = CLI_RULE;
            } else {
                errflg++;
            }
            break;
        case 'c':
            if (type == CLI_UNKNOWN) {
                type = CLI_COMPONENT;
            } else {
                errflg++;
            }
            break;
        case 's':
            hostname = optarg;
            break;
        case ':':
            fprintf(stderr, "Option -%c requires a parameter\n", optopt);
            errflg++;
            break;
        case '?':
            fprintf(stderr, "Unrecognised option: -%c\n", optopt);
            errflg++;
        }
    }

    if (errflg || type == CLI_UNKNOWN) {
        fprintf(stderr, "Usage: %s [-t | -a | -r | -c ] [-s server]\n",
                argv[0]);
        exit(1);
    }

    clnt = clnt_create(hostname, 
                       SWDIAG_REMOTE_MASTER_PROTOCOL,
                       SWDIAG_MASTER_PROTOCOL_V1,
                       "tcp");
    if (clnt == NULL) {
        fprintf(stderr, "Could not establish contact with HA Diagnostics Master\n");
        exit(1);
    }

    if (swdiag_get_info_handle_1(type, &handle, clnt) != RPC_SUCCESS) {
        fprintf(stderr, "Could not obtain a CLI handle from HA Diagnostics Master\n");
        clnt_destroy(clnt);
        exit(1);
    }

    /*
     * And now use that handle to obtain then format the information until
     * no more information is available.
     */
    if (type == CLI_COMPONENT) {
        printf("Health   Conf   Fail Abort     Pass  State"
                            " Component Name\n"); 
    } else {
        printf("   Last\n");
        printf(" Result   Fail Abort     Pass  State Sevrty Name\n");
    }
    rsp.swdiag_cli_info_rsp_t_len = -1;
    rsp.swdiag_cli_info_rsp_t_val = NULL;
    while(rsp.swdiag_cli_info_rsp_t_len != 0) {
        if (swdiag_get_info_1(handle, &rsp, clnt) == RPC_SUCCESS) {
            int i;
            rpc_element = rsp.swdiag_cli_info_rsp_t_val;
            for (i=0; i < rsp.swdiag_cli_info_rsp_t_len; i++) {
                if (type == CLI_COMPONENT) {
                    printf("%4d.%d %4d.%d %6d %5d %8d %6s %s\n",
                           (rpc_element[i].health/10),
                           (rpc_element[i].health%10),
                           (rpc_element[i].confidence/10),
                           (rpc_element[i].confidence%10),
                           rpc_element[i].failures,
                           rpc_element[i].aborts,
                           rpc_element[i].passes,
                           swdiag_unix_cli_state_str_compressed(rpc_element[i].state),
                           
                        rpc_element[i].name);
                } else {
                    printf("%7s %6d %5d %8d %6s %-6s %s\n",
                           swdiag_util_swdiag_result_str(rpc_element[i].last_result),
                           rpc_element[i].failures,
                           rpc_element[i].aborts,
                           rpc_element[i].passes,
                           swdiag_unix_cli_state_str_compressed(rpc_element[i].state),
                           (type == CLI_RULE) ? swdiag_cli_severity_str_compressed(rpc_element[i].severity) : "  -",
                           rpc_element[i].name);
                }

            }
        } else { 
            fprintf(stderr, "Could not obtain CLI data from HA Diagnostics Master\n");
        }
    }
    clnt_destroy(clnt);
    return(0);
}

