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

#include <stdbool.h>
#include <SDL.h>
#include <assert.h>
#include <emscripten.h>
#include <stdarg.h>

#include "fv-network.h"
#include "fv-util.h"
#include "fv-proto.h"
#include "fv-buffer.h"
#include "fv-person.h"
#include "fv-bitmask.h"
#include "fv-recorder.h"
#include "fv-error-message.h"

#include "fv-network-common.h"

struct fv_network {
        struct fv_network_base base;

        bool has_socket;
        bool connected;

        /* Current number of milliseconds to wait before trying to
         * connect. Doubles after each unsucessful connection up to a
         * maximum
         */
        uint32_t connect_wait_time;
        /* The last time we tried to connect in SDL ticks
         */
        uint32_t last_connect_time;

        /* This will be -1 if there is no timeout queued. Initially
         * the timeout period will be 0 when writing is first needed
         * in order to make it write straight away. If some writing is
         * done but the buffer becomes full this will be added with a
         * larger timeout so that it will wait for a while before
         * trying again. Otherwise if no other data currently needs to
         * be written then a larger timeout will be installed in order
         * to send a keep alive message.
         */
        int write_timeout_id;
        bool write_timeout_is_keep_alive;

        /* Similar timeout for connecting */
        int connect_timeout_id;

        uint8_t buf[FV_PROTO_MAX_FRAME_HEADER_LENGTH +
                    FV_PROTO_MAX_MESSAGE_SIZE];
};

void EMSCRIPTEN_KEEPALIVE
fv_network_open_cb(struct fv_network *nw);

void EMSCRIPTEN_KEEPALIVE
fv_network_message_cb(struct fv_network *nw,
                      size_t length);

void EMSCRIPTEN_KEEPALIVE
fv_network_error_cb(struct fv_network *nw);

void EMSCRIPTEN_KEEPALIVE
fv_network_connect_timeout_cb(struct fv_network *nw);

void EMSCRIPTEN_KEEPALIVE
fv_network_write_timeout_cb(struct fv_network *nw);

static struct fv_network_base *
fv_network_get_base(struct fv_network *nw)
{
        return &nw->base;
}

static void
cancel_connect_timeout(struct fv_network *nw)
{
        if (nw->connect_timeout_id == -1)
                return;

        EM_ASM_({window.clearTimeout($0);},
                nw->connect_timeout_id);
        nw->connect_timeout_id = -1;
}

static void
update_connect_timeout(struct fv_network *nw)
{
        uint32_t now;
        uint32_t delay;

        if (nw->connect_timeout_id != -1)
                return;

        now = SDL_GetTicks();
        if (nw->last_connect_time + nw->connect_wait_time >= now)
                delay = 0;
        else
                delay = nw->last_connect_time + nw->connect_wait_time - now;

        nw->connect_timeout_id = EM_ASM_INT({
                        return window.setTimeout(_fv_network_connect_timeout_cb,
                                                 $1,
                                                 $0);
                }, nw, delay);
}

static void
cancel_write_timeout(struct fv_network *nw)
{
        if (nw->write_timeout_id == -1)
                return;

        EM_ASM_({window.clearTimeout($0);},
                nw->write_timeout_id);
        nw->write_timeout_id = -1;
}

static void
install_write_timeout(struct fv_network *nw,
                      int delay,
                      bool is_keep_alive)
{
        nw->write_timeout_id = EM_ASM_INT({
                        return window.setTimeout(_fv_network_write_timeout_cb,
                                                 $1,
                                                 $0);
                }, nw, delay);
        nw->write_timeout_is_keep_alive = is_keep_alive;
}

static bool
needs_write_poll(struct fv_network *nw)
{
        if (!nw->connected)
                return false;

        return needs_write_poll_base(nw);
}

