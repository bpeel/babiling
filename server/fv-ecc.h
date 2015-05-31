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

#ifndef FV_ECC_H
#define FV_ECC_H

#include <stdint.h>
#include <stddef.h>
#include <openssl/ripemd.h>
#include <openssl/ec.h>

#include "fv-buffer.h"

#define FV_ECC_PRIVATE_KEY_SIZE 32
#define FV_ECC_PUBLIC_KEY_SIZE 65 /* includes the 0x04 prefix */

struct fv_ecc;

struct fv_ecc *
fv_ecc_new(void);

EC_POINT *
fv_ecc_make_pub_key_point(struct fv_ecc *ecc,
                           const uint8_t *private_key);

void
fv_ecc_make_pub_key_bin(struct fv_ecc *ecc,
                         const uint8_t *private_key,
                         uint8_t *public_key);

EC_KEY *
fv_ecc_create_key(struct fv_ecc *ecc,
                   const uint8_t *private_key);


EC_KEY *
fv_ecc_create_key_with_public(struct fv_ecc *ecc,
                               const uint8_t *private_key,
                               const uint8_t *public_key);

EC_KEY *
fv_ecc_create_random_key(struct fv_ecc *ecc);

void
fv_ecc_get_pub_key(struct fv_ecc *ecc,
                    EC_KEY *key,
                    uint8_t *public_key);

void
fv_ecc_free(struct fv_ecc *ecc);

void
fv_ecc_encrypt_with_point_begin(struct fv_ecc *ecc,
                                 const EC_POINT *public_key,
                                 struct fv_buffer *data_out);

void
fv_ecc_encrypt_update(struct fv_ecc *ecc,
                       const uint8_t *data_in,
                       size_t data_in_length,
                       struct fv_buffer *data_out);

void
fv_ecc_encrypt_end(struct fv_ecc *ecc,
                    struct fv_buffer *data_out);

void
fv_ecc_encrypt_with_point(struct fv_ecc *ecc,
                           const EC_POINT *public_key,
                           const uint8_t *data_in,
                           size_t data_in_length,
                           struct fv_buffer *data_out);

bool
fv_ecc_decrypt(struct fv_ecc *ecc,
                EC_KEY *key,
                const uint8_t *data_in,
                size_t data_in_length,
                struct fv_buffer *data_out);

#endif /* FV_ECC_H */
