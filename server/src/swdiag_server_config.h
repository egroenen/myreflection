/*
 * swdiag_server_config.h - SW Diagnostics Server Configuration file parsing
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

#ifndef SWDIAG_SERVER_CONFIG_H_
#define SWDIAG_SERVER_CONFIG_H_

#define FILEPATH_MAX (128)
#define EMAIL_MAX (128)
#define HOSTNAME_MAX (64)
#define HTTP_PORT_MAX (6)

typedef struct {
    char modules_path[FILEPATH_MAX];
    char **modules;
    int num_modules;
    char alert_email_to[EMAIL_MAX];
    char alert_email_from[EMAIL_MAX];
    char smtp_hostname[HOSTNAME_MAX];
    char http_root[FILEPATH_MAX];
    char http_port[HTTP_PORT_MAX];
    // TODO SMTP Auth must go somewhere - here?
} swdiag_server_config;

extern swdiag_server_config server_config;

extern boolean config_parse(char *filename);

#endif /* SWDIAG_SERVER_CONFIG_H_ */
