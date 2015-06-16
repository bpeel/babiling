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

#ifndef FV_WS_PARSER_H
#define FV_WS_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "fv-error.h"

extern struct fv_error_domain
fv_ws_parser_error;

#define FV_WS_PARSER_MAX_LINE_LENGTH 512

enum fv_ws_parser_error {
        FV_WS_PARSER_ERROR_INVALID,
        FV_WS_PARSER_ERROR_UNSUPPORTED,
        FV_WS_PARSER_ERROR_CANCELLED
};

struct fv_ws_parser_vtable {
        bool (* request_line_received)(const char *method,
                                       const char *uri,
                                       void *user_data);
        bool (* header_received)(const char *field_name,
                                 const char *value,
                                 void *user_data);
        bool (* data_received)(const uint8_t *data,
                               unsigned int length,
                               void *user_data);
        bool (* request_finished)(void *user_data);
};

struct fv_ws_parser {
        unsigned int buf_len;

        uint8_t buf[FV_WS_PARSER_MAX_LINE_LENGTH];

        enum {
                FV_WS_PARSER_READING_REQUEST_LINE,
                FV_WS_PARSER_TERMINATING_REQUEST_LINE,
                FV_WS_PARSER_READING_HEADER,
                FV_WS_PARSER_TERMINATING_HEADER,
                FV_WS_PARSER_CHECKING_HEADER_CONTINUATION,
                FV_WS_PARSER_READING_DATA_WITH_LENGTH,
                FV_WS_PARSER_READING_CHUNK_LENGTH,
                FV_WS_PARSER_TERMINATING_CHUNK_LENGTH,
                FV_WS_PARSER_IGNORING_CHUNK_EXTENSION,
                FV_WS_PARSER_TERMINATING_CHUNK_EXTENSION,
                FV_WS_PARSER_IGNORING_CHUNK_TRAILER,
                FV_WS_PARSER_TERMINATING_CHUNK_TRAILER,
                FV_WS_PARSER_READING_CHUNK,
                FV_WS_PARSER_READING_CHUNK_TERMINATOR1,
                FV_WS_PARSER_READING_CHUNK_TERMINATOR2
        } state;

        const struct fv_ws_parser_vtable *vtable;
        void *user_data;

        enum {
                FV_WS_PARSER_TRANSFER_NONE,
                FV_WS_PARSER_TRANSFER_CONTENT_LENGTH,
                FV_WS_PARSER_TRANSFER_CHUNKED
        } transfer_encoding;

        unsigned int content_length;
};

void
fv_ws_parser_init(struct fv_ws_parser *parser,
                  const struct fv_ws_parser_vtable *vtable,
                  void *user_data);

bool
fv_ws_parser_parse_data(struct fv_ws_parser *parser,
                        const uint8_t *data,
                        unsigned int length,
                        struct fv_error **error);

bool
fv_ws_parser_parser_eof(struct fv_ws_parser *parser,
                        struct fv_error **error);

#endif /* FV_WS_PARSER_H */
