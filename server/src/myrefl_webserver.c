/*
 * myrefl_json_parser.c - SW Diagnostics JSON web server
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

#include "mongoose/mongoose.h"
#include "myrefl_cli.h"
#include "myrefl_cli_local.h"

#include "myrefl_server_config.h"

/**
 * Maximum size of a HTTP response, default to 10kb. If need be we can extend this
 * within the server when we reach the maximum (rather than crashing :)
 */
#define MAX_HTTP_RESPONSE_SIZE (1024*10)

/*
 * Simple hello world callback. In reality we need to have an authorise callback
 * that will authorise the connection and setup a session. That session is then
 * returned in a cookie. That cookie is used for subsequent requests.
 *
 * We can use the CLI handle as the session, two birds with one stone.
 *
 * This is returning text for now, but as soon as the PrimUI is available will
 * return JSON so that the web interface can display the results in datatables
 * etc.
 */

static struct mg_context *ctx;

/**
 * Return in JSON a list of the components in a tree format
 */
static int get_components_json(cli_info_element_t *element, char *content, int content_length) {
	//content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length,
	//                                    "                         Health \n"
	//                                   "                Name   Now/Conf    Runs Passes  Fails\n");
	content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length, "[");
	while(element != NULL) {
		content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length,
				"{\"title\":\"%s\",\"health\":%5.1f,\"confidence\":%5.1f,\"runs\":%d,\"passes\":%d,\"failures\":%d}", element->name, element->health/10.0, element->confidence/10.0, element->stats.runs, element->stats.passes, element->stats.failures);
		element = element->next;
		if (element != NULL) {
			content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length, ",");
		}
	}
	content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length, "]");
	return content_length;
}

static void *https_request_callback(enum mg_event event,
        							struct mg_connection *conn) {

    void *processed = "yes";
    const struct mg_request_info *request_info = mg_get_request_info(conn);

    if (event == MG_NEW_REQUEST) {
        char *content;
        int content_length = 0;
        cli_info_element_t *element, *element_free, *element_instance, *element_instance_free;
        cli_type_t type;
        char name[MYREFL_MAX_NAME_LEN];

        memset(name, 0, sizeof(name));

        content = malloc(MAX_HTTP_RESPONSE_SIZE);

        if (!content) {
            return NULL;
        }

        if (strncmp(request_info->uri, "/comp/", 6) == 0) {
            type = CLI_COMPONENT;
            if (strlen(request_info->uri) > 0) {
            	// There is a component name
            	strncpy(name, request_info->uri+6, MYREFL_MAX_NAME_LEN);
            }
        } else if (strcmp(request_info->uri, "/tabcontent/2") == 0) {
            type = CLI_TEST;
        } else if (strcmp(request_info->uri, "/tabcontent/3") == 0) {
            type = CLI_RULE;
        } else if (strcmp(request_info->uri, "/tabcontent/4") == 0) {
            type = CLI_ACTION;
        } else {
            type = CLI_UNKNOWN;
        }

        if (type != CLI_UNKNOWN) {
            unsigned int handle = myrefl_cli_local_get_info_handle(name, type,
                    CLI_FILTER_NONE, NULL);
            // Now use the handle to get the actual information.

            if (handle != 0) {
                cli_info_t *info = myrefl_cli_local_get_info(handle, MAX_LOCAL);

                while (info != NULL) {
                    element = info->elements;
                    switch(element->type) {
                    case CLI_COMPONENT:
                    	content_length = get_components_json(element, content, content_length);
                        break;
                    case CLI_TEST:
                        while(element != NULL) {
                            content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length, "Test %s %s %d %d %d\n", element->name, myrefl_cli_state_to_str(element->state), element->stats.runs, element->stats.passes, element->stats.failures);
                            element = element->next;
                        }
                        break;
                    case CLI_RULE:
                        while(element != NULL) {
                            content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length, "Rule %s %d %d %d\n", element->name, element->stats.runs, element->stats.passes, element->stats.failures);
                            // Does this rule have any instances? if so get them and the info for each one.
                            unsigned int handle_instance = myrefl_cli_local_get_info_handle(element->name, CLI_RULE_INSTANCE,
                                                                                            CLI_FILTER_NONE, NULL);
                            if (handle_instance != 0) {
                                cli_info_t *info_instance = myrefl_cli_local_get_instance_info(handle_instance, MAX_LOCAL);
                                while (info_instance != NULL) {
                                    element_instance = info_instance->elements;
                                    while (element_instance != NULL) {
                                        content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length, "      <span style='%s'> %s %d %d %d</span>\n", (element_instance->last_result == MYREFL_RESULT_FAIL) ? "color:red" : "", element_instance->name, element_instance->stats.runs, element_instance->stats.passes, element_instance->stats.failures);
                                        element_instance = element_instance->next;
                                    }
                                    myrefl_cli_local_free_info(info_instance);
                                    info_instance = myrefl_cli_local_get_instance_info(handle_instance, MAX_LOCAL);
                                }
                            }
                            element = element->next;
                        }
                        break;
                    case CLI_ACTION:
                        while(element != NULL) {
                            content_length += snprintf(content + content_length, MAX_HTTP_RESPONSE_SIZE-content_length, "Action %s %d %d %d\n", element->name, element->stats.runs, element->stats.passes, element->stats.failures);
                            element = element->next;
                        }
                        break;
                    }
                    myrefl_cli_local_free_info(info);
                    info = myrefl_cli_local_get_info(handle, MAX_LOCAL);
                }
                content_length += 12; // For pre's below
                mg_printf(conn,
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/json\r\n"
                        "Content-Length: %d\r\n"        // Always set Content-Length
                        "\r\n"
                        "%s",
                        content_length, content);
            }
        } else {
            processed = NULL;
        }
        free(content);
    } else {
        // Not processed, let mongoose handle it (e.g. a static page).
        processed = NULL;;
    }

    return processed;
}


boolean myrefl_webserver_start() {
    /*
     * Start the embedded monsoon web server running non-SSL (for now) on 7654
     */
    static const char *options[] = {"listening_ports", server_config.http_port,
                                    "document_root", server_config.http_root,
                                    "num_threads", "5",
                                    "error_log_file", "/var/tmp/myrefl_web_error.log",
                                    "access_log_file", "/var/tmp/myrefl_web_access.log",
                                    NULL};

    ctx = mg_start(&https_request_callback, NULL, options);

    if (!ctx) {
        return FALSE;
    }
    return TRUE;
}

void myrefl_webserver_stop() {
	mg_stop(ctx);
}
