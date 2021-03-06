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

#include <math.h>
#include <limits.h>
#include <assert.h>
#include <string.h>

#include "fv-map-painter.h"
#include "fv-map.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-gl.h"
#include "fv-model.h"
#include "fv-array-object.h"
#include "fv-map-buffer.h"

#define FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE 64

#define FV_MAP_PAINTER_N_MODELS FV_N_ELEMENTS(fv_map_painter_models)
#define FV_MAP_PAINTER_N_TEXTURES FV_N_ELEMENTS(fv_map_painter_textures)

/* Maximum number of special instances to render in one draw call */
#define FV_MAP_PAINTER_MAX_SPECIALS 16

/* The normals for the map are only ever one of the the following
 * directions so instead of encoding each component of the normal in
 * the vertex we just encode a byte with one of the following values
 * and let the vertex shader expand it out.
 */
#define FV_MAP_PAINTER_NORMAL_UP 0
#define FV_MAP_PAINTER_NORMAL_NORTH 166
#define FV_MAP_PAINTER_NORMAL_EAST 255
#define FV_MAP_PAINTER_NORMAL_SOUTH 90
#define FV_MAP_PAINTER_NORMAL_WEST 3

struct fv_map_painter_model {
        const char *filename;
        bool has_texture;
};

static struct fv_map_painter_model
fv_map_painter_models[] = {
        { "table.ply", false },
        { "chair.ply", false },
        { "barrel.ply", false },
        { "bar.ply", true },
        { "flag-post-1-2-m.ply", true },
        { "flag-post-2-3-m.ply", true },
        { "flag-post-7-10-n.ply", true },
        { "flag-post-3-5-m.ply", true },
};

static enum fv_image_data_image
fv_map_painter_textures[] = {
        FV_IMAGE_DATA_BAR_TEXTURE,
        FV_IMAGE_DATA_UNION_JACK,
        FV_IMAGE_DATA_USA_FLAG,
        FV_IMAGE_DATA_SPANISH_FLAG,
        FV_IMAGE_DATA_ITALIAN_FLAG,
        FV_IMAGE_DATA_TRICOLOUR,
        FV_IMAGE_DATA_PORTUGUESE_FLAG,
        FV_IMAGE_DATA_BRAZILIAN_FLAG,
        FV_IMAGE_DATA_JAPANESE_FLAG,
        FV_IMAGE_DATA_CHINESE_FLAG,
        FV_IMAGE_DATA_RUSSIAN_FLAG,
        FV_IMAGE_DATA_DUTCH_FLAG,
        FV_IMAGE_DATA_SOUTH_KOREAN_FLAG,
        FV_IMAGE_DATA_GERMAN_FLAG
};

struct fv_map_painter_program {
        GLuint id;
        GLuint modelview_transform;
        GLuint normal_transform;
};

struct fv_map_painter_tile {
        size_t offset;
        int count;
        int min, max;
};

struct fv_map_painter {
        GLuint vertices_buffer;
        GLuint indices_buffer;
        struct fv_array_object *array;
        struct fv_map_painter_tile tiles[FV_MAP_TILES_X *
                                         FV_MAP_TILES_Y];

        struct fv_map_painter_program map_program;
        struct fv_map_painter_program color_program;
        struct fv_map_painter_program texture_program;

        GLuint instance_buffer;
        struct instance *instance_buffer_map;
        int n_instances;
        int current_model;
        int current_texture;

        struct fv_model models[FV_MAP_PAINTER_N_MODELS];
        GLuint textures[FV_MAP_PAINTER_N_TEXTURES];

        GLuint texture;

        const struct fv_map *map;
};

struct vertex {
        uint8_t x, y, z;
        /* The normal is encoded as the fourth component of the
         * position rather than its own component because I read
         * somewhere that all attributes should be aligned to a float.
         * I'm not sure if this is true or not but it's not really
         * difficult to do so we might as well play it safe.
         */
        uint8_t normal;
        uint16_t s, t;
};

