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

#include <SDL.h>
#include <stdbool.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <string.h>
#include <assert.h>

#include "fv-network.h"
#include "fv-util.h"
#include "fv-proto.h"
#include "fv-buffer.h"
#include "fv-person.h"
#include "fv-bitmask.h"

struct fv_network {
        SDL_Thread *thread;
        SDL_mutex *mutex;
        int wakeup_pipe[2];
        int sock;

        /* This state is accessed globally and needs the mutex */

        bool quit;

        bool player_queued;
        struct fv_person queued_player;

        /* This state is only accessed by the network thread so it
         * doesn't need the mutex
         */

        fv_network_consistent_event_cb consistent_event_cb;
        void *user_data;

        bool connected;
        bool sent_hello;
        bool has_player_id;
        uint64_t player_id;

        bool player_dirty;
        struct fv_person player;

        uint8_t read_buf[1024];
        size_t read_buf_pos;

        uint8_t write_buf[1024];
        size_t write_buf_pos;

        /* Current number of milliseconds to wait before trying to
         * connect. Doubles after each unsucessful connection up to a
         * maximum
         */
        uint32_t connect_wait_time;
        /* The last time we tried to connect in SDL ticks
         */
        uint32_t last_connect_time;

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
};

#define FV_NETWORK_MAX_CONNECT_WAIT_TIME (15 * 1000)

#define FV_NETWORK_N_PLAYERS(nw)                                \
        ((nw)->players.length / sizeof (struct fv_person))

/* Time in milliseconds after which if no other data is sent the
 * client will send a KEEP_ALIVE message */
#define FV_NETWORK_KEEP_ALIVE_TIME (60 * 1000)

static void
set_connected(struct fv_network *nw)
{
        nw->connected = true;
        nw->connect_wait_time = 0;
}

static void
set_connect_error(struct fv_network *nw)
{
        if (nw->connect_wait_time == 0) {
                nw->connect_wait_time = 1000;
        } else {
                nw->connect_wait_time *= 2;
                if (nw->connect_wait_time > FV_NETWORK_MAX_CONNECT_WAIT_TIME) {
                        nw->connect_wait_time =
                                FV_NETWORK_MAX_CONNECT_WAIT_TIME;
                }
        }
}

static void
set_socket_error(struct fv_network *nw)
{
        fv_close(nw->sock);
        nw->sock = -1;

        if (!nw->connected)
                set_connect_error(nw);
}

static void
try_connect(struct fv_network *nw)
{
        struct sockaddr_in addr;
        int sock;
        int ret;
        int flags;

        nw->connected = false;
        nw->sent_hello = false;
        nw->player_dirty = true;
        nw->read_buf_pos = 0;
        nw->write_buf_pos = 0;
        nw->last_update_time = SDL_GetTicks();

        sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == -1)
                goto error;

        flags = fcntl(sock, F_GETFL, 0);
        if (flags == -1 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1)
                goto error_socket;

        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(FV_PROTO_DEFAULT_PORT);

        ret = connect(sock, (struct sockaddr *) &addr, sizeof addr);
        if (ret == -1 && errno != EINPROGRESS)
                goto error_socket;

        nw->sock = sock;

        return;

error_socket:
        fv_close(sock);
error:
        set_connect_error(nw);
}

static bool
needs_write_poll(struct fv_network *nw)
{
        /* If we're not connected then we'll poll for writing so that
         * we can detect a successful connection.
         */
        if (!nw->connected)
                return true;

        if (!nw->sent_hello)
                return true;

        if (nw->player_dirty)
                return true;

        if (nw->write_buf_pos > 0)
                return true;

        if (nw->last_update_time + FV_NETWORK_KEEP_ALIVE_TIME <= SDL_GetTicks())
                return true;

        return false;
}

static bool
write_new_player(struct fv_network *nw)
{
        ssize_t res;

        res = fv_proto_write_command(nw->write_buf + nw->write_buf_pos,
                                     sizeof nw->write_buf - nw->write_buf_pos,
                                     FV_PROTO_NEW_PLAYER,
                                     FV_PROTO_TYPE_NONE);

        if (res != -1) {
                nw->sent_hello = true;
                nw->write_buf_pos += res;
                return true;
        } else {
                return false;
        }
}

