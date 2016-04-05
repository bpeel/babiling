/*
 * Babiling
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
        struct fv_audio_buffer *audio_buffer;
        struct fv_recorder *recorder;

        bool sent_hello;
        bool has_player_id;
        uint64_t player_id;

        enum fv_person_state dirty_player_state;
        struct fv_person player;

        /* Array of fv_persons */
        struct fv_buffer players;
        /* Array of FV_NETWORK_DIRTY_PLAYER_BITS bits for each player
         * to mark which state has changed since the last consistent
         * state was reached
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
write_speech(struct fv_network *nw);

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

        if (base->dirty_player_state)
                return true;

        if (fv_recorder_has_packet(base->recorder))
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
write_position(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        int res;

        res = write_command(nw,
                            FV_PROTO_UPDATE_POSITION,

                            FV_PROTO_TYPE_UINT32,
                            base->player.pos.x,

                            FV_PROTO_TYPE_UINT32,
                            base->player.pos.y,

                            FV_PROTO_TYPE_UINT16,
                            base->player.pos.direction,

                            FV_PROTO_TYPE_NONE);

        if (res != -1) {
                base->dirty_player_state &= ~FV_PERSON_STATE_POSITION;
                return true;
        } else {
                return false;
        }
}

static bool
write_appearance(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        int res;

        res = write_command(nw,
                            FV_PROTO_UPDATE_APPEARANCE,

                            FV_PROTO_TYPE_UINT8,
                            base->player.appearance.image,

                            FV_PROTO_TYPE_NONE);

        if (res != -1) {
                base->dirty_player_state &= ~FV_PERSON_STATE_APPEARANCE;
                return true;
        } else {
                return false;
        }
}

static bool
write_flags(struct fv_network *nw)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        int res;

        res = write_command(nw,
                            FV_PROTO_UPDATE_FLAGS,

                            FV_PROTO_TYPE_FLAGS,
                            base->player.flags.n_flags,
                            base->player.flags.flags,

                            FV_PROTO_TYPE_NONE);

        if (res != -1) {
                base->dirty_player_state &= ~FV_PERSON_STATE_FLAGS;
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

        if ((base->dirty_player_state & FV_PERSON_STATE_APPEARANCE)) {
                if (!write_appearance(nw))
                        return;
        }

        if ((base->dirty_player_state & FV_PERSON_STATE_POSITION)) {
                if (!write_position(nw))
                        return;
        }

        if ((base->dirty_player_state & FV_PERSON_STATE_FLAGS)) {
                if (!write_flags(nw))
                        return;
        }

        while (fv_recorder_has_packet(base->recorder)) {
                if (!write_speech(nw))
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

static void
dirty_player_state(struct fv_network_base *base,
                   int player_num,
                   enum fv_person_state state)
{
        int bit;

        while (state) {
                bit = fv_util_ffs(state) - 1;
                fv_bitmask_set(&base->dirty_players,
                               player_num * FV_NETWORK_DIRTY_PLAYER_BITS + bit,
                               true);
                state &= ~(1 << bit);
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
        fv_bitmask_set_length(&base->dirty_players,
                              n_players * FV_NETWORK_DIRTY_PLAYER_BITS);

        return true;
}

static bool
handle_player_position(struct fv_network *nw,
                       const uint8_t *payload,
                       size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        struct fv_person *person;
        struct fv_person_position position;
        uint16_t player_num;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_UINT16, &player_num,
                                   FV_PROTO_TYPE_UINT32, &position.x,
                                   FV_PROTO_TYPE_UINT32, &position.y,
                                   FV_PROTO_TYPE_UINT16, &position.direction,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        if (player_num < FV_NETWORK_N_PLAYERS(nw)) {
                person = (struct fv_person *) base->players.data + player_num;
                person->pos = position;
                dirty_player_state(base, player_num, FV_PERSON_STATE_POSITION);
        }

        return true;
}

static bool
handle_player_appearance(struct fv_network *nw,
                         const uint8_t *payload,
                         size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        struct fv_person *person;
        struct fv_person_appearance appearance;
        uint16_t player_num;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_UINT16, &player_num,
                                   FV_PROTO_TYPE_UINT8, &appearance.image,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        if (player_num < FV_NETWORK_N_PLAYERS(nw)) {
                person = (struct fv_person *) base->players.data + player_num;
                person->appearance = appearance;
                dirty_player_state(base,
                                   player_num,
                                   FV_PERSON_STATE_APPEARANCE);
        }

        return true;
}

static bool
handle_player_flags(struct fv_network *nw,
                    const uint8_t *payload,
                    size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        struct fv_person *person;
        struct fv_person_flags flags;
        uint16_t player_num;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_UINT16, &player_num,
                                   FV_PROTO_TYPE_FLAGS,
                                   &flags.n_flags,
                                   flags.flags,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        if (player_num < FV_NETWORK_N_PLAYERS(nw)) {
                person = (struct fv_person *) base->players.data + player_num;
                person->flags.n_flags = flags.n_flags;
                memcpy(person->flags.flags,
                       flags.flags,
                       (sizeof flags.flags[0]) * person->flags.n_flags);
                dirty_player_state(base, player_num, FV_PERSON_STATE_FLAGS);
        }

        return true;
}

static bool
handle_player_speech(struct fv_network *nw,
                     const uint8_t *payload,
                     size_t payload_length)
{
        struct fv_network_base *base = fv_network_get_base(nw);
        const uint8_t *packet;
        size_t packet_size;
        uint16_t player_num;

        if (!fv_proto_read_payload(payload,
                                   payload_length,
                                   FV_PROTO_TYPE_UINT16, &player_num,
                                   FV_PROTO_TYPE_BLOB,
                                   &packet_size,
                                   &packet,
                                   FV_PROTO_TYPE_NONE)) {
                set_socket_error(nw);
                return false;
        }

        fv_audio_buffer_add_packet(base->audio_buffer,
                                   player_num,
                                   packet,
                                   packet_size);

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

        case FV_PROTO_PLAYER_APPEARANCE:
                return handle_player_appearance(nw,
                                                message_payload,
                                                message_payload_length);

        case FV_PROTO_PLAYER_FLAGS:
                return handle_player_flags(nw,
                                           message_payload,
                                           message_payload_length);

        case FV_PROTO_PLAYER_SPEECH:
                return handle_player_speech(nw,
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
        base->dirty_player_state = (FV_PERSON_STATE_POSITION |
                                    FV_PERSON_STATE_APPEARANCE);
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
