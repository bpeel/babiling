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
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>

#include "fv-logic.h"
#include "fv-util.h"
#include "fv-map.h"
#include "fv-buffer.h"
#include "fv-random.h"

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

/* Acceleration in blocks per second² at which the player changes its
 * speed to match the target speed specified by the controls. If the
 * player needs to decelerate then it happens instantly.
 */
#define FV_LOGIC_ACCELERATION 20.0f

struct fv_logic_player {
        struct fv_logic_person person;
        float target_direction;
        float current_speed;
        float target_speed;
        float center_x, center_y;
};

struct fv_logic_npc {
        struct fv_logic_person person;
};

struct fv_logic {
        enum fv_logic_state state;

        struct fv_logic_player player;

        /* NPC player state. This state is not reset. Array of fv_logic_npc */
        struct fv_buffer npcs;
};

struct fv_logic *
fv_logic_new(void)
{
        struct fv_logic *logic = fv_alloc(sizeof *logic);
        struct fv_logic_player *player = &logic->player;

        fv_buffer_init(&logic->npcs);

        player = &logic->player;
        player->person.x = FV_MAP_START_X;
        player->person.y = FV_MAP_START_Y;
        player->person.direction = -M_PI / 2.0f;
        player->person.type = fv_random_range(0, FV_PERSON_N_TYPES);
        player->person.n_flags = 0;

        player->target_direction = 0.0f;
        player->current_speed = 0.0f;
        player->target_speed = 0.0f;

        player->center_x = player->person.x;
        player->center_y = player->person.y;

        logic->state = FV_LOGIC_STATE_RUNNING;

        return logic;
}

static bool
is_wall(int x, int y)
{
        if (x < 0 || x >= FV_MAP_WIDTH ||
            y < 0 || y >= FV_MAP_HEIGHT)
                return true;

        return FV_MAP_IS_WALL(fv_map.blocks[y * FV_MAP_WIDTH + x]);
}

static bool
person_in_range(const struct fv_logic_person *person,
                float x, float y,
                float distance)
{
        float dx = x - person->x;
        float dy = y - person->y;

        return dx * dx + dy * dy < distance * distance;
}

static bool
person_blocking(const struct fv_logic *logic,
                const struct fv_logic_person *this_person,
                float x, float y)
{
        const struct fv_logic_npc *npc;
        int i;

        if (this_person != &logic->player.person &&
            person_in_range(&logic->player.person,
                            x, y,
                            FV_LOGIC_PERSON_SIZE / 2.0f))
                return true;

        for (i = 0;
             i < logic->npcs.length / sizeof (struct fv_logic_npc);
             i++) {
                npc = (struct fv_logic_npc *) logic->npcs.data + i;

                if (this_person == &npc->person)
                        continue;

                if (person_in_range(&npc->person,
                                    x, y,
                                    FV_LOGIC_PERSON_SIZE / 2.0f))
                        return true;
        }

        return false;
}

static bool
update_player_direction(struct fv_logic *logic,
                        struct fv_logic_player *player,
                        float progress_secs)
{
        float diff, turned;

        if (player->target_direction == player->person.direction)
                return false;

        diff = player->target_direction - player->person.direction;

        if (diff > M_PI)
                diff = diff - 2.0f * M_PI;
        else if (diff < -M_PI)
                diff = 2.0f * M_PI + diff;

        turned = progress_secs * FV_LOGIC_TURN_SPEED;

        if (turned >= fabsf(diff))
                player->person.direction = player->target_direction;
        else if (diff < 0.0f)
                player->person.direction -= turned;
        else
                player->person.direction += turned;

        return true;
}

static bool
update_player_xy(struct fv_logic *logic,
                 struct fv_logic_player *player,
                 float speed,
                 float progress_secs)
{
        bool ret = false;
        float distance;
        float diff;
        float pos;

        distance = speed * progress_secs;

        diff = distance * cosf(player->target_direction);

        /* Don't let the player move more than one tile per frame
         * because otherwise it might be possible to skip over
         * walls */
        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (player->person.x + diff +
               copysignf(FV_LOGIC_PERSON_SIZE / 2.0f, diff));
        if (!is_wall(floorf(pos),
                     floorf(player->person.y + FV_LOGIC_PERSON_SIZE / 2.0f)) &&
            !is_wall(floorf(pos),
                     floorf(player->person.y - FV_LOGIC_PERSON_SIZE / 2.0f)) &&
            !person_blocking(logic, &player->person, pos, player->person.y)) {
                player->person.x += diff;
                ret = true;
        }

        diff = distance * sinf(player->target_direction);

        if (fabsf(diff) > 1.0f)
                diff = copysign(1.0f, diff);

        pos = (player->person.y + diff +
               copysignf(FV_LOGIC_PERSON_SIZE / 2.0f, diff));
        if (!is_wall(floorf(player->person.x + FV_LOGIC_PERSON_SIZE / 2.0f),
                     floorf(pos)) &&
            !is_wall(floorf(player->person.x - FV_LOGIC_PERSON_SIZE / 2.0f),
                     floorf(pos)) &&
            !person_blocking(logic, &player->person, player->person.x, pos)) {
                player->person.y += diff;
                ret = true;
        }

        /* If the player hits a wall then they will have to accelerate
         * again to move away.
         */
        if (!ret)
                player->current_speed = 0.0f;

        return ret;
}

