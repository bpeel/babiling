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

#ifndef FV_SLICE_H
#define FV_SLICE_H

#include "fv-util.h"
#include "fv-slab.h"

struct fv_slice {
        struct fv_slice *next;
};

struct fv_slice_allocator {
        size_t element_size;
        size_t element_alignment;
        struct fv_slice *magazine;
        struct fv_slab_allocator slab;
};

#define FV_SLICE_ALLOCATOR(type, name)                                 \
        static struct fv_slice_allocator                               \
        name = {                                                        \
                .element_size = MAX(sizeof(type), sizeof (struct fv_slice)), \
                .element_alignment = FV_ALIGNOF(type),                 \
                .magazine = NULL,                                       \
                .slab = FV_SLAB_STATIC_INIT                            \
        }

void
fv_slice_allocator_init(struct fv_slice_allocator *allocator,
                         size_t size,
                         size_t alignment);

void
fv_slice_allocator_destroy(struct fv_slice_allocator *allocator);

void *
fv_slice_alloc(struct fv_slice_allocator *allocator);

void
fv_slice_free(struct fv_slice_allocator *allocator,
               void *ptr);

#endif /* FV_SLICE_H */
