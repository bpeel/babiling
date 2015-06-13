/*
 * Notbit - A Bitmessage client
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

#ifndef NTB_CONNECTION_H
#define NTB_CONNECTION_H

#include <stdint.h>
#include <stdbool.h>

#include "ntb-error.h"
#include "ntb-netaddress.h"
#include "ntb-buffer.h"
#include "ntb-main-context.h"
#include "ntb-signal.h"
#include "ntb-proto.h"
#include "ntb-blob.h"

enum ntb_connection_event_type {
        NTB_CONNECTION_EVENT_CONNECT_FAILED,
        NTB_CONNECTION_EVENT_ERROR,

        NTB_CONNECTION_EVENT_PROXY_CONNECTED,
        NTB_CONNECTION_EVENT_VERSION,
        NTB_CONNECTION_EVENT_INV,
        NTB_CONNECTION_EVENT_ADDR,
        NTB_CONNECTION_EVENT_OBJECT,
        NTB_CONNECTION_EVENT_GETDATA,
        NTB_CONNECTION_EVENT_VERACK
};

struct ntb_connection_event {
        enum ntb_connection_event_type type;
        struct ntb_connection *connection;
};

struct ntb_connection_version_event {
        struct ntb_connection_event base;

        uint32_t version;
        uint64_t services;
        int64_t timestamp;

        struct ntb_netaddress addr_recv;
        struct ntb_netaddress addr_from;

        uint64_t nonce;
        struct ntb_proto_var_str user_agent;
        struct ntb_proto_var_int_list stream_numbers;
};

struct ntb_connection_object_event {
        struct ntb_connection_event base;

        enum ntb_proto_inv_type type;

        uint64_t nonce;
        int64_t timestamp;
        uint64_t stream_number;

        const uint8_t *object_data;
        size_t object_data_length;
};

struct ntb_connection_inv_event {
        struct ntb_connection_event base;

        uint64_t n_inventories;
        const uint8_t *inventories;
};

struct ntb_connection_addr_event {
        struct ntb_connection_event base;

        int64_t timestamp;
        uint32_t stream;
        uint64_t services;
        struct ntb_netaddress address;
};

struct ntb_connection_getdata_event {
        struct ntb_connection_event base;

        uint64_t n_hashes;
        const uint8_t *hashes;
};

struct ntb_connection;

struct ntb_connection *
ntb_connection_connect(const struct ntb_netaddress *address,
                       struct ntb_error **error);

struct ntb_connection *
ntb_connection_connect_proxy(const struct ntb_netaddress *proxy,
                             const struct ntb_netaddress *address,
                             struct ntb_error **error);

struct ntb_connection *
ntb_connection_accept(int server_sock,
                      struct ntb_error **error);

void
ntb_connection_free(struct ntb_connection *conn);

struct ntb_signal *
ntb_connection_get_event_signal(struct ntb_connection *conn);

const char *
ntb_connection_get_remote_address_string(struct ntb_connection *conn);

const struct ntb_netaddress *
ntb_connection_get_remote_address(struct ntb_connection *conn);

void
ntb_connection_send_verack(struct ntb_connection *conn);

void
ntb_connection_send_version(struct ntb_connection *conn,
                            uint64_t nonce,
                            uint16_t local_port);

void
ntb_connection_send_blob(struct ntb_connection *conn,
                         const uint8_t *hash,
                         struct ntb_blob *blob);

void
ntb_connection_begin_getdata(struct ntb_connection *conn);

void
ntb_connection_add_getdata_hash(struct ntb_connection *conn,
                                const uint8_t *hash);

void
ntb_connection_end_getdata(struct ntb_connection *conn);

void
ntb_connection_begin_addr(struct ntb_connection *conn);

void
ntb_connection_add_addr_address(struct ntb_connection *conn,
                                int64_t timestamp,
                                uint32_t stream,
                                uint64_t services,
                                const struct ntb_netaddress *address);

void
ntb_connection_end_addr(struct ntb_connection *conn);

void
ntb_connection_begin_inv(struct ntb_connection *conn);

void
ntb_connection_add_inv_hash(struct ntb_connection *conn,
                            const uint8_t *hash);

void
ntb_connection_end_inv(struct ntb_connection *conn);

#endif /* NTB_CONNECTION_H */
