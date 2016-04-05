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

#ifndef FV_PERSON_H
#define FV_PERSON_H

#include <stdint.h>

enum fv_person_type {
        FV_PERSON_TYPE_BAMBISTO1,
        FV_PERSON_TYPE_BAMBISTO2,
        FV_PERSON_TYPE_BAMBISTO3,
        FV_PERSON_TYPE_GUFUJESTRO,
        FV_PERSON_TYPE_TOILET_GUY,
        FV_PERSON_TYPE_PYJAMAS,
};

#define FV_PERSON_N_TYPES 6

struct fv_person_position {
        uint32_t x, y;
        uint16_t direction;
};

struct fv_person_appearance {
        uint8_t image;
};

enum fv_person_state {
        FV_PERSON_STATE_POSITION = (1 << 0),
        FV_PERSON_STATE_APPEARANCE = (1 << 1),
        FV_PERSON_STATE_ALL = (1 << 2) - 1,
};

struct fv_person {
        struct fv_person_position pos;
        struct fv_person_appearance appearance;
};

void
fv_person_copy_state(struct fv_person *dst,
                     const struct fv_person *src,
                     enum fv_person_state state);

#endif /* FV_PERSON_H */
