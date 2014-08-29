/* 
 * swdiag_os.c - SW Diagnostics Operating System Dependent Functions
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
 * External header files and (if possible) internal header files
 * will remain OS and platform independent. Dependent functions
 * can then be localized to this module.
 *
 * Note that each OS must have it's own OS specific object directory
 * under diagnostics. If this is not the case then we need separate
 * modules for each OS to ensure separate objects.
 */
#include "swdiag_types.h"
#include "swdiag_xos.h"
#include "swdiag_trace.h"

#if defined(__IOS__) /* Function implementations for IOS */
/*
 * For IOS we need ONESEC and uint
 */
#include <sys/shim/swdiag/swdiag_xos_c_shim.h>
#endif

void swdiag_xos_time_diff (xos_time_t *start, 
                           xos_time_t *end, 
                           xos_time_t *diff)
{
    if (end->nsec >= start->nsec) {
        diff->sec = end->sec - start->sec;
        diff->nsec = end->nsec - start->nsec;
    } else {
        diff->sec = end->sec - start->sec - 1;
        diff->nsec = 1e9 + end->nsec - start->nsec;
    }
}
