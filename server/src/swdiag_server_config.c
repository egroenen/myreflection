/*
 * swdiag_server_config.c - SW Diagnostics Server Configuration file parsing
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

#include <limits.h>
#include "swdiag_xos.h"
#include "swdiag_trace.h"
#include "jsmn/jsmn.h"
#include "swdiag_server_config.h"

static boolean config_parse_configuration(char *configuration);
static boolean parse_tuples(char *configuration, jsmntok_t *tokens);


#define MAXBUFLEN (1024 * 10)

swdiag_server_config server_config;

/**
 * The maximum number of JSON tokens that could possibly be in a request
 * is if there are just a number of name/value pairs like '"a":1,'. This
 * is 6 characters for two tokens.
 * In real life we will have more tokens than this, but that's OK, just
 * some (small amount) of wasted short term memory.
 */
static unsigned int calc_max_tokens(char *request) {
    int tokens = strlen(request)/5;
    // And one extra token to account for rounding.
    tokens*=2;
    return tokens;
}

static boolean is_valid_token(jsmntok_t *t) {
    return t != NULL && t->end > 0;
}

/**
 * strcmp on a json token, thanks to Alisdair McDiarmid
 */
static boolean json_token_streq(char *js, jsmntok_t *t, char *s)
{
    return (strncmp(js + t->start, s, t->end - t->start) == 0
            && strlen(s) == (size_t) (t->end - t->start));
}

/**
 * Grab the string for a JSON string token, thanks to Alisdair McDiarmid
 */
static char * json_token_to_str(char *js, jsmntok_t *t)
{
    js[t->end] = '\0';
    return js + t->start;
}

boolean config_parse(char *filename) {
    boolean ret = FALSE;
    if (filename) {
        FILE *fp = fopen(filename, "r");

        swdiag_debug(NULL, "Parsing Configuration '%s'", filename);

        if (fp != NULL) {
            // Have a file that we can read, parse it.
            char *configuration = calloc(MAXBUFLEN + 1, sizeof(char));
            if (!configuration) {
                swdiag_error("Error: memory allocation failure '%s'\n", filename);
                return FALSE;
            }
            size_t newLen = fread(configuration, sizeof(char), MAXBUFLEN, fp);
            if (newLen == 0) {
                swdiag_error("Error: empty configuration for file '%s'\n", filename);
            } else {
                configuration[++newLen] = '\0'; /* Just to be safe. */
            }
            if (fclose(fp) == 0) {
                // Parse and process the configuration.
                ret = config_parse_configuration(configuration);
            }
            free(configuration);
        } else {
            swdiag_error("Warning: Could not open the swdiag-server configuration file '%s'\n", filename);
            return ret;
        }
    }
    return ret;
}

static boolean config_parse_configuration(char *configuration) {
    jsmn_parser parser;
    jsmn_init(&parser);

    // We allocate one more token than we tell jsmn about since we
    // want an empty token on the end so we can detect the end of the
    // token stream (same principle as the end of string null).
    unsigned int n = calc_max_tokens(configuration);
    jsmntok_t *tokens = calloc(n+1, sizeof(jsmntok_t));

    int ret = jsmn_parse(&parser, configuration, tokens, n);

    if (ret == JSMN_ERROR_INVAL)
        swdiag_error("jsmn_parse: invalid JSON string");
    if (ret == JSMN_ERROR_PART)
        swdiag_error("jsmn_parse: truncated JSON string");
    if (ret == JSMN_ERROR_NOMEM)
        swdiag_error("jsmn_parse: not enough JSON tokens");

    // Process the tokens using our parser, filling in the
    // reply if required.
    if (ret == JSMN_SUCCESS) {
        boolean retval = parse_tuples(configuration, tokens);
        free(tokens);
        return retval;
    } else {
        return FALSE;
    }
}

/**
 * A configuration tuple consists of a primitive (String) followed by a primitive which
 * may be a string or a number.
 */
