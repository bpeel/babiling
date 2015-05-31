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

#include "config.h"

#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>

#include "fv-keyring.h"
#include "fv-util.h"
#include "fv-main-context.h"
#include "fv-log.h"
#include "fv-crypto.h"
#include "fv-key.h"
#include "fv-list.h"
#include "fv-store.h"
#include "fv-signal.h"
#include "fv-proto.h"
#include "fv-buffer.h"
#include "fv-pow.h"
#include "fv-pointer-array.h"
#include "fv-address.h"
#include "fv-slice.h"
#include "fv-hash-table.h"

struct fv_keyring {
        struct fv_network *nw;
        struct fv_crypto *crypto;
        struct fv_pow *pow;
        struct fv_buffer keys;
        struct fv_list tasks;
        struct fv_listener new_object_listener;

        bool started;

        /* Hash table of pubkey blobs indexed by either the ripe
         * (for v2/3 keys) or the tag (v4 keys) */
        struct fv_hash_table *pubkey_blob_table;
        /* Pubkey blobs with the same tag or ripe are grouped together
         * within this list. The hash table entry points to the first
         * entry in the group */
        struct fv_list pubkey_blob_list;

        struct fv_main_context_source *gc_source;
        struct fv_main_context_source *resend_source;

        /* The message contents are given a unique id using this
         * counter. The ID is used for the filename in the store */
        uint64_t next_message_content_id;

        struct fv_list messages;
};

struct fv_keyring_cookie {
        struct fv_keyring *keyring;
        fv_keyring_create_key_func func;
        void *user_data;
        struct fv_crypto_cookie *crypto_cookie;
};

struct fv_keyring_task {
        struct fv_keyring *keyring;
        struct fv_crypto_cookie *crypto_cookie;
        struct fv_pow_cookie *pow_cookie;
        struct fv_blob *blob;
        struct fv_list link;

        union {
                struct {
                        int64_t timestamp;
                } msg;
        };
};

enum fv_keyring_message_state {
        FV_KEYRING_MESSAGE_STATE_GENERATING_ACKDATA,
        FV_KEYRING_MESSAGE_STATE_LOADING_PUBKEY_FROM_STORE,
        FV_KEYRING_MESSAGE_STATE_TRYING_BLOB,
        FV_KEYRING_MESSAGE_STATE_CALCULATING_GETPUBKEY_POW,
        FV_KEYRING_MESSAGE_STATE_AWAITING_PUBKEY,
        FV_KEYRING_MESSAGE_STATE_LOADING_CONTENT,
        FV_KEYRING_MESSAGE_STATE_CALCULATING_ACKDATA_POW,
        FV_KEYRING_MESSAGE_STATE_CREATE_MSG_BLOB,
        FV_KEYRING_MESSAGE_STATE_CALCULATING_MSG_POW,
        FV_KEYRING_MESSAGE_STATE_AWAITING_ACKNOWLEDGEMENT
};

struct fv_keyring_message {
        struct fv_keyring *keyring;

        enum fv_keyring_message_state state;

        struct fv_key *from_key;
        struct fv_address to_address;
        uint8_t ripe_or_tag[FV_PROTO_HASH_LENGTH];
        struct fv_key *to_key;

        uint64_t content_id;
        int content_encoding;

        uint8_t ackdata[FV_PROTO_ACKDATA_SIZE];

        struct fv_crypto_cookie *crypto_cookie;
        struct fv_pow_cookie *pow_cookie;
        struct fv_store_cookie *store_cookie;
        struct fv_blob *blob;

        size_t blob_ackdata_offset;
        uint32_t blob_ackdata_length;

        int64_t last_getpubkey_send_time;
        int64_t last_msg_send_time;

        /* pubkey that we are current trying. This is only set when
         * the state is FV_KEYRING_MESSAGE_STATE_TRYING_PUBKEY */
        struct fv_keyring_pubkey_blob *trying_pubkey_blob;

        struct fv_list link;
};

struct fv_keyring_pubkey_blob {
        /* This struct is used to index the pubkey objects by either
         * the ripe or the tag so that when we want to use a new
         * public key we can first check if it's already in the
         * network */

        struct fv_list link;
        int64_t timestamp;
        uint8_t ripe_or_tag[FV_PROTO_HASH_LENGTH];
        uint8_t hash[FV_PROTO_HASH_LENGTH];
        int ref_count;
        bool in_list;
};

FV_STATIC_ASSERT(RIPEMD160_DIGEST_LENGTH <= FV_PROTO_HASH_LENGTH,
                  "The ripe is too long to fit in a hash");
FV_STATIC_ASSERT(FV_ADDRESS_TAG_SIZE <= FV_PROTO_HASH_LENGTH,
                  "The tag is too long to fit in a hash");

/* Time in minutes between each garbage collection run */
#define FV_KEYRING_GC_TIMEOUT 10

/* Time in minutes before checking whether to resend a message */
#define FV_KEYRING_RESEND_TIMEOUT 60

FV_SLICE_ALLOCATOR(struct fv_keyring_pubkey_blob,
                    fv_keyring_pubkey_blob_allocator);

struct fv_error_domain
fv_keyring_error;

static void
load_public_key_for_message(struct fv_keyring_message *message);

static void
post_message(struct fv_keyring_message *message);

static void
send_getpubkey_request(struct fv_keyring_message *message);

static bool
try_pubkey_blob_for_message(struct fv_keyring_message *message,
                            struct fv_keyring_pubkey_blob *pubkey_blob);

static struct fv_keyring_message *
create_message(struct fv_keyring *keyring,
               struct fv_key *from_key,
               const struct fv_address *to_address,
               int content_encoding,
               uint64_t content_id);

static void
unref_pubkey_blob(struct fv_keyring_pubkey_blob *pubkey)
{
        if (--pubkey->ref_count <= 0)
                fv_slice_free(&fv_keyring_pubkey_blob_allocator, pubkey);
}

static void
cancel_message_tasks(struct fv_keyring_message *message)
{
        if (message->crypto_cookie) {
                fv_crypto_cancel_task(message->crypto_cookie);
                message->crypto_cookie = NULL;
        }

        if (message->pow_cookie) {
                fv_pow_cancel(message->pow_cookie);
                message->pow_cookie = NULL;
        }

        if (message->store_cookie) {
                fv_store_cancel_task(message->store_cookie);
                message->store_cookie = NULL;
        }

        if (message->blob) {
                fv_blob_unref(message->blob);
                message->blob = NULL;
        }

        if (message->trying_pubkey_blob) {
                unref_pubkey_blob(message->trying_pubkey_blob);
                message->trying_pubkey_blob = NULL;
        }
}

static void
free_message(struct fv_keyring_message *message)
{
        fv_key_unref(message->from_key);

        if (message->to_key)
                fv_key_unref(message->to_key);

        cancel_message_tasks(message);

        fv_list_remove(&message->link);

        fv_free(message);
}

static void
maybe_delete_message_content(struct fv_keyring *keyring,
                             uint64_t content_id)
{
        struct fv_keyring_message *message;

        /* Check if any messages are still using this content */
        fv_list_for_each(message, &keyring->messages, link) {
                if (message->content_id == content_id)
                        return;
        }

        fv_store_delete_message_content(NULL, content_id);
}

