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

#define MIN_GL_MAJOR_VERSION 2
#define MIN_GL_MINOR_VERSION 0
#define CORE_GL_MAJOR_VERSION 3
#define CORE_GL_MINOR_VERSION 1

enum key_code {
        KEY_CODE_UP,
        KEY_CODE_DOWN,
        KEY_CODE_LEFT,
        KEY_CODE_RIGHT
};

#define N_KEYS 4

enum key_type {
        KEY_TYPE_KEYBOARD,
        KEY_TYPE_JOYSTICK,
        KEY_TYPE_MOUSE
};

struct key {
        enum key_type type;
        SDL_Keycode keycode;
        SDL_JoystickID device_id;
        Uint8 button;
        bool down;
};

struct player {
        struct key keys[N_KEYS];

        int viewport_x, viewport_y;
        int viewport_width, viewport_height;
        float center_x, center_y;
};

enum menu_state {
        MENU_STATE_CHOOSING_KEYS,
        MENU_STATE_PLAYING
};

struct data {
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
        int next_player;
        enum key_code next_key;

        struct fv_buffer joysticks;

        struct player players[FV_LOGIC_MAX_PLAYERS];

        bool redraw_queued;
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
};

static void
reset_menu_state(struct data *data)
{
        int i, j;

        data->menu_state = MENU_STATE_CHOOSING_KEYS;
        data->last_update_time = SDL_GetTicks();
        data->viewports_dirty = true;
        data->n_viewports = 1;
        data->n_players = 1;
        data->next_player = 0;
        data->next_key = 0;
        data->redraw_queued = true;

        for (i = 0; i < FV_LOGIC_MAX_PLAYERS; i++) {
                for (j = 0; j < N_KEYS; j++) {
                        data->players[i].keys[j].down = false;
                        data->players[i].keys[j].down = false;
                }
        }

        fv_logic_reset(data->logic, 0);
}

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
        data->redraw_queued = true;

        SDL_SetWindowFullscreen(data->window, data->is_fullscreen);
}

static void
update_direction(struct data *data,
                 int player_num)
{
        const struct player *player = data->players + player_num;
        float direction;
        bool moving = true;
        int pressed_keys = 0;
        int key_mask;

        if (player->keys[KEY_CODE_UP].down)
                pressed_keys |= 1 << KEY_CODE_UP;
        if (player->keys[KEY_CODE_DOWN].down)
                pressed_keys |= 1 << KEY_CODE_DOWN;
        if (player->keys[KEY_CODE_LEFT].down)
                pressed_keys |= 1 << KEY_CODE_LEFT;
        if (player->keys[KEY_CODE_RIGHT].down)
                pressed_keys |= 1 << KEY_CODE_RIGHT;

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
}

static bool
is_key(const struct key *key,
       const struct key *other_key)
{
        if (key->type != other_key->type)
                return false;

        switch (key->type) {
        case KEY_TYPE_KEYBOARD:
                return key->keycode == other_key->keycode;

        case KEY_TYPE_JOYSTICK:
        case KEY_TYPE_MOUSE:
                return (key->device_id == other_key->device_id &&
                        key->button == other_key->button);
        }

        assert(false);

        return false;
}

static void
set_key(struct data *data,
        const struct key *other_key)
{
        data->players[data->next_player].keys[data->next_key] = *other_key;
        data->next_key++;
        data->redraw_queued = true;

        if (data->next_key >= N_KEYS) {
                data->next_player++;
                data->next_key = 0;

                if (data->next_player >= data->n_players) {
                        data->menu_state = MENU_STATE_PLAYING;
                        data->last_update_time = SDL_GetTicks();
                        fv_logic_reset(data->logic, data->n_players);
                }
        }
}

static void
set_key_state(struct data *data,
              int player_num,
              enum key_code key,
              bool state)
{
        bool old_state = data->players[player_num].keys[key].down;

        if (old_state == state)
                return;

        data->players[player_num].keys[key].down = state;

        update_direction(data, player_num);

        data->redraw_queued = true;
}