struct instance {
        float modelview[4 * 4];
        float normal_transform[3 * 3];
};

struct tile_data {
        struct fv_buffer indices;
        struct fv_buffer vertices;
        int texture_width, texture_height;
        int image_width, image_height;
};

static float
get_block_height(fv_map_block_t block)
{
        switch (FV_MAP_GET_BLOCK_TYPE(block)) {
        case FV_MAP_BLOCK_TYPE_FULL_WALL:
                return 2.0f;
        case FV_MAP_BLOCK_TYPE_HALF_WALL:
                return 1.0f;
        default:
                return 0.0f;
        }
}

static float
get_position_height(struct fv_map_painter *painter,
                    int x, int y)
{
        if (x < 0 || x >= FV_MAP_WIDTH ||
            y < 0 || y >= FV_MAP_HEIGHT)
                return 0.0f;

        return get_block_height(painter->map->blocks[y * FV_MAP_WIDTH + x]);
}

static struct vertex *
reserve_quad(struct tile_data *data)
{
        struct vertex *v;
        uint16_t *idx;
        size_t v1, i1;

        v1 = data->vertices.length / sizeof (struct vertex);
        fv_buffer_set_length(&data->vertices,
                             sizeof (struct vertex) * (v1 + 4));
        v = (struct vertex *) data->vertices.data + v1;

        i1 = data->indices.length / sizeof (uint16_t);
        fv_buffer_set_length(&data->indices,
                             sizeof (uint16_t) * (i1 + 6));
        idx = (uint16_t *) data->indices.data + i1;

        *(idx++) = v1 + 0;
        *(idx++) = v1 + 1;
        *(idx++) = v1 + 2;
        *(idx++) = v1 + 2;
        *(idx++) = v1 + 1;
        *(idx++) = v1 + 3;

        return v;
}

static struct vertex *
add_horizontal_side(struct tile_data *data,
                    int y,
                    int x1, int z1,
                    int x2, int z2)
{
        struct vertex *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++)
                v[i].y = y;

        v[0].x = x1;
        v[0].z = z1;
        v[1].x = x2;
        v[1].z = z1;
        v[2].x = x1;
        v[2].z = z2;
        v[3].x = x2;
        v[3].z = z2;

        return v;
}

static struct vertex *
add_vertical_side(struct tile_data *data,
                  int x,
                  int y1, int z1,
                  int y2, int z2)
{
        struct vertex *v = reserve_quad(data);
        int i;

        for (i = 0; i < 4; i++)
                v[i].x = x;

        v[0].y = y1;
        v[0].z = z1;
        v[1].y = y2;
        v[1].z = z1;
        v[2].y = y1;
        v[2].z = z2;
        v[3].y = y2;
        v[3].z = z2;

        return v;
}

static void
set_tex_coords_for_image(struct fv_map_painter *painter,
                         struct tile_data *data,
                         struct vertex v[4],
                         int image,
                         int height)
{
        int blocks_h = (data->image_height /
                        FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE);
        int is1 = image / blocks_h * FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE * 2;
        int it1 = image % blocks_h * FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE;
        uint16_t s1 = is1 * (UINT16_MAX - 1) / data->texture_width;
        uint16_t t1 = it1 * (UINT16_MAX - 1) / data->texture_height;
        uint16_t s2 = ((is1 + FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE) *
                       (UINT16_MAX - 1) / data->texture_width);
        uint16_t t2 = ((it1 + FV_MAP_PAINTER_TEXTURE_BLOCK_SIZE * height) *
                       (UINT16_MAX - 1) / data->texture_height);

        v[0].s = s1;
        v[0].t = t2;
        v[1].s = s2;
        v[1].t = t2;
        v[2].s = s1;
        v[2].t = t1;
        v[3].s = s2;
        v[3].t = t1;
}