static void
save_keyring(struct fv_keyring *keyring)
{
        fv_store_save_keys(NULL /* default store */,
                            (struct fv_key **) keyring->keys.data,
                            fv_pointer_array_length(&keyring->keys));
}

static void
add_outgoing(struct fv_keyring_message *message,
             struct fv_buffer *buffer)
{
        struct fv_store_outgoing *outgoing;
        size_t old_length = buffer->length;

        fv_buffer_set_length(buffer,
                              buffer->length +
                              sizeof (struct fv_store_outgoing));
        outgoing = (struct fv_store_outgoing *) (buffer->data + old_length);

        outgoing->from_address = message->from_key->address;
        outgoing->to_address = message->to_address;
        memcpy(outgoing->ackdata, message->ackdata, FV_PROTO_ACKDATA_SIZE);
        outgoing->content_id = message->content_id;
        outgoing->content_encoding = message->content_encoding;
        outgoing->last_getpubkey_send_time = message->last_getpubkey_send_time;
        outgoing->last_msg_send_time = message->last_msg_send_time;

        /* If we are in the middle of calculating the POW then the
         * send time will have been updated but we won't have actually
         * sent the object yet. Therefore we'll reset the last send
         * time so that when we restart it will try resending
         * immediately */
        switch (message->state) {
        case FV_KEYRING_MESSAGE_STATE_CALCULATING_GETPUBKEY_POW:
                outgoing->last_getpubkey_send_time = 0;
                break;

        case FV_KEYRING_MESSAGE_STATE_CALCULATING_ACKDATA_POW:
        case FV_KEYRING_MESSAGE_STATE_CALCULATING_MSG_POW:
                outgoing->last_msg_send_time = 0;
                break;

        default:
                break;
        }
}

static void
save_messages(struct fv_keyring *keyring)
{
        struct fv_buffer buffer;
        struct fv_keyring_message *message;
        struct fv_blob *blob;

        fv_blob_dynamic_init(&buffer, FV_PROTO_INV_TYPE_MSG);

        fv_list_for_each(message, &keyring->messages, link) {
                if (message->state !=
                    FV_KEYRING_MESSAGE_STATE_GENERATING_ACKDATA)
                        add_outgoing(message, &buffer);
        }

        blob = fv_blob_dynamic_end(&buffer);

        fv_store_save_outgoings(NULL /* default store */, blob);

        fv_blob_unref(blob);
}

static void
add_key(struct fv_keyring *keyring,
        struct fv_key *key)
{
        fv_pointer_array_append(&keyring->keys, fv_key_ref(key));
}

static void
for_each_key_cb(struct fv_key *key,
                void *user_data)
{
        struct fv_keyring *keyring = user_data;

        add_key(keyring, key);
}

static struct fv_keyring_task *
add_task(struct fv_keyring *keyring)
{
        struct fv_keyring_task *task = fv_alloc(sizeof *task);

        task->keyring = keyring;
        task->crypto_cookie = NULL;
        task->pow_cookie = NULL;
        task->blob = NULL;

        fv_list_insert(&keyring->tasks, &task->link);

        return task;
}

static void
free_task(struct fv_keyring_task *task)
{
        if (task->crypto_cookie)
                fv_crypto_cancel_task(task->crypto_cookie);
        if (task->pow_cookie)
                fv_pow_cancel(task->pow_cookie);
        if (task->blob)
                fv_blob_unref(task->blob);
        fv_list_remove(&task->link);
        fv_free(task);
}

static void
create_pubkey_pow_cb(uint64_t nonce,
                     void *user_data)
{
        struct fv_keyring_task *task = user_data;
        struct fv_keyring *keyring = task->keyring;

        fv_log("Finished calculating proof-of-work for pubkey command. "
                "Nonce is %" PRIu64,
                nonce);

        nonce = FV_UINT64_TO_BE(nonce);

        memcpy(task->blob->data, &nonce, sizeof nonce);

        fv_network_add_blob(keyring->nw,
                             task->blob,
                             FV_NETWORK_DELAY |
                             FV_NETWORK_SKIP_VALIDATION,
                             "pubkey response");

        task->pow_cookie = NULL;
        free_task(task);
}

static void
create_pubkey_blob_cb(struct fv_blob *blob,
                      void *user_data)
{
        struct fv_keyring_task *task = user_data;
        struct fv_keyring *keyring = task->keyring;

        fv_log("Doing proof-of-work calculation to send pubkey command");

        task->crypto_cookie = NULL;
        task->pow_cookie =
                fv_pow_calculate(keyring->pow,
                                  blob->data + sizeof (uint64_t),
                                  blob->size - sizeof (uint64_t),
                                  FV_PROTO_MIN_POW_PER_BYTE,
                                  FV_PROTO_MIN_POW_EXTRA_BYTES,
                                  create_pubkey_pow_cb,
                                  task);
        task->blob = fv_blob_ref(blob);
}

static void
maybe_post_key(struct fv_keyring *keyring,
               int key_index)
{
        struct fv_key *key = fv_pointer_array_get(&keyring->keys, key_index);
        struct fv_key *tmp_key;
        int64_t now, last_send_age;
        struct fv_keyring_task *task;

        now = fv_main_context_get_wall_clock(NULL);

        last_send_age = now - key->last_pubkey_send_time;

        if (last_send_age <
            fv_proto_get_max_age_for_type(FV_PROTO_INV_TYPE_PUBKEY)) {
                fv_log("Ignoring getpubkey command for key that was broadcast "
                        "%" PRIi64 " seconds ago because it should still be in "
                        "the network",
                        last_send_age);
                return;
        }

        /* Update the last send age now so that a peer can't keep us
         * busy by queueing loads of getpubkey requests before we've
         * had a chance to notice that we've already started one
         * request. The keys are immutable so we need to use a copy.
         * The timestamp is obsucated with a random number so that a
         * peer can't tell how quickly we responded */
        tmp_key = fv_key_copy(key);
        tmp_key->last_pubkey_send_time = now + rand() % 600 - 300;
        fv_key_unref(key);
        fv_pointer_array_set(&keyring->keys, key_index, tmp_key);

        save_keyring(keyring);

        fv_log("Generating pubkey command for the key \"%s\"",
                tmp_key->label);

        task = add_task(keyring);
        task->crypto_cookie =
                fv_crypto_create_pubkey_blob(keyring->crypto,
                                              tmp_key,
                                              create_pubkey_blob_cb,
                                              task);
}

static void
handle_getpubkey_with_ripe(struct fv_keyring *keyring,
                           uint64_t address_version,
                           uint64_t stream_number,
                           const uint8_t *ripe)
{
        struct fv_key *key;
        int i;

        for (i = 0; i < fv_pointer_array_length(&keyring->keys); i++) {
                key = fv_pointer_array_get(&keyring->keys, i);

                if (!fv_key_has_private(key))
                        continue;

                if (!memcmp(key->address.ripe, ripe, RIPEMD160_DIGEST_LENGTH)) {
                        if (key->address.version != address_version ||
                            key->address.stream != stream_number) {
                                fv_log("getpubkey requested for key with the "
                                        "wrong version or stream number");
                        } else {
                                maybe_post_key(keyring, i);
                        }
                        break;
                }
        }
}

