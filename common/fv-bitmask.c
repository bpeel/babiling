/*
 * Babiling
 *
 * Copyright (C) 2015 Neil Roberts
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

#include "fv-bitmask.h"

void
fv_bitmask_set_length(struct fv_buffer *buffer,
                      int n_bits)
{
        unsigned long *longs;

        /* Round the length up to the nearest long */
        fv_buffer_set_length(buffer,
                             (n_bits + FV_BITMASK_BITS_PER_LONG - 1) /
                             FV_BITMASK_BITS_PER_LONG *
                             sizeof (unsigned long));
        /* Make sure that the bits in the trailing byte aren't set so
         * there's never a bit set that's greater or equal to the
         * number of bits.
         */
        if (n_bits % FV_BITMASK_BITS_PER_LONG) {
                longs = (unsigned long *) buffer->data;
                longs[FV_BITMASK_LONG(n_bits)] &= FV_BITMASK_MASK(n_bits) - 1;
        }
}

void
fv_bitmask_or(struct fv_buffer *a,
              const struct fv_buffer *b)
{
        unsigned long *longs_a;
        const unsigned long *longs_b;
        int i;

        if (b->length > a->length)
                fv_buffer_set_length(a, b->length);

        longs_a = (unsigned long *) a->data;
        longs_b = (const unsigned long *) b->data;

        for (i = 0; i < a->length / sizeof (unsigned long); i++)
                longs_a[i] |= longs_b[i];
}