static bool
write_reconnect(struct fv_network *nw)
{
        ssize_t res;

        res = fv_proto_write_command(nw->write_buf + nw->write_buf_pos,
                                     sizeof nw->write_buf - nw->write_buf_pos,
                                     FV_PROTO_RECONNECT,
                                     FV_PROTO_TYPE_UINT64,
                                     nw->player_id,
                                     FV_PROTO_TYPE_NONE);

        if (res != -1) {
                nw->sent_hello = true;
                nw->write_buf_pos += res;
                return true;
        } else {
                return false;
        }
}

static bool
write_player(struct fv_network *nw)
{
        ssize_t res;

        res = fv_proto_write_command(nw->write_buf + nw->write_buf_pos,
                                     sizeof nw->write_buf - nw->write_buf_pos,
                                     FV_PROTO_UPDATE_POSITION,

                                     FV_PROTO_TYPE_UINT32,
                                     nw->player.x_position,

                                     FV_PROTO_TYPE_UINT32,
                                     nw->player.y_position,

                                     FV_PROTO_TYPE_UINT16,
                                     nw->player.direction,

                                     FV_PROTO_TYPE_NONE);

        if (res != -1) {
                nw->player_dirty = false;
                nw->write_buf_pos += res;
                return true;
        } else {
                return false;
        }
}

static bool
write_keep_alive(struct fv_network *nw)
{
        ssize_t res;

        res = fv_proto_write_command(nw->write_buf + nw->write_buf_pos,
                                     sizeof nw->write_buf - nw->write_buf_pos,
                                     FV_PROTO_KEEP_ALIVE,

                                     FV_PROTO_TYPE_NONE);

        /* This should always succeed because it'll only be attempted
         * if the write buffer is empty.
         */
        assert(res > 0);

        nw->write_buf_pos += res;

        return true;
}

static void
fill_write_buf(struct fv_network *nw)
{
        if (!nw->sent_hello) {
                if (nw->has_player_id) {
                        if (!write_reconnect(nw))
                                return;
                } else if (!write_new_player(nw))
                        return;
        }

        if (nw->player_dirty) {
                if (!write_player(nw))
                        return;
        }

        /* If nothing else writes and we haven't written for a while
         * then add a keep alive. This should be the last thing in
         * this function.
         */
        if (nw->write_buf_pos == 0 &&
            nw->last_update_time + FV_NETWORK_KEEP_ALIVE_TIME <=
            SDL_GetTicks()) {
                if (!write_keep_alive(nw))
                        return;
        }
}

static bool
handle_write(struct fv_network *nw)
{
        ssize_t wrote;

        fill_write_buf(nw);

        if (nw->write_buf_pos <= 0)
                return true;

        wrote = write(nw->sock, nw->write_buf, nw->write_buf_pos);

        if (wrote < 0) {
                set_socket_error(nw);
                return false;
        }

        nw->last_update_time = SDL_GetTicks();

        /* Move any unwritten data to the beginning of the buffer */
        memmove(nw->write_buf,
                nw->write_buf + wrote,
                nw->write_buf_pos - wrote);
        nw->write_buf_pos -= wrote;

        return true;
}

static bool
handle_player_id(struct fv_network *nw,
                 const uint8_t *message)
{
        uint64_t player_id;

        if (fv_proto_read_command(message,
                                  FV_PROTO_TYPE_UINT64, &player_id,
                                  FV_PROTO_TYPE_NONE) == -1) {
                set_socket_error(nw);
                return false;
        }

        nw->player_id = player_id;
        nw->has_player_id = true;

        return true;
}

static bool
handle_consistent(struct fv_network *nw,
                  const uint8_t *message)
{
        struct fv_network_consistent_event event;

        if (fv_proto_read_command(message,
                                  FV_PROTO_TYPE_NONE) == -1) {
                set_socket_error(nw);
                return false;
        }

        if (nw->consistent_event_cb) {
                event.n_players = FV_NETWORK_N_PLAYERS(nw);
                event.players = (const struct fv_person *) nw->players.data;
                event.dirty_players = &nw->dirty_players;

                nw->consistent_event_cb(&event, nw->user_data);
        }

        memset(nw->dirty_players.data, 0, nw->dirty_players.length);

        return true;
}

