/*
 * Notbit - A Bitmessage client
 * Copyright (C) 2014  Neil Roberts
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

#ifndef FV_MAIL_PARSER_H
#define FV_MAIL_PARSER_H

#include <stdint.h>
#include <stdbool.h>

#include "fv-error.h"
#include "fv-address.h"

extern struct fv_error_domain
fv_mail_parser_error;

enum fv_mail_parser_error {
        FV_MAIL_PARSER_ERROR_INVALID_HEADER,
        FV_MAIL_PARSER_ERROR_INVALID_CONTENT_TYPE,
        FV_MAIL_PARSER_ERROR_INVALID_TRANSFER_ENCODING,
        FV_MAIL_PARSER_ERROR_INVALID_ADDRESS,
        FV_MAIL_PARSER_ERROR_MISSING_HEADER
};

enum fv_mail_parser_event {
        FV_MAIL_PARSER_EVENT_SUBJECT,
        FV_MAIL_PARSER_EVENT_SOURCE,
        FV_MAIL_PARSER_EVENT_DESTINATION,
        FV_MAIL_PARSER_EVENT_CONTENT
};

struct fv_mail_parser;

typedef bool
(* fv_mail_parser_address_cb)(enum fv_mail_parser_event event,
                               const struct fv_address *address,
                               void *user_data,
                               struct fv_error **error);

typedef bool
(* fv_mail_parser_data_cb)(enum fv_mail_parser_event event,
                            const uint8_t *data,
                            size_t length,
                            void *user_data,
                            struct fv_error **error);

struct fv_mail_parser *
fv_mail_parser_new(fv_mail_parser_address_cb address_cb,
                    fv_mail_parser_data_cb data_cb,
                    void *user_data);

bool
fv_mail_parser_parse(struct fv_mail_parser *parser,
                      const uint8_t *data,
                      size_t length,
                      struct fv_error **error);

bool
fv_mail_parser_end(struct fv_mail_parser *parser,
                    struct fv_error **error);

void
fv_mail_parser_free(struct fv_mail_parser *parser);

#endif /* FV_MAIL_PARSER_H */
