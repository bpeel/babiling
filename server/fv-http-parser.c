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

#include "fv-http-parser.h"

struct fv_error_domain
fv_http_parser_error;

void
fv_http_parser_init(struct fv_http_parser *parser,
                    const struct fv_http_parser_vtable *vtable,
                    void *user_data)
{
        parser->buf_len = 0;
        parser->state = FV_HTTP_PARSER_READING_REQUEST_LINE;
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
                     &fv_http_parser_error,
                     FV_HTTP_PARSER_ERROR_UNSUPPORTED,
                     "Unsupported HTTP version");
        return false;
}

static void
set_cancelled_error(struct fv_error **error)
{
        fv_set_error(error,
                     &fv_http_parser_error,
                     FV_HTTP_PARSER_ERROR_CANCELLED,
                     "Application cancelled parsing");
}

static bool
add_bytes_to_buffer(struct fv_http_parser *parser,
                    const uint8_t *data,
                    unsigned int length,
                    struct fv_error **error)
{
        if (parser->buf_len + length > FV_HTTP_PARSER_MAX_LINE_LENGTH) {
                fv_set_error(error,
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_UNSUPPORTED,
                             "Unsupported line length in HTTP request");
                return false;
        } else {
                memcpy(parser->buf + parser->buf_len, data, length);
                parser->buf_len += length;

                return true;
        }
}

static bool
process_request_line(struct fv_http_parser *parser,
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
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
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
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
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

        /* Assume there is no data unless we get a header specifying
         * otherwise
         */
        parser->transfer_encoding = FV_HTTP_PARSER_TRANSFER_NONE;

        return true;
}

static bool
process_header(struct fv_http_parser *parser,
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
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
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

        if (fv_ascii_string_case_equal(field_name, "content-length")) {
                char *tail;
                unsigned long int content_length;
                errno = 0;
                content_length = strtoul(value, &tail, 10);
                if (content_length > UINT_MAX || *tail || errno) {
                        fv_set_error(error,
                                     &fv_http_parser_error,
                                     FV_HTTP_PARSER_ERROR_INVALID,
                                     "Invalid HTTP request received");
                        return false;
                }

                parser->content_length = content_length;
                parser->transfer_encoding =
                        FV_HTTP_PARSER_TRANSFER_CONTENT_LENGTH;
        } else if (fv_ascii_string_case_equal(field_name,
                                              "transfer-encoding")) {
                if (!fv_ascii_string_case_equal(value, "chunked")) {
                        fv_set_error(error,
                                     &fv_http_parser_error,
                                     FV_HTTP_PARSER_ERROR_UNSUPPORTED,
                                     "Unsupported transfer-encoding \"%s\" "
                                     "from client",
                                     value);
                        return false;
                }

                parser->transfer_encoding = FV_HTTP_PARSER_TRANSFER_CHUNKED;
        }

        if (!parser->vtable->header_received(field_name,
                                             value,
                                             parser->user_data)) {
                set_cancelled_error(error);
                return false;
        }

        return true;
}

static bool
process_data(struct fv_http_parser *parser,
             const uint8_t *data,
             unsigned int length,
             struct fv_error **error)
{
        if (!parser->vtable->data_received(data, length, parser->user_data)) {
                set_cancelled_error(error);
                return false;
        }

        return true;
}

static bool
process_request_finished(struct fv_http_parser *parser,
                         struct fv_error **error)
{
        if (!parser->vtable->request_finished(parser->user_data)) {
                set_cancelled_error(error);
                return false;
        }

        return true;
}

struct fv_http_parser_closure {
        const uint8_t *data;
        unsigned int length;
};

