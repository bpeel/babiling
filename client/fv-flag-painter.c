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

#include "config.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "fv-flag-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-gl.h"
#include "fv-array-object.h"
#include "fv-map-buffer.h"

#include "data/flag-layout.h"

#define FV_FLAG_PAINTER_MAX_FLAGS FV_N_ELEMENTS(fv_flag_texture_flags)
/* One quad for each corner, one for each of the four edges and one
 * for the center quad.
 */
#define FV_FLAG_PAINTER_N_BACKGROUND_QUADS (4 + 4 + 1)

#define FV_FLAG_PAINTER_VERTEX_BUFFER_SIZE      \
        (sizeof (vertex) * 4 *                  \
         (FV_FLAG_PAINTER_MAX_FLAGS +           \
          FV_FLAG_PAINTER_N_BACKGROUND_QUADS))

#define FV_FLAG_PAINTER_GAP_RATIO (FV_FLAG_TEXTURE_FLAG_RATIO_Y / 4)

struct fv_flag_painter_vertex {
        float x, y;
        uint16_t s, t;
};

struct fv_flag_painter {
        GLuint program;
        GLuint transform_uniform;

        GLuint texture;
        struct fv_array_object *array;
        GLuint vertex_buffer;
        GLuint index_buffer;
};

static void
load_texture(struct fv_flag_painter *painter,
             struct fv_image_data *image_data)
{
        fv_gl.glGenTextures(1, &painter->texture);
        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);

        fv_image_data_set_2d(image_data,
                             GL_TEXTURE_2D,
                             0, /* level */
                             GL_RGBA,
                             FV_IMAGE_DATA_FLAG_TEXTURE);

        fv_gl.glGenerateMipmap(GL_TEXTURE_2D);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MIN_FILTER,
                              GL_LINEAR_MIPMAP_NEAREST);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MAG_FILTER,
                              GL_LINEAR);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_EDGE);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_EDGE);
}

static void
make_buffer(struct fv_flag_painter *painter)
{
        typedef struct fv_flag_painter_vertex vertex;
        uint16_t *index;
        int i;

        painter->array = fv_array_object_new();

        fv_gl.glGenBuffers(1, &painter->vertex_buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vertex_buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           FV_FLAG_PAINTER_VERTEX_BUFFER_SIZE,
                           NULL,
                           GL_DYNAMIC_DRAW);

        fv_array_object_set_attribute(painter->array,
                                      FV_SHADER_DATA_ATTRIB_POSITION,
                                      2, /* size */
                                      GL_FLOAT,
                                      GL_FALSE, /* normalized */
                                      sizeof (vertex),
                                      0, /* divisor */
                                      painter->vertex_buffer,
                                      offsetof(vertex, x));

        fv_array_object_set_attribute(painter->array,
                                      FV_SHADER_DATA_ATTRIB_TEX_COORD,
                                      2, /* size */
                                      GL_UNSIGNED_SHORT,
                                      GL_TRUE, /* normalized */
                                      sizeof (vertex),
                                      0, /* divisor */
                                      painter->vertex_buffer,
                                      offsetof(vertex, s));

        fv_gl.glGenBuffers(1, &painter->index_buffer);
        fv_array_object_set_element_buffer(painter->array,
                                           painter->index_buffer);
        fv_gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                           (FV_FLAG_PAINTER_MAX_FLAGS +
                            FV_FLAG_PAINTER_N_BACKGROUND_QUADS) *
                           sizeof (uint16_t) * 6,
                           NULL,
                           GL_STATIC_DRAW);

        index = fv_map_buffer_map(GL_ELEMENT_ARRAY_BUFFER,
                                  (FV_FLAG_PAINTER_MAX_FLAGS +
                                   FV_FLAG_PAINTER_N_BACKGROUND_QUADS) * 6 *
                                  sizeof (uint16_t),
                                  false, /* flush_explicit */
                                  GL_STATIC_DRAW);

        for (i = 0;
             i < (FV_FLAG_PAINTER_MAX_FLAGS +
                  FV_FLAG_PAINTER_N_BACKGROUND_QUADS);
             i++) {
                *(index++) = i * 4;
                *(index++) = i * 4 + 1;
                *(index++) = i * 4 + 3;
                *(index++) = i * 4 + 3;
                *(index++) = i * 4 + 1;
                *(index++) = i * 4 + 2;
        }

        fv_map_buffer_unmap();
}

