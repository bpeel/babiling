/*
 * Finvenkisto
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

#include "config.h"

#include <stdio.h>
#include <stdarg.h>

#include "fv-error.h"
#include "fv-util.h"

static struct fv_error *
fv_error_new(int buf_size)
{
        return fv_alloc(FV_STRUCT_OFFSET(struct fv_error, message) +
                         buf_size);
}

void
fv_set_error_va_list(struct fv_error **error_out,
                      struct fv_error_domain *domain,
                      int code,
                      const char *format,
                      va_list ap)
{
        struct fv_error *error;
        va_list apcopy;
        size_t buf_size, required_size;

        if (error_out == NULL)
                /* Error is being ignored */
                return;

        if (*error_out) {
                fv_warning("Multiple exceptions occured without "
                            "being handled");
                return;
        }

        buf_size = 64;

        error = fv_error_new(buf_size);

        va_copy(apcopy, ap);
        required_size = vsnprintf(error->message, buf_size, format, ap);

        if (required_size >= buf_size) {
                fv_free(error);
                buf_size = required_size + 1;
                error = fv_error_new(buf_size);
                vsnprintf(error->message, buf_size, format, apcopy);
        }

        va_end(apcopy);

        error->domain = domain;
        error->code = code;
        *error_out = error;
}

void
fv_set_error(struct fv_error **error_out,
              struct fv_error_domain *domain,
              int code,
              const char *format,
              ...)
{
        va_list ap;

        va_start(ap, format);
        fv_set_error_va_list(error_out, domain, code, format, ap);
        va_end(ap);
}

void
fv_error_free(struct fv_error *error)
{
        fv_free(error);
}

void
fv_error_clear(struct fv_error **error)
{
        fv_error_free(*error);
        *error = NULL;
}

void
fv_error_propagate(struct fv_error **error,
                    struct fv_error *other)
{
        fv_return_if_fail(other != NULL);

        if (error)
                *error = other;
        else
                fv_error_free(other);
}
