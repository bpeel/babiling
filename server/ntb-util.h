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

#ifndef NTB_UTIL_H
#define NTB_UTIL_H

#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>

#ifdef __GNUC__
#define NTB_NO_RETURN __attribute__((noreturn))
#define NTB_PRINTF_FORMAT(string_index, first_to_check) \
  __attribute__((format(printf, string_index, first_to_check)))
#define NTB_NULL_TERMINATED __attribute__((sentinel))
#else
#define NTB_NO_RETURN
#define NTB_PRINTF_FORMAT(string_index, first_to_check)
#define NTB_NULL_TERMINATED
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define NTB_ALIGNOF(x) ALIGNOF_NAME(x)

#define NTB_STRUCT_OFFSET(container, member) \
  ((size_t) &((container *) 0)->member)

#define NTB_SWAP_UINT16(x)                      \
  ((uint16_t)                                   \
   (((uint16_t) (x) >> 8) |                     \
    ((uint16_t) (x) << 8)))
#define NTB_SWAP_UINT32(x)                              \
  ((uint32_t)                                           \
   ((((uint32_t) (x) & UINT32_C(0x000000ff)) << 24) |   \
    (((uint32_t) (x) & UINT32_C(0x0000ff00)) << 8) |    \
    (((uint32_t) (x) & UINT32_C(0x00ff0000)) >> 8) |    \
    (((uint32_t) (x) & UINT32_C(0xff000000)) >> 24)))
#define NTB_SWAP_UINT64(x)                                              \
  ((uint64_t)                                                           \
   ((((uint64_t) (x) & (uint64_t) UINT64_C(0x00000000000000ff)) << 56) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C(0x000000000000ff00)) << 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C(0x0000000000ff0000)) << 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C(0x00000000ff000000)) << 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C(0x000000ff00000000)) >> 8) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C(0x0000ff0000000000)) >> 24) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C(0x00ff000000000000)) >> 40) | \
    (((uint64_t) (x) & (uint64_t) UINT64_C(0xff00000000000000)) >> 56)))

#if defined(HAVE_BIG_ENDIAN)
#define NTB_UINT16_FROM_BE(x) (x)
#define NTB_UINT32_FROM_BE(x) (x)
#define NTB_UINT64_FROM_BE(x) (x)
#define NTB_UINT16_FROM_LE(x) NTB_SWAP_UINT16(x)
#define NTB_UINT32_FROM_LE(x) NTB_SWAP_UINT32(x)
#define NTB_UINT64_FROM_LE(x) NTB_SWAP_UINT64(x)
#elif defined(HAVE_LITTLE_ENDIAN)
#define NTB_UINT16_FROM_LE(x) (x)
#define NTB_UINT32_FROM_LE(x) (x)
#define NTB_UINT64_FROM_LE(x) (x)
#define NTB_UINT16_FROM_BE(x) NTB_SWAP_UINT16(x)
#define NTB_UINT32_FROM_BE(x) NTB_SWAP_UINT32(x)
#define NTB_UINT64_FROM_BE(x) NTB_SWAP_UINT64(x)
#else
#error Platform is neither little-endian nor big-endian
#endif

#define NTB_UINT16_TO_LE(x) NTB_UINT16_FROM_LE(x)
#define NTB_UINT16_TO_BE(x) NTB_UINT16_FROM_BE(x)
#define NTB_UINT32_TO_LE(x) NTB_UINT32_FROM_LE(x)
#define NTB_UINT32_TO_BE(x) NTB_UINT32_FROM_BE(x)
#define NTB_UINT64_TO_LE(x) NTB_UINT64_FROM_LE(x)
#define NTB_UINT64_TO_BE(x) NTB_UINT64_FROM_BE(x)

#define NTB_STMT_START do
#define NTB_STMT_END while (0)

#define NTB_N_ELEMENTS(array) \
  (sizeof (array) / sizeof ((array)[0]))

#define NTB_STRINGIFY(macro_or_string) NTB_STRINGIFY_ARG(macro_or_string)
#define NTB_STRINGIFY_ARG(contents) #contents

void *
ntb_alloc(size_t size);

void *
ntb_realloc(void *ptr, size_t size);

void
ntb_free(void *ptr);

char *
ntb_strdup(const char *str);

void *
ntb_memdup(const void *data, size_t size);

NTB_NULL_TERMINATED char *
ntb_strconcat(const char *string1, ...);

NTB_NO_RETURN NTB_PRINTF_FORMAT(1, 2) void
ntb_fatal(const char *format, ...);

NTB_PRINTF_FORMAT(1, 2) void
ntb_warning(const char *format, ...);

int
ntb_close(int fd);

static inline char
ntb_ascii_tolower(char ch)
{
        if (ch >= 'A' && ch <= 'Z')
                return ch - 'A' + 'a';
        else
                return ch;
}

pthread_t
ntb_create_thread(void *(* thread_func)(void *),
                  void *user_data);

#ifdef HAVE_STATIC_ASSERT
#define NTB_STATIC_ASSERT(EXPRESSION, MESSAGE)  \
        _Static_assert(EXPRESSION, MESSAGE);
#else
#define NTB_STATIC_ASSERT(EXPRESSION, MESSAGE)
#endif

#define ntb_return_if_fail(condition)                           \
        NTB_STMT_START {                                        \
                if (!(condition)) {                             \
                        ntb_warning("assertion '%s' failed",    \
                                    #condition);                \
                        return;                                 \
                }                                               \
        } NTB_STMT_END

#define ntb_return_val_if_fail(condition, val)                  \
        NTB_STMT_START {                                        \
                if (!(condition)) {                             \
                        ntb_warning("assertion '%s' failed",    \
                                    #condition);                \
                        return (val);                           \
                }                                               \
        } NTB_STMT_END

#define ntb_warn_if_reached()                                           \
        NTB_STMT_START {                                                \
                ntb_warning("Line %i in %s should not be reached",      \
                            __LINE__,                                   \
                            __FILE__);                                  \
        } NTB_STMT_END

#endif /* NTB_UTIL_H */
