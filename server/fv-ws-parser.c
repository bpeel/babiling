/*
 * Finvenkisto
 * Copyright (C) 2011, 2015  Neil Roberts
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

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <limits.h>
#include <assert.h>

#include "fv-ws-parser.h"

struct fv_error_domain
fv_ws_parser_error;

void
fv_ws_parser_init(struct fv_ws_parser *parser,
                  const struct fv_ws_parser_vtable *vtable,
                  void *user_data)
{
        parser->buf_len = 0;
        parser->state = FV_WS_PARSER_READING_REQUEST_LINE;
        parser->vtable = vtable;
        parser->user_data = user_data;
}

static bool
check_http_version(const uint8_t *data,
                   unsigned int length,
                   struct fv_error **error)
{
        static const char prefix[] = "HTTP/1.";

        /* This accepts any 1.x version */

        if (length < sizeof(prefix) ||
            memcmp(data, prefix, sizeof(prefix) - 1))
                goto bad;

        data += sizeof(prefix) - 1;
        length -= sizeof(prefix) - 1;

        /* The remaining characters should all be digits */
        while (length > 0) {
                if (!fv_ascii_isdigit(data[--length]))
                        goto bad;
        }

        return true;

bad:
        fv_set_error(error,
                     &fv_ws_parser_error,
                     FV_WS_PARSER_ERROR_UNSUPPORTED,
                     "Unsupported HTTP version");
        return false;
}

static void
set_cancelled_error(struct fv_error **error)
{
        fv_set_error(error,
                     &fv_ws_parser_error,
                     FV_WS_PARSER_ERROR_CANCELLED,
                     "Application cancelled parsing");
}

static bool
add_bytes_to_buffer(struct fv_ws_parser *parser,
                    const uint8_t *data,
                    unsigned int length,
                    struct fv_error **error)
{
        if (parser->buf_len + length > FV_WS_PARSER_MAX_LINE_LENGTH) {
                fv_set_error(error,
                             &fv_ws_parser_error,
                             FV_WS_PARSER_ERROR_UNSUPPORTED,
                             "Unsupported line length in HTTP request");
                return false;
        } else {
                memcpy(parser->buf + parser->buf_len, data, length);
                parser->buf_len += length;

                return true;
        }
}

static bool
process_request_line(struct fv_ws_parser *parser,
                     uint8_t *data,
                     unsigned int length,
                     struct fv_error **error)
{
        uint8_t *method_end;
        uint8_t *uri_end;
        const char *method = (char *) data;
        const char *uri;

        method_end = memchr(data, ' ', length);

        if (method_end == NULL) {
                fv_set_error(error,
                             &fv_ws_parser_error,
                             FV_WS_PARSER_ERROR_INVALID,
                             "Invalid HTTP request received");
                return false;
        }

        /* Replace the space with a zero terminator so we can reuse
         * the buffer to pass to the callback
         */
        *method_end = '\0';

        length -= method_end - data + 1;
        data = method_end + 1;

        uri = (const char *) data;
        uri_end = memchr(data, ' ', length);

        if (uri_end == NULL) {
                fv_set_error(error,
                             &fv_ws_parser_error,
                             FV_WS_PARSER_ERROR_INVALID,
                             "Invalid HTTP request received");
                return false;
        }

        *uri_end = '\0';

        length -= uri_end - data + 1;
        data = uri_end + 1;

        if (!check_http_version(data, length, error))
                return false;

        if (!parser->vtable->request_line_received(method,
                                                   uri,
                                                   parser->user_data)) {
                set_cancelled_error(error);
                return false;
        }

        return true;
}

static bool
process_header(struct fv_ws_parser *parser,
               struct fv_error **error)
{
        uint8_t *data = parser->buf;
        unsigned int length = parser->buf_len;
        const char *field_name = (char *)data;
        const char *value;
        uint8_t zero = '\0';
        uint8_t *field_name_end;

        field_name_end = memchr(data, ':', length);

        if (field_name_end == NULL) {
                fv_set_error(error,
                             &fv_ws_parser_error,
                             FV_WS_PARSER_ERROR_INVALID,
                             "Invalid HTTP request received");
                return false;
        }

        /* Replace the colon with a zero terminator so we can reuse
         * the buffer to pass to the callback
         */
        *field_name_end = '\0';

        length -= field_name_end - data + 1;
        data = field_name_end + 1;