struct fv_flag_painter *
fv_flag_painter_new(struct fv_image_data *image_data,
                    struct fv_shader_data *shader_data)
{
        struct fv_flag_painter *painter = fv_calloc(sizeof *painter);
        GLuint tex_uniform;

        painter->program =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_HUD];

        load_texture(painter, image_data);

        make_buffer(painter);

        tex_uniform = fv_gl.glGetUniformLocation(painter->program, "tex");
        fv_gl.glUseProgram(painter->program);
        fv_gl.glUniform1i(tex_uniform, 0);

        painter->transform_uniform =
                fv_gl.glGetUniformLocation(painter->program, "transform");

        return painter;
}

static int
get_flag_index(uint32_t flag_name)
{
        int min = 0, max = FV_N_ELEMENTS(fv_flag_texture_flags), mid;

        while (max > min) {
                mid = (max + min) / 2;
                if (fv_flag_texture_flags[mid] >= flag_name)
                        max = mid;
                else
                        min = mid + 1;
        }

        if (fv_flag_texture_flags[min] == flag_name)
                return min;
        else
                return -1;
}

static void
get_flag_unit_coordinates(int flag_index,
                          int *unit_x,
                          int *unit_y)
{
        int column = flag_index % FV_FLAG_TEXTURE_N_COLUMNS;
        int row = flag_index / FV_FLAG_TEXTURE_N_COLUMNS;

        *unit_x = column * (FV_FLAG_TEXTURE_FLAG_RATIO_X +
                            FV_FLAG_TEXTURE_PADDING_RATIO_X * 2);
        *unit_y = row * (FV_FLAG_TEXTURE_FLAG_RATIO_Y +
                         FV_FLAG_TEXTURE_PADDING_RATIO_Y * 2);
}

static void
set_quad_coordinates(struct fv_flag_painter_vertex *vertices,
                     float x1, float y1,
                     float x2, float y2)
{
        vertices->x = x1;
        vertices->y = y2;
        vertices++;

        vertices->x = x1;
        vertices->y = y1;
        vertices++;

        vertices->x = x2;
        vertices->y = y1;
        vertices++;

        vertices->x = x2;
        vertices->y = y2;
        vertices++;
}

static void
set_border_quad(struct fv_flag_painter_vertex *vertices,
                float x1, float y1,
                float x2, float y2,
                int s1, int t1,
                int s2, int t2)
{
        int base_unit_x, base_unit_y;

        set_quad_coordinates(vertices, x1, y1, x2, y2);

        get_flag_unit_coordinates(FV_N_ELEMENTS(fv_flag_texture_flags),
                                  &base_unit_x,
                                  &base_unit_y);
        s1 = ((base_unit_x + s1) *
              UINT16_MAX / FV_FLAG_TEXTURE_WIDTH_UNITS);
        t1 = ((base_unit_y + t1) *
              UINT16_MAX / FV_FLAG_TEXTURE_HEIGHT_UNITS);
        s2 = ((base_unit_x + s2) *
              UINT16_MAX / FV_FLAG_TEXTURE_WIDTH_UNITS);
        t2 = ((base_unit_y + t2) *
              UINT16_MAX / FV_FLAG_TEXTURE_HEIGHT_UNITS);

        vertices->s = s1;
        vertices->t = t2;
        vertices++;

        vertices->s = s1;
        vertices->t = t1;
        vertices++;

        vertices->s = s2;
        vertices->t = t1;
        vertices++;

        vertices->s = s2;
        vertices->t = t2;
        vertices++;
}

