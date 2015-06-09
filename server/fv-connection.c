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
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include "fv-connection.h"
#include "fv-proto.h"
#include "fv-util.h"
#include "fv-main-context.h"
#include "fv-buffer.h"
#include "fv-log.h"
#include "fv-file-error.h"
#include "fv-socket.h"

struct fv_connection {
        char *remote_address_string;
        struct fv_main_context_source *socket_source;
        int sock;

        uint8_t read_buf[1024];
        size_t read_buf_pos;

        struct fv_buffer out_buf;

        struct fv_signal event_signal;
};

static bool
emit_event(struct fv_connection *conn,
           enum fv_connection_event_type type,
           struct fv_connection_event *event)
{
        event->type = type;
        event->connection = conn;
        return fv_signal_emit(&conn->event_signal, event);
}

static void
remove_sources(struct fv_connection *conn)
{
        if (conn->socket_source) {
                fv_main_context_remove_source(conn->socket_source);
                conn->socket_source = NULL;
        }
}

static void
set_error_state(struct fv_connection *conn)
{
        struct fv_connection_event event;

        /* Stop polling for further events */
        remove_sources(conn);

        emit_event(conn,
                   FV_CONNECTION_EVENT_ERROR,
                   &event);
}

static void
handle_error(struct fv_connection *conn)
{
        int value;
        unsigned int value_len = sizeof(value);

        if (getsockopt(conn->sock,
                       SOL_SOCKET,
                       SO_ERROR,
                       &value,
                       &value_len) == -1 ||
            value_len != sizeof(value) ||
            value == 0) {
                fv_log("Unknown error on socket for %s",
                       conn->remote_address_string);
        } else {
                fv_log("Error on socket for %s: %s",
                       conn->remote_address_string,
                       strerror(value));
        }

        set_error_state(conn);
}

static bool
connection_is_ready_to_write(struct fv_connection *conn)
{
        return conn->out_buf.length > 0;
}

static void
update_poll_flags(struct fv_connection *conn)
{
        enum fv_main_context_poll_flags flags = FV_MAIN_CONTEXT_POLL_IN;

        if (connection_is_ready_to_write(conn))
                flags |= FV_MAIN_CONTEXT_POLL_OUT;

        fv_main_context_modify_poll(conn->socket_source, flags);
}