        /* Skip leading spaces */
        while (length > 0 && *data == ' ') {
                length--;
                data++;
        }

        value = (char *) data;

        /* Add a terminator so we can pass it to the callback */
        if (!add_bytes_to_buffer(parser, &zero, 1, error))
                return false;

        if (!parser->vtable->header_received(field_name,
                                             value,
                                             parser->user_data)) {
                set_cancelled_error(error);
                return false;
        }

        return true;
}

static bool
process_headers_finished(struct fv_ws_parser *parser,
                         struct fv_error **error)
{
        if (!parser->vtable->headers_finished(parser->user_data)) {
                set_cancelled_error(error);
                return false;
        }

        return true;
}

static bool
process_data(struct fv_ws_parser *parser,
             const uint8_t *data,
             unsigned int length,
             struct fv_error **error)
{
        /* Ignore non-binary opcodes */
        if (parser->opcode != 0x2)
                return true;

        if (!parser->vtable->data_received(data, length, parser->user_data)) {
                set_cancelled_error(error);
                return false;
        }

        return true;
}

struct fv_ws_parser_closure {
        const uint8_t *data;
        unsigned int length;
};

static bool
handle_reading_request_line(struct fv_ws_parser *parser,
                            struct fv_ws_parser_closure *c,
                            struct fv_error **error)
{
        const uint8_t *terminator;

        /* Could the data contain a terminator? */
        if ((terminator = memchr(c->data, '\r', c->length))) {
                /* Add the data up to the potential terminator */
                if (!add_bytes_to_buffer(parser,
                                         c->data,
                                         terminator - c->data,
                                         error))
                        return false;

                /* Consume those bytes */
                c->length -= terminator - c->data + 1;
                c->data = terminator + 1;

                parser->state = FV_WS_PARSER_TERMINATING_REQUEST_LINE;
        } else {
                /* Add and consume all of the data */
                if (!add_bytes_to_buffer(parser,
                                         c->data,
                                         c->length,
                                         error))
                        return false;

                c->length = 0;
        }

        return true;
}

static bool
handle_terminating_request_line(struct fv_ws_parser *parser,
                                struct fv_ws_parser_closure *c,
                                struct fv_error **error)
{
        /* Do we have the \n needed to complete the terminator? */
        if (c->data[0] == '\n') {
                /* Apparently some clients send a '\r\n' after sending
                 * the request body. We can handle this by just
                 * ignoring empty lines before the request line
                 */
                if (parser->buf_len == 0)
                        parser->state =
                                FV_WS_PARSER_READING_REQUEST_LINE;
                else {
                        if (!process_request_line(parser,
                                                  parser->buf,
                                                  parser->
                                                  buf_len,
                                                  error))
                                return false;

                        parser->buf_len = 0;
                        /* Start processing headers */
                        parser->state = FV_WS_PARSER_READING_HEADER;
                }

                /* Consume the \n */
                c->data++;
                c->length--;
        } else {
                uint8_t r = '\r';
                /* Add the \r that we ignored when switching to this
                 * state and then switch back to reading the request
                 * line without consuming the char
                 */
                if (!add_bytes_to_buffer(parser, &r, 1, error))
                        return false;
                parser->state =
                        FV_WS_PARSER_READING_REQUEST_LINE;
        }

        return true;
}

static bool
handle_reading_header(struct fv_ws_parser *parser,
                      struct fv_ws_parser_closure *c,
                      struct fv_error **error)
{
        const uint8_t *terminator;

        /* Could the data contain a terminator? */
        if ((terminator = memchr(c->data, '\r', c->length))) {
                /* Add the data up to the potential terminator */
                if (!add_bytes_to_buffer(parser,
                                         c->data,
                                         terminator - c->data,
                                         error))
                        return false;

                /* Consume those bytes */
                c->length -= terminator - c->data + 1;
                c->data = terminator + 1;

                parser->state = FV_WS_PARSER_TERMINATING_HEADER;
        } else {
                /* Add and consume all of the data */
                if (!add_bytes_to_buffer(parser,
                                         c->data,
                                         c->length,
                                         error))
                        return false;

                c->length = 0;
        }

        return true;
}

