/*
 * Finvenkisto
 *
 * Copyright (C) 2013, 2014 Neil Roberts
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
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "fv-logic.h"
#include "fv-util.h"
#include "fv-map.h"
#include "fv-buffer.h"

/* Player movement speed measured in blocks per second */
#define FV_LOGIC_PLAYER_SPEED 10.0f

/* Turn speed of a person in radians per second */
#define FV_LOGIC_TURN_SPEED (2.5f * M_PI)

/* Maximum distance to the player from the center point before it will
 * start scrolling */
#define FV_LOGIC_CAMERA_DISTANCE 3.0f

/* The size of a person. When checking against something this
 * represents a square centered at the person's position. When being
 * checked against for person-person collisions it is a circle with
 * this diameter */
#define FV_LOGIC_PERSON_SIZE 0.8f

/* Gap between player positions at the start of the game */
#define FV_LOGIC_PLAYER_START_GAP 2.0f

struct fv_logic_position {
        float x, y;
        float current_direction;
        float target_direction;
        float speed;
};

struct fv_logic_player {
        struct fv_logic_position position;
        float center_x, center_y;
};

struct fv_logic_npc {
        struct fv_logic_position position;
};

struct fv_logic {
        enum fv_logic_state state;

        unsigned int last_ticks;

        struct fv_logic_player players[FV_LOGIC_MAX_PLAYERS];
        int n_players;

        /* NPC player state. This state is not reset. Array of fv_logic_npc */
        struct fv_buffer npcs;
};

void
fv_logic_reset(struct fv_logic *logic,
               int n_players)
{
        struct fv_logic_player *player;
        int i;

        logic->last_ticks = 0;
        logic->n_players = n_players;

        for (i = 0; i < n_players; i++) {
                player = logic->players + i;
                player->position.x = (FV_MAP_START_X -
                                      (n_players - 1) *
                                      FV_LOGIC_PLAYER_START_GAP / 2.0f +
                                      i * FV_LOGIC_PLAYER_START_GAP);
                player->position.y = FV_MAP_START_Y;
                player->position.current_direction = -M_PI / 2.0f;
                player->position.target_direction = 0.0f;
                player->position.speed = 0.0f;

                player->center_x = player->position.x;
                player->center_y = player->position.y;
        }

        if (n_players == 0)
                logic->state = FV_LOGIC_STATE_NO_PLAYERS;
        else
                logic->state = FV_LOGIC_STATE_RUNNING;
}

struct fv_logic *
fv_logic_new(void)
{
        struct fv_logic *logic = fv_alloc(sizeof *logic);

        fv_buffer_init(&logic->npcs);
        fv_logic_reset(logic, 0);

        return logic;
}

static bool
is_wall(int x, int y)
{
        if (x < 0 || x >= FV_MAP_WIDTH ||
            y < 0 || y >= FV_MAP_HEIGHT)
                return true;

        return FV_MAP_IS_WALL(fv_map[y * FV_MAP_WIDTH + x]);
}

static bool
position_in_range(const struct fv_logic_position *position,
                  float x, float y,
                  float distance)
{
        float dx = x - position->x;
        float dy = y - position->y;

        return dx * dx + dy * dy < distance * distance;
}

static bool
person_blocking(const struct fv_logic *logic,
                const struct fv_logic_position *this_position,
                float x, float y)
{
        const struct fv_logic_npc *npc;
        int i;

        for (i = 0; i < logic->n_players; i++) {
                if (this_position == &logic->players[i].position)
                        continue;

                if (position_in_range(&logic->players[i].position,
                                      x, y,
                                      FV_LOGIC_PERSON_SIZE / 2.0f))
                        return true;
        }

        for (i = 0;
             i < logic->npcs.length / sizeof (struct fv_logic_npc);
             i++) {
                npc = (struct fv_logic_npc *) logic->npcs.data + i;

                if (this_position == &npc->position)
                        continue;

                if (position_in_range(&npc->position,
                                      x, y,
                                      FV_LOGIC_PERSON_SIZE / 2.0f))
                        return true;
        }

        return false;
}

static void
update_position_direction(struct fv_logic *logic,
                          struct fv_logic_position *position,
                          float progress_secs)
{
        float diff, turned;

        if (position->target_direction == position->current_direction)
                return;

        diff = position->target_direction - position->current_direction;

        if (diff > M_PI)
                diff = diff - 2.0f * M_PI;
        else if (diff < -M_PI)
                diff = 2.0f * M_PI + diff;

        turned = progress_secs * FV_LOGIC_TURN_SPEED;

        if (turned >= fabsf(diff))
                position->current_direction =
                        position->target_direction;
        else if (diff < 0.0f)
                position->current_direction -= turned;
        else
                position->current_direction += turned;
}

