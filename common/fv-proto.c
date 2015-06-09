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

#include "fv-proto.h"

#define WRITE_TYPE_AP(enum_name, type_name, ap_type_name)               \
        case enum_name:                                                 \
        if ((size_t) pos + sizeof (type_name ## _t) > buffer_length) {  \
                pos = -1;                                               \
                goto done;                                              \
        }                                                               \
                                                                        \
        fv_proto_write_ ## type_name(buffer + pos,                      \
                                     va_arg(ap, ap_type_name));         \
        pos += sizeof (type_name ## _t);                                \
                                                                        \
        break

#define WRITE_TYPE(enum_name, type_name)                                \
        WRITE_TYPE_AP(enum_name, type_name, type_name ## _t)

ssize_t
fv_proto_write_command(uint8_t *buffer,
                       size_t buffer_length,
                       uint16_t command,
                       ...)
{
        ssize_t pos = FV_PROTO_HEADER_SIZE;
        va_list ap;

        if (buffer_length < (size_t) pos)
                return -1;

        fv_proto_write_uint16(buffer, command);

        va_start(ap, command);

        while (true) {
                switch (va_arg(ap, enum fv_proto_type)) {
                        WRITE_TYPE_AP(FV_PROTO_TYPE_UINT16, uint16,
                                      unsigned int);
                        WRITE_TYPE(FV_PROTO_TYPE_UINT32, uint32);
                        WRITE_TYPE(FV_PROTO_TYPE_UINT64, uint64);

                case FV_PROTO_TYPE_NONE:
                        fv_proto_write_uint16(buffer + sizeof (uint16_t),
                                              pos - FV_PROTO_HEADER_SIZE);
                        goto done;
                }
        }

done:
        va_end(ap);

        return pos;
}

#undef WRITE_TYPE

#define READ_TYPE(enum_name, type_name)                                 \
        case enum_name:                                                 \
        if ((size_t) pos + sizeof (type_name ## _t) > buffer_length) {  \
                pos = -1;                                               \
                goto done;                                              \
        }                                                               \
                                                                        \
        {                                                               \
                type_name ## _t *val = va_arg(ap, type_name ## _t *);   \
                *val = fv_proto_read_ ## type_name(buffer + pos);       \
        }                                                               \
                                                                        \
        pos += sizeof (type_name ## _t);                                \
                                                                        \
        break

ssize_t
fv_proto_read_command(const uint8_t *buffer,
                      ...)
{
        uint16_t payload_length = fv_proto_get_payload_length(buffer);
        size_t buffer_length = payload_length + FV_PROTO_HEADER_SIZE;
        ssize_t pos = FV_PROTO_HEADER_SIZE;
        va_list ap;

        va_start(ap, buffer);

        while (true) {
                switch (va_arg(ap, enum fv_proto_type)) {
                        READ_TYPE(FV_PROTO_TYPE_UINT16, uint16);
                        READ_TYPE(FV_PROTO_TYPE_UINT32, uint32);
                        READ_TYPE(FV_PROTO_TYPE_UINT64, uint64);

                case FV_PROTO_TYPE_NONE:
                        if (pos != buffer_length)
                                pos = -1;
                        goto done;
                }
        }

done:
        va_end(ap);

        return pos;
}

#undef READ_TYPE
