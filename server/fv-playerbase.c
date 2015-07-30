/*
 * Babiling
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
#include "fv-main-context.h"

/* Number of microseconds of inactivity before a player will be
 * considered for garbage collection.
 */
#define FV_PLAYERBASE_MAX_PLAYER_AGE ((uint64_t) 2 * 60 * 1000000)

struct fv_playerbase {
        int n_players;

        struct fv_buffer players;

        struct fv_signal dirty_signal;

        struct fv_main_context_source *gc_source;
};

static void
remove_player(struct fv_playerbase *playerbase,
              struct fv_player *player)
{
        size_t length = fv_pointer_array_length(&playerbase->players);
        struct fv_player *last_player;
        struct fv_playerbase_dirty_event event;

        last_player = fv_pointer_array_get(&playerbase->players, length - 1);
        fv_pointer_array_set_length(&playerbase->players, --length);

        /* Move the last player into the position that the removed
         * player had so that we don't have to reorder any of the
         * other players
         */
        if (player->num < length) {
                fv_pointer_array_set(&playerbase->players,
                                     player->num,
                                     last_player);
                last_player->num = player->num;

                event.player = last_player;
                event.dirty_state = FV_PLAYER_STATE_ALL;
        } else {
                event.player = NULL;
                event.dirty_state = 0;
        }

        fv_player_free(player);

        event.playerbase = playerbase;
        event.n_players_changed = true;

        fv_signal_emit(&playerbase->dirty_signal, &event);
}

static void
gc_cb(struct fv_main_context_source *source,
      void *user_data)
{
        struct fv_playerbase *playerbase = user_data;
        uint64_t now = fv_main_context_get_monotonic_clock(NULL);
        int i;

        for (i = 0; i < fv_pointer_array_length(&playerbase->players); i++) {
                struct fv_player *player =
                        fv_pointer_array_get(&playerbase->players, i);

                if (player->ref_count == 0 &&
                    now - player->last_update_time >=
                    FV_PLAYERBASE_MAX_PLAYER_AGE) {
                        remove_player(playerbase, player);
                        i--;
                }
        }
}

struct fv_playerbase *
fv_playerbase_new(void)
{
        struct fv_playerbase *playerbase = fv_alloc(sizeof *playerbase);

        fv_buffer_init(&playerbase->players);
        fv_signal_init(&playerbase->dirty_signal);
        playerbase->n_players = 0;

        playerbase->gc_source = fv_main_context_add_timer(NULL,
                                                          1, /* minutes */
                                                          gc_cb,
                                                          playerbase);

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

        fv_main_context_remove_source(playerbase->gc_source);

        fv_free(playerbase);
}
