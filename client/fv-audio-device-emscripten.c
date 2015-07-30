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
#include <math.h>
#include <emscripten.h>

#include "fv-audio-device.h"
#include "fv-util.h"
#include "fv-speech.h"
#include "fv-error-message.h"
#include "fv-buffer.h"

struct fv_audio_device {
        struct fv_buffer buffer;

        bool need_convert;
        SDL_AudioCVT audio_cvt;

        fv_audio_device_callback callback;
        void *user_data;
};

EMSCRIPTEN_KEEPALIVE int16_t *
fv_audio_device_get_data(struct fv_audio_device *dev,
                         int n_samples)
{
        size_t length;

        if (dev->need_convert)
                length = ceilf(n_samples * sizeof (int16_t) *
                               dev->audio_cvt.len_ratio);
        else
                length = n_samples * sizeof (int16_t);

        fv_buffer_set_length(&dev->buffer, length);

        dev->callback((int16_t *) dev->buffer.data,
                      length / sizeof (int16_t),
                      dev->user_data);

        if (dev->need_convert) {
                fv_buffer_ensure_size(&dev->buffer,
                                      length * dev->audio_cvt.len_mult);
                dev->audio_cvt.buf = dev->buffer.data;
                dev->audio_cvt.len = length;
                SDL_ConvertAudio(&dev->audio_cvt);
        }

        return (int16_t *) dev->buffer.data;
}

struct fv_audio_device *
fv_audio_device_new(fv_audio_device_callback callback,
                    void *user_data)
{
        struct fv_audio_device *dev;
        int sample_rate;
        int res;

        dev = fv_alloc(sizeof *dev);

        dev->callback = callback;
        dev->user_data = user_data;

        sample_rate = EM_ASM_INT({
                        var dev = $0;

                        function onProcess(e)
                        {
                                var ob = e.outputBuffer.getChannelData(0);
                                var len = e.outputBuffer.length;
                                var buf = _fv_audio_device_get_data(dev, len);
                                var i;

                                for (i = 0; i < len; i++)
                                        ob[i] = HEAP16[(buf >> 1) + i] /
                                                32767.0;
                        }

                        if (!window.AudioContext)
                                return 0;

                        var ac = new AudioContext();

                        if (!ac.createScriptProcessor)
                                return 0;

                        var sp = ac.createScriptProcessor(
                                0, /* buffer size */
                                0, /* input channels */
                                1 /* output channels */);

                        sp.onaudioprocess = onProcess;
                        sp.connect(ac.destination);

                        Module.audioContext = ac;

                        return ac.sampleRate;
                }, dev);

        if (!sample_rate) {
                fv_error_message("Audio output is not supported by this "
                                 "browser");

                fv_free(dev);

                return NULL;
        }

        dev->need_convert = false;

        if (sample_rate != FV_SPEECH_SAMPLE_RATE) {
                res = SDL_BuildAudioCVT(&dev->audio_cvt,
                                        AUDIO_S16,
                                        1, /* channels_in */
                                        FV_SPEECH_SAMPLE_RATE,
                                        AUDIO_S16,
                                        1, /* channels_out */
                                        sample_rate);
                if (res < 0) {
                        fv_error_message("Couldn't set up a conversion "
                                         "to browser's sample rate");
                        fv_free(dev);
                        return NULL;
                }

                dev->need_convert = true;
        }

        fv_buffer_init(&dev->buffer);

        return dev;
}

void
fv_audio_device_free(struct fv_audio_device *dev)
{
        fv_buffer_destroy(&dev->buffer);
        fv_free(dev);
}