static void
update_write_timeout(struct fv_network *nw)
{
        if (needs_write_poll(nw)) {
                if (nw->write_timeout_id == -1 ||
                    nw->write_timeout_is_keep_alive) {
                        cancel_write_timeout(nw);
                        install_write_timeout(nw,
                                              0, /* delay */
                                              false /* is_keep_alive */);
                }
        } else if (!nw->connected) {
                cancel_write_timeout(nw);
        } else if (nw->write_timeout_id == -1 ||
                   !nw->write_timeout_is_keep_alive) {
                cancel_write_timeout(nw);
                install_write_timeout(nw,
                                      nw->base.last_update_time +
                                      FV_NETWORK_KEEP_ALIVE_TIME -
                                      SDL_GetTicks() +
                                      1,
                                      true /* is_keep_alive */);
        }
}

static void
set_connect_error(struct fv_network *nw)
{
        nw->connect_wait_time *= 2;

        if (nw->connect_wait_time > FV_NETWORK_MAX_CONNECT_WAIT_TIME)
                nw->connect_wait_time = FV_NETWORK_MAX_CONNECT_WAIT_TIME;
}

static void
close_socket(struct fv_network *nw)
{
        EM_ASM({
                        Module.fv_socket.onopen = null;
                        Module.fv_socket.onmessage = null;
                        Module.fv_socket.onclose = null;
                        Module.fv_socket.onerror = null;

                        Module.fv_socket.close();
                });

        nw->has_socket = false;
}

static void
set_socket_error(struct fv_network *nw)
{
        close_socket(nw);

        cancel_write_timeout(nw);

        if (!nw->connected)
                set_connect_error(nw);

        update_connect_timeout(nw);
}

void EMSCRIPTEN_KEEPALIVE
fv_network_open_cb(struct fv_network *nw)
{
        nw->connected = true;
        nw->connect_wait_time = FV_NETWORK_MIN_CONNECT_WAIT_TIME;

        update_write_timeout(nw);
}

void EMSCRIPTEN_KEEPALIVE
fv_network_message_cb(struct fv_network *nw,
                      size_t length)
{
        if (length <= 0)
                return;

        handle_message(nw,
                       nw->buf[0],
                       nw->buf + 1,
                       length - 1);
}

void EMSCRIPTEN_KEEPALIVE
fv_network_error_cb(struct fv_network *nw)
{
        set_socket_error(nw);
}

void EMSCRIPTEN_KEEPALIVE
fv_network_connect_timeout_cb(struct fv_network *nw)
{
        bool res;

        nw->connect_timeout_id = -1;

        init_new_connection(nw);

        nw->has_socket = false;
        nw->connected = false;

        res = EM_ASM_INT({
                        var connect_res = false;
                        var hostname = window.location.hostname;
                        try {
                                Module.fv_socket =
                                        new WebSocket("ws://" + hostname +
                                                      ":" + $0 +
                                                      "/finvenkisto");
                                connect_res = true;
                        } catch (e) {
                        }
                        return connect_res ? 1 : 0;
                }, FV_PROTO_DEFAULT_PORT);

        if (!res) {
                set_connect_error(nw);
                update_connect_timeout(nw);
                return;
        }

        EM_ASM_({
                        var nw = $0;
                        var buf_offset = $1;
                        var max_size = $2;

                        Module.fv_socket.binaryType = "arraybuffer";

                        function fv_onopen(e) {
                                _fv_network_open_cb(nw);
                        };
                        function fv_onmessage(e) {
                                if (e.data.byteLength > max_size) {
                                        console.log("Server sent a message " +
                                                    "that is too long");
                                        _fv_network_error_cb(nw);
                                } else {
                                        var ba = new Uint8Array(e.data);
                                        HEAPU8.set(ba, buf_offset);
                                        _fv_network_message_cb(nw, ba.length);
                                }
                        };
                        function fv_onclose(e) {
                                _fv_network_error_cb(nw);
                        }
                        function fv_onerror(e) {
                                _fv_network_error_cb(nw);
                        }
                        Module.fv_socket.onopen = fv_onopen;
                        Module.fv_socket.onmessage = fv_onmessage;
                        Module.fv_socket.onclose = fv_onclose;
                        Module.fv_socket.onerror = fv_onerror;
                }, nw, &nw->buf, sizeof nw->buf);

        nw->has_socket = true;
}

