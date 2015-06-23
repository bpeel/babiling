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
#include <stdarg.h>

#include "fv-connection.h"
#include "fv-proto.h"
#include "fv-util.h"
#include "fv-main-context.h"
#include "fv-buffer.h"
#include "fv-log.h"
#include "fv-file-error.h"
#include "fv-socket.h"
#include "fv-main-context.h"
#include "fv-ws-parser.h"
#include "fv-base64.h"
#include "sha1.h"

struct fv_connection {
        struct fv_netaddress remote_address;
        char *remote_address_string;
        struct fv_main_context_source *socket_source;
        int sock;

        struct fv_playerbase *playerbase;
        struct fv_player *player;

        bool sent_player_id;
        bool consistent;

        /* Number of players that we last told the client about */
        int n_players;

        /* One byte for each player to mark the dirty state */
        struct fv_buffer dirty_players;

        uint8_t read_buf[1024];
        size_t read_buf_pos;

        uint8_t write_buf[1024];
        size_t write_buf_pos;

        /* If pong_queued is non-zero then pong_data then we need to
         * send a pong control frame with the payload given payload.
         */
        bool pong_queued;
        _Static_assert(FV_PROTO_MAX_CONTROL_FRAME_PAYLOAD <= UINT8_MAX,
                       "The max pong data length is too for a uint8_t");
        uint8_t pong_data_length;
        uint8_t pong_data[FV_PROTO_MAX_CONTROL_FRAME_PAYLOAD];

        /* If message_data_length is non-zero then we are part way
         * through reading a message whose data is stored in
         * message_data.
         */
        _Static_assert(FV_PROTO_MAX_MESSAGE_SIZE <= UINT8_MAX,
                       "The message size is too long for a uint8_t");
        uint8_t message_data_length;
        uint8_t message_data[FV_PROTO_MAX_MESSAGE_SIZE];

        struct fv_signal event_signal;

        /* Last monotonic clock time when data was received on this
         * connection. This is used for garbage collection.
         */
        uint64_t last_update_time;

        /* This is freed and becomes NULL once the headers have all
         * been parsed.
         */
        struct fv_ws_parser *ws_parser;
        /* This is allocated temporarily between getting the WebSocket
         * key header and finishing all of the headers.
         */
        SHA1_CTX *sha1_ctx;
};

static const char
ws_sec_key_guid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

static const char
ws_header_prefix[] =
        "HTTP/1.1 101 Switching Protocols\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Accept: ";

static const char
ws_header_postfix[] = "\r\n\r\n";

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
        if (conn->write_buf_pos > 0)
                return true;

        if (conn->pong_queued)
                return true;

        if (conn->player) {
                if (!conn->sent_player_id)
                        return true;

                if (!conn->consistent)
                        return true;
        }

        return false;
}

static void
update_poll_flags(struct fv_connection *conn)
{
        enum fv_main_context_poll_flags flags = FV_MAIN_CONTEXT_POLL_IN;

        if (connection_is_ready_to_write(conn))
                flags |= FV_MAIN_CONTEXT_POLL_OUT;

        fv_main_context_modify_poll(conn->socket_source, flags);
}

static int
write_command(struct fv_connection *conn,
              uint16_t command,
              ...)
{
        int ret;
        va_list ap;

        va_start(ap, command);

        ret = fv_proto_write_command_v(conn->write_buf +
                                       conn->write_buf_pos,
                                       sizeof conn->write_buf -
                                       conn->write_buf_pos,
                                       command,
                                       ap);

        va_end(ap);

        return ret;
}

