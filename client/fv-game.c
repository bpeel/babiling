/*
 * Babiling
 *
 * Copyright (C) 2013, 2014, 2015 Neil Roberts
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
#include <stdbool.h>
#include <assert.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-map-painter.h"
#include "fv-person-painter.h"
#include "fv-flag-painter.h"
#include "fv-map.h"
#include "fv-gl.h"
#include "fv-paint-state.h"
#include "fv-ray.h"

#define FV_GAME_FRUSTUM_TOP 1.428f
/* 40° vertical FOV angle when the height of the display is
 * FV_GAME_FRUSTUM_TOP*2
 * ie, top / tan(40 / 2)
 */
#define FV_GAME_NEAR_PLANE 3.9233977549812007f
#define FV_GAME_FAR_PLANE 21.429f

#define FV_GAME_ORIGIN_DISTANCE 14.286f

struct fv_game {
        /* Size of a players viewport the last time we painted */
        int last_viewport_width, last_viewport_height;

        struct fv_paint_state paint_state;

        struct fv_map_painter *map_painter;
        struct fv_person_painter *person_painter;
        struct fv_flag_painter *flag_painter;

        struct fv_matrix base_transform;
        struct fv_matrix base_inverse;
};

struct fv_game *
fv_game_new(struct fv_image_data *image_data,
            struct fv_shader_data *shader_data)
{
        struct fv_game *game = fv_calloc(sizeof *game);

        fv_matrix_init_identity(&game->base_transform);

        fv_matrix_translate(&game->base_transform,
                            0.0f, 0.0f, -FV_GAME_ORIGIN_DISTANCE);

        fv_matrix_rotate(&game->base_transform,
                         -30.0f,
                         1.0f, 0.0f, 0.0f);

        game->map_painter = fv_map_painter_new(&fv_map,
                                               image_data,
                                               shader_data);
        if (game->map_painter == NULL)
                goto error;

        game->person_painter = fv_person_painter_new(image_data, shader_data);
        if (game->person_painter == NULL)
                goto error_map;

        game->flag_painter = fv_flag_painter_new(image_data, shader_data);
        if (game->flag_painter == NULL)
                goto error_person;

        return game;

error_person:
        fv_person_painter_free(game->person_painter);
error_map:
        fv_map_painter_free(game->map_painter);
error:
        fv_free(game);

        return NULL;
}

static void
update_base_inverse(struct fv_game *game)
{
        struct fv_matrix m;

        fv_matrix_multiply(&m,
                           &game->paint_state.transform.projection,
                           &game->base_transform);
        fv_matrix_get_inverse(&m, &game->base_inverse);
}

static void
screen_to_world_ray_internal(struct fv_game *game,
                             float x, float y,
                             float *ray_points)
{
        float points_in[2 * 3], points_out[2 * 4];
        int i, j;

        points_in[0] = x;
        points_in[1] = y;
        points_in[2] = -1.0f;
        points_in[3] = x;
        points_in[4] = y;
        points_in[5] = 1.0f;

        fv_matrix_project_points(&game->base_inverse,
                                 3, /* n_components */
                                 sizeof (float) * 3,
                                 points_in,
                                 sizeof (float) * 4,
                                 points_out,
                                 2 /* n_points */);

        for (i = 0; i < 2; i++) {
                for (j = 0; j < 3; j++) {
                        ray_points[i * 3 + j] =
                                points_out[i * 4 + j] /
                                points_out[i * 4 + 3];
                }
        }
}

static void
update_visible_area(struct fv_game *game)
{
        float min_x = FLT_MAX, max_x = -FLT_MAX;
        float min_y = FLT_MAX, max_y = -FLT_MAX;
        float ray_points[3 * 2 * 4];
        float *p;
        int x, y, z, i;
        float px, py;

        p = ray_points;

        for (y = -1; y <= 1; y += 2) {
                for (x = -1; x <= 1; x += 2) {
                        screen_to_world_ray_internal(game, x, y, p);
                        p += 3 * 2;
                }
        }

        p = ray_points;

        for (i = 0; i < 4; i++) {
                /* The two unprojected points represent a line going
                 * from the near plane to the far plane which gets
                 * projected to a single point touching one of the
                 * corners of the viewport. Here we work out the x/y
                 * position of the point along the line where it
                 * touches the plane representing the floor and the
                 * ceiling of the world and keep track of the furthest
                 * one. */
                for (z = 0; z <= 2; z += 2) {
                        fv_ray_intersect_z_plane(p, z, &px, &py);

                        if (px < min_x)
                                min_x = px;
                        if (px > max_x)
                                max_x = px;
                        if (py < min_y)
                                min_y = py;
                        if (py > max_y)
                                max_y = py;
                }

                p += 3 * 2;
        }

        game->paint_state.visible_w =
                fmaxf(fabsf(min_x), fabsf(max_x)) * 2.0f + 1.0f;
        game->paint_state.visible_h =
                fmaxf(fabsf(min_y), fabsf(max_y)) * 2.0f + 1.0f;
}

