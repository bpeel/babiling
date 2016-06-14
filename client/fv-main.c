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
#include <assert.h>
#include <stdlib.h>
#include <time.h>
#include <stdarg.h>

#include "fv-game.h"
#include "fv-logic.h"
#include "fv-image-data.h"
#include "fv-shader-data.h"
#include "fv-gl.h"
#include "fv-util.h"
#include "fv-hud.h"
#include "fv-buffer.h"
#include "fv-map.h"
#include "fv-error-message.h"
#include "fv-network.h"
#include "fv-bitmask.h"
#include "fv-pointer-array.h"
#include "fv-audio-buffer.h"
#include "fv-mutex.h"
#include "fv-audio-device.h"
#include "fv-random.h"

#ifdef EMSCRIPTEN
#include <emscripten.h>
#include <emscripten/html5.h>
/* On Emscripten you have to request 2.0 to get a 2.0 ES context but
 * the version is reports in GL_VERSION is 1.0 because that is the
 * WebGL version.
 */
#define MIN_GL_MAJOR_VERSION 1
#define MIN_GL_MINOR_VERSION 0
#define REQUEST_GL_MAJOR_VERSION 2
#define REQUEST_GL_MINOR_VERSION 0
#define FV_GL_PROFILE SDL_GL_CONTEXT_PROFILE_ES
#else
#define MIN_GL_MAJOR_VERSION 2
#define MIN_GL_MINOR_VERSION 0
#define REQUEST_GL_MAJOR_VERSION MIN_GL_MAJOR_VERSION
#define REQUEST_GL_MINOR_VERSION MIN_GL_MINOR_VERSION
#define CORE_GL_MAJOR_VERSION 3
#define CORE_GL_MINOR_VERSION 1
#define FV_GL_PROFILE SDL_GL_CONTEXT_PROFILE_COMPATIBILITY
#endif

/* Minimum movement before we consider the joystick axis to be moving.
 * This is 20% of the total.
 */
#define MIN_JOYSTICK_AXIS_MOVEMENT (32767 * 2 / 10)
/* Maximum movement before we consider the joystick to be at full
 * speed. This is 90% of the total.
 */
#define MAX_JOYSTICK_AXIS_MOVEMENT (32767 * 9 / 10)

enum key_code {
        KEY_CODE_UP,
        KEY_CODE_DOWN,
        KEY_CODE_LEFT,
        KEY_CODE_RIGHT
};

struct key_mapping {
        enum key_code code;
        SDL_Keycode sym;
};

struct button_mapping {
        enum key_code code;
        uint8_t button;
};

struct joystick {
        SDL_Joystick *joystick;
        SDL_JoystickID id;
        uint32_t button_state;
        int16_t x_axis;
        int16_t y_axis;
        float direction;
        float speed;
};

static const struct key_mapping
key_mappings[] = {
        { KEY_CODE_UP, SDLK_w },
        { KEY_CODE_DOWN, SDLK_s },
        { KEY_CODE_LEFT, SDLK_a },
        { KEY_CODE_RIGHT, SDLK_d },
        { KEY_CODE_UP, SDLK_UP },
        { KEY_CODE_DOWN, SDLK_DOWN },
        { KEY_CODE_LEFT, SDLK_LEFT },
        { KEY_CODE_RIGHT, SDLK_RIGHT },
};

_Static_assert(FV_N_ELEMENTS(key_mappings) <= sizeof (uint32_t) * 8,
               "There are too many key mappings to store the state "
               "in a uint32_t");

/* The buttons are taken from the W3C gamepad API standard gamepad
 * mapping.
 */
static const struct button_mapping
button_mappings[] = {
        /* These are the keys that SDL reports for a PS3 controller on
         * Linux. I'm not sure if this is a standard mapping.
         */
        /* D-pad */
        { KEY_CODE_UP, 4 },
        { KEY_CODE_DOWN, 6 },
        { KEY_CODE_LEFT, 7 },
        { KEY_CODE_RIGHT, 5 },
        /* Shape buttons */
        { KEY_CODE_UP, 12 },
        { KEY_CODE_DOWN, 14 },
        { KEY_CODE_LEFT, 15 },
        { KEY_CODE_RIGHT, 13 },
};

_Static_assert(FV_N_ELEMENTS(button_mappings) <= sizeof (uint32_t) * 8,
               "There are too many button mappings to store the state "
               "in a uint32_t");

enum menu_state {
        MENU_STATE_TITLE_SCREEN,
        MENU_STATE_PLAYING
};

struct data {
        /* Pointer array of server addresses to try connecting to.
         * These are const strings that point into argv and so are not
         * freed.
         */
        struct fv_buffer server_addresses;
        struct fv_network *nw;