static void
set_normals(struct vertex *v,
            int8_t value)
{
        int i;

        for (i = 0; i < 4; i++)
                v[i].normal = value;
}

static void
generate_square(struct fv_map_painter *painter,
                struct tile_data *data,
                int x, int y)
{
        fv_map_block_t block = painter->map->blocks[y * FV_MAP_WIDTH + x];
        struct vertex *v;
        int i;
        int z, oz;

        v = reserve_quad(data);

        z = get_block_height(block);

        set_tex_coords_for_image(painter,
                                 data,
                                 v,
                                 FV_MAP_GET_BLOCK_TOP_IMAGE(block),
                                 1.0f);
        set_normals(v, FV_MAP_PAINTER_NORMAL_UP);

        for (i = 0; i < 4; i++)
                v[i].z = z;

        v->x = x;
        v->y = y;
        v++;
        v->x = x + 1;
        v->y = y;
        v++;
        v->x = x;
        v->y = y + 1;
        v++;
        v->x = x + 1;
        v->y = y + 1;

        /* Add the side walls */
        if (z > (oz = get_position_height(painter, x, y + 1))) {
                v = add_horizontal_side(data, y + 1, x + 1, oz, x, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_NORTH);
                set_tex_coords_for_image(painter,
                                         data,
                                         v,
                                         FV_MAP_GET_BLOCK_NORTH_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(painter, x, y - 1))) {
                v = add_horizontal_side(data, y, x, oz, x + 1, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_SOUTH);
                set_tex_coords_for_image(painter,
                                         data,
                                         v,
                                         FV_MAP_GET_BLOCK_SOUTH_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(painter, x - 1, y))) {
                v = add_vertical_side(data, x, y + 1, oz, y, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_WEST);
                set_tex_coords_for_image(painter,
                                         data,
                                         v,
                                         FV_MAP_GET_BLOCK_WEST_IMAGE(block),
                                         z - oz);
        }
        if (z > (oz = get_position_height(painter, x + 1, y))) {
                v = add_vertical_side(data, x + 1, y, oz, y + 1, z);
                set_normals(v, FV_MAP_PAINTER_NORMAL_EAST);
                set_tex_coords_for_image(painter,
                                         data,
                                         v,
                                         FV_MAP_GET_BLOCK_EAST_IMAGE(block),
                                         z - oz);
        }
}

static void
generate_tile(struct fv_map_painter *painter,
              struct tile_data *data,
              int tx, int ty)
{
        int x, y;

        for (y = 0; y < FV_MAP_TILE_HEIGHT; y++) {
                for (x = 0; x < FV_MAP_TILE_WIDTH; x++) {
                        generate_square(painter,
                                        data,
                                        tx * FV_MAP_TILE_WIDTH + x,
                                        ty * FV_MAP_TILE_HEIGHT + y);
                }
        }

}

static void
load_textures(struct fv_map_painter *painter,
              struct fv_image_data *image_data)
{
        int i;

