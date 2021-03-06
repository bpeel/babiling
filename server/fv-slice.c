/*
 * Babiling
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

#include "fv-slice.h"

void
fv_slice_allocator_init(struct fv_slice_allocator *allocator,
                         size_t size,
                         size_t alignment)
{
        allocator->element_size = MAX(size, sizeof (struct fv_slice));
        allocator->element_alignment = alignment;
        allocator->magazine = NULL;
        fv_slab_init(&allocator->slab);
}

void
fv_slice_allocator_destroy(struct fv_slice_allocator *allocator)
{
        fv_slab_destroy(&allocator->slab);
}

void *
fv_slice_alloc(struct fv_slice_allocator *allocator)
{
        void *ret;

        if (allocator->magazine) {
                ret = allocator->magazine;
                allocator->magazine = allocator->magazine->next;
        } else {
                ret = fv_slab_allocate(&allocator->slab,
                                        allocator->element_size,
                                        allocator->element_alignment);
        }

        return ret;
}

void
fv_slice_free(struct fv_slice_allocator *allocator,
               void *ptr)
{
        struct fv_slice *slice = ptr;

        slice->next = allocator->magazine;
        allocator->magazine = slice;
}
