lib_LTLIBRARIES = libswdiag.la

bin_PROGRAMS =

check_PROGRAMS =

EXTRA_DIST = LICENSE

###############################################################################

libswdiag_la_LDFLAGS = \
    $(AM_LDFLAGS) \
    -version-number $(PROJECT_version_major):$(PROJECT_version_minor):$(PROJECT_version_point) $(X_libswdiag_la_LDFLAGS)


libswdiag_la_SOURCES = \
    src/swdiag_api.c                     \
    src/swdiag_api.h                     \
    src/swdiag_cli.h                     \
    src/swdiag_cli_local.c               \
    src/swdiag_cli_local.h               \
    src/swdiag_cli_handle.h              \
    src/swdiag_linux.c                   \
    src/swdiag_linux.h                   \
    src/swdiag_obj.c                     \
    src/swdiag_obj.h                     \
    src/swdiag_rci.c                     \
    src/swdiag_rci.h                     \
    src/swdiag_sched.c                   \
    src/swdiag_sched.h                   \
    src/swdiag_sequence.c                \
    src/swdiag_sequence.h                \
    src/swdiag_syslog.c                  \
    src/swdiag_thread.c                  \
    src/swdiag_thread.h                  \
    src/swdiag_trace.c                   \
    src/swdiag_trace.h                   \
    src/swdiag_types.h                   \
    src/swdiag_util.c                    \
    src/swdiag_util.h                    \
    src/swdiag_xos.c                     \
    src/swdiag_xos.h                    

if ADD_RPC
#
# Add RPC Network support, unconditional for now.
#
#libswdiag_la_SOURCES += \
#    src/swdiag_unix_rpc.h                \
#    src/swdiag_unix_rpc_xdr.c            \
#    src/swdiag_unix_rpc_clnt.c           \
#    src/swdiag_unix_rpc_svc.c            \
#    src/swdiag_unix_rpc.x

CLEANFILES = \
    src/swdiag_unix_rpc.h                \
    src/swdiag_unix_rpc_xdr.c            \
    src/swdiag_unix_rpc_clnt.c           \
    src/swdiag_unix_rpc_svc.c            
endif

#
# Shouldn't really add the examples to libswdiag, use
# a different target that is not installed.
#
#libswdiag_la_SOURCES += \
#    src/examples/example1_polledtest.c   

if ADD_PLATFORM_POSIX
    libswdiag_la_SOURCES += \
    src/swdiag_posix.c                   
endif
if ADD_PLATFORM_WIN32
    libswdiag_la_SOURCES += 

endif

if ADD_SERVER

#bin_PROGRAMS += swdiagcli
bin_PROGRAMS += swdiag
#bin_PROGRAMS += swdiagtest


swdiag_SOURCES    = server/src/swdiag_server.c \
                    server/src/swdiag_server_module.c \
                    server/src/swdiag_server_module.h \
                    server/src/swdiag_server_config.c \
                    server/src/swdiag_server_config.h \
                    server/src/mongoose/mongoose.c \
                    server/src/mongoose/mongoose.h \
                    server/src/jsmn/jsmn.c \
                    server/src/jsmn/jsmn.h \
                    server/src/smtpfuncs.c \
                    server/src/smtpfuncs.h \
                    server/src/swdiag_json_parser.c \
                    server/src/swdiag_json_parser.h \
                    server/src/swdiag_webserver.c \
                    server/src/swdiag_webserver.h \
                    server/src/swdiag_server_libinterface.c

swdiag_CFLAGS = -DNO_SSL_DL

# dynamically linked
swdiag_LDADD      = libswdiag.la $(X_LDFLAGS) -lnsl -lpthread -lrt -ldl -lssl -lcrypto

# static link it
swdiag_LDFLAGS    = -static

swdiag_modulesdir    = $(datadir)/swdiag/server/modules
dist_swdiag_modules_SCRIPTS = \
                    server/modules/diag_diskspace.sh \
                    server/modules/diag_diskspace.sh.conf \
                    server/modules/diag_memory_conf.py \
                    server/modules/diag_memory.py \
                    server/modules/diag_postgres_conf.py \
                    server/modules/diag_postgres.py

swdiag_htmldir    = $(datadir)/swdiag/
nobase_dist_swdiag_html_DATA = \
                    server/http/prime-ui-0.2/production/prime-ui-0.2-min.js \
                    server/http/prime-ui-0.2/production/prime-ui-0.2-min.css \
                    server/http/index.html \
                    server/http/jquery-ui-1.9.1.custom/css/start/jquery-ui-1.9.1.custom.min.css \
                    server/http/jquery-ui-1.9.1.custom/css/start/images/ui-bg_gloss-wave_75_2191c0_500x100.png \
                    server/http/jquery-ui-1.9.1.custom/css/start/images/ui-bg_flat_75_aaaaaa_40x100.png \
                    server/http/jquery-ui-1.9.1.custom/css/start/images/ui-bg_inset-hard_100_fcfdfd_1x100.png \
                    server/http/jquery-ui-1.9.1.custom/css/start/images/ui-bg_gloss-wave_45_e14f1c_500x100.png \
                    server/http/jquery-ui-1.9.1.custom/css/start/images/ui-icons_d8e7f3_256x240.png \
                    server/http/jquery-ui-1.9.1.custom/js/jquery-ui-1.9.1.custom.min.js \
                    server/http/jquery-ui-1.9.1.custom/js/jquery-1.8.2.js


#init_ddir         = $(sysconfdir)/rc.d/init.d
#init_d_SCRIPTS    = server/init/swdiag

swdiag_confdir    = $(sysconfdir)
dist_swdiag_conf_DATA = server/swdiag.cfg

dist-hook:
	rm -rf `find $(distdir)/server -type d -name .svn`

endif

###############################################################################

DEJATOOL = main

TESTLOGDIR = $(builddir)/testlog

override RUNTESTDEFAULTFLAGS = --tool $$tool --srcdir $$srcdir/testsuite --outdir $(TESTLOGDIR)

###############################################################################

libswdiagincdir = $(pkgincludedir)

libswdiaginc_HEADERS = \
    include/project.h \
    \
    include/swdiag_client.h

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
	cp $(srcdir)/packages/rpm/swdiag.spec $$RPM_TOPDIR/SPECS ; \
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

###############################################################################

##
## workaround: DejaGNU adds a hard-coded dependency on Makefile
## and need to create logdir
##
Makefile: ${Makefile}
	touch $@
	$(mkdir_p) $(TESTLOGDIR)


###############################################################################

##
## include GNUmakefiles which are not full-fledged automake citizens but
## they are aware of automake variables and targets. Note that we purposely
## use a GNUmakefile extension (-include) to prevent automake from parsing
## the file.
##
#-include $(top_srcdir)/doc/GNUmakefile.mk
