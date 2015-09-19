/* 
 * myrefl_linux.c - SW Diagnostics OS specific interface
 *
 * Copyright (c) 2007-2009 Cisco Systems Inc.
 * Copyright (c) 2010-2015 Edward Groenendaal
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
 * NOTE: To be replaced with a more generic unix interface rather than 
 * separate linux and sunos since they are identical.
 */

#include "myrefl_xos.h"
#include "myrefl_obj.h"

/*
 * myrefl_xos_get_time()
 *
 * Get the unix time, secs since 1970
 */
time_t myrefl_xos_get_time (void)
{
    time_t time_now;

    time_now = time(NULL);

    if (time_now == (time_t)-1) {
        /*
         * An error, die.
         *
         * TBD.
         */
    }
    return(time_now);
}

/*
 * myrefl_xos_sleep()
 *
 * Sleep for 'time' milliseconds.
 */
void myrefl_xos_sleep (uint milliseconds)
{
    struct timespec req;

    req.tv_sec = milliseconds / 1000;
    req.tv_nsec = (milliseconds % 1000) * 1E6;

    nanosleep(&req, NULL);
}

/*
 * myrefl_xos_errmsg_to_name()
 *
 * linux can't trigger off of error messages (yet).
 */
const char *myrefl_xos_errmsg_to_name (const void *msgsym)
{
    return("invalid errmsg");
}

/* safe_srncpy() is Copyright 1998 by Mark Whitis
 * All Rights reserved.
 * use it or distribute it (including for profit) at your own risk
 * but please don't GPL it.  Retain this notice.  Modified versions
 * should be clearly labeled as such.
 * This Version has stripped out the error code
 */
char *myrefl_xos_sstrncpy (char *dst, const char *src, unsigned long size)
{
	/* Make sure destination buffer exists and has positive size */

	if(!dst) {
		return(dst);
	}
	if(size<1) {
		return(dst);
	}

	/* treat NULL and "", identically */
	if(!src) {
	 dst[0]=0;
	 return(dst);
	}

	size--;  /* leave room for trailing zero */
	while(*src && ( size-- > 0 ) ) {
		*(dst+1) = 0; // moving zero terminator
		*dst++ = *src++;
	}
	*dst = 0;

	// zero fill rest of dst buffer
	while( size-- > 0 ) {
	 *dst++ = 0;
	}

	return(dst);
}

char *myrefl_xos_sstrncat (char *s1, const char *s2, unsigned long max)
{
   return (strncat(s1, s2, max));
}

/*
 * Need a better home for this! I'm not even convinced that we need it at all.
 * TODO
 */
void myrefl_xos_recovery_in_progress (obj_instance_t *rule_instance,
                                      obj_instance_t *action_instance)
{
//    printf("Software Diagnostics Identified Fault\n"
//           " Rule Name Failed:          %s\n"
//           " Recovery Action Initiated: %s\n"
//           " Enter 'swdiag -r %s%s%s' for further details\n",
//           myrefl_obj_instance_name(rule_instance),
//           myrefl_obj_instance_name(action_instance),
//           rule_instance->obj->i.name,
//           myrefl_obj_is_member_instance(rule_instance) ? " -i " : "",
//           myrefl_obj_is_member_instance(rule_instance) ? rule_instance->name : "");
}
