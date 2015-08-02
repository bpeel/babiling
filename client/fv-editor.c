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

#include <stdio.h>
#include <SDL.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#include "fv-image-data.h"
#include "fv-shader-data.h"
#include "fv-gl.h"
#include "fv-util.h"
#include "fv-map.h"
#include "fv-error-message.h"
#include "fv-map-painter.h"
#include "fv-array-object.h"
#include "fv-data.h"

#define MIN_GL_MAJOR_VERSION 3
#define MIN_GL_MINOR_VERSION 3
#define CORE_GL_MAJOR_VERSION MIN_GL_MAJOR_VERSION
#define CORE_GL_MINOR_VERSION MIN_GL_MINOR_VERSION

/* 40° vertical FOV angle when the height of the display is 2.0 */
#define FV_EDITOR_NEAR_PLANE 2.7474774194546225f
#define FV_EDITOR_FAR_PLANE 40.0f

#define FV_EDITOR_MIN_DISTANCE 10
#define FV_EDITOR_MAX_DISTANCE 30
#define FV_EDITOR_SCALE 0.7f

static const char
cursor_vertex_shader[] =
        "#version 330\n"
        "\n"
        "layout(location = 0) in vec3 position;\n"
        "uniform mat4 transform;\n"
        "\n"
        "void\n"
        "main()\n"
        "{\n"
        "        gl_Position = transform * vec4(position, 1.0);\n"
        "}\n";

static const char
cursor_fragment_shader[] =
        "#version 330\n"
        "\n"
        "layout(location = 0) out vec4 color;\n"
        "\n"
        "void\n"
        "main()\n"
        "{\n"
        "        color = vec4(0.6, 0.6, 0.8, 0.8);\n"
        "}\n";

struct color_map {
        int r, g, b, value;
};

static const struct color_map
top_map[] = {
        { 0xbb, 0x99, 0x55, 2 }, /* brick flooring */
        { 0xcc, 0x99, 0x00, 0 }, /* wall top */
        { 0x44, 0x55, 0x22, 4 }, /* grass */
        { -1 }
};

static const struct color_map
side_map[] = {
        { 0x66, 0x44, 0x44, 6 }, /* brick wall */
        { 0x99, 0xcc, 0xcc, 9 }, /* inner wall */
        { 0x55, 0x66, 0xcc, 12 }, /* welcome poster 1 */
        { 0x55, 0x66, 0xdd, 14 }, /* welcome poster 2 */
        { -1 }
};

static const struct color_map
special_map[] = {
        { 0xdd, 0x55, 0x33, 0 }, /* table */
        { 0x00, 0x00, 0xee, 1 }, /* chair */
        { 0xbb, 0x33, 0xbb, 2 }, /* barrel */
        { 0xbb, 0xaa, 0xaa, 3 }, /* bar */
        { 0x00, 0x00, 0x00, -1 }, /* covered by a neighbouring special */
        { -1 }
};

struct data {
        struct fv_image_data *image_data;
        Uint32 image_data_event;

        struct {
                struct fv_shader_data shader_data;
                bool shader_data_loaded;
                struct fv_map_painter *map_painter;
        } graphics;

        struct fv_map map;

        SDL_Window *window;
        SDL_GLContext gl_context;

        int x_pos;
        int y_pos;
        int distance;
        int rotation;

        GLuint cursor_program;
        GLuint cursor_buffer;
        struct fv_array_object *cursor_array_object;
        GLint cursor_transform_uniform;

        struct fv_map_painter *map_painter;

        bool quit;

        bool redraw_queued;
};

struct cursor_vertex {
        float x, y, z;
};

static void
queue_redraw(struct data *data)
{
        data->redraw_queued = true;
}

static void
destroy_map_painter(struct data *data)
{
        if (data->graphics.map_painter) {
                fv_map_painter_free(data->graphics.map_painter);
                data->graphics.map_painter = NULL;
        }
}

static bool
create_map_painter(struct data *data)
{
        data->map_painter = fv_map_painter_new(&data->map,
                                               data->image_data,
                                               &data->graphics.shader_data);

        return data->map_painter != NULL;
}

static void
redraw_map(struct data *data)
{
        if (data->image_data == NULL)
                return;

        destroy_map_painter(data);
        create_map_painter(data);
        queue_redraw(data);
}

