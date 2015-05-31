/*
 * Finvenkisto
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

#ifndef FV_PARSE_ADDRESSES_H
#define FV_PARSE_ADDRESSES_H

#include <stdbool.h>
#include <stdint.h>

#include "fv-error.h"
#include "fv-buffer.h"
#include "fv-address.h"

extern struct fv_error_domain
fv_parse_addresses_error;

enum fv_parse_addresses_error {
        FV_PARSE_ADDRESSES_ERROR_INVALID
};

typedef bool
(* fv_parse_addresses_cb)(const struct fv_address *address,
                           void *user_data,
                           struct fv_error **error);

bool
fv_parse_addresses(struct fv_buffer *buffer,
                    fv_parse_addresses_cb cb,
                    void *user_data,
                    struct fv_error **error);

#endif /* FV_PARSE_ADDRESSES_H */