static void
handle_getpubkey_with_tag(struct fv_keyring *keyring,
                          uint64_t address_version,
                          uint64_t stream_number,
                          const uint8_t *tag)
{
        struct fv_key *key;
        int i;

        for (i = 0; i < fv_pointer_array_length(&keyring->keys); i++) {
                key = fv_pointer_array_get(&keyring->keys, i);

                if (!fv_key_has_private(key))
                        continue;

                if (!memcmp(key->tag, tag, FV_ADDRESS_TAG_SIZE)) {
                        if (key->address.version != address_version ||
                            key->address.stream != stream_number) {
                                fv_log("getpubkey requested for key with the "
                                        "wrong version or stream number");
                        } else {
                                maybe_post_key(keyring, i);
                        }
                        break;
                }
        }
}

static void
handle_getpubkey(struct fv_keyring *keyring,
                 struct fv_blob *blob)
{
        const uint8_t *ripe_or_tag;
        uint64_t nonce;
        int64_t timestamp;
        uint64_t address_version;
        ssize_t header_length;
        uint64_t stream_number;

        header_length = fv_proto_get_command(blob->data,
                                              blob->size,

                                              FV_PROTO_ARGUMENT_64,
                                              &nonce,

                                              FV_PROTO_ARGUMENT_TIMESTAMP,
                                              &timestamp,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &address_version,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &stream_number,

                                              FV_PROTO_ARGUMENT_END);

        if (header_length == -1) {
                fv_log("Invalid getpubkey message received");
                return;
        }

        if (address_version < 2 || address_version > 4) {
                fv_log("getpubkey with unsupported address version "
                        "%" PRIu64 " received",
                        address_version);
                return;
        }

        ripe_or_tag = blob->data + header_length;

        if (address_version < 4) {
                if (blob->size - header_length < RIPEMD160_DIGEST_LENGTH) {
                        fv_log("Invalid getpubkey message received");
                        return;
                }

                handle_getpubkey_with_ripe(keyring,
                                           address_version,
                                           stream_number,
                                           ripe_or_tag);
        } else {
                if (blob->size - header_length < FV_ADDRESS_TAG_SIZE) {
                        fv_log("Invalid getpubkey message received");
                        return;
                }

                handle_getpubkey_with_tag(keyring,
                                          address_version,
                                          stream_number,
                                          ripe_or_tag);
        }
}

static void
check_pubkey_blob_with_messages(struct fv_keyring *keyring,
                                struct fv_keyring_pubkey_blob *pubkey_blob)
{
        struct fv_keyring_message *message;

        fv_list_for_each(message, &keyring->messages, link) {
                if ((message->state ==
                     FV_KEYRING_MESSAGE_STATE_CALCULATING_GETPUBKEY_POW ||
                     message->state ==
                     FV_KEYRING_MESSAGE_STATE_AWAITING_PUBKEY) &&
                    !memcmp(message->ripe_or_tag,
                            pubkey_blob->ripe_or_tag,
                            FV_PROTO_HASH_LENGTH)) {
                        cancel_message_tasks(message);
                        try_pubkey_blob_for_message(message, pubkey_blob);
                }
        }
}

static void
handle_pubkey(struct fv_keyring *keyring,
              struct fv_blob *blob)
{
        struct fv_proto_pubkey pubkey;
        struct fv_keyring_pubkey_blob *pubkey_blob;
        struct fv_keyring_pubkey_blob *insert_pos;
        struct fv_address address;

        if (!fv_proto_get_pubkey(false, /* not decrypted */
                                  blob->data,
                                  blob->size,
                                  &pubkey))
                return;

        pubkey_blob = fv_slice_alloc(&fv_keyring_pubkey_blob_allocator);

        pubkey_blob->ref_count = 1;
        pubkey_blob->in_list = true;
        pubkey_blob->timestamp = pubkey.timestamp;

        fv_proto_double_hash(blob->data, blob->size, pubkey_blob->hash);

        if (pubkey.tag) {
                memcpy(pubkey_blob->ripe_or_tag,
                       pubkey.tag,
                       FV_PROTO_HASH_LENGTH);
                memset(pubkey_blob->ripe_or_tag + FV_ADDRESS_TAG_SIZE,
                       0,
                       FV_PROTO_HASH_LENGTH - FV_ADDRESS_TAG_SIZE);
        } else {
                fv_address_from_network_keys(&address,
                                              pubkey.address_version,
                                              pubkey.stream,
                                              pubkey.public_signing_key,
                                              pubkey.public_encryption_key);
                memcpy(pubkey_blob->ripe_or_tag,
                       address.ripe,
                       RIPEMD160_DIGEST_LENGTH);
                memset(pubkey_blob->ripe_or_tag + RIPEMD160_DIGEST_LENGTH,
                       0,
                       FV_PROTO_HASH_LENGTH - RIPEMD160_DIGEST_LENGTH);
        }

        insert_pos = fv_hash_table_get(keyring->pubkey_blob_table,
                                        pubkey_blob->ripe_or_tag);

        if (insert_pos == NULL) {
                fv_list_insert(&keyring->pubkey_blob_list, &pubkey_blob->link);
                fv_hash_table_set(keyring->pubkey_blob_table, pubkey_blob);
        } else {
                fv_list_insert(&insert_pos->link, &pubkey_blob->link);
        }

        check_pubkey_blob_with_messages(keyring, pubkey_blob);
}

static void
send_acknowledgement(struct fv_keyring *keyring,
                     const uint8_t *ack,
                     size_t ack_length)
{
        enum fv_proto_inv_type type;
        const char *command_name;

        if (ack_length == 0) {
                fv_log("The decrypted message contains no "
                        "acknowledgement data");
                return;
        }

        if (ack_length < FV_PROTO_HEADER_SIZE ||
            !fv_proto_check_command_string(ack + 4)) {
                fv_log("The acknowledgement message in the decrypted message "
                        "is invalid");
                return;
        }

        command_name = (const char *) ack + 4;
        ack += FV_PROTO_HEADER_SIZE;
        ack_length -= FV_PROTO_HEADER_SIZE;

        for (type = 0; type < 4; type++) {
                if (!strcmp(fv_proto_get_command_name_for_type(type),
                            command_name)) {
                        fv_network_add_object_from_data(keyring->nw,
                                                         type,
                                                         ack,
                                                         ack_length,
                                                         FV_NETWORK_DELAY,
                                                         "acknowledgement "
                                                         "data");
                        return;
                }
        }

        fv_log("The acknowledgement data contains an unknown command “%s”",
                ack + 4);
}

static void
add_public_key(struct fv_keyring *keyring,
               struct fv_key *public_key)
{
        struct fv_key *key;
        int i;

        /* Check if we already have the key. It could have been added
         * in the time between queuing the crypto to create the key
         * and getting the result */
        for (i = 0; i < fv_pointer_array_length(&keyring->keys); i++) {
                key = fv_pointer_array_get(&keyring->keys, i);

                if (fv_address_equal(&public_key->address, &key->address))
                        return;
        }

        add_key(keyring, public_key);
        save_keyring(keyring);
}

static void
create_public_key_cb(struct fv_key *public_key,
                     void *user_data)
{
        struct fv_keyring_task *task = user_data;
        struct fv_keyring *keyring = task->keyring;

        task->crypto_cookie = NULL;
        free_task(task);

        add_public_key(keyring, public_key);
}