static bool
write_player_state(struct fv_connection *conn,
                   int player_num)
{
        struct fv_player *player =
                fv_playerbase_get_player_by_num(conn->playerbase, player_num);
        int wrote;
        uint8_t *state = conn->dirty_players.data + player_num;

        /* We don't send any information about the player belonging to
         * this client
         */
        if (player == conn->player) {
                *state = 0;
                return true;
        }

        /* The player numbers that are sent to the client are faked in
         * order to exculde the client's own player
         */
        if (player_num >= conn->player->num)
                player_num--;

        if (*state & FV_PLAYER_STATE_POSITION) {
                wrote = write_command(conn,

                                      FV_PROTO_PLAYER_POSITION,

                                      FV_PROTO_TYPE_UINT16,
                                      (uint16_t) player_num,

                                      FV_PROTO_TYPE_UINT32,
                                      player->x_position,

                                      FV_PROTO_TYPE_UINT32,
                                      player->y_position,

                                      FV_PROTO_TYPE_UINT16,
                                      player->direction,

                                      FV_PROTO_TYPE_NONE);

                if (wrote == -1)
                        return false;

                conn->write_buf_pos += wrote;
                *state &= ~FV_PLAYER_STATE_POSITION;
        }

        return true;
}

static bool
write_player_id(struct fv_connection *conn)
{
        int wrote;

        wrote = write_command(conn,

                              FV_PROTO_PLAYER_ID,

                              FV_PROTO_TYPE_UINT64,
                              conn->player->id,

                              FV_PROTO_TYPE_NONE);

        if (wrote == -1)
                return false;

        conn->write_buf_pos += wrote;
        conn->sent_player_id = true;

        return true;
}

static bool
write_pong(struct fv_connection *conn)
{
        if (conn->write_buf_pos + conn->pong_data_length + 2 >
            sizeof (conn->write_buf))
                return false;

        /* FIN bit + opcode 0xa (pong) */
        conn->write_buf[conn->write_buf_pos++] = 0x8a;
        conn->write_buf[conn->write_buf_pos++] = conn->pong_data_length;
        memcpy(conn->write_buf + conn->write_buf_pos,
               conn->pong_data,
               conn->pong_data_length);
        conn->write_buf_pos += conn->pong_data_length;
        conn->pong_queued = false;

        return true;
}

static void
fill_write_buf(struct fv_connection *conn)
{
        int n_players;
        int wrote;
        int i;

        if (conn->pong_queued && !write_pong(conn))
                return;

        if (conn->player == NULL)
                return;

        if (!conn->sent_player_id &&
            !write_player_id(conn))
                return;

        if (conn->consistent)
                return;

        n_players = fv_playerbase_get_n_players(conn->playerbase);

        if (n_players != conn->n_players) {
                /* We don't send any information about the
                 * connection's own player to the client so we don't
                 * include it in the count.
                 */
                wrote = write_command(conn,
                                      FV_PROTO_N_PLAYERS,
                                      FV_PROTO_TYPE_UINT16,
                                      (uint16_t) (n_players - 1),
                                      FV_PROTO_TYPE_NONE);
                if (wrote == -1)
                        return;

                conn->write_buf_pos += wrote;
                conn->n_players = n_players;
        }

        if (conn->dirty_players.length > n_players)
                fv_buffer_set_length(&conn->dirty_players, n_players);

        for (i = 0; i < conn->dirty_players.length; i++) {
                if (conn->dirty_players.data[i]) {
                        if (!write_player_state(conn, i))
                                return;
                }
        }

        wrote = write_command(conn,
                              FV_PROTO_CONSISTENT,
                              FV_PROTO_TYPE_NONE);
        if (wrote == -1)
                return;

        conn->write_buf_pos += wrote;
        conn->consistent = true;
}

