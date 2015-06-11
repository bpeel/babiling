/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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

#ifndef FV_LOGIC_H
#define FV_LOGIC_H

#include <float.h>
#include <stdbool.h>
#include <math.h>

#include "fv-person.h"

enum fv_logic_state {
        FV_LOGIC_STATE_NO_PLAYERS,
        FV_LOGIC_STATE_RUNNING,
};

enum fv_logic_state_change {
        /* The player position or direction has changed. Ie, something
         * that should be sent over the network.
         */
        FV_LOGIC_STATE_CHANGE_PLAYER = 1 << 0,
        /* The player's center has changed. This only affects rendering. */
        FV_LOGIC_STATE_CHANGE_CENTER = 1 << 1,
        /* There is something happening that might cause another state
         * change even if this time it didn't.
         */
        FV_LOGIC_STATE_CHANGE_ALIVE = 1 << 2
};

#define FV_LOGIC_MAX_PLAYERS 4

struct fv_logic_person {
        float direction;
        float x, y;
        enum fv_person_type type;
};

typedef void
(* fv_logic_person_cb)(const struct fv_logic_person *person,
                       void *user_data);

struct fv_logic *
fv_logic_new(void);

void
fv_logic_reset(struct fv_logic *logic,
               int n_players);

/* Update the state according to the time passed in
 * milliseconds. Returns whether any of the players has changed state.
 */
enum fv_logic_state_change
fv_logic_update(struct fv_logic *logic,
                unsigned int progress);

void
fv_logic_get_player(struct fv_logic *logic,
                    int player_num,
                    struct fv_person *person);

void
fv_logic_get_center(struct fv_logic *logic,
                    int player_num,
                    float *x, float *y);

void
fv_logic_for_each_person(struct fv_logic *logic,
                         fv_logic_person_cb person_cb,
                         void *user_data);

/* The direction is given in radians where 0 is the positive x-axis
 * and the angle is measured counter-clockwise from that.
 */
void
fv_logic_set_direction(struct fv_logic *logic,
                       int player_num,
                       bool moving,
                       float direction);

void
fv_logic_set_n_npcs(struct fv_logic *logic,
                    int n_npcs);

void
fv_logic_update_npc(struct fv_logic *logic,
                    int npc_num,
                    const struct fv_person *person);

int
fv_logic_get_n_players(struct fv_logic *logic);

enum fv_logic_state
fv_logic_get_state(struct fv_logic *logic);

void
fv_logic_free(struct fv_logic *logic);

#endif /* FV_LOGIC_H */
