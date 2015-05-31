/*
 * Notbit - A Bitmessage client
 * Copyright (C) 2013, 2014  Neil Roberts
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

#include <stdint.h>
#include <stdbool.h>
#include <openssl/ripemd.h>
#include <openssl/bn.h>
#include <string.h>
#include <limits.h>
#include <errno.h>

#include "fv-load-keys.h"
#include "fv-key-value.h"
#include "fv-buffer.h"
#include "fv-proto.h"
#include "fv-ecc.h"
#include "fv-log.h"
#include "fv-util.h"
#include "fv-base58.h"
#include "fv-address.h"

struct fv_load_keys_data {
        struct fv_ecc *ecc;

        fv_load_keys_func func;
        void *user_data;

        struct fv_buffer label;
        struct fv_buffer address;

        int pow_per_byte;
        int pow_extra_bytes;
        int64_t last_pubkey_send_time;
        bool enabled;
        bool decoy;
        bool has_private_signing_key;
        bool has_private_encryption_key;
        bool has_public_signing_key;
        bool has_public_encryption_key;

        uint8_t private_signing_key[FV_ECC_PRIVATE_KEY_SIZE];
        uint8_t private_encryption_key[FV_ECC_PRIVATE_KEY_SIZE];
        uint8_t public_signing_key[FV_ECC_PUBLIC_KEY_SIZE];
        uint8_t public_encryption_key[FV_ECC_PUBLIC_KEY_SIZE];
};

static void
reset_data(struct fv_load_keys_data *data)
{
        data->label.length = 0;
        data->address.length = 0;
        data->pow_per_byte = FV_PROTO_MIN_POW_PER_BYTE;
        data->pow_extra_bytes = FV_PROTO_MIN_POW_EXTRA_BYTES;
        data->last_pubkey_send_time = 0;
        data->enabled = true;
        data->decoy = false;

        data->has_private_signing_key = false;
        data->has_private_encryption_key = false;
        data->has_public_signing_key = false;
        data->has_public_encryption_key = false;
}

static void
flush_key(struct fv_load_keys_data *data)
{
        struct fv_key *key;
        struct fv_address address;
        struct fv_key_params params;
        bool has_private_keys;

        has_private_keys = (data->has_private_signing_key &&
                            data->has_private_encryption_key);

        if (!has_private_keys &&
            (!data->has_public_signing_key ||
             !data->has_public_encryption_key)) {
                reset_data(data);
                return;
        }

        if (!fv_address_decode(&address,
                                (const char *) data->address.data)) {
                fv_log("Address is invalid “%s”",
                        (const char *) data->address.data);
                address.version = 4;
                address.stream = 1;
        }

        params.flags = (FV_KEY_PARAM_LABEL |
                        FV_KEY_PARAM_VERSION |
                        FV_KEY_PARAM_STREAM |
                        FV_KEY_PARAM_POW_DIFFICULTY |
                        FV_KEY_PARAM_LAST_PUBKEY_SEND_TIME |
                        FV_KEY_PARAM_ENABLED |
                        FV_KEY_PARAM_DECOY);
        params.label = (const char *) data->label.data;
        params.version = address.version;
        params.stream = address.stream;
        params.pow_per_byte = data->pow_per_byte;
        params.pow_extra_bytes = data->pow_extra_bytes;
        params.last_pubkey_send_time = data->last_pubkey_send_time;
        params.enabled = data->enabled;
        params.decoy = data->decoy;

        if (has_private_keys) {
                params.flags |= FV_KEY_PARAM_PRIVATE_KEYS;
                params.private_signing_key = data->private_signing_key;
                params.private_encryption_key = data->private_encryption_key;
        } else {
                params.flags |= FV_KEY_PARAM_PUBLIC_KEYS;
                params.public_signing_key = data->public_signing_key;
                params.public_encryption_key = data->public_encryption_key;
        }

        key = fv_key_new(data->ecc, &params);

        if (memcmp(key->address.ripe, address.ripe, RIPEMD160_DIGEST_LENGTH)) {
                fv_log("Calculated address for %s does not match",
                        (const char *) data->address.data);
        }

        data->func(key, data->user_data);

        fv_key_unref(key);

        reset_data(data);
}

static bool
parse_wif(struct fv_load_keys_data *data,
          int line_number,
          const char *value,
          uint8_t *result)
{
        uint8_t key_buf[1 + FV_ECC_PRIVATE_KEY_SIZE + 4];
        uint8_t hash1[SHA256_DIGEST_LENGTH];
        uint8_t hash2[SHA256_DIGEST_LENGTH];
        ssize_t key_length;

        key_length = fv_base58_decode(value,
                                       strlen(value),
                                       key_buf,
                                       sizeof key_buf);

        if (key_length == -1) {
                fv_log("Invalid private key on line %i",
                        line_number);
                return false;
        }

        memmove(key_buf + sizeof key_buf - key_length, key_buf, key_length);
        memset(key_buf, 0, sizeof key_buf - key_length);

        if (key_buf[0] != 0x80) {
                fv_log("Private key on line %i does not have the right prefix",
                        line_number);
                return false;
        }

        SHA256(key_buf, FV_ECC_PRIVATE_KEY_SIZE + 1, hash1);
        SHA256(hash1, SHA256_DIGEST_LENGTH, hash2);

        if (memcmp(key_buf + 1 + FV_ECC_PRIVATE_KEY_SIZE, hash2, 4)) {
                fv_log("Checksum does not match for private key on line %i",
                        line_number);
                return false;
        }

        memcpy(result, key_buf + 1, FV_ECC_PRIVATE_KEY_SIZE);

        return true;
}

static bool
parse_public_key(struct fv_load_keys_data *data,
                 int line_number,
                 const char *value,
                 uint8_t *result)
{
        ssize_t key_length;

        key_length = fv_base58_decode(value,
                                       strlen(value),
                                       result,
                                       FV_ECC_PUBLIC_KEY_SIZE);

        if (key_length != FV_ECC_PUBLIC_KEY_SIZE) {
                fv_log("Invalid public key on line %i",
                        line_number);
                return false;
        }

        if (result[0] != 0x04) {
                fv_log("Public key on line %i does not have the right prefix",
                        line_number);
                return false;
        }

        return true;
}

static void
process_property(struct fv_load_keys_data *data,
                 int line_number,
                 const char *key,
                 const char *value)
{
        int64_t int_value;
        bool bool_value;

        if (!strcmp(key, "label")) {
                fv_buffer_set_length(&data->label, 0);
                fv_buffer_append_string(&data->label, value);
        } else if (!strcmp(key, "enabled")) {
                if (fv_key_value_parse_bool_value(line_number,
                                                   value,
                                                   &bool_value))
                        data->enabled = bool_value;
        } else if (!strcmp(key, "decoy")) {
                if (fv_key_value_parse_bool_value(line_number,
                                                   value,
                                                   &bool_value))
                        data->decoy = bool_value;
        } else if (!strcmp(key, "noncetrialsperbyte")) {
                if (fv_key_value_parse_int_value(line_number,
                                                  value,
                                                  INT_MAX,
                                                  &int_value))
                        data->pow_per_byte = int_value;
        } else if (!strcmp(key, "payloadlengthextrabytes")) {
                if (fv_key_value_parse_int_value(line_number,
                                                  value,
                                                  INT_MAX,
                                                  &int_value))
                        data->pow_extra_bytes = int_value;
        } else if (!strcmp(key, "privsigningkey")) {
                if (parse_wif(data,
                              line_number,
                              value,
                              data->private_signing_key))
                        data->has_private_signing_key = true;
        } else if (!strcmp(key, "privencryptionkey")) {
                if (parse_wif(data,
                              line_number,
                              value,
                              data->private_encryption_key))
                        data->has_private_encryption_key = true;
        } else if (!strcmp(key, "pubsigningkey")) {
                if (parse_public_key(data,
                                     line_number,
                                     value,
                                     data->public_signing_key))
                        data->has_public_signing_key = true;
        } else if (!strcmp(key, "pubencryptionkey")) {
                if (parse_public_key(data,
                                     line_number,
                                     value,
                                     data->public_encryption_key))
                        data->has_public_encryption_key = true;
        } else if (!strcmp(key, "lastpubkeysendtime")) {
                if (fv_key_value_parse_int_value(line_number,
                                                  value,
                                                  INT64_MAX,
                                                  &int_value))
                        data->last_pubkey_send_time = int_value;
        }
}

static void
key_value_event_cb(enum fv_key_value_event event,
                   int line_number,
                   const char *key,
                   const char *value,
                   void *user_data)
{
        struct fv_load_keys_data *data = user_data;

        switch (event) {
        case FV_KEY_VALUE_EVENT_HEADER:
                flush_key(data);
                data->address.length = 0;
                fv_buffer_append_string(&data->address, value);
                break;

        case FV_KEY_VALUE_EVENT_PROPERTY:
                process_property(data, line_number, key, value);
                break;
        }
}

void
fv_load_keys(FILE *file,
              fv_load_keys_func func,
              void *user_data)
{
        struct fv_load_keys_data data;

        fv_log("Loading keys");

        data.ecc = fv_ecc_new();

        fv_buffer_init(&data.label);
        fv_buffer_init(&data.address);

        data.func = func;
        data.user_data = user_data;

        reset_data(&data);

        fv_key_value_load(file, key_value_event_cb, &data);

        flush_key(&data);

        fv_buffer_destroy(&data.label);
        fv_buffer_destroy(&data.address);

        fv_ecc_free(data.ecc);
}
