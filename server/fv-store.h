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

#ifndef FV_STORE_H
#define FV_STORE_H

#include <stdint.h>

#include "fv-blob.h"
#include "fv-error.h"
#include "fv-netaddress.h"
#include "fv-key.h"
#include "fv-proto.h"

/* The store is used to do all of the disk I/O. The actions are stored
 * in a queue and then executed in a separate thread */

struct fv_store;

extern struct fv_error_domain
fv_store_error;

enum fv_store_error {
        FV_STORE_ERROR_INVALID_STORE_DIRECTORY,
        FV_STORE_ERROR_INVALID_MAILDIR
};

struct fv_store_cookie;

struct fv_store_addr {
        int64_t timestamp;
        uint32_t stream;
        uint64_t services;
        struct fv_netaddress address;
};

struct fv_store_outgoing {
        struct fv_address from_address;
        struct fv_address to_address;
        uint8_t ackdata[FV_PROTO_ACKDATA_SIZE];
        uint64_t content_id;
        int content_encoding;
        int64_t last_getpubkey_send_time;
        int64_t last_msg_send_time;
};

typedef void
(* fv_store_for_each_blob_func)(enum fv_proto_inv_type type,
                                 const uint8_t *hash,
                                 int64_t timestamp,
                                 void *user_data);

typedef void
(* fv_store_for_each_pubkey_blob_func)(const uint8_t *hash,
                                        int64_t timestamp,
                                        struct fv_blob *blob,
                                        void *user_data);

typedef void
(* fv_store_for_each_addr_func)(const struct fv_store_addr *addr,
                                 void *user_data);

typedef void
(* fv_store_for_each_key_func)(struct fv_key *key,
                                void *user_data);

typedef void
(* fv_store_for_each_outgoing_func)(const struct fv_store_outgoing *outgoing,
                                     void *user_data);

/* This is called when a load is complete. If the load succeeded then
 * blob will point to the contents. If it failed the callback will
 * still be called but blob will be NULL. The callback won't be called
 * at all if the task is cancelled. The callback will always be
 * invoked from an idle handler in the main thread */
typedef void (* fv_store_load_callback)(struct fv_blob *blob,
                                         void *user_data);

struct fv_store *
fv_store_new(const char *store_directory,
              const char *maildir,
              struct fv_error **error);

const char *
fv_store_get_directory(struct fv_store *store);

void
fv_store_start(struct fv_store *store);

struct fv_store *
fv_store_get_default(void);

void
fv_store_set_default(struct fv_store *store);

void
fv_store_save_blob(struct fv_store *store,
                    const uint8_t *hash,
                    struct fv_blob *blob);

void
fv_store_delete_object(struct fv_store *store,
                        const uint8_t *hash);

void
fv_store_save_addr_list(struct fv_store *store,
                         struct fv_store_addr *addrs,
                         int n_addrs);

void
fv_store_save_keys(struct fv_store *store,
                    struct fv_key * const *keys,
                    int n_keys);

void
fv_store_save_outgoings(struct fv_store *store,
                         struct fv_blob *blob);

void
fv_store_save_message(struct fv_store *store,
                       int64_t timestamp,
                       struct fv_key *from_key,
                       const char *from_address,
                       struct fv_key *to_key,
                       struct fv_blob *blob);

void
fv_store_save_message_content(struct fv_store *store,
                               uint64_t content_id,
                               struct fv_blob *blob);

struct fv_store_cookie *
fv_store_load_message_content(struct fv_store *store,
                               uint64_t content_id,
                               fv_store_load_callback func,
                               void *user_data);

void
fv_store_delete_message_content(struct fv_store *store,
                                 uint64_t content_id);

void
fv_store_for_each_blob(struct fv_store *store,
                        fv_store_for_each_blob_func func,
                        void *user_data);

void
fv_store_for_each_pubkey_blob(struct fv_store *store,
                               fv_store_for_each_pubkey_blob_func func,
                               void *user_data);

void
fv_store_for_each_addr(struct fv_store *store,
                        fv_store_for_each_addr_func func,
                        void *user_data);

void
fv_store_for_each_key(struct fv_store *store,
                       fv_store_for_each_key_func func,
                       void *user_data);

void
fv_store_for_each_outgoing(struct fv_store *store,
                            fv_store_for_each_outgoing_func func,
                            void *user_data);

struct fv_store_cookie *
fv_store_load_blob(struct fv_store *store,
                    const uint8_t *hash,
                    fv_store_load_callback func,
                    void *user_data);

void
fv_store_cancel_task(struct fv_store_cookie *cookie);

void
fv_store_free(struct fv_store *store);

#endif /* FV_STORE_H */