static void
handle_key(struct data *data,
           const struct key *key)
{
        struct key *player_key;
        int i, j;

        switch (data->menu_state) {
        case MENU_STATE_CHOOSING_KEYS:
                for (i = 0; i < data->next_player; i++) {
                        for (j = 0; j < N_KEYS; j++) {
                                player_key = data->players[i].keys + j;
                                if (is_key(player_key, key)) {
                                        set_key_state(data, i, j, key->down);
                                        goto handled;
                                }
                        }
                }

                for (j = 0; j < data->next_key; j++) {
                        player_key = data->players[i].keys + j;
                        if (is_key(player_key, key)) {
                                set_key_state(data, i, j, key->down);
                                goto handled;
                        }
                }

                if (key->down)
                        set_key(data, key);

        handled:
                break;

        case MENU_STATE_PLAYING:
                for (i = 0; i < data->n_players; i++) {
                        for (j = 0; j < N_KEYS; j++) {
                                player_key = data->players[i].keys + j;
                                if (is_key(player_key, key)) {
                                        set_key_state(data, i, j, key->down);
                                        goto found;
                                }
                        }
                }
        found:
                break;
        }
}

static void
handle_other_key(struct data *data,
                 const SDL_KeyboardEvent *event)
{
        struct key key;

        key.type = KEY_TYPE_KEYBOARD;
        key.keycode = event->keysym.sym;
        key.down = event->state == SDL_PRESSED;

        handle_key(data, &key);
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

        case SDLK_F11:
                if (event->state == SDL_PRESSED)
                        toggle_fullscreen(data);
                break;

        default:
                handle_other_key(data, event);
                break;
        }
}

static void
handle_joystick_button(struct data *data,
                       const SDL_JoyButtonEvent *event)
{
        struct key key;

        key.type = KEY_TYPE_JOYSTICK;
        key.device_id = event->which;
        key.button = event->button;
        key.down = event->state == SDL_PRESSED;

        handle_key(data, &key);
}

static void
handle_joystick_added(struct data *data,
                      const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick *joystick = SDL_JoystickOpen(event->which);
        SDL_Joystick **joysticks = (SDL_Joystick **) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (SDL_Joystick *);
        int i;

        if (joystick == NULL) {
                fprintf(stderr, "failed to open joystick %i: %s\n",
                        event->which,
                        SDL_GetError());
                return;
        }

        /* Check if we already have this joystick open */
        for (i = 0; i < n_joysticks; i++) {
                if (SDL_JoystickInstanceID(joysticks[i]) ==
                    SDL_JoystickInstanceID(joystick)) {
                        SDL_JoystickClose(joystick);
                        return;
                }
        }

        fv_buffer_append(&data->joysticks, &joystick, sizeof joystick);
}

static void
handle_joystick_removed(struct data *data,
                        const SDL_JoyDeviceEvent *event)
{
        SDL_Joystick **joysticks = (SDL_Joystick **) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (SDL_Joystick *);
        int i;

        for (i = 0; i < n_joysticks; i++) {
                if (SDL_JoystickInstanceID(joysticks[i]) == event->which) {
                        SDL_JoystickClose(joysticks[i]);
                        if (i < n_joysticks - 1)
                                joysticks[i] = joysticks[n_joysticks - 1];
                        data->joysticks.length -= sizeof (SDL_Joystick *);
                        break;
                }
        }
}

static void
handle_mouse_button(struct data *data,
                    const SDL_MouseButtonEvent *event)
{
        struct key key;

        key.type = KEY_TYPE_MOUSE;
        key.device_id = event->which;
        key.button = event->button;
        key.down = event->state == SDL_PRESSED;

        handle_key(data, &key);
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
                case SDL_WINDOWEVENT_EXPOSED:
                        data->redraw_queued = true;
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

        if (event->type == data->redraw_user_event) {
                data->redraw_queued = true;
                goto handled;
        }

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
                                        data->next_player,
                                        data->next_key,
                                        data->n_players);
                break;
        case MENU_STATE_PLAYING:
                break;
        }
}