static bool
handle_n_players(struct fv_network *nw,
                 const uint8_t *message)
{
        uint16_t n_players;

        if (fv_proto_read_command(message,
                                  FV_PROTO_TYPE_UINT16, &n_players,
                                  FV_PROTO_TYPE_NONE) == -1) {
                set_socket_error(nw);
                return false;
        }

        fv_buffer_set_length(&nw->players,
                             sizeof (struct fv_person) * n_players);
        fv_bitmask_set_length(&nw->dirty_players, n_players);

        return true;
}

static bool
handle_player_position(struct fv_network *nw,
                       const uint8_t *message)
{
        struct fv_person player;
        uint16_t player_num;

        if (fv_proto_read_command(message,
                                  FV_PROTO_TYPE_UINT16, &player_num,
                                  FV_PROTO_TYPE_UINT32, &player.x_position,
                                  FV_PROTO_TYPE_UINT32, &player.y_position,
                                  FV_PROTO_TYPE_UINT16, &player.direction,
                                  FV_PROTO_TYPE_NONE) == -1) {
                set_socket_error(nw);
                return false;
        }

        if (player_num < FV_NETWORK_N_PLAYERS(nw)) {
                memcpy(nw->players.data +
                       player_num * sizeof (struct fv_person),
                       &player,
                       sizeof player);
                fv_bitmask_set(&nw->dirty_players, player_num, true);
        }

        return true;
}

static bool
handle_server_data(struct fv_network *nw)
{
        uint16_t payload_length;
        const uint8_t *message;
        size_t pos = 0;
        int got;

        got = read(nw->sock,
                   nw->read_buf + nw->read_buf_pos,
                   sizeof nw->read_buf - nw->read_buf_pos);

        if (got == -1 || got == 0) {
                set_socket_error(nw);
                return false;
        }

        nw->read_buf_pos += got;

        while (pos + FV_PROTO_HEADER_SIZE <= nw->read_buf_pos) {
                message = nw->read_buf + pos;
                payload_length = fv_proto_get_payload_length(message);

                if (payload_length + FV_PROTO_HEADER_SIZE >
                    sizeof nw->read_buf) {
                        /* The server has sent a message that we can't
                         * fit in the read buffer. There shouldn't be
                         * any messages this big so something has gone
                         * wrong.
                         */
                        set_socket_error(nw);
                        return false;
                }

                /* If we haven't got a complete message then stop processing */
                if (pos + payload_length + FV_PROTO_HEADER_SIZE >
                    sizeof nw->read_buf)
                        break;

                switch (fv_proto_get_message_id(message)) {
                case FV_PROTO_PLAYER_ID:
                        if (!handle_player_id(nw, message))
                                return false;
                        break;
                case FV_PROTO_CONSISTENT:
                        if (!handle_consistent(nw, message))
                                return false;
                        break;
                case FV_PROTO_N_PLAYERS:
                        if (!handle_n_players(nw, message))
                                return false;
                        break;
                case FV_PROTO_PLAYER_POSITION:
                        if (!handle_player_position(nw, message))
                                return false;
                        break;
                }

                pos += payload_length + FV_PROTO_HEADER_SIZE;
        }

        /* Move any remaining partial message to the beginning of the buffer */
        memmove(nw->read_buf, nw->read_buf + pos, nw->read_buf_pos - pos);
        nw->read_buf_pos -= pos;

        return true;
}

static void
set_timeout_for(int *timeout,
                uint32_t next_wakeup_time)
{
        uint32_t now = SDL_GetTicks();

        if (now >= next_wakeup_time)
                *timeout = 0;
        else
                *timeout = next_wakeup_time - now + 1;
}