static bool
process_control_frame(struct fv_connection *conn,
                      int opcode,
                      const uint8_t *data,
                      size_t data_length)
{
        switch (opcode) {
        case 0x8:
                fv_log("Client %s sent a close control frame",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        case 0x9:
                assert(data_length < sizeof conn->pong_data);
                memcpy(conn->pong_data, data, data_length);
                conn->pong_data_length = data_length;
                conn->pong_queued = true;
                update_poll_flags(conn);
                break;
        case 0xa:
                /* pong, ignore */
                break;
        default:
                fv_log("Client %s sent an unknown control frame",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return true;
}

static bool
handle_new_player(struct fv_connection *conn)
{
        struct fv_connection_event event;

        if (!fv_proto_read_payload(conn->message_data + 1,
                                   conn->message_data_length - 1,
                                   FV_PROTO_TYPE_NONE)) {
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
handle_reconnect(struct fv_connection *conn)
{
        struct fv_connection_reconnect_event event;

        if (!fv_proto_read_payload(conn->message_data + 1,
                                   conn->message_data_length - 1,

                                   FV_PROTO_TYPE_UINT64,
                                   &event.player_id,

                                   FV_PROTO_TYPE_NONE)) {
                fv_log("Invalid reconnect command received from %s",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_RECONNECT,
                          &event.base);
}

static bool
handle_update_position(struct fv_connection *conn)
{
        struct fv_connection_update_position_event event;

        if (!fv_proto_read_payload(conn->message_data + 1,
                                   conn->message_data_length - 1,

                                   FV_PROTO_TYPE_UINT32,
                                   &event.x_position,

                                   FV_PROTO_TYPE_UINT32,
                                   &event.y_position,

                                   FV_PROTO_TYPE_UINT16,
                                   &event.direction,

                                   FV_PROTO_TYPE_NONE)) {
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
handle_keep_alive(struct fv_connection *conn)
{
        if (!fv_proto_read_payload(conn->message_data + 1,
                                   conn->message_data_length - 1,
                                   FV_PROTO_TYPE_NONE)) {
                fv_log("Invalid keep alive command received from %s",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return true;
}

static bool
process_message(struct fv_connection *conn)
{
        switch (conn->message_data[0]) {
        case FV_PROTO_NEW_PLAYER:
                return handle_new_player(conn);
        case FV_PROTO_RECONNECT:
                return handle_reconnect(conn);
        case FV_PROTO_UPDATE_POSITION:
                return handle_update_position(conn);
        case FV_PROTO_KEEP_ALIVE:
                return handle_keep_alive(conn);
        }

        fv_log("Client %s sent an unknown message ID (0x%u)",
               conn->remote_address_string,
               conn->message_data[0]);
        set_error_state(conn);

        return false;
}

static void
unmask_data(uint32_t mask,
            uint8_t *buffer,
            size_t buffer_length)
{
        uint32_t val;
        int i;

        for (i = 0; i + sizeof mask <= buffer_length; i += sizeof mask) {
                memcpy(&val, buffer + i, sizeof val);
                val ^= mask;
                memcpy(buffer + i, &val, sizeof val);
        }

        for (; i < buffer_length; i++)
                buffer[i] ^= *(uint8_t *) &mask;
}

static void
process_frames(struct fv_connection *conn)
{
        uint8_t *data = conn->read_buf;
        size_t length = conn->read_buf_pos;
        bool has_mask;
        bool is_fin;
        uint32_t mask;
        uint8_t payload_length;
        uint8_t opcode;

        while (true) {
                if (length < 2)
                        break;

                is_fin = data[0] & 0x80;
                opcode = data[0] & 0xf;
                has_mask = data[1] & 0x80;
                /* The extended payload lengths are currently just
                 * treated as lengths of 126 and 127 because any
                 * length greater than 125 will be caught by one of
                 * the error conditions below anyway.
                 */
                payload_length = data[1] & 0x7f;

                /* RSV bits must be zero */
                if (data[0] & 0x70) {
                        fv_log("Client %s sent a frame with non-zero "
                               "RSV bits",
                               conn->remote_address_string);
                        set_error_state(conn);
                        return;
                }

                if (opcode & 0x8) {
                        /* Control frame */
                        if (payload_length >
                            FV_PROTO_MAX_CONTROL_FRAME_PAYLOAD) {
                                fv_log("Client %s sent a control frame (0x%x) "
                                       "that is too long (%u)",
                                       conn->remote_address_string,
                                       opcode,
                                       payload_length);
                                set_error_state(conn);
                                return;
                        }
                        if (!is_fin) {
                                fv_log("Client %s sent a fragmented "
                                       "control frame",
                                       conn->remote_address_string);
                                set_error_state(conn);
                                return;
                        }
                } else if (opcode == 0x2 || opcode == 0x0) {
                        if (payload_length + conn->message_data_length >
                            FV_PROTO_MAX_MESSAGE_SIZE) {
                                fv_log("Client %s sent a message (0x%x) "
                                       "that is too long (%u)",
                                       conn->remote_address_string,
                                       opcode,
                                       payload_length);
                                set_error_state(conn);
                                return;
                        }
                        if (opcode == 0x0 && conn->message_data_length == 0) {
                                fv_log("Client %s sent a continuation frame "
                                       "without starting a message",
                                       conn->remote_address_string);
                                return;
                        }
                        if (payload_length == 0 && !is_fin) {
                                fv_log("Client %s sent an empty fragmented "
                                       "message",
                                       conn->remote_address_string);
                                set_error_state(conn);
                                return;
                        }
                } else {
                        fv_log("Client %s sent a frame opcode (0x%x) which "
                               "the server doesn't understand",
                               conn->remote_address_string,
                               opcode);
                        set_error_state(conn);
                }

                if (payload_length + 2 + (has_mask ? sizeof mask : 0) > length)
                        break;

                data += 2;
                length -= 2;

                if (has_mask) {
                        memcpy(&mask, data, sizeof mask);
                        data += sizeof mask;
                        length -= sizeof mask;
                        unmask_data(mask, data, payload_length);
                }

                if (opcode & 0x8) {
                        if (!process_control_frame(conn,
                                                   opcode,
                                                   data,
                                                   payload_length))
                                return;
                } else {
                        memcpy(conn->message_data + conn->message_data_length,
                               data,
                               payload_length);
                        conn->message_data_length += payload_length;

                        if (is_fin) {
                                if (!process_message(conn))
                                        return;

                                conn->message_data_length = 0;
                        }
                }

                data += payload_length;
                length -= payload_length;
        }

        memmove(conn->read_buf, data, length);
        conn->read_buf_pos = length;
}

static ssize_t
repeat_read(int fd,
            void *buffer,
            size_t size)
{
        ssize_t got;

        do {
                got = read(fd, buffer, size);
        } while (got == -1 && errno == EINTR);

        return got;
}

static void
handle_read_error(struct fv_connection *conn,
                  size_t got)
{
        if (got == 0) {
                fv_log("Connection closed for %s",
                       conn->remote_address_string);
                set_error_state(conn);
        } else {
                if (fv_file_error_from_errno(errno) != FV_FILE_ERROR_AGAIN) {
                        fv_log("Error reading from socket for %s: %s",
                               conn->remote_address_string,
                               strerror(errno));
                        set_error_state(conn);
                }
        }
}

static bool
ws_request_line_received_cb(const char *method,
                            const char *uri,
                            void *user_data)
{
        return true;
}

static bool
ws_header_received_cb(const char *field_name,
                      const char *value,
                      void *user_data)
{
        struct fv_connection *conn = user_data;

        if (!fv_ascii_string_case_equal(field_name, "sec-websocket-key"))
                return true;

        if (conn->sha1_ctx != NULL) {
                fv_log("Client at %s sent a WebSocket header with multiple "
                       "Sec-WebSocket-Key headers",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        conn->sha1_ctx = fv_alloc(sizeof *conn->sha1_ctx);
        SHA1Init(conn->sha1_ctx);
        SHA1Update(conn->sha1_ctx, (const uint8_t *) value, strlen(value));

        return true;
}

static bool
ws_headers_finished(struct fv_connection *conn)
{
        uint8_t sha1_hash[SHA1_DIGEST_LENGTH];
        size_t encoded_size;

        if (conn->sha1_ctx == NULL) {
                fv_log("Client at %s sent a WebSocket header without a "
                       "Sec-WebSocket-Key header",
                       conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        SHA1Update(conn->sha1_ctx,
                   (const uint8_t *) ws_sec_key_guid,
                   sizeof ws_sec_key_guid - 1);
        SHA1Final(sha1_hash, conn->sha1_ctx);
        fv_free(conn->sha1_ctx);
        conn->sha1_ctx = NULL;

        /* Send the WebSocket protocol response. This is the first
         * thing we'll send to the client so there should always be
         * enough space in the write buffer.
         */

        {
                _Static_assert(FV_BASE64_ENCODED_SIZE(SHA1_DIGEST_LENGTH) +
                               sizeof ws_header_prefix - 1 +
                               sizeof ws_header_postfix - 1 <=
                               sizeof conn->write_buf,
                               "The write buffer is too small to contain the "
                               "WebSocket protocol reply");
        }

        memcpy(conn->write_buf,
               ws_header_prefix,
               sizeof ws_header_prefix - 1);
        encoded_size = fv_base64_encode(sha1_hash,
                                        sizeof sha1_hash,
                                        (char *) conn->write_buf +
                                        sizeof ws_header_prefix - 1);
        assert(encoded_size == FV_BASE64_ENCODED_SIZE(SHA1_DIGEST_LENGTH));
        memcpy(conn->write_buf + sizeof ws_header_prefix - 1 + encoded_size,
               ws_header_postfix,
               sizeof ws_header_postfix - 1);

        conn->write_buf_pos = (FV_BASE64_ENCODED_SIZE(SHA1_DIGEST_LENGTH) +
                               sizeof ws_header_prefix - 1 +
                               sizeof ws_header_postfix - 1);

        update_poll_flags(conn);

        return true;
}

static const struct fv_ws_parser_vtable
ws_parser_vtable = {
        .request_line_received = ws_request_line_received_cb,
        .header_received = ws_header_received_cb
};

static void
handle_ws_data(struct fv_connection *conn,
               size_t got)
{
        struct fv_error *error = NULL;
        enum fv_ws_parser_result result;
        size_t consumed;

        result = fv_ws_parser_parse_data(conn->ws_parser,
                                         conn->read_buf,
                                         got,
                                         &consumed,
                                         &error);

        switch (result) {
        case FV_WS_PARSER_RESULT_NEED_MORE_DATA:
                break;
        case FV_WS_PARSER_RESULT_FINISHED:
                fv_ws_parser_free(conn->ws_parser);
                conn->ws_parser = NULL;
                memmove(conn->read_buf,
                        conn->read_buf + consumed,
                        got - consumed);
                conn->read_buf_pos = got - consumed;

                if (ws_headers_finished(conn))
                        process_frames(conn);
                break;
        case FV_WS_PARSER_RESULT_ERROR:
                if (error->domain != &fv_ws_parser_error ||
                    error->code != FV_WS_PARSER_ERROR_CANCELLED) {
                        fv_log("WebSocket protocol error from %s: %s",
                               conn->remote_address_string,
                               error->message);
                        set_error_state(conn);
                }
                fv_error_free(error);
                break;
        }
}

static void
handle_read(struct fv_connection *conn)
{
        uint64_t now;
        ssize_t got;

        got = repeat_read(conn->sock,
                          conn->read_buf + conn->read_buf_pos,
                          sizeof conn->read_buf - conn->read_buf_pos);

        if (got <= 0) {
                handle_read_error(conn, got);
        } else {
                now = fv_main_context_get_monotonic_clock(NULL);

                conn->last_update_time = now;

                if (conn->player)
                        conn->player->last_update_time = now;

                if (conn->ws_parser) {
                        handle_ws_data(conn, got);
                } else {
                        conn->read_buf_pos += got;

                        process_frames(conn);
                }
        }
}

static void
handle_write(struct fv_connection *conn)
{
        int wrote;

        fill_write_buf(conn);

        do {
                wrote = write(conn->sock,
                              conn->write_buf,
                              conn->write_buf_pos);
        } while (wrote == -1 && errno == EINTR);

        if (wrote == -1) {
                if (fv_file_error_from_errno(errno) != FV_FILE_ERROR_AGAIN) {
                        fv_log("Error writing to socket for %s: %s",
                                conn->remote_address_string,
                                strerror(errno));
                        set_error_state(conn);
                }
        } else {
                memmove(conn->write_buf,
                        conn->write_buf + wrote,
                        conn->write_buf_pos - wrote);
                conn->write_buf_pos -= wrote;

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
        fv_close(conn->sock);

        fv_buffer_destroy(&conn->dirty_players);

        if (conn->player)
                conn->player->ref_count--;

        if (conn->ws_parser)
                fv_free(conn->ws_parser);

        if (conn->sha1_ctx)
                fv_free(conn->sha1_ctx);

        fv_free(conn);
}

static struct fv_connection *
fv_connection_new_for_socket(struct fv_playerbase *playerbase,
                             int sock,
                             const struct fv_netaddress *remote_address)
{
        struct fv_connection *conn;
        int n_players;

        conn = fv_alloc(sizeof *conn);

        conn->sock = sock;
        conn->remote_address = *remote_address;
        conn->remote_address_string = fv_netaddress_to_string(remote_address);
        conn->playerbase = playerbase;
        conn->player = NULL;
        conn->ws_parser = NULL;
        conn->sha1_ctx = NULL;
        conn->pong_queued = false;
        conn->message_data_length = 0;
        conn->ws_parser = fv_ws_parser_new(&ws_parser_vtable, conn);

        fv_signal_init(&conn->event_signal);

        conn->socket_source =
                fv_main_context_add_poll(NULL, /* context */
                                          sock,
                                          FV_MAIN_CONTEXT_POLL_IN,
                                          connection_poll_cb,
                                          conn);

        conn->read_buf_pos = 0;
        conn->write_buf_pos = 0;

        fv_buffer_init(&conn->dirty_players);
        conn->sent_player_id = false;
        conn->consistent = false;
        conn->n_players = 0;
        conn->last_update_time = fv_main_context_get_monotonic_clock(NULL);

        n_players = fv_playerbase_get_n_players(playerbase);
        fv_buffer_set_length(&conn->dirty_players, n_players);
        memset(conn->dirty_players.data, FV_PLAYER_STATE_ALL, n_players);

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

const struct fv_netaddress *
fv_connection_get_remote_address(struct fv_connection *conn)
{
        return &conn->remote_address;
}

struct fv_connection *
fv_connection_accept(struct fv_playerbase *playerbase,
                     int server_sock,
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

        conn = fv_connection_new_for_socket(playerbase, sock, &address);

        return conn;
}

void
fv_connection_set_player(struct fv_connection *conn,
                         struct fv_player *player,
                         bool from_reconnect)
{
        if (player)
                player->ref_count++;

        if (conn->player)
                conn->player->ref_count--;

        conn->player = player;

        conn->sent_player_id = from_reconnect;

        update_poll_flags(conn);
}

struct fv_player *
fv_connection_get_player(struct fv_connection *conn)
{
        return conn->player;
}

void
fv_connection_dirty_player(struct fv_connection *conn,
                           int player_num,
                           int state)
{
        int old_length = conn->dirty_players.length;

        /* We don't send any information about the player that the
         * connection is controlling.
         */
        if (conn->player && conn->player->num == player_num)
                return;

        if (old_length <= player_num) {
                fv_buffer_set_length(&conn->dirty_players, player_num + 1);
                memset(conn->dirty_players.data + old_length,
                       0,
                       player_num + 1 - old_length);
        }

        conn->dirty_players.data[player_num] |= state;
        conn->consistent = false;

        update_poll_flags(conn);
}

uint64_t
fv_connection_get_last_update_time(struct fv_connection *conn)
{
        return conn->last_update_time;
}

void
fv_connection_dirty_n_players(struct fv_connection *conn)
{
        /* This will cause fill_write_buf to recheck whether the last
         * player count we sent matches what is currently in the
         * playerbase
         */
        conn->consistent = false;
        update_poll_flags(conn);
}
