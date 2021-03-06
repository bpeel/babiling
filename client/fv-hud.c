/*
 * Babiling
 *
 * Copyright (C) 2014, 2015 Neil Roberts
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

#include <assert.h>
#include <stdarg.h>

#include "fv-hud.h"
#include "fv-shader-data.h"
#include "fv-util.h"
#include "fv-logic.h"
#include "fv-gl.h"
#include "fv-array-object.h"
#include "fv-map-buffer.h"

struct fv_hud_vertex {
        float x, y;
        float s, t;
};

struct fv_hud {
        GLuint tex;
        int tex_width, tex_height;

        GLuint program;

        GLuint vertex_buffer;
        GLuint element_buffer;
        struct fv_array_object *array;

        int n_rectangles;
        struct fv_hud_vertex *vertex;
        int screen_width, screen_height;
};

struct fv_hud_image {
        int x, y, w, h;
};

#include "data/hud-layout.h"

#define FV_HUD_MAX_RECTANGLES 16

struct fv_hud *
fv_hud_new(struct fv_image_data *image_data,
           struct fv_shader_data *shader_data)
{
        struct fv_hud *hud;
        uint8_t *elements;
        GLuint tex_location;
        size_t element_buffer_size;
        int i;

        hud = fv_alloc(sizeof *hud);

        fv_image_data_get_size(image_data,
                               FV_IMAGE_DATA_HUD,
                               &hud->tex_width,
                               &hud->tex_height);

        hud->program = shader_data->programs[FV_SHADER_DATA_PROGRAM_HUD];

        fv_gl.glUseProgram(hud->program);
        tex_location = fv_gl.glGetUniformLocation(hud->program, "tex");
        fv_gl.glUniform1i(tex_location, 0);

        fv_gl.glGenTextures(1, &hud->tex);
        fv_gl.glBindTexture(GL_TEXTURE_2D, hud->tex);
        fv_image_data_set_2d(image_data,
                             GL_TEXTURE_2D,
                             0, /* level */
                             GL_RGBA,
                             FV_IMAGE_DATA_HUD);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MIN_FILTER,
                              GL_NEAREST);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_MAG_FILTER,
                              GL_LINEAR);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_WRAP_S,
                              GL_CLAMP_TO_EDGE);
        fv_gl.glTexParameteri(GL_TEXTURE_2D,
                              GL_TEXTURE_WRAP_T,
                              GL_CLAMP_TO_EDGE);

        hud->array = fv_array_object_new();

        fv_gl.glGenBuffers(1, &hud->element_buffer);
        fv_array_object_set_element_buffer(hud->array, hud->element_buffer);
        element_buffer_size = FV_HUD_MAX_RECTANGLES * 6 * sizeof (GLubyte);
        fv_gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                           element_buffer_size,
                           NULL, /* data */
                           GL_STATIC_DRAW);

        elements = fv_map_buffer_map(GL_ELEMENT_ARRAY_BUFFER,
                                     element_buffer_size,
                                     false /* flush_explicit */,
                                     GL_STATIC_DRAW);

        for (i = 0; i < FV_HUD_MAX_RECTANGLES; i++) {
                elements[i * 6 + 0] = i * 4 + 0;
                elements[i * 6 + 1] = i * 4 + 1;
                elements[i * 6 + 2] = i * 4 + 3;
                elements[i * 6 + 3] = i * 4 + 3;
                elements[i * 6 + 4] = i * 4 + 1;
                elements[i * 6 + 5] = i * 4 + 2;
        }

        fv_map_buffer_unmap();

        fv_gl.glGenBuffers(1, &hud->vertex_buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, hud->vertex_buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           FV_HUD_MAX_RECTANGLES * 4 *
                           sizeof (struct fv_hud_vertex),
                           NULL, /* data */
                           GL_DYNAMIC_DRAW);

        fv_array_object_set_attribute(hud->array,
                                      FV_SHADER_DATA_ATTRIB_POSITION,
                                      2, /* size */
                                      GL_FLOAT,
                                      GL_FALSE, /* normalized */
                                      sizeof (struct fv_hud_vertex),
                                      0, /* divisor */
                                      hud->vertex_buffer,
                                      offsetof(struct fv_hud_vertex, x));

        fv_array_object_set_attribute(hud->array,
                                      FV_SHADER_DATA_ATTRIB_TEX_COORD,
                                      2, /* size */
                                      GL_FLOAT,
                                      GL_FALSE, /* normalized */
                                      sizeof (struct fv_hud_vertex),
                                      0, /* divisor */
                                      hud->vertex_buffer,
                                      offsetof(struct fv_hud_vertex, s));

        return hud;
}

