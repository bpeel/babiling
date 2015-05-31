/*
 * Finvenkisto
 * Copyright (C) 2014  Neil Roberts
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

#include <sys/stat.h>
#include <errno.h>
#include <string.h>

#include "fv-mkdir.h"
#include "fv-buffer.h"
#include "fv-file-error.h"

bool
fv_mkdir(const char *name,
          struct fv_error **error)
{
        if (mkdir(name, S_IRWXU | S_IRWXG | S_IRWXO) == -1 && errno != EEXIST) {
                fv_file_error_set(error,
                                   errno,
                                   "Error creating directory %s: %s",
                                   name,
                                   strerror(errno));
                return false;
        }

        return true;
}

bool
fv_mkdir_hierarchy(struct fv_buffer *buf,
                    struct fv_error **error)
{
        uint8_t *slash;
        bool res;

        if (buf->length < 1)
                return true;

        slash = buf->data;

        while ((slash = memchr(slash + 1,
                               '/',
                               buf->data + buf->length - slash - 1))) {
                *slash = '\0';

                res = fv_mkdir((const char *) buf->data, error);

                *slash = '/';

                if (!res)
                        return false;
        }

        if (buf->data[buf->length - 1] != '/') {
                fv_buffer_append_c(buf, '\0');
                buf->length--;
                if (!fv_mkdir((const char *) buf->data, error))
                        return false;
        }

        return true;
}
