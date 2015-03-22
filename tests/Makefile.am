## Process this file with automake to produce Makefile.in

##TESTS = check_xos
TESTS = check_xos check_util check_thread check_obj check_sched 
check_PROGRAMS = check_xos check_thread check_obj check_sched check_util

check_util_SOURCES = check_util.c check_base_xos_stubs.c $(top_builddir)/src/swdiag_sched.h
check_util_CFLAGS = @CHECK_CFLAGS@ -I../include
check_util_LDADD = $(top_builddir)/libswdiag.la @CHECK_LIBS@ -lm -lrt -lpthread

check_xos_SOURCES = check_xos.c check_base_xos_stubs.c $(top_builddir)/src/swdiag_sched.h
check_xos_CFLAGS = @CHECK_CFLAGS@ -I../include
check_xos_LDADD = $(top_builddir)/libswdiag.la @CHECK_LIBS@ -lm -lrt -lpthread

check_thread_SOURCES = check_thread.c check_base_xos_stubs.c $(top_builddir)/src/swdiag_sched.h
check_thread_CFLAGS = @CHECK_CFLAGS@ -I../include
check_thread_LDADD = $(top_builddir)/libswdiag.la @CHECK_LIBS@ -lm -lrt -lpthread

check_obj_SOURCES = check_obj.c check_base_xos_stubs.c $(top_builddir)/src/swdiag_obj.h
check_obj_CFLAGS = @CHECK_CFLAGS@ -I../include
check_obj_LDADD = $(top_builddir)/libswdiag.la @CHECK_LIBS@ -lm -lrt -lpthread

check_sched_SOURCES = check_sched.c check_base_xos_stubs.c $(top_builddir)/src/swdiag_sched.h
check_sched_CFLAGS = @CHECK_CFLAGS@ -I../include
check_sched_LDADD = $(top_builddir)/libswdiag.la @CHECK_LIBS@ -lm -lrt -lpthread

