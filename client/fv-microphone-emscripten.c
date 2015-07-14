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

#include "fv-microphone.h"
#include "fv-util.h"

struct fv_microphone {
        int stub;
};

struct fv_microphone *
fv_microphone_new(fv_microphone_callback callback,
                  void *user_data)
{
        return fv_alloc(sizeof (struct fv_microphone));
}

void
fv_microphone_free(struct fv_microphone *mic)
{
        fv_free(mic);
}