static struct fv_key *
add_public_key_from_network_keys(struct fv_keyring *keyring,
                                 const struct fv_address *address,
                                 const uint8_t *public_signing_key,
                                 const uint8_t *public_encryption_key,
                                 uint64_t pow_per_byte,
                                 uint64_t pow_extra_bytes)
{
        uint8_t full_public_signing_key[FV_ECC_PUBLIC_KEY_SIZE];
        uint8_t full_public_encryption_key[FV_ECC_PUBLIC_KEY_SIZE];
        struct fv_keyring_task *task;
        struct fv_key_params params;
        struct fv_key *key;
        int i;

        /* Check if we already have the key */
        for (i = 0; i < fv_pointer_array_length(&keyring->keys); i++) {
                key = fv_pointer_array_get(&keyring->keys, i);

                if (fv_address_equal(address, &key->address))
                        return key;
        }

        /* The keys from the network don't have the 0x04 prefix so we
         * have to add it */
        full_public_signing_key[0] = 0x04;
        memcpy(full_public_signing_key + 1,
               public_signing_key,
               FV_ECC_PUBLIC_KEY_SIZE - 1);
        full_public_encryption_key[0] = 0x04;
        memcpy(full_public_encryption_key + 1,
               public_encryption_key,
               FV_ECC_PUBLIC_KEY_SIZE - 1);

        params.flags = (FV_KEY_PARAM_PUBLIC_KEYS |
                        FV_KEY_PARAM_VERSION |
                        FV_KEY_PARAM_STREAM |
                        FV_KEY_PARAM_POW_DIFFICULTY);

        params.public_signing_key = full_public_signing_key;
        params.public_encryption_key = full_public_encryption_key;
        params.version = address->version;
        params.stream = address->stream;
        params.pow_per_byte = pow_per_byte;
        params.pow_extra_bytes = pow_extra_bytes;

        task = add_task(keyring);
        task->crypto_cookie =
                fv_crypto_create_public_key(keyring->crypto,
                                             &params,
                                             create_public_key_cb,
                                             task);

        return NULL;
}

static void
decrypt_msg_cb(struct fv_key *key,
               struct fv_blob *blob,
               void *user_data)
{
        struct fv_keyring_task *task = user_data;
        struct fv_keyring *keyring = task->keyring;
        struct fv_proto_decrypted_msg msg;
        struct fv_address sender_address;
        struct fv_key *sender_key;
        char sender_address_string[FV_ADDRESS_MAX_LENGTH + 1];
        int64_t timestamp = task->msg.timestamp;

        task->crypto_cookie = NULL;

        free_task(task);

        /* If we couldn't decrypt it then the key will be NULL */
        if (key == NULL)
                return;

        if (!fv_proto_get_decrypted_msg(blob->data,
                                         blob->size,
                                         &msg))
                goto invalid;

        /* We can't encode the address if these numbers are too high
         * so instead we'll just assume the message is invalid */
        if (msg.sender_stream_number > 255 ||
            msg.sender_address_version > 255)
                goto invalid;

        if (memcmp(key->address.ripe,
                   msg.destination_ripe,
                   RIPEMD160_DIGEST_LENGTH)) {
                fv_log("The key that was used to encrypt the message does "
                        "not match the destination address embedded in the "
                        "message. This could be a surreptitious forwarding "
                        "attack");
                return;
        }

        fv_address_from_network_keys(&sender_address,
                                      msg.sender_address_version,
                                      msg.sender_stream_number,
                                      msg.sender_signing_key,
                                      msg.sender_encryption_key);
        fv_address_encode(&sender_address, sender_address_string);

        /* Store the public key so we don't have to request it if we reply */
        sender_key =
                add_public_key_from_network_keys(keyring,
                                                 &sender_address,
                                                 msg.sender_signing_key,
                                                 msg.sender_encryption_key,
                                                 msg.pow_per_byte,
                                                 msg.pow_extra_bytes);

        fv_log("Accepted message from %s", sender_address_string);

        send_acknowledgement(keyring, msg.ack, msg.ack_length);

        fv_store_save_message(NULL, /* default store */
                               timestamp,
                               sender_key,
                               sender_address_string,
                               key,
                               blob);

        return;

invalid:
        fv_log("Decrypted message is invalid");
}

static void
message_acknowledged(struct fv_keyring_message *message)
{
        struct fv_keyring *keyring = message->keyring;
        char to_address_string[FV_ADDRESS_MAX_LENGTH + 1];
        uint64_t content_id;

        fv_address_encode(&message->to_address,
                           to_address_string);
        fv_log("Received acknowledgement for message from %s",
                to_address_string);

        content_id = message->content_id;

        free_message(message);

        maybe_delete_message_content(keyring, content_id);

        save_messages(keyring);
}

static bool
check_msg_acknowledgement(struct fv_keyring *keyring,
                          const uint8_t *content,
                          size_t content_length)
{
        struct fv_keyring_message *message;

        if (content_length != FV_PROTO_ACKDATA_SIZE)
                return false;

        fv_list_for_each(message, &keyring->messages, link) {
                if (message->state !=
                    FV_KEYRING_MESSAGE_STATE_GENERATING_ACKDATA &&
                    !memcmp(message->ackdata,
                            content,
                            FV_PROTO_ACKDATA_SIZE)) {
                        message_acknowledged(message);
                        return true;
                }
        }

        return false;
}

static void
handle_msg(struct fv_keyring *keyring,
           struct fv_blob *blob)
{
        struct fv_keyring_task *task;
        uint64_t nonce;
        int64_t timestamp;
        ssize_t header_length;
        uint64_t stream_number;

        header_length = fv_proto_get_command(blob->data,
                                              blob->size,

                                              FV_PROTO_ARGUMENT_64,
                                              &nonce,

                                              FV_PROTO_ARGUMENT_TIMESTAMP,
                                              &timestamp,

                                              FV_PROTO_ARGUMENT_VAR_INT,
                                              &stream_number,

                                              FV_PROTO_ARGUMENT_END);

        if (header_length == -1) {
                fv_log("Invalid msg command received");
                return;
        }

        if (check_msg_acknowledgement(keyring,
                                      blob->data + header_length,
                                      blob->size - header_length))
            return;

        task = add_task(keyring);
        task->crypto_cookie =
                fv_crypto_decrypt_msg(keyring->crypto,
                                       blob,
                                       (struct fv_key * const *)
                                       keyring->keys.data,
                                       fv_pointer_array_length(&keyring->keys),
                                       decrypt_msg_cb,
                                       task);
        task->msg.timestamp = timestamp;
}

static void
handle_broadcast(struct fv_keyring *keyring,
                 struct fv_blob *blob)
{
}

static bool
new_object_cb(struct fv_listener *listener,
              void *data)
{
        struct fv_keyring *keyring =
                fv_container_of(listener,
                                 struct fv_keyring,
                                 new_object_listener);
        struct fv_blob *blob = data;

        switch (blob->type) {
        case FV_PROTO_INV_TYPE_GETPUBKEY:
                handle_getpubkey(keyring, blob);
                break;
        case FV_PROTO_INV_TYPE_PUBKEY:
                handle_pubkey(keyring, blob);
                break;
        case FV_PROTO_INV_TYPE_MSG:
                handle_msg(keyring, blob);
                break;
        case FV_PROTO_INV_TYPE_BROADCAST:
                handle_broadcast(keyring, blob);
                break;
        }

        return true;
}

