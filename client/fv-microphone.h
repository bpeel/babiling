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

#ifndef FV_MICROPHONE_H
#define FV_MICROPHONE_H

#include <stdint.h>
#include <stdlib.h>

/* Callback invoked whenever some data is received from the
 * microphone. This may be called from another thread.
 */
typedef void
(* fv_microphone_callback)(const int16_t *data,
                           int n_samples,
                           void *user_data);

struct fv_microphone *
fv_microphone_new(fv_microphone_callback callback,
                  void *user_data);

void
fv_microphone_free(struct fv_microphone *mic);

#endif /* FV_MICROPHONE_H */
