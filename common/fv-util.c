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

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>

#include "fv-util.h"

void
fv_fatal(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);

        fputc('\n', stderr);

        fflush(stderr);

        abort();
}

void
fv_warning(const char *format, ...)
{
        va_list ap;

        va_start(ap, format);
        vfprintf(stderr, format, ap);
        va_end(ap);

        fputc('\n', stderr);
}

void *
fv_alloc(size_t size)
{
        void *result = malloc(size);

        if (result == NULL)
                fv_fatal("Memory exhausted");

        return result;
}

void *
fv_calloc(size_t size)
{
        void *result = fv_alloc(size);

        memset(result, 0, size);

        return result;
}

void *
fv_realloc(void *ptr, size_t size)
{
        if (ptr == NULL)
                return fv_alloc(size);

        ptr = realloc(ptr, size);

        if (ptr == NULL)
                fv_fatal("Memory exhausted");

        return ptr;
}

void
fv_free(void *ptr)
{
        if (ptr)
                free(ptr);
}

char *
fv_strdup(const char *str)
{
        return fv_memdup(str, strlen(str) + 1);
}

void *
fv_memdup(const void *data, size_t size)
{
        void *ret;

        ret = fv_alloc(size);
        memcpy(ret, data, size);

        return ret;
}

char *
fv_strconcat(const char *string1, ...)
{
        size_t string1_length;
        size_t total_length;
        size_t str_length;
        va_list ap, apcopy;
        const char *str;
        char *result, *p;

        if (string1 == NULL)
                return fv_strdup("");

        total_length = string1_length = strlen(string1);

        va_start(ap, string1);
        va_copy(apcopy, ap);

        while ((str = va_arg(ap, const char *)))
                total_length += strlen(str);

        va_end(ap);

        result = fv_alloc(total_length + 1);
        memcpy(result, string1, string1_length);
        p = result + string1_length;

        while ((str = va_arg(apcopy, const char *))) {
                str_length = strlen(str);
                memcpy(p, str, str_length);
                p += str_length;
        }
        *p = '\0';

        va_end(apcopy);

        return result;
}

int
fv_close(int fd)
{
        int ret;

        do {
                ret = close(fd);
        } while (ret == -1 && errno == EINTR);

        return ret;
}

#ifndef HAVE_FFS

int
fv_util_ffs(int value)
{
        int pos = 1;

        if (value == 0)
                return 0;

        while ((value & 1) == 0) {
                value >>= 1;
                pos++;
        }

        return pos;
}

#endif

#ifndef HAVE_FFSL

int
fv_util_ffsl(long int value)
{
        int pos = fv_util_ffs(value);

        if (pos)
                return pos;

        pos = fv_util_ffs(value >> ((sizeof (long int) - sizeof (int)) * 8));

        if (pos)
                return pos + (sizeof (long int) - sizeof (int)) * 8;

        return 0;
}

#endif
