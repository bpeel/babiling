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

#ifndef FV_BLOB_H
#define FV_BLOB_H

#include <stdint.h>
#include <stddef.h>

#include "fv-proto.h"
#include "fv-ref-count.h"
#include "fv-buffer.h"

/* A blob represents a ref-counted immutable chunk of data. This will
 * be used to hold all inventory objects from the network such as
 * messages and public keys. The ref-count is thread-safe so that the
 * blob can be passed off to the store thread to be written to
 * disk. */

struct fv_blob {
        enum fv_proto_inv_type type;

        struct fv_ref_count ref_count;

        size_t size;

        /* Over-allocated to contain the data */
        uint8_t data[1];
};

void
fv_blob_dynamic_init(struct fv_buffer *buffer,
                      enum fv_proto_inv_type type);

struct fv_blob *
fv_blob_dynamic_end(struct fv_buffer *buffer);

struct fv_blob *
fv_blob_new(enum fv_proto_inv_type type,
             const void *data,
             size_t size);

struct fv_blob *
fv_blob_ref(struct fv_blob *blob);

void
fv_blob_unref(struct fv_blob *blob);

#endif /* FV_BLOB_H */