        for (i = 0; i < FV_MAP_PAINTER_N_TEXTURES; i++) {
                fv_gl.glGenTextures(1, &painter->textures[i]);
                fv_gl.glBindTexture(GL_TEXTURE_2D,
                                    painter->textures[i]);
                fv_image_data_set_2d(image_data,
                                     GL_TEXTURE_2D,
                                     0, /* level */
                                     GL_RGB,
                                     fv_map_painter_textures[i]);
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
}

static bool
load_models(struct fv_map_painter *painter)
{
        struct fv_model *model;
        struct fv_map_painter_program *program;
        bool res;
        GLint transform;
        size_t offset;
        int i, j;

        for (i = 0; i < FV_MAP_PAINTER_N_MODELS; i++) {
                model = painter->models + i;

                res = fv_model_load(model, fv_map_painter_models[i].filename);
                if (!res)
                        goto error;

                if (!fv_gl.have_instanced_arrays)
                        continue;

                if (fv_map_painter_models[i].has_texture)
                        program = &painter->texture_program;
                else
                        program = &painter->color_program;

                transform = program->modelview_transform;

                for (j = 0; j < 4; j++) {
                        offset = offsetof(struct instance, modelview[j * 4]);
                        fv_array_object_set_attribute(model->array,
                                                      transform + j,
                                                      4, /* size */
                                                      GL_FLOAT,
                                                      GL_FALSE, /* normalized */
                                                      sizeof (struct instance),
                                                      1, /* divisor */
                                                      painter->instance_buffer,
                                                      offset);
                }

                transform = program->normal_transform;

                for (j = 0; j < 3; j++) {
                        offset = offsetof(struct instance,
                                          normal_transform[j * 3]);
                        fv_array_object_set_attribute(model->array,
                                                      transform + j,
                                                      3, /* size */
                                                      GL_FLOAT,
                                                      GL_FALSE, /* normalized */
                                                      sizeof (struct instance),
                                                      1, /* divisor */
                                                      painter->instance_buffer,
                                                      offset);
                }
        }

        return true;

error:
        while (--i >= 0)
                fv_model_destroy(&painter->models[i]);

        return false;
}

static int
smallest_pot(int x)
{
        int y = 1;

        while (y < x)
                y *= 2;

        return y;
}

static void
init_programs(struct fv_map_painter *painter,
              struct fv_shader_data *shader_data)
{
        painter->map_program.id =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_MAP];
        painter->map_program.modelview_transform =
                fv_gl.glGetUniformLocation(painter->map_program.id,
                                           "transform");
        painter->map_program.normal_transform =
                fv_gl.glGetUniformLocation(painter->map_program.id,
                                           "normal_transform");
        painter->color_program.id =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_SPECIAL_COLOR];
        painter->texture_program.id =
                shader_data->programs[FV_SHADER_DATA_PROGRAM_SPECIAL_TEXTURE];

        if (fv_gl.have_instanced_arrays) {
                painter->color_program.modelview_transform =
                        fv_gl.glGetAttribLocation(painter->color_program.id,
                                                  "transform");
                painter->color_program.normal_transform =
                        fv_gl.glGetAttribLocation(painter->color_program.id,
                                                  "normal_transform");
                painter->texture_program.modelview_transform =
                        fv_gl.glGetAttribLocation(painter->texture_program.id,
                                                  "transform");
                painter->texture_program.normal_transform =
                        fv_gl.glGetAttribLocation(painter->texture_program.id,
                                                  "normal_transform");
        } else {
                painter->color_program.modelview_transform =
                        fv_gl.glGetUniformLocation(painter->color_program.id,
                                                   "transform");
                painter->color_program.normal_transform =
                        fv_gl.glGetUniformLocation(painter->color_program.id,
                                                   "normal_transform");
                painter->texture_program.modelview_transform =
                        fv_gl.glGetUniformLocation(painter->texture_program.id,
                                                   "transform");
                painter->texture_program.normal_transform =
                        fv_gl.glGetUniformLocation(painter->texture_program.id,
                                                   "normal_transform");
        }
}

struct fv_map_painter *
fv_map_painter_new(const struct fv_map *map,
                   struct fv_image_data *image_data,
                   struct fv_shader_data *shader_data)
{
        struct fv_map_painter *painter;
        struct tile_data data;
        struct fv_map_painter_tile *tile;
        int first, tx, ty;
        GLuint tex_uniform;

        painter = fv_alloc(sizeof *painter);

        painter->map = map;