        struct fv_image_data *image_data;
        Uint32 image_data_event;

        SDL_Window *window;
        int last_fb_width, last_fb_height;
        SDL_GLContext gl_context;

        struct {
                struct fv_shader_data shader_data;
                struct fv_game *game;
                struct fv_hud *hud;
                bool shader_data_loaded;
        } graphics;

        struct fv_logic *logic;

        bool quit;
        bool is_fullscreen;

        unsigned int last_update_time;

        enum menu_state menu_state;
        int n_players;

        struct fv_buffer joysticks;

        uint32_t key_state;

        enum {
                CURSOR_STATE_NONE,
                CURSOR_STATE_MOUSE,
                CURSOR_STATE_TOUCH
        } cursor_state;
        union {
                SDL_TouchID touch_device;
                Uint32 mouse_device;
        };
        bool cursor_pos_dirty;
        int cursor_screen_x, cursor_screen_y;
        float cursor_x, cursor_y;

        bool redraw_queued;

        struct fv_audio_device *audio_device;
        struct fv_audio_buffer *audio_buffer;

#ifndef EMSCRIPTEN

        /* Event that is sent asynchronously to queue a redraw */
        Uint32 redraw_user_event;

        /* This is a cache of the NPC state that is updated
         * asynchronously. It is copied into the fv_logic just before
         * updating it. It is always accessed with the mutex locked.
         */
        struct fv_mutex *npcs_mutex;
        /* Array of fv_person */
        struct fv_buffer npcs;
        /* Array with FV_NETWORK_DIRTY_PLAYER_BITS bits of
         * fv_person_state for each npc
         */
        struct fv_buffer dirty_npcs;

#endif /* EMSCRIPTEN */
};

static void
queue_redraw(struct data *data)
{
#ifdef EMSCRIPTEN

        if (data->redraw_queued)
                return;

        emscripten_resume_main_loop();

        data->last_update_time = SDL_GetTicks();

#endif /* EMSCRIPTEN */

        data->redraw_queued = true;
}

static void
reset_menu_state(struct data *data)
{
        data->menu_state = MENU_STATE_TITLE_SCREEN;
        data->last_update_time = SDL_GetTicks();

        queue_redraw(data);
}

#ifndef EMSCRIPTEN
static void
toggle_fullscreen(struct data *data)
{
        int display_index;
        SDL_DisplayMode mode;

        display_index = SDL_GetWindowDisplayIndex(data->window);

        if (display_index == -1)
                return;

        if (SDL_GetDesktopDisplayMode(display_index, &mode) == -1)
                return;

        SDL_SetWindowDisplayMode(data->window, &mode);

        data->is_fullscreen = !data->is_fullscreen;
        queue_redraw(data);

        SDL_SetWindowFullscreen(data->window, data->is_fullscreen);
}
#endif /* EMSCRIPTEN */

static bool
check_joystick_axis_movement(struct data *data,
                             float *direction,
                             float *speed)
{
        struct joystick *joystick;
        int i;

        for (i = 0;
             i < data->joysticks.length / sizeof (struct joystick);
             i++)  {
                joystick = (struct joystick *) data->joysticks.data + i;

                if (joystick->speed > 0.0f) {
                        *direction = joystick->direction;
                        *speed = joystick->speed;
                        return true;
                }
        }

        return false;
}

static bool
check_cursor_movement(struct data *data,
                      float *direction)
{
        float player_x, player_y;
        float center_x, center_y;
        float dx, dy;

        if (data->cursor_state == CURSOR_STATE_NONE)
                return false;

        if (data->cursor_pos_dirty) {
                fv_game_screen_to_world(data->graphics.game,
                                        data->last_fb_width,
                                        data->last_fb_height,
                                        data->cursor_screen_x,
                                        data->cursor_screen_y,
                                        &data->cursor_x,
                                        &data->cursor_y);
                data->cursor_pos_dirty = false;
        }

        fv_logic_get_center(data->logic, &center_x, &center_y);
        fv_logic_get_player_position(data->logic, &player_x, &player_y);

        dx = data->cursor_x + center_x - player_x;
        dy = data->cursor_y + center_y - player_y;

        if (dx * dx + dy * dy <= 0.1f * 0.1f)
                return false;

        *direction = atan2f(dy, dx);

        return true;
}