static void
update_projection(struct fv_game *game,
                  int w, int h)
{
        struct fv_transform *transform = &game->paint_state.transform;
        float right, top;

        if (w == 0 || h == 0)
                w = h = 1;

        /* Recalculate the projection matrix if we've got a different size
         * from last time */
        if (w != game->last_viewport_width || h != game->last_viewport_height) {
                if (w < h) {
                        right = FV_GAME_FRUSTUM_TOP;
                        top = h * FV_GAME_FRUSTUM_TOP / (float) w;
                } else {
                        top = FV_GAME_FRUSTUM_TOP;
                        right = w * FV_GAME_FRUSTUM_TOP / (float) h;
                }

                fv_matrix_init_identity(&transform->projection);

                fv_matrix_frustum(&transform->projection,
                                  -right, right,
                                  -top, top,
                                  FV_GAME_NEAR_PLANE,
                                  FV_GAME_FAR_PLANE);

                fv_transform_dirty(transform);

                update_base_inverse(game);
                update_visible_area(game);

                game->last_viewport_width = w;
                game->last_viewport_height = h;
        }
}

static void
update_modelview(struct fv_game *game,
                 struct fv_logic *logic)
{
        game->paint_state.transform.modelview = game->base_transform;

        fv_matrix_translate(&game->paint_state.transform.modelview,
                            -game->paint_state.center_x,
                            -game->paint_state.center_y,
                            0.0f);

        fv_transform_dirty(&game->paint_state.transform);
}

void
fv_game_screen_to_world_ray(struct fv_game *game,
                            int width, int height,
                            int screen_x, int screen_y,
                            float *ray_points)
{
        update_projection(game, width, height);
        screen_to_world_ray_internal(game,
                                     (screen_x + 0.5f) / width * 2.0f - 1.0f,
                                     (0.5f - screen_y) / height * 2.0f + 1.0f,
                                     ray_points);
}

void
fv_game_screen_to_world(struct fv_game *game,
                        int width, int height,
                        int screen_x, int screen_y,
                        float *world_x, float *world_y)
{
        float ray_points[2 * 3];

        fv_game_screen_to_world_ray(game,
                                    width, height,
                                    screen_x, screen_y,
                                    ray_points);
        fv_ray_intersect_z_plane(ray_points,
                                 0.0f, /* z_plane */
                                 world_x, world_y);
}

bool
fv_game_covers_framebuffer(struct fv_game *game,
                           float center_x, float center_y,
                           int width, int height)
{
        float visible_w, visible_h;

        update_projection(game, width, height);

        visible_w = game->paint_state.visible_w;
        visible_h = game->paint_state.visible_h;

        /* We only need to clear if the map doesn't cover the entire
         * viewport */
        return (center_x - visible_w / 2.0f >= 0.0f &&
                center_y - visible_h / 2.0f >= 0.0f &&
                center_x + visible_w / 2.0f <= FV_MAP_WIDTH &&
                center_y + visible_h / 2.0f <= FV_MAP_HEIGHT);
}

void
fv_game_paint(struct fv_game *game,
              float center_x, float center_y,
              int width, int height,
              struct fv_logic *logic)
{
        game->paint_state.width = width;
        game->paint_state.height = height;
        game->paint_state.center_x = center_x;
        game->paint_state.center_y = center_y;

        update_projection(game, width, height);

        update_modelview(game, logic);

        fv_person_painter_paint(game->person_painter,
                                logic,
                                &game->paint_state);

        fv_map_painter_paint(game->map_painter,
                             &game->paint_state);

        fv_flag_painter_paint(game->flag_painter,
                              logic,
                              &game->paint_state);
}

void
fv_game_free(struct fv_game *game)
{
        fv_flag_painter_free(game->flag_painter);
        fv_person_painter_free(game->person_painter);
        fv_map_painter_free(game->map_painter);
        fv_free(game);
}
