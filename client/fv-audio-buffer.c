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

#include <stdint.h>
#include <SDL.h>
#include <opus.h>

#include "fv-audio-buffer.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-mutex.h"
#include "fv-speech.h"

struct fv_audio_buffer {
        struct fv_mutex *mutex;

        struct fv_buffer channels;

        /* The mixed audio is stored uncompressed in a ring buffer */
        int16_t *buffer;
        int size;
        int start;
        int length;
};

struct fv_audio_buffer_channel {
        OpusDecoder *decoder;

        /* The offset along the ring buffer of where to start mixing
         * data for this channel.
         */
        int offset;
};

struct fv_audio_buffer *
fv_audio_buffer_new(void)
{
        struct fv_audio_buffer *ab = fv_alloc(sizeof *ab);

        ab->mutex = fv_mutex_new();

        ab->size = 512;
        ab->buffer = fv_alloc(ab->size * sizeof *ab->buffer);
        ab->start = 0;
        ab->length = 0;

        memset(ab->buffer, 0, ab->size * sizeof *ab->buffer);

        fv_buffer_init(&ab->channels);

        return ab;
}

static struct fv_audio_buffer_channel *
get_channel(struct fv_audio_buffer *ab,
            int channel_num)
{
        struct fv_audio_buffer_channel *channel;
        size_t min_length, old_length;
        int error;

        old_length = ab->channels.length;
        min_length = ((channel_num + 1) *
                      sizeof (struct fv_audio_buffer_channel));

        if (old_length < min_length) {
                fv_buffer_set_length(&ab->channels, min_length);
                memset(ab->channels.data + old_length,
                       0,
                       min_length - old_length);
        }

        channel = ((struct fv_audio_buffer_channel *) ab->channels.data +
                   channel_num);

        if (channel->decoder == NULL) {
                channel->decoder = opus_decoder_create(FV_SPEECH_SAMPLE_RATE,
                                                       1, /* channels */
                                                       &error);
                if (error != OPUS_OK)
                        return NULL;
        }

        return channel;
}

static void
reserve_buffer_space(struct fv_audio_buffer *ab,
                     int size)
{
        int16_t *buf;
        int new_size;
        int to_copy;

        if (size <= ab->size)
                return;

        for (new_size = ab->size; new_size < size; new_size *= 2);

        buf = fv_alloc(new_size * sizeof *buf);
        to_copy = MIN(ab->size - ab->start, ab->length);
        memcpy(buf, ab->buffer + ab->start, to_copy * sizeof *buf);
        memcpy(buf + to_copy,
               ab->buffer,
               (ab->length - to_copy) * sizeof *buf);
        memset(buf + ab->length,
               0,
               (new_size - ab->length) * sizeof *buf);

        fv_free(ab->buffer);
        ab->buffer = buf;
        ab->size = new_size;
        ab->start = 0;
}

void
fv_audio_buffer_add_packet(struct fv_audio_buffer *ab,
                           int channel_num,
                           const uint8_t *packet_data,
                           size_t packet_length)
{
        struct fv_audio_buffer_channel *channel;
        int16_t *buf;
        int n_samples;
        int to_copy;
        int start;

        fv_mutex_lock(ab->mutex);

        channel = get_channel(ab, channel_num);

        if (channel == NULL)
                goto done;

        n_samples = opus_packet_get_nb_samples(packet_data,
                                               packet_length,
                                               FV_SPEECH_SAMPLE_RATE);

        /* Ignore invalid packets */
        if (n_samples < 0)
                goto done;

        buf = alloca(sizeof *buf * n_samples);

        n_samples = opus_decode(channel->decoder,
                                packet_data,
                                packet_length,
                                buf,
                                n_samples,
                                false /* decode_fec */);

        if (n_samples < 0)
                goto done;

        reserve_buffer_space(ab, channel->offset + n_samples);

        start = (channel->offset + ab->start) & (ab->size - 1);
        to_copy = MIN(ab->size - start, n_samples);

        SDL_MixAudioFormat((Uint8 *) (ab->buffer + start),
                           (Uint8 *) buf,
                           AUDIO_S16SYS,
                           to_copy * sizeof *buf,
                           SDL_MIX_MAXVOLUME);

        SDL_MixAudioFormat((Uint8 *) ab->buffer,
                           (Uint8 *) (buf + to_copy),
                           AUDIO_S16SYS,
                           sizeof *buf * (n_samples - to_copy),
                           SDL_MIX_MAXVOLUME);

        channel->offset += n_samples;
        ab->length = MAX(ab->length, channel->offset);

done:
        fv_mutex_unlock(ab->mutex);
}

void
fv_audio_buffer_get(struct fv_audio_buffer *ab,
                    int16_t *data,
                    size_t data_length)
{
        struct fv_audio_buffer_channel *channels;
        int n_channels;
        int from_buffer, to_copy;
        int i;

        fv_mutex_lock(ab->mutex);

        from_buffer = MIN(data_length, ab->length);

        to_copy = MIN(from_buffer, ab->size - ab->start);
        memcpy(data, ab->buffer + ab->start, to_copy * sizeof *data);
        memset(ab->buffer + ab->start, 0, to_copy * sizeof *data);
        data += to_copy;

        to_copy = from_buffer - to_copy;
        memcpy(data, ab->buffer, to_copy * sizeof *data);
        memset(ab->buffer, 0, to_copy * sizeof *data);
        data += to_copy;

        memset(data, 0, (data_length - from_buffer) * sizeof (int16_t));

        ab->start = (ab->start + from_buffer) & (ab->size - 1);
        ab->length -= from_buffer;

        channels = (struct fv_audio_buffer_channel *) ab->channels.data;
        n_channels = (ab->channels.length /
                      sizeof (struct fv_audio_buffer_channel));

        for (i = 0; i < n_channels; i++) {
                if (channels[i].offset < from_buffer)
                        channels[i].offset = 0;
                else
                        channels[i].offset -= from_buffer;
        }

        fv_mutex_unlock(ab->mutex);
}

void
fv_audio_buffer_free(struct fv_audio_buffer *ab)
{
        struct fv_audio_buffer_channel *channels;
        int n_channels;
        int i;

        channels = (struct fv_audio_buffer_channel *) ab->channels.data;
        n_channels = (ab->channels.length /
                      sizeof (struct fv_audio_buffer_channel));

        for (i = 0; i < n_channels; i++) {
                if (channels[i].decoder)
                        opus_decoder_destroy(channels[i].decoder);
        }

        fv_buffer_destroy(&ab->channels);

        fv_mutex_free(ab->mutex);
}