static void
update_direction(struct data *data)
{
        struct joystick *joystick;
        float direction;
        float speed = FV_LOGIC_PLAYER_SPEED;
        int pressed_keys = 0;
        int key_mask;
        int i, j;

        for (i = 0; i < FV_N_ELEMENTS(key_mappings); i++) {
                if ((data->key_state & (1 << i)))
                        pressed_keys |= 1 << key_mappings[i].code;
        }

        for (i = 0;
             i < data->joysticks.length / sizeof (struct joystick);
             i++)  {
                joystick = (struct joystick *) data->joysticks.data + i;

                for (j = 0; j < FV_N_ELEMENTS(button_mappings); j++) {
                        if (joystick->button_state & (1 << j))
                                pressed_keys |= 1 << button_mappings[j].code;
                }
        }

        /* Cancel out directions where opposing keys are pressed */
        key_mask = ((pressed_keys & 10) >> 1) ^ (pressed_keys & 5);
        key_mask |= key_mask << 1;
        pressed_keys &= key_mask;

        switch (pressed_keys) {
        case 1 << KEY_CODE_UP:
                direction = M_PI / 2.0f;
                break;
        case (1 << KEY_CODE_UP) | (1 << KEY_CODE_LEFT):
                direction = M_PI * 3.0f / 4.0f;
                break;
        case (1 << KEY_CODE_UP) | (1 << KEY_CODE_RIGHT):
                direction = M_PI / 4.0f;
                break;
        case 1 << KEY_CODE_DOWN:
                direction = -M_PI / 2.0f;
                break;
        case (1 << KEY_CODE_DOWN) | (1 << KEY_CODE_LEFT):
                direction = -M_PI * 3.0f / 4.0f;
                break;
        case (1 << KEY_CODE_DOWN) | (1 << KEY_CODE_RIGHT):
                direction = -M_PI / 4.0f;
                break;
        case 1 << KEY_CODE_LEFT:
                direction = M_PI;
                break;
        case 1 << KEY_CODE_RIGHT:
                direction = 0.0f;
                break;
        default:
                /* If no buttons or keys are pressed then check if any
                 * movement is triggered by a joystick axis or the
                 * cursor.
                 */
                if (!check_joystick_axis_movement(data, &direction, &speed) &&
                    !check_cursor_movement(data, &direction)) {
                        speed = 0.0f;
                        direction = 0.0f;
                }
                break;
        }

        if (speed > 0.0f && data->menu_state == MENU_STATE_TITLE_SCREEN) {
                data->menu_state = MENU_STATE_PLAYING;
                data->last_update_time = SDL_GetTicks();
        }

        fv_logic_set_direction(data->logic, speed, direction);

        fv_logic_set_flag_person(data->logic, FV_LOGIC_PERSON_NONE);

        queue_redraw(data);
}

static void
handle_other_key(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        int i;

        for (i = 0; i < FV_N_ELEMENTS(key_mappings); i++) {
                if (key_mappings[i].sym == event->keysym.sym) {
                        if (event->state == SDL_PRESSED)
                                data->key_state |= 1 << i;
                        else
                                data->key_state &= ~(1 << i);

                        update_direction(data);

                        break;
                }
        }
}

static void
handle_key_event(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        switch (event->keysym.sym) {
        case SDLK_ESCAPE:
                if (event->state == SDL_PRESSED) {
                        if (data->menu_state == MENU_STATE_TITLE_SCREEN)
                                data->quit = true;
                        else
                                reset_menu_state(data);
                }
                break;

#ifndef EMSCRIPTEN
        case SDLK_F11:
                if (event->state == SDL_PRESSED)
                        toggle_fullscreen(data);
                break;
#endif

        default:
                handle_other_key(data, event);
                break;
        }
}

static struct joystick *
find_joystick(struct data *data,
              SDL_JoystickID id)
{
        struct joystick *joystick;
        int i;

        for (i = 0;
             i < data->joysticks.length / sizeof (struct joystick);
             i++)  {
                joystick = (struct joystick *) data->joysticks.data + i;

                if (joystick->id == id)
                        return joystick;
        }

        return NULL;
}

static void
handle_joystick_button(struct data *data,
                       const SDL_JoyButtonEvent *event)
{
        struct joystick *joystick = find_joystick(data, event->which);
        int i;

        if (joystick == NULL)
                return;

        for (i = 0; i < FV_N_ELEMENTS(button_mappings); i++) {
                if (button_mappings[i].button == event->button) {
                        if (event->state == SDL_PRESSED)
                                joystick->button_state |= 1 << i;
                        else
                                joystick->button_state &= ~(1 << i);

                        update_direction(data);

                        break;
                }
        }
}

