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

#ifndef FV_SLAB_H
#define FV_SLAB_H

#include <stddef.h>

struct fv_slab;

#define FV_SLAB_SIZE 2048

struct fv_slab_allocator {
        struct fv_slab *slabs;
        size_t slab_used;
};

#define FV_SLAB_STATIC_INIT \
        { .slabs = NULL, .slab_used = FV_SLAB_SIZE }

void
fv_slab_init(struct fv_slab_allocator *allocator);

void *
fv_slab_allocate(struct fv_slab_allocator *allocator,
                  size_t size,
                  int alignment);

void
fv_slab_destroy(struct fv_slab_allocator *allocator);

#endif /* FV_SLAB_H */
