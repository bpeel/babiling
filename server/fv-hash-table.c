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

#include "config.h"

#include <string.h>

#include "fv-hash-table.h"
#include "fv-util.h"
#include "fv-slice.h"
#include "fv-proto.h"

struct fv_hash_table_entry {
        struct fv_hash_table_entry *next;
        uint8_t *data;
};

struct fv_hash_table {
        size_t hash_offset;
        size_t size;
        size_t n_entries;
        struct fv_hash_table_entry **entries;
};

FV_SLICE_ALLOCATOR(struct fv_hash_table_entry,
                    fv_hash_table_entry_allocator);

static void
alloc_entries(struct fv_hash_table *hash_table,
              size_t size)
{
        size_t alloc_size = sizeof (struct fv_hash_table_entry *) * size;
        hash_table->entries = fv_alloc(alloc_size);
        memset(hash_table->entries, 0, alloc_size);
        hash_table->size = size;
}

struct fv_hash_table *
fv_hash_table_new(size_t hash_offset)
{
        struct fv_hash_table *hash_table = fv_alloc(sizeof *hash_table);

        hash_table->hash_offset = hash_offset;
        hash_table->n_entries = 0;

        alloc_entries(hash_table, 8);

        return hash_table;
}

static uint32_t
get_index(struct fv_hash_table *hash_table,
          const uint8_t *hash)
{
        uint32_t index;

        memcpy(&index, hash, sizeof index);

        return index & (hash_table->size - 1);
}

void *
fv_hash_table_get(struct fv_hash_table *hash_table,
                   const uint8_t *hash)
{
        struct fv_hash_table_entry *entry;
        uint32_t index = get_index(hash_table, hash);

        for (entry = hash_table->entries[index]; entry; entry = entry->next) {
                if (!memcmp(hash,
                            entry->data + hash_table->hash_offset,
                            FV_PROTO_HASH_LENGTH)) {
                        return entry->data;
                }
        }

        return NULL;
}

static void
insert_entry_at_index(struct fv_hash_table *hash_table,
                      struct fv_hash_table_entry *entry,
                      uint32_t index)
{
        entry->next = hash_table->entries[index];
        hash_table->entries[index] = entry;
}

static void
fv_hash_table_grow(struct fv_hash_table *hash_table)
{
        size_t old_size = hash_table->size;
        struct fv_hash_table_entry **old_entries = hash_table->entries;
        struct fv_hash_table_entry *entry, *next;
        const uint8_t *hash;
        uint32_t index;
        int i;

        alloc_entries(hash_table, old_size * 2);

        for (i = 0; i < old_size; i++) {
                for (entry = old_entries[i]; entry; entry = next) {
                        next = entry->next;

                        hash = entry->data + hash_table->hash_offset;

                        index = get_index(hash_table, hash);

                        insert_entry_at_index(hash_table, entry, index);
                }
        }

        fv_free(old_entries);
}

void *
fv_hash_table_set(struct fv_hash_table *hash_table,
                   void *value)
{
        struct fv_hash_table_entry *entry;
        const uint8_t *hash = (const uint8_t *) value + hash_table->hash_offset;
        uint32_t index = get_index(hash_table, hash);
        void *old_value;

        for (entry = hash_table->entries[index]; entry; entry = entry->next) {
                if (!memcmp(hash,
                            entry->data + hash_table->hash_offset,
                            FV_PROTO_HASH_LENGTH)) {
                        old_value = entry->data;
                        entry->data = value;
                        return old_value;
                }
        }

        if (hash_table->n_entries >= hash_table->size * 3 / 4) {
                fv_hash_table_grow(hash_table);
                index = get_index(hash_table, hash);
        }

        entry = fv_slice_alloc(&fv_hash_table_entry_allocator);
        entry->data = value;
        insert_entry_at_index(hash_table, entry, index);

        hash_table->n_entries++;

        return NULL;
}

bool
fv_hash_table_remove(struct fv_hash_table *hash_table,
                      const void *value)
{
        struct fv_hash_table_entry *entry, *prev = NULL;
        const uint8_t *hash = (const uint8_t *) value + hash_table->hash_offset;
        uint32_t index = get_index(hash_table, hash);

        for (entry = hash_table->entries[index]; entry; entry = entry->next) {
                if (entry->data == value) {
                        if (prev)
                                prev->next = entry->next;
                        else
                                hash_table->entries[index] = entry->next;

                        fv_slice_free(&fv_hash_table_entry_allocator, entry);

                        hash_table->n_entries--;

                        return true;
                }

                prev = entry;
        }

        return false;
}

void
fv_hash_table_free(struct fv_hash_table *hash_table)
{
        struct fv_hash_table_entry *entry, *next;
        int i;

        for (i = 0; i < hash_table->size; i++) {
                for (entry = hash_table->entries[i]; entry; entry = next) {
                        next = entry->next;
                        fv_slice_free(&fv_hash_table_entry_allocator, entry);
                }
        }

        fv_free(hash_table->entries);
        fv_free(hash_table);
}
