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

#ifndef FV_CRYPTO_H
#define FV_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>

#include "fv-error.h"
#include "fv-key.h"
#include "fv-blob.h"
#include "fv-address.h"

struct fv_crypto;

struct fv_crypto_cookie;

typedef void
(* fv_crypto_create_key_func)(struct fv_key *key,
                               void *user_data);

typedef void
(* fv_crypto_create_pubkey_blob_func)(struct fv_blob *blob,
                                       void *user_data);

typedef void
(* fv_crypto_create_msg_blob_func)(struct fv_blob *blob,
                                    void *user_data);

/* If the decryption failed, the key and blob will be NULL. The blob
 * will have the msg type but it isn't a real msg and instead it
 * contains the decrypted data */
typedef void
(* fv_crypto_decrypt_msg_func)(struct fv_key *key,
                                struct fv_blob *blob,
                                void *user_data);

typedef void
(* fv_crypto_generate_ackdata_func)(const uint8_t *ackdata,
                                     void *user_data);


struct fv_crypto *
fv_crypto_new(void);

/* Creates a new private key. The key parameters must not be given
 * because they will be generated */
struct fv_crypto_cookie *
fv_crypto_create_key(struct fv_crypto *crypto,
                      const struct fv_key_params *params,
                      int leading_zeroes,
                      fv_crypto_create_key_func callback,
                      void *user_data);

struct fv_crypto_cookie *
fv_crypto_create_pubkey_blob(struct fv_crypto *crypto,
                              struct fv_key *key,
                              fv_crypto_create_pubkey_blob_func callback,
                              void *user_data);

struct fv_crypto_cookie *
fv_crypto_create_msg_blob(struct fv_crypto *crypto,
                           int64_t timestamp,
                           struct fv_key *from_key,
                           struct fv_key *to_key,
                           struct fv_blob *content,
                           fv_crypto_create_msg_blob_func callback,
                           void *user_data);

/* The private keys must not be given but the public keys must */
struct fv_crypto_cookie *
fv_crypto_create_public_key(struct fv_crypto *crypto,
                             const struct fv_key_params *params,
                             fv_crypto_create_key_func callback,
                             void *user_data);

struct fv_crypto_cookie *
fv_crypto_check_pubkey(struct fv_crypto *crypto,
                        const struct fv_address *address,
                        struct fv_blob *blob,
                        fv_crypto_create_key_func callback,
                        void *user_data);

struct fv_crypto_cookie *
fv_crypto_decrypt_msg(struct fv_crypto *crypto,
                       struct fv_blob *msg,
                       struct fv_key * const *keys,
                       int n_keys,
                       fv_crypto_decrypt_msg_func callback,
                       void *user_data);

struct fv_crypto_cookie *
fv_crypto_generate_ackdata(struct fv_crypto *crypto,
                            fv_crypto_generate_ackdata_func callback,
                            void *user_data);

void
fv_crypto_cancel_task(struct fv_crypto_cookie *cookie);

void
fv_crypto_free(struct fv_crypto *crypto);

#endif /* FV_CRYPTO_H */
