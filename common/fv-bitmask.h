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

#ifndef FV_BITMASK_H
#define FV_BITMASK_H

#include <limits.h>
#include <stdbool.h>

#include "fv-buffer.h"
#include "fv-util.h"

#define FV_BITMASK_BITS_PER_LONG (sizeof (unsigned long) * 8)
#define FV_BITMASK_MASK(bit) (1UL << ((bit) % FV_BITMASK_BITS_PER_LONG))
#define FV_BITMASK_LONG(bit) ((bit) / FV_BITMASK_BITS_PER_LONG)

void
fv_bitmask_set_length(struct fv_buffer *buffer,
                      int n_bits);

static inline void
fv_bitmask_set(struct fv_buffer *buffer,
               int bit,
               bool value)
{
        unsigned long *longs = (unsigned long *) buffer->data;
        unsigned long mask = FV_BITMASK_MASK(bit);

        if (value)
                longs[FV_BITMASK_LONG(bit)] |= mask;
        else
                longs[FV_BITMASK_LONG(bit)] &= ~mask;
}

static inline bool
fv_bitmask_get(struct fv_buffer *buffer,
               int bit)
{
        unsigned long *longs = (unsigned long *) buffer->data;

        return !!(longs[FV_BITMASK_LONG(bit)] & FV_BITMASK_MASK(bit));
}

void
fv_bitmask_or(struct fv_buffer *a,
              const struct fv_buffer *b);

/* To be used like this:
 *  fv_bitmask_for_each(buffer, bit_num) {
 *    do_something_with(bit_num);
 *  }
 * where bit_num is an integer variable that will be set in turn with
 * all bits (starting from 0) which are set.
 * Note that this is a nested loop so break won't work.
 */
#define fv_bitmask_for_each(buffer, bit_num)                            \
        for (int _l = 0;                                                \
             _l < (buffer)->length / sizeof (unsigned long);            \
             _l++)                                                      \
                for (unsigned long _mask = ((const unsigned long *)     \
                                            (buffer)->data)[_l],        \
                             _bit_pos;                                  \
                     _bit_pos = fv_util_ffsl(_mask),                    \
                             (bit_num) = _bit_pos - 1 +                 \
                             _l * FV_BITMASK_BITS_PER_LONG,             \
                             _bit_pos;                                  \
                     _mask &= (ULONG_MAX - 1) << (_bit_pos - 1))

#endif /* FV_BITMASK_H */