static void
handle_joystick_axis_motion(struct data *data,
                            const SDL_JoyAxisEvent *event)
{
        struct joystick *joystick;
        int mag_squared;
        int16_t value = event->value;

        /* Ignore axes other than 0 and 1 */
        if (event->axis & ~1)
                return;

        joystick = find_joystick(data, event->which);

        if (joystick == NULL)
                return;

        if (value < -INT16_MAX)
                value = -INT16_MAX;

        if (event->axis)
                joystick->y_axis = -value;
        else
                joystick->x_axis = value;

        mag_squared = (joystick->y_axis * (int) joystick->y_axis +
                       joystick->x_axis * (int) joystick->x_axis);

        if (mag_squared <= (MIN_JOYSTICK_AXIS_MOVEMENT *
                            MIN_JOYSTICK_AXIS_MOVEMENT)) {
                joystick->direction = 0.0f;
                joystick->speed = 0.0f;
        } else {
                if (mag_squared >= (MAX_JOYSTICK_AXIS_MOVEMENT *
                                    MAX_JOYSTICK_AXIS_MOVEMENT)) {
                        joystick->speed = FV_LOGIC_PLAYER_SPEED;
                } else {
                        joystick->speed = ((sqrtf(mag_squared) -
                                            MIN_JOYSTICK_AXIS_MOVEMENT) *
                                           FV_LOGIC_PLAYER_SPEED /
                                           (MAX_JOYSTICK_AXIS_MOVEMENT -
                                            MIN_JOYSTICK_AXIS_MOVEMENT));
                }
                joystick->direction = atan2f(joystick->y_axis,
                                             joystick->x_axis);
        }

        update_direction(data);
}

static void
handle_joystick_added(struct data *data,
                      const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick *joystick = SDL_JoystickOpen(event->which);
        struct joystick *old_joystick, *new_joystick;
        SDL_JoystickID id;

        if (joystick == NULL) {
                fprintf(stderr, "failed to open joystick %i: %s\n",
                        event->which,
                        SDL_GetError());
                return;
        }

        id = SDL_JoystickInstanceID(joystick);

        /* Check if we already have this joystick open */
        old_joystick = find_joystick(data, id);
        if (old_joystick) {
                SDL_JoystickClose(joystick);
                return;
        }

        fv_buffer_set_length(&data->joysticks,
                             data->joysticks.length + sizeof (struct joystick));

        new_joystick = (struct joystick *) (data->joysticks.data +
                                            data->joysticks.length -
                                            sizeof (struct joystick));
        new_joystick->joystick = joystick;
        new_joystick->id = id;
        new_joystick->button_state = 0;
        new_joystick->speed = 0.0f;
        new_joystick->direction = 0.0f;
        new_joystick->x_axis = 0;
        new_joystick->y_axis = 0;
}

static void
handle_joystick_removed(struct data *data,
                        const SDL_JoyDeviceEvent *event)
{
        struct joystick *joysticks = (struct joystick *) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (struct joystick);
        int i;

        for (i = 0; i < n_joysticks; i++) {
                if (joysticks[i].id == event->which) {
                        SDL_JoystickClose(joysticks[i].joystick);
                        if (i < n_joysticks - 1)
                                joysticks[i] = joysticks[n_joysticks - 1];
                        data->joysticks.length -= sizeof (struct joystick);
                        break;
                }
        }
}

static void
set_cursor_screen_pos(struct data *data,
                      int x,
                      int y)
{
        data->cursor_screen_x = x;
        data->cursor_screen_y = y;
        data->cursor_pos_dirty = true;
        queue_redraw(data);
}

static void
release_cursor(struct data *data)
{
        data->cursor_state = CURSOR_STATE_NONE;
        update_direction(data);
}

static bool
check_click_person(struct data *data,
                   int x, int y)
{
        float ray_points[6];
        int person;

        fv_game_screen_to_world_ray(data->graphics.game,
                                    data->last_fb_width,
                                    data->last_fb_height,
                                    x, y,
                                    ray_points);
        person = fv_logic_find_person_intersecting_ray(data->logic,
                                                       ray_points);

        if (person != FV_LOGIC_PERSON_NONE) {
                fv_logic_set_flag_person(data->logic, person);
                queue_redraw(data);
                return true;
        }

        return false;
}

static void
handle_mouse_button(struct data *data,
                    const SDL_MouseButtonEvent *event)
{
        if (event->button != 1)
                return;

        if (event->state == SDL_PRESSED) {
                if (data->cursor_state != CURSOR_STATE_NONE ||
                    event->which == SDL_TOUCH_MOUSEID)
                        return;

                if (check_click_person(data, event->x, event->y))
                        return;

                data->cursor_state = CURSOR_STATE_MOUSE;
                data->mouse_device = event->which;
                set_cursor_screen_pos(data, event->x, event->y);
        } else {
                if (data->cursor_state != CURSOR_STATE_MOUSE ||
                    data->mouse_device != event->which)
                        return;

                release_cursor(data);
        }
}

static void
handle_mouse_motion(struct data *data,
                    const SDL_MouseMotionEvent *event)
{
        if (data->cursor_state != CURSOR_STATE_MOUSE ||
            event->which != data->mouse_device)
                return;

        set_cursor_screen_pos(data, event->x, event->y);
}

