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

#include "config.h"

#include <stdarg.h>
#include <assert.h>

#include "fv-proto.h"

#define FV_PROTO_TYPE(enum_name, type_name, ap_type_name)       \
        case enum_name:                                         \
        payload_length += sizeof (type_name);                   \
        va_arg(ap, ap_type_name);                               \
        break;

static size_t
get_payload_length(va_list ap)
{
        /* The payload always at least includes the message ID */
        size_t payload_length = 1;

        while (true) {
                switch (va_arg(ap, enum fv_proto_type)) {
#include "fv-proto-types.h"

                case FV_PROTO_TYPE_NONE:
                        return payload_length;
                }
        }
}

#undef FV_PROTO_TYPE

#define FV_PROTO_TYPE(enum_name, type_name, ap_type_name)               \
        case enum_name:                                                 \
        fv_proto_write_ ## type_name(buffer + pos,                      \
                                     va_arg(ap, ap_type_name));         \
        pos += sizeof (type_name);                                      \
                                                                        \
        break;

int
fv_proto_write_command_v(uint8_t *buffer,
                         size_t buffer_length,
                         uint8_t command,
                         va_list ap)
{
        int pos;
        size_t payload_length = 0;
        size_t frame_header_length;
        va_list ap_copy;

        va_copy(ap_copy, ap);
        payload_length = get_payload_length(ap_copy);
        va_end(ap_copy);

        frame_header_length = 2;
        if (payload_length > 0xffff)
                frame_header_length += sizeof (uint64_t);
        else if (payload_length >= 126)
                frame_header_length += sizeof (uint16_t);

        if (frame_header_length + payload_length > buffer_length)
                return -1;

        /* opcode (2) (binary) with FIN bit set */
        buffer[0] = 0x82;
        if (payload_length > 0xffff) {
                buffer[1] = 127;
                fv_proto_write_uint64_t(buffer + 2, payload_length);
        } else if (payload_length >= 126) {
                buffer[1] = 126;
                fv_proto_write_uint16_t(buffer + 2, payload_length);
        } else {
                buffer[1] = payload_length;
        }

        buffer[frame_header_length] = command;

        pos = frame_header_length + 1;

        /* Calculate the length of the payload */
        while (true) {
                switch (va_arg(ap, enum fv_proto_type)) {
#include "fv-proto-types.h"

                case FV_PROTO_TYPE_NONE:
                        goto done;
                }
        }
done:

        va_end(ap);

        assert(pos == frame_header_length + payload_length);

        return pos;
}

#undef FV_PROTO_TYPE

int
fv_proto_write_command(uint8_t *buffer,
                       size_t buffer_length,
                       uint8_t command,
                       ...)
{
        int ret;
        va_list ap;

        va_start(ap, command);

        ret = fv_proto_write_command_v(buffer, buffer_length, command, ap);

        va_end(ap);

        return ret;
}

#define FV_PROTO_TYPE(enum_name, type_name, ap_type_name)               \
        case enum_name:                                                 \
        if ((size_t) pos + sizeof (type_name) > length) {               \
                ret = false;                                            \
                goto done;                                              \
        }                                                               \
                                                                        \
        {                                                               \
                type_name *val = va_arg(ap, type_name *);               \
                *val = fv_proto_read_ ## type_name(buffer + pos);       \
        }                                                               \
                                                                        \
        pos += sizeof (type_name);                                      \
                                                                        \
        break;

bool
fv_proto_read_payload(const uint8_t *buffer,
                      size_t length,
                      ...)
{
        size_t pos = 0;
        bool ret = true;
        va_list ap;

        va_start(ap, length);

        while (true) {
                switch (va_arg(ap, enum fv_proto_type)) {
#include "fv-proto-types.h"

                case FV_PROTO_TYPE_NONE:
                        if (pos != length)
                                ret = false;
                        goto done;
                }
        }

done:
        va_end(ap);

        return ret;
}

#undef FV_PROTO_TYPE
