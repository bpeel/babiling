/*
 * Babiling
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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

#ifndef FV_GAME_H
#define FV_GAME_H

#include <stdbool.h>

#include "fv-logic.h"
#include "fv-shader-data.h"
#include "fv-image-data.h"

struct fv_game *
fv_game_new(struct fv_image_data *image_data,
            struct fv_shader_data *shader_data);

void
fv_game_paint(struct fv_game *game,
              float center_x, float center_y,
              int width, int height,
              struct fv_logic *logic);

bool
fv_game_covers_framebuffer(struct fv_game *game,
                           float center_x, float center_y,
                           int width, int height);

/* Converts the window-relative coordinates to two points representing
 * a ray projected from the screen position into the world, assuming
 * that the bottom left corner of the map is the center of the window
 * (ie, it doesn't take into account the center transform).
 *
 * The coordinates of the ray are returned in ray_points which should
 * be an array of 6 floats. Each pair of three coordinates correspond
 * to the x,y,z coords of one of the ends of the ray touching the near
 * or far plane of the projection.
 */
void
fv_game_screen_to_world_ray(struct fv_game *game,
                            int width, int height,
                            int screen_x, int screen_y,
                            float *ray_points);

/* This converts window-relative coordinates to a position on the
 * floor of world, assuming that the bottom left corner of the map is
 * the center of the window (ie, it doesn't take into account the
 * center transform).
 */
void
fv_game_screen_to_world(struct fv_game *game,
                        int width, int height,
                        int screen_x, int screen_y,
                        float *world_x, float *world_y);

void
fv_game_free(struct fv_game *game);

#endif /* FV_GAME_H */