static bool
handle_terminating_header(struct fv_ws_parser *parser,
                          struct fv_ws_parser_closure *c,
                          struct fv_error **error)
{
        /* Do we have the \n needed to complete the terminator? */
        if (c->data[0] == '\n') {
                /* If the header is empty then this marks the end of the
                 * headers
                 */
                if (parser->buf_len == 0) {
                        if (!process_headers_finished(parser, error))
                                return false;
                        parser->state = FV_WS_PARSER_READING_FRAME_HEADER;
                } else {
                        /* Start checking for a continuation */
                        parser->state =
                                FV_WS_PARSER_CHECKING_HEADER_CONTINUATION;
                }

                /* Consume the \n */
                c->data++;
                c->length--;
        } else {
                uint8_t r = '\r';
                /* Add the \r that we ignored when switching to this
                 * state and then switch back to reading the header
                 * without consuming the char
                 */
                if (!add_bytes_to_buffer(parser, &r, 1, error))
                        return false;
                parser->state = FV_WS_PARSER_READING_HEADER;
        }

        return true;
}

static bool
handle_checking_header_continuation(struct fv_ws_parser *parser,
                                    struct fv_ws_parser_closure *c,
                                    struct fv_error **error)
{
        /* Do we have a continuation character? */
        if (c->data[0] == ' ') {
                /* Yes, continue processing headers */
                parser->state = FV_WS_PARSER_READING_HEADER;
                /* We don't consume the character so that the space
                 * will be added to the buffer
                 */
        } else {
                /* We have a complete header */
                if (!process_header(parser, error))
                        return false;

                parser->buf_len = 0;
                parser->state = FV_WS_PARSER_READING_HEADER;
        }

        return true;
}

static bool
handle_reading_frame_header(struct fv_ws_parser *parser,
                            struct fv_ws_parser_closure *c,
                            struct fv_error **error)
{
        uint8_t header = c->data[0];

        parser->opcode = header & 0xf;

        c->data++;
        c->length--;
        parser->state = FV_WS_PARSER_READING_PAYLOAD_LENGTH;

        return true;
}

static void
got_payload_length(struct fv_ws_parser *parser)
{
        if (parser->has_mask) {
                parser->state = FV_WS_PARSER_READING_MASKING_KEY;
                parser->got_bytes = 0;
        } else {
                parser->state = FV_WS_PARSER_READING_PAYLOAD;
        }
}

static bool
handle_reading_payload_length(struct fv_ws_parser *parser,
                              struct fv_ws_parser_closure *c,
                              struct fv_error **error)
{
        uint8_t length = c->data[0];

        parser->has_mask = (length & 0x80) != 0;
        parser->payload_length = length & 0x7f;

        switch (parser->payload_length) {
        case 126:
                parser->state = FV_WS_PARSER_READING_PAYLOAD_LENGTH16;
                parser->got_bytes = 0;
                break;

        case 127:
                parser->state = FV_WS_PARSER_READING_PAYLOAD_LENGTH64;
                parser->got_bytes = 0;
                break;

        default:
                got_payload_length(parser);
                break;
        }

        c->data++;
        c->length--;

        return true;
}

static bool
handle_reading_payload_length16(struct fv_ws_parser *parser,
                                struct fv_ws_parser_closure *c,
                                struct fv_error **error)
{
        size_t to_read = MIN(c->length, sizeof (uint16_t) - parser->got_bytes);

        memcpy((uint8_t *) &parser->payload_length + parser->got_bytes,
               c->data,
               to_read);

        parser->got_bytes += to_read;

        if (parser->got_bytes >= sizeof (uint16_t)) {
                parser->payload_length =
                        FV_UINT16_FROM_BE(*(uint16_t *)
                                          &parser->payload_length);
                got_payload_length(parser);
        }

        c->data += to_read;
        c->length -= to_read;

        return true;
}

static bool
handle_reading_payload_length64(struct fv_ws_parser *parser,
                                struct fv_ws_parser_closure *c,
                                struct fv_error **error)
{
        size_t to_read = MIN(c->length, sizeof (uint64_t) - parser->got_bytes);

        memcpy((uint8_t *) &parser->payload_length + parser->got_bytes,
               c->data,
               to_read);

        parser->got_bytes += to_read;

        if (parser->got_bytes >= sizeof (uint64_t)) {
                parser->payload_length =
                        FV_UINT64_FROM_BE(parser->payload_length);
                got_payload_length(parser);
        }

        c->data += to_read;
        c->length -= to_read;

        return true;
}