/* Updates the current speed according to the target speed and the
 * acceleration and returns the average speed that happened during
 * that time.
 */
static float
update_player_speed(struct fv_logic_player *player,
                    float progress_secs)
{
        float direction_difference;
        float target_difference;
        float time_difference;
        float average_speed;
        float acceleration_time;
        float average_acceleration_speed;

        /* If the target angle is more than 90.5° away from the
         * current angle then the player can't move at all until they
         * finish turning.
         */
        direction_difference = fabsf(player->target_direction -
                                     player->person.direction);
        if (direction_difference > M_PI)
                direction_difference = 2.0f * M_PI - direction_difference;
        if (direction_difference > 90.5f * M_PI / 180.0f)
                return player->current_speed = 0.0f;

        target_difference = player->target_speed - player->current_speed;

        /* Deceleration happens instantly */
        if (target_difference <= 0.0f)
                return player->current_speed = player->target_speed;

        time_difference = FV_LOGIC_ACCELERATION * progress_secs;

        if (time_difference < target_difference) {
                average_speed = (player->current_speed +
                                 time_difference / 2.0f);
                player->current_speed += time_difference;
                return average_speed;
        }

        acceleration_time = target_difference / FV_LOGIC_ACCELERATION;
        average_acceleration_speed = (player->current_speed +
                                      player->target_speed) / 2.0f;
        average_speed = ((average_acceleration_speed *
                          acceleration_time +
                          player->target_speed *
                          (progress_secs - acceleration_time)) /
                         progress_secs);

        player->current_speed = player->target_speed;

        return average_speed;
}

static enum fv_logic_state_change
update_player_position(struct fv_logic *logic,
                       struct fv_logic_player *player,
                       float progress_secs)
{
        bool position_changed, direction_changed;
        enum fv_logic_state_change state_change = 0;
        float average_speed;

        if (player->target_speed == 0.0f &&
            player->current_speed == 0.0f)
                return 0;

        state_change |= FV_LOGIC_STATE_CHANGE_ALIVE;

        average_speed = update_player_speed(player, progress_secs);

        position_changed =
                update_player_xy(logic,
                                 player,
                                 average_speed,
                                 progress_secs);
        direction_changed =
                update_player_direction(logic, player, progress_secs);

        if (position_changed || direction_changed)
                state_change |= FV_LOGIC_STATE_CHANGE_POSITION;

        return state_change;
}

static enum fv_logic_state_change
update_center(struct fv_logic_player *player)
{
        float dx = player->person.x - player->center_x;
        float dy = player->person.y - player->center_y;
        float d2, d;

        d2 = dx * dx + dy * dy;

        if (d2 > FV_LOGIC_CAMERA_DISTANCE * FV_LOGIC_CAMERA_DISTANCE) {
                d = sqrtf(d2);
                player->center_x += dx * (1 - FV_LOGIC_CAMERA_DISTANCE / d);
                player->center_y += dy * (1 - FV_LOGIC_CAMERA_DISTANCE / d);

                return FV_LOGIC_STATE_CHANGE_CENTER;
        } else {
                return 0;
        }
}

static enum fv_logic_state_change
update_player_movement(struct fv_logic *logic,
                       float progress_secs)
{
        struct fv_logic_player *player = &logic->player;

        if (player->target_speed == 0.0f &&
            player->current_speed == 0.0f)
                return 0;

        return (update_player_position(logic, player, progress_secs) |
                update_center(player));
}

enum fv_logic_state_change
fv_logic_update(struct fv_logic *logic,
                unsigned int progress)
{
        float progress_secs;
        enum fv_logic_state_change state_change = 0;

        /* If we've skipped over half a second then we'll assume something
         * has gone wrong and we won't do anything */
        if (progress >= 500)
                return FV_LOGIC_STATE_CHANGE_ALIVE;

        if (logic->state != FV_LOGIC_STATE_RUNNING)
                return 0;

        progress_secs = progress / 1000.0f;

        state_change |= update_player_movement(logic,
                                               progress_secs);

        if (state_change)
                state_change |= FV_LOGIC_STATE_CHANGE_ALIVE;

        return state_change;
}

