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

#ifndef FV_NETWORK_H
#define FV_NETWORK_H

#include <stdbool.h>

#include "fv-error.h"
#include "fv-signal.h"
#include "fv-blob.h"
#include "fv-netaddress.h"

extern struct fv_error_domain
fv_network_error;

enum fv_network_error {
        FV_NETWORK_ERROR_INVALID_ADDRESS
};

enum fv_network_add_object_flags {
        FV_NETWORK_SKIP_VALIDATION = (1 << 0),
        FV_NETWORK_DELAY = (1 << 1)
};

enum fv_network_object_location {
        FV_NETWORK_OBJECT_LOCATION_NOWHERE,
        FV_NETWORK_OBJECT_LOCATION_STORE,
        FV_NETWORK_OBJECT_LOCATION_MEMORY
};

struct fv_network;

struct fv_network *
fv_network_new(bool add_default_nodes);

void
fv_network_add_object_from_data(struct fv_network *nw,
                                 enum fv_proto_inv_type type,
                                 const uint8_t *object_data,
                                 size_t object_data_length,
                                 enum fv_network_add_object_flags flags,
                                 const char *source_note);

void
fv_network_add_blob(struct fv_network *nw,
                     struct fv_blob *blob,
                     enum fv_network_add_object_flags flags,
                     const char *source_note);

void
fv_network_load_store(struct fv_network *nw, bool bootstrap);

bool
fv_network_add_listen_address(struct fv_network *nw,
                               const char *address,
                               struct fv_error **error);

bool
fv_network_add_peer_address(struct fv_network *nw,
                             const char *address,
                             struct fv_error **error);

struct fv_signal *
fv_network_get_new_object_signal(struct fv_network *nw);

void
fv_network_set_only_use_explicit_addresses(struct fv_network *nw,
                                            bool value);

void
fv_network_set_allow_private_addresses(struct fv_network *nw,
                                        bool value);

void
fv_network_set_proxy_address(struct fv_network *nw,
                              const struct fv_netaddress *addr);

enum fv_network_object_location
fv_network_get_object(struct fv_network *nw,
                       const uint8_t *hash,
                       struct fv_blob **blob);

void
fv_network_free(struct fv_network *nw);

#endif /* FV_NETWORK_H */
