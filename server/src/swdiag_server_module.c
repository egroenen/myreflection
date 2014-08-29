/*
 * swdiag_server_exec.c - SW Diagnostics Server Exec functions
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
 * Functions that swdiag execs that in turn call the swdiag server
 * modules to exec a test or action.
 */

#include "swdiag_xos.h"
#include "swdiag_client.h"
#include "swdiag_trace.h"
#include "swdiag_server_module.h"
#include "swdiag_server_config.h"
#include "smtpfuncs.h"
#include <dirent.h>
#include <errno.h>

int moduleCount = 0;
static char **modules_;
static char *modules_path_;

// Maximum size for a modules configuration
#define MAXBUFLEN (1024 * 32)

boolean str_ends_with(const char *str, const char *suffix)
{
    if (!str || !suffix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lensuffix = strlen(suffix);
    if (lensuffix >  lenstr)
        return 0;
    return strncmp(str + lenstr - lensuffix, suffix, lensuffix) == 0;
}

boolean str_starts_with(const char *str, const char *prefix)
{
    if (!str || !prefix)
        return 0;
    size_t lenstr = strlen(str);
    size_t lenprefix = strlen(prefix);
    if (lenprefix >  lenstr)
        return 0;
    return strncmp(str, prefix, lenprefix) == 0;
}

boolean is_configured_module(const char *filename) {
    int i;

    for(i = 0; i < server_config.num_modules; i++) {
        if (strcmp(filename, server_config.modules[i]) == 0) {
            return TRUE;
        }
    }
    return FALSE;
}

boolean is_valid_module(const char *filename) {
    return is_configured_module(filename) &&
            *filename != '.' &&
            !str_ends_with(filename, "~") &&
            !str_ends_with(filename, ".conf") &&
            !str_ends_with(filename, "_conf.py") &&
            !str_ends_with(filename, "_conf.pyc") &&
            !str_ends_with(filename, "_conf.pyo") &&
            !str_ends_with(filename, ".pyc");
}

void modules_init(char *modules_path) {
    DIR *d;
    struct dirent *dir;
    modules_path_ = modules_path;

    d = opendir(modules_path_);
    if (d) {
        while ((dir = readdir(d)) != NULL) {
            char *filename = dir->d_name;
            if (is_valid_module(filename)) {
                moduleCount++;
            }
        }
        closedir(d);
    }

    if (moduleCount > 0) {
        modules_ = (char**) calloc(moduleCount, sizeof(void*));
        moduleCount = 0;
        d = opendir(modules_path_);
        if (d) {
            while ((dir = readdir(d)) != NULL) {
                char *filename = dir->d_name;
                if (is_valid_module(filename)) {
                    modules_[moduleCount++] = strdup(filename);
                    swdiag_debug(NULL, "Added MODULE '%s'", modules_[moduleCount-1]);
                }
            }
            closedir(d);
        }
    }
}

boolean modules_process_config() {
    boolean ret = FALSE;
    int i;
    if (modules_ != NULL)  {
        char *configuration = calloc(MAXBUFLEN+1, sizeof(char));
        char *filename = malloc(MAXBUFLEN+1);

        if (!filename || !configuration || strlen(modules_path_) > (MAXBUFLEN/2)) {
            fprintf(stderr, "Error: memory allocation failure\n");
            return FALSE;
        }
        for (i = 0; i < moduleCount; i++) {
            swdiag_debug(NULL, "Processing configuration for MODULE '%s'", modules_[i]);
            if (strlen(modules_[i]) > (MAXBUFLEN/2)) {
                fprintf(stderr, "Error: filename too long\n");
                return FALSE;
            }
            strcpy(filename, modules_path_);
            strcat(filename, "/");
            strcat(filename, modules_[i]);
            strcat(filename, " --conf");
            swdiag_debug(NULL, "MODULE path: %s", filename);
            FILE *fp = popen(filename, "r");
            if (fp != NULL) {
                size_t newLen = fread(configuration, sizeof(char), MAXBUFLEN, fp);
                swdiag_trace(NULL, "Reading module configuration '%s', %d bytes read", filename, newLen);
                if (newLen == 0) {
                    fprintf(stderr, "Error: empty configuration for module file '%s'\n", filename);
                    break;
                } else {
                    configuration[newLen] = '\0'; /* Just to be safe. */
                }
                if (pclose(fp) == 0) {
                    // Parse and process the configuration.
                    ret = process_json_request(modules_[i], configuration, NULL);
                }
            }
        }
        free(filename);
        free(configuration);
    }
    return ret;
}
/**
 * The context contains a string with the name of the module in it, the instance may or may not
 * contain the instance name, and the value is there in case any value that can be converted into
 * a number is returned from the test.
 */
swdiag_result_t swdiag_server_exec_test(const char *instance, void *context, long *value) {
    swdiag_result_t result = SWDIAG_RESULT_ABORT;
    test_context *testcontext = (test_context*)context;
    int rc;;

    swdiag_debug(NULL, "Module %s: Test %s instance %s is being run", testcontext->module_name, testcontext->test_name, instance);
    char *test_results = calloc(MAXBUFLEN+1, sizeof(char));
    char *filename = malloc(strlen(modules_path_) + MAXBUFLEN);

    if (!filename || !test_results) {
        fprintf(stderr, "Error: memory allocation failure\n");
        return FALSE;
    }

    strcpy(filename, modules_path_);
    strcat(filename, "/");
    strcat(filename, testcontext->module_name);
    strcat(filename, " --test ");
    strcat(filename, testcontext->test_name);
    if (instance != NULL) {
        strcat(filename, " --instance ");
        strcat(filename, instance);
    }
    swdiag_debug(NULL, "MODULE path: %s", filename);
    FILE *fp = popen(filename, "r");
    if (fp != NULL) {
        size_t newLen = fread(test_results, sizeof(char), MAXBUFLEN, fp);
        if (newLen != 0) {
            test_results[newLen] = '\0'; /* Just to be safe. */
        }
        pclose(fp);
        if (newLen != 0) {
            if (process_json_request(testcontext->module_name, test_results, NULL)) {
                result = SWDIAG_RESULT_IN_PROGRESS;
            } else {
                result = SWDIAG_RESULT_ABORT;
            }
        } else {
            result = SWDIAG_RESULT_ABORT;
        }
    }

    free(test_results);
    free(filename);


    return result;
}

/**
 * The context contains a string with the name of the module in it, the instance may or may not
 * contain the instance name, and the value is there in case any value that can be converted into
 * a number is returned from the test.
 */
swdiag_result_t swdiag_server_exec_action(const char *instance, void *context) {
    swdiag_result_t result = SWDIAG_RESULT_ABORT;
    test_context *testcontext = (test_context*)context;
    int rc;;

    swdiag_debug(NULL, "Module %s: Test %s instance %s is being run", testcontext->module_name, testcontext->test_name, instance);
    char *test_results = calloc(MAXBUFLEN+1, sizeof(char));
    char *filename = malloc(strlen(modules_path_) + MAXBUFLEN);

    if (!filename || !test_results) {
        fprintf(stderr, "Error: memory allocation failure\n");
        return FALSE;
    }

    strcpy(filename, modules_path_);
    strcat(filename, "/");
    strcat(filename, testcontext->module_name);
    strcat(filename, " action ");
    strcat(filename, testcontext->test_name);
    if (instance != NULL) {
        strcat(filename, " ");
        strcat(filename, instance);
    }
    swdiag_debug(NULL, "MODULE path: %s", filename);
    FILE *fp = popen(filename, "r");
    if (fp != NULL) {
        size_t newLen = fread(test_results, sizeof(char), MAXBUFLEN, fp);
        if (newLen != 0) {
            test_results[newLen] = '\0'; /* Just to be safe. */
        }
        pclose(fp);
        if (newLen != 0) {
            process_json_request(testcontext->module_name, test_results, NULL);
            result = SWDIAG_RESULT_IN_PROGRESS;
        } else {
            result = SWDIAG_RESULT_ABORT;
        }
    }

    free(test_results);
    free(filename);

    return result;
}

/**
 * Alert the user using the swdiag server preferred mechanism with the message in context. We
 * are using this in preference to the swdiag_action_create_user_alert() for now since it allows
 * better control.
 */
swdiag_result_t swdiag_server_email(const char *instance, void *context) {
    swdiag_result_t result = SWDIAG_RESULT_PASS;
    email_context *email = (email_context*)context;

    if (!instance) {
        instance = "";
    }

    if (email) {
        char *to = email->to;
        char body[MAXBUFLEN];

        if (*to == '\0') {
            to = server_config.alert_email_to;
        }

        if (email->command[0] != '\0') {
            FILE *fp = popen(email->command, "r");
            if (fp != NULL) {
                size_t newLen = fread(body, sizeof(char), MAXBUFLEN, fp);
                if (newLen != 0) {
                    body[newLen] = '\0';
                }
                pclose(fp);
            }
        } else {
            strncpy(body, email->subject, MAXBUFLEN-1);
        }
        /* I'm hardcoding the hostname to swdiag-server - it's not AFAIK important anyway. */
        send_mail(server_config.smtp_hostname, "swdiag-server", server_config.alert_email_from, to, email->subject, server_config.alert_email_from, body);
    }

    return result;
}
