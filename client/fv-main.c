/*
 * Finvenkisto
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

#ifdef EMSCRIPTEN
#include <emscripten.h>
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

struct player {
        uint32_t key_state;

        int viewport_x, viewport_y;
        int viewport_width, viewport_height;
        float center_x, center_y;
};

struct joystick {
        SDL_Joystick *joystick;
        SDL_JoystickID id;
        uint32_t button_state;
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
        MENU_STATE_CHOOSING_KEYS,
        MENU_STATE_PLAYING
};

struct data {
        /* Pointer array of server addresses to try connecting to.
         * These are const strings that point into argv and so are not
         * freed.
         */
        struct fv_buffer server_addresses;

        struct fv_shader_data shader_data;
        struct fv_game *game;
        struct fv_logic *logic;
        struct fv_hud *hud;
        struct fv_network *nw;

        SDL_Window *window;
        int last_fb_width, last_fb_height;
        SDL_GLContext gl_context;

        bool quit;
        bool is_fullscreen;

        bool viewports_dirty;
        int n_viewports;

        unsigned int last_update_time;

        enum menu_state menu_state;
        int n_players;

        struct fv_buffer joysticks;

        struct player players[FV_LOGIC_MAX_PLAYERS];

        bool redraw_queued;

#ifndef EMSCRIPTEN

        /* Event that is sent asynchronously to queue a redraw */
        Uint32 redraw_user_event;

        /* This is a cache of the NPC state that is updated
         * asynchronously. It is copied into the fv_logic just before
         * updating it. It is always accessed with the mutex locked.
         */
        SDL_mutex *npcs_mutex;
        /* Array of fv_person */
        struct fv_buffer npcs;
        /* Bitmask with a bit for each npc */
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
        data->menu_state = MENU_STATE_CHOOSING_KEYS;
        data->last_update_time = SDL_GetTicks();
        data->viewports_dirty = true;
        data->n_viewports = 1;
        data->n_players = 1;

        queue_redraw(data);

        fv_logic_reset(data->logic, 0);
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