        if (fv_gl.have_instanced_arrays) {
                fv_gl.glGenBuffers(1, &painter->instance_buffer);
                fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->instance_buffer);
                fv_gl.glBufferData(GL_ARRAY_BUFFER,
                                   sizeof (struct instance) *
                                   FV_MAP_PAINTER_MAX_SPECIALS,
                                   NULL, /* data */
                                   GL_DYNAMIC_DRAW);
        }

        init_programs(painter, shader_data);

        if (!load_models(painter))
                goto error_instance_buffer;

        load_textures(painter, image_data);

        fv_image_data_get_size(image_data,
                               FV_IMAGE_DATA_MAP_TEXTURE,
                               &data.image_width, &data.image_height);

        if (fv_gl.have_npot_mipmaps) {
                data.texture_width = data.image_width;
                data.texture_height = data.image_height;
        } else {
                data.texture_width = smallest_pot(data.image_width);
                data.texture_height = smallest_pot(data.image_height);
        }

        fv_gl.glGenTextures(1, &painter->texture);
        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);
        fv_gl.glTexImage2D(GL_TEXTURE_2D,
                           0, /* level */
                           GL_RGB,
                           data.texture_width, data.texture_height,
                           0, /* border */
                           GL_RGB,
                           GL_UNSIGNED_BYTE,
                           NULL);
        fv_image_data_set_sub_2d(image_data,
                                 GL_TEXTURE_2D,
                                 0, /* level */
                                 0, 0, /* x/y offset */
                                 FV_IMAGE_DATA_MAP_TEXTURE);

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

        tex_uniform = fv_gl.glGetUniformLocation(painter->map_program.id,
                                                 "tex");
        fv_gl.glUseProgram(painter->map_program.id);
        fv_gl.glUniform1i(tex_uniform, 0);

        tex_uniform = fv_gl.glGetUniformLocation(painter->texture_program.id,
                                                 "tex");
        fv_gl.glUseProgram(painter->texture_program.id);
        fv_gl.glUniform1i(tex_uniform, 0);

        fv_buffer_init(&data.vertices);
        fv_buffer_init(&data.indices);

        tile = painter->tiles;

        for (ty = 0; ty < FV_MAP_TILES_Y; ty++) {
                for (tx = 0; tx < FV_MAP_TILES_X; tx++) {
                        first = data.indices.length / sizeof (uint16_t);
                        tile->min = (data.vertices.length /
                                     sizeof (struct vertex));
                        tile->offset = data.indices.length;
                        generate_tile(painter, &data, tx, ty);
                        tile->max = (data.vertices.length /
                                     sizeof (struct vertex)) - 1;
                        tile->count = (data.indices.length /
                                       sizeof (uint16_t) -
                                       first);
                        tile++;
                }
        }

        assert(data.vertices.length / sizeof (struct vertex) < 65536);

        painter->array = fv_array_object_new();

        fv_gl.glGenBuffers(1, &painter->vertices_buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, painter->vertices_buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           data.vertices.length,
                           data.vertices.data,
                           GL_STATIC_DRAW);

        fv_array_object_set_attribute(painter->array,
                                      FV_SHADER_DATA_ATTRIB_POSITION,
                                      4, /* size */
                                      GL_UNSIGNED_BYTE,
                                      GL_FALSE, /* normalized */
                                      sizeof (struct vertex),
                                      0, /* divisor */
                                      painter->vertices_buffer,
                                      offsetof(struct vertex, x));

        fv_array_object_set_attribute(painter->array,
                                      FV_SHADER_DATA_ATTRIB_TEX_COORD,
                                      2, /* size */
                                      GL_UNSIGNED_SHORT,
                                      GL_TRUE, /* normalized */
                                      sizeof (struct vertex),
                                      0, /* divisor */
                                      painter->vertices_buffer,
                                      offsetof(struct vertex, s));

        fv_gl.glGenBuffers(1, &painter->indices_buffer);
        fv_array_object_set_element_buffer(painter->array,
                                           painter->indices_buffer);
        fv_gl.glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                           data.indices.length,
                           data.indices.data,
                           GL_STATIC_DRAW);

        fv_buffer_destroy(&data.indices);
        fv_buffer_destroy(&data.vertices);

        return painter;

error_instance_buffer:
        if (fv_gl.have_instanced_arrays)
                fv_gl.glDeleteBuffers(1, &painter->instance_buffer);
        fv_free(painter);

        return NULL;
}

