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
#include <openssl/obj_mac.h>
#include <openssl/ecdh.h>
#include <assert.h>

#include "fv-key.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-proto.h"

static void
generate_ripe(struct fv_ecc *ecc,
              struct fv_key *key)
{
        SHA512_CTX sha_ctx;
        uint8_t public_key[FV_ECC_PUBLIC_KEY_SIZE];
        uint8_t sha_hash[SHA512_DIGEST_LENGTH];

        SHA512_Init(&sha_ctx);

        fv_ecc_get_pub_key(ecc, key->signing_key, public_key);
        SHA512_Update(&sha_ctx, public_key, FV_ECC_PUBLIC_KEY_SIZE);

        fv_ecc_get_pub_key(ecc, key->encryption_key, public_key);
        SHA512_Update(&sha_ctx, public_key, FV_ECC_PUBLIC_KEY_SIZE);

        SHA512_Final(sha_hash, &sha_ctx);

        RIPEMD160(sha_hash, SHA512_DIGEST_LENGTH, key->address.ripe);
}

struct fv_key *
fv_key_new(struct fv_ecc *ecc,
            const struct fv_key_params *params)
{
        struct fv_key *key = fv_alloc(sizeof *key);
        const uint8_t *public_signing_key;
        const uint8_t *public_encryption_key;
        const uint8_t *private_signing_key;
        const uint8_t *private_encryption_key;

        /* At least one of FV_KEY_PARAM_PRIVATE/PUBLIC_KEYS must be
         * provided */
        assert((params->flags & (FV_KEY_PARAM_PRIVATE_KEYS |
                                 FV_KEY_PARAM_PUBLIC_KEYS)) != 0);

        fv_ref_count_init(&key->ref_count);

        if ((params->flags & FV_KEY_PARAM_LABEL))
                key->label = fv_strdup(params->label);
        else
                key->label = fv_strdup("");

        if ((params->flags & FV_KEY_PARAM_VERSION))
                key->address.version = params->version;
        else
                key->address.version = 4;

        if ((params->flags & FV_KEY_PARAM_STREAM))
                key->address.stream = params->stream;
        else
                key->address.stream = 1;

        if ((params->flags & FV_KEY_PARAM_POW_DIFFICULTY)) {
                key->pow_per_byte = params->pow_per_byte;
                key->pow_extra_bytes = params->pow_extra_bytes;
        } else {
                key->pow_per_byte = FV_PROTO_MIN_POW_PER_BYTE;
                key->pow_extra_bytes = FV_PROTO_MIN_POW_EXTRA_BYTES;
        }

        if ((params->flags & FV_KEY_PARAM_LAST_PUBKEY_SEND_TIME))
                key->last_pubkey_send_time = params->last_pubkey_send_time;
        else
                key->last_pubkey_send_time = 0;

        if ((params->flags & FV_KEY_PARAM_ENABLED))
                key->enabled = params->enabled;
        else
                key->enabled = true;

        if ((params->flags & FV_KEY_PARAM_DECOY))
                key->decoy = params->decoy;
        else
                key->decoy = false;

        if ((params->flags & FV_KEY_PARAM_PRIVATE_KEYS)) {
                private_signing_key = params->private_signing_key;
                private_encryption_key = params->private_encryption_key;
        } else {
                private_signing_key = NULL;
                private_encryption_key = NULL;
        }

        if ((params->flags & FV_KEY_PARAM_PUBLIC_KEYS)) {
                public_signing_key = params->public_signing_key;
                public_encryption_key = params->public_encryption_key;

                key->signing_key =
                        fv_ecc_create_key_with_public(ecc,
                                                       private_signing_key,
                                                       public_signing_key);
                key->encryption_key =
                        fv_ecc_create_key_with_public(ecc,
                                                       private_encryption_key,
                                                       public_encryption_key);
        } else {
                key->signing_key =
                        fv_ecc_create_key(ecc, private_signing_key);
                key->encryption_key =
                        fv_ecc_create_key(ecc, private_encryption_key);
        }

        if (private_encryption_key)
                ECDH_set_method(key->encryption_key, ECDH_OpenSSL());

        if ((params->flags & FV_KEY_PARAM_RIPE)) {
                memcpy(key->address.ripe,
                       params->ripe,
                       RIPEMD160_DIGEST_LENGTH);
        } else {
                generate_ripe(ecc, key);
        }

        fv_address_get_tag(&key->address, key->tag, key->tag_private_key);

        return key;
}

struct fv_key *
fv_key_copy(struct fv_key *key)
{
        key = fv_memdup(key, sizeof *key);

        fv_ref_count_init(&key->ref_count);

        key->label = fv_strdup(key->label);

        key->signing_key = EC_KEY_dup(key->signing_key);
        assert(key->signing_key);
        key->encryption_key = EC_KEY_dup(key->encryption_key);
        assert(key->encryption_key);

        return key;
}

struct fv_key *
fv_key_ref(struct fv_key *key)
{
        fv_ref_count_ref(&key->ref_count);

        return key;
}

void
fv_key_unref(struct fv_key *key)
{
        if (fv_ref_count_unref(&key->ref_count) <= 1) {
                EC_KEY_free(key->signing_key);
                EC_KEY_free(key->encryption_key);
                fv_ref_count_destroy(&key->ref_count);
                fv_free(key->label);
                fv_free(key);
        }
}

bool
fv_key_has_private(struct fv_key *key)
{
        return (EC_KEY_get0_private_key(key->signing_key) &&
                EC_KEY_get0_private_key(key->encryption_key));
}