static boolean parse_tuples(char *configuration, jsmntok_t *tokens) {
    boolean ret = TRUE;
    jsmntok_t *token;
    jsmntok_t **token_ptr = malloc(sizeof(void*));

    if (!token_ptr) {
        return FALSE;
    }
    *token_ptr = &tokens[0];
    token = *token_ptr;

    while (ret && is_valid_token(token)) {
        if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
            if (json_token_streq(configuration, token, "default-email-to")) {
                (*token_ptr)++;
                token = *token_ptr;
                if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                    /* Email address */
                    char *email_address = json_token_to_str(configuration, token);
                    if (email_address) {
                        strncpy(server_config.alert_email_to, email_address, EMAIL_MAX-1);
                    }
                }
                (*token_ptr)++;
            } else if (json_token_streq(configuration, token, "default-email-from")) {
                (*token_ptr)++;
                token = *token_ptr;
                if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                    /* Email address */
                    char *email_address = json_token_to_str(configuration, token);
                    if (email_address) {
                        strncpy(server_config.alert_email_from, email_address, EMAIL_MAX-1);
                    }
                }
                (*token_ptr)++;
            } else if (json_token_streq(configuration, token, "modules-dir")) {
                (*token_ptr)++;
                token = *token_ptr;
                if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                    /* Email address */
                    char *path = json_token_to_str(configuration, token);
                    if (path) {
                        strncpy(server_config.modules_path, path, FILEPATH_MAX-1);
                    }
                }
                (*token_ptr)++;
            } else if (json_token_streq(configuration, token, "http-dir")) {
                (*token_ptr)++;
                token = *token_ptr;
                if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                    /* Email address */
                    char *path = json_token_to_str(configuration, token);
                    if (path) {
                        strncpy(server_config.http_root, path, FILEPATH_MAX-1);
                    }
                }
                (*token_ptr)++;
            } else if (json_token_streq(configuration, token, "http-port")) {
                (*token_ptr)++;
                token = *token_ptr;
                if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                    /* Email address */
                    char *path = json_token_to_str(configuration, token);
                    if (path) {
                        strncpy(server_config.http_port, path, HTTP_PORT_MAX-1);
                    }
                }
                (*token_ptr)++;
            } else if (json_token_streq(configuration, token, "smtp-hostname")) {
                (*token_ptr)++;
                token = *token_ptr;
                if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                    /* Email address */
                    char *smtp_hostname = json_token_to_str(configuration, token);
                    if (smtp_hostname) {
                        strncpy(server_config.smtp_hostname, smtp_hostname, HOSTNAME_MAX-1);
                    }
                }
                (*token_ptr)++;
            } else if (json_token_streq(configuration, token, "enabled-modules")) {
                // eg. enabled-modules: ['diag_postgres', 'diag_diskspace', 'diag_memory']
                (*token_ptr)++;
                token = *token_ptr;
                if (is_valid_token(token) && token->type == JSMN_ARRAY) {
                    /* List */
                    server_config.modules = (char**)calloc(token->size, sizeof(char*));
                    int i, modules=token->size;
                    (*token_ptr)++;
                    token = *token_ptr;
                    for (i=0; i<modules; i++) {
                        if (is_valid_token(token) && (token->type == JSMN_STRING || token->type == JSMN_PRIMITIVE)) {
                            char *modulename = json_token_to_str(configuration, token);
                            if (modulename) {
                                server_config.modules[i] = strdup(modulename);
                            } else {
                                swdiag_error("WARNING: Could not understand the configuration reading modules\n");
                                break;
                            }
                        }
                        (*token_ptr)++;
                        token = *token_ptr;
                    }
                    server_config.num_modules = modules;
                }
            } else {
                /*
                 * Unknown configuration paramater - go on to the next one.
                 */
                swdiag_error("WARNING: Could not understand the configuration. Token %s\n",
                        json_token_to_str(configuration, token));
                (*token_ptr)++;
            }
        } else {
            swdiag_error("WARNING: Could not understand the configuration directive type\n");
            (*token_ptr)++;
        }
        token = *token_ptr;
    }
    if (token_ptr) {
        free(token_ptr);
    }
    return ret;
}