static void
close_joysticks(struct data *data)
{
        SDL_Joystick **joysticks = (SDL_Joystick **) data->joysticks.data;
        int n_joysticks = data->joysticks.length / sizeof (SDL_Joystick *);
        int i;

        for (i = 0; i < n_joysticks; i++)
                SDL_JoystickClose(joysticks[i]);

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

        /* If the logic hasn't become stable then we'll queue another
         * redraw immediately so that we'll continue updating the
         * logic.
         */
        if (state_change == 0)
                data->redraw_queued = false;
}

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
        printf("Finvenkisto - Instruludo por venigi la finan venkon\n"
               "uzo: finvenkisto [opcioj]\n"
               "Opcioj:\n"
               " -h       Montru ĉi tiun helpmesaĝon\n"
               " -f       Rulu la ludon en fenestro\n"
               " -p       Rulu la ludon plenekrane (defaŭlto)\n");
}

static bool
process_argument_flags(struct data *data,
                       const char *flags)
{
        while (*flags) {
                switch (*flags) {
                case 'h':
                        show_help();
                        return false;

                case 'f':
                        data->is_fullscreen = false;
                        break;

                case 'p':
                        data->is_fullscreen = true;
                        break;

                default:
                        fprintf(stderr, "Neatendita opcio ‘%c’\n", *flags);
                        show_help();
                        return false;
                }

                flags++;
        }

        return true;
}

static bool
process_arguments(struct data *data,
                  int argc, char **argv)
{
        int i;

        for (i = 1; i < argc; i++) {
                if (argv[i][0] == '-') {
                        if (!process_argument_flags(data, argv[i] + 1))
                                return false;
                } else {
                        fprintf(stderr, "Neatendita argumento ‘%s’\n", argv[i]);
                        show_help();
                        return false;
                }
        }

        return true;
}

static SDL_GLContext
create_gl_context(SDL_Window *window)
{
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

        /* Otherwise try a compatibility profile context */
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,
                            MIN_GL_MAJOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,
                            MIN_GL_MINOR_VERSION);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);

        return SDL_GL_CreateContext(window);
}

int
main(int argc, char **argv)
{
        struct data data;
        SDL_Event event;
        Uint32 flags;
        bool had_event;
        int res;
        int ret;

        data.is_fullscreen = true;

        if (!process_arguments(&data, argc, argv)) {
                ret = EXIT_FAILURE;
                goto out;
        }

        res = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
        if (res < 0) {
                fv_error_message("Unable to init SDL: %s\n", SDL_GetError());
                ret = EXIT_FAILURE;
                goto out;
        }

        data.redraw_user_event = SDL_RegisterEvents(1);
        data.redraw_queued = true;

        data.npcs_mutex = SDL_CreateMutex();
        if (data.npcs_mutex == NULL) {
                fv_error_message("Failed to create mutex");
                ret = EXIT_FAILURE;
                goto out_sdl;
        }

        fv_buffer_init(&data.npcs);
        fv_buffer_init(&data.dirty_npcs);

        data.nw = fv_network_new(consistent_event_cb, &data);
        fv_network_add_host(data.nw, "localhost");

        fv_buffer_init(&data.joysticks);

        SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 8);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

        flags = SDL_WINDOW_OPENGL;
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

        while (!data.quit) {
                if (data.redraw_queued) {
                        had_event = SDL_PollEvent(&event);
                } else {
                        had_event = SDL_WaitEvent(&event);
                        data.last_update_time = SDL_GetTicks();
                }

                if (had_event)
                        handle_event(&data, &event);
                else if (data.redraw_queued)
                        paint(&data);
        }

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
        fv_buffer_destroy(&data.npcs);
        fv_buffer_destroy(&data.dirty_npcs);
        SDL_DestroyMutex(data.npcs_mutex);
        close_joysticks(&data);
 out_sdl:
        SDL_Quit();
 out:
        return ret;
}
