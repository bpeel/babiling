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

#ifndef FV_HASH_TABLE_H
#define FV_HASH_TABLE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct fv_hash_table;

struct fv_hash_table *
fv_hash_table_new(size_t hash_offset);

void *
fv_hash_table_get(struct fv_hash_table *hash_table,
                   const uint8_t *hash);

void *
fv_hash_table_set(struct fv_hash_table *hash_table,
                   void *value);

bool
fv_hash_table_remove(struct fv_hash_table *hash_table,
                      const void *value);

void
fv_hash_table_free(struct fv_hash_table *hash_table);

#endif /* FV_HASH_TABLE_H */