static void
remove_pubkey_blob(struct fv_keyring *keyring,
                   struct fv_keyring_pubkey_blob *pubkey_blob)
{
        struct fv_keyring_pubkey_blob *prev, *next;

        if (!pubkey_blob->in_list)
                return;

        prev = fv_container_of(pubkey_blob->link.prev,
                                struct fv_keyring_pubkey_blob,
                                link);
        next = fv_container_of(pubkey_blob->link.next,
                                struct fv_keyring_pubkey_blob,
                                link);

        fv_list_remove(&pubkey_blob->link);

        /* If this key is the first of its group then we need to move
         * the hash table index to the next key in the group */
        if (&prev->link == &keyring->pubkey_blob_list ||
            memcmp(prev->ripe_or_tag,
                   pubkey_blob->ripe_or_tag,
                   FV_PROTO_HASH_LENGTH)) {
                if (&next->link == &keyring->pubkey_blob_list ||
                    memcmp(next->ripe_or_tag,
                           pubkey_blob->ripe_or_tag,
                           FV_PROTO_HASH_LENGTH))
                        fv_hash_table_remove(keyring->pubkey_blob_table,
                                              pubkey_blob);
                else
                        fv_hash_table_set(keyring->pubkey_blob_table, next);
        }

        pubkey_blob->in_list = false;

        unref_pubkey_blob(pubkey_blob);
}

static void
gc_timeout_cb(struct fv_main_context_source *source,
              void *user_data)
{
        struct fv_keyring *keyring = user_data;
        struct fv_keyring_pubkey_blob *pubkey, *tmp;
        int64_t now = fv_main_context_get_wall_clock(NULL);
        int64_t max_age =
                fv_proto_get_max_age_for_type(FV_PROTO_INV_TYPE_PUBKEY);
        int64_t age;


        fv_list_for_each_safe(pubkey, tmp, &keyring->pubkey_blob_list, link) {
                age = now - pubkey->timestamp;

                if (age >= max_age)
                        remove_pubkey_blob(keyring, pubkey);
        }
}

static void
resend_timeout_cb(struct fv_main_context_source *source,
                  void *user_data)
{
        struct fv_keyring *keyring = user_data;
        struct fv_keyring_message *message, *tmp;

        fv_list_for_each_safe(message, tmp, &keyring->messages, link) {
                switch (message->state) {
                case FV_KEYRING_MESSAGE_STATE_AWAITING_PUBKEY:
                        /* This won't actually do anything if the
                         * pubkey request is still in the network */
                        send_getpubkey_request(message);
                        break;
                case FV_KEYRING_MESSAGE_STATE_AWAITING_ACKNOWLEDGEMENT:
                        /* This won't actually do anything if the msg
                         * is still in the network */
                        post_message(message);
                        break;
                default:
                        break;
                }
        }
}

struct fv_keyring *
fv_keyring_new(struct fv_network *nw)
{
        struct fv_keyring *keyring;
        const size_t pubkey_blob_hash_offset =
                FV_STRUCT_OFFSET(struct fv_keyring_pubkey_blob, ripe_or_tag);

        keyring = fv_alloc(sizeof *keyring);

        keyring->nw = nw;
        keyring->started = false;

        keyring->crypto = NULL;
        keyring->pow = NULL;

        keyring->next_message_content_id = 0;

        fv_list_init(&keyring->tasks);

        keyring->new_object_listener.notify = new_object_cb;
        fv_signal_add(fv_network_get_new_object_signal(nw),
                       &keyring->new_object_listener);

        fv_buffer_init(&keyring->keys);

        fv_list_init(&keyring->messages);

        fv_list_init(&keyring->pubkey_blob_list);
        keyring->pubkey_blob_table =
                fv_hash_table_new(pubkey_blob_hash_offset);

        fv_store_for_each_key(NULL, /* default store */
                               for_each_key_cb,
                               keyring);

        keyring->gc_source =
                fv_main_context_add_timer(NULL,
                                           FV_KEYRING_GC_TIMEOUT,
                                           gc_timeout_cb,
                                           keyring);
        keyring->resend_source =
                fv_main_context_add_timer(NULL,
                                           FV_KEYRING_RESEND_TIMEOUT,
                                           resend_timeout_cb,
                                           keyring);

        return keyring;
}

void
fv_keyring_start(struct fv_keyring *keyring)
{
        if (keyring->started)
                return;

        keyring->started = true;
        keyring->crypto = fv_crypto_new();
        keyring->pow = fv_pow_new();
}

static struct fv_key *
get_private_key_for_address(struct fv_keyring *keyring,
                            const struct fv_address *address)
{
        struct fv_key *key;
        int i;

        for (i = 0; i < fv_pointer_array_length(&keyring->keys); i++) {
                key = fv_pointer_array_get(&keyring->keys, i);

                if (fv_key_has_private(key) &&
                    fv_address_equal(&key->address, address))
                        return key;
        }

        return NULL;
}

static struct fv_key *
get_any_key_for_address(struct fv_keyring *keyring,
                        const struct fv_address *address)
{
        struct fv_key *key;
        int i;

        for (i = 0; i < fv_pointer_array_length(&keyring->keys); i++) {
                key = fv_pointer_array_get(&keyring->keys, i);

                if (fv_address_equal(&key->address, address))
                        return key;
        }

        return NULL;
}

static void
for_each_pubkey_blob_cb(const uint8_t *hash,
                        int64_t timestamp,
                        struct fv_blob *blob,
                        void *user_data)
{
        struct fv_keyring *keyring = user_data;

        handle_pubkey(keyring, blob);
}

static void
for_each_outgoing_cb(const struct fv_store_outgoing *outgoing,
                     void *user_data)
{
        struct fv_keyring *keyring = user_data;
        struct fv_keyring_message *message;
        struct fv_key *from_key;
        char from_address_string[FV_ADDRESS_MAX_LENGTH + 1];

        if (outgoing->content_id >= keyring->next_message_content_id)
                keyring->next_message_content_id = outgoing->content_id + 1;

        from_key = get_private_key_for_address(keyring,
                                               &outgoing->from_address);
        if (from_key == NULL) {
                fv_address_encode(&outgoing->from_address,
                                   from_address_string);
                fv_log("Skipping saved message from %s because the private "
                        "key is no longer available",
                        from_address_string);
                return;
        }

        message = create_message(keyring,
                                 from_key,
                                 &outgoing->to_address,
                                 outgoing->content_encoding,
                                 outgoing->content_id);

        message->last_getpubkey_send_time = outgoing->last_getpubkey_send_time;
        message->last_msg_send_time = outgoing->last_msg_send_time;
        memcpy(message->ackdata, outgoing->ackdata, FV_PROTO_ACKDATA_SIZE);

        if (message->to_key == NULL)
                load_public_key_for_message(message);
        else
                post_message(message);
}

void
fv_keyring_load_store(struct fv_keyring *keyring)
{
        fv_store_for_each_pubkey_blob(NULL,
                                       for_each_pubkey_blob_cb,
                                       keyring);
        fv_store_for_each_outgoing(NULL,
                                    for_each_outgoing_cb,
                                    keyring);
}

static void
create_key_cb(struct fv_key *key,
              void *user_data)
{
        struct fv_keyring_cookie *cookie = user_data;
        struct fv_keyring *keyring = cookie->keyring;

        add_key(keyring, key);
        save_keyring(keyring);

        if (cookie->func)
                cookie->func(key, cookie->user_data);

        fv_free(cookie);
}