static bool
handle_reading_request_line(struct fv_http_parser *parser,
                            struct fv_http_parser_closure *c,
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

                parser->state = FV_HTTP_PARSER_TERMINATING_REQUEST_LINE;
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
handle_terminating_request_line(struct fv_http_parser *parser,
                                struct fv_http_parser_closure *c,
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
                                FV_HTTP_PARSER_READING_REQUEST_LINE;
                else {
                        if (!process_request_line(parser,
                                                  parser->buf,
                                                  parser->
                                                  buf_len,
                                                  error))
                                return false;

                        parser->buf_len = 0;
                        /* Start processing headers */
                        parser->state = FV_HTTP_PARSER_READING_HEADER;
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
                        FV_HTTP_PARSER_READING_REQUEST_LINE;
        }

        return true;
}

static bool
handle_reading_header(struct fv_http_parser *parser,
                      struct fv_http_parser_closure *c,
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

                parser->state = FV_HTTP_PARSER_TERMINATING_HEADER;
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
handle_terminating_header(struct fv_http_parser *parser,
                          struct fv_http_parser_closure *c,
                          struct fv_error **error)
{
        /* Do we have the \n needed to complete the terminator? */
        if (c->data[0] == '\n') {
                /* If the header is empty then this marks the end of the
                 * headers
                 */
                if (parser->buf_len == 0) {
                        switch (parser->transfer_encoding) {
                        case FV_HTTP_PARSER_TRANSFER_NONE:
                                /* The request is finished */
                                if (!process_request_finished(parser, error))
                                        return false;
                                parser->buf_len = 0;
                                parser->state =
                                        FV_HTTP_PARSER_READING_REQUEST_LINE;
                                break;

                        case FV_HTTP_PARSER_TRANSFER_CONTENT_LENGTH:
                                parser->state =
                                        FV_HTTP_PARSER_READING_DATA_WITH_LENGTH;
                                break;

                        case FV_HTTP_PARSER_TRANSFER_CHUNKED:
                                parser->state =
                                        FV_HTTP_PARSER_READING_CHUNK_LENGTH;
                                parser->content_length = 0;
                                break;
                        }
                } else {
                        /* Start checking for a continuation */
                        parser->state =
                                FV_HTTP_PARSER_CHECKING_HEADER_CONTINUATION;
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
                parser->state = FV_HTTP_PARSER_READING_HEADER;
        }

        return true;
}

static bool
handle_checking_header_continuation(struct fv_http_parser *parser,
                                    struct fv_http_parser_closure *c,
                                    struct fv_error **error)
{
        /* Do we have a continuation character? */
        if (c->data[0] == ' ') {
                /* Yes, continue processing headers */
                parser->state = FV_HTTP_PARSER_READING_HEADER;
                /* We don't consume the character so that the space
                 * will be added to the buffer
                 */
        } else {
                /* We have a complete header */
                if (!process_header(parser, error))
                        return false;

                parser->buf_len = 0;
                parser->state = FV_HTTP_PARSER_READING_HEADER;
        }

        return true;
}

static bool
handle_reading_data_with_length(struct fv_http_parser *parser,
                                struct fv_http_parser_closure *c,
                                struct fv_error **error)
{
        unsigned int to_process_length = MIN(parser->content_length, c->length);

        if (!process_data(parser, c->data, to_process_length, error))
                return false;

        parser->content_length -= to_process_length;
        c->data += to_process_length;
        c->length -= to_process_length;

        if (parser->content_length == 0) {
                /* The request is finished */
                if (!process_request_finished(parser, error))
                        return false;

                parser->buf_len = 0;
                parser->state =
                        FV_HTTP_PARSER_READING_REQUEST_LINE;
        }

        return true;
}

static bool
handle_reading_chunk_length(struct fv_http_parser *parser,
                            struct fv_http_parser_closure *c,
                            struct fv_error **error)
{
        if (fv_ascii_isxdigit(*c->data)) {
                unsigned int new_length;

                new_length = (parser->content_length * 0x10 +
                              fv_ascii_xdigit_value(*c->data));

                if (new_length < parser->content_length) {
                        fv_set_error(error,
                                     &fv_http_parser_error,
                                     FV_HTTP_PARSER_ERROR_INVALID,
                                     "Invalid chunk length received");
                        return false;
                }

                parser->content_length = new_length;

                /* Consume the digit */
                c->data++;
                c->length--;
        } else if (*c->data == ';') {
                parser->state = FV_HTTP_PARSER_IGNORING_CHUNK_EXTENSION;
        } else if (*c->data == '\r') {
                c->data++;
                c->length--;
                parser->state = FV_HTTP_PARSER_TERMINATING_CHUNK_LENGTH;
        } else {
                fv_set_error(error,
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
                             "Invalid chunk length received");
                return false;
        }

        return true;
}

static bool
handle_terminating_chunk_length(struct fv_http_parser *parser,
                                struct fv_http_parser_closure *c,
                                struct fv_error **error)
{
        if (*c->data != '\n') {
                fv_set_error(error,
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
                             "Invalid chunk length received");
                return false;
        }

        c->data++;
        c->length--;

        if (parser->content_length)
                parser->state = FV_HTTP_PARSER_READING_CHUNK;
        else
                parser->state =
                        FV_HTTP_PARSER_IGNORING_CHUNK_TRAILER;

        return true;
}

static bool
handle_ignoring_chunk_extension(struct fv_http_parser *parser,
                                struct fv_http_parser_closure *c,
                                struct fv_error **error)
{
        const uint8_t *terminator;

        /* Could the data contain a terminator? */
        if ((terminator = memchr(c->data, '\r', c->length))) {
                parser->state = FV_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION;
                c->length -= terminator - c->data + 1;
                c->data = terminator + 1;
        } else {
                /* Consume all of the data */
                c->length = 0;
        }

        return true;
}

static bool
handle_terminating_chunk_extension(struct fv_http_parser *parser,
                                   struct fv_http_parser_closure *c,
                                   struct fv_error **error)
{
        if (*c->data == '\n') {
                c->data++;
                c->length--;

                if (parser->content_length)
                        parser->state = FV_HTTP_PARSER_READING_CHUNK;
                else
                        parser->state = FV_HTTP_PARSER_IGNORING_CHUNK_TRAILER;
        } else {
                parser->state = FV_HTTP_PARSER_IGNORING_CHUNK_EXTENSION;
        }

        return true;
}

static bool
handle_ignoring_chunk_trailer(struct fv_http_parser *parser,
                              struct fv_http_parser_closure *c,
                              struct fv_error **error)
{
        const uint8_t *terminator;

        /* Could the data contain a terminator? */
        if ((terminator = memchr(c->data, '\r', c->length))) {
                parser->state = FV_HTTP_PARSER_TERMINATING_CHUNK_TRAILER;
                parser->content_length += terminator - c->data;
                c->length -= terminator - c->data + 1;
                c->data = terminator + 1;
        } else {
                /* Consume all of the data */
                c->length = 0;
        }

        return true;
}

static bool
handle_terminating_chunk_trailer(struct fv_http_parser *parser,
                                 struct fv_http_parser_closure *c,
                                 struct fv_error **error)
{
        if (*c->data == '\n') {
                c->length--;
                c->data++;
                /* A blank line marks the end of the trailer and thus
                 * the request also
                 */
                if (parser->content_length == 0) {
                        if (!process_request_finished(parser, error))
                                return false;
                        parser->buf_len = 0;
                        parser->state = FV_HTTP_PARSER_READING_REQUEST_LINE;
                } else {
                        parser->content_length = 0;
                        parser->state = FV_HTTP_PARSER_IGNORING_CHUNK_TRAILER;
                }
        } else {
                /* Count one character for the '\r' */
                parser->content_length++;
                parser->state = FV_HTTP_PARSER_IGNORING_CHUNK_TRAILER;
        }

        return true;
}

static bool
handle_reading_chunk(struct fv_http_parser *parser,
                     struct fv_http_parser_closure *c,
                     struct fv_error **error)
{
        unsigned int to_process_length = MIN(parser->content_length,
                                             c->length);

        if (!process_data(parser, c->data, to_process_length, error))
                return false;

        parser->content_length -= to_process_length;
        c->data += to_process_length;
        c->length -= to_process_length;

        if (parser->content_length == 0)
                /* The chunk is finished */
                parser->state = FV_HTTP_PARSER_READING_CHUNK_TERMINATOR1;

        return true;
}

static bool
handle_reading_chunk_terminator1(struct fv_http_parser *parser,
                                 struct fv_http_parser_closure *c,
                                 struct fv_error **error)
{
        if (*c->data != '\r') {
                fv_set_error(error,
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
                             "Invalid chunk terminator received");
                return false;
        }

        c->data++;
        c->length--;

        parser->state = FV_HTTP_PARSER_READING_CHUNK_TERMINATOR2;

        return true;
}

static bool
handle_reading_chunk_terminator2(struct fv_http_parser *parser,
                                 struct fv_http_parser_closure *c,
                                 struct fv_error **error)
{
        if (*c->data != '\n') {
                fv_set_error(error,
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
                             "Invalid chunk terminator received");
                return false;
        }

        c->data++;
        c->length--;

        parser->state = FV_HTTP_PARSER_READING_CHUNK_LENGTH;

        return true;
}

bool
fv_http_parser_parse_data(struct fv_http_parser *parser,
                          const uint8_t *data,
                          unsigned int length,
                          struct fv_error **error)
{
        struct fv_http_parser_closure closure;

        closure.data = data;
        closure.length = length;

        while (length > 0) {
                switch (parser->state) {
                case FV_HTTP_PARSER_READING_REQUEST_LINE:
                        if (!handle_reading_request_line(parser,
                                                         &closure,
                                                         error))
                                return false;
                        break;

                case FV_HTTP_PARSER_TERMINATING_REQUEST_LINE:
                        if (!handle_terminating_request_line(parser,
                                                             &closure,
                                                             error))
                                return false;
                        break;

                case FV_HTTP_PARSER_READING_HEADER:
                        if (!handle_reading_header(parser,
                                                   &closure,
                                                   error))
                                return false;
                        break;

                case FV_HTTP_PARSER_TERMINATING_HEADER:
                        if (!handle_terminating_header(parser,
                                                       &closure,
                                                       error))
                                return false;
                        break;

                case FV_HTTP_PARSER_CHECKING_HEADER_CONTINUATION:
                        if (!handle_checking_header_continuation(parser,
                                                                 &closure,
                                                                 error))
                                return false;
                        break;

                case FV_HTTP_PARSER_READING_DATA_WITH_LENGTH:
                        if (!handle_reading_data_with_length(parser,
                                                             &closure,
                                                             error))
                                return false;
                        break;

                case FV_HTTP_PARSER_READING_CHUNK_LENGTH:
                        if (!handle_reading_chunk_length(parser,
                                                         &closure,
                                                         error))
                                return false;
                        break;

                case FV_HTTP_PARSER_TERMINATING_CHUNK_LENGTH:
                        if (!handle_terminating_chunk_length(parser,
                                                             &closure,
                                                             error))
                                return false;
                        break;

                case FV_HTTP_PARSER_IGNORING_CHUNK_EXTENSION:
                        if (!handle_ignoring_chunk_extension(parser,
                                                             &closure,
                                                             error))
                                return false;
                        break;

                case FV_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION:
                        if (!handle_terminating_chunk_extension(parser,
                                                                &closure,
                                                                error))
                                return false;
                        break;

                case FV_HTTP_PARSER_IGNORING_CHUNK_TRAILER:
                        if (!handle_ignoring_chunk_trailer(parser,
                                                           &closure,
                                                           error))
                                return false;
                        break;

                case FV_HTTP_PARSER_TERMINATING_CHUNK_TRAILER:
                        if (!handle_terminating_chunk_trailer(parser,
                                                              &closure,
                                                              error))
                                return false;
                        break;

                case FV_HTTP_PARSER_READING_CHUNK:
                        if (!handle_reading_chunk(parser,
                                                  &closure,
                                                  error))
                                return false;
                        break;

                case FV_HTTP_PARSER_READING_CHUNK_TERMINATOR1:
                        if (!handle_reading_chunk_terminator1(parser,
                                                              &closure,
                                                              error))
                                return false;
                        break;

                case FV_HTTP_PARSER_READING_CHUNK_TERMINATOR2:
                        if (!handle_reading_chunk_terminator2(parser,
                                                              &closure,
                                                              error))
                                return false;
                        break;
                }
        }

        return true;
}

bool
fv_http_parser_parser_eof(struct fv_http_parser *parser,
                          struct fv_error **error)
{
        switch (parser->state) {
        case FV_HTTP_PARSER_READING_REQUEST_LINE:
                /* This is an acceptable place for the client to shutdown the
                 * connection if we haven't received any of the line
                 * yet
                 */
                if (parser->buf_len == 0)
                        return true;

                /* flow through */
        case FV_HTTP_PARSER_TERMINATING_REQUEST_LINE:
        case FV_HTTP_PARSER_READING_HEADER:
        case FV_HTTP_PARSER_TERMINATING_HEADER:
        case FV_HTTP_PARSER_CHECKING_HEADER_CONTINUATION:
        case FV_HTTP_PARSER_READING_DATA_WITH_LENGTH:
        case FV_HTTP_PARSER_READING_CHUNK_LENGTH:
        case FV_HTTP_PARSER_TERMINATING_CHUNK_LENGTH:
        case FV_HTTP_PARSER_IGNORING_CHUNK_EXTENSION:
        case FV_HTTP_PARSER_TERMINATING_CHUNK_EXTENSION:
        case FV_HTTP_PARSER_IGNORING_CHUNK_TRAILER:
        case FV_HTTP_PARSER_TERMINATING_CHUNK_TRAILER:
        case FV_HTTP_PARSER_READING_CHUNK:
        case FV_HTTP_PARSER_READING_CHUNK_TERMINATOR1:
        case FV_HTTP_PARSER_READING_CHUNK_TERMINATOR2:
                /* Closing the connection here is invalid */
                fv_set_error(error,
                             &fv_http_parser_error,
                             FV_HTTP_PARSER_ERROR_INVALID,
                             "Client closed the connection unexpectedly");
                return false;
        }

        assert(NULL);

        return false;
}