static void
add_background(struct fv_flag_painter_vertex *vertices,
               float unit_size_x,
               float unit_size_y,
               float x1, float y1,
               float x2, float y2)
{
        /* Four corner quads */
        set_border_quad(vertices,
                        x1, y1,
                        x1 + unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y1 + unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        0, 0,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_BORDER_RATIO);
        vertices += 4;
        set_border_quad(vertices,
                        x2 - unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y1,
                        x2,
                        y1 + unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        0,
                        0,
                        FV_FLAG_TEXTURE_BORDER_RATIO);
        vertices += 4;
        set_border_quad(vertices,
                        x1,
                        y2 - unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        x1 + unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y2,
                        0,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        0);
        vertices += 4;
        set_border_quad(vertices,
                        x2 - unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y2 - unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        x2,
                        y2,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        0,
                        0);
        vertices += 4;

        /* Four edge quads */
        set_border_quad(vertices,
                        x1 + unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y1,
                        x2 - unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y1 + unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_FLAG_RATIO_X / 2,
                        0,
                        FV_FLAG_TEXTURE_FLAG_RATIO_X / 2,
                        FV_FLAG_TEXTURE_BORDER_RATIO);
        vertices += 4;
        set_border_quad(vertices,
                        x1 + unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y2 - unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        x2 - unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y2,
                        FV_FLAG_TEXTURE_FLAG_RATIO_X / 2,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_FLAG_RATIO_X / 2,
                        0);
        vertices += 4;
        set_border_quad(vertices,
                        x1,
                        y1 + unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        x1 + unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y2 - unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        0,
                        FV_FLAG_TEXTURE_FLAG_RATIO_Y / 2,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_FLAG_RATIO_Y / 2);
        vertices += 4;
        set_border_quad(vertices,
                        x2 - unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y1 + unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        x2,
                        y2 - unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_FLAG_RATIO_Y / 2,
                        0,
                        FV_FLAG_TEXTURE_FLAG_RATIO_Y / 2);
        vertices += 4;

        /* Center quad */
        set_border_quad(vertices,
                        x1 + unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y1 + unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        x2 - unit_size_x * FV_FLAG_TEXTURE_BORDER_RATIO,
                        y2 - unit_size_y * FV_FLAG_TEXTURE_BORDER_RATIO,
                        FV_FLAG_TEXTURE_FLAG_RATIO_X / 2,
                        FV_FLAG_TEXTURE_FLAG_RATIO_Y,
                        FV_FLAG_TEXTURE_FLAG_RATIO_X / 2,
                        FV_FLAG_TEXTURE_FLAG_RATIO_Y);
}

static void
set_flag_texture_coordinates(struct fv_flag_painter_vertex *vertices,
                             int flag_index)
{
        int unit_x, unit_y;

        get_flag_unit_coordinates(flag_index, &unit_x, &unit_y);

        vertices[0].s = unit_x * UINT16_MAX / FV_FLAG_TEXTURE_WIDTH_UNITS;
        vertices[0].t = unit_y * UINT16_MAX / FV_FLAG_TEXTURE_HEIGHT_UNITS;
        vertices[1].s = unit_x * UINT16_MAX / FV_FLAG_TEXTURE_WIDTH_UNITS;
        vertices[1].t = ((unit_y + FV_FLAG_TEXTURE_FLAG_RATIO_Y) *
                         UINT16_MAX / FV_FLAG_TEXTURE_HEIGHT_UNITS);
        vertices[2].s = ((unit_x + FV_FLAG_TEXTURE_FLAG_RATIO_X) *
                         UINT16_MAX / FV_FLAG_TEXTURE_WIDTH_UNITS);
        vertices[2].t = ((unit_y + FV_FLAG_TEXTURE_FLAG_RATIO_Y) *
                         UINT16_MAX / FV_FLAG_TEXTURE_HEIGHT_UNITS);
        vertices[3].s = ((unit_x + FV_FLAG_TEXTURE_FLAG_RATIO_X) *
                         UINT16_MAX / FV_FLAG_TEXTURE_WIDTH_UNITS);
        vertices[3].t = unit_y * UINT16_MAX / FV_FLAG_TEXTURE_HEIGHT_UNITS;
}

static int
get_vertices_for_flags(struct fv_flag_painter *painter,
                       int screen_width,
                       int screen_height,
                       const uint32_t *flags,
                       int n_flags,
                       struct fv_flag_painter_vertex *vertices)
{
        int unit_pixels;
        float unit_size_x, unit_size_y;
        int max_columns, max_rows;
        int n_columns, n_rows;
        int n_quads = 0;
        int i, column, row;
        int flag_index;
        int remainder;
        float border_x1, border_y1;
        float flag_x1, flag_y1;

        unit_pixels = MIN(screen_width, screen_height) / 128;
        if (unit_pixels < 1)
                unit_pixels = 1;

        unit_size_x = unit_pixels * 2.0f / screen_width;
        unit_size_y = unit_pixels * 2.0f / screen_height;

        max_columns = ((screen_width / unit_pixels -
                        FV_FLAG_TEXTURE_BORDER_RATIO +
                        FV_FLAG_PAINTER_GAP_RATIO) /
                       (FV_FLAG_TEXTURE_FLAG_RATIO_X +
                        FV_FLAG_PAINTER_GAP_RATIO));
        max_rows = ((screen_height / unit_pixels -
                     FV_FLAG_TEXTURE_BORDER_RATIO +
                     FV_FLAG_PAINTER_GAP_RATIO) /
                    (FV_FLAG_TEXTURE_FLAG_RATIO_Y +
                     FV_FLAG_PAINTER_GAP_RATIO));

