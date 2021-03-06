/*
 * Babiling
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

#ifndef FV_MAP_PAINTER_H
#define FV_MAP_PAINTER_H

#include "fv-image-data.h"
#include "fv-shader-data.h"
#include "fv-paint-state.h"
#include "fv-map.h"

struct fv_map_painter *
fv_map_painter_new(const struct fv_map *map,
                   struct fv_image_data *image_data,
                   struct fv_shader_data *shader_data);

void
fv_map_painter_paint(struct fv_map_painter *painter,
                     struct fv_paint_state *paint_state);

void
fv_map_painter_free(struct fv_map_painter *painter);

#endif /* FV_MAP_PAINTER_H */