static void
update_direction(struct data *data,
                 int player_num)
{
        const struct player *player = data->players + player_num;
        struct joystick *joystick;
        float direction;
        bool moving = true;
        int pressed_keys = 0;
        int key_mask;
        int i, j;

        for (i = 0; i < FV_N_ELEMENTS(key_mappings); i++) {
                if ((player->key_state & (1 << i)))
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

        if (pressed_keys && data->menu_state == MENU_STATE_CHOOSING_KEYS) {
                data->menu_state = MENU_STATE_PLAYING;
                data->last_update_time = SDL_GetTicks();
                fv_logic_reset(data->logic, data->n_players);
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
                moving = false;
                direction = 0.0f;
                break;
        }

        fv_logic_set_direction(data->logic, player_num, moving, direction);

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
                                data->players[0].key_state |= 1 << i;
                        else
                                data->players[0].key_state &= ~(1 << i);

                        update_direction(data, 0);

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
                        if (data->menu_state == MENU_STATE_CHOOSING_KEYS)
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

static void
handle_joystick_button(struct data *data,
                       const SDL_JoyButtonEvent *event)
{
        struct joystick *joystick = NULL;
        int i;

        for (i = 0;
             i < data->joysticks.length / sizeof (struct joystick);
             i++)  {
                joystick = (struct joystick *) data->joysticks.data + i;

                if (joystick->id == event->which)
                        goto found_joystick;
        }

        return;

found_joystick:

        for (i = 0; i < FV_N_ELEMENTS(button_mappings); i++) {
                if (button_mappings[i].button == event->button) {
                        if (event->state == SDL_PRESSED)
                                joystick->button_state |= 1 << i;
                        else
                                joystick->button_state &= ~(1 << i);

                        update_direction(data, 0);

                        break;
                }
        }
}

static void
handle_joystick_added(struct data *data,
                      const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick *joystick = SDL_JoystickOpen(event->which);
        struct joystick *joysticks = (struct joystick *) data->joysticks.data;
        struct joystick *new_joystick;
        int n_joysticks = data->joysticks.length / sizeof (struct joystick);
        SDL_JoystickID id;
        int i;

        if (joystick == NULL) {
                fprintf(stderr, "failed to open joystick %i: %s\n",
                        event->which,
                        SDL_GetError());
                return;
        }

        id = SDL_JoystickInstanceID(joystick);

        /* Check if we already have this joystick open */
        for (i = 0; i < n_joysticks; i++) {
                if (joysticks[i].id == id) {
                        SDL_JoystickClose(joystick);
                        return;
                }
        }

        fv_buffer_set_length(&data->joysticks,
                             data->joysticks.length + sizeof (struct joystick));

        new_joystick = (struct joystick *) data->joysticks.data + n_joysticks;
        new_joystick->joystick = joystick;
        new_joystick->id = id;
        new_joystick->button_state = 0;
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

        case SDL_JOYBUTTONDOWN:
        case SDL_JOYBUTTONUP:
                handle_joystick_button(data, &event->jbutton);
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
        case MENU_STATE_CHOOSING_KEYS:
                fv_hud_paint_key_select(data->hud,
                                        w, h,
                                        0, 0,
                                        data->n_players);
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
update_viewports(struct data *data)
{
        int viewport_width, viewport_height;
        int vertical_divisions = 1;
        int i;

        if (!data->viewports_dirty)
                return;

        data->n_viewports = data->n_players;

        viewport_width = data->last_fb_width;
        viewport_height = data->last_fb_height;

        if (data->n_viewports > 1) {
                viewport_width /= 2;
                if (data->n_viewports > 2) {
                        viewport_height /= 2;
                        vertical_divisions = 2;
                }
        }

        for (i = 0; i < data->n_viewports; i++) {
                data->players[i].viewport_x = i % 2 * viewport_width;
                data->players[i].viewport_y = (vertical_divisions - 1 -
                                               i / 2) * viewport_height;
                data->players[i].viewport_width = viewport_width;
                data->players[i].viewport_height = viewport_height;
        }

        data->viewports_dirty = false;
}

static void
update_centers(struct data *data)
{
        int i;

        if (data->menu_state == MENU_STATE_PLAYING) {
                for (i = 0; i < data->n_viewports; i++) {
                        fv_logic_get_center(data->logic,
                                            i,
                                            &data->players[i].center_x,
                                            &data->players[i].center_y);
                }
        } else {
                for (i = 0; i < data->n_viewports; i++) {
                        data->players[i].center_x = FV_MAP_START_X;
                        data->players[i].center_y = FV_MAP_START_Y;
                }
        }
}

static bool
need_clear(struct data *data)
{
        struct player *player;
        int i;

        /* If there are only 3 divisions then one of the panels will
         * be blank so we always need to clear */
        if (data->n_viewports == 3)
                return true;

        /* If the window is an odd size then the divisions might not
         * cover the entire window */
        if (data->n_viewports >= 2) {
                if (data->last_fb_width & 1)
                        return true;
                if (data->n_viewports >= 3 && (data->last_fb_height & 1))
                        return true;
        }

        /* Otherwise check if all of the divisions currently cover
         * their visible area */
        for (i = 0; i < data->n_viewports; i++) {
                player = data->players + i;
                if (!fv_game_covers_framebuffer(data->game,
                                                player->center_x,
                                                player->center_y,
                                                player->viewport_width,
                                                player->viewport_height))
                        return true;
        }

        return false;
}

static void
update_npcs(struct data *data)
{
#ifndef EMSCRIPTEN
        const struct fv_person *npcs;
        int npc_num;

        SDL_LockMutex(data->npcs_mutex);

        npcs = (const struct fv_person *) data->npcs.data;

        fv_logic_set_n_npcs(data->logic,
                            data->npcs.length /
                            sizeof (struct fv_person));

        fv_bitmask_for_each(&data->dirty_npcs, npc_num) {
                fv_logic_update_npc(data->logic,
                                    npc_num,
                                    npcs + npc_num);
        }

        memset(data->dirty_npcs.data, 0, data->dirty_npcs.length);

        SDL_UnlockMutex(data->npcs_mutex);
#endif
}

static void
paint(struct data *data)
{
        GLbitfield clear_mask = GL_DEPTH_BUFFER_BIT;
        struct fv_person player;
        enum fv_logic_state_change state_change;
        unsigned int now;
        int w, h;
        int i;

        SDL_GetWindowSize(data->window, &w, &h);

        if (w != data->last_fb_width || h != data->last_fb_height) {
                fv_gl.glViewport(0, 0, w, h);
                data->last_fb_width = w;
                data->last_fb_height = h;
                data->viewports_dirty = true;
        }

        update_npcs(data);

        now = SDL_GetTicks();
        state_change = fv_logic_update(data->logic,
                                       now - data->last_update_time);
        data->last_update_time = now;

        if ((state_change & FV_LOGIC_STATE_CHANGE_PLAYER)) {
                fv_logic_get_player(data->logic,
                                    0 /* player_num */,
                                    &player);
                fv_network_update_player(data->nw, &player);
        }

        update_viewports(data);
        update_centers(data);

        if (need_clear(data))
                clear_mask |= GL_COLOR_BUFFER_BIT;

        fv_gl.glClear(clear_mask);

        for (i = 0; i < data->n_viewports; i++) {
                if (data->n_viewports != 1)
                        fv_gl.glViewport(data->players[i].viewport_x,
                                         data->players[i].viewport_y,
                                         data->players[i].viewport_width,
                                         data->players[i].viewport_height);
                fv_game_paint(data->game,
                              data->players[i].center_x,
                              data->players[i].center_y,
                              data->players[i].viewport_width,
                              data->players[i].viewport_height,
                              data->logic);
        }

        if (data->n_viewports != 1)
                fv_gl.glViewport(0, 0, w, h);

        paint_hud(data, w, h);

        SDL_GL_SwapWindow(data->window);

        /* If the logic has become stable then we'll stop redrawing
         * until something changes.
         */
        if (state_change == 0) {
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
        int player_num;

        fv_logic_set_n_npcs(data->logic, event->n_players);

        fv_bitmask_for_each(event->dirty_players, player_num) {
                fv_logic_update_npc(data->logic,
                                    player_num,
                                    &event->players[player_num]);
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
        int player_num;

        SDL_LockMutex(data->npcs_mutex);

        fv_buffer_set_length(&data->npcs,
                             sizeof (struct fv_person) * event->n_players);
        fv_bitmask_set_length(&data->dirty_npcs, event->n_players);
        fv_bitmask_or(&data->dirty_npcs, event->dirty_players);

        fv_bitmask_for_each(event->dirty_players, player_num) {
                memcpy(data->npcs.data +
                       player_num * sizeof (struct fv_person),
                       &event->players[player_num],
                       sizeof (struct fv_person));
        }

        SDL_PushEvent(&redraw_event);

        SDL_UnlockMutex(data->npcs_mutex);
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
        printf("Finvenkisto - Virtual Language Exchange\n"
               "usage: finvenkisto [options]\n"
               "Options:\n"
               " -h       Show this help messae\n"
               " -w       Run in a window\n"
               " -f       Run fullscreen (default)\n");
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

        paint(data);
}

static int
emscripten_event_filter(void *userdata,
                        SDL_Event *event)
{
        handle_event(userdata, event);

        /* Filter the event */
        return 0;
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
                        paint(data);
        }
}

#endif /* EMSCRIPTEN */

int
main(int argc, char **argv)
{
        struct data data;
        Uint32 flags;
        int res;
        int ret = EXIT_SUCCESS;

        fv_buffer_init(&data.server_addresses);

#ifdef EMSCRIPTEN
        data.is_fullscreen = false;
#else
        data.is_fullscreen = true;
#endif

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out_addresses;
        }

        res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
        if (res < 0) {
                fv_error_message("Unable to init SDL: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_addresses;
        }

        memset(&data.players, 0, sizeof data.players);

        data.redraw_queued = true;

#ifndef EMSCRIPTEN

        data.npcs_mutex = SDL_CreateMutex();
        if (data.npcs_mutex == NULL) {
                fv_error_message("Failed to create mutex");
                ret = EXIT_FAILURE;
                goto out_sdl;
        }

        fv_buffer_init(&data.npcs);
        fv_buffer_init(&data.dirty_npcs);

        data.redraw_user_event = SDL_RegisterEvents(1);

#endif /* EMSCRIPTEN */

        data.nw = fv_network_new(consistent_event_cb, &data);

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

        data.window = SDL_CreateWindow("Finvenkisto",
                                       SDL_WINDOWPOS_UNDEFINED,
                                       SDL_WINDOWPOS_UNDEFINED,
                                       800, 600,
                                       flags);

        if (data.window == NULL) {
                fv_error_message("Failed to create SDL window: %s",
                                 SDL_GetError());
                ret = EXIT_FAILURE;
                goto out_network;
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

        SDL_ShowCursor(0);

        /* All of the painting functions expect to have the default
         * OpenGL state plus the following modifications */

        fv_gl.glEnable(GL_CULL_FACE);
        fv_gl.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        /* The current program, vertex array, array buffer and bound
         * textures are not expected to be reset back to zero */

        data.quit = false;
        data.last_fb_width = data.last_fb_height = 0;

        if (!fv_shader_data_init(&data.shader_data)) {
                ret = EXIT_FAILURE;
                goto out_context;
        }

        data.hud = fv_hud_new(&data.shader_data);

        if (data.hud == NULL) {
                ret = EXIT_FAILURE;
                goto out_shader_data;
        }

        data.game = fv_game_new(&data.shader_data);

        if (data.game == NULL) {
                ret = EXIT_FAILURE;
                goto out_hud;
        }

        data.logic = fv_logic_new();

        reset_menu_state(&data);

#ifdef EMSCRIPTEN

        SDL_SetEventFilter(emscripten_event_filter, &data);

        emscripten_set_main_loop_arg(emscripten_loop_cb,
                                     &data,
                                     0, /* fps (use browser's choice) */
                                     true /* simulate infinite loop */);

#else /* EMSCRIPTEN */

        run_main_loop(&data);

#endif /* EMSCRIPTEN */

        fv_logic_free(data.logic);

        fv_game_free(data.game);
 out_hud:
        fv_hud_free(data.hud);
 out_shader_data:
        fv_shader_data_destroy(&data.shader_data);
 out_context:
        SDL_GL_MakeCurrent(NULL, NULL);
        SDL_GL_DeleteContext(data.gl_context);
 out_window:
        SDL_DestroyWindow(data.window);
 out_network:
        fv_network_free(data.nw);
#ifndef EMSCRIPTEN
        fv_buffer_destroy(&data.npcs);
        fv_buffer_destroy(&data.dirty_npcs);
        SDL_DestroyMutex(data.npcs_mutex);
#endif /* EMSCRIPTEN */
        close_joysticks(&data);
#ifndef EMSCRIPTEN
 out_sdl:
#endif /* EMSCRIPTEN */
        SDL_Quit();
 out_addresses:
        fv_buffer_destroy(&data.server_addresses);
        return ret;
}
