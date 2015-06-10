/*
 * Finvenkisto
 * Copyright (C) 2013, 2015  Neil Roberts
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

#ifndef FV_POINTER_ARRAY_H
#define FV_POINTER_ARRAY_H

#include <stdint.h>

#include "fv-buffer.h"

static inline void
fv_pointer_array_append(struct fv_buffer *buf,
                        void *pointer)
{
        fv_buffer_append(buf, &pointer, sizeof (pointer));
}

#define fv_pointer_array_length(buf)            \
        ((buf)->length / sizeof (void *))

#define fv_pointer_array_get(buf, index)        \
        ((((void **) (buf)->data))[index])

#define fv_pointer_array_set(buf, index, pointer)       \
        ((((void **) (buf)->data))[index] = (pointer))

#endif /* FV_POINTER_ARRAY_H */