static bool
write_buffer_full(struct fv_network *nw)
{
        int buffered_amount = EM_ASM_INT_V({
                        return Module.fv_socket.bufferedAmount;
                });

        /* We don't know what size buffer the browser actually has.
         * According to the spec if we go over this then the socket
         * will be closed. This just uses a number out of a hat that
         * might work.
         */
        return buffered_amount >= 800;
}

static void
send_buf(struct fv_network *nw,
         uint8_t *buf,
         size_t length)
{
        EM_ASM_({
                        var buf = HEAPU8.subarray($0, $0 + $1);
                        Module.fv_socket.send(buf);
                }, buf, length);

        nw->base.last_update_time = SDL_GetTicks();
}

static int
write_command(struct fv_network *nw,
              uint8_t command,
              ...)
{
        va_list ap;
        int res;

        if (write_buffer_full(nw))
                return -1;

        va_start(ap, command);

        res = fv_proto_write_command_v(nw->buf,
                                       sizeof nw->buf,
                                       command,
                                       ap);

        va_end(ap);

        assert(res != -1);

        send_buf(nw, nw->buf + 2, res - 2);

        return res;
}

static bool
write_speech(struct fv_network *nw)
{
        uint8_t buf[1 + FV_PROTO_MAX_SPEECH_SIZE];
        int packet_size;

        if (write_buffer_full(nw))
                return false;

        packet_size = fv_recorder_get_packet(nw->base.recorder,
                                             buf + 1,
                                             FV_PROTO_MAX_SPEECH_SIZE);

        assert(packet_size != -1);

        buf[0] = FV_PROTO_SPEECH;

        send_buf(nw, buf, packet_size + 1);

        return true;
}

static bool
write_buf_is_empty(struct fv_network *nw)
{
        int buffered_amount = EM_ASM_INT_V({
                        return Module.fv_socket.bufferedAmount;
                });
        return buffered_amount == 0;
}

void EMSCRIPTEN_KEEPALIVE
fv_network_write_timeout_cb(struct fv_network *nw)
{
        nw->write_timeout_id = -1;

        fill_write_buf(nw);

        /* If we still need to write more then the buffer must have
         * been too full so we'll requeue the timeout in order to try
         * again after a short delay.
         */
        if (needs_write_poll(nw)) {
                install_write_timeout(nw,
                                      17, /* delay */
                                      false /* is_keep_alive */);
        } else {
                update_write_timeout(nw);
        }
}

struct fv_network *
fv_network_new(fv_network_consistent_event_cb consistent_event_cb,
               void *user_data)
{
        struct fv_network *nw = fv_alloc(sizeof *nw);

        nw->base.consistent_event_cb = consistent_event_cb;
        nw->base.user_data = user_data;

        nw->has_socket = false;
        nw->connected = false;
        nw->write_timeout_id = -1;
        nw->connect_timeout_id = -1;

        nw->last_connect_time = 0;
        nw->connect_wait_time = FV_NETWORK_MIN_CONNECT_WAIT_TIME;

        init_base(&nw->base);

        update_connect_timeout(nw);

        return nw;
}

void
fv_network_update_player(struct fv_network *nw,
                         const struct fv_person *player)
{
        nw->base.player_dirty = true;
        nw->base.player = *player;

        update_write_timeout(nw);
}

void
fv_network_add_host(struct fv_network *nw,
                    const char *name)
{
        /* The hosts are ignored and the client always connects to the
         * same host that it was served from.
         */
}

void
fv_network_free(struct fv_network *nw)
{
        destroy_base(&nw->base);

        cancel_connect_timeout(nw);
        cancel_write_timeout(nw);

        if (nw->has_socket)
                close_socket(nw);

        fv_free(nw);
}