static void
msg_pow_cb(uint64_t nonce,
           void *user_data)
{
        struct fv_keyring_message *message = user_data;
        struct fv_keyring *keyring = message->keyring;

        message->pow_cookie = NULL;

        fv_log("Finished calculating proof-of-work for msg. Nonce is %" PRIu64,
                nonce);

        nonce = FV_UINT64_TO_BE(nonce);

        memcpy(message->blob->data, &nonce, sizeof nonce);

        fv_network_add_blob(keyring->nw,
                             message->blob,
                             FV_NETWORK_SKIP_VALIDATION,
                             "outgoing message");

        fv_blob_unref(message->blob);
        message->blob = NULL;

        message->state = FV_KEYRING_MESSAGE_STATE_AWAITING_ACKNOWLEDGEMENT;

        save_messages(keyring);
}

static void
create_msg_blob_cb(struct fv_blob *blob,
                   void *user_data)
{
        struct fv_keyring_message *message = user_data;
        struct fv_keyring *keyring = message->keyring;
        int pow_per_byte;
        int pow_extra_bytes;

        message->crypto_cookie = NULL;

        fv_blob_unref(message->blob);
        message->blob = fv_blob_ref(blob);

        fv_log("Doing proof-of-work calculation for msg");

        message->state = FV_KEYRING_MESSAGE_STATE_CALCULATING_MSG_POW;

        /* Make sure the POW difficulty is at least the network
         * minimum otherwise the message won't propagate through the
         * network and someone would be able to deduce that we are the
         * originator of this message. */
        pow_per_byte = message->to_key->pow_per_byte;
        if (pow_per_byte < FV_PROTO_MIN_POW_PER_BYTE)
                pow_per_byte = FV_PROTO_MIN_POW_PER_BYTE;

        pow_extra_bytes = message->to_key->pow_extra_bytes;
        if (pow_extra_bytes < FV_PROTO_MIN_POW_EXTRA_BYTES)
                pow_extra_bytes = FV_PROTO_MIN_POW_EXTRA_BYTES;

        message->pow_cookie =
                fv_pow_calculate(keyring->pow,
                                  blob->data + sizeof (uint64_t),
                                  blob->size - sizeof (uint64_t),
                                  pow_per_byte,
                                  pow_extra_bytes,
                                  msg_pow_cb,
                                  message);
}

static void
ackdata_pow_cb(uint64_t nonce,
               void *user_data)
{
        uint8_t hash[SHA512_DIGEST_LENGTH];
        struct fv_keyring_message *message = user_data;
        struct fv_keyring *keyring = message->keyring;

        message->pow_cookie = NULL;

        fv_log("Finished calculating proof-of-work for acknowledgement data. "
                "Nonce is %" PRIu64,
                nonce);

        nonce = FV_UINT64_TO_BE(nonce);

        memcpy(message->blob->data +
               message->blob_ackdata_offset +
               FV_PROTO_HEADER_SIZE,
               &nonce,
               sizeof nonce);

        SHA512(message->blob->data + message->blob_ackdata_offset +
               FV_PROTO_HEADER_SIZE,
               message->blob_ackdata_length - FV_PROTO_HEADER_SIZE,
               hash);
        memcpy(message->blob->data + message->blob_ackdata_offset + 20,
               hash,
               4);

        message->last_msg_send_time =
                fv_main_context_get_wall_clock(NULL) +
                rand() % 600 - 300;

        message->state = FV_KEYRING_MESSAGE_STATE_CREATE_MSG_BLOB;

        message->crypto_cookie =
                fv_crypto_create_msg_blob(keyring->crypto,
                                           message->last_msg_send_time,
                                           message->from_key,
                                           message->to_key,
                                           message->blob,
                                           create_msg_blob_cb,
                                           message);
}

static void
add_ackdata_to_message(struct fv_keyring_message *message,
                       size_t message_offset,
                       struct fv_buffer *buffer)
{
        uint32_t msg_length, payload_length, payload_length_be;
        size_t ack_offset;

        /* Leave space for the acknowledgement length. This is a
         * varint but we should never need a length that would tip it
         * over a single byte */
        fv_buffer_set_length(buffer, buffer->length + 1);

        ack_offset = buffer->length;

        fv_buffer_append(buffer, fv_proto_magic, 4);
        fv_buffer_append(buffer, "msg\0\0\0\0\0\0\0\0\0", 12);

        /* Leave space for the message length, checksum and POW */
        fv_buffer_set_length(buffer,
                              buffer->length +
                              sizeof (uint32_t) +
                              sizeof (uint32_t) +
                              sizeof (uint64_t));

        fv_proto_add_64(buffer,
                         fv_main_context_get_wall_clock(NULL) +
                         rand() % 600 - 300);
        fv_proto_add_var_int(buffer, message->from_key->address.stream);
        fv_buffer_append(buffer, message->ackdata, FV_PROTO_ACKDATA_SIZE);

        msg_length = buffer->length - ack_offset;

        /* If this fails then the length won't fit in a byte and we
         * haven't reserved enough space */
        assert(msg_length < 0xfd);

        buffer->data[ack_offset - 1] = msg_length;

        payload_length = msg_length - FV_PROTO_HEADER_SIZE;
        payload_length_be = FV_UINT32_TO_BE(payload_length);

        memcpy(buffer->data + ack_offset + 16,
               &payload_length_be,
               sizeof payload_length_be);

        message->blob_ackdata_offset = ack_offset - message_offset;
        message->blob_ackdata_length = msg_length;
}

static void
load_message_content_cb(struct fv_blob *content_blob,
                        void *user_data)
{
        struct fv_keyring_message *message = user_data;
        struct fv_keyring *keyring = message->keyring;
        struct fv_key *from_key = message->from_key;
        struct fv_buffer buffer;
        size_t message_offset;

        message->store_cookie = NULL;

        /* If the content has disappeared then there's nothing we can
         * do with the message so we'll abandon it */
        if (content_blob == NULL) {
                free_message(message);
                save_messages(keyring);
                return;
        }

        if (message->blob) {
                fv_blob_unref(message->blob);
                message->blob = NULL;
        }

        fv_blob_dynamic_init(&buffer, FV_PROTO_INV_TYPE_MSG);

        message_offset = buffer.length;

        /* Build the unencrypted message */

        fv_proto_add_var_int(&buffer, 1 /* message version */);
        fv_proto_add_var_int(&buffer, message->from_key->address.version);
        fv_proto_add_var_int(&buffer, message->from_key->address.stream);
        fv_proto_add_32(&buffer, FV_PROTO_PUBKEY_BEHAVIORS);
        fv_proto_add_public_key(&buffer, from_key->signing_key);
        fv_proto_add_public_key(&buffer, from_key->encryption_key);
        if (message->from_key->address.version >= 3) {
                fv_proto_add_var_int(&buffer, from_key->pow_per_byte);
                fv_proto_add_var_int(&buffer,
                                      from_key->pow_extra_bytes);
        }
        fv_buffer_append(&buffer,
                          message->to_address.ripe,
                          RIPEMD160_DIGEST_LENGTH);
        fv_proto_add_var_int(&buffer, message->content_encoding);

        fv_proto_add_var_int(&buffer, content_blob->size);
        fv_buffer_append(&buffer, content_blob->data, content_blob->size);

        add_ackdata_to_message(message, message_offset, &buffer);

        message->blob = fv_blob_dynamic_end(&buffer);

        message->state = FV_KEYRING_MESSAGE_STATE_CALCULATING_ACKDATA_POW;

        fv_log("Doing proof-of-work calculation for acknowledgement data");

        message->pow_cookie =
                fv_pow_calculate(keyring->pow,
                                  message->blob->data +
                                  message->blob_ackdata_offset +
                                  FV_PROTO_HEADER_SIZE +
                                  sizeof (uint64_t),
                                  message->blob_ackdata_length -
                                  FV_PROTO_HEADER_SIZE -
                                  sizeof (uint64_t),
                                  FV_PROTO_MIN_POW_PER_BYTE,
                                  FV_PROTO_MIN_POW_EXTRA_BYTES,
                                  ackdata_pow_cb,
                                  message);
}

