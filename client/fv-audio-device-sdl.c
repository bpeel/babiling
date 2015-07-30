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

#include "config.h"

#include <SDL.h>

#include "fv-audio-device.h"
#include "fv-util.h"
#include "fv-speech.h"
#include "fv-error-message.h"

struct fv_audio_device {
        SDL_AudioDeviceID device_id;

        fv_audio_device_callback callback;
        void *user_data;
};

static void
audio_cb(void *user_data,
         uint8_t *stream,
         int len)
{
        struct fv_audio_device *dev = user_data;

        dev->callback((int16_t *) stream,
                      len / sizeof (int16_t),
                      dev->user_data);
}

struct fv_audio_device *
fv_audio_device_new(fv_audio_device_callback callback,
                    void *user_data)
{
        SDL_AudioSpec desired, obtained;
        struct fv_audio_device *dev;

        dev = fv_alloc(sizeof *dev);

        SDL_zero(desired);
        desired.freq = FV_SPEECH_SAMPLE_RATE;
        desired.format = AUDIO_S16SYS;
        desired.channels = 1;
        desired.samples = 4096;
        desired.callback = audio_cb;
        desired.userdata = dev;

        dev->device_id =
                SDL_OpenAudioDevice(NULL, /* default device */
                                    false, /* iscapture */
                                    &desired,
                                    &obtained,
                                    0 /* allowed changes */);
        if (dev->device_id == 0) {
                fv_error_message("Error opening audio device: %s",
                                 SDL_GetError());
                fv_free(dev);
                return NULL;
        }

        dev->callback = callback;
        dev->user_data = user_data;

        SDL_PauseAudioDevice(dev->device_id, false);

        return dev;
}

void
fv_audio_device_free(struct fv_audio_device *dev)
{
        SDL_CloseAudioDevice(dev->device_id);
        fv_free(dev);
}
