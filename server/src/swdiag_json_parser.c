/*
 * swdiag_json_parser.c - SW Diagnostics JSON parser
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
 * Thanks to Alisdair McDiarmid for some snippets of code for working
 * with jsmn.
 */

/*
 * The network interface with the swdiag server is implemented using
 * a JSON interface. We are using the jsmn library to tokenise and
 * partially parse the JSON. Then we use the parser in this file to
 * parse the remaining tokens and call the appropriate swdiag APIs
 * in response.
 *
 * Where appropriate a response is returned also in JSON.
 */

#include <limits.h>
#include "swdiag_client.h"
#include "swdiag_trace.h"
#include "jsmn/jsmn.h"
#include "swdiag_server_module.h"


// Forward declarations for the parser
static boolean parse_request(char *module, char *request, jsmntok_t *tokens, char *reply);

// Configuration keywords
static boolean parse_configuration(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_command(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_test(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_comp(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_rule(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_action(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_instance(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_test_ready(char *module, char *request, jsmntok_t **token_ptr, char *reply);
static boolean parse_email(char *module, char *request, jsmntok_t **token_ptr, char *reply);

// Test result keywords
static boolean parse_result(char *module, char *request, jsmntok_t **token_ptr, char *reply);

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

/**
 * The end of the token stream is identified by a token filled with
 * 0's.
 */
static boolean is_valid_token(jsmntok_t *t) {
    return t != NULL && t->end > 0;
}
/**
 * Process a new JSON request.
 */
boolean process_json_request(char *module, char *request, char *reply) {
    boolean retval = FALSE;
    jsmn_parser parser;
    jsmn_init(&parser);

    // We allocate one more token than we tell jsmn about since we
    // want an empty token on the end so we can detect the end of the
    // token stream (same principle as the end of string null).
    unsigned int n = calc_max_tokens(request);
    jsmntok_t *tokens = calloc(n+1, sizeof(jsmntok_t));

    if (!tokens) {
        swdiag_error("Memory allocation error");
        return FALSE;
    }
    int ret = jsmn_parse(&parser, request, tokens, n);

    if (ret == JSMN_ERROR_INVAL)
        swdiag_error("jsmn_parse: invalid JSON string '%s'", request);
    if (ret == JSMN_ERROR_PART)
        swdiag_error("jsmn_parse: truncated JSON string '%s'", request);
    if (ret == JSMN_ERROR_NOMEM)
        swdiag_error("jsmn_parse: not enough JSON tokens");

    // Process the tokens using our parser, filling in the
    // reply if required.
    if (ret == JSMN_SUCCESS)
        retval =  parse_request(module, request, tokens, reply);

    free(tokens);
    return retval;
}

/**
 * Parse the request entire request performing any required
 * actions and responses inline with the parsing. I.e. there
 * is no verification prior to performing the actions, which
 * could lead to incomplete processing of a request.
 *
 * A request is formed from a series of commands. These commands
 * may be configuration commands, to create a new test or rule,
 * or maybe a request for the state of the system.
 *
 * The tokens are in an array, to keep our position in the array we
 * are using a pointer to a pointer and incrementing that.
 */
static boolean parse_request(char *module, char *request, jsmntok_t *tokens, char *reply) {
    jsmntok_t **token_ptr = malloc(sizeof(void*));
    *token_ptr = &tokens[0];
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        // Gobble the encapsulating object if present.
        (*token_ptr)++;
        token = *token_ptr;
    }
    boolean ret = parse_configuration(module, request, token_ptr, reply);

    free(token_ptr);
    return ret;
}

static boolean parse_configuration(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = TRUE;
    jsmntok_t *token = *token_ptr;

    while (ret && is_valid_token(token)) {
        ret &= parse_command(module, request, token_ptr, reply);
        token = *token_ptr;
    }
    return ret;
}

static boolean parse_command(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = TRUE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && (token->type == JSMN_ARRAY || token->type == JSMN_OBJECT)) {
        // Gobble the object or list and move on.
        (*token_ptr)++;
    } else if (is_valid_token(token) && token->type == JSMN_STRING) {
        if (json_token_streq(request, token, "test")) {
            (*token_ptr)++;
            ret &= parse_test(module, request, token_ptr, reply);
        } else if (json_token_streq(request, token, "comp")) {
            (*token_ptr)++;
            ret &= parse_comp(module, request, token_ptr, reply);
        } else if (json_token_streq(request, token, "rule")) {
            (*token_ptr)++;
            ret &= parse_rule(module, request, token_ptr, reply);
        } else if (json_token_streq(request, token, "action")) {
            (*token_ptr)++;
            ret &= parse_action(module, request, token_ptr, reply);
        } else if (json_token_streq(request, token, "instance")) {
            (*token_ptr)++;
            ret &= parse_instance(module, request, token_ptr, reply);
        } else if (json_token_streq(request, token, "ready")) {
            (*token_ptr)++;
            ret &= parse_test_ready(module, request, token_ptr, reply);
        } else if (json_token_streq(request, token, "email")) {
            (*token_ptr)++;
            ret &= parse_email(module, request, token_ptr, reply);
        } else if (json_token_streq(request, token, "result")) {
            (*token_ptr)++;
            ret &= parse_result(module, request, token_ptr, reply);
        } else {
            swdiag_error("Module '%s': Configuration contains invalid command '%s'", module, json_token_to_str(request, token));
            (*token_ptr)++;
            ret = FALSE;
        }
    } else {
        swdiag_error("Module '%s': Configuration contains invalid command type '%s'", module, json_token_to_str(request, token));
        (*token_ptr)++;
        ret = FALSE;
    }
    return ret;
}

static boolean parse_test(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = FALSE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        int attributes = token->size/2;
        char *test_name = NULL;
        long int interval = 0;
        boolean polled = FALSE;
        char *health_comp = NULL;
        char *comp_name = NULL;
        char *description = NULL;
        int i;

        (*token_ptr)++; // Consume Object token

        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
                // Which attribute do we have here?
                if (json_token_streq(request, token, "name")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_STRING) {
                        test_name = json_token_to_str(request, token);
                        (*token_ptr)++;
                    } else {
                        swdiag_error("Module '%s': Configuration contains invalid test name type", module);
                        ret = FALSE;
                        break;
                    }
                } else if (json_token_streq(request, token, "polled")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                        if (json_token_streq(request, token, "true")) {
                            polled = TRUE;
                        } else if (json_token_streq(request, token, "false")) {
                            polled = FALSE;
                        } else {
                            swdiag_error("Module '%s': Configuration contains invalid test type", module);
                            ret = FALSE;
                            break;
                        }
                        (*token_ptr)++;
                    } else {
                        swdiag_error("Module '%s': Configuration contains invalid test type !primitive", module);
                        ret = FALSE;
                        break;
                    }
                } else if (json_token_streq(request, token, "interval")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                        interval = strtol(json_token_to_str(request, token), NULL, 10);
                        if (interval == LONG_MAX || interval == LONG_MIN || interval == 0) {
                            swdiag_error("Module '%s': Configuration contains invalid interval period", module);
                            ret = FALSE;
                            break;
                        }
                        (*token_ptr)++;
                    } else if (is_valid_token(token) && token->type == JSMN_STRING) {
                        if (json_token_streq(request, token, "fast")) {
                            interval = SWDIAG_PERIOD_FAST;
                        } else if (json_token_streq(request, token, "normal")) {
                            interval = SWDIAG_PERIOD_NORMAL;
                        } else if (json_token_streq(request, token, "slow")) {
                            interval = SWDIAG_PERIOD_SLOW;
                        } else {
                            swdiag_error("Module '%s': Configuration contains invalid interval name", module);
                            ret = FALSE;
                            break;
                        }
                        (*token_ptr)++;
                    } else {
                        swdiag_error("Module '%s': Configuration contains invalid test type, not a number or 'fast|normal|slow' strings", module);
                        ret = FALSE;
                        break;
                    }
                } else if (json_token_streq(request, token, "comp")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_STRING) {
                        comp_name = json_token_to_str(request, token);
                        (*token_ptr)++;
                    } else {
                        swdiag_error("Module '%s': Configuration contains invalid interval name", module);
                        ret = FALSE;
                        break;
                    }
                } else if (json_token_streq(request, token, "description")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_STRING) {
                        description = json_token_to_str(request, token);
                        (*token_ptr)++;
                    } else {
                        swdiag_error("Module '%s': Configuration contains invalid description type", module);
                        ret = FALSE;
                        break;
                    }
                } else if (json_token_streq(request, token, "health")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_STRING) {
                        health_comp = json_token_to_str(request, token);
                        (*token_ptr)++;
                    } else {
                        swdiag_error("Module '%s': Configuration contains invalid health comp name", module);
                        ret = FALSE;
                        break;
                    }
                } else {
                    swdiag_error("Module '%s': Configuration contains invalid test attribute '%s'", module, json_token_to_str(request, token));
                    ret = FALSE;
                    break;
                }
            } else {
                // Parsing error
                swdiag_error("Module '%s': Configuration expecting a JSON string", module);
                ret = FALSE;
                break;
            }
            ret = TRUE;
        }
        if (ret == TRUE && test_name != NULL && (polled == FALSE || (polled == TRUE && interval > 0 ))) {
            if (health_comp == NULL) {
                if (polled) {
                    test_context *context = calloc(1, sizeof(test_context));
                    if (context) {
                        strncpy(context->module_name, module, SWDIAG_MAX_NAME_LEN-1);
                        strncpy(context->test_name, test_name, SWDIAG_MAX_NAME_LEN-1);
                        swdiag_test_create_polled(test_name, swdiag_server_exec_test, context, interval);
                    }
                } else {
                    swdiag_test_create_notification(test_name);
                }
            } else {
                swdiag_test_create_comp_health(test_name, health_comp);
            }
            if (comp_name) {
                swdiag_comp_contains(comp_name, test_name);
            }
            if (description) {
                swdiag_test_set_description(test_name, description);
            }
        } else {
            swdiag_error("Module '%s': Configuration missing attributes for test", module);
        }
    }

    if (!ret) {
        swdiag_error("Module '%s': Configuration parse_test failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}

static boolean parse_comp(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = FALSE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        int attributes = token->size/2;
        char *comp_name = NULL;
        char *parent_comp = NULL;
        int i;
        (*token_ptr)++; // Consume the Object
        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
               // Which attribute do we have here?
               if (json_token_streq(request, token, "name")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       comp_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "parent")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       parent_comp = json_token_to_str(request, token);
                       (*token_ptr)++;
                   }
               } else {
                   swdiag_error("Module '%s': Configuration contains invalid comp attribute '%s'", module, json_token_to_str(request, token));
                   ret = FALSE;
                   break;
               }
            }
            ret = TRUE;
        }

        if (ret == TRUE && comp_name != NULL) {
            swdiag_comp_create(comp_name);
            if (parent_comp != NULL) {
                swdiag_comp_contains(parent_comp, comp_name);
            }
        }
    }

    if (!ret) {
        swdiag_error("Module '%s': Configuration parse_comp failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}

static boolean parse_rule(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = FALSE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        int attributes = token->size/2;
        char *rule_name = NULL;
        char *input_name = NULL;
        char *action_name = SWDIAG_ACTION_NOOP;
        char *comp_name = NULL;
        char *description = NULL;
        swdiag_severity_t severity = SWDIAG_SEVERITY_NONE;
        swdiag_rule_operator_t operator = SWDIAG_RULE_ON_FAIL;
        long n = 0, m = 0;
        int i;

        (*token_ptr)++;

        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
               // Which attribute do we have here?
               if (json_token_streq(request, token, "name")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       rule_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "input")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       input_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "action")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       action_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "comp")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       comp_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "operator")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       if (json_token_streq(request, token, "SWDIAG_RULE_ON_FAIL")) {
                           operator = SWDIAG_RULE_ON_FAIL;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_DISABLE")) {
                           operator = SWDIAG_RULE_DISABLE;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_EQUAL_TO_N")) {
                           operator = SWDIAG_RULE_EQUAL_TO_N;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_NOT_EQUAL_TO_N")) {
                           operator = SWDIAG_RULE_NOT_EQUAL_TO_N;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_LESS_THAN_N")) {
                           operator = SWDIAG_RULE_LESS_THAN_N;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_GREATER_THAN_N")) {
                           operator = SWDIAG_RULE_GREATER_THAN_N;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_N_EVER")) {
                           operator = SWDIAG_RULE_N_EVER;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_N_IN_ROW")) {
                           operator = SWDIAG_RULE_N_IN_ROW;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_N_IN_M")) {
                           operator = SWDIAG_RULE_N_IN_M;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_RANGE_N_TO_M")) {
                           operator = SWDIAG_RULE_RANGE_N_TO_M;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_N_IN_TIME_M")) {
                           operator = SWDIAG_RULE_N_IN_TIME_M;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_FAIL_FOR_TIME_N")) {
                           operator = SWDIAG_RULE_FAIL_FOR_TIME_N;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_OR")) {
                           operator = SWDIAG_RULE_OR;
                       } else if (json_token_streq(request, token, "SWDIAG_RULE_AND")) {
                           operator = SWDIAG_RULE_AND;
                       } else {
                           swdiag_error("Module '%s': Configuration contains invalid rule operator '%s'", module, json_token_to_str(request, token));
                       }
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "n")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                       n = strtol(json_token_to_str(request, token), NULL, 10);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "m")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                       m = strtol(json_token_to_str(request, token), NULL, 10);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "description")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       comp_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   }
               } else if (json_token_streq(request, token, "severity")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       if (json_token_streq(request, token, "SWDIAG_SEVERITY_CATASTROPHIC")) {
                           severity = SWDIAG_SEVERITY_CATASTROPHIC;
                       } else if (json_token_streq(request, token, "SWDIAG_SEVERITY_CRITICAL")) {
                           severity = SWDIAG_SEVERITY_CRITICAL;
                       } else if (json_token_streq(request, token, "SWDIAG_SEVERITY_HIGH")) {
                           severity = SWDIAG_SEVERITY_HIGH;
                       } else if (json_token_streq(request, token, "SWDIAG_SEVERITY_MEDIUM")) {
                           severity = SWDIAG_SEVERITY_MEDIUM;
                       } else if (json_token_streq(request, token, "SWDIAG_SEVERITY_LOW")) {
                           severity = SWDIAG_SEVERITY_LOW;
                       } else if (json_token_streq(request, token, "SWDIAG_SEVERITY_NONE")) {
                           severity = SWDIAG_SEVERITY_NONE;
                       } else if (json_token_streq(request, token, "SWDIAG_SEVERITY_POSITIVE")) {
                           severity = SWDIAG_SEVERITY_POSITIVE;
                       } else {
                           swdiag_error("Module '%s': Configuration contains invalid severity '%s'", module, json_token_to_str(request, token));
                       }
                       (*token_ptr)++;
                   }
               } else {
                   swdiag_error("Module '%s': Configuration contains invalid rule attribute '%s'", module, json_token_to_str(request, token));
                   ret = FALSE;
                   break;
               }
            }
            ret = TRUE;
        }
        if (ret == TRUE && rule_name != NULL && input_name != NULL) {
            swdiag_rule_create(rule_name, input_name, action_name);

            if (operator != SWDIAG_RULE_ON_FAIL) {
                swdiag_rule_set_type(rule_name, operator, n, m);
            }

            if (severity != SWDIAG_SEVERITY_NONE) {
                swdiag_rule_set_severity(rule_name, severity);
            }

            if (description) {
                swdiag_rule_set_description(rule_name, description);
            }

            if (comp_name) {
                swdiag_comp_contains(comp_name, rule_name);
            }
        }
    }

    if (!ret) {
        swdiag_error("Module '%s': Configuration parse_rule failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}

static boolean parse_action(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = TRUE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        int attributes = token->size/2;
        char *action_name = NULL;

        (*token_ptr)++; // Consume Object token

        int i;
        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
               // Which attribute do we have here?
               if (json_token_streq(request, token, "name")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       action_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   } else {
                       swdiag_error("Module '%s': Configuration contains invalid action name type", module);
                       ret = FALSE;
                       break;
                   }
               }
            } else {
                swdiag_error("Module '%s': Configuration contains invalid action type", module);
                ret = FALSE;
                break;
            }
        }

        if (ret == TRUE && action_name != NULL) {
            test_context *context = calloc(1, sizeof(test_context));
            if (context) {
                strncpy(context->module_name, module, SWDIAG_MAX_NAME_LEN-1);
                strncpy(context->test_name, action_name, SWDIAG_MAX_NAME_LEN-1);
                swdiag_action_create(action_name, swdiag_server_exec_action, context);
            }
        }

    }

    if (!ret) {
        swdiag_error("Module '%s': Configuration parse_action failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}

static boolean parse_email(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = TRUE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        int attributes = token->size/2;
        char *alert_name = NULL;
        char *alert_to = NULL;
        char *alert_subject = NULL;
        char *email_command = NULL;
        char *instance_name = NULL;
        (*token_ptr)++; // Consume Object token

        int i;
        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
               // Which attribute do we have here?
               if (json_token_streq(request, token, "name")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       alert_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   } else {
                       swdiag_error("Module '%s': Configuration contains invalid alert name type", module);
                       ret = FALSE;
                       break;
                   }
               } else if (json_token_streq(request, token, "subject")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       alert_subject = json_token_to_str(request, token);
                       (*token_ptr)++;
                   } else {
                       swdiag_error("Module '%s': Configuration contains invalid alert subject type", module);
                       ret = FALSE;
                       break;
                   }
               } else if (json_token_streq(request, token, "instance")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       instance_name = json_token_to_str(request, token);
                       (*token_ptr)++;
                   } else {
                       swdiag_error("Module '%s': Configuration contains invalid alert instance  ", module);
                       ret = FALSE;
                       break;
                   }
               } else if (json_token_streq(request, token, "to")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       alert_to = json_token_to_str(request, token);
                       (*token_ptr)++;
                   } else {
                       swdiag_error("Module '%s': Configuration contains invalid alert to  ", module);
                       ret = FALSE;
                       break;
                   }
               } else if (json_token_streq(request, token, "command")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       email_command = json_token_to_str(request, token);
                       (*token_ptr)++;
                   } else {
                       swdiag_error("Module '%s': Configuration contains invalid alert command  ", module);
                       ret = FALSE;
                       break;
                   }
               } else {
                   swdiag_error("Module '%s': Configuration contains invalid alert type '%s'", module,
                           json_token_to_str(request, token));
                   ret = FALSE;
                   break;
               }
            }
        }

        if (ret == TRUE && alert_name != NULL && alert_subject != NULL) {
            email_context *context = (email_context*)calloc(1, sizeof(email_context));
            if (context) {
                if (alert_to) {
                    // to can be NULL, in which case use the default email address
                    // from the configuration.
                    strncpy(context->to, alert_to, EMAIL_TO_MAX-1);
                }
                strncpy(context->subject, alert_subject, EMAIL_SUBJECT_MAX-1);
                if (email_command) {
                strncpy(context->command, email_command, EMAIL_COMMAND_MAX-1);
                } else {
                    context->command[0] = '\0';
                }

                swdiag_action_create(alert_name, swdiag_server_email, context);
                if (instance_name != NULL) {
                    swdiag_instance_create(alert_name, instance_name, context);
                }
            }
        }
    }

    if (!ret) {
        swdiag_error("Module '%s': Configuration parse_email failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}

static boolean parse_instance(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = FALSE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        int attributes = token->size/2;
        char *instance_name = NULL;
        char *object_name = NULL;
        int i;
        boolean delete = FALSE;

        (*token_ptr)++; // Consume Object
        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
               // Which attribute do we have here?
               if (json_token_streq(request, token, "name")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       instance_name = json_token_to_str(request, token);
                   }
                   (*token_ptr)++;
               } else if (json_token_streq(request, token, "object")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_STRING) {
                       object_name = json_token_to_str(request, token);
                   }
                   (*token_ptr)++;
               } else if (json_token_streq(request, token, "delete")) {
                   (*token_ptr)++;
                   token = *token_ptr;
                   if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                       char *command_name = json_token_to_str(request, token);
                       if (strncmp(command_name, "true", 4) == 0) {
                           delete = TRUE;
                       }
                   }
                   (*token_ptr)++;
               } else {
                   swdiag_error("parse: invalid instance attribute '%s'", json_token_to_str(request, token));
                   ret = FALSE;
                   break;
               }
            }
            ret = TRUE;
        }
        if (ret == TRUE && instance_name != NULL && object_name != NULL) {
            // Construct a test context - even for rules - so that when our
            // tests are called we know which test it is actually for. We do
            // it for rules as well since we don't know whether this is a rule
            // or a test.

            // We need to grab the object that this is an instance for and reuse
            // that context.
            if (!delete) {
                test_context *context = calloc(1, sizeof(test_context));
                strncpy(context->module_name, module, SWDIAG_MAX_NAME_LEN-1);
                strncpy(context->test_name, object_name, SWDIAG_MAX_NAME_LEN-1);
                swdiag_instance_create(object_name, instance_name, context);
            } else {
                swdiag_instance_delete(object_name, instance_name);
            }
        }
    }

    if (!ret) {
        swdiag_error("Module '%s': Configuration parse_instance failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}

static boolean parse_test_ready(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = FALSE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_ARRAY) {
        int attributes = token->size;
        int i;
        (*token_ptr)++; // Consume array token
        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
                char *test_name = json_token_to_str(request, token);
                swdiag_test_chain_ready(test_name);
                (*token_ptr)++;
            } else {
                swdiag_error("Module '%s': Configuration contains invalid ready attribute '%s'", module, json_token_to_str(request, token));
                (*token_ptr)++;
                ret = FALSE;
                break;
            }
            ret = TRUE;
        }
    }

    if (!ret) {
        swdiag_error("Module '%s': Configuration parse_test_ready failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}

static boolean parse_result(char *module, char *request, jsmntok_t **token_ptr, char *reply) {
    boolean ret = FALSE;
    jsmntok_t *token = *token_ptr;

    if (is_valid_token(token) && token->type == JSMN_OBJECT) {
        int attributes = token->size/2;
        char *test_name = NULL;
        char *instance_name = NULL;
        swdiag_result_t result = SWDIAG_RESULT_ABORT;
        long value = 0;
        int i;

        (*token_ptr)++;

        for (i = 0; i < attributes; i++) {
            token = *token_ptr;
            if (is_valid_token(token) && token->type == JSMN_STRING) {
                // Which attribute do we have here?
                if (json_token_streq(request, token, "test")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_STRING) {
                        test_name = json_token_to_str(request, token);
                        (*token_ptr)++;
                    }
                } else if (json_token_streq(request, token, "instance")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_STRING) {
                        instance_name = json_token_to_str(request, token);
                        (*token_ptr)++;
                    }
                } else if (json_token_streq(request, token, "result")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_STRING) {
                        if (json_token_streq(request, token, "pass")) {
                            result = SWDIAG_RESULT_PASS;
                        } else if (json_token_streq(request, token, "fail")) {
                            result = SWDIAG_RESULT_FAIL;
                        } else if (json_token_streq(request, token, "ignore")) {
                            result = SWDIAG_RESULT_IGNORE;
                        } else {
                            swdiag_error("Module '%s': Result contains invalid result value [pass|fail] '%s'", module, json_token_to_str(request, token));
                            (*token_ptr)++;
                            ret = FALSE;
                            break;
                        }
                        (*token_ptr)++;
                    }
                } else if (json_token_streq(request, token, "value")) {
                    (*token_ptr)++;
                    token = *token_ptr;
                    if (is_valid_token(token) && token->type == JSMN_PRIMITIVE) {
                        value = strtol(json_token_to_str(request, token), NULL, 10);
                        result = SWDIAG_RESULT_VALUE;
                        (*token_ptr)++;
                    }
                } else {
                    swdiag_error("Module '%s': Result contains invalid attribute '%s'", module, json_token_to_str(request, token));
                    (*token_ptr)++;
                    ret = FALSE;
                    break;
                }
            }
            ret = TRUE;
        }
        if (ret == TRUE && test_name != NULL) {
            swdiag_test_notify(test_name, instance_name, result, value);
        }
    }

    if (!ret) {
        swdiag_error("Module '%s': parse_result failure '%s'", module, json_token_to_str(request, token));
    }
    return ret;
}
