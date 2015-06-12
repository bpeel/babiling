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

#ifndef FV_PLAYERBASE_H
#define FV_PLAYERBASE_H

#include <stdint.h>

#include "fv-player.h"
#include "fv-signal.h"

struct fv_playerbase_dirty_event {
        struct fv_playerbase *playerbase;

        int dirty_state;
        struct fv_player *player;

        bool n_players_changed;
};

struct fv_playerbase *
fv_playerbase_new(void);

struct fv_player *
fv_playerbase_get_player_by_id(struct fv_playerbase *playerbase,
                               uint64_t id);

struct fv_player *
fv_playerbase_get_player_by_num(struct fv_playerbase *playerbase,
                                int num);

struct fv_player *
fv_playerbase_add_player(struct fv_playerbase *playerbase,
                         uint64_t id);

int
fv_playerbase_get_n_players(struct fv_playerbase *playerbase);

struct fv_signal *
fv_playerbase_get_dirty_signal(struct fv_playerbase *playerbase);

void
fv_playerbase_free(struct fv_playerbase *playerbase);

#endif /* FV_PLAYERBASE_H */
