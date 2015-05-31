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

#ifndef FV_KEY_VALUE_H
#define FV_KEY_VALUE_H

#include <stdio.h>

#include "fv-key.h"

enum fv_key_value_event {
        FV_KEY_VALUE_EVENT_HEADER,
        FV_KEY_VALUE_EVENT_PROPERTY
};

typedef void
(* fv_key_value_func)(enum fv_key_value_event event,
                       int line_number,
                       const char *key,
                       const char *value,
                       void *user_data);

void
fv_key_value_load(FILE *file,
                   fv_key_value_func func,
                   void *user_data);

bool
fv_key_value_parse_bool_value(int line_number,
                               const char *value,
                               bool *result);

bool
fv_key_value_parse_int_value(int line_number,
                              const char *value_string,
                              int64_t max,
                              int64_t *result);

#endif /* FV_KEY_VALUE_H */
