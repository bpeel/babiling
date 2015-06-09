/*
 * Finvenkisto
 * Copyright (C) 2015  Neil Roberts
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that copyright
 * notice and this permission notice appear in supporting documentation, and
 * that the name of the copyright holders not be used in advertising or
 * publicity pertaining to distribution of the software without specific,
 * written prior permission.  The copyright holders make no representations
 * about the suitability of this software for any purpose.  It is provided "as
 * is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE
 * OF THIS SOFTWARE.
 */

#ifndef FV_PROTO_H
#define FV_PROTO_H

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "fv-util.h"

#define FV_PROTO_DEFAULT_PORT 3468

/* Size of the header that is common to all messages */
#define FV_PROTO_HEADER_SIZE (sizeof (uint16_t) * 2)

#define FV_PROTO_NEW_PLAYER 0x1000
#define FV_PROTO_RECONNECT 0x1001
#define FV_PROTO_UPDATE_POSITION 0x1002

#define FV_PROTO_PLAYER_ID 0x0000
#define FV_PROTO_CONSISTENT 0x0001
#define FV_PROTO_N_PLAYERS 0x0002
#define FV_PROTO_PLAYER_POSITION 0x0003

enum fv_proto_type {
        FV_PROTO_TYPE_UINT16,
        FV_PROTO_TYPE_UINT32,
        FV_PROTO_TYPE_UINT64,
        FV_PROTO_TYPE_NONE
};

static inline void
fv_proto_write_uint16(uint8_t *buffer,
                      uint16_t value)
{
        value = FV_UINT16_TO_LE(value);
        memcpy(buffer, &value, sizeof value);
}

static inline void
fv_proto_write_uint32(uint8_t *buffer,
                      uint32_t value)
{
        value = FV_UINT32_TO_LE(value);
        memcpy(buffer, &value, sizeof value);
}

static inline void
fv_proto_write_uint64(uint8_t *buffer,
                      uint64_t value)
{
        value = FV_UINT64_TO_LE(value);
        memcpy(buffer, &value, sizeof value);
}

ssize_t
fv_proto_write_command(uint8_t *buffer,
                       size_t buffer_length,
                       uint16_t command,
                       ...);

static inline uint16_t
fv_proto_read_uint16(const uint8_t *buffer)
{
        uint16_t value;
        memcpy(&value, buffer, sizeof value);
        return FV_UINT16_FROM_LE(value);
}

static inline uint32_t
fv_proto_read_uint32(const uint8_t *buffer)
{
        uint32_t value;
        memcpy(&value, buffer, sizeof value);
        return FV_UINT32_FROM_LE(value);
}

static inline uint64_t
fv_proto_read_uint64(const uint8_t *buffer)
{
        uint64_t value;
        memcpy(&value, buffer, sizeof value);
        return FV_UINT64_FROM_LE(value);
}

ssize_t
fv_proto_read_command(const uint8_t *buffer,
                      ...);

static inline uint16_t
fv_proto_get_message_id(const uint8_t *buffer)
{
        return fv_proto_read_uint16(buffer);
}

static inline uint16_t
fv_proto_get_payload_length(const uint8_t *buffer)
{
        return fv_proto_read_uint16(buffer + sizeof (uint16_t));
}

#endif /* FV_PROTO_H */
