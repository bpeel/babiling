/*
 * Finvenkisto
 * Copyright (C) 2013, 2015  Neil Roberts
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#include "config.h"

#include <stdbool.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <arpa/inet.h>
#include <assert.h>
#include <stdlib.h>
#include <inttypes.h>

#include "fv-util.h"
#include "fv-slice.h"
#include "fv-connection.h"
#include "fv-log.h"
#include "fv-list.h"
#include "fv-network.h"
#include "fv-file-error.h"
#include "fv-socket.h"
#include "fv-netaddress.h"

struct fv_error_domain
fv_network_error;

struct fv_network_client {
        struct fv_list link;
        struct fv_connection *connection;
        struct fv_listener event_listener;
        struct fv_network *network;
};

struct fv_network_listen_socket {
        struct fv_list link;
        struct fv_netaddress address;
        int sock;
        struct fv_main_context_source *source;
        struct fv_network *nw;
};

struct fv_network {
        struct fv_list listen_sockets;

        struct fv_playerbase *playerbase;

        int n_clients;
        struct fv_list clients;
};

FV_SLICE_ALLOCATOR(struct fv_network_client,
                   fv_network_client_allocator);

#define FV_NETWORK_MAX_CLIENTS 1024

static void
update_all_listen_socket_sources(struct fv_network *nw);

static bool
connection_event_cb(struct fv_listener *listener,
                    void *data);

static void
remove_client(struct fv_network *nw,
              struct fv_network_client *client)
{
        fv_connection_free(client->connection);

        nw->n_clients--;

        fv_list_remove(&client->link);
        fv_slice_free(&fv_network_client_allocator, client);

        update_all_listen_socket_sources(nw);
}

static struct fv_network_client *
add_client(struct fv_network *nw,
           struct fv_connection *conn)
{
        struct fv_network_client *client;
        struct fv_signal *command_signal;

        client = fv_slice_alloc(&fv_network_client_allocator);

        command_signal = fv_connection_get_event_signal(conn);
        fv_signal_add(command_signal, &client->event_listener);

        client->event_listener.notify = connection_event_cb;
        client->network = nw;
        client->connection = conn;

        fv_list_insert(&nw->clients, &client->link);

        update_all_listen_socket_sources(nw);

        return client;
}

static bool
connection_event_cb(struct fv_listener *listener,
                    void *data)
{
        struct fv_connection_event *event = data;
        struct fv_network_client *client =
                fv_container_of(listener,
                                struct fv_network_client,
                                event_listener);
        struct fv_network *nw = client->network;

        switch (event->type) {
        case FV_CONNECTION_EVENT_ERROR:
                remove_client(nw, client);
                return false;
        }

        return true;
}

static void
remove_listen_socket(struct fv_network_listen_socket *listen_socket)
{
        if (listen_socket->source)
                fv_main_context_remove_source(listen_socket->source);
        fv_list_remove(&listen_socket->link);
        fv_close(listen_socket->sock);
        free(listen_socket);
}

static void
listen_socket_source_cb(struct fv_main_context_source *source,
                        int fd,
                        enum fv_main_context_poll_flags flags,
                        void *user_data)
{
        struct fv_network_listen_socket *listen_socket = user_data;
        struct fv_network *nw = listen_socket->nw;
        struct fv_connection *conn;
        struct fv_error *error = NULL;

        conn = fv_connection_accept(nw->playerbase, fd, &error);

        if (conn == NULL) {
                if (error->domain != &fv_file_error ||
                    error->code != FV_FILE_ERROR_AGAIN) {
                        fv_log("%s", error->message);
                        remove_listen_socket(listen_socket);
                }
                fv_error_free(error);
                return;
        }

        fv_log("Accepted connection from %s",
               fv_connection_get_remote_address_string(conn));

        add_client(nw, conn);

        nw->n_clients++;
}

static void
update_listen_socket_source(struct fv_network *nw,
                            struct fv_network_listen_socket *listen_socket)
{
        if (nw->n_clients >= FV_NETWORK_MAX_CLIENTS) {
                if (listen_socket->source) {
                        fv_main_context_remove_source(listen_socket->source);
                        listen_socket->source = NULL;
                }
        } else if (listen_socket->source == NULL) {
                listen_socket->source =
                        fv_main_context_add_poll(NULL,
                                                  listen_socket->sock,
                                                  FV_MAIN_CONTEXT_POLL_IN,
                                                  listen_socket_source_cb,
                                                  listen_socket);
        }
}

static void
update_all_listen_socket_sources(struct fv_network *nw)
{
        struct fv_network_listen_socket *listen_socket;

        fv_list_for_each(listen_socket, &nw->listen_sockets, link)
                update_listen_socket_source(nw, listen_socket);
}

struct fv_network *
fv_network_new(void)
{
        struct fv_network *nw = fv_alloc(sizeof *nw);

        nw->playerbase = fv_playerbase_new();

        fv_list_init(&nw->listen_sockets);
        fv_list_init(&nw->clients);

        nw->n_clients = 0;

        return nw;
}

bool
fv_network_add_listen_address(struct fv_network *nw,
                              const char *address,
                              struct fv_error **error)
{
        struct fv_network_listen_socket *listen_socket;
        struct fv_netaddress netaddress;
        struct fv_netaddress_native native_address;
        const int true_value = true;
        int sock;

        if (!fv_netaddress_from_string(&netaddress,
                                       address,
                                       FV_PROTO_DEFAULT_PORT)) {
                fv_set_error(error,
                             &fv_network_error,
                             FV_NETWORK_ERROR_INVALID_ADDRESS,
                             "The listen address %s is invalid", address);
                return false;
        }

        fv_netaddress_to_native(&netaddress, &native_address);

        sock = socket(native_address.sockaddr.sa_family == AF_INET6 ?
                      PF_INET6 : PF_INET, SOCK_STREAM, 0);
        if (sock == -1) {
                fv_file_error_set(error,
                                   errno,
                                   "Failed to create socket: %s",
                                   strerror(errno));
                return false;
        }

        setsockopt(sock,
                   SOL_SOCKET, SO_REUSEADDR,
                   &true_value, sizeof true_value);

        if (!fv_socket_set_nonblock(sock, error))
                goto error;

        if (bind(sock, &native_address.sockaddr, native_address.length) == -1) {
                fv_file_error_set(error,
                                   errno,
                                   "Failed to bind socket: %s",
                                   strerror(errno));
                goto error;
        }

        if (listen(sock, 10) == -1) {
                fv_file_error_set(error,
                                   errno,
                                   "Failed to make socket listen: %s",
                                   strerror(errno));
                goto error;
        }

        listen_socket = fv_alloc(sizeof *listen_socket);
        listen_socket->sock = sock;
        listen_socket->nw = nw;
        fv_list_insert(&nw->listen_sockets, &listen_socket->link);

        listen_socket->address = netaddress;

        listen_socket->source = NULL;

        update_listen_socket_source(nw, listen_socket);

        return true;

error:
        fv_close(sock);
        return false;
}

static void
free_listen_sockets(struct fv_network *nw)
{
        struct fv_network_listen_socket *listen_socket, *tmp;

        fv_list_for_each_safe(listen_socket, tmp, &nw->listen_sockets, link)
                remove_listen_socket(listen_socket);
}

static void
free_clients(struct fv_network *nw)
{
        struct fv_network_client *client, *tmp;

        fv_list_for_each_safe(client, tmp, &nw->clients, link)
                remove_client(nw, client);
}

void
fv_network_free(struct fv_network *nw)
{
        free_clients(nw);
        free_listen_sockets(nw);

        assert(nw->n_clients == 0);

        fv_playerbase_free(nw->playerbase);

        free(nw);
}
