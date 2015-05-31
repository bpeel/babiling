/*
 * Finvenkisto
 * Copyright (C) 2013  Neil Roberts
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
#include <openssl/sha.h>
#include <openssl/ripemd.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include "fv-connection.h"
#include "fv-proto.h"
#include "fv-util.h"
#include "fv-slice.h"
#include "fv-main-context.h"
#include "fv-buffer.h"
#include "fv-log.h"
#include "fv-store.h"
#include "fv-file-error.h"
#include "fv-socket.h"
#include "fv-proxy.h"

#define FV_CONNECTION_MAX_MESSAGE_SIZE (128 * 1024 * 1024)

/* Time in minutes between each check for whether we should send a pong
 * command and whether the remote end has gone silent */
#define FV_CONNECTION_PONG_CHECK_INTERVAL 3

/* Minimum time in seconds between sending each pong command if no
 * other data has been sent */
#define FV_CONNECTION_PONG_INTERVAL (5 * 60)

/* Time in seconds after which we will consider a remote connection to
 * be dead if we don't receive any data */
#define FV_CONNECTION_TIMEOUT (10 * 60)

/* If this assert fails then notbit clients won't recognise the pongs
 * from each other correctly */
FV_STATIC_ASSERT(FV_CONNECTION_PONG_CHECK_INTERVAL * 60 +
                  FV_CONNECTION_PONG_INTERVAL < FV_CONNECTION_TIMEOUT,
                  "The pong check and timeout values aren't going to work");

struct fv_connection {
        struct fv_netaddress remote_address;
        char *remote_address_string;
        struct fv_main_context_source *socket_source;
        struct fv_main_context_source *timer_source;
        int sock;

        struct fv_proxy *proxy;

        struct fv_buffer in_buf;
        struct fv_buffer out_buf;

        struct fv_signal event_signal;

        bool connect_succeeded;

        /* Position in out_buf of the start of a command. Used for
         * functions that build up a command on the fly */
        size_t command_start;

        /* We only load one blob from the store at a time. If we are
         * currently loading a blob then this is its cookie */
        struct fv_store_cookie *load_cookie;

        /* List of queued loads that already have a blob. This can be
         * directly copied into the out buffer next time we need to
         * write */
        struct fv_list ready_objects;
        struct fv_list objects_to_load;

        /* The last time we wrote anything to the socket. This is used
         * to determine when we need to send a pong event to keep the
         * connection alive */
        uint64_t last_write_time;
        /* Similarly for the last time we received anything. This is
         * used to detect peers that have gone silent */
        uint64_t last_read_time;
};

struct fv_connection_queue_entry {
        struct fv_list link;
        uint8_t hash[FV_PROTO_HASH_LENGTH];
        struct fv_blob *blob;
};

FV_SLICE_ALLOCATOR(struct fv_connection,
                    fv_connection_allocator);
FV_SLICE_ALLOCATOR(struct fv_connection_queue_entry,
                    fv_connection_queue_entry_allocator);

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
        if (conn->timer_source) {
                fv_main_context_remove_source(conn->timer_source);
                conn->timer_source = NULL;
        }
}

static void
set_error_state(struct fv_connection *conn)
{
        struct fv_connection_event event;

        /* Stop polling for further events */
        remove_sources(conn);

        emit_event(conn,
                   conn->connect_succeeded ?
                   FV_CONNECTION_EVENT_ERROR :
                   FV_CONNECTION_EVENT_CONNECT_FAILED,
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
        } else if (conn->connect_succeeded) {
                fv_log("Error on socket for %s: %s",
                        conn->remote_address_string,
                        strerror(value));
        } else {
                fv_log("Error connecting to %s: %s",
                        conn->remote_address_string,
                        strerror(value));
        }

        set_error_state(conn);
}

static void
get_hex_string(const uint8_t *data,
               int length,
               char *string)
{
        int i;

        for (i = 0; i < length; i++)
                snprintf(string + i * 2, 3, "%02x", data[i]);
}

