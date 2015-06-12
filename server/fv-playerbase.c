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

#include "config.h"

#include "fv-playerbase.h"
#include "fv-pointer-array.h"
#include "fv-util.h"

struct fv_playerbase {
        int n_players;

        struct fv_buffer players;

        struct fv_signal dirty_signal;
};

struct fv_playerbase *
fv_playerbase_new(void)
{
        struct fv_playerbase *playerbase = fv_alloc(sizeof *playerbase);

        fv_buffer_init(&playerbase->players);
        fv_signal_init(&playerbase->dirty_signal);
        playerbase->n_players = 0;

        return playerbase;
}

struct fv_player *
fv_playerbase_get_player_by_id(struct fv_playerbase *playerbase,
                               uint64_t id)
{
        struct fv_player *player;
        int i;

        /* FIXME: This should probably use a hash table or something */

        for (i = 0; i < fv_pointer_array_length(&playerbase->players); i++) {
                player = fv_pointer_array_get(&playerbase->players, i);

                if (player->id == id)
                        return player;
        }

        return NULL;
}

struct fv_player *
fv_playerbase_get_player_by_num(struct fv_playerbase *playerbase,
                                int num)
{
        return fv_pointer_array_get(&playerbase->players, num);
}

int
fv_playerbase_get_n_players(struct fv_playerbase *playerbase)
{
        return fv_pointer_array_length(&playerbase->players);
}

struct fv_player *
fv_playerbase_add_player(struct fv_playerbase *playerbase,
                         uint64_t id)
{
        struct fv_player *player = fv_player_new(id);

        player->num = fv_pointer_array_length(&playerbase->players);
        fv_pointer_array_append(&playerbase->players, player);

        return player;
}

struct fv_signal *
fv_playerbase_get_dirty_signal(struct fv_playerbase *playerbase)
{
        return &playerbase->dirty_signal;
}

void
fv_playerbase_free(struct fv_playerbase *playerbase)
{
        int i;

        for (i = 0; i < fv_pointer_array_length(&playerbase->players); i++)
                fv_player_free(fv_pointer_array_get(&playerbase->players, i));

        fv_buffer_destroy(&playerbase->players);

        fv_free(playerbase);
}