static void
handle_finger_down(struct data *data,
                   const SDL_TouchFingerEvent *event)
{
        int x_pos, y_pos;

        if (data->cursor_state != CURSOR_STATE_NONE ||
            event->fingerId != 0)
                return;

        x_pos = event->x * data->last_fb_width;
        y_pos = event->y * data->last_fb_height;

        if (check_click_person(data, x_pos, y_pos))
                return;

        data->cursor_state = CURSOR_STATE_TOUCH;
        data->touch_device = event->touchId;

        set_cursor_screen_pos(data, x_pos, y_pos);
}

static void
handle_finger_up(struct data *data,
                   const SDL_TouchFingerEvent *event)
{
        if (data->cursor_state != CURSOR_STATE_TOUCH ||
            data->touch_device != event->touchId ||
            event->fingerId != 0)
                return;

        release_cursor(data);
}

static void
handle_finger_motion(struct data *data,
                     const SDL_TouchFingerEvent *event)
{
        if (data->cursor_state != CURSOR_STATE_TOUCH ||
            event->touchId != data->touch_device ||
            event->fingerId != 0)
                return;

        set_cursor_screen_pos(data,
                              event->x * data->last_fb_width,
                              event->y * data->last_fb_height);
}

static void
destroy_graphics(struct data *data)
{
        if (data->graphics.game) {
                fv_game_free(data->graphics.game);
                data->graphics.game = NULL;
        }

        if (data->graphics.shader_data_loaded) {
                fv_shader_data_destroy(&data->graphics.shader_data);
                data->graphics.shader_data_loaded = false;
        }

        if (data->graphics.hud) {
                fv_hud_free(data->graphics.hud);
                data->graphics.hud = NULL;
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

        data->last_fb_width = data->last_fb_height = 0;

        if (!fv_shader_data_init(&data->graphics.shader_data))
                goto error;

        data->graphics.shader_data_loaded = true;

        data->graphics.hud = fv_hud_new(data->image_data,
                                        &data->graphics.shader_data);

        if (data->graphics.hud == NULL)
                goto error;

        data->graphics.game = fv_game_new(data->image_data,
                                          &data->graphics.shader_data);

        if (data->graphics.game == NULL)
                goto error;

#ifdef EMSCRIPTEN
        emscripten_resume_main_loop();
#endif

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

        fv_image_data_free(data->image_data);
        data->image_data = NULL;
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
        case SDL_KEYUP:
                handle_key_event(data, &event->key);
                goto handled;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
                handle_mouse_button(data, &event->button);
                goto handled;

        case SDL_MOUSEMOTION:
                handle_mouse_motion(data, &event->motion);
                goto handled;

        case SDL_FINGERDOWN:
                handle_finger_down(data, &event->tfinger);
                goto handled;

        case SDL_FINGERUP:
                handle_finger_up(data, &event->tfinger);
                goto handled;

        case SDL_FINGERMOTION:
                handle_finger_motion(data, &event->tfinger);
                goto handled;

        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
                handle_joystick_button(data, &event->jbutton);
                goto handled;

        case SDL_JOYAXISMOTION:
                handle_joystick_axis_motion(data, &event->jaxis);
                goto handled;

        case SDL_JOYDEVICEADDED:
                handle_joystick_added(data, &event->jdevice);
                goto handled;

        case SDL_JOYDEVICEREMOVED:
                handle_joystick_removed(data, &event->jdevice);
                goto handled;

        case SDL_QUIT:
                data->quit = true;
                goto handled;
        }

        if (event->type == data->image_data_event) {
                handle_image_data_event(data, &event->user);
                goto handled;
        }

#ifndef EMSCRIPTEN
        if (event->type == data->redraw_user_event) {
                queue_redraw(data);
                goto handled;
        }
#endif

handled:
        (void) 0;
}

static void
paint_hud(struct data *data,
          int w, int h)
{
        switch (data->menu_state) {
        case MENU_STATE_TITLE_SCREEN:
                fv_hud_paint_title_screen(data->graphics.hud,
                                          w, h);
                break;
        case MENU_STATE_PLAYING:
                break;
        }
}

static void
close_joysticks(struct data *data)
{
        struct joystick *joysticks = (struct joystick *) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (struct joystick);
        int i;

        for (i = 0; i < n_joysticks; i++)
                SDL_JoystickClose(joysticks[i].joystick);

        fv_buffer_destroy(&data->joysticks);
}

