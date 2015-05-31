bin_PROGRAMS = \
	finvenkisto-server \
	$(NULL)

AM_CFLAGS = \
	$(FINVENKISTO_EXTRA_CFLAGS) \
	$(SERVER_EXTRA_CFLAGS) \
	-I$(srcdir)/../common \
	$(NULL)

finvenkisto_server_SOURCES = \
	fv-buffer.c \
	fv-buffer.h \
	fv-connection.c \
	fv-connection.h \
	fv-error.c \
	fv-error.h \
	fv-file-error.c \
	fv-file-error.h \
	fv-list.c \
	fv-list.h \
	fv-log.c \
	fv-log.h \
	fv-main-context.c \
	fv-main-context.h \
	fv-netaddress.c \
	fv-netaddress.h \
	fv-network.c \
	fv-network.h \
	fv-proto.h \
	fv-signal.h \
	fv-slab.c \
	fv-slab.h \
	fv-slice.c \
	fv-slice.h \
	fv-socket.c \
	fv-socket.h \
	fv-thread.c \
	fv-thread.h \
	main.c \
	$(NULL)

finvenkisto_server_LDFLAGS = \
	-pthread \
	$(NULL)

finvenkisto_server_LDADD = \
	$(FINVENKISTO_EXTRA_LIBS) \
	$(SERVER_EXTRA_LIBS) \
	$(builddir)/../common/libcommon.a \
	$(NULL)