static struct fv_map_special *
get_special(struct data *data,
            int x, int y)
{
        int tx = x / FV_MAP_TILE_WIDTH;
        int ty = y / FV_MAP_TILE_HEIGHT;
        struct fv_map_tile *tile =
                data->map.tiles + tx + ty * FV_MAP_TILES_X;
        struct fv_map_special *special;
        int i;

        for (i = 0; i < tile->n_specials; i++) {
                special = tile->specials + i;
                if (special->x == x && special->y == y)
                        return special;
        }

        return NULL;
}

static void
set_special(struct data *data,
            int x, int y,
            int special_num)
{
        struct fv_map_special *special = get_special(data, x, y);
        struct fv_map_tile *tile;
        int tx, ty;

        if (special) {
                special->num = special_num;
                return;
        }

        tx = x / FV_MAP_TILE_WIDTH;
        ty = y / FV_MAP_TILE_HEIGHT;
        tile = data->map.tiles + tx + ty * FV_MAP_TILES_X;

        if (tile->n_specials < FV_MAP_MAX_SPECIALS) {
                special = tile->specials + tile->n_specials++;
                special->num = special_num;
                special->x = x;
                special->y = y;
                special->rotation = 0;
        }
}

static void
update_position(struct data *data,
                int x_offset,
                int y_offset)
{
        int t;

        switch (data->rotation) {
        case 1:
                t = x_offset;
                x_offset = y_offset;
                y_offset = -t;
                break;

        case 2:
                x_offset = -x_offset;
                y_offset = -y_offset;
                break;

        case 3:
                t = x_offset;
                x_offset = -y_offset;
                y_offset = t;
                break;
        }

        data->x_pos += x_offset;
        data->y_pos += y_offset;

        if (data->x_pos < 0)
                data->x_pos = 0;
        else if (data->x_pos >= FV_MAP_WIDTH)
                data->x_pos = FV_MAP_WIDTH - 1;

        if (data->y_pos < 0)
                data->y_pos = 0;
        else if (data->y_pos >= FV_MAP_HEIGHT)
                data->y_pos = FV_MAP_HEIGHT - 1;

        queue_redraw(data);
}

static void
update_distance(struct data *data,
                int offset)
{
        data->distance += offset;

        if (data->distance > FV_EDITOR_MAX_DISTANCE)
                data->distance = FV_EDITOR_MAX_DISTANCE;
        else if (data->distance < FV_EDITOR_MIN_DISTANCE)
                data->distance = FV_EDITOR_MIN_DISTANCE;

        queue_redraw(data);
}

static void
toggle_height(struct data *data)
{
        fv_map_block_t *block = (data->map.blocks +
                                 data->x_pos +
                                 data->y_pos * FV_MAP_WIDTH);
        int new_type;

        switch (FV_MAP_GET_BLOCK_TYPE(*block)) {
        case FV_MAP_BLOCK_TYPE_FLOOR:
                new_type = FV_MAP_BLOCK_TYPE_HALF_WALL;
                break;
        case FV_MAP_BLOCK_TYPE_HALF_WALL:
                new_type = FV_MAP_BLOCK_TYPE_FULL_WALL;
                break;
        case FV_MAP_BLOCK_TYPE_FULL_WALL:
                new_type = FV_MAP_BLOCK_TYPE_FLOOR;
                break;
        default:
                /* Don't modify special blocks */
                return;
        }

        *block = (*block & ~FV_MAP_BLOCK_TYPE_MASK) | new_type;

        redraw_map(data);
}

static const struct color_map *
lookup_color(const struct color_map *map,
             int value)
{
        int i;

        for (i = 0; map[i].r != -1; i++) {
                if (map[i].value == value)
                        return map + i;
        }

        return map;
}

static void
next_special(struct data *data)
{
        struct fv_map_special *special =
                get_special(data, data->x_pos, data->y_pos);
        const struct color_map *color;
        int special_num;

        if (special == NULL) {
                special_num = 0;
        } else {
                color = lookup_color(special_map, special->num) + 1;
                if (color->r == -1 || color->value == -1)
                        special_num = 0;
                else
                        special_num = color->value;
        }

        set_special(data, data->x_pos, data->y_pos, special_num);

        redraw_map(data);
}