static void
flush_specials(struct fv_map_painter *painter)
{
        const struct fv_model *model;
        struct fv_map_painter_program *program;
        GLuint tex;

        if (painter->n_instances == 0)
                return;

        model = painter->models + painter->current_model;

        fv_map_buffer_flush(0, /* offset */
                            sizeof (struct instance) *
                            painter->n_instances);
        fv_map_buffer_unmap();

        if (painter->current_texture == FV_MAP_NO_TEXTURE) {
                program = &painter->color_program;
        } else {
                tex = painter->textures[painter->current_texture];
                fv_gl.glBindTexture(GL_TEXTURE_2D, tex);
                program = &painter->texture_program;
        }
        fv_gl.glUseProgram(program->id);

        fv_array_object_bind(model->array);

        fv_gl.glDrawElementsInstanced(GL_TRIANGLES,
                                      model->n_indices,
                                      GL_UNSIGNED_SHORT,
                                      NULL, /* offset */
                                      painter->n_instances);

        painter->n_instances = 0;
}

static void
paint_special(struct fv_map_painter *painter,
              const struct fv_map_special *special,
              const struct fv_transform *transform_in)
{
        struct fv_transform transform = *transform_in;
        struct fv_map_painter_program *program;
        struct instance *instance;
        GLuint tex;

        if (painter->current_model != special->num ||
            painter->current_texture != special->texture ||
            painter->n_instances >= FV_MAP_PAINTER_MAX_SPECIALS)
                flush_specials(painter);

        fv_matrix_translate(&transform.modelview,
                            special->x + 0.5f,
                            special->y + 0.5f,
                            0.0f);
        if (special->rotation != 0)
                fv_matrix_rotate(&transform.modelview,
                                 special->rotation * 360.0f /
                                 (UINT16_MAX + 1.0f),
                                 0.0f, 0.0f, 1.0f);

        fv_transform_dirty(&transform);
        fv_transform_ensure_mvp(&transform);
        fv_transform_ensure_normal_transform(&transform);

        if (fv_gl.have_instanced_arrays) {
                if (painter->n_instances == 0) {
                        fv_gl.glBindBuffer(GL_ARRAY_BUFFER,
                                           painter->instance_buffer);
                        painter->instance_buffer_map =
                                fv_map_buffer_map(GL_ARRAY_BUFFER,
                                                  sizeof (struct instance) *
                                                  FV_MAP_PAINTER_MAX_SPECIALS,
                                                  true /* flush_explicit */,
                                                  GL_DYNAMIC_DRAW);
                        painter->current_model = special->num;
                        painter->current_texture = special->texture;
                }

                instance = painter->instance_buffer_map + painter->n_instances;
                memcpy(instance->modelview,
                       &transform.mvp.xx,
                       sizeof instance->modelview);
                memcpy(instance->normal_transform,
                       transform.normal_transform,
                       sizeof instance->normal_transform);

                painter->n_instances++;
        } else {
                if (special->texture == FV_MAP_NO_TEXTURE) {
                        program = &painter->color_program;
                } else {
                        tex = painter->textures[special->texture];
                        fv_gl.glBindTexture(GL_TEXTURE_2D, tex);
                        program = &painter->texture_program;
                }
                fv_gl.glUseProgram(program->id);
                fv_gl.glUniformMatrix4fv(program->modelview_transform,
                                         1, /* count */
                                         GL_FALSE, /* transpose */
                                         &transform.mvp.xx);
                fv_gl.glUniformMatrix3fv(program->normal_transform,
                                         1, /* count */
                                         GL_FALSE, /* transpose */
                                         transform.normal_transform);
                fv_model_paint(&painter->models[special->num]);
        }
}

void
fv_map_painter_paint(struct fv_map_painter *painter,
                     struct fv_paint_state *paint_state)
{
        int x_min, x_max, y_min, y_max;
        int idx_min;
        int idx_max;
        const struct fv_map_painter_tile *tile = NULL;
        int count;
        int y, x, i;
        const struct fv_map_tile *map_tile;

