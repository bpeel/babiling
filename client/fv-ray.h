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

#ifndef FV_RAY_H
#define FV_RAY_H

/* Calculates where the infinetely long ray described by ray_points
 * intersects the z-plane situated at z_plane. ray_points should be 6
 * floats representing two 3-coordinate points along the ray.
 */
void
fv_ray_intersect_z_plane(const float *ray_points,
                         float z_plane,
                         float *world_x, float *world_y);

#endif /* FV_RAY_H */
