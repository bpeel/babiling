/*
 * Babiling
 *
 * Copyright (C) 2015, 2016 Neil Roberts
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

#ifndef FV_FLAG_PAINTER_H
#define FV_FLAG_PAINTER_H

#include "fv-logic.h"
#include "fv-image-data.h"
#include "fv-shader-data.h"
#include "fv-paint-state.h"

struct fv_flag_painter *
fv_flag_painter_new(struct fv_image_data *image_data,
                    struct fv_shader_data *shader_data);

void
fv_flag_painter_paint(struct fv_flag_painter *painter,
                      struct fv_logic *logic,
                      struct fv_paint_state *paint_state);

void
fv_flag_painter_free(struct fv_flag_painter *painter);

#endif /* FV_FLAG_PAINTER_H */
