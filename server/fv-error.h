/*
 * Notbit - A Bitmessage client
 * Copyright (C) 2013  Neil Roberts
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

#ifndef FV_ERROR_H
#define FV_ERROR_H

#include <stdarg.h>

#include "fv-util.h"

/* Exception handling mechanism inspired by glib's GError */

struct fv_error_domain {
        int stub;
};

struct fv_error {
        struct fv_error_domain *domain;
        int code;
        char message[1];
};

void
fv_set_error_va_list(struct fv_error **error_out,
                      struct fv_error_domain *domain,
                      int code,
                      const char *format,
                      va_list ap);

FV_PRINTF_FORMAT(4, 5) void
fv_set_error(struct fv_error **error,
              struct fv_error_domain *domain,
              int code,
              const char *format,
              ...);

void
fv_error_free(struct fv_error *error);

void
fv_error_clear(struct fv_error **error);

void
fv_error_propagate(struct fv_error **error,
                    struct fv_error *other);

#endif /* FV_ERROR_H */
