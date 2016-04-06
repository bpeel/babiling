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

#include <math.h>
#include <float.h>

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

/* Checks whether the ray intersects a rectangle which is
 * perpendicular to the axis given by plane_axis. plane_pos is the
 * position of the plane along this axis. The other two axes are
 * called ‘a’ and ‘b’ and are plane_axis+1 and +2 mod 3 respectively.
 * The center and size of the rectangle is given in along two axes,
 * whatever they are. If the ray intersects the rectangle at a
 * position which is better than the one in *best_frac then it updates
 * *best_frac.
 */
static void
check_rectangle(const float *ray_points,
                int plane_axis,
                float plane_pos,
                float center_a,
                float center_b,
                float a_size,
                float b_size,
                float *best_frac)
{
        int a_axis = (plane_axis + 1) % 3;
        int b_axis = (plane_axis + 2) % 3;
        float intersect_a, intersect_b;
        float frac;

        /* If the ray is parallel to the plane then don't bother checking */
        if (ray_points[plane_axis] == ray_points[plane_axis + 3])
                return;

        frac = ((plane_pos - ray_points[plane_axis + 3]) /
                (ray_points[plane_axis] - ray_points[plane_axis + 3]));

        /* If we've already found a better intersection point then
         * there's no point in checking whether it actually
         * intersects.
         */
        if (frac < *best_frac)
                return;

        /* Get the coordinates of the point of intersection in the
         * other two axes.
         */
        intersect_a = (frac * (ray_points[a_axis] - ray_points[a_axis + 3]) +
                       ray_points[a_axis + 3]);
        intersect_b = (frac * (ray_points[b_axis] - ray_points[b_axis + 3]) +
                       ray_points[b_axis + 3]);

        if (fabsf(intersect_a - center_a) < a_size / 2.0f &&
            fabsf(intersect_b - center_b) < b_size / 2.0f)
                *best_frac = frac;
}

bool
fv_ray_intersect_aabb(const float *ray_points,
                      const float *center,
                      const float *size,
                      float *intersection)
{
        float best_frac = -FLT_MAX;
        int plane_axis, a_axis, b_axis;

        for (plane_axis = 0; plane_axis < 3; plane_axis++) {
                a_axis = (plane_axis + 1) % 3;
                b_axis = (plane_axis + 2) % 3;

                check_rectangle(ray_points,
                                plane_axis,
                                center[plane_axis] - size[plane_axis] / 2.0f,
                                center[a_axis],
                                center[b_axis],
                                size[a_axis],
                                size[b_axis],
                                &best_frac);
                check_rectangle(ray_points,
                                plane_axis,
                                center[plane_axis] + size[plane_axis] / 2.0f,
                                center[a_axis],
                                center[b_axis],
                                size[a_axis],
                                size[b_axis],
                                &best_frac);
        }

        if (best_frac == -FLT_MAX)
                return false;

        *intersection = best_frac;

        return true;
}
