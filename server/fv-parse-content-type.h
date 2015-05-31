/*
 * Finvenkisto
 * Copyright (C) 2011, 2014  Neil Roberts
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

#ifndef FV_PARSE_CONTENT_TYPE_H
#define FV_PARSE_CONTENT_TYPE_H

#include <stdbool.h>

typedef bool
(* fv_parse_content_type_type_cb)(const char *type,
                                   void *user_data);

typedef bool
(* fv_parse_content_type_attribute_cb)(const char *attribute,
                                        const char *value,
                                        void *user_data);

bool
fv_parse_content_type(const char *header_value,
                       fv_parse_content_type_type_cb type_cb,
                       fv_parse_content_type_attribute_cb attribute_cb,
                       void *user_data);

#endif /* FV_PARSE_CONTENT_TYPE_H */