static void
post_message(struct fv_keyring_message *message)
{
        int64_t now = fv_main_context_get_wall_clock(NULL);

        /* Don't do anything if the msg is still in the network */
        if (now - message->last_msg_send_time <
            fv_proto_get_max_age_for_type(FV_PROTO_INV_TYPE_MSG)) {
                message->state =
                        FV_KEYRING_MESSAGE_STATE_AWAITING_ACKNOWLEDGEMENT;
                return;
        }

        message->state = FV_KEYRING_MESSAGE_STATE_LOADING_CONTENT;

        message->store_cookie =
                fv_store_load_message_content(NULL,
                                               message->content_id,
                                               load_message_content_cb,
                                               message);
}

static void
getpubkey_pow_cb(uint64_t nonce,
                 void *user_data)
{
        struct fv_keyring_message *message = user_data;
        struct fv_keyring *keyring = message->keyring;

        message->pow_cookie = NULL;

        fv_log("Finished calculating proof-of-work for getpubkey. "
                "Nonce is %" PRIu64,
                nonce);

        nonce = FV_UINT64_TO_BE(nonce);

        memcpy(message->blob->data, &nonce, sizeof nonce);

        fv_network_add_blob(keyring->nw,
                             message->blob,
                             FV_NETWORK_SKIP_VALIDATION,
                             "outgoing getpubkey request");

        fv_blob_unref(message->blob);
        message->blob = NULL;

        message->state = FV_KEYRING_MESSAGE_STATE_AWAITING_PUBKEY;

        save_messages(keyring);
}

static void
send_getpubkey_request(struct fv_keyring_message *message)
{
        struct fv_buffer buffer;
        int64_t now = fv_main_context_get_wall_clock(NULL);

        /* Don't do anything if the getpubkey request is still in the
         * network */
        if (now - message->last_getpubkey_send_time <
            fv_proto_get_max_age_for_type(FV_PROTO_INV_TYPE_GETPUBKEY)) {
                message->state = FV_KEYRING_MESSAGE_STATE_AWAITING_PUBKEY;
                return;
        }

        if (message->blob)
                fv_blob_unref(message->blob);

        fv_blob_dynamic_init(&buffer, FV_PROTO_INV_TYPE_GETPUBKEY);

        /* Leave space for the nonce */
        fv_buffer_set_length(&buffer, buffer.length + sizeof (uint64_t));

        message->last_getpubkey_send_time = now + rand() % 600 - 300;

        fv_proto_add_64(&buffer, message->last_getpubkey_send_time);
        fv_proto_add_var_int(&buffer, message->to_address.version);
        fv_proto_add_var_int(&buffer, message->to_address.stream);

        if (message->to_address.version < 4) {
                fv_buffer_append(&buffer,
                                  message->to_address.ripe,
                                  RIPEMD160_DIGEST_LENGTH);
        } else {
                fv_buffer_set_length(&buffer,
                                      buffer.length + FV_ADDRESS_TAG_SIZE);
                fv_address_get_tag(&message->to_address,
                                    buffer.data +
                                    buffer.length -
                                    FV_ADDRESS_TAG_SIZE,
                                    NULL /* tag_private_key */);
        }

        message->blob = fv_blob_dynamic_end(&buffer);

        message->state = FV_KEYRING_MESSAGE_STATE_CALCULATING_GETPUBKEY_POW;

        fv_log("Doing proof-of-work calculation to send getpubkey command");

        message->pow_cookie =
                fv_pow_calculate(message->keyring->pow,
                                  message->blob->data + sizeof (uint64_t),
                                  message->blob->size - sizeof (uint64_t),
                                  FV_PROTO_MIN_POW_PER_BYTE,
                                  FV_PROTO_MIN_POW_EXTRA_BYTES,
                                  getpubkey_pow_cb,
                                  message);
}

static void
check_pubkey_cb(struct fv_key *key,
                void *user_data)
{
        struct fv_keyring_message *message = user_data;
        struct fv_keyring *keyring = message->keyring;

        message->crypto_cookie = NULL;

        if (key == NULL) {
                /* The pubkey is invalid so we'll remove it from the
                 * list and try the next one */
                remove_pubkey_blob(keyring, message->trying_pubkey_blob);
                load_public_key_for_message(message);
        } else {
                add_public_key(keyring, key);
                message->to_key = fv_key_ref(key);
                post_message(message);
        }
}

static void
try_blob_for_message(struct fv_keyring_message *message,
                     struct fv_blob *blob)
{
        message->state = FV_KEYRING_MESSAGE_STATE_TRYING_BLOB;

        message->crypto_cookie =
                fv_crypto_check_pubkey(message->keyring->crypto,
                                        &message->to_address,
                                        blob,
                                        check_pubkey_cb,
                                        message);
}

static void
load_pubkey_from_store_cb(struct fv_blob *blob,
                          void *user_data)
{
        struct fv_keyring_message *message = user_data;

        message->store_cookie = NULL;

        if (blob == NULL) {
                /* Something has gone wrong with the store. We'll just
                 * abandon the key and hope for the best */
                /* The key is garbage so we'll abandon it */
                remove_pubkey_blob(message->keyring,
                                   message->trying_pubkey_blob);
                /* Now we can start again. This will try the next key
                 * if there is one */
                load_public_key_for_message(message);
        } else {
                try_blob_for_message(message, blob);
        }
}

static void
load_pubkey_from_store(struct fv_keyring_message *message,
                       const uint8_t *hash)
{
        message->store_cookie =
                fv_store_load_blob(NULL,
                                    hash,
                                    load_pubkey_from_store_cb,
                                    message);
        message->state = FV_KEYRING_MESSAGE_STATE_LOADING_PUBKEY_FROM_STORE;
}

static bool
try_pubkey_blob_for_message(struct fv_keyring_message *message,
                            struct fv_keyring_pubkey_blob *pubkey_blob)
{
        struct fv_keyring *keyring = message->keyring;
        struct fv_blob *blob;

        if (message->trying_pubkey_blob)
                unref_pubkey_blob(message->trying_pubkey_blob);
        message->trying_pubkey_blob = pubkey_blob;
        pubkey_blob->ref_count++;

