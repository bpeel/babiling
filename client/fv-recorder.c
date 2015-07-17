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

#include <SDL.h>
#include <opus.h>
#include <stdlib.h>
#include <assert.h>

#include "fv-recorder.h"
#include "fv-microphone.h"
#include "fv-util.h"
#include "fv-proto.h"
#include "fv-error-message.h"
#include "fv-mutex.h"
#include "fv-speech.h"

#define FV_RECORDER_SAMPLES_PER_PACKET (FV_SPEECH_SAMPLE_RATE * \
                                        FV_PROTO_SPEECH_TIME /  \
                                        1000)
/* After receiving one second's worth of silence it will stop
 * recording.
 */
#define FV_RECORDER_MAX_SILENT_PACKETS (1000 / FV_PROTO_SPEECH_TIME)
#define FV_RECORDER_SILENCE_THRESHOLD 1024

/* The packets aren't emitted until at least this amount number of
 * packets is initially buffered. This is a quarter of a second.
 */
#define FV_RECORDER_MIN_BUFFER (1000 / 4 / FV_PROTO_SPEECH_TIME)
/* Don't buffer more than three seconds worth of compressed audio
 */
#define FV_RECORDER_MAX_BUFFER (3000 / FV_PROTO_SPEECH_TIME)

struct fv_recorder {
        struct fv_mutex *mutex;

        struct fv_microphone *mic;

        fv_recorder_callback callback;
        void *user_data;

        OpusEncoder *encoder;

        /* This buffers uncompressed samples until the size of a
         * packet is reached.
         */
        int16_t raw_buffer[FV_RECORDER_SAMPLES_PER_PACKET];
        int raw_sample_count;

        /* Once we get a get a packet that has a sample above the
         * silence threshold then we will start recording and this
         * will become true.
         */
        bool recording;
        /* While recording whenever a packet is received which is
         * entirely below the silence threshold then this is
         * increased. If a non-silent packet is reached then it is
         * reset to zero. If it ever reaches enough to cover one
         * second then recording stops.
         */
        int silence_count;

        /* Ring buffer of compressed packets. Each packet is preceeded
         * by a byte length.
         */
        _Static_assert(FV_PROTO_MAX_SPEECH_SIZE <= 255,
                       "The maximum size of a compressed speech packet is too "
                       "large to fit in a uint8_t");
        uint8_t *ring_buffer;
        int ring_buffer_size;
        int ring_buffer_length;
        int ring_buffer_start;

        /* Number of compressed packets buffered in the ring buffer */
        int n_packets;
        /* No packets are sent until a minimum number of packets are
         * buffered. Once this minimum is reached the variable below
         * will be set to true and it will start sending packets even
         * if the buffer reaches the minimum. This is reset whenever
         * silence is reached.
         */
        bool emitting;
};

bool
fv_recorder_has_packet(struct fv_recorder *recorder)
{
        bool res;

        fv_mutex_lock(recorder->mutex);
        res = recorder->emitting && recorder->n_packets > 0;
        fv_mutex_unlock(recorder->mutex);

        return res;
}

static void
check_emitting(struct fv_recorder *recorder)
{
        /* Once the buffer becomes empty we'll wait until it has
         * reached the minimum level again before starting to emit
         * the packets.
         */
        if (recorder->n_packets <= 0 && !recorder->recording)
                recorder->emitting = false;
}

static void
consume_packet(struct fv_recorder *recorder)
{
        int packet_size;

        packet_size = recorder->ring_buffer[recorder->ring_buffer_start];

        recorder->ring_buffer_start = ((recorder->ring_buffer_start +
                                        packet_size + 1) &
                                       (recorder->ring_buffer_size - 1));
        recorder->ring_buffer_length -= packet_size + 1;

        recorder->n_packets--;

        check_emitting(recorder);
}

int
fv_recorder_get_packet(struct fv_recorder *recorder,
                       uint8_t *buffer,
                       size_t buffer_size)
{
        int res = -1;
        int packet_size;
        int to_copy;

        assert(fv_recorder_has_packet(recorder));

        fv_mutex_lock(recorder->mutex);

        packet_size = recorder->ring_buffer[recorder->ring_buffer_start];

        if (buffer_size < packet_size)
                goto out;

        to_copy = MIN(recorder->ring_buffer_size -
                      recorder->ring_buffer_start - 1,
                      packet_size);
        memcpy(buffer,
               recorder->ring_buffer + recorder->ring_buffer_start + 1,
               to_copy);
        memcpy(buffer + to_copy, recorder->ring_buffer, packet_size - to_copy);

        consume_packet(recorder);

        res = packet_size;

out:
        fv_mutex_unlock(recorder->mutex);

        return res;
}

static bool
packet_is_silence(const int16_t *data)
{
        int i;

        for (i = 0; i < FV_RECORDER_SAMPLES_PER_PACKET; i++) {
                if (abs(data[i]) >= FV_RECORDER_SILENCE_THRESHOLD)
                        return false;
        }

        return true;
}

