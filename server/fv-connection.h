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
#include <stdlib.h>

#include "fv-error.h"
#include "fv-netaddress.h"
#include "fv-buffer.h"
#include "fv-main-context.h"
#include "fv-signal.h"
#include "fv-proto.h"
#include "fv-playerbase.h"

enum fv_connection_event_type {
        FV_CONNECTION_EVENT_ERROR,

        FV_CONNECTION_EVENT_NEW_PLAYER,
        FV_CONNECTION_EVENT_RECONNECT,
        FV_CONNECTION_EVENT_UPDATE_POSITION,
        FV_CONNECTION_EVENT_SPEECH,
};

struct fv_connection_event {
        enum fv_connection_event_type type;
        struct fv_connection *connection;
};

struct fv_connection_reconnect_event {
        struct fv_connection_event base;

        uint64_t player_id;
};

struct fv_connection_update_position_event {
        struct fv_connection_event base;

        uint32_t x_position;
        uint32_t y_position;
        uint16_t direction;
};

struct fv_connection_speech_event {
        struct fv_connection_event base;

        const uint8_t *packet;
        size_t packet_size;
};

struct fv_connection;

struct fv_connection *
fv_connection_accept(struct fv_playerbase *playerbase,
                     int server_sock,
                     struct fv_error **error);

void
fv_connection_free(struct fv_connection *conn);

struct fv_signal *
fv_connection_get_event_signal(struct fv_connection *conn);

const char *
fv_connection_get_remote_address_string(struct fv_connection *conn);

const struct fv_netaddress *
fv_connection_get_remote_address(struct fv_connection *conn);

uint64_t
fv_connection_get_last_update_time(struct fv_connection *conn);

void
fv_connection_set_player(struct fv_connection *conn,
                         struct fv_player *player,
                         bool from_reconnect);

struct fv_player *
fv_connection_get_player(struct fv_connection *conn);

void
fv_connection_dirty_player(struct fv_connection *conn,
                           int player_num,
                           int state_flags);

void
fv_connection_queue_speech(struct fv_connection *conn,
                           int player_num);

void
fv_connection_dirty_n_players(struct fv_connection *conn);

#endif /* FV_CONNECTION_H */
