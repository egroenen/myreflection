/*
 * swdiag_server.c - SW Diagnostics Server
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
 *
 * This is the Unix version of the swdiag-server, when running on Windows
 * we use a different launcher.
 */
#include <pthread.h>
#include <getopt.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>

#include "swdiag_client.h"
#include "swdiag_sched.h"
#include "swdiag_cli.h"
#include "swdiag_cli_local.h"
#include "swdiag_api.h"

#include "swdiag_json_parser.h"
#include "swdiag_webserver.h"
#include "swdiag_server_config.h"

int debug_flag = 0;
int terminal = 0;
int webserver = 0;

/**
 * The server
 */
int main (int argc, char **argv)
{
    pthread_t rpc_thread_id;
    int rc;
    char *modules_path = "/usr/share/swdiag/modules";
    char *config_path = "/etc/swdiag/swdiag.cfg";
    char *logging_path="/var/log/swdiag.log";
    char *http_path="/usr/share/swdiag/http";
    char *http_port="7654";
    int c;
    pid_t pid, sid;

    static struct option long_options[] = {
            {"debug", no_argument, &debug_flag, 1},
            {"modules", required_argument, 0, 'm'},
            {"config", required_argument, 0, 'c'},
            {"http",   required_argument, 0, 'w'},
            {"terminal", no_argument, &terminal, 1},
            {"webserver", no_argument, &webserver, 1},
            {0,0,0,0}
    };

    while (1) {
        int option_index = 0;

        c = getopt_long(argc, argv, "m:c:w:", long_options, &option_index);

        if (c == -1)
            break;

        switch(c) {
        case 0:
            if (long_options[option_index].flag != 0)
                break;
            break;
        case 'm':
            // TODO free these
            modules_path = strdup(optarg);
            break;
        case 'c':
            config_path = strdup(optarg);
            break;
        case 'w':
            http_path = strdup(optarg);
            break;
        default:
            fprintf(stderr, "Usage: %s [-m <module-path>] [-c <config-path>] [--debug] [--webserver]\n", argv[0]);
            exit(1);
        }
    }

    if (debug_flag) {
        swdiag_debug_enable();
    }

    if (terminal) {
        swdiag_xos_running_in_terminal();
    }

    config_parse(config_path);

    if (server_config.smtp_hostname[0] == '\0') {
        strncpy(server_config.smtp_hostname, "localhost", HOSTNAME_MAX-1);
    }

    if (server_config.modules_path[0] == '\0') {
        strncpy(server_config.modules_path, modules_path, FILEPATH_MAX-1);
    }

    if (server_config.http_root[0] == '\0') {
        strncpy(server_config.http_root, http_path, FILEPATH_MAX-1);
    }

    if (server_config.http_port[0] == '\0') {
        strncpy(server_config.http_port, http_port, HTTP_PORT_MAX-1);
    }

    modules_init(server_config.modules_path);

    if (!modules_process_config()) {
        // Failed to read the configuration.
        fprintf(stderr, "ERROR: Failed to read all of the module configuration, exiting.\n");
        exit(2);
    }

    // Now that the configuration has been read, lets daemonise, if not in terminal mode.

    if (!terminal) {
        /* Fork off the parent process */
        pid = fork();
        if (pid < 0) {
            exit(EXIT_FAILURE);
        }
        /* If we got a good PID, then
           we can exit the parent process. */
        if (pid > 0) {
            exit(EXIT_SUCCESS);
        }

        /* Change the file mode mask */
        umask(0);

        /* Open any logs here */

        /* Create a new SID for the child process */
        sid = setsid();
        if (sid < 0) {
            /* Log the failure */
            exit(EXIT_FAILURE);
        }

        /* Change the current working directory */
        if ((chdir("/")) < 0) {
            /* Log the failure */
            exit(EXIT_FAILURE);
        }

        /* Close out the standard file descriptors */
        close(STDIN_FILENO);
        close(STDOUT_FILENO);
        close(STDERR_FILENO);
    }

    // Starting server here...
    swdiag_sched_initialize();

    if (webserver && !swdiag_webserver_start()) {
        fprintf(stderr, "ERROR: Failed to start the webserver, exiting. Do you have another instance of the swdiag-server already running?\n");
        exit(2);
    }

    swdiag_set_master();

    //swdiag_api_comp_set_context(SWDIAG_SYSTEM_COMP, NULL);
    //swdiag_set_slave("slave");

    pthread_exit(NULL);
    return(0);
}