static void
fv_hud_begin_rectangles(struct fv_hud *hud,
                        int screen_width,
                        int screen_height)
{
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, hud->vertex_buffer);
        hud->vertex = fv_map_buffer_map(GL_ARRAY_BUFFER,
                                        sizeof (struct fv_hud_vertex) *
                                        FV_HUD_MAX_RECTANGLES * 4,
                                        true /* flush_explicit */,
                                        GL_DYNAMIC_DRAW);
        hud->n_rectangles = 0;
        hud->screen_width = screen_width;
        hud->screen_height = screen_height;
}

static void
fv_hud_add_rectangle(struct fv_hud *hud,
                     int x, int y,
                     const struct fv_hud_image *image)
{
        float x1, y1, x2, y2, s1, t1, s2, t2;

        assert(hud->n_rectangles < FV_HUD_MAX_RECTANGLES);

        x1 = x * 2.0f / hud->screen_width - 1.0f;
        y1 = y * 2.0f / hud->screen_height - 1.0f;
        x2 = (x + image->w) * 2.0f / hud->screen_width - 1.0f;
        y2 = (y + image->h) * 2.0f / hud->screen_height - 1.0f;
        s1 = image->x / (float) hud->tex_width;
        t1 = (image->y + image->h) / (float) hud->tex_height;
        s2 = (image->x + image->w) / (float) hud->tex_width;
        t2 = image->y / (float) hud->tex_height;

        hud->vertex->x = x1;
        hud->vertex->y = y1;
        hud->vertex->s = s1;
        hud->vertex->t = t1;
        hud->vertex++;

        hud->vertex->x = x2;
        hud->vertex->y = y1;
        hud->vertex->s = s2;
        hud->vertex->t = t1;
        hud->vertex++;

        hud->vertex->x = x2;
        hud->vertex->y = y2;
        hud->vertex->s = s2;
        hud->vertex->t = t2;
        hud->vertex++;

        hud->vertex->x = x1;
        hud->vertex->y = y2;
        hud->vertex->s = s1;
        hud->vertex->t = t2;
        hud->vertex++;

        hud->n_rectangles++;
}

static void
fv_hud_end_rectangles(struct fv_hud *hud)
{
        fv_map_buffer_flush(0, /* offset */
                            hud->n_rectangles * 4 *
                            sizeof (struct fv_hud_vertex));
        fv_map_buffer_unmap();

        /* There's no benefit to using multisampling for the HUD
         * because it is only drawing screen-aligned rectangles */
        if (fv_gl.have_multisampling)
                fv_gl.glDisable(GL_MULTISAMPLE);

        fv_gl.glEnable(GL_BLEND);

        fv_gl.glUseProgram(hud->program);

        fv_gl.glBindTexture(GL_TEXTURE_2D, hud->tex);

        fv_array_object_bind(hud->array);

        fv_gl_draw_range_elements(GL_TRIANGLES,
                                  0, /* start */
                                  hud->n_rectangles * 4 - 1, /* end */
                                  hud->n_rectangles * 6, /* count */
                                  GL_UNSIGNED_BYTE,
                                  NULL);

        if (fv_gl.have_multisampling)
                fv_gl.glEnable(GL_MULTISAMPLE);

        fv_gl.glDisable(GL_BLEND);
}

void
fv_hud_paint_title_screen(struct fv_hud *hud,
                          int screen_width,
                          int screen_height)
{
        fv_hud_begin_rectangles(hud, screen_width, screen_height);

        fv_hud_add_rectangle(hud,
                             hud->screen_width / 2 - fv_hud_image_title.w / 2,
                             hud->screen_height / 2 - fv_hud_image_title.h / 2,
                             &fv_hud_image_title);

        fv_hud_end_rectangles(hud);
}

void
fv_hud_free(struct fv_hud *hud)
{
        fv_gl.glDeleteBuffers(1, &hud->vertex_buffer);
        fv_gl.glDeleteBuffers(1, &hud->element_buffer);
        fv_array_object_free(hud->array);
        fv_gl.glDeleteTextures(1, &hud->tex);
        fv_free(hud);
}