        x_min = floorf((paint_state->center_x - paint_state->visible_w / 2.0f) /
                       FV_MAP_TILE_WIDTH);
        x_max = ceilf((paint_state->center_x + paint_state->visible_w / 2.0f) /
                      FV_MAP_TILE_WIDTH);
        y_min = floorf((paint_state->center_y - paint_state->visible_h / 2.0f) /
                       FV_MAP_TILE_HEIGHT);
        y_max = ceilf((paint_state->center_y + paint_state->visible_h / 2.0f) /
                      FV_MAP_TILE_HEIGHT);

        if (x_min < 0)
                x_min = 0;
        if (x_max > FV_MAP_TILES_X)
                x_max = FV_MAP_TILES_X;
        if (y_min < 0)
                y_min = 0;
        if (y_max > FV_MAP_TILES_Y)
                y_max = FV_MAP_TILES_Y;

        if (y_min >= y_max || x_min >= x_max)
                return;

        fv_gl.glEnable(GL_DEPTH_TEST);

        painter->n_instances = 0;
        painter->current_model = 0;
        painter->current_texture = 0;

        for (y = y_min; y < y_max; y++) {
                for (x = x_max - 1; x >= x_min; x--) {
                        map_tile = painter->map->tiles + y * FV_MAP_TILES_X + x;
                        for (i = 0; i < map_tile->n_specials; i++) {
                                paint_special(painter,
                                              map_tile->specials + i,
                                              &paint_state->transform);
                        }
                }
        }

        flush_specials(painter);

        fv_transform_ensure_mvp(&paint_state->transform);
        fv_transform_ensure_normal_transform(&paint_state->transform);

        fv_gl.glUseProgram(painter->map_program.id);
        fv_gl.glUniformMatrix4fv(painter->map_program.modelview_transform,
                                 1, /* count */
                                 GL_FALSE, /* transpose */
                                 &paint_state->transform.mvp.xx);
        fv_gl.glUniformMatrix3fv(painter->map_program.normal_transform,
                                 1, /* count */
                                 GL_FALSE, /* transpose */
                                 paint_state->transform.normal_transform);

        fv_gl.glBindTexture(GL_TEXTURE_2D, painter->texture);

        fv_array_object_bind(painter->array);

        for (y = y_min; y < y_max; y++) {
                count = 0;
                idx_min = INT_MAX;
                idx_max = INT_MIN;

                for (x = x_max - 1; x >= x_min; x--) {
                        tile = painter->tiles +
                                y * FV_MAP_TILES_X + x;
                        count += tile->count;
                        if (tile->min < idx_min)
                                idx_min = tile->min;
                        if (tile->max > idx_max)
                                idx_max = tile->max;
                }

                fv_gl_draw_range_elements(GL_TRIANGLES,
                                          idx_min, idx_max,
                                          count,
                                          GL_UNSIGNED_SHORT,
                                          (void *) (intptr_t)
                                          tile->offset);
        }

        fv_gl.glDisable(GL_DEPTH_TEST);
}

void
fv_map_painter_free(struct fv_map_painter *painter)
{
        int i;

        fv_gl.glDeleteTextures(1, &painter->texture);
        fv_array_object_free(painter->array);
        fv_gl.glDeleteBuffers(1, &painter->vertices_buffer);
        fv_gl.glDeleteBuffers(1, &painter->indices_buffer);

        if (fv_gl.have_instanced_arrays)
                fv_gl.glDeleteBuffers(1, &painter->instance_buffer);

        for (i = 0; i < FV_MAP_PAINTER_N_MODELS; i++)
                fv_model_destroy(&painter->models[i]);
        fv_gl.glDeleteTextures(FV_MAP_PAINTER_N_TEXTURES,
                               painter->textures);

        fv_free(painter);
}