static void
update_position_xy(struct fv_logic *logic,
                   struct fv_logic_position *position,
                   float progress_secs)
{
        float distance;
        float diff;
        float pos;

        distance = position->speed * progress_secs;

        diff = distance * cosf(position->target_direction);

        /* Don't let the player move more than one tile per frame
         * because otherwise it might be possible to skip over
         * walls */
        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (position->x + diff +
               copysignf(FV_LOGIC_PERSON_SIZE / 2.0f, diff));
        if (!is_wall(floorf(pos),
                     floorf(position->y + FV_LOGIC_PERSON_SIZE / 2.0f)) &&
            !is_wall(floorf(pos),
                     floorf(position->y - FV_LOGIC_PERSON_SIZE / 2.0f)) &&
            !person_blocking(logic, position, pos, position->y))
                position->x += diff;

        diff = distance * sinf(position->target_direction);

        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (position->y + diff +
               copysignf(FV_LOGIC_PERSON_SIZE / 2.0f, diff));
        if (!is_wall(floorf(position->x + FV_LOGIC_PERSON_SIZE / 2.0f),
                     floorf(pos)) &&
            !is_wall(floorf(position->x - FV_LOGIC_PERSON_SIZE / 2.0f),
                     floorf(pos)) &&
            !person_blocking(logic, position, position->x, pos))
                position->y += diff;
}

static void
update_position(struct fv_logic *logic,
                struct fv_logic_position *position,
                float progress_secs)
{
        if (position->speed == 0.0f)
                return;

        update_position_xy(logic, position, progress_secs);
        update_position_direction(logic, position, progress_secs);
}

static void
update_center(struct fv_logic_player *player)
{
        float dx = player->position.x - player->center_x;
        float dy = player->position.y - player->center_y;
        float d2, d;

        d2 = dx * dx + dy * dy;

        if (d2 > FV_LOGIC_CAMERA_DISTANCE * FV_LOGIC_CAMERA_DISTANCE) {
                d = sqrtf(d2);
                player->center_x += dx * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
                player->center_y += dy * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
        }
}

static void
update_player_movement(struct fv_logic *logic,
                       struct fv_logic_player *player,
                       float progress_secs)
{
        if (!player->position.speed)
                return;

        update_position(logic, &player->position, progress_secs);
        update_center(player);
}

void
fv_logic_update(struct fv_logic *logic, unsigned int ticks)
{
        unsigned int progress = ticks - logic->last_ticks;
        float progress_secs;
        int i;

        logic->last_ticks = ticks;

        /* If we've skipped over half a second then we'll assume something
         * has gone wrong and we won't do anything */
        if (progress >= 500 || progress < 0)
                return;

        if (logic->state != FV_LOGIC_STATE_RUNNING)
                return;

        progress_secs = progress / 1000.0f;

        for (i = 0; i < logic->n_players; i++)
                update_player_movement(logic,
                                       logic->players + i,
                                       progress_secs);
}

void
fv_logic_set_direction(struct fv_logic *logic,
                       int player_num,
                       bool moving,
                       float direction)
{
        struct fv_logic_player *player = logic->players + player_num;

        if (moving) {
                player->position.speed = FV_LOGIC_PLAYER_SPEED;
                player->position.target_direction = direction;
        } else {
                player->position.speed = 0.0f;
        }
}

void
fv_logic_set_n_npcs(struct fv_logic *logic,
                    int n_npcs)
{
        fv_buffer_set_length(&logic->npcs,
                             sizeof (struct fv_logic_npc) * n_npcs);
}

void
fv_logic_update_npc(struct fv_logic *logic,
                    int npc_num,
                    const struct fv_person *person)
{
        struct fv_logic_npc *npc;
        struct fv_logic_position *pos;

        assert(npc_num < logic->npcs.length / sizeof (struct fv_logic_npc));

        npc = (struct fv_logic_npc *) logic->npcs.data + npc_num;
        pos = &npc->position;

        pos->x = person->x_position / (float) UINT32_MAX * FV_MAP_WIDTH;
        pos->y = person->y_position / (float) UINT32_MAX * FV_MAP_HEIGHT;
        pos->current_direction = (person->direction / (float) UINT16_MAX *
                                  2 * M_PI);
        if (pos->current_direction > M_PI)
                pos->current_direction -= 2 * M_PI;
        pos->target_direction = pos->current_direction;
        pos->speed = 0.0f;
}

void
fv_logic_free(struct fv_logic *logic)
{
        fv_buffer_destroy(&logic->npcs);
        fv_free(logic);
}

void
fv_logic_get_center(struct fv_logic *logic,
                    int player_num,
                    float *x, float *y)
{
        *x = logic->players[player_num].center_x;
        *y = logic->players[player_num].center_y;
}

void
fv_logic_for_each_person(struct fv_logic *logic,
                         fv_logic_person_cb person_cb,
                         void *user_data)
{
        const struct fv_logic_player *player;
        const struct fv_logic_npc *npc;
        struct fv_logic_person person;
        int i;

        person.type = FV_PERSON_TYPE_FINVENKISTO;

        for (i = 0; i < logic->n_players; i++) {
                player = logic->players + i;

                person.x = player->position.x;
                person.y = player->position.y;
                person.direction = player->position.current_direction;

                person_cb(&person, user_data);
        }

        person.type = FV_PERSON_TYPE_PYJAMAS;

        for (i = 0;
             i < logic->npcs.length / sizeof (struct fv_logic_npc);
             i++) {
                npc = (struct fv_logic_npc *) logic->npcs.data + i;

                person.x = npc->position.x;
                person.y = npc->position.y;
                person.direction = npc->position.current_direction;

                person_cb(&person, user_data);
        }
}

int
fv_logic_get_n_players(struct fv_logic *logic)
{
        return logic->n_players;
}

enum fv_logic_state
fv_logic_get_state(struct fv_logic *logic)
{
        return logic->state;
}
