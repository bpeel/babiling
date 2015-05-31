/*
 * Finvenkisto
 * Copyright (C) 2013, 2014  Neil Roberts
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

#ifndef FV_POW_H
#define FV_POW_H

#include <stdint.h>
#include <stdbool.h>

#include "fv-error.h"

struct fv_pow;

struct fv_pow_cookie;

typedef void (* fv_pow_calculate_func)(uint64_t nonce,
                                        void *user_data);

struct fv_pow *
fv_pow_new(void);

struct fv_pow_cookie *
fv_pow_calculate(struct fv_pow *pow,
                  const uint8_t *payload,
                  size_t length,
                  int pow_per_byte,
                  int pow_extra_bytes,
                  fv_pow_calculate_func func,
                  void *user_data);

void
fv_pow_cancel(struct fv_pow_cookie *cookie);

void
fv_pow_free(struct fv_pow *pow);

uint64_t
fv_pow_calculate_target(size_t length,
                         int payload_extra_bytes,
                         int average_trials_per_byte);

uint64_t
fv_pow_calculate_value(const uint8_t *payload,
                        size_t length);

bool
fv_pow_check(const uint8_t *payload,
              size_t length,
              int pow_per_byte,
              int pow_extra_bytes);

#endif /* FV_POW_H */
