/*
 * Finvenkisto
 *
 * Copyright (C) 2015 Neil Roberts
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"

struct fv_network_base {
        bool sent_hello;
        bool has_player_id;
        uint64_t player_id;

        bool player_dirty;
        struct fv_person player;

        /* Array of fv_persons */
        struct fv_buffer players;
        /* Array of one bit for each player to mark whether it has
         * changed since the last consistent state was reached
         */
        struct fv_buffer dirty_players;

        /* The last time we sent any data to the server. This is used
         * to track when we need to send a keep alive event.
         */
        uint32_t last_update_time;

        fv_network_consistent_event_cb consistent_event_cb;
        void *user_data;
};

struct fv_network;

#define FV_NETWORK_MIN_CONNECT_WAIT_TIME (1 * 1000)
#define FV_NETWORK_MAX_CONNECT_WAIT_TIME (15 * 1000)

#define FV_NETWORK_N_PLAYERS(nw)                                        \
        (fv_network_get_base(nw)->players.length / sizeof (struct fv_person))

/* Time in milliseconds after which if no other data is sent the
 * client will send a KEEP_ALIVE message */
#define FV_NETWORK_KEEP_ALIVE_TIME (60 * 1000)

static int
write_command(struct fv_network *nw,
              uint8_t command,
              ...);

static bool
write_buf_is_empty(struct fv_network *nw);

static struct fv_network_base *
fv_network_get_base(struct fv_network *nw);

static void
set_socket_error(struct fv_network *nw);

static bool
needs_write_poll_base(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);

        if (!base->sent_hello)
                return true;

        if (base->player_dirty)
                return true;

        if (base->last_update_time + FV_NETWORK_KEEP_ALIVE_TIME <=
            SDL_GetTicks())
                return true;

        return false;
}

static bool
write_new_player(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        int res;

        res = write_command(nw,
                            FV_PROTO_NEW_PLAYER,
                            FV_PROTO_TYPE_NONE);

        if (res != -1) {
                base->sent_hello = true;
                return true;
        } else {
                return false;
        }
}

static bool
write_reconnect(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        int res;

        res = write_command(nw,
                            FV_PROTO_RECONNECT,
                            FV_PROTO_TYPE_UINT64,
                            base->player_id,
                            FV_PROTO_TYPE_NONE);

        if (res != -1) {
                base->sent_hello = true;
                return true;
        } else {
                return false;
        }
}

static bool
write_player(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        int res;

        res = write_command(nw,
                            FV_PROTO_UPDATE_POSITION,

                            FV_PROTO_TYPE_UINT32,
                            base->player.x_position,

                            FV_PROTO_TYPE_UINT32,
                            base->player.y_position,

                            FV_PROTO_TYPE_UINT16,
                            base->player.direction,

                            FV_PROTO_TYPE_NONE);

        if (res != -1) {
                base->player_dirty = false;
                return true;
        } else {
                return false;
        }
}

static bool
write_keep_alive(struct fv_network *nw)
{
        int res;

        res = write_command(nw,
                            FV_PROTO_KEEP_ALIVE,

                            FV_PROTO_TYPE_NONE);

        /* This should always succeed because it'll only be attempted
         * if the write buffer is empty.
         */
        assert(res > 0);

        return true;
}

static void
fill_write_buf(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);

        if (!base->sent_hello) {
                if (base->has_player_id) {
                        if (!write_reconnect(nw))
                                return;
                } else if (!write_new_player(nw))
                        return;
        }

        if (base->player_dirty) {
                if (!write_player(nw))
                        return;
        }

        /* If nothing else writes and we haven't written for a while
         * then add a keep alive. This should be the last thing in
         * this function.
         */
        if (write_buf_is_empty(nw) &&
            base->last_update_time + FV_NETWORK_KEEP_ALIVE_TIME <=
            SDL_GetTicks()) {
                if (!write_keep_alive(nw))
                        return;
        }
}

static bool
handle_player_id(struct fv_network *nw,
                 const uint8_t *payload,
                 size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        uint64_t player_id;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_UINT64, &player_id,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        base->player_id = player_id;
        base->has_player_id = true;

        return true;
}

static bool
handle_consistent(struct fv_network *nw,
                  const uint8_t *payload,
                  size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        struct fv_network_consistent_event event;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        if (base->consistent_event_cb) {
                event.n_players = FV_NETWORK_N_PLAYERS(nw);
                event.players = (const struct fv_person *) base->players.data;
                event.dirty_players = &base->dirty_players;

                base->consistent_event_cb(&event, base->user_data);
        }

        memset(base->dirty_players.data, 0, base->dirty_players.length);

        return true;
}

static bool
handle_n_players(struct fv_network *nw,
                 const uint8_t *payload,
                 size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        uint16_t n_players;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_UINT16, &n_players,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        fv_buffer_set_length(&base->players,
                             sizeof (struct fv_person) * n_players);
        fv_bitmask_set_length(&base->dirty_players, n_players);

        return true;
}

static bool
handle_player_position(struct fv_network *nw,
                       const uint8_t *payload,
                       size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        struct fv_person player;
        uint16_t player_num;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_UINT16, &player_num,
                                   FV_PROTO_TYPE_UINT32, &player.x_position,
                                   FV_PROTO_TYPE_UINT32, &player.y_position,
                                   FV_PROTO_TYPE_UINT16, &player.direction,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        if (player_num < FV_NETWORK_N_PLAYERS(nw)) {
                memcpy(base->players.data +
                       player_num * sizeof (struct fv_person),
                       &player,
                       sizeof player);
                fv_bitmask_set(&base->dirty_players, player_num, true);
        }

        return true;
}


static bool
handle_message(struct fv_network *nw,
               uint8_t message_id,
               const uint8_t *message_payload,
               size_t message_payload_length)
{
        switch (message_id) {
        case FV_PROTO_PLAYER_ID:
                return handle_player_id(nw,
                                        message_payload,
                                        message_payload_length);
        case FV_PROTO_CONSISTENT:
                return handle_consistent(nw,
                                         message_payload,
                                         message_payload_length);
        case FV_PROTO_N_PLAYERS:
                return handle_n_players(nw,
                                        message_payload,
                                        message_payload_length);
        case FV_PROTO_PLAYER_POSITION:
                return handle_player_position(nw,
                                              message_payload,
                                              message_payload_length);
        }

        assert(!"unknown message_id");

        return false;
}

static void
init_new_connection(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);

        base->sent_hello = false;
        base->player_dirty = true;
        base->last_update_time = SDL_GetTicks();
}

static void
init_base(struct fv_network_base *nw)
{
        nw->has_player_id = false;

        fv_buffer_init(&nw->players);
        fv_buffer_init(&nw->dirty_players);
}

static void
destroy_base(struct fv_network_base *nw)
{
        fv_buffer_destroy(&nw->players);
        fv_buffer_destroy(&nw->dirty_players);
}