static int
thread_func(void *user_data)
{
        struct fv_network *nw = user_data;
        bool quit;
        struct pollfd pollfd[2];
        nfds_t n_pollfds;
        char wakeup_buf[8];
        uint32_t now;
        int timeout;

        while (true) {
                SDL_LockMutex(nw->mutex);

                quit = nw->quit;

                if (nw->player_queued) {
                        nw->player_dirty = true;
                        nw->player_queued = false;
                        nw->player = nw->queued_player;
                }

                SDL_UnlockMutex(nw->mutex);

                if (quit)
                        break;

                pollfd[0].fd = nw->wakeup_pipe[0];
                pollfd[0].events = POLLIN | POLLHUP;
                pollfd[0].revents = 0;
                n_pollfds = 1;

                if (nw->sock != -1) {
                        pollfd[1].fd = nw->sock;
                        pollfd[1].events = POLLIN | POLLHUP;
                        if (needs_write_poll(nw))
                                pollfd[1].events |= POLLOUT;
                        pollfd[1].revents = 0;
                        n_pollfds++;
                }

                if (nw->sock == -1) {
                        set_timeout_for(&timeout,
                                        nw->last_connect_time +
                                        nw->connect_wait_time);
                } else if ((pollfd[1].events & POLLOUT) == 0) {
                        set_timeout_for(&timeout,
                                        FV_NETWORK_KEEP_ALIVE_TIME +
                                        nw->last_update_time);
                } else {
                        timeout = -1;
                }

                poll(pollfd, n_pollfds, timeout);

                if (pollfd[0].revents)
                        read(nw->wakeup_pipe[0], wakeup_buf, sizeof wakeup_buf);

                if (nw->sock == -1) {
                        now = SDL_GetTicks();
                        if (now - nw->last_connect_time >=
                            nw->connect_wait_time) {
                                nw->last_connect_time = now;
                                try_connect(nw);
                        }
                } else {
                        if ((pollfd[1].revents & (POLLOUT | POLLERR)) ==
                            POLLOUT)
                                set_connected(nw);

                        if ((pollfd[1].revents & POLLERR)) {
                                set_socket_error(nw);
                                continue;
                        }

                        if ((pollfd[1].revents & (POLLIN | POLLHUP))) {
                                if (!handle_server_data(nw))
                                        continue;
                        }

                        if ((pollfd[1].revents & POLLOUT)) {
                                if (!handle_write(nw))
                                        continue;
                        }
                }
        }

        return 0;
}

static void
fv_network_wakeup_thread(struct fv_network *nw)
{
        const char ch = 'w';
        int ret;

        do {
                ret = write(nw->wakeup_pipe[1], &ch, 1);
        } while (ret == -1 && errno == EINTR);
}

struct fv_network *
fv_network_new(fv_network_consistent_event_cb consistent_event_cb,
               void *user_data)
{
        struct fv_network *nw = fv_alloc(sizeof *nw);

        nw->consistent_event_cb = consistent_event_cb;
        nw->user_data = user_data;

        nw->sock = -1;
        nw->connected = false;
        nw->has_player_id = false;
        nw->last_connect_time = 0;
        nw->connect_wait_time = 0;
        nw->quit = false;
        nw->player_queued = false;

        fv_buffer_init(&nw->players);
        fv_buffer_init(&nw->dirty_players);

        if (pipe(nw->wakeup_pipe) == -1)
                fv_fatal("Error creating pipe: %s", strerror(errno));

        nw->mutex = SDL_CreateMutex();
        if (nw->mutex == NULL)
                fv_fatal("Error creating mutex: %s", SDL_GetError());

        nw->thread = SDL_CreateThread(thread_func,
                                      "Network",
                                      nw);
        if (nw->thread == NULL)
                fv_fatal("Error creating thread: %s", SDL_GetError());

        return nw;
}

void
fv_network_update_player(struct fv_network *nw,
                         const struct fv_person *player)
{
        SDL_LockMutex(nw->mutex);
        nw->queued_player = *player;
        nw->player_queued = true;
        fv_network_wakeup_thread(nw);
        SDL_UnlockMutex(nw->mutex);
}

void
fv_network_free(struct fv_network *nw)
{
        SDL_LockMutex(nw->mutex);
        nw->quit = true;
        SDL_UnlockMutex(nw->mutex);

        fv_network_wakeup_thread(nw);

        SDL_WaitThread(nw->thread, NULL /* status */);

        SDL_DestroyMutex(nw->mutex);

        fv_buffer_destroy(&nw->players);
        fv_buffer_destroy(&nw->dirty_players);

        fv_close(nw->wakeup_pipe[0]);
        fv_close(nw->wakeup_pipe[1]);

        if (nw->sock != -1)
                fv_close(nw->sock);

        fv_free(nw);
}
