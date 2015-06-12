/*
 * Finvenkisto
 * Copyright (C) 2015  Neil Roberts
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

#ifndef FV_PLAYER_H
#define FV_PLAYER_H

#include <stdint.h>

struct fv_player {
        /* This is the randomly generated globally unique ID for the
         * player that is used like a password for the clients.
         */
        uint64_t id;

        /* This is simply the player's position in the list.
         */
        int num;

        /* The number of connections listening to this player. The
         * player is a candidate for garbage collection if this
         * reaches zero.
         */
        int ref_count;

        /* FV_PLAYER_STATE_POSITION */
        uint32_t x_position, y_position;
        uint16_t direction;

        /* The last time a connection that is using this player sent
         * some data. If this gets too old it will be a candidate for
         * garbage collection.
         */
        uint64_t last_update_time;
};

#define FV_PLAYER_STATE_POSITION (1 << 0)
#define FV_PLAYER_STATE_ALL ((1 << 1) - 1)

struct fv_player *
fv_player_new(uint64_t id);

void
fv_player_free(struct fv_player *player);

#endif /* FV_PLAYER_H */