static void
set_pixel(uint8_t *buf,
          int x, int y,
          int ox, int oy,
          const struct color_map *color)
{
        y = FV_MAP_HEIGHT - 1 - y;
        buf += (x * 4 + ox) * 3 + (y * 4 + oy) * FV_MAP_WIDTH * 4 * 3;
        buf[0] = color->r;
        buf[1] = color->g;
        buf[2] = color->b;
}

static void
set_special_colors(uint8_t *buf,
                   int x, int y,
                   const struct color_map *color)
{
        set_pixel(buf, x, y, 2, 1, color);
        set_pixel(buf, x, y, 0, 0, color);
        set_pixel(buf, x, y, 3, 0, color);
        set_pixel(buf, x, y, 0, 3, color);
        set_pixel(buf, x, y, 3, 3, color);
}

static void
save_block(uint8_t *buf,
           int x, int y,
           fv_map_block_t block)
{
        const struct color_map *color;
        int type;
        int i, j;

        color = lookup_color(top_map, FV_MAP_GET_BLOCK_TOP_IMAGE(block));

        for (i = 0; i < 4; i++) {
                for (j = 0; j < 4; j++) {
                        set_pixel(buf, x, y, i, j, color);
                }
        }

        type = FV_MAP_GET_BLOCK_TYPE(block);

        if (type == FV_MAP_BLOCK_TYPE_SPECIAL) {
                color = lookup_color(special_map, -1);
                set_special_colors(buf, x, y, color);
        } else if (type != FV_MAP_BLOCK_TYPE_FLOOR) {
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_NORTH_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, i, 0, color);
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_EAST_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, 3, i, color);
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_SOUTH_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, i + 1, 3, color);
                color = lookup_color(side_map,
                                     FV_MAP_GET_BLOCK_WEST_IMAGE(block));
                for (i = 0; i < 3; i++)
                        set_pixel(buf, x, y, 0, i + 1, color);

                if (type == FV_MAP_BLOCK_TYPE_HALF_WALL)
                        set_pixel(buf, x, y, 1, 2, color);
        }
}

static void
save_special(struct data *data,
             uint8_t *buf,
             const struct fv_map_special *special)
{
        const struct color_map *color;
        struct color_map rotation_color;

        color = lookup_color(special_map, special->num);
        set_special_colors(buf, special->x, special->y, color);

        if (special->rotation) {
                rotation_color.r = special->rotation >> 8;
                rotation_color.g = special->rotation & 0xff;
                rotation_color.b = 0;
                set_pixel(buf, special->x, special->y, 2, 2, &rotation_color);
        }
}

static void
save(struct data *data)
{
        uint8_t *buf;
        char *filename;
        FILE *out;
        int y, x, i, j;

        filename = fv_data_get_filename("../fv-map.ppm");

        if (filename == NULL) {
                fv_error_message("error getting save filename");
                return;
        }

        buf = fv_alloc(FV_MAP_WIDTH * FV_MAP_HEIGHT * 4 * 4 * 3);

        for (y = 0; y < FV_MAP_HEIGHT; y++) {
                for (x = 0; x < FV_MAP_WIDTH; x++) {
                        save_block(buf,
                                   x, y,
                                   data->map.blocks[y * FV_MAP_WIDTH + x]);
                }
        }

        for (i = 0; i < FV_MAP_TILES_X * FV_MAP_TILES_Y; i++) {
                for (j = 0; j < data->map.tiles[i].n_specials; j++) {
                        save_special(data,
                                     buf,
                                     data->map.tiles[i].specials + j);
                }
        }

        out = fopen(filename, "wb");

        fv_free(filename);

        if (out == NULL) {
                fv_error_message("error saving: %s", strerror(errno));
        } else {
                fprintf(out,
                        "P6\n"
                        "%i %i\n"
                        "255\n",
                        FV_MAP_WIDTH * 4,
                        FV_MAP_HEIGHT * 4);

                fwrite(buf, FV_MAP_WIDTH * FV_MAP_HEIGHT * 4 * 4 * 3, 1, out);

                fclose(out);
        }

        fv_free(buf);
}

