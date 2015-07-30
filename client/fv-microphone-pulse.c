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

#include <pulse/simple.h>
#include <SDL.h>

#include "fv-microphone.h"
#include "fv-util.h"
#include "fv-error-message.h"
#include "fv-mutex.h"
#include "fv-speech.h"

struct fv_microphone {
        pa_simple *pa;

        SDL_Thread *thread;

        fv_microphone_callback callback;
        void *user_data;

        struct fv_mutex *mutex;
        bool quit;
};

static int
thread_func(void *user_data)
{
        struct fv_microphone *mic = user_data;
        int16_t buf[480];
        bool quit;
        int res;

        while (true) {
                fv_mutex_lock(mic->mutex);

                quit = mic->quit;

                fv_mutex_unlock(mic->mutex);

                if (quit)
                        break;

                res = pa_simple_read(mic->pa,
                                     buf,
                                     sizeof buf,
                                     NULL);
                if (res == -1)
                        break;

                mic->callback(buf,
                              FV_N_ELEMENTS(buf),
                              mic->user_data);
        }

        return 0;
}

struct fv_microphone *
fv_microphone_new(fv_microphone_callback callback,
                  void *user_data)
{
        struct fv_microphone *mic = fv_alloc(sizeof *mic);
        pa_sample_spec ss;

        mic->callback = callback;
        mic->user_data = user_data;
        mic->quit = false;

        ss.format = PA_SAMPLE_S16NE;
        ss.channels = 1;
        ss.rate = FV_SPEECH_SAMPLE_RATE;

        mic->pa = pa_simple_new(NULL, /* server */
                                "Babiling",
                                PA_STREAM_RECORD,
                                NULL, /* device */
                                "VoIP input",
                                &ss,
                                NULL, /* channel map */
                                NULL, /* buffering attributes */
                                NULL /* error */);

        if (mic->pa == NULL) {
                fv_error_message("Error connecting to PulseAudio");
                goto error;
        }

        mic->mutex = fv_mutex_new();
        if (mic->mutex == NULL) {
                fv_error_message("Error creating mutex: %s", SDL_GetError());
                goto error_pa;
        }

        mic->thread = SDL_CreateThread(thread_func,
                                       "Microphone",
                                       mic);
        if (mic->thread == NULL) {
                fv_error_message("Error creating thread: %s", SDL_GetError());
                goto error_mutex;
        }

        return mic;

error_pa:
        pa_simple_free(mic->pa);
error_mutex:
        fv_mutex_free(mic->mutex);
error:
        fv_free(mic);

        return NULL;
}

void
fv_microphone_free(struct fv_microphone *mic)
{
        fv_mutex_lock(mic->mutex);
        mic->quit = true;
        fv_mutex_unlock(mic->mutex);

        SDL_WaitThread(mic->thread, NULL /* status */);

        fv_mutex_free(mic->mutex);

        pa_simple_free(mic->pa);

        fv_free(mic);
}
