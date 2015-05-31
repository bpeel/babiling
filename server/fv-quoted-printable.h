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

#ifndef FV_QUOTED_PRINTABLE_H
#define FV_QUOTED_PRINTABLE_H

#include <stdlib.h>
#include <stdint.h>

#include "fv-error.h"

extern struct fv_error_domain
fv_quoted_printable_error;

enum fv_quoted_printable_error {
        FV_QUOTED_PRINTABLE_ERROR_INVALID_ESCAPE
};

enum fv_quoted_printable_state {
        /* The default state where if we see a character we will
         * directly add it to the output or if we see an equals sign
         * we'll start a quote */
        FV_QUOTED_PRINTABLE_STATE_OCTET,
        /* We've encountered an equals sign and the next character
         * will determine how to handle it */
        FV_QUOTED_PRINTABLE_STATE_QUOTE_START,
        /* We've encountered a space or tab character afer an equals
         * sign and we're ignoring the rest of the whitespace until
         * the end of the line */
        FV_QUOTED_PRINTABLE_STATE_SKIP_SPACES,
        /* We've encountered the CR (0xd) of a soft line break */
        FV_QUOTED_PRINTABLE_STATE_SOFT_CR,
        /* We've encountered the first hex digit of an escaped octet */
        FV_QUOTED_PRINTABLE_STATE_ESCAPED_OCTET
};

struct fv_quoted_printable_data {
        enum fv_quoted_printable_state state;
        int nibble;
        uint8_t *out;
        bool underscore_is_space;
};

void
fv_quoted_printable_decode_start(struct fv_quoted_printable_data *state,
                                  bool underscore_is_space);

ssize_t
fv_quoted_printable_decode(struct fv_quoted_printable_data *state,
                            const uint8_t *in_buffer,
                            size_t length,
                            uint8_t *out_buffer,
                            struct fv_error **error);

bool
fv_quoted_printable_decode_end(struct fv_quoted_printable_data *state,
                                struct fv_error **error);

#endif /* FV_QUOTED_PRINTABLE_H */
