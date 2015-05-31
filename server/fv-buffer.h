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

#ifndef FV_BUFFER_H
#define FV_BUFFER_H

#include <stdint.h>
#include <stdarg.h>

#include "fv-util.h"

struct fv_buffer {
        uint8_t *data;
        size_t length;
        size_t size;
};

#define FV_BUFFER_STATIC_INIT { .data = NULL, .length = 0, .size = 0 }

void
fv_buffer_init(struct fv_buffer *buffer);

void
fv_buffer_ensure_size(struct fv_buffer *buffer,
                       size_t size);

void
fv_buffer_set_length(struct fv_buffer *buffer,
                      size_t length);

FV_PRINTF_FORMAT(2, 3) void
fv_buffer_append_printf(struct fv_buffer *buffer,
                         const char *format,
                         ...);

void
fv_buffer_append_vprintf(struct fv_buffer *buffer,
                          const char *format,
                          va_list ap);

void
fv_buffer_append(struct fv_buffer *buffer,
                  const void *data,
                  size_t length);

static inline void
fv_buffer_append_c(struct fv_buffer *buffer,
                    char c)
{
        if (buffer->size > buffer->length)
                buffer->data[buffer->length++] = c;
        else
                fv_buffer_append(buffer, &c, 1);
}

void
fv_buffer_append_string(struct fv_buffer *buffer,
                         const char *str);

void
fv_buffer_destroy(struct fv_buffer *buffer);

#endif /* FV_BUFFER_H */
