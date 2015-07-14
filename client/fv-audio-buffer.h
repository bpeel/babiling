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

#ifndef FV_AUDIO_BUFFER_H
#define FV_AUDIO_BUFFER_H

#include <stdint.h>
#include <stdlib.h>

struct fv_audio_buffer *
fv_audio_buffer_new(int rate);

/* Thread safe */
void
fv_audio_buffer_add_packet(struct fv_audio_buffer *ab,
                           int channel,
                           const uint8_t *packet_data,
                           size_t packet_length);

/* Thread safe */
void
fv_audio_buffer_get(struct fv_audio_buffer *ab,
                    int16_t *data,
                    size_t data_length);

void
fv_audio_buffer_free(struct fv_audio_buffer *ab);

#endif /* FV_AUDIO_BUFFER_H */
