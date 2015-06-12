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

        struct fv_listener dirty_listener;

        struct fv_main_context_source *gc_source;
};

FV_SLICE_ALLOCATOR(struct fv_network_client,
                   fv_network_client_allocator);

#define FV_NETWORK_MAX_CLIENTS 1024

/* Number of microseconds of inactivity before a client will be
 * considered for garbage collection.
 */
#define FV_NETWORK_MAX_CLIENT_AGE ((uint64_t) 2 * 60 * 1000000)

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

static void
dirty_player(struct fv_network *nw,
             struct fv_player *player,
             int state)
{
        struct fv_network_client *client;

        fv_list_for_each(client, &nw->clients, link)
                fv_connection_dirty_player(client->connection,
                                           player->num,
                                           state);
}

static void
dirty_n_players(struct fv_network *nw)
{
        struct fv_network_client *client;

        fv_list_for_each(client, &nw->clients, link)
                fv_connection_dirty_n_players(client->connection);
}

static bool
dirty_cb(struct fv_listener *listener,
         void *data)
{
        struct fv_playerbase_dirty_event *event = data;
        struct fv_network *nw = fv_container_of(listener,
                                                struct fv_network,
                                                dirty_listener);

        if (event->n_players_changed)
                dirty_n_players(nw);

        if (event->dirty_state)
                dirty_player(nw, event->player, event->dirty_state);

        return true;
}

static bool
handle_update_position(struct fv_network *nw,
                       struct fv_network_client *client,
                       struct fv_connection_update_position_event *event)
{
        const char *remote_address_string =
                fv_connection_get_remote_address_string(client->connection);
        struct fv_player *player = fv_connection_get_player(client->connection);

        if (player == NULL) {
                fv_log("Client %s sent a position update before a hello "
                       "message",
                       remote_address_string);
                remove_client(nw, client);
                return false;
        }

        player->x_position = event->x_position;
        player->y_position = event->y_position;
        player->direction = event->direction;

        dirty_player(nw, player, FV_PLAYER_STATE_POSITION);

        return true;
}

static void
xor_bytes(uint64_t *id,
          const uint8_t *data,
          size_t data_length)
{
        uint8_t *id_bytes = (uint8_t *) id;
        int data_pos = 0;
        int i;

        for (i = 0; i < sizeof (*id); i++) {
                id_bytes[i] ^= data[data_pos];
                data_pos = (data_pos + 1) % data_length;
        }
}

static uint64_t
generate_id(const struct fv_netaddress *remote_address)
{
        uint16_t random_data;
        uint64_t id = 0;
        int i;

        for (i = 0; i < sizeof id / sizeof random_data; i++) {
                random_data = rand();
                memcpy((uint8_t *) &id + i * sizeof random_data,
                       &random_data,
                       sizeof random_data);
        }

        /* XOR in the bytes of the client's address so that even if
         * the client can predict the random number sequence it'll
         * still be hard to guess a number of another client
         */
        xor_bytes(&id,
                  (uint8_t *) &remote_address->port,
                  sizeof remote_address->port);
        if (remote_address->family == AF_INET6)
                xor_bytes(&id,
                          (uint8_t *) &remote_address->ipv6,
                          sizeof remote_address->ipv6);
        else
                xor_bytes(&id,
                          (uint8_t *) &remote_address->ipv4,
                          sizeof remote_address->ipv4);

        return id;
}

static bool
handle_new_player(struct fv_network *nw,
                  struct fv_network_client *client,
                  struct fv_connection_event *event)
{
        const char *remote_address_string =
                fv_connection_get_remote_address_string(client->connection);
        const struct fv_netaddress *remote_address =
                fv_connection_get_remote_address(client->connection);
        struct fv_player *player =
                fv_connection_get_player(client->connection);
        uint64_t id;

        if (player != NULL) {
                fv_log("Client %s multiple hello messages",
                       remote_address_string);
                remove_client(nw, client);
                return false;
        }

        do {
                id = generate_id(remote_address);
        } while (fv_playerbase_get_player_by_id(nw->playerbase, id));

        player = fv_playerbase_add_player(nw->playerbase, id);

        fv_connection_set_player(client->connection,
                                 player,
                                 false /* from_reconnect */);
        dirty_player(nw, player, FV_PLAYER_STATE_POSITION);
        dirty_n_players(nw);

        return true;
}

static bool
handle_reconnect(struct fv_network *nw,
                 struct fv_network_client *client,
                 struct fv_connection_reconnect_event *event)
{
        const char *remote_address_string =
                fv_connection_get_remote_address_string(client->connection);
        struct fv_player *player =
                fv_connection_get_player(client->connection);

        if (player != NULL) {
                fv_log("Client %s multiple hello messages",
                       remote_address_string);
                remove_client(nw, client);
                return false;
        }

        player = fv_playerbase_get_player_by_id(nw->playerbase,
                                                event->player_id);

        /* If the client requested a player that doesn't exist then
         * divert it to a new player instead.
         */
        if (player == NULL)
                return handle_new_player(nw, client, &event->base);

        fv_connection_set_player(client->connection,
                                 player,
                                 true /* from_reconnect */);

        return true;
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

        case FV_CONNECTION_EVENT_UPDATE_POSITION: {
                struct fv_connection_update_position_event *de = (void *) event;
                return handle_update_position(nw, client, de);
        }

        case FV_CONNECTION_EVENT_RECONNECT: {
                struct fv_connection_reconnect_event *de = (void *) event;
                return handle_reconnect(nw, client, de);
        }

        case FV_CONNECTION_EVENT_NEW_PLAYER:
                return handle_new_player(nw, client, event);
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

static void
gc_cb(struct fv_main_context_source *source,
      void *user_data)
{
        struct fv_network *nw = user_data;
        struct fv_network_client *client, *tmp;
        struct fv_connection *conn;
        uint64_t now = fv_main_context_get_monotonic_clock(NULL);
        uint64_t last_update_time;

        fv_list_for_each_safe(client, tmp, &nw->clients, link) {
                conn = client->connection;
                last_update_time = fv_connection_get_last_update_time(conn);
                if (now - last_update_time >= FV_NETWORK_MAX_CLIENT_AGE) {
                        fv_log("Removing connection from %s which has been "
                               "idle for %i seconds",
                               fv_connection_get_remote_address_string(conn),
                               (int) ((now - last_update_time) / 1000000));
                        remove_client(nw, client);
                }
        }
}

struct fv_network *
fv_network_new(void)
{
        struct fv_network *nw = fv_alloc(sizeof *nw);

        nw->playerbase = fv_playerbase_new();
        nw->dirty_listener.notify = dirty_cb;
        fv_signal_add(fv_playerbase_get_dirty_signal(nw->playerbase),
                      &nw->dirty_listener);

        fv_list_init(&nw->listen_sockets);
        fv_list_init(&nw->clients);

        nw->n_clients = 0;

        nw->gc_source = fv_main_context_add_timer(NULL,
                                                  1, /* minutes */
                                                  gc_cb,
                                                  nw);

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

        fv_main_context_remove_source(nw->gc_source);

        free(nw);
}