static void
update_npcs(struct data *data)
{
#ifndef EMSCRIPTEN
        const struct fv_person *npcs;
        int npc_num, state_num, bit_num;

        fv_mutex_lock(data->npcs_mutex);

        npcs = (const struct fv_person *) data->npcs.data;

        fv_logic_set_n_npcs(data->logic,
                            data->npcs.length /
                            sizeof (struct fv_person));

        fv_bitmask_for_each(&data->dirty_npcs, bit_num) {
                npc_num = bit_num / FV_NETWORK_DIRTY_PLAYER_BITS;
                state_num = bit_num % FV_NETWORK_DIRTY_PLAYER_BITS;
                fv_logic_update_npc(data->logic,
                                    npc_num,
                                    npcs + npc_num,
                                    1 << state_num);
        }

        memset(data->dirty_npcs.data, 0, data->dirty_npcs.length);

        fv_mutex_unlock(data->npcs_mutex);
#endif
}

/* Returns whether redrawing should continue */
static bool
paint(struct data *data)
{
        GLbitfield clear_mask = GL_DEPTH_BUFFER_BIT;
        struct fv_person player;
        enum fv_logic_state_change state_change;
        float center_x, center_y;
        unsigned int now;
        int w, h;

        SDL_GetWindowSize(data->window, &w, &h);

        if (w != data->last_fb_width || h != data->last_fb_height) {
                fv_gl.glViewport(0, 0, w, h);
                data->last_fb_width = w;
                data->last_fb_height = h;
        }

        update_npcs(data);

        /* The direction constantly changes when the mouse is pressed
         * so we need to recalculate every time.
         */
        if (data->cursor_state != CURSOR_STATE_NONE)
                update_direction(data);

        now = SDL_GetTicks();
        state_change = fv_logic_update(data->logic,
                                       now - data->last_update_time);
        data->last_update_time = now;

        if ((state_change & FV_LOGIC_STATE_CHANGE_POSITION)) {
                fv_logic_get_player(data->logic,
                                    &player,
                                    FV_PERSON_STATE_POSITION);
                fv_network_update_player(data->nw,
                                         &player,
                                         FV_PERSON_STATE_POSITION);
        }

        fv_logic_get_center(data->logic, &center_x, &center_y);

        if (!fv_game_covers_framebuffer(data->graphics.game,
                                        center_x, center_y,
                                        w, h))
                clear_mask |= GL_COLOR_BUFFER_BIT;

        fv_gl.glClear(clear_mask);

        fv_game_paint(data->graphics.game,
                      center_x, center_y,
                      w, h,
                      data->logic);

        paint_hud(data, w, h);

        SDL_GL_SwapWindow(data->window);

        /* If the logic has become stable then we'll stop redrawing
         * until something changes.
         */
        return state_change != 0;
}

static void
handle_redraw(struct data *data)
{
        /* If the graphics aren't loaded yet then don't load anything.
         * Otherwise try painting and if nothing has changed then stop
         * redrawing.
         */

        if (data->graphics.game == NULL ||
            !paint(data)) {
#ifdef EMSCRIPTEN
                emscripten_pause_main_loop();
#endif
                data->redraw_queued = false;
        }
}

#ifdef EMSCRIPTEN

static void
consistent_event_cb(const struct fv_network_consistent_event *event,
                    void *user_data)
{
        struct data *data = user_data;
        int player_num, state_num, bit_num;

        fv_logic_set_n_npcs(data->logic, event->n_players);

        fv_bitmask_for_each(event->dirty_players, bit_num) {
                player_num = bit_num / FV_NETWORK_DIRTY_PLAYER_BITS;
                state_num = bit_num % FV_NETWORK_DIRTY_PLAYER_BITS;
                fv_logic_update_npc(data->logic,
                                    player_num,
                                    event->players + player_num,
                                    1 << state_num);
        }

        queue_redraw(data);
}

#else /* EMSCRIPTEN */

static void
consistent_event_cb(const struct fv_network_consistent_event *event,
                    void *user_data)
{
        struct data *data = user_data;
        SDL_Event redraw_event = { .type = data->redraw_user_event };
        int player_num, state_num, bit_num;

        fv_mutex_lock(data->npcs_mutex);

        fv_buffer_set_length(&data->npcs,
                             sizeof (struct fv_person) * event->n_players);
        fv_bitmask_set_length(&data->dirty_npcs,
                              event->n_players * FV_NETWORK_DIRTY_PLAYER_BITS);
        fv_bitmask_or(&data->dirty_npcs, event->dirty_players);

        fv_bitmask_for_each(event->dirty_players, bit_num) {
                player_num = bit_num / FV_NETWORK_DIRTY_PLAYER_BITS;
                state_num = bit_num % FV_NETWORK_DIRTY_PLAYER_BITS;
                fv_person_copy_state((struct fv_person *) data->npcs.data +
                                     player_num,
                                     event->players + player_num,
                                     1 << state_num);
        }

        SDL_PushEvent(&redraw_event);

        fv_mutex_unlock(data->npcs_mutex);
}

