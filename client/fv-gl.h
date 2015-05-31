/*
 * Finvenkisto
 *
 * Copyright (C) 2014 Neil Roberts
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

#ifndef FV_GL_H
#define FV_GL_H

#include <GL/gl.h>
#include <stdbool.h>

struct fv_gl {
#define FV_GL_BEGIN_GROUP(a, b, c)
#define FV_GL_FUNC(return_type, name, args) return_type (APIENTRYP name) args;
#define FV_GL_END_GROUP()
#include "fv-gl-funcs.h"
#undef FV_GL_BEGIN_GROUP
#undef FV_GL_FUNC
#undef FV_GL_END_GROUP

        int major_version;
        int minor_version;

        bool have_map_buffer_range;
        bool have_vertex_array_objects;
        bool have_texture_2d_array;
        bool have_instanced_arrays;
};

extern struct fv_gl fv_gl;

void
fv_gl_init(void);

#endif /* FV_GL_H */