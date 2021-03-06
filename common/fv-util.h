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

#ifndef FV_UTIL_H
#define FV_UTIL_H

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef __GNUC__
#define FV_NO_RETURN __attribute__((noreturn))
#define FV_PRINTF_FORMAT(string_index, first_to_check) \
  __attribute__((format(printf, string_index, first_to_check)))
#define FV_NULL_TERMINATED __attribute__((sentinel))
#else
#define FV_NO_RETURN
#define FV_PRINTF_FORMAT(string_index, first_to_check)
#define FV_NULL_TERMINATED
#endif

#ifndef MIN
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#endif

#if defined(HAVE_ALIGNOF)
#include <stdalign.h>
#define FV_ALIGNOF(x) alignof(x)
#elif defined(HAVE_UUALIGNOF)
#define FV_ALIGNOF(x) __alignof__(x)
#else
#error No alignof operator found
#endif

#define FV_STRUCT_OFFSET(container, member) \
  ((size_t) &((container *) 0)->member)

#define FV_SWAP_UINT16(x)                      \
  ((uint16_t)                                   \
   (((uint16_t) (x) >> 8) |                     \
    ((uint16_t) (x) << 8)))
#define FV_SWAP_UINT32(x)                               \
  ((uint32_t)                                           \
   ((((uint32_t) (x) & UINT32_C (0x000000ff)) << 24) |  \
    (((uint32_t) (x) & UINT32_C (0x0000ff00)) << 8) |   \
    (((uint32_t) (x) & UINT32_C (0x00ff0000)) >> 8) |   \
    (((uint32_t) (x) & UINT32_C (0xff000000)) >> 24)))
#define FV_SWAP_UINT64(x)                                               \
  ((uint64_t)                                                           \
   ((((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000000000ff)) << 56) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000000000ff00)) << 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000000000ff0000)) << 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00000000ff000000)) << 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x000000ff00000000)) >> 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x0000ff0000000000)) >> 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0x00ff000000000000)) >> 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C (0xff00000000000000)) >> 56)))

#if defined(HAVE_BIG_ENDIAN)
#define FV_UINT16_FROM_BE(x) ((uint16_t) (x))
#define FV_UINT32_FROM_BE(x) ((uint32_t) (x))
#define FV_UINT64_FROM_BE(x) ((uint64_t) (x))
#define FV_UINT16_FROM_LE(x) FV_SWAP_UINT16((uint16_t) (x))
#define FV_UINT32_FROM_LE(x) FV_SWAP_UINT32((uint32_t) (x))
#define FV_UINT64_FROM_LE(x) FV_SWAP_UINT64((uint64_t) (x))
#elif defined(HAVE_LITTLE_ENDIAN)
#define FV_UINT16_FROM_LE(x) ((uint16_t) (x))
#define FV_UINT32_FROM_LE(x) ((uint32_t) (x))
#define FV_UINT64_FROM_LE(x) ((uint64_t) (x))
#define FV_UINT16_FROM_BE(x) FV_SWAP_UINT16((uint16_t) (x))
#define FV_UINT32_FROM_BE(x) FV_SWAP_UINT32((uint32_t) (x))
#define FV_UINT64_FROM_BE(x) FV_SWAP_UINT64((uint64_t) (x))
#else
#error Platform is neither little-endian nor big-endian
#endif

#define FV_UINT16_TO_LE(x) FV_UINT16_FROM_LE(x)
#define FV_UINT16_TO_BE(x) FV_UINT16_FROM_BE(x)
#define FV_UINT32_TO_LE(x) FV_UINT32_FROM_LE(x)
#define FV_UINT32_TO_BE(x) FV_UINT32_FROM_BE(x)
#define FV_UINT64_TO_LE(x) FV_UINT64_FROM_LE(x)
#define FV_UINT64_TO_BE(x) FV_UINT64_FROM_BE(x)

#define FV_STMT_START do
#define FV_STMT_END while (0)

#define FV_N_ELEMENTS(array) \
  (sizeof (array) / sizeof ((array)[0]))

#define FV_STRINGIFY(macro_or_string) FV_STRINGIFY_ARG(macro_or_string)
#define FV_STRINGIFY_ARG(contents) #contents

void *
fv_alloc(size_t size);

void *
fv_calloc(size_t size);

void *
fv_realloc(void *ptr, size_t size);

void
fv_free(void *ptr);

char *
fv_strdup(const char *str);

void *
fv_memdup(const void *data, size_t size);

FV_NULL_TERMINATED char *
fv_strconcat(const char *string1, ...);

FV_NO_RETURN FV_PRINTF_FORMAT(1, 2) void
fv_fatal(const char *format, ...);

FV_PRINTF_FORMAT(1, 2) void
fv_warning(const char *format, ...);

int
fv_close(int fd);

static inline char
fv_ascii_tolower(char ch)
{
        if (ch >= 'A' && ch <= 'Z')
                return ch - 'A' + 'a';
        else
                return ch;
}

static inline bool
fv_ascii_isdigit(char ch)
{
        return ch >= '0' && ch <= '9';
}

static inline bool
fv_ascii_isxdigit(char ch)
{
        return (fv_ascii_isdigit(ch) ||
                (ch >= 'a' && ch <= 'f') ||
                (ch >= 'A' && ch <= 'F'));
}

/* Returns the value of a single ASCII hexadecimal digit, which is
 * either in upper or lower case. If the character is not a hex digit
 * then the results are undefined.
 */
static inline int
fv_ascii_xdigit_value(char ch)
{
        if (ch >= 'A')
                return (ch & ~('a' - 'A')) - 'A' + 10;
        else
                return ch - '0';
}

/* Returns true if the given strings are the same, ignoring case. The
 * case is compared ignoring the locale and operates on ASCII only.
 */
bool
fv_ascii_string_case_equal(const char *a, const char *b);

#ifdef HAVE_STATIC_ASSERT
#define FV_STATIC_ASSERT(EXPRESSION, MESSAGE)  \
        _Static_assert(EXPRESSION, MESSAGE);
#else
#define FV_STATIC_ASSERT(EXPRESSION, MESSAGE)
#endif

#define fv_return_if_fail(condition)                           \
        FV_STMT_START {                                        \
                if (!(condition)) {                             \
                        fv_warning("assertion '%s' failed",    \
                                    #condition);                \
                        return;                                 \
                }                                               \
        } FV_STMT_END

#define fv_return_val_if_fail(condition, val)                  \
        FV_STMT_START {                                        \
                if (!(condition)) {                             \
                        fv_warning("assertion '%s' failed",    \
                                    #condition);                \
                        return (val);                           \
                }                                               \
        } FV_STMT_END

#define fv_warn_if_reached()                                           \
        FV_STMT_START {                                                \
                fv_warning("Line %i in %s should not be reached",      \
                            __LINE__,                                   \
                            __FILE__);                                  \
        } FV_STMT_END

#ifdef HAVE_FFS
#include <strings.h>
#define fv_util_ffs(x) ffs(x)
#else
int
fv_util_ffs(int value);
#endif

#ifdef HAVE_FFSL
#include <string.h>
#define fv_util_ffsl(x) ffsl(x)
#else
int
fv_util_ffsl(long int value);
#endif

#endif /* FV_UTIL_H */