#endif /* EMSCRIPTEN */

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

static void
show_help(void)
{
        printf("Babiling - Virtual Language Exchange\n"
               "usage: babiling [options]\n"
               "Options:\n"
               " -h        Show this help messae\n"
               " -w        Run in a window\n"
               " -s <host> Specify the server to connect to. Can be given\n"
               "           multiple times to add alternatives.\n"
               " -f        Run fullscreen (default)\n");
}

static int
process_argument_flags(struct data *data,
                       const char *flags,
                       int remaining_argc,
                       char **remaining_argv)
{
        int args_used = 0;

        while (*flags) {
                switch (*flags) {
                case 'h':
                        show_help();
                        return -1;

                case 'w':
                        data->is_fullscreen = false;
                        break;

                case 'f':
                        data->is_fullscreen = true;
                        break;

                case 's':
                        if (remaining_argc <= 0) {
                                fprintf(stderr,
                                        "Option -s requires an argument\n");
                                show_help();
                                return -1;
                        }
                        fv_pointer_array_append(&data->server_addresses,
                                                remaining_argv[0]);
                        remaining_argv++;
                        remaining_argc--;
                        args_used++;
                        break;

                default:
                        fprintf(stderr, "Unknown option ‘%c’\n", *flags);
                        show_help();
                        return -1;
                }

                flags++;
        }

        return args_used;
}

static bool
process_arguments(struct data *data,
                  int argc, char **argv)
{
        int args_used;
        int i;

        for (i = 1; i < argc; i++) {
                if (argv[i][0] == '-') {
                        args_used = process_argument_flags(data,
                                                           argv[i] + 1,
                                                           argc - i - 1,
                                                           argv + i + 1);
                        if (args_used == -1)
                                return false;
                        i += args_used;
                } else {
                        fprintf(stderr, "Unexpected argument ‘%s’\n", argv[i]);
                        show_help();
                        return false;
                }
        }

        return true;
}

static SDL_GLContext
create_gl_context(SDL_Window *window)
{
#ifndef EMSCRIPTEN
        SDL_GLContext context;

        /* First try creating a core context because if we get one it
         * can be more efficient.
         */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            CORE_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            CORE_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_CORE);

        context = SDL_GL_CreateContext(window);

        if (context != NULL)
                return context;
#endif /* EMSCRIPTEN */

        /* Otherwise try a compatibility profile context */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            REQUEST_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            REQUEST_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            FV_GL_PROFILE);

        return SDL_GL_CreateContext(window);
}

static void
add_server_addresses(struct data *data)
{
        const char *host;
        int i;

        if (data->server_addresses.length == 0) {
                fv_network_add_host(data->nw, "localhost");
                return;
        }

        for (i = 0; i < fv_pointer_array_length(&data->server_addresses); i++) {
                host = fv_pointer_array_get(&data->server_addresses, i);
                fv_network_add_host(data->nw, host);
        }
}

#ifdef EMSCRIPTEN

static void
emscripten_loop_cb(void *user_data)
{
        struct data *data = user_data;

        handle_redraw(data);
}

static int
emscripten_event_filter(void *userdata,
                        SDL_Event *event)
{
        handle_event(userdata, event);

        /* Filter the event */
        return 0;
}

static EM_BOOL
context_lost_cb(int event_type,
                const void *reserved,
                void *user_data)
{
        struct data *data = user_data;

        destroy_graphics(data);

        /* Cancel loading the images */
        if (data->image_data) {
                fv_image_data_free(data->image_data);
                data->image_data = NULL;
        } else {
                emscripten_pause_main_loop();
        }

        return true;
}

static EM_BOOL
context_restored_cb(int event_type,
                    const void *reserved,
                    void *user_data)
{
        struct data *data = user_data;

        /* When the context is lost all of the extension objects that
         * Emscripten created become invalid so it needs to query them
         * again. Ideally it would handle this itself internally. This
         * is probably poking into its internals a bit.
         */
        EM_ASM({
                        var context = GL.currentContext;
                        context.initExtensionsDone = false;
                        GL.initExtensions(context);
                });

        /* Reload the images. This will also reload the graphics when
         * it has finished.
         */
        if (data->image_data == NULL)
                data->image_data = fv_image_data_new(data->image_data_event);

        return true;
}

#else /* EMSCRIPTEN */

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
                        data->last_update_time = SDL_GetTicks();
                }

                if (had_event)
                        handle_event(data, &event);
                else if (data->redraw_queued)
                        handle_redraw(data);
        }
}

#endif /* EMSCRIPTEN */

static void
audio_cb(int16_t *buffer,
         int n_samples,
         void *user_data)
{
        struct data *data = user_data;

        fv_audio_buffer_get(data->audio_buffer,
                            buffer,
                            n_samples);
}

