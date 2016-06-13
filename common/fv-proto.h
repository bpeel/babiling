/*
 * Babiling
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
#include <stdarg.h>

#include "fv-util.h"

#define FV_PROTO_DEFAULT_PORT 3468

/* Size of the header that is common to all messages */
#define FV_PROTO_HEADER_SIZE 1

/* Maximum number of bytes allowed in an Opus packet. Considering that
 * each packet is 10ms, this allows 11.9kb/sec. 122 is chosen so that
 * the maximum frame payload size won't overflow 125 bytes. That way
 * the length can always be stored in a byte.
 */
#define FV_PROTO_MAX_SPEECH_SIZE 122

/* The length of time that all Opus packets should be, in ms */
#define FV_PROTO_SPEECH_TIME 10

/* Maximum size of a message including the header and payload. */
#define FV_PROTO_MAX_MESSAGE_SIZE (FV_PROTO_HEADER_SIZE +       \
                                   sizeof (uint16_t) +          \
                                   FV_PROTO_MAX_SPEECH_SIZE)

/* The WebSocket protocol says that a control frame payload can not be
 * longer than 125 bytes.
 */
#define FV_PROTO_MAX_CONTROL_FRAME_PAYLOAD 125

#define FV_PROTO_MAX_FLAGS 16

#define FV_PROTO_NEW_PLAYER 0x80
#define FV_PROTO_RECONNECT 0x81
#define FV_PROTO_UPDATE_POSITION 0x82
#define FV_PROTO_KEEP_ALIVE 0x83
#define FV_PROTO_SPEECH 0x84
#define FV_PROTO_UPDATE_APPEARANCE 0x85
#define FV_PROTO_UPDATE_FLAGS 0x86

#define FV_PROTO_PLAYER_ID 0x00
#define FV_PROTO_CONSISTENT 0x01
#define FV_PROTO_N_PLAYERS 0x02
#define FV_PROTO_PLAYER_POSITION 0x03
#define FV_PROTO_PLAYER_SPEECH 0x04
#define FV_PROTO_PLAYER_APPEARANCE 0x05
#define FV_PROTO_PLAYER_FLAGS 0x06

#define FV_PROTO_MAX_FRAME_HEADER_LENGTH (1 + 1 + 8 + 4)

enum fv_proto_type {
        FV_PROTO_TYPE_UINT8,
        FV_PROTO_TYPE_UINT16,
        FV_PROTO_TYPE_UINT32,
        FV_PROTO_TYPE_UINT64,
        FV_PROTO_TYPE_BLOB,
        FV_PROTO_TYPE_NONE
};

static inline void
fv_proto_write_uint8_t(uint8_t *buffer,
                       uint8_t value)
{
        *buffer = value;
}

static inline void
fv_proto_write_uint16_t(uint8_t *buffer,
                        uint16_t value)
{
        value = FV_UINT16_TO_LE(value);
        memcpy(buffer, &value, sizeof value);
}

static inline void
fv_proto_write_uint32_t(uint8_t *buffer,
                        uint32_t value)
{
        value = FV_UINT32_TO_LE(value);
        memcpy(buffer, &value, sizeof value);
}

static inline void
fv_proto_write_uint64_t(uint8_t *buffer,
                        uint64_t value)
{
        value = FV_UINT64_TO_LE(value);
        memcpy(buffer, &value, sizeof value);
}

int
fv_proto_write_command_v(uint8_t *buffer,
                         size_t buffer_length,
                         uint8_t command,
                         va_list ap);

int
fv_proto_write_command(uint8_t *buffer,
                       size_t buffer_length,
                       uint8_t command,
                       ...);

static inline uint8_t
fv_proto_read_uint8_t(const uint8_t *buffer)
{
        return *buffer;
}

static inline uint16_t
fv_proto_read_uint16_t(const uint8_t *buffer)
{
        uint16_t value;
        memcpy(&value, buffer, sizeof value);
        return FV_UINT16_FROM_LE(value);
}

static inline uint32_t
fv_proto_read_uint32_t(const uint8_t *buffer)
{
        uint32_t value;
        memcpy(&value, buffer, sizeof value);
        return FV_UINT32_FROM_LE(value);
}

static inline uint64_t
fv_proto_read_uint64_t(const uint8_t *buffer)
{
        uint64_t value;
        memcpy(&value, buffer, sizeof value);
        return FV_UINT64_FROM_LE(value);
}

bool
fv_proto_read_payload(const uint8_t *buffer,
                      size_t length,
                      ...);

#endif /* FV_PROTO_H */