static void
handle_key_down(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        switch (event->keysym.sym) {
        case SDLK_ESCAPE:
                data->quit = true;
                break;

        case SDLK_LEFT:
                update_position(data, -1, 0);
                break;

        case SDLK_RIGHT:
                update_position(data, 1, 0);
                break;

        case SDLK_DOWN:
                update_position(data, 0, -1);
                break;

        case SDLK_UP:
                update_position(data, 0, 1);
                break;

        case SDLK_a:
                update_distance(data, -1);
                break;

        case SDLK_z:
                update_distance(data, 1);
                break;

        case SDLK_r:
                data->rotation = (data->rotation + 1) % 4;
                queue_redraw(data);
                break;

        case SDLK_h:
                toggle_height(data);
                break;

        case SDLK_s:
                save(data);
                break;

        case SDLK_m:
                next_special(data);
                break;
        }
}

static void
destroy_graphics(struct data *data)
{
        destroy_map_painter(data);

        if (data->graphics.shader_data_loaded) {
                fv_shader_data_destroy(&data->graphics.shader_data);
                data->graphics.shader_data_loaded = false;
        }
}

static void
create_graphics(struct data *data)
{
        /* All of the painting functions expect to have the default
         * OpenGL state plus the following modifications */

        fv_gl.glEnable(GL_CULL_FACE);
        fv_gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        /* The current program, vertex array, array buffer and bound
         * textures are not expected to be reset back to zero */

        if (!fv_shader_data_init(&data->graphics.shader_data))
                goto error;
        data->graphics.shader_data_loaded = true;

        if (!create_map_painter(data))
                goto error;

        return;

error:
        destroy_graphics(data);
        data->quit = true;
}

static void
handle_image_data_event(struct data *data,
                        const SDL_UserEvent *event)
{
        switch ((enum fv_image_data_result) event->code) {
        case FV_IMAGE_DATA_SUCCESS:
                create_graphics(data);
                queue_redraw(data);
                break;

        case FV_IMAGE_DATA_FAIL:
                data->quit = true;
                break;
        }
}

static void
handle_event(struct data *data,
             const SDL_Event *event)
{
        switch (event->type) {
        case SDL_WINDOWEVENT:
                switch (event->window.event) {
                case SDL_WINDOWEVENT_CLOSE:
                        data->quit = true;
                        break;
                case SDL_WINDOWEVENT_SIZE_CHANGED:
                case SDL_WINDOWEVENT_EXPOSED:
                        queue_redraw(data);
                        break;
                }
                goto handled;

        case SDL_KEYDOWN:
                handle_key_down(data, &event->key);
                goto handled;

        case SDL_QUIT:
                data->quit = true;
                goto handled;
        }

        if (event->type == data->image_data_event) {
                handle_image_data_event(data, &event->user);
                goto handled;
        }

handled:
        (void) 0;
}

static void
draw_cursor(struct data *data,
            const struct fv_paint_state *paint_state)
{
        struct cursor_vertex vertices[4];
        int block_pos = data->x_pos + data->y_pos * FV_MAP_WIDTH;
        float z_pos;
        int i;

        switch (FV_MAP_GET_BLOCK_TYPE(data->map.blocks[block_pos])) {
        case FV_MAP_BLOCK_TYPE_FULL_WALL:
                z_pos = 2.1f;
                break;
        case FV_MAP_BLOCK_TYPE_HALF_WALL:
                z_pos = 1.1f;
                break;
        default:
                z_pos = 0.1f;
                break;
        }

        for (i = 0; i < FV_N_ELEMENTS(vertices); i++)
                vertices[i].z = z_pos;

        vertices[0].x = data->x_pos;
        vertices[0].y = data->y_pos;
        vertices[1].x = data->x_pos + 1;
        vertices[1].y = data->y_pos;
        vertices[2].x = data->x_pos;
        vertices[2].y = data->y_pos + 1;
        vertices[3].x = data->x_pos + 1;
        vertices[3].y = data->y_pos + 1;

        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, data->cursor_buffer);
        fv_gl.glBufferData(GL_ARRAY_BUFFER,
                           sizeof vertices,
                           vertices,
                           GL_STREAM_DRAW);

        fv_gl.glUseProgram(data->cursor_program);
        fv_gl.glUniformMatrix4fv(data->cursor_transform_uniform,
                                 1, /* count */
                                 GL_FALSE, /* transpose */
                                 &paint_state->transform.mvp.xx);

        fv_array_object_bind(data->cursor_array_object);

        fv_gl.glEnable(GL_DEPTH_TEST);
        fv_gl.glEnable(GL_BLEND);
        fv_gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        fv_gl.glDisable(GL_BLEND);
        fv_gl.glDisable(GL_DEPTH_TEST);
}