int
main(int argc, char **argv)
{
        struct data data;
        struct fv_person player;
        enum fv_person_state initial_update_state;
        Uint32 flags;
        int res;
        int ret = EXIT_SUCCESS;
        int i;

        fv_buffer_init(&data.server_addresses);

#ifdef EMSCRIPTEN
        data.is_fullscreen = false;
#else
        data.is_fullscreen = true;
#endif

        memset(&data.graphics, 0, sizeof data.graphics);

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out_addresses;
        }

        fv_random_init();

        res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK | SDL_INIT_AUDIO);
        if (res < 0) {
                fv_error_message("Unable to init SDL: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_addresses;
        }

        data.audio_buffer = fv_audio_buffer_new();

        data.audio_device = fv_audio_device_new(audio_cb, &data);
        if (data.audio_device == NULL) {
                ret = EXIT_FAILURE;
                goto out_audio_buffer;
        }

        data.key_state = 0;
        data.cursor_state = CURSOR_STATE_NONE;

        data.redraw_queued = true;

#ifndef EMSCRIPTEN

        data.npcs_mutex = fv_mutex_new();
        if (data.npcs_mutex == NULL) {
                fv_error_message("Failed to create mutex");
                ret = EXIT_FAILURE;
                goto out_audio_device;
        }

        fv_buffer_init(&data.npcs);
        fv_buffer_init(&data.dirty_npcs);

        data.redraw_user_event = SDL_RegisterEvents(1);

#endif /* EMSCRIPTEN */

        data.nw = fv_network_new(data.audio_buffer, consistent_event_cb, &data);
        if (data.nw == NULL) {
                ret = EXIT_FAILURE;
                goto out_npcs;
        }

        add_server_addresses(&data);

        fv_buffer_init(&data.joysticks);

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;
        if (data.is_fullscreen)
                flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;

        /* First try creating a window with multisampling */
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 2);

        for (i = 0; ; i++) {
                data.window = SDL_CreateWindow("Babiling",
                                               SDL_WINDOWPOS_UNDEFINED,
                                               SDL_WINDOWPOS_UNDEFINED,
                                               800, 600,
                                               flags);
                if (data.window)
                        break;

                if (i == 0) {
                        /* Try again without multisampling */
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
                        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
                } else {
                        fv_error_message("Failed to create SDL window: %s",
                                         SDL_GetError());
                        ret = EXIT_FAILURE;
                        goto out_network;
                }
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

        data.image_data_event = SDL_RegisterEvents(1);

        data.quit = false;

        data.logic = fv_logic_new();

        initial_update_state = (FV_PERSON_STATE_POSITION |
                                FV_PERSON_STATE_APPEARANCE |
                                FV_PERSON_STATE_FLAGS);
        fv_logic_get_player(data.logic,
                            &player,
                            initial_update_state);
        fv_network_update_player(data.nw, &player, initial_update_state);

        data.image_data = fv_image_data_new(data.image_data_event);

        reset_menu_state(&data);

#ifdef EMSCRIPTEN

        emscripten_set_webglcontextlost_callback("canvas",
                                                 &data,
                                                 false /* useCapture */,
                                                 context_lost_cb);
        emscripten_set_webglcontextrestored_callback("canvas",
                                                     &data,
                                                     false /* useCapture */,
                                                     context_restored_cb);

        SDL_SetEventFilter(emscripten_event_filter, &data);

        emscripten_set_main_loop_arg(emscripten_loop_cb,
                                     &data,
                                     0, /* fps (use browser's choice) */
                                     true /* simulate infinite loop */);

#else /* EMSCRIPTEN */

        run_main_loop(&data);

#endif /* EMSCRIPTEN */

        fv_logic_free(data.logic);

        destroy_graphics(&data);

        if (data.image_data)
                fv_image_data_free(data.image_data);

 out_context:
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_DeleteContext(data.gl_context);
 out_window:
        SDL_DestroyWindow(data.window);
 out_network:
        fv_network_free(data.nw);
 out_npcs:
#ifndef EMSCRIPTEN
        fv_buffer_destroy(&data.npcs);
        fv_buffer_destroy(&data.dirty_npcs);
        fv_mutex_free(data.npcs_mutex);
#endif /* EMSCRIPTEN */
        close_joysticks(&data);
#ifndef EMSCRIPTEN
 out_audio_device:
#endif /* EMSCRIPTEN */
        fv_audio_device_free(data.audio_device);
 out_audio_buffer:
        fv_audio_buffer_free(data.audio_buffer);
        SDL_Quit();
 out_addresses:
        fv_buffer_destroy(&data.server_addresses);
        return ret;
}