static void
add_to_ring_buffer(struct fv_recorder *recorder,
                   const uint8_t *data,
                   int length)
{
        uint8_t *new_buf;
        int to_copy;
        int dst;
        int new_size;

        if (recorder->ring_buffer_length + length >
            recorder->ring_buffer_size) {
                for (new_size = recorder->ring_buffer_size;
                     new_size < recorder->ring_buffer_length + length;
                     new_size *= 2);
                new_buf = fv_alloc(new_size);
                to_copy = MIN(recorder->ring_buffer_size -
                              recorder->ring_buffer_start,
                              recorder->ring_buffer_length);
                memcpy(new_buf,
                       recorder->ring_buffer + recorder->ring_buffer_start,
                       to_copy);
                memcpy(new_buf + to_copy,
                       recorder->ring_buffer,
                       recorder->ring_buffer_length - to_copy);

                fv_free(recorder->ring_buffer);
                recorder->ring_buffer_size = new_size;
                recorder->ring_buffer_start = 0;
                recorder->ring_buffer = new_buf;
        }

        dst = ((recorder->ring_buffer_start + recorder->ring_buffer_length) &
               (recorder->ring_buffer_size - 1));
        to_copy = MIN(recorder->ring_buffer_size - dst, length);
        memcpy(recorder->ring_buffer + dst, data, to_copy);
        memcpy(recorder->ring_buffer, data + to_copy, length - to_copy);

        recorder->ring_buffer_length += length;
}

static bool
add_packet(struct fv_recorder *recorder,
           const int16_t *data)
{
        uint8_t buf[FV_PROTO_MAX_SPEECH_SIZE + 1];
        bool is_silence = packet_is_silence(data);
        opus_int32 length;

        if (recorder->recording) {
                /* Stop recording if we've received too much silence */
                if (is_silence &&
                    ++recorder->silence_count >=
                    FV_RECORDER_MAX_SILENT_PACKETS) {
                        recorder->recording = false;
                        check_emitting(recorder);
                        return false;
                }
        } else {
                /* Skip packets until we receive a non-silent one */
                if (is_silence)
                        return false;
                recorder->recording = true;
                recorder->silence_count = 0;
        }

        length = opus_encode(recorder->encoder,
                             data,
                             FV_RECORDER_SAMPLES_PER_PACKET,
                             buf + 1,
                             FV_PROTO_MAX_SPEECH_SIZE);
        if (length < 0)
                return false;

        buf[0] = length;
        add_to_ring_buffer(recorder, buf, length + 1);

        recorder->n_packets++;

        if (recorder->n_packets >= FV_RECORDER_MIN_BUFFER) {
                recorder->emitting = true;

                if (recorder->n_packets > FV_RECORDER_MAX_BUFFER)
                        consume_packet(recorder);
        }

        return true;
}

static void
microphone_cb(const int16_t *data,
              int n_samples,
              void *user_data)
{
        struct fv_recorder *recorder = user_data;
        int to_copy;
        bool packet_added = false;

        fv_mutex_lock(recorder->mutex);

        /* Try to complete any incomplete packet that we received last time */
        if (recorder->raw_sample_count > 0) {
                to_copy = MIN(n_samples,
                              FV_RECORDER_SAMPLES_PER_PACKET -
                              recorder->raw_sample_count);

                memcpy(recorder->raw_buffer + recorder->raw_sample_count,
                       data,
                       to_copy * sizeof (int16_t));

                recorder->raw_sample_count += to_copy;

                if (recorder->raw_sample_count < FV_RECORDER_SAMPLES_PER_PACKET)
                        goto out;

                packet_added |= add_packet(recorder, recorder->raw_buffer);

                data += to_copy;
                n_samples -= to_copy;

                recorder->raw_sample_count = 0;
        }

        /* Add any complete packets */
        while (n_samples >= FV_RECORDER_SAMPLES_PER_PACKET) {
                packet_added |= add_packet(recorder, data);
                data += FV_RECORDER_SAMPLES_PER_PACKET;
                n_samples -= FV_RECORDER_SAMPLES_PER_PACKET;
        }

        /* Queue any remaining data so we can have a complete packet
         * next time.
         */
        memcpy(recorder->raw_buffer, data, n_samples * sizeof (int16_t));
        recorder->raw_sample_count = n_samples;

out:
        if (!recorder->emitting)
                packet_added = false;

        fv_mutex_unlock(recorder->mutex);

        if (packet_added)
                recorder->callback(recorder->user_data);
}

struct fv_recorder *
fv_recorder_new(fv_recorder_callback callback,
                void *user_data)
{
        struct fv_recorder *recorder = fv_alloc(sizeof *recorder);

        recorder->callback = callback;
        recorder->user_data = user_data;

        recorder->recording = false;
        recorder->silence_count = 0;
        recorder->raw_sample_count = 0;

        recorder->ring_buffer_size = 512;
        recorder->ring_buffer = fv_alloc(recorder->ring_buffer_size);
        recorder->ring_buffer_length = 0;
        recorder->ring_buffer_start = 0;
        recorder->n_packets = 0;
        recorder->emitting = false;

        recorder->mutex = fv_mutex_new();
        if (recorder->mutex == NULL) {
                fv_error_message("Error creating mutex");
                goto error;
        }

        recorder->encoder = opus_encoder_create(FV_SPEECH_SAMPLE_RATE,
                                                1, /* channels */
                                                OPUS_APPLICATION_VOIP,
                                                NULL /* error */);
        if (recorder->encoder == NULL) {
                fv_error_message("Error creating speech encoder");
                goto error_mutex;
        }

        opus_encoder_ctl(recorder->encoder, OPUS_SET_BITRATE(8192));

        recorder->mic = fv_microphone_new(microphone_cb, recorder);

        if (recorder->mic == NULL) {
                fv_free(recorder);
                goto error_opus;
        }

        return recorder;

error_opus:
        opus_encoder_destroy(recorder->encoder);
error_mutex:
        fv_mutex_free(recorder->mutex);
        error:
        fv_free(recorder->ring_buffer);
        fv_free(recorder);

        return NULL;
}

void
fv_recorder_free(struct fv_recorder *recorder)
{
        fv_microphone_free(recorder->mic);

        fv_mutex_free(recorder->mutex);

        opus_encoder_destroy(recorder->encoder);

        fv_free(recorder->ring_buffer);

        fv_free(recorder);
}
