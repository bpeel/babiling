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

enum fv_connection_event_type {
        FV_CONNECTION_EVENT_ERROR,
};

struct fv_connection_event {
        enum fv_connection_event_type type;
        struct fv_connection *connection;
};

struct fv_connection;

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

#endif /* FV_CONNECTION_H */
