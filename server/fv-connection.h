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

#ifndef FV_CONNECTION_H
#define FV_CONNECTION_H

#include <stdint.h>
#include <stdbool.h>

#include "fv-error.h"
#include "fv-netaddress.h"
#include "fv-buffer.h"
#include "fv-main-context.h"
#include "fv-signal.h"
#include "fv-proto.h"
#include "fv-blob.h"

enum fv_connection_event_type {
        FV_CONNECTION_EVENT_CONNECT_FAILED,
        FV_CONNECTION_EVENT_ERROR,

        FV_CONNECTION_EVENT_PROXY_CONNECTED,
        FV_CONNECTION_EVENT_VERSION,
        FV_CONNECTION_EVENT_INV,
        FV_CONNECTION_EVENT_ADDR,
        FV_CONNECTION_EVENT_OBJECT,
        FV_CONNECTION_EVENT_GETDATA,
        FV_CONNECTION_EVENT_VERACK
};

struct fv_connection_event {
        enum fv_connection_event_type type;
        struct fv_connection *connection;
};

struct fv_connection_version_event {
        struct fv_connection_event base;

        uint32_t version;
        uint64_t services;
        int64_t timestamp;

        struct fv_netaddress addr_recv;
        struct fv_netaddress addr_from;

        uint64_t nonce;
        struct fv_proto_var_str user_agent;
        struct fv_proto_var_int_list stream_numbers;
};

struct fv_connection_object_event {
        struct fv_connection_event base;

        enum fv_proto_inv_type type;

        uint64_t nonce;
        int64_t timestamp;
        uint64_t stream_number;

        const uint8_t *object_data;
        size_t object_data_length;
};

struct fv_connection_inv_event {
        struct fv_connection_event base;

        uint64_t n_inventories;
        const uint8_t *inventories;
};

struct fv_connection_addr_event {
        struct fv_connection_event base;

        int64_t timestamp;
        uint32_t stream;
        uint64_t services;
        struct fv_netaddress address;
};

struct fv_connection_getdata_event {
        struct fv_connection_event base;

        uint64_t n_hashes;
        const uint8_t *hashes;
};

struct fv_connection;

struct fv_connection *
fv_connection_connect(const struct fv_netaddress *address,
                       struct fv_error **error);

struct fv_connection *
fv_connection_connect_proxy(const struct fv_netaddress *proxy,
                             const struct fv_netaddress *address,
                             struct fv_error **error);

struct fv_connection *
fv_connection_accept(int server_sock,
                      struct fv_error **error);

void
fv_connection_free(struct fv_connection *conn);

struct fv_signal *
fv_connection_get_event_signal(struct fv_connection *conn);

const char *
fv_connection_get_remote_address_string(struct fv_connection *conn);

const struct fv_netaddress *
fv_connection_get_remote_address(struct fv_connection *conn);

void
fv_connection_send_verack(struct fv_connection *conn);

void
fv_connection_send_version(struct fv_connection *conn,
                            uint64_t nonce,
                            uint16_t local_port);

void
fv_connection_send_blob(struct fv_connection *conn,
                         const uint8_t *hash,
                         struct fv_blob *blob);

void
fv_connection_begin_getdata(struct fv_connection *conn);

void
fv_connection_add_getdata_hash(struct fv_connection *conn,
                                const uint8_t *hash);

void
fv_connection_end_getdata(struct fv_connection *conn);

void
fv_connection_begin_addr(struct fv_connection *conn);

void
fv_connection_add_addr_address(struct fv_connection *conn,
                                int64_t timestamp,
                                uint32_t stream,
                                uint64_t services,
                                const struct fv_netaddress *address);

void
fv_connection_end_addr(struct fv_connection *conn);

void
fv_connection_begin_inv(struct fv_connection *conn);

void
fv_connection_add_inv_hash(struct fv_connection *conn,
                            const uint8_t *hash);

void
fv_connection_end_inv(struct fv_connection *conn);

#endif /* FV_CONNECTION_H */
