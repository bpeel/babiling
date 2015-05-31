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

#ifndef FV_PROTO_H
#define FV_PROTO_H

#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <openssl/sha.h>
#include <openssl/ecdsa.h>

#include "fv-buffer.h"
#include "fv-netaddress.h"

extern struct fv_error_domain
fv_proto_error;

enum fv_proto_error {
        FV_PROTO_ERROR_PROTOCOL
};

enum fv_proto_inv_type {
        FV_PROTO_INV_TYPE_GETPUBKEY,
        FV_PROTO_INV_TYPE_PUBKEY,
        FV_PROTO_INV_TYPE_MSG,
        FV_PROTO_INV_TYPE_BROADCAST
};

enum fv_proto_argument {
        FV_PROTO_ARGUMENT_8,
        FV_PROTO_ARGUMENT_16,
        FV_PROTO_ARGUMENT_32,
        FV_PROTO_ARGUMENT_64,
        FV_PROTO_ARGUMENT_BOOL,
        FV_PROTO_ARGUMENT_VAR_INT,
        FV_PROTO_ARGUMENT_TIMESTAMP,
        FV_PROTO_ARGUMENT_NETADDRESS,
        FV_PROTO_ARGUMENT_VAR_STR,
        FV_PROTO_ARGUMENT_VAR_INT_LIST,
        FV_PROTO_ARGUMENT_END
};

struct fv_proto_var_str {
        uint64_t length;
        const char *data;
};

struct fv_proto_var_int_list {
        uint64_t n_ints;
        const uint8_t *values;
};

struct fv_proto_decrypted_msg {
        const uint8_t *sender_signing_key;
        const uint8_t *sender_encryption_key;
        uint64_t message_version;
        uint64_t sender_address_version;
        uint64_t sender_stream_number;
        uint32_t sender_behaviors;
        uint64_t pow_per_byte;
        uint64_t pow_extra_bytes;
        const uint8_t *destination_ripe;
        uint64_t encoding;
        const uint8_t *message, *ack, *sig;
        uint64_t message_length, ack_length, sig_length;
        size_t signed_data_length;
};

struct fv_proto_pubkey {
        uint64_t nonce;
        int64_t timestamp;

        uint64_t stream;
        uint64_t version;

        uint64_t address_version;
        uint32_t behaviours;

        const uint8_t *public_signing_key;
        const uint8_t *public_encryption_key;

        uint64_t pow_per_byte;
        uint64_t pow_extra_bytes;

        uint64_t signature_length;
        const uint8_t *signature;

        size_t signed_data_length;
        const uint8_t *signed_data;

        const uint8_t *tag;

        size_t encrypted_data_length;
        const uint8_t *encrypted_data;
};

#define FV_PROTO_HEADER_SIZE (4 + 12 + 4 + 4)

#define FV_PROTO_VERSION UINT32_C(2)

#define FV_PROTO_NETWORK_NODE UINT64_C(1)
#define FV_PROTO_SERVICES (FV_PROTO_NETWORK_NODE)

/* The hashes in Bitmessage are an SHA512 digest but only the first 32
 * bytes are used */
#define FV_PROTO_HASH_LENGTH (SHA512_DIGEST_LENGTH / 2)

#define FV_PROTO_MIN_POW_PER_BYTE 320
#define FV_PROTO_MIN_POW_EXTRA_BYTES 14000

/* In addition to the maximum age of an object defined by the
 * protocol, we won't delete objects on disk for this amount of extra
 * time so that we can cope with clocks that are a bit different and
 * won't request objects from peers */
#define FV_PROTO_EXTRA_AGE (6 * 60 * 60 /* 6 hours */)

#define FV_PROTO_DEFAULT_PORT 8444

/* We send acknowledgements */
#define FV_PROTO_PUBKEY_BEHAVIORS UINT32_C(0x00000001)

#define FV_PROTO_ACKDATA_SIZE 32

extern const uint8_t
fv_proto_magic[4];

void
fv_proto_double_hash(const void *data,
                      int length,
                      uint8_t *hash);

void
fv_proto_address_hash(const void *data,
                       int length,
                       uint8_t *hash);

bool
fv_proto_check_command_string(const uint8_t *command_string);

int64_t
fv_proto_get_max_age_for_type(enum fv_proto_inv_type type);

const char *
fv_proto_get_command_name_for_type(enum fv_proto_inv_type type);

static inline uint8_t
fv_proto_get_8(const uint8_t *p)
{
        return *p;
}

uint16_t
fv_proto_get_16(const uint8_t *p);

uint32_t
fv_proto_get_32(const uint8_t *p);

uint64_t
fv_proto_get_64(const uint8_t *p);

bool
fv_proto_get_var_int(const uint8_t **p_ptr,
                      uint32_t *length_ptr,
                      uint64_t *result);

bool
fv_proto_get_timestamp(const uint8_t **p_ptr,
                        uint32_t *length_ptr,
                        int64_t *result);

bool
fv_proto_get_var_str(const uint8_t **p_ptr,
                      uint32_t *length_ptr,
                      struct fv_proto_var_str *result);

bool
fv_proto_get_var_int_list(const uint8_t **p_ptr,
                           uint32_t *length_ptr,
                           struct fv_proto_var_int_list *result);

ssize_t
fv_proto_get_command_va_list(const uint8_t *data,
                              uint32_t length,
                              va_list ap);

ssize_t
fv_proto_get_command(const uint8_t *data,
                      uint32_t length,
                      ...);

bool
fv_proto_get_decrypted_msg(const uint8_t *data,
                            uint32_t length,
                            struct fv_proto_decrypted_msg *msg);

bool
fv_proto_get_pubkey(bool decrypted,
                     const uint8_t *data,
                     uint32_t length,
                     struct fv_proto_pubkey *pubkey);

static inline void
fv_proto_add_8(struct fv_buffer *buf,
                uint8_t value)
{
        fv_buffer_append_c(buf, value);
}

static inline void
fv_proto_add_16(struct fv_buffer *buf,
                 uint16_t value)
{
        value = FV_UINT16_TO_BE(value);
        fv_buffer_append(buf, (uint8_t *) &value, sizeof value);
}

static inline void
fv_proto_add_32(struct fv_buffer *buf,
                 uint32_t value)
{
        value = FV_UINT32_TO_BE(value);
        fv_buffer_append(buf, (uint8_t *) &value, sizeof value);
}

static inline void
fv_proto_add_64(struct fv_buffer *buf,
                 uint64_t value)
{
        value = FV_UINT64_TO_BE(value);
        fv_buffer_append(buf, (uint8_t *) &value, sizeof value);
}

static inline void
fv_proto_add_bool(struct fv_buffer *buf,
                   int value)
{
        fv_proto_add_8(buf, !!value);
}

void
fv_proto_add_var_int(struct fv_buffer *buf,
                      uint64_t value);

void
fv_proto_add_timestamp(struct fv_buffer *buf);

void
fv_proto_add_netaddress(struct fv_buffer *buf,
                         const struct fv_netaddress *address);

void
fv_proto_add_var_str(struct fv_buffer *buf,
                      const char *str);

void
fv_proto_add_public_key(struct fv_buffer *buf,
                         const EC_KEY *key);

void
fv_proto_begin_command(struct fv_buffer *buf,
                        const char *command);

void
fv_proto_end_command(struct fv_buffer *buf,
                      size_t command_start);

void
fv_proto_add_command(struct fv_buffer *buf,
                      const char *command,
                      ...);

void
fv_proto_add_command_va_list(struct fv_buffer *buf,
                              const char *command,
                              va_list ap);

#endif /* FV_PROTO_H */
