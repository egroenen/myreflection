/*
 * Copyright (c) 2003, Mayukh Bose
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of Mayukh Bose nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */
/* 2003-11-17 - Reply-To Mod added by Chris Lacy-Hulbert */
/* 2003-12-26 - Added snprintf fix for WIN32. Thanks Wingman! */

#ifndef __SMTPFUNCS_H_2003_11_15__
#define __SMTPFUNCS_H_2003_11_15__

#ifdef WIN32
#include <winsock2.h>
#include <io.h>
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#define INVALID_SOCKET	-1
#define SOCKET_ERROR	-1
#endif

#ifndef FALSE
#define FALSE	0
#endif

#ifndef TRUE
#define TRUE	1
#endif

#ifndef	NULL
#define	NULL	0
#endif

#ifndef ERROR
#define ERROR	-1
#endif

/* Mail server status codes */
#define	MAIL_WELCOME	220
#define MAIL_OK			250
#define MAIL_GO_AHEAD	354
#define MAIL_GOODBYE	221

/* Error codes returned by send_mail */
#define E_NO_SOCKET_CONN	-1
#define E_PROTOCOL_ERROR	-2

int send_mail(const char *smtpserver, const char *hostname, const char *from, const char *to,
				const char *subject, const char *replyto, const char *msg);
int connect_to_server(const char *server);
int send_command(int n_sock, const char *prefix, const char *cmd, 
					const char *suffix, int ret_code);
int send_mail_message(int n_sock, const char *from, const char *to, 
						const char *subject, const char *replyto, const char *msg);


#ifdef WIN32
int startup_sockets_lib(void);
int cleanup_sockets_lib(void);
#define snprintf  _snprintf
#endif

#endif
