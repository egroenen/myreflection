lib_LTLIBRARIES = libmyreflection.la

bin_PROGRAMS =

SUBDIRS = . tests

EXTRA_DIST = LICENSE

###############################################################################

libmyreflection_la_LDFLAGS = \
    $(AM_LDFLAGS) \
    -version-number $(PROJECT_version_major):$(PROJECT_version_minor):$(PROJECT_version_point) $(X_libmyrefl_la_LDFLAGS)


libmyreflection_la_SOURCES = \
    src/myrefl_api.c                     \
    src/myrefl_api.h                     \
    src/myrefl_cli.h                     \
    src/myrefl_cli_local.c               \
    src/myrefl_cli_local.h               \
    src/myrefl_cli_handle.h              \
    src/myrefl_linux.c                   \
    src/myrefl_linux.h                   \
    src/myrefl_obj.c                     \
    src/myrefl_obj.h                     \
    src/myrefl_rci.c                     \
    src/myrefl_rci.h                     \
    src/myrefl_sched.c                   \
    src/myrefl_sched.h                   \
    src/myrefl_sequence.c                \
    src/myrefl_sequence.h                \
    src/myrefl_syslog.c                  \
    src/myrefl_thread.c                  \
    src/myrefl_thread.h                  \
    src/myrefl_trace.c                   \
    src/myrefl_trace.h                   \
    src/myrefl_types.h                   \
    src/myrefl_util.c                    \
    src/myrefl_util.h                    \
    src/myrefl_xos.c                     \
    src/myrefl_xos.h                    

if ADD_RPC
#
# Add RPC Network support, unconditional for now.
#
#libmyreflection_la_SOURCES += \
#    src/myrefl_unix_rpc.h                \
#    src/myrefl_unix_rpc_xdr.c            \
#    src/myrefl_unix_rpc_clnt.c           \
#    src/myrefl_unix_rpc_svc.c            \
#    src/myrefl_unix_rpc.x

CLEANFILES = \
    src/myrefl_unix_rpc.h                \
    src/myrefl_unix_rpc_xdr.c            \
    src/myrefl_unix_rpc_clnt.c           \
    src/myrefl_unix_rpc_svc.c            
endif

#
# Shouldn't really add the examples to libswdiag, use
# a different target that is not installed.
#
#libmyreflection_la_SOURCES += \
#    src/examples/example1_polledtest.c   

if ADD_PLATFORM_POSIX
    libmyreflection_la_SOURCES += \
    src/myrefl_posix.c                   
endif
if ADD_PLATFORM_WIN32
    libmyreflection_la_SOURCES += 

endif

if ADD_SERVER

#bin_PROGRAMS += myreflectioncli
bin_PROGRAMS += myreflection
#bin_PROGRAMS += myreflectiontest


myreflection_SOURCES    = server/src/myrefl_server.c \
                    server/src/myrefl_server_module.c \
                    server/src/myrefl_server_module.h \
                    server/src/myrefl_server_config.c \
                    server/src/myrefl_server_config.h \
                    server/src/mongoose/mongoose.c \
                    server/src/mongoose/mongoose.h \
                    server/src/jsmn/jsmn.c \
                    server/src/jsmn/jsmn.h \
                    server/src/smtpfuncs.c \
                    server/src/smtpfuncs.h \
                    server/src/myrefl_json_parser.c \
                    server/src/myrefl_json_parser.h \
                    server/src/myrefl_webserver.c \
                    server/src/myrefl_webserver.h \
                    server/src/myrefl_server_libinterface.c

myreflection_CFLAGS = -DNO_SSL_DL

# dynamically linked
myreflection_LDADD      = libmyreflection.la $(X_LDFLAGS) -lnsl -lpthread -lrt -ldl -lssl -lcrypto

# static link it
myreflection_LDFLAGS    = -static

myreflection_modulesdir    = $(datadir)/myreflection/server/modules
dist_myreflection_modules_SCRIPTS = \
                    server/modules/diag_diskspace.sh \
                    server/modules/diag_diskspace.sh.conf \
                    server/modules/diag_memory_conf.py \
                    server/modules/diag_memory.py \
                    server/modules/diag_postgres_conf.py \
                    server/modules/diag_postgres.py

myreflection_htmldir    = $(datadir)/myreflection/
nobase_dist_myreflection_html_DATA = \
                    server/http/index.html \
                    server/http/assets/js/get-shit-done.js
                    
                    


#init_ddir         = $(sysconfdir)/rc.d/init.d
#init_d_SCRIPTS    = server/init/myreflection

myreflection_confdir    = $(sysconfdir)
dist_myreflection_conf_DATA = server/myreflection.cfg

dist-hook:
	rm -rf `find $(distdir)/server -type d -name .svn`

endif

libmyreflectionincdir = $(pkgincludedir)

libmyreflectioninc_HEADERS = \
    include/project.h \
    \
    include/myrefl_client.h

###############################################################################

EXTRA_DIST += project/project.m4

###############################################################################

clean-local:

distclean-local:
	rm -f $(top_builddir)/Makefile
	rm -f $(top_builddir)/testlog/*.log
	rm -f $(top_builddir)/testlog/*.sum

###############################################################################
# Build source and binary rpms. For rpm-3.0 and above, the ~/.rpmmacros
# must contain the following line:
# %_topdir /home/<username>/rpm
# cd /home/<username>/rpm ; mkdir -p SOURCES BUILD RPMS/i386 SPECS SRPMS

rpm:
	$(MAKE) distdir ; \
	mv $(PACKAGE)-$(VERSION) $(PACKAGE)-0.0.1 ; \
	tar cvzf $(PACKAGE)-0.0.1.tar.gz $(PACKAGE)-0.0.1 ; \
	$(RM) -rf $(PACKAGE)-0.0.1 ; \
	RPM_TOPDIR=`rpm --showrc | perl -n -e 'print if(s/.*_topdir\s+(.*)/$$1/)'` ; \
	cp $(srcdir)/packages/rpm/myreflection.spec $$RPM_TOPDIR/SPECS ; \
	cp $(PACKAGE)-0.0.1.tar.gz $$RPM_TOPDIR/SOURCES/ ; \
	rpmbuild -ba --clean --rmsource $$RPM_TOPDIR/SPECS/$(PACKAGE).spec ; \
	mv $$RPM_TOPDIR/RPMS/x86_64/$(PACKAGE)-*.rpm . ; \
	mv $$RPM_TOPDIR/SRPMS/$(PACKAGE)-*.src.rpm .

###############################################################################

AM_CPPFLAGS = $(strip $(MK_CXX_ARCH) $(X_CXX_ARCH) $(MK_CXX_I) $(X_CXX_I))

AM_LDFLAGS = $(strip $(MK_CXX_ARCH) $(X_CXX_ARCH))

AM_CXXFLAGS = $(strip $(MK_CXX_W) $(X_CXX_W))

LIBS := $(LIBS) $(X_MINGW_LIBS)

###############################################################################

MK_CXX_ARCH = 
MK_CXX_W    = -Wall -Wformat
MK_CXX_D    = 

MK_CXX_I = \
    -I$(top_builddir)/include -I$(top_srcdir)/include -I$(top_srcdir)/src \
    -I$(top_builddir)/src

##
## include GNUmakefiles which are not full-fledged automake citizens but
## they are aware of automake variables and targets. Note that we purposely
## use a GNUmakefile extension (-include) to prevent automake from parsing
## the file.
##
#-include $(top_srcdir)/doc/GNUmakefile.mk
