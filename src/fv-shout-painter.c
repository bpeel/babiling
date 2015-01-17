/*
 * Finvenkisto
 *
 * Copyright (C) 2015 Neil Roberts
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

#include "fv-shout-painter.h"
#include "fv-logic.h"
#include "fv-util.h"
#include "fv-matrix.h"
#include "fv-transform.h"
#include "fv-gl.h"
#include "fv-image.h"

struct fv_shout_painter_vertex {
        float x, y, z;
        float s, t;
};

struct fv_shout_painter {
        GLuint program;
        GLuint transform_uniform;

        GLuint texture;
        GLuint array;
        GLuint vertex_buffer;
};

static bool
load_texture(struct fv_shout_painter *painter)
{
        int tex_width, tex_height;
        uint8_t *tex_data;

        tex_data = fv_image_load("nekrokodilu.png",
                                 &tex_width, &tex_height,
                                 4 /* components */);
        if (tex_data == NULL)
                return false;

        fv_gl.glGenTextures(1, &painter->texture);
        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);

        fv_gl.glTexImage2D(GL_TEXTURE_2D,
                           0, /* level */
                           GL_RGBA,
                           tex_width, tex_height,
                           0, /* border */
                           GL_RGBA,
                           GL_UNSIGNED_BYTE,
                           tex_data);

        fv_free(tex_data);

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

        return true;
}

static void
make_buffer(struct fv_shout_painter *painter)
{
        typedef struct fv_shout_painter_vertex vertex;

        fv_gl.glGenVertexArrays(1, &painter->array);
        fv_gl.glBindVertexArray(painter->array);

        fv_gl.glGenBuffers(1, &painter->vertex_buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vertex_buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           sizeof (vertex) * FV_LOGIC_MAX_PLAYERS * 3,
                           NULL,
                           GL_DYNAMIC_DRAW);

        fv_gl.glEnableVertexAttribArray(0);
        fv_gl.glVertexAttribPointer(0, /* index */
                                    3, /* size */
                                    GL_FLOAT,
                                    GL_FALSE, /* normalized */
                                    sizeof (vertex),
                                    (void *) (intptr_t)
                                    offsetof(vertex, x));
        fv_gl.glEnableVertexAttribArray(1);
        fv_gl.glVertexAttribPointer(1, /* index */
                                    2, /* size */
                                    GL_FLOAT,
                                    GL_FALSE, /* normalized */
                                    sizeof (vertex),
                                    (void *) (intptr_t)
                                    offsetof(vertex, s));
}

struct fv_shout_painter *
fv_shout_painter_new(struct fv_shader_data *shader_data)
{
        struct fv_shout_painter *painter = fv_calloc(sizeof *painter);
        GLuint tex_uniform;

        painter->program =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_TEXTURE];

        if (!load_texture(painter))
                goto error;

        make_buffer(painter);

        tex_uniform = fv_gl.glGetUniformLocation(painter->program, "tex");
        fv_gl.glUseProgram(painter->program);
        fv_gl.glUniform1i(tex_uniform, 0);

        painter->transform_uniform =
                fv_gl.glGetUniformLocation(painter->program, "transform");

        return painter;

error:
        fv_free(painter);

        return NULL;
}

struct paint_closure {
        struct fv_shout_painter *painter;
        struct fv_shout_painter_vertex *buffer_map;
        int n_shouts;
};

static void
paint_cb(const struct fv_logic_shout *shout,
         void *user_data)
{
        struct paint_closure *data = user_data;
        struct fv_shout_painter *painter = data->painter;
        struct fv_shout_painter_vertex *vertex;
        float cx, cy, ccx, ccy;

        if (data->n_shouts == 0) {
                fv_gl.glBindBuffer(GL_ARRAY_BUFFER,
                                   painter->vertex_buffer);
                data->buffer_map =
                        fv_gl.glMapBufferRange(GL_ARRAY_BUFFER,
                                               0, /* offset */
                                               FV_LOGIC_MAX_PLAYERS *
                                               sizeof *vertex * 3,
                                               GL_MAP_WRITE_BIT |
                                               GL_MAP_INVALIDATE_BUFFER_BIT |
                                               GL_MAP_FLUSH_EXPLICIT_BIT);
        }

        cx = cosf(shout->direction - FV_LOGIC_SHOUT_ANGLE / 2.0f);
        cy = sinf(shout->direction - FV_LOGIC_SHOUT_ANGLE / 2.0f);
        ccx = cosf(shout->direction + FV_LOGIC_SHOUT_ANGLE / 2.0f);
        ccy = sinf(shout->direction + FV_LOGIC_SHOUT_ANGLE / 2.0f);

        vertex = data->buffer_map + data->n_shouts * 3;

        vertex->x = shout->x;
        vertex->y = shout->y;
        vertex->z = 1.5f;
        vertex->s = 0.0f;
        vertex->t = 0.5f;
        vertex++;

        vertex->x = shout->x + shout->distance * cx;
        vertex->y = shout->y + shout->distance * cy;
        vertex->z = 1.5f;
        vertex->s = 1.0f;
        vertex->t = cx >= 0.0f;
        vertex++;

        vertex->x = shout->x + shout->distance * ccx;
        vertex->y = shout->y + shout->distance * ccy;
        vertex->z = 1.5f;
        vertex->s = 1.0f;
        vertex->t = cx < 0.0f;
        vertex++;

        data->n_shouts++;
}

void
fv_shout_painter_paint(struct fv_shout_painter *painter,
                        struct fv_logic *logic,
                        const struct fv_paint_state *paint_state)
{
        struct paint_closure data;

        data.painter = painter;
        data.n_shouts = 0;

        fv_logic_for_each_shout(logic, paint_cb, &data);

        if (data.n_shouts <= 0)
                return;

        fv_gl.glFlushMappedBufferRange(GL_ARRAY_BUFFER,
                                       0, /* offset */
                                       sizeof *data.buffer_map *
                                       data.n_shouts * 3);
        fv_gl.glUnmapBuffer(GL_ARRAY_BUFFER);

        fv_gl.glUseProgram(painter->program);
        fv_gl.glUniformMatrix4fv(painter->transform_uniform,
                                 1, /* count */
                                 GL_FALSE, /* transpose */
                                 &paint_state->transform.mvp.xx);
        fv_gl.glBindVertexArray(painter->array);
        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);
        fv_gl.glEnable(GL_BLEND);
        fv_gl.glDrawArrays(GL_TRIANGLES, 0, data.n_shouts * 3);
        fv_gl.glDisable(GL_BLEND);
}

void
fv_shout_painter_free(struct fv_shout_painter *painter)
{
        fv_gl.glDeleteVertexArrays(1, &painter->array);
        fv_gl.glDeleteBuffers(1, &painter->vertex_buffer);
        fv_gl.glDeleteTextures(1, &painter->texture);

        fv_free(painter);
}