static void
paint(struct data *data)
{
        struct fv_paint_state paint_state;
        struct fv_transform *transform = &paint_state.transform;
        float right, top;
        int w, h;

        SDL_GetWindowSize(data->window, &w, &h);

        fv_gl.glViewport(0, 0, w, h);

        fv_gl.glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        paint_state.center_x = data->x_pos + 0.5f;
        paint_state.center_y = data->y_pos + 0.5f;
        paint_state.visible_w = FV_MAP_WIDTH * 8.0f;
        paint_state.visible_h = FV_MAP_HEIGHT * 8.0f;

        if (w < h) {
                right = 1.0f;
                top = h / (float) w;
        } else {
                top = 1.0f;
                right = w / (float) h;
        }

        fv_matrix_init_identity(&transform->projection);

        fv_matrix_frustum(&transform->projection,
                          -right, right,
                          -top, top,
                          FV_EDITOR_NEAR_PLANE,
                          FV_EDITOR_FAR_PLANE);

        fv_matrix_init_identity(&transform->modelview);

        fv_matrix_translate(&transform->modelview,
                            0.0f, 0.0f, -data->distance);

        fv_matrix_rotate(&transform->modelview,
                         -30.0f,
                         1.0f, 0.0f, 0.0f);

        fv_matrix_scale(&transform->modelview,
                        FV_EDITOR_SCALE, FV_EDITOR_SCALE, FV_EDITOR_SCALE);

        fv_matrix_rotate(&transform->modelview,
                         data->rotation * 90.0f,
                         0.0f, 0.0f, 1.0f);

        fv_matrix_translate(&transform->modelview,
                            -paint_state.center_x,
                            -paint_state.center_y,
                            0.0f);

        fv_transform_update_derived_values(&paint_state.transform);

        fv_map_painter_paint(data->map_painter,
                             &paint_state);

        draw_cursor(data, &paint_state);

        SDL_GL_SwapWindow(data->window);
}

static void
handle_redraw(struct data *data)
{
        /* If the graphics aren't loaded yet then don't load anything.
         * Otherwise try painting and if nothing has changed then stop
         * redrawing.
         */

        if (data->graphics.shader_data_loaded)
                paint(data);

        data->redraw_queued = false;
}

static bool
check_gl_version(void)
{
        if (fv_gl.major_version < 0 ||
            fv_gl.minor_version < 0) {
                fv_error_message("Invalid GL version string encountered: %s",
                                 (const char *) fv_gl.glGetString(GL_VERSION));

                return false;
        }

        if (fv_gl.major_version < MIN_GL_MAJOR_VERSION ||
                   (fv_gl.major_version == MIN_GL_MAJOR_VERSION &&
                    fv_gl.minor_version < MIN_GL_MINOR_VERSION)) {
                fv_error_message("GL version %i.%i is required but the driver "
                                 "is reporting:\n"
                                 "Version: %s\n"
                                 "Vendor: %s\n"
                                 "Renderer: %s",
                                 MIN_GL_MAJOR_VERSION,
                                 MIN_GL_MINOR_VERSION,
                                 (const char *) fv_gl.glGetString(GL_VERSION),
                                 (const char *) fv_gl.glGetString(GL_VENDOR),
                                 (const char *) fv_gl.glGetString(GL_RENDERER));
                return false;
        }

        if (fv_gl.glGenerateMipmap == NULL) {
                fv_error_message("glGenerateMipmap is required (from "
                                 "GL_ARB_framebuffer_object)\n"
                                 "Version: %s\n"
                                 "Vendor: %s\n"
                                 "Renderer: %s",
                                 (const char *) fv_gl.glGetString(GL_VERSION),
                                 (const char *) fv_gl.glGetString(GL_VENDOR),
                                 (const char *) fv_gl.glGetString(GL_RENDERER));
                return false;
        }

        return true;
}

static SDL_GLContext
create_gl_context(SDL_Window *window)
{
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            CORE_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            CORE_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);

        return SDL_GL_CreateContext(window);
}

static GLuint
make_shader(GLenum type,
            const char *source)
{
        GLuint shader;
        GLint length = strlen(source);

        shader = fv_gl.glCreateShader(type);
        fv_gl.glShaderSource(shader, 1, &source, &length);
        fv_gl.glCompileShader(shader);

        return shader;
}

