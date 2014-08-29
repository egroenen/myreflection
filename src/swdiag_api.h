/* 
 * swdiag_api.h - SW Diagnostics client API module
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
 * This file declares the private internals of swdiag_api.c
 */

#ifndef __SWDIAG_API_H__
#define __SWDIAG_API_H__

#include "swdiag_obj.h"

char *swdiag_api_convert_name(const char *from_to_name);
obj_t *swdiag_api_get_or_create(const char *name, obj_type_t type);

void swdiag_api_init(void);

void swdiag_set_slave(const char *slave_name);
void swdiag_set_master(void);

void swdiag_api_test_enable_guts(const char *test_name,
                                 const char *instance_name,
                                 boolean cli);
void swdiag_api_test_disable_guts(const char *test_name, 
                                  const char *instance_name,
                                  boolean cli);
void swdiag_api_test_default(const char *test_name,
                             const char *instance_name);
void swdiag_api_rule_enable_guts(const char *rule_name,
                                 const char *instance_name,
                                 boolean cli);
void swdiag_api_rule_disable_guts(const char *rule_name, 
                                  const char *instance_name,
                                  boolean cli);
void swdiag_api_rule_default(const char *rule_name,
                             const char *instance_name);
void swdiag_api_action_enable_guts(const char *action_name,
                                   const char *instance_name,
                                   boolean cli);
void swdiag_api_action_disable_guts(const char *action_name, 
                                    const char *instance_name,
                                    boolean cli);
void swdiag_api_action_default(const char *action_name,
                               const char *instance_name);
void swdiag_api_comp_enable_guts(const char *comp_name,
                                 boolean cli);
void swdiag_api_comp_disable_guts(const char *comp_name, 
                                  boolean cli);
void swdiag_api_comp_default(const char *comp_name);

void swdiag_api_comp_contains(obj_t *parent, obj_t *child);

/** Concatenate two strings to make a new name
 *
 * Utility function that simply concatenates the two strings and returns
 * the result. The result must be used fairly quickly since the memory
 * comes from a pool of recycling buffers and will be overwritten at
 * some stage.
 *
 * Useful for creating inline object or instance names
 *
 * @param[in] prefix Prefix part of the name
 * @param[in] suffix Suffix part of the name
 * @returns A new string containing "prefixsuffix" which must be used as quickly as possible without too many intervening additional calls to swdiag_make_name()
 *
 * @warning This function is dangerous since the buffer may well be reused 
 *          before you are done with it. 
 * @todo Remove internal uses of this function in tracing since it reuses the
 *       buffers too quickly causing issues for our clients.
 */
const char *swdiag_api_make_name(const char *prefix, const char *suffix);

/** Get the test context
 *
 * Get the context for the named test if available.
 *
 * @param[in] test_name to get the context for
 * @returns The context provided by the client in swdiag_test_create_polled()
 *
 * @pre Test with test_name must exist
 *
 * @note contexts are only available for polled tests at this time.
 * 
 * @see swdiag_test_create_polled()
 */
void *swdiag_api_test_get_context(const char *test_name);

/** Add a context to this component
 * 
 * Add a context to this component which can be later retreived using swdiag_comp_get_context()
 *
 * @param[in] component_name Name of component to add the context to
 * @param[in] context Opaque context to add to this component
 */
void swdiag_api_comp_set_context(const char *component_name,
                                 void *context);

/** Get the context for this component
 *
 * @param[in] component_name Name of the component to get the context for
 * @returns The context, NULL if no context was located, or if the component could not be found.
 */
void *swdiag_api_comp_get_context(const char *component_name);

/*
 * Default object state that objects go to when created and ready to be used.
 */
extern obj_state_t default_obj_state;

#endif /* __SWDIAG_API_H__ */
