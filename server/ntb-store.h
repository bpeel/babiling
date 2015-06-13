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

#ifndef NTB_STORE_H
#define NTB_STORE_H

#include <stdint.h>

#include "ntb-blob.h"
#include "ntb-error.h"
#include "ntb-netaddress.h"
#include "ntb-key.h"
#include "ntb-proto.h"

/* The store is used to do all of the disk I/O. The actions are stored
 * in a queue and then executed in a separate thread */

struct ntb_store;

extern struct ntb_error_domain
ntb_store_error;

enum ntb_store_error {
        NTB_STORE_ERROR_INVALID_STORE_DIRECTORY,
        NTB_STORE_ERROR_INVALID_MAILDIR
};

struct ntb_store_cookie;

struct ntb_store_addr {
        int64_t timestamp;
        uint32_t stream;
        uint64_t services;
        struct ntb_netaddress address;
};

struct ntb_store_outgoing {
        struct ntb_address from_address;
        struct ntb_address to_address;
        uint8_t ackdata[NTB_PROTO_ACKDATA_SIZE];
        uint64_t content_id;
        int content_encoding;
        int64_t last_getpubkey_send_time;
        int64_t last_msg_send_time;
};

typedef void
(* ntb_store_for_each_blob_func)(enum ntb_proto_inv_type type,
                                 const uint8_t *hash,
                                 int64_t timestamp,
                                 void *user_data);

typedef void
(* ntb_store_for_each_pubkey_blob_func)(const uint8_t *hash,
                                        int64_t timestamp,
                                        struct ntb_blob *blob,
                                        void *user_data);

typedef void
(* ntb_store_for_each_addr_func)(const struct ntb_store_addr *addr,
                                 void *user_data);

typedef void
(* ntb_store_for_each_key_func)(struct ntb_key *key,
                                void *user_data);

typedef void
(* ntb_store_for_each_outgoing_func)(const struct ntb_store_outgoing *outgoing,
                                     void *user_data);

/* This is called when a load is complete. If the load succeeded then
 * blob will point to the contents. If it failed the callback will
 * still be called but blob will be NULL. The callback won't be called
 * at all if the task is cancelled. The callback will always be
 * invoked from an idle handler in the main thread */
typedef void (* ntb_store_load_callback)(struct ntb_blob *blob,
                                         void *user_data);

struct ntb_store *
ntb_store_new(const char *store_directory,
              const char *maildir,
              struct ntb_error **error);

const char *
ntb_store_get_directory(struct ntb_store *store);

void
ntb_store_start(struct ntb_store *store);

struct ntb_store *
ntb_store_get_default(void);

void
ntb_store_set_default(struct ntb_store *store);

void
ntb_store_save_blob(struct ntb_store *store,
                    const uint8_t *hash,
                    struct ntb_blob *blob);

void
ntb_store_delete_object(struct ntb_store *store,
                        const uint8_t *hash);

void
ntb_store_save_addr_list(struct ntb_store *store,
                         struct ntb_store_addr *addrs,
                         int n_addrs);

void
ntb_store_save_keys(struct ntb_store *store,
                    struct ntb_key * const *keys,
                    int n_keys);

void
ntb_store_save_outgoings(struct ntb_store *store,
                         struct ntb_blob *blob);

void
ntb_store_save_message(struct ntb_store *store,
                       int64_t timestamp,
                       struct ntb_key *from_key,
                       const char *from_address,
                       struct ntb_key *to_key,
                       struct ntb_blob *blob);

void
ntb_store_save_message_content(struct ntb_store *store,
                               uint64_t content_id,
                               struct ntb_blob *blob);

struct ntb_store_cookie *
ntb_store_load_message_content(struct ntb_store *store,
                               uint64_t content_id,
                               ntb_store_load_callback func,
                               void *user_data);

void
ntb_store_delete_message_content(struct ntb_store *store,
                                 uint64_t content_id);

void
ntb_store_for_each_blob(struct ntb_store *store,
                        ntb_store_for_each_blob_func func,
                        void *user_data);

void
ntb_store_for_each_pubkey_blob(struct ntb_store *store,
                               ntb_store_for_each_pubkey_blob_func func,
                               void *user_data);

void
ntb_store_for_each_addr(struct ntb_store *store,
                        ntb_store_for_each_addr_func func,
                        void *user_data);

void
ntb_store_for_each_key(struct ntb_store *store,
                       ntb_store_for_each_key_func func,
                       void *user_data);

void
ntb_store_for_each_outgoing(struct ntb_store *store,
                            ntb_store_for_each_outgoing_func func,
                            void *user_data);

struct ntb_store_cookie *
ntb_store_load_blob(struct ntb_store *store,
                    const uint8_t *hash,
                    ntb_store_load_callback func,
                    void *user_data);

void
ntb_store_cancel_task(struct ntb_store_cookie *cookie);

void
ntb_store_free(struct ntb_store *store);

#endif /* NTB_STORE_H */
