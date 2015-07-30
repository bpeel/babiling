/*
 * Babiling
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
};

enum fv_ws_parser_result {
        FV_WS_PARSER_RESULT_NEED_MORE_DATA,
        FV_WS_PARSER_RESULT_FINISHED,
        FV_WS_PARSER_RESULT_ERROR
};

struct fv_ws_parser *
fv_ws_parser_new(const struct fv_ws_parser_vtable *vtable,
                 void *user_data);

enum fv_ws_parser_result
fv_ws_parser_parse_data(struct fv_ws_parser *parser,
                        const uint8_t *data,
                        size_t length,
                        size_t *consumed,
                        struct fv_error **error);

void
fv_ws_parser_free(struct fv_ws_parser *parser);

#endif /* FV_WS_PARSER_H */