static bool
handle_new_player(struct fv_connection *conn,
                  const uint8_t *data)
{
        struct fv_connection_event event;

        if (fv_proto_read_command(data, FV_PROTO_TYPE_NONE) == -1) {
                fv_log("Invalid new player command received from %s",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_NEW_PLAYER,
                          &event);
}

static bool
handle_reconnect(struct fv_connection *conn,
                 const uint8_t *data)
{
        struct fv_connection_reconnect_event event;

        if (fv_proto_read_command(data,

                                  FV_PROTO_TYPE_UINT64,
                                  &event.player_id,

                                  FV_PROTO_TYPE_NONE) == -1) {
                fv_log("Invalid reconnect command received from %s",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_UPDATE_POSITION,
                          &event.base);
}

static bool
handle_update_position(struct fv_connection *conn,
                       const uint8_t *data)
{
        struct fv_connection_update_position_event event;

        if (fv_proto_read_command(data,

                                  FV_PROTO_TYPE_UINT32,
                                  &event.x_position,

                                  FV_PROTO_TYPE_UINT32,
                                  &event.y_position,

                                  FV_PROTO_TYPE_UINT16,
                                  &event.direction,

                                  FV_PROTO_TYPE_NONE) == -1) {
                fv_log("Invalid update position command received from %s",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_UPDATE_POSITION,
                          &event.base);
}

static bool
process_command(struct fv_connection *conn,
                const uint8_t *data)
{
        switch (fv_proto_get_message_id(data)) {
        case FV_PROTO_NEW_PLAYER:
                return handle_new_player(conn, data);
        case FV_PROTO_RECONNECT:
                return handle_reconnect(conn, data);
        case FV_PROTO_UPDATE_POSITION:
                return handle_update_position(conn, data);
        }

        /* Unknown command which we'll just ignore */
        return true;
}

static void
process_commands(struct fv_connection *conn)
{
        uint16_t payload_length;
        uint8_t *data = conn->read_buf;
        size_t length = conn->read_buf_pos;

        while (true) {
                if (length < FV_PROTO_HEADER_SIZE)
                        break;

                payload_length = fv_proto_get_payload_length(data);

                /* Don't let the client try to send a command that
                 * would overflow the read buffer. All of the commands
                 * in the protocol are quite short so something has
                 * gone wrong if this happens.
                 */
                if (payload_length + FV_PROTO_HEADER_SIZE >
                    sizeof (conn->read_buf)) {
                        fv_log("Client %s sent a command that is "
                               "too long (%lu)",
                               conn->remote_address_string,
                               (unsigned long)
                               (payload_length + FV_PROTO_HEADER_SIZE));
                        set_error_state(conn);
                        return;
                }

                if (length < FV_PROTO_HEADER_SIZE + payload_length)
                        break;

                if (!process_command(conn, data))
                        return;

                data += payload_length + FV_PROTO_HEADER_SIZE;
                length -= payload_length + FV_PROTO_HEADER_SIZE;
        }

        memmove(conn->read_buf, data, length);
        conn->read_buf_pos = length;
}

static void
handle_read(struct fv_connection *conn)
{
        int got;

        do {
                got = read(conn->sock,
                           conn->read_buf + conn->read_buf_pos,
                           sizeof conn->read_buf - conn->read_buf_pos);
        } while (got == -1 && errno == EINTR);

        if (got == 0) {
                fv_log("Connection closed for %s",
                       conn->remote_address_string);
                set_error_state(conn);
        } else if (got == -1) {
                if (fv_file_error_from_errno(errno) != FV_FILE_ERROR_AGAIN) {
                        fv_log("Error reading from socket for %s: %s",
                               conn->remote_address_string,
                               strerror(errno));
                        set_error_state(conn);
                }
        } else {
                conn->read_buf_pos += got;

                process_commands(conn);
        }
}

static void
handle_write(struct fv_connection *conn)
{
        int wrote;

        do {
                wrote = write(conn->sock,
                              conn->out_buf.data,
                              conn->out_buf.length);
        } while (wrote == -1 && errno == EINTR);

        if (wrote == -1) {
                if (fv_file_error_from_errno(errno) != FV_FILE_ERROR_AGAIN) {
                        fv_log("Error writing to socket for %s: %s",
                                conn->remote_address_string,
                                strerror(errno));
                        set_error_state(conn);
                }
        } else {
                memmove(conn->out_buf.data,
                        conn->out_buf.data + wrote,
                        conn->out_buf.length - wrote);
                conn->out_buf.length -= wrote;

                update_poll_flags(conn);
        }
}

static void
connection_poll_cb(struct fv_main_context_source *source,
                   int fd,
                   enum fv_main_context_poll_flags flags, void *user_data)
{
        struct fv_connection *conn = user_data;

        if (flags & FV_MAIN_CONTEXT_POLL_ERROR)
                handle_error(conn);
        else if (flags & FV_MAIN_CONTEXT_POLL_IN)
                handle_read(conn);
        else if (flags & FV_MAIN_CONTEXT_POLL_OUT)
                handle_write(conn);
}

void
fv_connection_free(struct fv_connection *conn)
{
        remove_sources(conn);

        fv_free(conn->remote_address_string);
        fv_buffer_destroy(&conn->out_buf);
        fv_close(conn->sock);

        fv_free(conn);
}

static struct fv_connection *
fv_connection_new_for_socket(int sock,
                             const struct fv_netaddress *remote_address)
{
        struct fv_connection *conn;

        conn = fv_alloc(sizeof *conn);

        conn->sock = sock;
        conn->remote_address_string = fv_netaddress_to_string(remote_address);

        fv_signal_init(&conn->event_signal);

        conn->socket_source =
                fv_main_context_add_poll(NULL, /* context */
                                          sock,
                                          FV_MAIN_CONTEXT_POLL_IN,
                                          connection_poll_cb,
                                          conn);

        conn->read_buf_pos = 0;
        fv_buffer_init(&conn->out_buf);

        return conn;
}

struct fv_signal *
fv_connection_get_event_signal(struct fv_connection *conn)
{
        return &conn->event_signal;
}

const char *
fv_connection_get_remote_address_string(struct fv_connection *conn)
{
        return conn->remote_address_string;
}

struct fv_connection *
fv_connection_accept(int server_sock,
                     struct fv_error **error)
{
        struct fv_netaddress address;
        struct fv_netaddress_native native_address;
        struct fv_connection *conn;
        int sock;

        native_address.length = sizeof native_address.sockaddr_in6;

        do {
                sock = accept(server_sock,
                              &native_address.sockaddr,
                              &native_address.length);
        } while (sock == -1 && errno == EINTR);

        if (sock == -1) {
                fv_file_error_set(error,
                                  errno,
                                  "Error accepting connection: %s",
                                  strerror(errno));
                return NULL;
        }

        if (!fv_socket_set_nonblock(sock, error)) {
                fv_close(sock);
                return NULL;
        }

        fv_netaddress_from_native(&address, &native_address);

        conn = fv_connection_new_for_socket(sock, &address);

        return conn;
}