void
fv_logic_set_direction(struct fv_logic *logic,
                       float speed,
                       float direction)
{
        struct fv_logic_player *player = &logic->player;

        if (speed > 0.0f) {
                player->target_speed = speed;
                player->target_direction = direction;
        } else {
                player->target_speed = 0.0f;
        }
}

void
fv_logic_set_n_npcs(struct fv_logic *logic,
                    int n_npcs)
{
        int old_length = logic->npcs.length;

        fv_buffer_set_length(&logic->npcs,
                             sizeof (struct fv_logic_npc) * n_npcs);

        if (old_length < logic->npcs.length) {
                memset(logic->npcs.data + old_length,
                       0,
                       logic->npcs.length - old_length);
        }
}

void
fv_logic_update_npc(struct fv_logic *logic,
                    int npc_num,
                    const struct fv_person *person,
                    enum fv_person_state state)
{
        struct fv_logic_npc *npc;

        assert(npc_num < logic->npcs.length / sizeof (struct fv_logic_npc));

        npc = (struct fv_logic_npc *) logic->npcs.data + npc_num;

        if ((state & FV_PERSON_STATE_POSITION)) {
                npc->person.x = (person->pos.x /
                                 (float) UINT32_MAX *
                                 FV_MAP_WIDTH);
                npc->person.y = (person->pos.y /
                                 (float) UINT32_MAX *
                                 FV_MAP_HEIGHT);
                npc->person.direction = (person->pos.direction /
                                         (float) UINT16_MAX *
                                         2 * M_PI);
                if (npc->person.direction > M_PI)
                        npc->person.direction -= 2 * M_PI;
        }

        if ((state & FV_PERSON_STATE_APPEARANCE)) {
                npc->person.type = MIN(person->appearance.image,
                                       FV_PERSON_N_TYPES - 1);
        }

        if ((state & FV_PERSON_STATE_FLAGS)) {
                npc->person.n_flags = person->flags.n_flags;
                memcpy(npc->person.flags,
                       person->flags.flags,
                       sizeof npc->person.flags[0] * person->flags.n_flags);
        }
}

void
fv_logic_free(struct fv_logic *logic)
{
        fv_buffer_destroy(&logic->npcs);
        fv_free(logic);
}

void
fv_logic_get_player(struct fv_logic *logic,
                    struct fv_person *person,
                    enum fv_person_state state)
{
        const struct fv_logic_player *player = &logic->player;
        float direction;

        if ((state & FV_PERSON_STATE_POSITION)) {
                person->pos.x = (player->person.x /
                                 (float) FV_MAP_WIDTH *
                                 UINT32_MAX);
                person->pos.y = (player->person.y /
                                 (float) FV_MAP_HEIGHT *
                                 UINT32_MAX);
                direction = player->person.direction;
                if (direction < 0)
                        direction += 2 * M_PI;
                person->pos.direction = direction / (2 * M_PI) * UINT16_MAX;
        }

        if ((state & FV_PERSON_STATE_APPEARANCE))
                person->appearance.image = player->person.type;

        if ((state & FV_PERSON_STATE_FLAGS)) {
                person->flags.n_flags = player->person.n_flags;
                memcpy(person->flags.flags,
                       player->person.flags,
                       sizeof person->flags.flags[0] * person->flags.n_flags);
        }
}

void
fv_logic_get_player_position(struct fv_logic *logic,
                             float *x, float *y)
{
        *x = logic->player.person.x;
        *y = logic->player.person.y;
}

void
fv_logic_get_center(struct fv_logic *logic,
                    float *x, float *y)
{
        *x = logic->player.center_x;
        *y = logic->player.center_y;
}

void
fv_logic_for_each_person(struct fv_logic *logic,
                         fv_logic_person_cb person_cb,
                         void *user_data)
{
        const struct fv_logic_npc *npc;
        int i;

        person_cb(&logic->player.person, user_data);

        for (i = 0;
             i < logic->npcs.length / sizeof (struct fv_logic_npc);
             i++) {
                npc = (struct fv_logic_npc *) logic->npcs.data + i;

                person_cb(&npc->person, user_data);
        }
}

enum fv_logic_state
fv_logic_get_state(struct fv_logic *logic)
{
        return logic->state;
}
