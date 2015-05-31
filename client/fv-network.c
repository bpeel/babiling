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

#include "fv-network.h"
#include "fv-util.h"
#include "fv-proto.h"

struct fv_network {
        SDL_Thread *thread;
        SDL_mutex *mutex;
        int wakeup_pipe[2];
        int sock;
        bool connected;
        bool quit;

        /* Current number of milliseconds to wait before trying to
         * connect. Doubles after each unsucessful connection up to a
         * maximum
         */
        uint32_t connect_wait_time;
        /* The last time we tried to connect in SDL ticks
         */
        uint32_t last_connect_time;
};

#define FV_NETWORK_MAX_CONNECT_WAIT_TIME (15 * 1000)

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
        if (ret == -1)
                goto error_socket;

        nw->sock = sock;

        return;

error_socket:
        fv_close(sock);
error:
        set_connect_error(nw);
}

static void
handle_server_data(struct fv_network *nw)
{
        char buf[1024];
        int got;

        got = read(nw->sock, buf, sizeof buf);

        if (got == -1 || got == 0)
                set_socket_error(nw);
}

static int
thread_func(void *user_data)
{
        struct fv_network *nw = user_data;
        bool quit;
        struct pollfd pollfd[2];
        nfds_t n_pollfds;
        char wakeup_buf[8];
        uint32_t now, next_connect_time;
        int timeout;

        while (true) {
                SDL_LockMutex(nw->mutex);
                quit = nw->quit;
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
                        if (!nw->connected)
                                pollfd[1].revents |= POLLOUT;
                        pollfd[1].revents = 0;
                        n_pollfds++;
                }

                if (nw->sock == -1) {
                        now = SDL_GetTicks();
                        next_connect_time = (nw->last_connect_time +
                                             nw->connect_wait_time);
                        if (now >= next_connect_time)
                                timeout = 0;
                        else
                                timeout = next_connect_time - now + 1;
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
                        if ((pollfd[1].revents & POLLOUT))
                                set_connected(nw);

                        if ((pollfd[1].revents & POLLERR))
                                set_socket_error(nw);
                        else if ((pollfd[1].revents & (POLLIN | POLLHUP)))
                                handle_server_data(nw);
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
fv_network_new(void)
{
        struct fv_network *nw = fv_alloc(sizeof *nw);

        nw->sock = -1;
        nw->connected = false;
        nw->last_connect_time = 0;
        nw->connect_wait_time = 0;
        nw->quit = false;

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
fv_network_free(struct fv_network *nw)
{
        SDL_LockMutex(nw->mutex);
        nw->quit = true;
        SDL_UnlockMutex(nw->mutex);

        fv_network_wakeup_thread(nw);

        SDL_WaitThread(nw->thread, NULL /* status */);

        SDL_DestroyMutex(nw->mutex);

        fv_close(nw->wakeup_pipe[0]);
        fv_close(nw->wakeup_pipe[1]);

        if (nw->sock != -1)
                fv_close(nw->sock);

        fv_free(nw);
}