        n_columns = MIN(max_columns, n_flags);
        if (n_columns < 1)
                n_columns = 1;

        while (true) {
                n_rows = (n_flags + n_columns - 1) / n_columns;

                if (n_columns <= 1 || n_rows + 1 > max_rows)
                        break;

                if (n_columns <= n_rows * 4 / 3) {
                        remainder = n_flags % n_columns;

                        if (remainder == 0)
                                break;

                        if (remainder + n_rows - 1 > n_columns - 1)
                                break;
                }

                n_columns--;
        }

        border_x1 = -(n_columns * (FV_FLAG_TEXTURE_FLAG_RATIO_X +
                                   FV_FLAG_PAINTER_GAP_RATIO) -
                      FV_FLAG_PAINTER_GAP_RATIO +
                      FV_FLAG_TEXTURE_BORDER_RATIO * 2) / 2.0f * unit_size_x;
        border_y1 = -(n_rows * (FV_FLAG_TEXTURE_FLAG_RATIO_Y +
                                FV_FLAG_PAINTER_GAP_RATIO) -
                      FV_FLAG_PAINTER_GAP_RATIO +
                      FV_FLAG_TEXTURE_BORDER_RATIO * 2) / 2.0f * unit_size_y;

        add_background(vertices,
                       unit_size_x,
                       unit_size_y,
                       border_x1, border_y1,
                       -border_x1, -border_y1);
        n_quads += FV_FLAG_PAINTER_N_BACKGROUND_QUADS;

        for (i = 0; i < n_flags; i++) {
                flag_index = get_flag_index(flags[i]);

                if (flag_index == -1)
                        continue;

                column = i % n_columns;
                row = i / n_columns;

                flag_x1 = (border_x1 +
                           (column * (FV_FLAG_TEXTURE_FLAG_RATIO_X +
                                      FV_FLAG_PAINTER_GAP_RATIO) +
                            FV_FLAG_TEXTURE_BORDER_RATIO) *
                           unit_size_x);
                flag_y1 = (border_y1 +
                           ((n_rows - 1 - row) *
                            (FV_FLAG_TEXTURE_FLAG_RATIO_Y +
                             FV_FLAG_PAINTER_GAP_RATIO) +
                            FV_FLAG_TEXTURE_BORDER_RATIO) *
                           unit_size_y);

                set_quad_coordinates(vertices + n_quads * 4,
                                     flag_x1, flag_y1,
                                     flag_x1 +
                                     FV_FLAG_TEXTURE_FLAG_RATIO_X *
                                     unit_size_x,
                                     flag_y1 +
                                     FV_FLAG_TEXTURE_FLAG_RATIO_Y *
                                     unit_size_y);
                set_flag_texture_coordinates(vertices + n_quads * 4,
                                             flag_index);

                n_quads++;
        }

        return n_quads;
}

void
fv_flag_painter_paint(struct fv_flag_painter *painter,
                      int screen_width,
                      int screen_height,
                      struct fv_logic *logic)
{
        struct fv_flag_painter_vertex *vertex;
        int n_quads;

        fv_gl.glUseProgram(painter->program);
        fv_array_object_bind(painter->array);

        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);
        fv_gl.glEnable(GL_BLEND);

        fv_gl.glBindBuffer(GL_ARRAY_BUFFER,
                           painter->vertex_buffer);
        vertex = fv_map_buffer_map(GL_ARRAY_BUFFER,
                                   FV_FLAG_PAINTER_VERTEX_BUFFER_SIZE,
                                   true, /* flush_explicit */
                                   GL_DYNAMIC_DRAW);

        n_quads = get_vertices_for_flags(painter,
                                         screen_width,
                                         screen_height,
                                         fv_flag_texture_flags,
                                         8,
                                         vertex);

        fv_map_buffer_flush(0, /* offset */
                            4 * sizeof *vertex * n_quads);
        fv_map_buffer_unmap();

        fv_gl.glDrawElements(GL_TRIANGLES,
                             n_quads * 6, /* count */
                             GL_UNSIGNED_SHORT,
                             NULL /* offset */);

        fv_gl.glDisable(GL_BLEND);
}

void
fv_flag_painter_free(struct fv_flag_painter *painter)
{
        fv_array_object_free(painter->array);
        fv_gl.glDeleteBuffers(1, &painter->index_buffer);
        fv_gl.glDeleteBuffers(1, &painter->vertex_buffer);
        fv_gl.glDeleteTextures(1, &painter->texture);

        fv_free(painter);
}
