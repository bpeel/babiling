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

#ifndef FV_RECORDER_H
#define FV_RECORDER_H

#include <stdint.h>
#include <stdlib.h>
#include <stdbool.h>

/* Callback invoked whenever there is a new packet ready to be sent.
 * This may be called from another thread.
 */
typedef void
(* fv_recorder_callback)(void *user_data);

struct fv_recorder *
fv_recorder_new(fv_recorder_callback callback,
                void *user_data);

bool
fv_recorder_has_packet(struct fv_recorder *recorder);

int
fv_recorder_get_packet(struct fv_recorder *recorder,
                       uint8_t *buffer,
                       size_t buffer_size);

void
fv_recorder_free(struct fv_recorder *recorder);

#endif /* FV_RECORDER_H */
