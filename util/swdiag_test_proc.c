/* 
 * swdiag_test_proc.c 
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
 * Test process for ths swdiag_test to monitor and restart. Just
 * sit in the background with my pid in a known place so that it can
 * be monitored by SW Diags.
 */

#include <stdio.h> 
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <syslog.h>

static void signal_handler (int signal)
{
    switch(signal) {
    case SIGHUP:
        openlog("SWDIAG Test", (LOG_ODELAY | LOG_PID), LOG_LOCAL5);
        syslog(LOG_ERR, "Received SIGHUP, exiting daemon");
        closelog();
        exit(0);
        break;
    case SIGTERM:
        openlog("SWDIAG Test", (LOG_ODELAY | LOG_PID), LOG_LOCAL5);
        syslog(LOG_ERR, "Received SIGTERM, exiting daemon");
        closelog();
        exit(0);
        break;
    default:
        break;
    }
}

static void daemonise (void)
{
    int i, lfp;
    char str[10];

    if (getppid() == 1) {
        /*
         * Already a daemon
         */
        return;
    }

    i = fork();

    if (i < 0) {
        fprintf(stderr, "Could not fork daemon\n");
        exit(1);
    }

    if (i > 0) {
        /*
         * I'm the parent, only the child lives, so exit.
         */
        exit(0);
    }

    /*
     * I'm the child (daemon)
     */

    setsid();

    /*
     * Close all file descriptors
     */
    for (i = getdtablesize(); i >= 0; --i) {
        close(i);
    }

    /*
     * Redirect stderr and stdout to /dev/null
     */
    i = open("/dev/null", O_RDWR);
    dup(i);
    dup(i);

    umask(027);

    chdir("/tmp");
    
    lfp = open("/tmp/cisco_test_proc2.pid", (O_RDWR | O_CREAT | O_TRUNC), 0666);

    if (lfp < 0) {
        /*
         * Could not create lock file
         */
        exit(1);
    }

    if (lockf(lfp, F_TLOCK, 0) < 0) {
        /*
         * Could not get an exclusive lock on my lock file. Must already
         * be running.
         */
        exit(0);
    }

    /*
     * Put my pid in the lock file
     */
    snprintf(str, 10, "%d\n", getpid());
    write(lfp, str, strlen(str));

    signal(SIGCHLD, SIG_IGN);
    signal(SIGTSTP, SIG_IGN);
    signal(SIGTTOU, SIG_IGN);
    signal(SIGTTIN, SIG_IGN);
    signal(SIGHUP, signal_handler);
    signal(SIGTERM, signal_handler);

    openlog("HAD Test", (LOG_ODELAY | LOG_PID), LOG_LOCAL5);
    syslog(LOG_ERR, "Starting daemon");
    closelog();
}

int main(int argc, char *argv[])
{
    daemonise();

    while(1) {
        sleep(1);
    }

}