        switch (fv_network_get_object(keyring->nw,
                                       pubkey_blob->hash,
                                       &blob)) {
        case FV_NETWORK_OBJECT_LOCATION_NOWHERE:
                return false;

        case FV_NETWORK_OBJECT_LOCATION_STORE:
                load_pubkey_from_store(message, pubkey_blob->hash);
                return true;

        case FV_NETWORK_OBJECT_LOCATION_MEMORY:
                try_blob_for_message(message, blob);
                return true;
        }

        assert(false);

        return false;
}

static void
load_public_key_for_message(struct fv_keyring_message *message)
{
        struct fv_keyring *keyring = message->keyring;
        struct fv_keyring_pubkey_blob *pubkey_blob_start, *pubkey_blob, *tmp;

        pubkey_blob_start = fv_hash_table_get(keyring->pubkey_blob_table,
                                               message->ripe_or_tag);

        if (pubkey_blob_start == NULL) {
                send_getpubkey_request(message);
                return;
        }

        fv_list_for_each_safe(pubkey_blob,
                               tmp,
                               pubkey_blob_start->link.prev,
                               link) {
                /* Check if we've reached the end of the actual list */
                if (&pubkey_blob->link == &keyring->pubkey_blob_list)
                        break;

                /* Check if we've reached the end of the group of
                 * pubkey_blobs with the same ripe/tag */
                if (memcmp(pubkey_blob->ripe_or_tag,
                           message->ripe_or_tag,
                           FV_PROTO_HASH_LENGTH))
                        break;

                if (try_pubkey_blob_for_message(message, pubkey_blob))
                        return;
        }

        send_getpubkey_request(message);
}

static void
generate_ackdata_cb(const uint8_t *ackdata,
                    void *user_data)
{
        struct fv_keyring_message *message = user_data;

        memcpy(message->ackdata, ackdata, FV_PROTO_ACKDATA_SIZE);

        message->crypto_cookie = NULL;

        if (message->to_key == NULL)
                load_public_key_for_message(message);
        else
                post_message(message);

        save_messages(message->keyring);
}

static struct fv_keyring_message *
create_message(struct fv_keyring *keyring,
               struct fv_key *from_key,
               const struct fv_address *to_address,
               int content_encoding,
               uint64_t content_id)
{
        struct fv_keyring_message *message;

        message = fv_alloc(sizeof *message);

        message->keyring = keyring;

        message->from_key = fv_key_ref(from_key);

        message->to_address = *to_address;

        message->to_key = get_any_key_for_address(keyring, to_address);

        if (message->to_key)
                fv_key_ref(message->to_key);

        message->content_encoding = content_encoding;
        message->content_id = content_id;

        message->pow_cookie = NULL;
        message->crypto_cookie = NULL;
        message->store_cookie = NULL;
        message->blob = NULL;
        message->trying_pubkey_blob = NULL;

        message->last_getpubkey_send_time = 0;
        message->last_msg_send_time = 0;

        if (message->to_address.version < 4) {
                memcpy(message->ripe_or_tag,
                       message->to_address.ripe,
                       RIPEMD160_DIGEST_LENGTH);
                memset(message->ripe_or_tag + RIPEMD160_DIGEST_LENGTH,
                       0,
                       FV_PROTO_HASH_LENGTH - RIPEMD160_DIGEST_LENGTH);
        } else {
                fv_address_get_tag(&message->to_address,
                                    message->ripe_or_tag,
                                    NULL /* tag_private_key */);
                memset(message->ripe_or_tag + FV_ADDRESS_TAG_SIZE,
                       0,
                       FV_PROTO_HASH_LENGTH - FV_ADDRESS_TAG_SIZE);
        }

        fv_list_insert(keyring->messages.prev, &message->link);

        return message;
}

bool
fv_keyring_send_message(struct fv_keyring *keyring,
                         const struct fv_address *from_address,
                         const struct fv_address *to_addresses,
                         int n_to_addresses,
                         int content_encoding,
                         struct fv_blob *content,
                         struct fv_error **error)
{
        struct fv_key *from_key;
        uint64_t content_id;
        struct fv_keyring_message *message;
        int i;

        fv_return_val_if_fail(n_to_addresses >= 1, false);

        from_key = get_private_key_for_address(keyring, from_address);

        if (from_key == NULL) {
                fv_set_error(error,
                              &fv_keyring_error,
                              FV_KEYRING_ERROR_UNKNOWN_FROM_ADDRESS,
                              "The private key for the from address is not "
                              "available");
                return false;
        }

        content_id = keyring->next_message_content_id++;

        fv_store_save_message_content(NULL, content_id, content);

        for (i = 0; i < n_to_addresses; i++) {
                message = create_message(keyring,
                                         from_key,
                                         to_addresses + i,
                                         content_encoding,
                                         content_id);

                message->state = FV_KEYRING_MESSAGE_STATE_GENERATING_ACKDATA;
                message->crypto_cookie =
                        fv_crypto_generate_ackdata(keyring->crypto,
                                                    generate_ackdata_cb,
                                                    message);
        }

        return true;
}

struct fv_keyring_cookie *
fv_keyring_create_key(struct fv_keyring *keyring,
                       const struct fv_key_params *params,
                       int leading_zeroes,
                       fv_keyring_create_key_func func,
                       void *user_data)
{
        struct fv_keyring_cookie *cookie;

        cookie = fv_alloc(sizeof *cookie);
        cookie->keyring = keyring;
        cookie->func = func;
        cookie->user_data = user_data;

        cookie->crypto_cookie = fv_crypto_create_key(keyring->crypto,
                                                      params,
                                                      leading_zeroes,
                                                      create_key_cb,
                                                      cookie);

        return cookie;
}

void
fv_keyring_cancel_task(struct fv_keyring_cookie *cookie)
{
        fv_crypto_cancel_task(cookie->crypto_cookie);
        fv_free(cookie);
}

static void
cancel_tasks(struct fv_keyring *keyring)
{
        struct fv_keyring_task *task, *tmp;

        fv_list_for_each_safe(task, tmp, &keyring->tasks, link)
                free_task(task);
}

static void
free_pubkey_blobs(struct fv_keyring *keyring)
{
        struct fv_keyring_pubkey_blob *pubkey, *tmp;

        fv_list_for_each_safe(pubkey, tmp, &keyring->pubkey_blob_list, link)
                fv_slice_free(&fv_keyring_pubkey_blob_allocator, pubkey);
        fv_hash_table_free(keyring->pubkey_blob_table);
}

static void
free_messages(struct fv_keyring *keyring)
{
        struct fv_keyring_message *message, *tmp;

        fv_list_for_each_safe(message, tmp, &keyring->messages, link)
                free_message(message);
}

void
fv_keyring_free(struct fv_keyring *keyring)
{
        int i;

        save_messages(keyring);
        save_keyring(keyring);

        fv_main_context_remove_source(keyring->resend_source);
        fv_main_context_remove_source(keyring->gc_source);

        fv_list_remove(&keyring->new_object_listener.link);

        free_messages(keyring);
        free_pubkey_blobs(keyring);
        cancel_tasks(keyring);

        for (i = 0; i < fv_pointer_array_length(&keyring->keys); i++)
                fv_key_unref(fv_pointer_array_get(&keyring->keys, i));
        fv_buffer_destroy(&keyring->keys);

        if (keyring->pow)
                fv_pow_free(keyring->pow);
        if (keyring->crypto)
                fv_crypto_free(keyring->crypto);
        fv_free(keyring);
}
