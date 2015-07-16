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

#include <emscripten.h>
#include <SDL.h>

#include "fv-microphone.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-error-message.h"
#include "fv-speech.h"

struct fv_microphone {
        struct fv_buffer buffer;

        bool need_convert;
        SDL_AudioCVT audio_cvt;

        fv_microphone_callback callback;
        void *user_data;
};

EMSCRIPTEN_KEEPALIVE uint8_t *
fv_microphone_get_buffer_space(struct fv_microphone *mic,
                               size_t n_samples)
{
        fv_buffer_set_length(&mic->buffer, n_samples * sizeof (int16_t));
        return mic->buffer.data;
}

EMSCRIPTEN_KEEPALIVE void
fv_microphone_invoke_callback(struct fv_microphone *mic,
                              size_t n_samples)
{
        size_t length;
        int res;

        if (mic->need_convert) {
                length = n_samples * sizeof (int16_t);
                fv_buffer_ensure_size(&mic->buffer,
                                      length * mic->audio_cvt.len_mult);
                mic->audio_cvt.buf = mic->buffer.data;
                mic->audio_cvt.len = length;

                res = SDL_ConvertAudio(&mic->audio_cvt);
                if (res < 0)
                        return;

                n_samples = mic->audio_cvt.len_cvt / sizeof (int16_t);
        }

        mic->callback((const int16_t *) mic->buffer.data,
                      n_samples,
                      mic->user_data);
}

struct fv_microphone *
fv_microphone_new(fv_microphone_callback callback,
                  void *user_data)
{
        struct fv_microphone *mic = fv_alloc(sizeof *mic);
        int res;

        mic->callback = callback;
        mic->user_data = user_data;

        int sample_rate = EM_ASM_INT({
                        var mic = $0;

                        function onProcess(e)
                        {
                                var ib = e.inputBuffer.getChannelData(0);
                                var ob = e.outputBuffer.getChannelData(0);
                                var len = e.inputBuffer.length;
                                var buf = _fv_microphone_get_buffer_space(mic,
                                                                          len);
                                var i;

                                for (i = 0; i < len; i++)
                                        HEAP16[(buf >> 1) + i] =
                                                ib[i] * 32767.0;

                                /* The audio API doesn't seem to let
                                 * you get away without connecting the
                                 * script processor node to the
                                 * destination we we'll have to
                                 * generate silence in order for the
                                 * microphone not to be heard.
                                 */
                                for (i = 0; i < e.outputBuffer.length; i++)
                                        ob[i] = 0.0;

                                _fv_microphone_invoke_callback(mic, len);
                        }

                        function onFail()
                        {
                                console.warn("User denied access to the " +
                                             "microphone");
                        }

                        function onSuccess(um)
                        {
                                var ac = Module.audioContext;
                                var source = ac.createMediaStreamSource(um);
                                var node = ac.createScriptProcessor(
                                        0, /* buffer size */
                                        1, /* input channels */
                                        1 /* output channels */);

                                node.onaudioprocess = onProcess;
                                source.connect(node);
                                node.connect(ac.destination);
                        }

                        var getUserMedia = (navigator.getUserMedia ||
                                            navigator.webkitGetUserMedia ||
                                            navigator.mozGetUserMedia);
                        if (!getUserMedia)
                                return 0;
                        if (!window.AudioContext)
                                return 0;

                        Module.audioContext = new AudioContext();

                        if (!Module.audioContext.createScriptProcessor ||
                            !Module.audioContext.createMediaStreamSource)
                                return 0;

                        getUserMedia.call(navigator,
                                          { audio: true },
                                          onSuccess,
                                          onFail);

                        return Module.audioContext.sampleRate;
                }, mic);

        if (!sample_rate) {
                fv_error_message("The microphone is not supported by this "
                                 "browser");

                fv_free(mic);

                return NULL;
        }

        mic->need_convert = false;

        if (sample_rate != FV_SPEECH_SAMPLE_RATE) {
                res = SDL_BuildAudioCVT(&mic->audio_cvt,
                                        AUDIO_S16,
                                        1, /* channels_in */
                                        sample_rate,
                                        AUDIO_S16,
                                        1, /* channels_out */
                                        FV_SPEECH_SAMPLE_RATE);
                if (res < 0) {
                        fv_error_message("Couldn't set up a conversion from "
                                         "the microphone's sample rate");
                        fv_free(mic);
                        return NULL;
                }

                mic->need_convert = true;
        }

        fv_buffer_init(&mic->buffer);

        return mic;
}

void
fv_microphone_free(struct fv_microphone *mic)
{
        EM_ASM({
                        Module.audioContext.close();
                        Module.audioContext = undefined;
                });

        fv_buffer_destroy(&mic->buffer);

        fv_free(mic);
}