static bool
connection_is_ready_to_write(struct fv_connection *conn)
{
        return (conn->out_buf.length > 0 ||
                !fv_list_empty(&conn->ready_objects));
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
addr_command_handler(struct fv_connection *conn,
                     const uint8_t *data,
                     uint32_t command_length)
{
        struct fv_connection_addr_event event;
        uint64_t n_addresses;
        ssize_t addr_length;

        if (!fv_proto_get_var_int(&data, &command_length, &n_addresses))
                goto error;

        while (n_addresses--) {
                addr_length =
                        fv_proto_get_command(data,
                                              command_length,

                                              FV_PROTO_ARGUMENT_TIMESTAMP,
                                              &event.timestamp,

                                              FV_PROTO_ARGUMENT_32,
                                              &event.stream,

                                              FV_PROTO_ARGUMENT_64,
                                              &event.services,

                                              FV_PROTO_ARGUMENT_NETADDRESS,
                                              &event.address,

                                              FV_PROTO_ARGUMENT_END);

                if (addr_length == -1)
                        goto error;

                command_length -= addr_length;
                data += addr_length;

                if (!emit_event(conn,
                                FV_CONNECTION_EVENT_ADDR,
                                &event.base))
                        return false;
        }

        return true;

error:
        fv_log("Invalid addr command received from %s",
                conn->remote_address_string);
        set_error_state(conn);
        return false;
}

static bool
getdata_command_handler(struct fv_connection *conn,
                        const uint8_t *data,
                        uint32_t command_length)
{
        struct fv_connection_getdata_event event;

        if (!fv_proto_get_var_int(&data, &command_length, &event.n_hashes))
                goto error;

        if (command_length < event.n_hashes * FV_PROTO_HASH_LENGTH)
                goto error;

        event.hashes = data;

        return emit_event(conn,
                          FV_CONNECTION_EVENT_GETDATA,
                          &event.base);

        return true;

error:
        fv_log("Invalid addr command received from %s",
                conn->remote_address_string);
        set_error_state(conn);
        return false;
}

static bool
version_command_handler(struct fv_connection *conn,
                        const uint8_t *data,
                        uint32_t command_length)
{
        struct fv_connection_version_event event;
        uint64_t dummy_64;

        if (fv_proto_get_command(data,
                                  command_length,

                                  FV_PROTO_ARGUMENT_32,
                                  &event.version,

                                  FV_PROTO_ARGUMENT_64,
                                  &event.services,

                                  FV_PROTO_ARGUMENT_TIMESTAMP,
                                  &event.timestamp,

                                  FV_PROTO_ARGUMENT_64,
                                  &dummy_64,
                                  FV_PROTO_ARGUMENT_NETADDRESS,
                                  &event.addr_recv,

                                  FV_PROTO_ARGUMENT_64,
                                  &dummy_64,
                                  FV_PROTO_ARGUMENT_NETADDRESS,
                                  &event.addr_from,

                                  FV_PROTO_ARGUMENT_64,
                                  &event.nonce,

                                  FV_PROTO_ARGUMENT_VAR_STR,
                                  &event.user_agent,

                                  FV_PROTO_ARGUMENT_VAR_INT_LIST,
                                  &event.stream_numbers,

                                  FV_PROTO_ARGUMENT_END) == -1) {
                fv_log("Invalid version command received from %s",
                        conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_VERSION,
                          &event.base);
}

static bool
verack_command_handler(struct fv_connection *conn,
                       const uint8_t *data,
                       uint32_t command_length)
{
        struct fv_connection_event event;

        return emit_event(conn, FV_CONNECTION_EVENT_VERACK, &event);
}

static bool
getpubkey_command_handler(struct fv_connection *conn,
                          const uint8_t *data,
                          uint32_t command_length)
{
        struct fv_connection_object_event event;
        ssize_t header_length;
        uint64_t address_version;

        event.type = FV_PROTO_INV_TYPE_GETPUBKEY;
        event.object_data_length = command_length;
        event.object_data = data;

        header_length = fv_proto_get_command(data,
                                              command_length,

                                              FV_PROTO_ARGUMENT_64,
                                              &event.nonce,

                                              FV_PROTO_ARGUMENT_TIMESTAMP,
                                              &event.timestamp,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &address_version,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &event.stream_number,

                                              FV_PROTO_ARGUMENT_END);

        if (header_length == -1) {
                fv_log("Invalid getpubkey command received from %s",
                        conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_OBJECT,
                          &event.base);
}

static bool
pubkey_command_handler(struct fv_connection *conn,
                       const uint8_t *data,
                       uint32_t command_length)
{
        struct fv_connection_object_event event;
        ssize_t header_length;
        uint64_t address_version;

        event.type = FV_PROTO_INV_TYPE_PUBKEY;
        event.object_data_length = command_length;
        event.object_data = data;

        header_length = fv_proto_get_command(data,
                                              command_length,

                                              FV_PROTO_ARGUMENT_64,
                                              &event.nonce,

                                              FV_PROTO_ARGUMENT_TIMESTAMP,
                                              &event.timestamp,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &address_version,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &event.stream_number,

                                              FV_PROTO_ARGUMENT_END);

        if (header_length == -1) {
                fv_log("Invalid pubkey command received from %s",
                        conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_OBJECT,
                          &event.base);
}

static bool
msg_command_handler(struct fv_connection *conn,
                    const uint8_t *data,
                    uint32_t command_length)
{
        struct fv_connection_object_event event;
        ssize_t header_length;

        event.type = FV_PROTO_INV_TYPE_MSG;
        event.object_data_length = command_length;
        event.object_data = data;

        header_length = fv_proto_get_command(data,
                                              command_length,

                                              FV_PROTO_ARGUMENT_64,
                                              &event.nonce,

                                              FV_PROTO_ARGUMENT_TIMESTAMP,
                                              &event.timestamp,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &event.stream_number,

                                              FV_PROTO_ARGUMENT_END);

        if (header_length == -1) {
                fv_log("Invalid msg command received from %s",
                        conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_OBJECT,
                          &event.base);
}

static bool
broadcast_command_handler(struct fv_connection *conn,
                          const uint8_t *data,
                          uint32_t command_length)
{
        struct fv_connection_object_event event;
        ssize_t header_length;
        uint64_t broadcast_version;

        event.type = FV_PROTO_INV_TYPE_BROADCAST;
        event.object_data_length = command_length;
        event.object_data = data;

        header_length = fv_proto_get_command(data,
                                              command_length,

                                              FV_PROTO_ARGUMENT_64,
                                              &event.nonce,

                                              FV_PROTO_ARGUMENT_TIMESTAMP,
                                              &event.timestamp,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &broadcast_version,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &event.stream_number,

                                              FV_PROTO_ARGUMENT_END);

        if (header_length == -1) {
                fv_log("Invalid msg command received from %s",
                        conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        return emit_event(conn,
                          FV_CONNECTION_EVENT_OBJECT,
                          &event.base);
}

static bool
inv_command_handler(struct fv_connection *conn,
                    const uint8_t *data,
                    uint32_t command_length)
{
        struct fv_connection_inv_event event;

        if (!fv_proto_get_var_int(&data,
                                   &command_length,
                                   &event.n_inventories) ||
            command_length < event.n_inventories * FV_PROTO_HASH_LENGTH) {
                fv_log("Invalid inv command received from %s",
                        conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        event.inventories = data;

        return emit_event(conn,
                          FV_CONNECTION_EVENT_INV,
                          &event.base);
}

static void
send_pong(struct fv_connection *conn)
{
        fv_proto_add_command(&conn->out_buf, "pong", FV_PROTO_ARGUMENT_END);
        update_poll_flags(conn);
}

static bool
ping_command_handler(struct fv_connection *conn,
                     const uint8_t *data,
                     uint32_t command_length)
{
        send_pong(conn);

        return true;
}

static const struct {
        const char *command_name;
        bool (* func)(struct fv_connection *conn,
                      const uint8_t *data,
                      uint32_t command_length);
} command_handlers[] = {
        { "getpubkey", getpubkey_command_handler },
        { "pubkey", pubkey_command_handler },
        { "msg", msg_command_handler },
        { "broadcast", broadcast_command_handler },
        { "inv", inv_command_handler },
        { "version", version_command_handler },
        { "addr", addr_command_handler },
        { "getdata", getdata_command_handler },
        { "verack", verack_command_handler },
        { "ping", ping_command_handler }
};

static bool
process_command(struct fv_connection *conn,
                const uint8_t *data,
                uint32_t command_length)
{
        char hex_a[9], hex_b[9];
        uint8_t hash[SHA512_DIGEST_LENGTH];
        int i;

        if (memcmp(data, fv_proto_magic, sizeof fv_proto_magic)) {
                get_hex_string(data, sizeof fv_proto_magic, hex_a);
                fv_log("Invalid command magic from %s (%s)",
                        conn->remote_address_string, hex_a);
                set_error_state(conn);
                return false;
        }

        if (!fv_proto_check_command_string(data + 4)) {
                fv_log("Invalid command string from %s",
                        conn->remote_address_string);
                set_error_state(conn);
                return false;
        }

        SHA512(data + FV_PROTO_HEADER_SIZE, command_length, hash);

        /* Compare the checksum */
        if (memcmp(hash, data + 20, 4)) {
                get_hex_string(data + 20, 4, hex_a);
                get_hex_string(hash, 4, hex_b);
                fv_log("Invalid checksum for %s command "
                        "received from %s (%s != %s)",
                        data + 4,
                        conn->remote_address_string,
                        hex_a,
                        hex_b);
                set_error_state(conn);
                return false;
        }

        for (i = 0; i < FV_N_ELEMENTS(command_handlers); i++) {
                if (!strcmp((const char *) data + 4,
                            command_handlers[i].command_name))
                        return command_handlers[i].func(conn,
                                                        data +
                                                        FV_PROTO_HEADER_SIZE,
                                                        command_length);
        }

        /* Unknown command which we'll just ignore */
        return true;
}

static bool
process_proxy(struct fv_connection *conn)
{
        struct fv_error *error = NULL;
        struct fv_connection_event event;

        if (conn->proxy == NULL)
                return true;

        if (fv_proxy_process_commands(conn->proxy, &error)) {
                update_poll_flags(conn);

                if (fv_proxy_is_connected(conn->proxy)) {
                        fv_proxy_free(conn->proxy);
                        conn->proxy = NULL;

                        if (!emit_event(conn,
                                        FV_CONNECTION_EVENT_PROXY_CONNECTED,
                                        &event))
                                return false;
                }
                return true;
        } else {
                fv_log("Connect failed for %s: %s",
                        conn->remote_address_string,
                        error->message);
                fv_error_free(error);
                set_error_state(conn);
                return false;
        }
}

static void
process_commands(struct fv_connection *conn)
{
        uint32_t command_length;
        uint8_t *data = conn->in_buf.data;
        size_t length = conn->in_buf.length;

        while (true) {
                if (length < FV_PROTO_HEADER_SIZE)
                        break;

                command_length = fv_proto_get_32(data + 16);

                /* Limit the length of a command or the client would
                 * be able to pretend it's going to send a really long
                 * message and we'd just keep growing the buffer
                 * until we run out of memory and abort */
                if (command_length > FV_CONNECTION_MAX_MESSAGE_SIZE) {
                        fv_log("Client %s sent a command that is too long "
                                "(%" PRIu32 ")",
                                conn->remote_address_string,
                                command_length);
                        set_error_state(conn);
                        return;
                }

                if (length < FV_PROTO_HEADER_SIZE + command_length)
                        break;

                if (!process_command(conn, data, command_length))
                        return;

                data += command_length + FV_PROTO_HEADER_SIZE;
                length -= command_length + FV_PROTO_HEADER_SIZE;
        }

        memmove(conn->in_buf.data, data, length);
        conn->in_buf.length = length;
}

static void
handle_read(struct fv_connection *conn)
{
        int got;

        fv_buffer_ensure_size(&conn->in_buf,
                               conn->in_buf.length + 1024);

        do {
                got = read(conn->sock,
                           conn->in_buf.data + conn->in_buf.length,
                           conn->in_buf.size - conn->in_buf.length);
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
                conn->last_read_time =
                        fv_main_context_get_monotonic_clock(NULL);
                conn->in_buf.length += got;

                if (process_proxy(conn)) {
                        if (conn->proxy == NULL)
                                process_commands(conn);
                }
        }
}

static void
load_cb(struct fv_blob *blob,
        void *user_data)
{
        struct fv_connection *conn = user_data;
        struct fv_connection_queue_entry *entry;

        assert(!fv_list_empty(&conn->objects_to_load));

        entry = fv_container_of(conn->objects_to_load.next,
                                 struct fv_connection_queue_entry,
                                 link);

        assert(entry->blob == NULL);

        fv_list_remove(&entry->link);
        entry->blob = fv_blob_ref(blob);

        fv_list_insert(conn->ready_objects.prev, &entry->link);

        conn->load_cookie = NULL;

        update_poll_flags(conn);
}

static void
maybe_queue_load(struct fv_connection *conn)
{
        struct fv_connection_queue_entry *entry;

        if (conn->load_cookie)
                return;

        /* We only want to load one blob at a time because we can
         * probably load items from the disk faster than we can write
         * to the socket so it we don't do this then it might end up
         * loading the whole database into memory if a peer requests
         * everything */
        if (!fv_list_empty(&conn->ready_objects))
                return;

        if (fv_list_empty(&conn->objects_to_load))
                return;

        entry = fv_container_of(conn->objects_to_load.next,
                                 struct fv_connection_queue_entry,
                                 link);
        conn->load_cookie = fv_store_load_blob(NULL, /* default store */
                                                entry->hash,
                                                load_cb,
                                                conn);
}

void
fv_connection_send_blob(struct fv_connection *conn,
                         const uint8_t *hash,
                         struct fv_blob *blob)
{
        struct fv_connection_queue_entry *entry;

        entry = fv_slice_alloc(&fv_connection_queue_entry_allocator);

        memcpy(entry->hash, hash, FV_PROTO_HASH_LENGTH);

        if (blob) {
                entry->blob = fv_blob_ref(blob);
                fv_list_insert(conn->ready_objects.prev, &entry->link);
                update_poll_flags(conn);
        } else {
                entry->blob = NULL;
                fv_list_insert(conn->objects_to_load.prev, &entry->link);
                maybe_queue_load(conn);
        }
}

static void
free_queue_entry(struct fv_connection_queue_entry *entry)
{
        if (entry->blob)
                fv_blob_unref(entry->blob);
        fv_list_remove(&entry->link);
        fv_slice_free(&fv_connection_queue_entry_allocator, entry);
}

static void
add_ready_objects(struct fv_connection *conn)
{
        struct fv_connection_queue_entry *entry;
        enum fv_proto_inv_type type;
        const char *command_name;
        size_t command_start;

        /* Keep adding objects until we either run out or we've filled
         * 1024 bytes. We don't want to add too many in one go because
         * otherwise we'll just be pointlessly copying all of the data
         * to another buffer. The socket buffer wouldn't be large
         * enough to hold all of them */
        while (conn->out_buf.length < 1024 &&
               !fv_list_empty(&conn->ready_objects)) {
                entry = fv_container_of(conn->ready_objects.next,
                                         struct fv_connection_queue_entry,
                                         link);
                type = entry->blob->type;
                command_name = fv_proto_get_command_name_for_type(type);

                command_start = conn->out_buf.length;
                fv_proto_begin_command(&conn->out_buf, command_name);
                fv_buffer_append(&conn->out_buf,
                                  entry->blob->data,
                                  entry->blob->size);
                fv_proto_end_command(&conn->out_buf, command_start);

                free_queue_entry(entry);

                maybe_queue_load(conn);
        }
}

static void
handle_write(struct fv_connection *conn)
{
        int wrote;

        add_ready_objects(conn);

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

                conn->last_write_time =
                        fv_main_context_get_monotonic_clock(NULL);

                update_poll_flags(conn);
        }
}

static void
connection_poll_cb(struct fv_main_context_source *source,
                   int fd,
                   enum fv_main_context_poll_flags flags, void *user_data)
{
        struct fv_connection *conn = user_data;

        /* If the connection ever becomes ready for writing then we
         * know it has successfully connected */
        if ((flags & (FV_MAIN_CONTEXT_POLL_OUT |
                      FV_MAIN_CONTEXT_POLL_ERROR)) ==
            FV_MAIN_CONTEXT_POLL_OUT &&
            conn->proxy == NULL &&
            !conn->connect_succeeded) {
                conn->connect_succeeded = true;
                fv_log("Connected to %s", conn->remote_address_string);
        }

        if (flags & FV_MAIN_CONTEXT_POLL_ERROR)
                handle_error(conn);
        else if (flags & FV_MAIN_CONTEXT_POLL_IN)
                handle_read(conn);
        else if (flags & FV_MAIN_CONTEXT_POLL_OUT)
                handle_write(conn);
}

static void
pong_check_cb(struct fv_main_context_source *source,
              void *user_data)
{
        struct fv_connection *conn = user_data;
        uint64_t now = fv_main_context_get_monotonic_clock(NULL);

        if ((now - conn->last_read_time) / 1000000 >= FV_CONNECTION_TIMEOUT) {
                fv_log("No data received from %s for %" PRIu64 " seconds. "
                        "Closing connection",
                        conn->remote_address_string,
                        (now - conn->last_read_time) / 1000000);
                set_error_state(conn);
                return;
        }

        if ((now - conn->last_write_time) / 1000000 >=
            FV_CONNECTION_PONG_INTERVAL &&
            !connection_is_ready_to_write(conn) &&
            conn->proxy == NULL)
                send_pong(conn);
}

static void
free_queue_entry_list(struct fv_list *list)
{
        struct fv_connection_queue_entry *entry, *tmp;

        fv_list_for_each_safe(entry, tmp, list, link)
                free_queue_entry(entry);
}

void
fv_connection_free(struct fv_connection *conn)
{
        remove_sources(conn);

        free_queue_entry_list(&conn->ready_objects);
        free_queue_entry_list(&conn->objects_to_load);

        if (conn->load_cookie)
                fv_store_cancel_task(conn->load_cookie);

        fv_free(conn->remote_address_string);
        fv_buffer_destroy(&conn->in_buf);
        fv_buffer_destroy(&conn->out_buf);
        fv_close(conn->sock);

        if (conn->proxy)
                fv_proxy_free(conn->proxy);

        fv_slice_free(&fv_connection_allocator, conn);
}

static struct fv_connection *
fv_connection_new_for_socket(int sock,
                              const struct fv_netaddress *remote_address)
{
        struct fv_connection *conn;

        conn = fv_slice_alloc(&fv_connection_allocator);

        conn->sock = sock;
        conn->remote_address = *remote_address;
        conn->remote_address_string = fv_netaddress_to_string(remote_address);
        conn->connect_succeeded = false;
        conn->proxy = NULL;

        fv_signal_init(&conn->event_signal);

        conn->socket_source =
                fv_main_context_add_poll(NULL, /* context */
                                          sock,
                                          FV_MAIN_CONTEXT_POLL_IN,
                                          connection_poll_cb,
                                          conn);
        conn->timer_source =
                fv_main_context_add_timer(NULL,
                                           FV_CONNECTION_PONG_CHECK_INTERVAL,
                                           pong_check_cb,
                                           conn);

        fv_buffer_init(&conn->in_buf);
        fv_buffer_init(&conn->out_buf);

        fv_list_init(&conn->objects_to_load);
        fv_list_init(&conn->ready_objects);
        conn->load_cookie = NULL;

        conn->last_write_time = fv_main_context_get_monotonic_clock(NULL);
        conn->last_read_time = conn->last_write_time;

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

static int
connect_socket(const struct fv_netaddress *address,
               struct fv_error **error)
{
        struct fv_netaddress_native native_address;
        char *address_string;
        int sock;

        fv_netaddress_to_native(address, &native_address);

        sock = socket(native_address.sockaddr.sa_family == AF_INET6 ?
                      PF_INET6 : PF_INET,
                      SOCK_STREAM,
                      0);
        if (sock == -1) {
                fv_file_error_set(error,
                                   errno,
                                   "Failed to create socket: %s",
                                   strerror(errno));
                return -1;
        }

        if (!fv_socket_set_nonblock(sock, error)) {
                fv_close(sock);
                return -1;
        }

        if (connect(sock,
                    &native_address.sockaddr,
                    native_address.length) == -1 &&
            errno != EINPROGRESS) {
                address_string = fv_netaddress_to_string(address);
                fv_file_error_set(error,
                                   errno,
                                   "Failed to connect to %s: %s",
                                   address_string,
                                   strerror(errno));
                fv_free(address_string);
                fv_close(sock);
                return -1;
        }

        return sock;
}

struct fv_connection *
fv_connection_connect(const struct fv_netaddress *address,
                       struct fv_error **error)
{
        int sock = connect_socket(address, error);

        return fv_connection_new_for_socket(sock, address);
}

struct fv_connection *
fv_connection_connect_proxy(const struct fv_netaddress *proxy,
                             const struct fv_netaddress *address,
                             struct fv_error **error)
{
        struct fv_connection *conn;
        int sock = connect_socket(proxy, error);

        conn = fv_connection_new_for_socket(sock, address);

        conn->proxy = fv_proxy_new(address,
                                    &conn->in_buf,
                                    &conn->out_buf);

        update_poll_flags(conn);

        return conn;
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

        conn->connect_succeeded = true;

        return conn;
}

void
fv_connection_send_verack(struct fv_connection *conn)
{
        fv_proto_add_command(&conn->out_buf,
                              "verack",
                              FV_PROTO_ARGUMENT_END);

        update_poll_flags(conn);
}

void
fv_connection_send_version(struct fv_connection *conn,
                            uint64_t nonce,
                            uint16_t local_port)
{
        struct fv_netaddress local_address;

        /* The address part of the local address is ignored by the
         * peers because they can just use the public-facing remote
         * peer address instead. In order to avoid giving away
         * unnecessary information about the local network we'll send
         * zeroes here instead */
        memset(local_address.host, 0, sizeof(local_address.host));
        local_address.port = local_port;

        fv_proto_add_command(&conn->out_buf,
                              "version",

                              FV_PROTO_ARGUMENT_32,
                              FV_PROTO_VERSION,

                              FV_PROTO_ARGUMENT_64,
                              FV_PROTO_SERVICES,

                              FV_PROTO_ARGUMENT_TIMESTAMP,

                              FV_PROTO_ARGUMENT_64,
                              FV_PROTO_SERVICES,
                              FV_PROTO_ARGUMENT_NETADDRESS,
                              &conn->remote_address,

                              FV_PROTO_ARGUMENT_64,
                              FV_PROTO_SERVICES,
                              FV_PROTO_ARGUMENT_NETADDRESS,
                              &local_address,

                              FV_PROTO_ARGUMENT_64,
                              nonce,

                              FV_PROTO_ARGUMENT_VAR_STR,
                              "/notbit:" PACKAGE_VERSION "/",

                              /* Number of streams */
                              FV_PROTO_ARGUMENT_VAR_INT,
                              UINT64_C(1),

                              /* The one stream */
                              FV_PROTO_ARGUMENT_VAR_INT,
                              UINT64_C(1),

                              FV_PROTO_ARGUMENT_END);

        update_poll_flags(conn);
}

static int
get_n_hashes_for_command(struct fv_connection *conn)
{
        return (conn->out_buf.length -
                conn->command_start -
                1 -
                FV_PROTO_HEADER_SIZE) / FV_PROTO_HASH_LENGTH;
}

static void
begin_hash_command(struct fv_connection *conn,
                   const char *command)
{
        conn->command_start = conn->out_buf.length;

        fv_proto_begin_command(&conn->out_buf, command);

        /* Reserve space for a 1-byte varint. If we need more than
         * this then we'll split the command up on the fly */
        fv_buffer_ensure_size(&conn->out_buf, conn->out_buf.length + 1);
        conn->out_buf.length += 1;
}

static void
end_hash_command(struct fv_connection *conn)
{
        int n_hashes = get_n_hashes_for_command(conn);

        if (n_hashes == 0) {
                /* Abandon the command if there weren't any hashes */
                conn->out_buf.length = conn->command_start;
        } else {
                /* Update the number of hashes */
                conn->out_buf.data[conn->command_start +
                                   FV_PROTO_HEADER_SIZE] = n_hashes;
                fv_proto_end_command(&conn->out_buf, conn->command_start);
        }

        update_poll_flags(conn);
}

static void
add_hash_for_command(struct fv_connection *conn,
                     const char *command,
                     const uint8_t *hash)
{
        int n_hashes = get_n_hashes_for_command(conn);

        /* If we can't fit further hashes into a 1-byte varint then
         * we'll start another command */
        if (n_hashes >= 0xfc) {
                end_hash_command(conn);
                begin_hash_command(conn, command);
        }

        fv_buffer_append(&conn->out_buf, hash, FV_PROTO_HASH_LENGTH);
}

void
fv_connection_begin_getdata(struct fv_connection *conn)
{
        begin_hash_command(conn, "getdata");
}

void
fv_connection_add_getdata_hash(struct fv_connection *conn,
                                const uint8_t *hash)
{
        add_hash_for_command(conn, "getdata", hash);
}

void
fv_connection_end_getdata(struct fv_connection *conn)
{
        end_hash_command(conn);
}

void
fv_connection_begin_inv(struct fv_connection *conn)
{
        begin_hash_command(conn, "inv");
}

void
fv_connection_add_inv_hash(struct fv_connection *conn,
                                const uint8_t *hash)
{
        add_hash_for_command(conn, "inv", hash);
}

void
fv_connection_end_inv(struct fv_connection *conn)
{
        end_hash_command(conn);
}

void
fv_connection_begin_addr(struct fv_connection *conn)
{
        conn->command_start = conn->out_buf.length;

        fv_proto_begin_command(&conn->out_buf, "addr");

        /* Reserve space for a 1-byte varint. If we need more than
         * this then we'll split the command up on the fly */
        fv_buffer_ensure_size(&conn->out_buf, conn->out_buf.length + 1);
        conn->out_buf.length += 1;
}

static int
get_n_hashes_for_addr(struct fv_connection *conn)
{
        return (conn->out_buf.length -
                conn->command_start -
                1 -
                FV_PROTO_HEADER_SIZE) /
                (sizeof (uint64_t) +
                 sizeof (uint32_t) +
                 sizeof (uint64_t) +
                 16 + 2);
}

void
fv_connection_add_addr_address(struct fv_connection *conn,
                                int64_t timestamp,
                                uint32_t stream,
                                uint64_t services,
                                const struct fv_netaddress *address)
{
        int n_hashes = get_n_hashes_for_addr(conn);

        /* If we can't fit further hashes into a 1-byte varint then
         * we'll start another command */
        if (n_hashes >= 0xfc) {
                fv_connection_end_addr(conn);
                fv_connection_begin_addr(conn);
        }

        fv_proto_add_64(&conn->out_buf, timestamp);
        fv_proto_add_32(&conn->out_buf, stream);
        fv_proto_add_64(&conn->out_buf, services);
        fv_proto_add_netaddress(&conn->out_buf, address);
}

void
fv_connection_end_addr(struct fv_connection *conn)
{
        int n_hashes = get_n_hashes_for_addr(conn);

        if (n_hashes == 0) {
                /* Abandon the command if there weren't any hashes */
                conn->out_buf.length = conn->command_start;
        } else {
                /* Update the number of hashes */
                conn->out_buf.data[conn->command_start +
                                   FV_PROTO_HEADER_SIZE] = n_hashes;
                fv_proto_end_command(&conn->out_buf, conn->command_start);
        }

        update_poll_flags(conn);
}
