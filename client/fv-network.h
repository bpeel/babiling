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

#ifndef FV_NETWORK_H
#define FV_NETWORK_H

#include <stdint.h>

#include "fv-person.h"
#include "fv-buffer.h"

struct fv_network;

struct fv_network_consistent_event {
        int n_players;

        const struct fv_person *players;

        /* fv_bitmask with 1-bit for each player to mark whether it
         * has changed since the last consistent event
         */
        const struct fv_buffer *dirty_players;
};

/**
 * This will be invoked whenever the server tells us that enough state
 * messages have been received to describe a consistent state. Note
 * that this will be emitted asynchronously from a different thread.
 */
typedef void
(* fv_network_consistent_event_cb)(const struct fv_network_consistent_event *e,
                                   void *user_data);

struct fv_network *
fv_network_new(fv_network_consistent_event_cb consistent_event_cb,
               void *user_data);

void
fv_network_update_player(struct fv_network *nw,
                         const struct fv_person *player);

void
fv_network_free(struct fv_network *nw);

#endif /* FV_NETWORK_H */
