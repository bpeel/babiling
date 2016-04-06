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

#include "fv-ray.h"

void
fv_ray_intersect_z_plane(const float *ray_points,
                         float z_plane,
                         float *world_x, float *world_y)
{
        float frac;

        frac = (z_plane - ray_points[5]) / (ray_points[2] - ray_points[5]);
        *world_x = frac * (ray_points[0] - ray_points[3]) + ray_points[3];
        *world_y = frac * (ray_points[1] - ray_points[4]) + ray_points[4];
}
