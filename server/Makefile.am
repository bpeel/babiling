bin_PROGRAMS = \
	finvenkisto-server \
	$(NULL)

AM_CFLAGS = \
	$(FINVENKISTO_EXTRA_CFLAGS) \
	$(SERVER_EXTRA_CFLAGS) \
	-I$(srcdir)/../common \
	$(NULL)

finvenkisto_server_SOURCES = \
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

finvenkisto_server_LDFLAGS = \
	-pthread \
	$(NULL)

finvenkisto_server_LDADD = \
	$(FINVENKISTO_EXTRA_LIBS) \
	$(SERVER_EXTRA_LIBS) \
	$(builddir)/../common/libcommon.a \
	$(NULL)
