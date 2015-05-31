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

#ifndef FV_KEYRING_H
#define FV_KEYRING_H

#include <stdbool.h>
#include <stdint.h>

#include "fv-network.h"
#include "fv-key.h"
#include "fv-address.h"
#include "fv-error.h"

struct fv_keyring;

struct fv_keyring_cookie;

extern struct fv_error_domain
fv_keyring_error;

enum fv_keyring_error {
        FV_KEYRING_ERROR_UNKNOWN_FROM_ADDRESS
};

typedef void
(* fv_keyring_create_key_func)(struct fv_key *key,
                                void *user_data);

struct fv_keyring *
fv_keyring_new(struct fv_network *nw);

void
fv_keyring_start(struct fv_keyring *keyring);

void
fv_keyring_load_store(struct fv_keyring *keyring);

bool
fv_keyring_send_message(struct fv_keyring *keyring,
                         const struct fv_address *from_address,
                         const struct fv_address *to_addresses,
                         int n_to_addresses,
                         int content_encoding,
                         struct fv_blob *content,
                         struct fv_error **error);

struct fv_keyring_cookie *
fv_keyring_create_key(struct fv_keyring *keyring,
                       const struct fv_key_params *params,
                       int leading_zeroes,
                       fv_keyring_create_key_func func,
                       void *user_data);

void
fv_keyring_cancel_task(struct fv_keyring_cookie *cookie);

void
fv_keyring_free(struct fv_keyring *crypto);

#endif /* FV_KEYRING_H */
