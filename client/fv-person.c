/*
 * Babiling
 *
 * Copyright (C) 2016 Neil Roberts
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

#include "fv-person.h"

void
fv_person_copy_state(struct fv_person *dst,
                     const struct fv_person *src,
                     enum fv_person_state state)
{
        if ((state & FV_PERSON_STATE_POSITION))
                dst->pos = src->pos;

        if ((state & FV_PERSON_STATE_APPEARANCE))
                dst->appearance = src->appearance;
}
