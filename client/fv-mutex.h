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

#ifndef FV_MUTEX_H
#define FV_MUTEX_H

struct fv_mutex;

#ifdef EMSCRIPTEN

/* There are no threads in the Emscripten version so mutexes aren't
 * necessary. Instead we can just use a dummy pointer.
 */

static inline struct fv_mutex *
fv_mutex_new(void)
{
        return (struct fv_mutex *) 42;
}

static inline void
fv_mutex_lock(struct fv_mutex *mutex)
{
}

static inline void
fv_mutex_unlock(struct fv_mutex *mutex)
{
}

static inline void
fv_mutex_free(struct fv_mutex *mutex)
{
}

#else /* EMSCRIPTEN */

#include <SDL.h>

static inline struct fv_mutex *
fv_mutex_new(void)
{
        return (struct fv_mutex *) SDL_CreateMutex();
}

static inline void
fv_mutex_lock(struct fv_mutex *mutex)
{
        SDL_LockMutex((SDL_mutex *) mutex);
}

static inline void
fv_mutex_unlock(struct fv_mutex *mutex)
{
        SDL_UnlockMutex((SDL_mutex *) mutex);
}

static inline void
fv_mutex_free(struct fv_mutex *mutex)
{
        SDL_DestroyMutex((SDL_mutex *) mutex);
}

#endif /* EMSCRIPTEN */

#endif /* FV_MUTEX_H */
