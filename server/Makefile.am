bin_PROGRAMS = \
	finvenkisto-server \
	$(NULL)

AM_CPPFLAGS = \
	$(LIBCRYPTO_CFLAGS) \
	$(FINVENKISTO_EXTRA_CFLAGS) \
	$(SERVER_EXTRA_CFLAGS) \
	$(NULL)

finvenkisto_server_SOURCES = \
	fv-address.c \
	fv-address.h \
	fv-base58.c \
	fv-base58.h \
	fv-base64.c \
	fv-base64.h \
	fv-blob.c \
	fv-blob.h \
	fv-buffer.c \
	fv-buffer.h \
	fv-connection.c \
	fv-connection.h \
	fv-cpus.c \
	fv-cpus.h \
	fv-crypto.c \
	fv-crypto.h \
	fv-daemon.c \
	fv-daemon.h \
	fv-dns-bootstrap.c \
	fv-dns-bootstrap.h \
	fv-ecc.c \
	fv-ecc.h \
	fv-encoded-words.c \
	fv-encoded-words.h \
	fv-error.c \
	fv-error.h \
	fv-file-error.c \
	fv-file-error.h \
	fv-hash-table.c \
	fv-hash-table.h \
	fv-ipc.c \
	fv-ipc.h \
	fv-ipc-client.c \
	fv-ipc-client.h \
	fv-ipc-proto.c \
	fv-ipc-proto.h \
	fv-ipc-sockaddr.c \
	fv-ipc-sockaddr.h \
	fv-key.c \
	fv-key.h \
	fv-key-value.c \
	fv-key-value.h \
	fv-keygen.c \
	fv-keygen.h \
	fv-keyring.c \
	fv-keyring.h \
	fv-list.c \
	fv-list.h \
	fv-load-keys.c \
	fv-load-keys.h \
	fv-load-outgoings.c \
	fv-load-outgoings.h \
	fv-log.c \
	fv-log.h \
	fv-mail-parser.c \
	fv-mail-parser.h \
	fv-main-context.c \
	fv-main-context.h \
	fv-mkdir.c \
	fv-mkdir.h \
	fv-netaddress.c \
	fv-netaddress.h \
	fv-network.c \
	fv-network.h \
	fv-parse-addresses.c \
	fv-parse-addresses.h \
	fv-parse-content-type.c \
	fv-parse-content-type.h \
	fv-pointer-array.h \
	fv-pow.c \
	fv-pow.h \
	fv-proto.c \
	fv-proto.h \
	fv-proxy.c \
	fv-proxy.h \
	fv-quoted-printable.c \
	fv-quoted-printable.h \
	fv-ref-count.h \
	fv-save-message.c \
	fv-save-message.h \
	fv-sendmail.c \
	fv-sendmail.h \
	fv-signal.h \
	fv-slab.c \
	fv-slab.h \
	fv-slice.c \
	fv-slice.h \
	fv-socket.c \
	fv-socket.h \
	fv-store.c \
	fv-store.h \
	fv-util.c \
	fv-util.h \
	main.c \
	$(NULL)

finvenkisto_server_LDFLAGS = \
	-pthread \
	$(NULL)

finvenkisto_server_LDADD = \
	$(LIBCRYPTO_LIBS) \
	$(FINVENKISTO_EXTRA_LIBS) \
	$(SERVER_EXTRA_LIBS) \
	$(NULL)