static bool
handle_reading_masking_key(struct fv_ws_parser *parser,
                           struct fv_ws_parser_closure *c,
                           struct fv_error **error)
{
        size_t to_read = MIN(c->length,
                             sizeof parser->masking_key - parser->got_bytes);

        memcpy((uint8_t *) &parser->masking_key + parser->got_bytes,
               c->data,
               to_read);

        parser->got_bytes += to_read;

        if (parser->got_bytes >= sizeof parser->masking_key) {
                /* There's no need to byte-swap the masking key
                 * because we're only going to use it to XOR against
                 * integers that are read from the buffer and it will
                 * work correctly regardless of the endianness of the
                 * machine.
                 */
                parser->state = FV_WS_PARSER_READING_MASKED_PAYLOAD;
        }

        c->data += to_read;
        c->length -= to_read;

        return true;
}

static bool
handle_reading_payload(struct fv_ws_parser *parser,
                       struct fv_ws_parser_closure *c,
                       struct fv_error **error)
{
        size_t to_read = MIN(c->length, parser->payload_length);

        c->data += to_read;
        c->length -= to_read;
        parser->payload_length -= to_read;

        if (parser->payload_length <= 0)
                parser->state = FV_WS_PARSER_READING_FRAME_HEADER;

        return process_data(parser, c->data - to_read, to_read, error);
}

static bool
handle_reading_masked_payload(struct fv_ws_parser *parser,
                              struct fv_ws_parser_closure *c,
                              struct fv_error **error)
{
        size_t to_read = MIN(c->length, MIN(parser->payload_length,
                                            sizeof parser->buf));
        uint32_t val;
        int i;

        for (i = 0;
             i + sizeof parser->masking_key <= to_read;
             i += sizeof parser->masking_key) {
                memcpy(&val, c->data + i, sizeof val);
                val ^= parser->masking_key;
                memcpy(parser->buf + i, &val, sizeof val);
        }

        for (; i < to_read; i++)
                parser->buf[i] = c->data[i] ^ *(uint8_t *) &parser->masking_key;

        c->data += to_read;
        c->length -= to_read;
        parser->payload_length -= to_read;

        if (parser->payload_length <= 0)
                parser->state = FV_WS_PARSER_READING_FRAME_HEADER;

        return process_data(parser, parser->buf, to_read, error);
}

bool
fv_ws_parser_parse_data(struct fv_ws_parser *parser,
                        const uint8_t *data,
                        unsigned int length,
                        struct fv_error **error)
{
        struct fv_ws_parser_closure closure;

        closure.data = data;
        closure.length = length;

        while (closure.length > 0) {
                switch (parser->state) {
                case FV_WS_PARSER_READING_REQUEST_LINE:
                        if (!handle_reading_request_line(parser,
                                                         &closure,
                                                         error))
                                return false;
                        break;

                case FV_WS_PARSER_TERMINATING_REQUEST_LINE:
                        if (!handle_terminating_request_line(parser,
                                                             &closure,
                                                             error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_HEADER:
                        if (!handle_reading_header(parser,
                                                   &closure,
                                                   error))
                                return false;
                        break;

                case FV_WS_PARSER_TERMINATING_HEADER:
                        if (!handle_terminating_header(parser,
                                                       &closure,
                                                       error))
                                return false;
                        break;

                case FV_WS_PARSER_CHECKING_HEADER_CONTINUATION:
                        if (!handle_checking_header_continuation(parser,
                                                                 &closure,
                                                                 error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_FRAME_HEADER:
                        if (!handle_reading_frame_header(parser,
                                                         &closure,
                                                         error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_PAYLOAD_LENGTH:
                        if (!handle_reading_payload_length(parser,
                                                           &closure,
                                                           error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_PAYLOAD_LENGTH16:
                        if (!handle_reading_payload_length16(parser,
                                                             &closure,
                                                             error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_PAYLOAD_LENGTH64:
                        if (!handle_reading_payload_length64(parser,
                                                             &closure,
                                                             error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_MASKING_KEY:
                        if (!handle_reading_masking_key(parser,
                                                        &closure,
                                                        error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_PAYLOAD:
                        if (!handle_reading_payload(parser,
                                                    &closure,
                                                    error))
                                return false;
                        break;

                case FV_WS_PARSER_READING_MASKED_PAYLOAD:
                        if (!handle_reading_masked_payload(parser,
                                                           &closure,
                                                           error))
                                return false;
                        break;
                }
        }

        return true;
}
