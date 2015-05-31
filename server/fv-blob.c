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

#include <string.h>

#include "fv-blob.h"
#include "fv-util.h"

void
fv_blob_dynamic_init(struct fv_buffer *buffer,
                      enum fv_proto_inv_type type)
{
        struct fv_blob *blob;

        fv_buffer_init(buffer);

        fv_buffer_set_length(buffer,
                              FV_STRUCT_OFFSET(struct fv_blob, data));

        blob = (struct fv_blob *) buffer->data;
        blob->type = type;
}

struct fv_blob *
fv_blob_dynamic_end(struct fv_buffer *buffer)
{
        struct fv_blob *blob = (struct fv_blob *) buffer->data;

        blob->size = buffer->length - FV_STRUCT_OFFSET(struct fv_blob, data);
        fv_ref_count_init(&blob->ref_count);

        return blob;
}

struct fv_blob *
fv_blob_new(enum fv_proto_inv_type type,
             const void *data,
             size_t size)
{
        struct fv_blob *blob =
                fv_alloc(FV_STRUCT_OFFSET(struct fv_blob, data) + size);

        blob->type = type;
        blob->size = size;

        fv_ref_count_init(&blob->ref_count);

        if (data)
                memcpy(blob->data, data, size);

        return blob;
}

struct fv_blob *
fv_blob_ref(struct fv_blob *blob)
{
        fv_ref_count_ref(&blob->ref_count);

        return blob;
}

void
fv_blob_unref(struct fv_blob *blob)
{
        if (fv_ref_count_unref(&blob->ref_count) <= 1) {
                fv_ref_count_destroy(&blob->ref_count);
                fv_free(blob);
        }
}