static GLuint
make_cursor_program(void)
{
        GLuint shader;
        GLint link_status;
        GLuint program;

        program = fv_gl.glCreateProgram();

        shader = make_shader(GL_VERTEX_SHADER, cursor_vertex_shader);
        fv_gl.glAttachShader(program, shader);
        fv_gl.glDeleteShader(shader);

        shader = make_shader(GL_FRAGMENT_SHADER, cursor_fragment_shader);
        fv_gl.glAttachShader(program, shader);
        fv_gl.glDeleteShader(shader);

        fv_gl.glLinkProgram(program);

        fv_gl.glGetProgramiv(program, GL_LINK_STATUS, &link_status);

        if (!link_status) {
                fv_error_message("failed to link cursor program");
                fv_gl.glDeleteProgram(program);
                return 0;
        }

        return program;
}

static void
make_cursor_buffer(struct data *data)
{
        fv_gl.glGenBuffers(1, &data->cursor_buffer);
        fv_gl.glBindBuffer(GL_ARRAY_BUFFER, data->cursor_buffer);

        data->cursor_array_object = fv_array_object_new();
        fv_array_object_set_attribute(data->cursor_array_object,
                                      0, /* index */
                                      3, /* size */
                                      GL_FLOAT,
                                      GL_FALSE, /* normalized */
                                      sizeof (struct cursor_vertex),
                                      0, /* divisor */
                                      data->cursor_buffer,
                                      0 /* buffer_offset */);
}

static void
run_main_loop(struct data *data)
{
        SDL_Event event;
        bool had_event;

        while (!data->quit) {
                if (data->redraw_queued) {
                        had_event = SDL_PollEvent(&event);
                } else {
                        had_event = SDL_WaitEvent(&event);
                }

                if (had_event)
                        handle_event(data, &event);
                else if (data->redraw_queued)
                        handle_redraw(data);
        }
}

int
main(int argc, char **argv)
{
        struct data data;
        int res;
        int ret = EXIT_SUCCESS;

        memset(&data.graphics, 0, sizeof data.graphics);
        data.map = fv_map;
        data.x_pos = FV_MAP_WIDTH / 2;
        data.y_pos = FV_MAP_HEIGHT / 2;
        data.distance = FV_EDITOR_MIN_DISTANCE;
        data.rotation = 0;

        res = SDL_Init(SDL_INIT_VIDEO);
        if (res < 0) {
                fv_error_message("Unable to init SDL: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out;
        }

        data.redraw_queued = true;

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);

        data.window = SDL_CreateWindow("Babiling",
                                       SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       800, 600,
                                       SDL_WINDOW_OPENGL |
                                       SDL_WINDOW_RESIZABLE);
        if (data.window == NULL) {
                fv_error_message("Failed to create SDL window: %s",
                                 SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_sdl;
        }

        data.gl_context = create_gl_context(data.window);
        if (data.gl_context == NULL) {
                fv_error_message("Failed to create GL context: %s",
                                 SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_window;
        }

        SDL_GL_MakeCurrent(data.window, data.gl_context);

        fv_gl_init();

        /* SDL seems to happily give you a GL 2 context if you ask for
         * a 3.x core profile but it can't provide one so we have to
         * additionally check that we actually got what we asked
         * for */
        if (!check_gl_version()) {
                ret = EXIT_FAILURE;
                goto out_context;
        }

        data.cursor_program = make_cursor_program();
        if (data.cursor_program == 0) {
                ret = EXIT_FAILURE;
                goto out_context;
        }

        data.cursor_transform_uniform =
                fv_gl.glGetUniformLocation(data.cursor_program, "transform");

        make_cursor_buffer(&data);

        data.image_data_event = SDL_RegisterEvents(1);

        data.quit = false;

        data.image_data = fv_image_data_new(data.image_data_event);

        run_main_loop(&data);

        fv_gl.glDeleteProgram(data.cursor_program);
        fv_array_object_free(data.cursor_array_object);
        fv_gl.glDeleteBuffers(1, &data.cursor_buffer);

        destroy_graphics(&data);

        if (data.image_data)
                fv_image_data_free(data.image_data);

out_context:
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_DeleteContext(data.gl_context);
out_window:
        SDL_DestroyWindow(data.window);
out_sdl:
        SDL_Quit();
out:
        return ret;
}