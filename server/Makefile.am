bin_PROGRAMS = \
	babiling-server \
	$(NULL)

AM_CFLAGS = \
	$(FINVENKISTO_EXTRA_CFLAGS) \
	$(SERVER_EXTRA_CFLAGS) \
	$(OPUS_CFLAGS) \
	-I$(srcdir)/../common \
	$(NULL)

if USE_SYSTEMD
AM_CFLAGS += $(LIBSYSTEMD_CFLAGS)
endif

babiling_server_SOURCES = \
	fv-base64.c \
	fv-base64.h \
	fv-connection.c \
	fv-connection.h \
	fv-error.c \
	fv-error.h \
	fv-file-error.c \
	fv-file-error.h \
	fv-log.c \
	fv-log.h \
	fv-main-context.c \
	fv-main-context.h \
	fv-network.c \
	fv-network.h \
	fv-player.c \
	fv-player.h \
	fv-playerbase.c \
	fv-playerbase.h \
	fv-signal.h \
	fv-slab.c \
	fv-slab.h \
	fv-slice.c \
	fv-slice.h \
	fv-socket.c \
	fv-socket.h \
	fv-thread.c \
	fv-thread.h \
	fv-ws-parser.c \
	fv-ws-parser.h \
	main.c \
	sha1.c \
	sha1.h \
	$(NULL)

babiling_server_LDFLAGS = \
	-pthread \
	$(NULL)

babiling_server_LDADD = \
	$(FINVENKISTO_EXTRA_LIBS) \
	$(SERVER_EXTRA_LIBS) \
	$(OPUS_LIBS) \
	$(builddir)/../common/libcommon.a \
	$(NULL)

if USE_SYSTEMD
babiling_server_LDADD += $(LIBSYSTEMD_LIBS)

dist_systemd_DATA = \
	$(builddir)/babiling.service \
	$(srcdir)/babiling.socket

systemddir = $(sysconfdir)/babiling/services
endif

%.service : %.service.in
	$(AM_V_GEN)sed -e 's,@bindir\@,$(bindir),g' $< > $@

EXTRA_DIST = \
	babiling.service.in
