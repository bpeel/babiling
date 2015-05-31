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

#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/types.h>
#include <dirent.h>
#include <assert.h>
#include <inttypes.h>
#include <sys/time.h>
#include <fcntl.h>

#include "fv-store.h"
#include "fv-util.h"
#include "fv-buffer.h"
#include "fv-log.h"
#include "fv-list.h"
#include "fv-slice.h"
#include "fv-proto.h"
#include "fv-main-context.h"
#include "fv-file-error.h"
#include "fv-base58.h"
#include "fv-base64.h"
#include "fv-address.h"
#include "fv-load-keys.h"
#include "fv-load-outgoings.h"
#include "fv-save-message.h"
#include "fv-mkdir.h"

struct fv_error_domain
fv_store_error;

struct fv_store {
        struct fv_buffer filename_buf;
        struct fv_buffer tmp_buf;
        int directory_len;
        struct fv_buffer maildir_buf;
        int maildir_len;

        pthread_mutex_t mutex;
        pthread_cond_t cond;
        pthread_t thread;
        bool started;

        struct fv_list queue;

        /* The slice allocator has to be per-store rather than a
         * global variable so that we can be sure to use it only when
         * the mutex is locked in order to make it thread-safe */
        struct fv_slice_allocator allocator;

        /* Number of messages we have saved. This is just used to help
         * generate a unique name */
        unsigned int num_stored_messages;

        bool quit;
};

enum fv_store_task_type {
        FV_STORE_TASK_TYPE_SAVE_BLOB,
        FV_STORE_TASK_TYPE_LOAD_BLOB,
        FV_STORE_TASK_TYPE_SAVE_ADDR_LIST,
        FV_STORE_TASK_TYPE_SAVE_KEYS,
        FV_STORE_TASK_TYPE_SAVE_OUTGOINGS,
        FV_STORE_TASK_TYPE_SAVE_MESSAGE,
        FV_STORE_TASK_TYPE_SAVE_MESSAGE_CONTENT,
        FV_STORE_TASK_TYPE_LOAD_MESSAGE_CONTENT,
        FV_STORE_TASK_TYPE_DELETE_MESSAGE_CONTENT,
        FV_STORE_TASK_TYPE_DELETE_OBJECT
};

struct fv_store_task {
        struct fv_list link;
        enum fv_store_task_type type;

        union {
                struct {
                        uint8_t hash[FV_PROTO_HASH_LENGTH];
                        struct fv_blob *blob;
                } save_blob;

                struct {
                        uint8_t hash[FV_PROTO_HASH_LENGTH];
                        struct fv_store_cookie *cookie;
                } load_blob;

                struct {
                        struct fv_store_addr *addrs;
                        int n_addrs;
                } save_addr_list;

                struct {
                        struct fv_key **keys;
                        int n_keys;
                } save_keys;

                struct {
                        struct fv_blob *blob;
                } save_outgoings;

                struct {
                        int64_t timestamp;
                        struct fv_key *from_key;
                        char from_address[FV_ADDRESS_MAX_LENGTH + 1];
                        struct fv_key *to_key;
                        struct fv_blob *blob;
                } save_message;

                struct {
                        uint64_t id;
                        struct fv_blob *blob;
                } save_message_content;

                struct {
                        uint64_t id;
                        struct fv_store_cookie *cookie;
                } load_message_content;

                struct {
                        uint64_t id;
                } delete_message_content;

                struct {
                        uint8_t hash[FV_PROTO_HASH_LENGTH];
                } delete_object;
        };
};

struct fv_store_cookie {
        struct fv_store *store;
        struct fv_blob *blob;
        struct fv_store_task *task;
        struct fv_main_context_source *idle_source;
        fv_store_load_callback func;
        void *user_data;
};

typedef void (* for_each_blob_internal_func)(enum fv_proto_inv_type type,
                                             const uint8_t *hash,
                                             int64_t timestamp,
                                             const char *filename,
                                             FILE *file,
                                             void *user_data);

/* The cookies are only allocated and destroyed in the main thread so
 * we don't need to have a per-store allocator */
FV_SLICE_ALLOCATOR(struct fv_store_cookie, fv_store_cookie_allocator);

/* ceil(log₅₈(2 ** ((private_key_size + 4 + 1) × 8))) */
/* The added four is for the checksum, and the 1 for the 0x80 prefix */
#define FV_STORE_MAX_WIF_LENGTH 51

/* ceil(log₅₈(2 ** (FV_ECC_PUBLIC_KEY_SIZE × 8))) */
/* The added four is for the checksum, and the 1 for the 0x80 prefix */
#define FV_STORE_MAX_PUBLIC_KEY_LENGTH 89

/* ceil(log₅₈(2 ** (FV_PROTO_ACKDATA_SIZE × 8))) */
#define FV_STORE_MAX_ACKDATA_LENGTH 44

static struct fv_store *fv_store_default = NULL;

static struct fv_store *
fv_store_get_default_or_abort(void)
{
        struct fv_store *store;

        store = fv_store_get_default();

        if (store == NULL)
                fv_fatal("default store is missing");

        return store;
}

struct fv_store *
fv_store_get_default(void)
{
        return fv_store_default;
}

void
fv_store_set_default(struct fv_store *store)
{
        fv_store_default = store;
}

static void
strip_trailing_slashes(struct fv_buffer *buffer)
{
        /* Strip all but the first slash */
        while (buffer->length > 1 && buffer->data[buffer->length - 1] == '/')
                buffer->length--;
}

static bool
append_cwd(struct fv_buffer *buffer)
{
        size_t try_size = 32;

        while (true) {
                fv_buffer_ensure_size(buffer, buffer->length + try_size);

                if (getcwd((char *) buffer->data + buffer->length,
                           buffer->size - buffer->length)) {
                        buffer->length += strlen((char *) buffer->data +
                                                 buffer->length);
                        return true;
                } else if (errno != ERANGE) {
                        return false;
                }

                try_size *= 2;
        }
}

static void
append_absolute_path(struct fv_buffer *buffer,
                     const char *path)
{
        if (path[0] != '/' && append_cwd(buffer))
                fv_buffer_append_c(buffer, '/');

        fv_buffer_append_string(buffer, path);
        strip_trailing_slashes(buffer);
}

static bool
init_store_directory(struct fv_store *store,
                     const char *store_directory,
                     struct fv_error **error)
{
        const char *data_home, *home;

        if (store_directory) {
                append_absolute_path(&store->filename_buf, store_directory);
                fv_buffer_append_string(&store->filename_buf, "/");
        } else if ((data_home = getenv("XDG_DATA_HOME"))) {
                if (data_home[0] != '/') {
                        fv_set_error(error,
                                      &fv_store_error,
                                      FV_STORE_ERROR_INVALID_STORE_DIRECTORY,
                                      "The XDG_DATA_HOME path is not "
                                      "absolute");
                        return false;
                }

                fv_buffer_append_string(&store->filename_buf,
                                         data_home);
                strip_trailing_slashes(&store->filename_buf);
                fv_buffer_append_string(&store->filename_buf,
                                         "/notbit/");
        } else if ((home = getenv("HOME"))) {
                if (home[0] != '/') {
                        fv_set_error(error,
                                      &fv_store_error,
                                      FV_STORE_ERROR_INVALID_STORE_DIRECTORY,
                                      "The HOME path is not "
                                      "absolute");
                        return false;
                }

                fv_buffer_append_string(&store->filename_buf, home);
                strip_trailing_slashes(&store->filename_buf);
                fv_buffer_append_string(&store->filename_buf,
                                         "/.local/share/notbit/");
        } else {
                fv_set_error(error,
                              &fv_store_error,
                              FV_STORE_ERROR_INVALID_STORE_DIRECTORY,
                              "Neither XDG_DATA_HOME nor HOME is set");
                return false;
        }

        store->directory_len = store->filename_buf.length;

        fv_buffer_append_string(&store->filename_buf, "objects");

        if (!fv_mkdir_hierarchy(&store->filename_buf, error))
                return false;

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf, "outgoing");
        if (!fv_mkdir((const char *) store->filename_buf.data, error))
                return false;

        return true;
}

static bool
init_maildir(struct fv_store *store,
             const char *maildir,
             struct fv_error **error)
{
        const char *home;

        if (maildir) {
                append_absolute_path(&store->maildir_buf, maildir);
                fv_buffer_append_c(&store->maildir_buf, '/');
        } else if ((home = getenv("HOME"))) {
                if (home[0] != '/') {
                        fv_set_error(error,
                                      &fv_store_error,
                                      FV_STORE_ERROR_INVALID_MAILDIR,
                                      "The HOME path is not "
                                      "absolute");
                        return false;
                }

                fv_buffer_append_string(&store->maildir_buf,
                                         home);
                strip_trailing_slashes(&store->maildir_buf);
                fv_buffer_append_string(&store->maildir_buf,
                                         "/.maildir/");
        } else {
                fv_set_error(error,
                              &fv_store_error,
                              FV_STORE_ERROR_INVALID_MAILDIR,
                              "HOME is not set");
                return false;
        }

        store->maildir_len = store->maildir_buf.length;

        fv_buffer_append_string(&store->maildir_buf, "new");

        if (!fv_mkdir_hierarchy(&store->maildir_buf, error))
                return false;

        fv_buffer_set_length(&store->maildir_buf,
                              store->maildir_buf.length - 3);
        fv_buffer_append_string(&store->maildir_buf, "tmp");

        if (!fv_mkdir((const char *) store->maildir_buf.data, error))
                return false;

        fv_buffer_set_length(&store->maildir_buf,
                              store->maildir_buf.length - 3);
        fv_buffer_append_string(&store->maildir_buf, "cur");

        if (!fv_mkdir((const char *) store->maildir_buf.data, error))
                return false;

        return true;
}

static void
append_hash(struct fv_buffer *buffer,
            const uint8_t *hash)
{
        int i;

        for (i = 0; i < FV_PROTO_HASH_LENGTH; i++)
                fv_buffer_append_printf(buffer, "%02x", hash[i]);
}

static void
load_data_idle_cb(struct fv_main_context_source *source,
                  void *user_data)
{
        struct fv_store_cookie *cookie = user_data;

        cookie->func(cookie->blob, cookie->user_data);

        if (cookie->blob)
                fv_blob_unref(cookie->blob);

        fv_slice_free(&fv_store_cookie_allocator, cookie);

        fv_main_context_remove_source(source);
}

static bool
read_all(const char *filename,
         void *data,
         size_t size,
         FILE *stream)
{
        errno = 0;

        if (fread(data, 1, size, stream) != size) {
                if (errno == 0)
                        fv_log("The object file %s is too short",
                                filename);
                else
                        fv_log("Error reading %s: %s",
                                filename,
                                strerror(errno));

                return false;
        }

        return true;
}

static struct fv_blob *
load_blob_from_file(const char *filename,
                    FILE *file)
{
        struct stat statbuf;
        struct fv_blob *blob;
        uint32_t type;

        if (fstat(fileno(file), &statbuf) == -1) {
                fv_log("Error getting info for %s", filename);
                return NULL;
        }

        if (statbuf.st_size < sizeof (uint32_t)) {
                fv_log("Object file %s is too short", filename);
                return NULL;
        }

        if (!read_all(filename, &type, sizeof type, file))
                return NULL;

        blob = fv_blob_new(FV_UINT32_FROM_BE(type),
                            NULL /* data */,
                            statbuf.st_size - sizeof (uint32_t));

        if (!read_all(filename, blob->data, blob->size, file)) {
                fv_blob_unref(blob);
                return NULL;
        }

        return blob;
}

static void
set_hash_filename(struct fv_store *store,
                  const uint8_t *hash)
{
        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf, "objects/");
        append_hash(&store->filename_buf, hash);
}

static void
handle_load_blob(struct fv_store *store,
                 struct fv_store_task *task)
{
        struct fv_blob *blob = NULL;
        FILE *file;

        /* As a special case this the lock is still held when this
         * function is called */

        /* If the task was cancelled before we got here then the
         * cookie will have been reset to NULL. In that case we don't
         * need to do anything */
        if (task->load_blob.cookie == NULL)
                return;

        pthread_mutex_unlock(&store->mutex);

        set_hash_filename(store, task->load_blob.hash);

        file = fopen((char *) store->filename_buf.data, "rb");

        if (file == NULL) {
                fv_log("Error opening %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
        } else {
                blob = load_blob_from_file((char *) store->filename_buf.data,
                                           file);

                fclose(file);
        }

        pthread_mutex_lock(&store->mutex);

        /* The task could have also been cancelled while we were
         * loading with the mutex unlocked */
        if (task->load_blob.cookie == NULL) {
                if (blob)
                        fv_blob_unref(blob);
                return;
        }

        task->load_blob.cookie->blob = blob;
        task->load_blob.cookie->idle_source =
                fv_main_context_add_idle(NULL,
                                          load_data_idle_cb,
                                          task->load_blob.cookie);
}

static void
rename_tmp_file(struct fv_store *store)
{
        store->tmp_buf.length = 0;
        fv_buffer_append(&store->tmp_buf,
                          store->filename_buf.data,
                          store->filename_buf.length - 4);
        fv_buffer_append_c(&store->tmp_buf, '\0');

        if (rename((char *) store->filename_buf.data,
                   (char *) store->tmp_buf.data) == -1) {
                fv_log("Error renaming %s to %s: %s",
                        (char *) store->filename_buf.data,
                        (char *) store->tmp_buf.data,
                        strerror(errno));
                unlink((char *) store->filename_buf.data);
        }
}

static void
handle_save_blob(struct fv_store *store,
                 struct fv_store_task *task)
{
        FILE *file;
        uint32_t type;

        set_hash_filename(store, task->save_blob.hash);

        fv_buffer_append_string(&store->filename_buf, ".tmp");

        file = fopen((char *) store->filename_buf.data, "wb");

        if (file == NULL) {
                fv_log("Error opening %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                return;
        }

        type = FV_UINT32_TO_BE(task->save_blob.blob->type);

        if (fwrite(&type, 1, sizeof type, file) != sizeof type ||
            fwrite(task->save_blob.blob->data, 1,
                   task->save_blob.blob->size, file) !=
            task->save_blob.blob->size) {
                fv_log("Error writing %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                fclose(file);
                unlink((char *) store->filename_buf.data);
                return;
        }

        if (fclose(file) == EOF) {
                fv_log("Error writing %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                unlink((char *) store->filename_buf.data);
                return;
        }

        rename_tmp_file(store);
}

static void
handle_delete_object(struct fv_store *store,
                     struct fv_store_task *task)
{
        set_hash_filename(store, task->delete_object.hash);

        if (unlink((char *) store->filename_buf.data) == -1) {
                fv_log("Error deleting %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
        }
}

static void
handle_save_addr_list(struct fv_store *store,
                      struct fv_store_task *task)
{
        struct fv_store_addr *addrs;
        char *address;
        FILE *out;
        int i;

        fv_log("Saving addr list");

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf,
                                 "addr-list.txt.tmp");

        addrs = task->save_addr_list.addrs;

        out = fopen((char *) store->filename_buf.data, "w");

        if (out == NULL) {
                fv_log("Error opening %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                return;
        }

        for (i = 0; i < task->save_addr_list.n_addrs; i++) {
                address = fv_netaddress_to_string(&addrs[i].address);
                fprintf(out,
                        "%" PRIi64 ",%" PRIu32 ",%" PRIu64 ",%s\n",
                        addrs[i].timestamp,
                        addrs[i].stream,
                        addrs[i].services,
                        address);
                fv_free(address);
        }

        if (fclose(out) == EOF) {
                fv_log("Error writing to %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                return;
        }

        rename_tmp_file(store);
}

static void
encode_wif(const EC_KEY *key,
           char *wif)
{
        const BIGNUM *private_key;
        uint8_t hash1[SHA256_DIGEST_LENGTH];
        uint8_t hash2[SHA256_DIGEST_LENGTH];
        uint8_t address_bytes[FV_ECC_PRIVATE_KEY_SIZE + 4 + 1];
        size_t wif_length;
        int n_bytes;

        address_bytes[0] = 0x80;

        private_key = EC_KEY_get0_private_key(key);
        n_bytes = BN_num_bytes(private_key);
        BN_bn2bin(private_key,
                  address_bytes + 1 + FV_ECC_PRIVATE_KEY_SIZE - n_bytes);
        memset(address_bytes + 1, 0, FV_ECC_PRIVATE_KEY_SIZE - n_bytes);

        SHA256(address_bytes, FV_ECC_PRIVATE_KEY_SIZE + 1, hash1);
        SHA256(hash1, SHA256_DIGEST_LENGTH, hash2);

        memcpy(address_bytes + FV_ECC_PRIVATE_KEY_SIZE + 1, hash2, 4);

        wif_length = fv_base58_encode(address_bytes,
                                       sizeof address_bytes,
                                       wif);
        assert(wif_length <= FV_STORE_MAX_WIF_LENGTH);

        wif[wif_length] = '\0';
}

static void
encode_public_key(const EC_KEY *key,
                  char *public_key)
{
        uint8_t buf[FV_ECC_PUBLIC_KEY_SIZE];
        size_t size;

        size = EC_POINT_point2oct(EC_KEY_get0_group(key),
                                  EC_KEY_get0_public_key(key),
                                  POINT_CONVERSION_UNCOMPRESSED,
                                  buf,
                                  sizeof buf,
                                  NULL);
        assert(size == FV_ECC_PUBLIC_KEY_SIZE);

        size = fv_base58_encode(buf, sizeof buf, public_key);
        assert(size <= FV_STORE_MAX_PUBLIC_KEY_LENGTH);

        public_key[size] = '\0';
}

static void
write_key(struct fv_key *key,
          FILE *out)
{
        char address[FV_ADDRESS_MAX_LENGTH + 1];
        char signing_key_wif[FV_STORE_MAX_WIF_LENGTH + 1];
        char encryption_key_wif[FV_STORE_MAX_WIF_LENGTH + 1];
        char public_signing_key[FV_STORE_MAX_PUBLIC_KEY_LENGTH + 1];
        char public_encryption_key[FV_STORE_MAX_PUBLIC_KEY_LENGTH + 1];

        fv_address_encode(&key->address, address);

        fprintf(out,
                "[%s]\n"
                "label = %s\n"
                "noncetrialsperbyte = %i\n"
                "payloadlengthextrabytes = %i\n",
                address,
                key->label,
                key->pow_per_byte,
                key->pow_extra_bytes);

        if (fv_key_has_private(key)) {
                encode_wif(key->signing_key, signing_key_wif);
                encode_wif(key->encryption_key, encryption_key_wif);

                fprintf(out,
                        "privsigningkey = %s\n"
                        "privencryptionkey = %s\n"
                        "lastpubkeysendtime = %" PRIi64 "\n"
                        "enabled = %s\n"
                        "decoy = %s\n",
                        signing_key_wif,
                        encryption_key_wif,
                        key->last_pubkey_send_time,
                        key->enabled ? "true" : "false",
                        key->decoy ? "true" : "false");
        } else {
                encode_public_key(key->signing_key, public_signing_key);
                encode_public_key(key->encryption_key, public_encryption_key);

                fprintf(out,
                        "pubsigningkey = %s\n"
                        "pubencryptionkey = %s\n",
                        public_signing_key,
                        public_encryption_key);
        }

        fputc('\n', out);
}

static FILE *
open_sensitive_file(const char *filename)
{
        FILE *file;
        int fd;

        /* open and fdopen is used instead of fopen so that we can
         * make the permissions on the file be at most 600. We don't
         * want sensitive files be world-readable */
        fd = open(filename,
                  O_WRONLY | O_CREAT,
                  S_IRUSR | S_IWUSR);

        if (fd == -1) {
                fv_log("Error opening %s: %s",
                        filename,
                        strerror(errno));
                return NULL;
        }

        file = fdopen(fd, "w");
        if (file == NULL) {
                fv_log("Error opening %s: %s",
                        filename,
                        strerror(errno));
                close(fd);
                return NULL;
        }

        return file;
}

static void
handle_save_keys(struct fv_store *store,
                 struct fv_store_task *task)
{
        FILE *out;
        int i;

        fv_log("Saving keys");

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf,
                                 "keys.dat.tmp");

        out = open_sensitive_file((char *) store->filename_buf.data);

        if (out == NULL)
                return;

        for (i = 0; i < task->save_keys.n_keys; i++)
                write_key(task->save_keys.keys[i], out);

        if (fclose(out) == EOF) {
                fv_log("Error writing to %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                return;
        }

        rename_tmp_file(store);
}

static void
write_outgoing(const struct fv_store_outgoing *outgoing,
               FILE *out)
{
        char from_address[FV_ADDRESS_MAX_LENGTH + 1];
        char to_address[FV_ADDRESS_MAX_LENGTH + 1];
        char ackdata[FV_STORE_MAX_ACKDATA_LENGTH + 1];
        ssize_t ackdata_length;

        fv_address_encode(&outgoing->from_address, from_address);
        fv_address_encode(&outgoing->to_address, to_address);
        ackdata_length = fv_base58_encode(outgoing->ackdata,
                                           FV_PROTO_ACKDATA_SIZE,
                                           ackdata);

        assert(ackdata_length <= FV_STORE_MAX_ACKDATA_LENGTH);

        ackdata[ackdata_length] = '\0';

        fprintf(out,
                "[message]\n"
                "fromaddress = %s\n"
                "toaddress = %s\n"
                "ackdata = %s\n"
                "contentid = %" PRIu64 "\n"
                "contentencoding = %i\n"
                "lastgetpubkeysendtime = %" PRIi64 "\n"
                "lastmsgsendtime = %" PRIi64 "\n"
                "\n",
                from_address,
                to_address,
                ackdata,
                outgoing->content_id,
                outgoing->content_encoding,
                outgoing->last_getpubkey_send_time,
                outgoing->last_msg_send_time);
}

static void
handle_save_outgoings(struct fv_store *store,
                      struct fv_store_task *task)
{
        struct fv_blob *blob = task->save_outgoings.blob;
        const struct fv_store_outgoing *outgoings =
                (const struct fv_store_outgoing *) blob->data;
        FILE *out;
        size_t i;

        fv_log("Saving outgoing messages");

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf,
                                 "outgoing-messages.dat.tmp");

        out = open_sensitive_file((char *) store->filename_buf.data);

        if (out == NULL)
                return;

        for (i = 0; i < blob->size / sizeof *outgoings; i++)
                write_outgoing(outgoings + i, out);

        if (fclose(out) == EOF) {
                fv_log("Error writing to %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                return;
        }

        rename_tmp_file(store);
}

static void
generate_maildir_name(struct fv_store *store,
                      struct fv_buffer *buffer)
{
        struct timeval tv;
        int hostname_length = 2;

        gettimeofday(&tv, NULL /* tz */);

        fv_buffer_append_printf(buffer,
                                 "%li.M%liQ%u.",
                                 (long int) tv.tv_sec,
                                 (long int) tv.tv_usec,
                                 store->num_stored_messages++);

        while (true) {
                fv_buffer_ensure_size(buffer,
                                       buffer->length + hostname_length);

                if (gethostname((char *) buffer->data + buffer->length,
                                hostname_length) == -1) {
                        if (errno == ENAMETOOLONG)
                                hostname_length *= 2;
                        else
                                buffer->data[buffer->length--] = '\0';
                } else {
                        break;
                }
        }

        while (buffer->data[buffer->length]) {
                if (buffer->data[buffer->length] == '/')
                        buffer->data[buffer->length] = '\057';
                else if (buffer->data[buffer->length] == ':')
                        buffer->data[buffer->length] = '\072';
                buffer->length++;
        }
}

static void
handle_save_message(struct fv_store *store,
                    struct fv_store_task *task)
{
        FILE *out;

        fv_log("Saving message");

        store->maildir_buf.length = store->maildir_len;
        fv_buffer_append_string(&store->maildir_buf, "tmp/");

        generate_maildir_name(store, &store->maildir_buf);

        out = fopen((char *) store->maildir_buf.data, "w");

        if (out == NULL) {
                fv_log("Error opening %s: %s",
                        (char *) store->maildir_buf.data,
                        strerror(errno));
                return;
        }

        fv_save_message(task->save_message.timestamp,
                         task->save_message.from_key,
                         task->save_message.from_address,
                         task->save_message.to_key,
                         task->save_message.blob,
                         out);

        if (fclose(out) == EOF) {
                fv_log("Error writing to %s: %s",
                        (char *) store->maildir_buf.data,
                        strerror(errno));
                return;
        }

        store->tmp_buf.length = 0;
        fv_buffer_append(&store->tmp_buf,
                          store->maildir_buf.data,
                          store->maildir_len);
        fv_buffer_append_string(&store->tmp_buf, "new");
        fv_buffer_append(&store->tmp_buf,
                          store->maildir_buf.data +
                          store->maildir_len + 3,
                          store->maildir_buf.length -
                          store->maildir_len - 3 + 1);

        if (rename((char *) store->maildir_buf.data,
                   (char *) store->tmp_buf.data) == -1) {
                fv_log("Error renaming %s to %s: %s",
                        (char *) store->filename_buf.data,
                        (char *) store->tmp_buf.data,
                        strerror(errno));
        }
}

static void
set_message_content_filename(struct fv_store *store,
                             uint64_t content_id)
{
        store->filename_buf.length = store->directory_len;
        fv_buffer_append_printf(&store->filename_buf,
                                 "outgoing/%016" PRIx64,
                                 content_id);
}

static void
handle_save_message_content(struct fv_store *store,
                            struct fv_store_task *task)
{
        struct fv_blob *blob = task->save_message_content.blob;
        FILE *file;

        set_message_content_filename(store, task->save_message_content.id);
        fv_buffer_append_string(&store->filename_buf, ".tmp");

        file = open_sensitive_file((char *) store->filename_buf.data);

        if (file == NULL)
                return;

        if (fwrite(blob->data, 1, blob->size, file) != blob->size) {
                fv_log("Error writing %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                fclose(file);
                unlink((char *) store->filename_buf.data);
                return;
        }

        if (fclose(file) == EOF) {
                fv_log("Error writing %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                unlink((char *) store->filename_buf.data);
                return;
        }

        rename_tmp_file(store);
}

static struct fv_blob *
load_message_content_from_file(const char *filename,
                               FILE *file)
{
        struct stat statbuf;
        struct fv_blob *blob;

        if (fstat(fileno(file), &statbuf) == -1) {
                fv_log("Error getting info for %s", filename);
                return NULL;
        }

        blob = fv_blob_new(FV_PROTO_INV_TYPE_MSG,
                            NULL /* data */,
                            statbuf.st_size);

        if (!read_all(filename, blob->data, blob->size, file)) {
                fv_blob_unref(blob);
                return NULL;
        }

        return blob;
}

static void
handle_load_message_content(struct fv_store *store,
                            struct fv_store_task *task)
{
        struct fv_blob *blob = NULL;
        const char *filename;
        FILE *file;

        /* As a special case this the lock is still held when this
         * function is called */

        /* If the task was cancelled before we got here then the
         * cookie will have been reset to NULL. In that case we don't
         * need to do anything */
        if (task->load_message_content.cookie == NULL)
                return;

        pthread_mutex_unlock(&store->mutex);

        set_message_content_filename(store, task->load_message_content.id);
        filename = (const char *) store->filename_buf.data;

        file = fopen(filename, "rb");

        if (file == NULL) {
                fv_log("Error opening %s: %s", filename, strerror(errno));
        } else {
                blob = load_message_content_from_file(filename, file);
                fclose(file);
        }

        pthread_mutex_lock(&store->mutex);

        /* The task could have also been cancelled while we were
         * loading with the mutex unlocked */
        if (task->load_message_content.cookie == NULL) {
                if (blob)
                        fv_blob_unref(blob);
                return;
        }

        task->load_message_content.cookie->blob = blob;
        task->load_message_content.cookie->idle_source =
                fv_main_context_add_idle(NULL,
                                          load_data_idle_cb,
                                          task->load_message_content.cookie);
}

static void
handle_delete_message_content(struct fv_store *store,
                              struct fv_store_task *task)
{
        set_message_content_filename(store, task->delete_message_content.id);

        if (unlink((char *) store->filename_buf.data) == -1) {
                fv_log("Error deleting “%s”: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
        }
}

static void
free_task(struct fv_store *store,
          struct fv_store_task *task)
{
        int i;

        /* This must be called with the lock */

        switch (task->type) {
        case FV_STORE_TASK_TYPE_SAVE_BLOB:
                fv_blob_unref(task->save_blob.blob);
                break;
        case FV_STORE_TASK_TYPE_LOAD_BLOB:
        case FV_STORE_TASK_TYPE_DELETE_OBJECT:
                break;
        case FV_STORE_TASK_TYPE_SAVE_ADDR_LIST:
                fv_free(task->save_addr_list.addrs);
                break;
        case FV_STORE_TASK_TYPE_SAVE_KEYS:
                for (i = 0; i < task->save_keys.n_keys; i++)
                        fv_key_unref(task->save_keys.keys[i]);
                fv_free(task->save_keys.keys);
                break;
        case FV_STORE_TASK_TYPE_SAVE_OUTGOINGS:
                fv_blob_unref(task->save_outgoings.blob);
                break;
        case FV_STORE_TASK_TYPE_SAVE_MESSAGE:
                fv_blob_unref(task->save_message.blob);
                if (task->save_message.from_key)
                        fv_key_unref(task->save_message.from_key);
                fv_key_unref(task->save_message.to_key);
                break;
        case FV_STORE_TASK_TYPE_SAVE_MESSAGE_CONTENT:
                fv_blob_unref(task->save_message_content.blob);
                break;
        case FV_STORE_TASK_TYPE_LOAD_MESSAGE_CONTENT:
                break;
        case FV_STORE_TASK_TYPE_DELETE_MESSAGE_CONTENT:
                break;
        }

        fv_slice_free(&store->allocator, task);
}

static void *
store_thread_func(void *user_data)
{
        struct fv_store *store = user_data;
        struct fv_store_task *task;

        pthread_mutex_lock(&store->mutex);

        while (true) {
                /* Block until there is something to do */
                while (!store->quit && fv_list_empty(&store->queue))
                        pthread_cond_wait(&store->cond, &store->mutex);

                if (store->quit && fv_list_empty(&store->queue))
                        break;

                task = fv_container_of(store->queue.next,
                                        struct fv_store_task,
                                        link);
                fv_list_remove(&task->link);

                if (task->type == FV_STORE_TASK_TYPE_LOAD_BLOB) {
                        /* This special case needs to keep the lock
                         * held for part of the task */
                        handle_load_blob(store, task);
                } else if (task->type ==
                           FV_STORE_TASK_TYPE_LOAD_MESSAGE_CONTENT) {
                        /* This special case needs to keep the lock
                         * held for part of the task */
                        handle_load_message_content(store, task);
                } else {
                        pthread_mutex_unlock(&store->mutex);

                        switch (task->type) {
                        case FV_STORE_TASK_TYPE_SAVE_BLOB:
                                handle_save_blob(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_DELETE_OBJECT:
                                handle_delete_object(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_SAVE_ADDR_LIST:
                                handle_save_addr_list(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_SAVE_KEYS:
                                handle_save_keys(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_SAVE_OUTGOINGS:
                                handle_save_outgoings(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_SAVE_MESSAGE:
                                handle_save_message(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_SAVE_MESSAGE_CONTENT:
                                handle_save_message_content(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_DELETE_MESSAGE_CONTENT:
                                handle_delete_message_content(store, task);
                                break;
                        case FV_STORE_TASK_TYPE_LOAD_BLOB:
                        case FV_STORE_TASK_TYPE_LOAD_MESSAGE_CONTENT:
                                assert(false);
                                break;
                        }

                        pthread_mutex_lock(&store->mutex);
                }

                free_task(store, task);
        }

        pthread_mutex_unlock(&store->mutex);

        return NULL;
}

struct fv_store *
fv_store_new(const char *store_directory,
              const char *maildir,
              struct fv_error **error)
{
        struct fv_store *store = fv_alloc(sizeof *store);

        fv_list_init(&store->queue);
        store->quit = false;
        store->started = false;
        store->num_stored_messages = 0;

        fv_buffer_init(&store->filename_buf);
        fv_buffer_init(&store->maildir_buf);

        if (!init_store_directory(store, store_directory, error))
                goto error;

        if (!init_maildir(store, maildir, error))
                goto error;

        pthread_mutex_init(&store->mutex, NULL /* attrs */);
        pthread_cond_init(&store->cond, NULL /* attrs */);

        fv_slice_allocator_init(&store->allocator,
                                 sizeof (struct fv_store_task),
                                 FV_ALIGNOF(struct fv_store_task));
        fv_buffer_init(&store->tmp_buf);

        return store;

error:
        fv_buffer_destroy(&store->maildir_buf);
        fv_buffer_destroy(&store->filename_buf);
        fv_free(store);
        return NULL;
}

const char *
fv_store_get_directory(struct fv_store *store)
{
        fv_buffer_set_length(&store->filename_buf, store->directory_len);
        fv_buffer_append_c(&store->filename_buf, '\0');

        return (const char *) store->filename_buf.data;
}

void
fv_store_start(struct fv_store *store)
{
        if (store->started)
                return;

        store->thread = fv_create_thread(store_thread_func, store);
        store->started = true;
}

static struct fv_store_task *
new_task(struct fv_store *store,
         enum fv_store_task_type type)
{
        struct fv_store_task *task;

        /* This should only be called while the mutex is held */

        task = fv_slice_alloc(&store->allocator);
        task->type = type;
        fv_list_insert(store->queue.prev, &task->link);

        pthread_cond_signal(&store->cond);

        return task;
}

void
fv_store_save_blob(struct fv_store *store,
                    const uint8_t *hash,
                    struct fv_blob *blob)
{
        struct fv_store_task *task;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_SAVE_BLOB);
        memcpy(task->save_blob.hash, hash, FV_PROTO_HASH_LENGTH);
        task->save_blob.blob = fv_blob_ref(blob);

        pthread_mutex_unlock(&store->mutex);
}

void
fv_store_delete_object(struct fv_store *store,
                        const uint8_t *hash)
{
        struct fv_store_task *task;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_DELETE_OBJECT);
        memcpy(task->delete_object.hash, hash, FV_PROTO_HASH_LENGTH);

        pthread_mutex_unlock(&store->mutex);
}

void
fv_store_save_message(struct fv_store *store,
                       int64_t timestamp,
                       struct fv_key *from_key,
                       const char *from_address,
                       struct fv_key *to_key,
                       struct fv_blob *blob)
{
        struct fv_store_task *task;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_SAVE_MESSAGE);

        task->save_message.timestamp = timestamp;
        if (from_key)
                task->save_message.from_key = fv_key_ref(from_key);
        else
                task->save_message.from_key = NULL;
        strcpy(task->save_message.from_address, from_address);
        task->save_message.to_key = fv_key_ref(to_key);
        task->save_message.blob = fv_blob_ref(blob);

        pthread_mutex_unlock(&store->mutex);
}

void
fv_store_save_message_content(struct fv_store *store,
                               uint64_t content_id,
                               struct fv_blob *blob)
{
        struct fv_store_task *task;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_SAVE_MESSAGE_CONTENT);

        task->save_message_content.id = content_id;
        task->save_message_content.blob = fv_blob_ref(blob);

        pthread_mutex_unlock(&store->mutex);
}

struct fv_store_cookie *
fv_store_load_message_content(struct fv_store *store,
                               uint64_t content_id,
                               fv_store_load_callback func,
                               void *user_data)
{
        struct fv_store_task *task;
        struct fv_store_cookie *cookie;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_LOAD_MESSAGE_CONTENT);
        task->load_message_content.id = content_id;

        cookie = fv_slice_alloc(&fv_store_cookie_allocator);
        cookie->store = store;
        cookie->blob = NULL;
        cookie->task = task;
        cookie->idle_source = NULL;
        cookie->func = func;
        cookie->user_data = user_data;

        task->load_message_content.cookie = cookie;

        pthread_mutex_unlock(&store->mutex);

        return cookie;
}

void
fv_store_delete_message_content(struct fv_store *store,
                                 uint64_t content_id)
{
        struct fv_store_task *task;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_DELETE_MESSAGE_CONTENT);

        task->delete_message_content.id = content_id;

        pthread_mutex_unlock(&store->mutex);
}

void
fv_store_save_addr_list(struct fv_store *store,
                         struct fv_store_addr *addrs,
                         int n_addrs)
{
        struct fv_store_task *task;

        /* This function takes ownership of the addrs array */

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_SAVE_ADDR_LIST);

        task->save_addr_list.addrs = addrs;
        task->save_addr_list.n_addrs = n_addrs;

        pthread_mutex_unlock(&store->mutex);
}

void
fv_store_save_keys(struct fv_store *store,
                    struct fv_key * const *keys,
                    int n_keys)
{
        struct fv_store_task *task;
        int i;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_SAVE_KEYS);

        task->save_keys.keys = fv_alloc(sizeof (struct fv_key *) * n_keys);

        for (i = 0; i < n_keys; i++)
                task->save_keys.keys[i] = fv_key_ref(keys[i]);

        task->save_keys.n_keys = n_keys;

        pthread_mutex_unlock(&store->mutex);
}

void
fv_store_save_outgoings(struct fv_store *store,
                         struct fv_blob *blob)
{
        struct fv_store_task *task;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_SAVE_OUTGOINGS);

        task->save_outgoings.blob = fv_blob_ref(blob);

        pthread_mutex_unlock(&store->mutex);
}

static int
hex_digit_value(int ch)
{
        if (ch >= 'a')
                return ch - 'a' + 10;
        if (ch >= 'A')
                return ch - 'A' + 10;

        return ch - '0';
}

static bool
is_hex_digit(int ch)
{
        return ((ch >= 'a' && ch <= 'f') ||
                (ch >= 'A' && ch <= 'F') ||
                (ch >= '0' && ch <= '9'));
}

static void
process_file(struct fv_store *store,
             const char *filename,
             for_each_blob_internal_func func,
             void *user_data)
{
        uint8_t hash[FV_PROTO_HASH_LENGTH];
        uint8_t buf[sizeof (uint32_t) + sizeof (uint64_t) * 2];
        uint32_t type;
        int64_t timestamp;
        FILE *file;
        const char *p;
        const uint8_t *buf_ptr;
        uint32_t length;
        int64_t now;
        int i;

        p = filename + store->directory_len + 8;

        for (i = 0; i < FV_PROTO_HASH_LENGTH; i++) {
                /* Skip files that don't look like a hash */
                if (!is_hex_digit(p[0]) ||
                    !is_hex_digit(p[1]))
                        return;

                hash[i] = ((hex_digit_value(p[0]) << 4) |
                           hex_digit_value(p[1]));
                p += 2;
        }

        /* Delete any temporary files. These could be left around if
         * notbit crashes while writing a file */
        if (!strcmp(p, ".tmp")) {
                if (unlink(filename) == -1)
                        fv_log("Error deleting %s: %s",
                                filename,
                                strerror(errno));
                return;
        } else if (p[0] != '\0') {
                return;
        }

        file = fopen(filename, "rb");
        if (file == NULL) {
                fv_log("Error reading %s: %s",
                        filename,
                        strerror(errno));
                return;
        }

        /* All of the files should start with a 32-bit type, the
         * 64-bit nonce and then either a 32-bit or 64-bit timestamp.
         * We only need the type and timestamp so we don't need to
         * read the rest */
        if (!read_all(filename, buf, sizeof buf, file)) {
                fclose(file);
                return;
        }

        now = fv_main_context_get_wall_clock(NULL);

        type = fv_proto_get_32(buf);
        buf_ptr = buf + sizeof (uint32_t) + sizeof (uint64_t);
        length = sizeof (uint64_t);
        fv_proto_get_timestamp(&buf_ptr, &length, &timestamp);

        if (now - timestamp >= (fv_proto_get_max_age_for_type(type) +
                                FV_PROTO_EXTRA_AGE)) {
                if (unlink(filename) == -1)
                        fv_log("Error deleting %s: %s",
                                filename,
                                strerror(errno));
        } else {
                func(type, hash, timestamp, filename, file, user_data);
        }

        fclose(file);
}

static void
for_each_blob_internal(struct fv_store *store,
                       for_each_blob_internal_func func,
                       void *user_data)
{
        DIR *dir;
        struct dirent *dirent;

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf, "objects");

        dir = opendir((char *) store->filename_buf.data);
        if (dir == NULL) {
                fv_log("Error listing %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                return;
        }

        fv_buffer_append_c(&store->filename_buf, '/');

        while ((dirent = readdir(dir))) {
                store->filename_buf.length = store->directory_len + 8;
                fv_buffer_append_string(&store->filename_buf, dirent->d_name);

                process_file(store,
                             (char *) store->filename_buf.data,
                             func,
                             user_data);
        }

        closedir(dir);
}

struct for_each_blob_data {
        fv_store_for_each_blob_func func;
        void *user_data;
};

static void
for_each_blob_cb(enum fv_proto_inv_type type,
                 const uint8_t *hash,
                 int64_t timestamp,
                 const char *filename,
                 FILE *file,
                 void *user_data)
{
        struct for_each_blob_data *data = user_data;

        data->func(type, hash, timestamp, data->user_data);
}

void
fv_store_for_each_blob(struct fv_store *store,
                        fv_store_for_each_blob_func func,
                        void *user_data)
{
        struct for_each_blob_data data;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        /* This function runs synchronously but it should only be
         * called once at startup before connecting to any peers so it
         * shouldn't really matter */

        fv_log("Loading saved object store");

        data.func = func;
        data.user_data = user_data;

        for_each_blob_internal(store, for_each_blob_cb, &data);

        fv_log("Finished loading object store");
}

struct for_each_pubkey_blob_data {
        fv_store_for_each_pubkey_blob_func func;
        void *user_data;
};

static void
for_each_pubkey_blob_cb(enum fv_proto_inv_type type,
                        const uint8_t *hash,
                        int64_t timestamp,
                        const char *filename,
                        FILE *file,
                        void *user_data)
{
        struct for_each_pubkey_blob_data *data = user_data;
        struct stat statbuf;
        struct fv_blob *blob;

        if (type != FV_PROTO_INV_TYPE_PUBKEY)
                return;

        /* Reset the file to just after the type */
        if (fseek(file, sizeof (uint32_t), SEEK_SET))
                return;

        if (fstat(fileno(file), &statbuf) == -1)
                return;

        blob = fv_blob_new(FV_PROTO_INV_TYPE_PUBKEY,
                            NULL, /* data */
                            statbuf.st_size - sizeof (uint32_t));

        if (read_all(filename,
                     blob->data,
                     statbuf.st_size - sizeof (uint32_t),
                     file))
                data->func(hash, timestamp, blob, data->user_data);

        fv_blob_unref(blob);
}

void
fv_store_for_each_pubkey_blob(struct fv_store *store,
                               fv_store_for_each_pubkey_blob_func func,
                               void *user_data)
{
        struct for_each_pubkey_blob_data data;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        /* This function runs synchronously but it should only be
         * called once at startup before connecting to any peers so it
         * shouldn't really matter */

        fv_log("Loading pubkey objects");

        data.func = func;
        data.user_data = user_data;

        for_each_blob_internal(store, for_each_pubkey_blob_cb, &data);

        fv_log("Finished loading pubkey objects");
}

static void
process_addr_line(struct fv_store *store,
                  char *line,
                  fv_store_for_each_addr_func func,
                  void *user_data)
{
        struct fv_store_addr addr;
        int address_length;
        char *tail;

        addr.timestamp = strtoll(line, &tail, 10);

        if (tail == line || *tail != ',')
                return;

        line = tail + 1;
        addr.stream = strtoul(line, &tail, 10);

        if (tail == line || *tail != ',')
                return;

        line = tail + 1;
        addr.services = strtoull(line, &tail, 10);

        if (tail == line || *tail != ',')
                return;

        line = tail + 1;
        address_length = strlen(line);

        if (address_length > 0 && line[address_length - 1] == '\n')
                line[--address_length] = '\0';

        if (!fv_netaddress_from_string(&addr.address,
                                        line,
                                        FV_PROTO_DEFAULT_PORT))
                return;

        func(&addr, user_data);
}

void
fv_store_for_each_addr(struct fv_store *store,
                        fv_store_for_each_addr_func func,
                        void *user_data)
{
        FILE *file;
        char line[1024];

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        /* This function runs synchronously but it should only be
         * called once at startup before connecting to any peers so it
         * shouldn't really matter */

        fv_log("Loading saved address list");

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf, "addr-list.txt");

        file = fopen((char *) store->filename_buf.data, "r");

        if (file == NULL) {
                if (errno != ENOENT)
                        fv_log("Error opening %s: %s",
                                (char *) store->filename_buf.data,
                                strerror(errno));
                return;
        }

        while(fgets(line, sizeof line, file))
                process_addr_line(store, line, func, user_data);

        fclose(file);
}

void
fv_store_for_each_key(struct fv_store *store,
                       fv_store_for_each_key_func func,
                       void *user_data)
{
        FILE *file;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        /* This function runs synchronously but it should only be
         * called once at startup before connecting to any peers so it
         * shouldn't really matter */

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf, "keys.dat");

        file = fopen((char *) store->filename_buf.data, "r");

        if (file == NULL) {
                if (errno != ENOENT)
                        fv_log("Error opening %s: %s",
                                (char *) store->filename_buf.data,
                                strerror(errno));
                return;
        }

        fv_load_keys(file, (fv_store_for_each_key_func) func, user_data);

        fclose(file);
}

struct for_each_outgoing_data {
        fv_store_for_each_outgoing_func func;
        void *user_data;
        struct fv_buffer used_content_ids;
};

static void
for_each_outgoing_cb(const struct fv_store_outgoing *outgoing,
                     void *user_data)
{
        struct for_each_outgoing_data *data = user_data;

        data->func(outgoing, data->user_data);

        fv_buffer_append(&data->used_content_ids,
                          &outgoing->content_id,
                          sizeof outgoing->content_id);
}

static void
maybe_delete_outgoing(struct fv_store *store,
                      const char *filename,
                      const uint64_t *used_content_ids,
                      size_t n_used_content_ids)
{
        const char *bn = filename + store->directory_len + 9;
        long long int content_id_ll;
        uint64_t content_id;
        char *tail;
        size_t i;

        errno = 0;
        content_id_ll = strtoll(bn, &tail, 16);

        /* Don't delete the file if the name doesn't look like a
         * hexadecimal number */
        if (errno ||
            tail == bn ||
            content_id_ll < 0 ||
            content_id_ll > UINT64_MAX)
                return;

        content_id = content_id_ll;

        if (*tail) {
                /* Don't delete the file unless it is a temporary file
                 * that we probably created */
                if (strcmp(tail, ".tmp"))
                        return;
        } else {
                /* Don't delete the file if it is in use */
                for (i = 0; i < n_used_content_ids; i++)
                        if (used_content_ids[i] == content_id)
                                return;
        }

        if (unlink(filename) == -1)
                fv_log("Error deleting %s: %s",
                        filename,
                        strerror(errno));
}

static void
delete_unused_outgoings(struct fv_store *store,
                        const uint64_t *used_content_ids,
                        size_t n_used_content_ids)
{
        DIR *dir;
        struct dirent *dirent;

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf, "outgoing");

        dir = opendir((char *) store->filename_buf.data);
        if (dir == NULL) {
                fv_log("Error listing %s: %s",
                        (char *) store->filename_buf.data,
                        strerror(errno));
                return;
        }

        fv_buffer_append_c(&store->filename_buf, '/');

        while ((dirent = readdir(dir))) {
                store->filename_buf.length = store->directory_len + 9;
                fv_buffer_append_string(&store->filename_buf, dirent->d_name);

                maybe_delete_outgoing(store,
                                      (char *) store->filename_buf.data,
                                      used_content_ids,
                                      n_used_content_ids);
        }

        closedir(dir);
}

void
fv_store_for_each_outgoing(struct fv_store *store,
                            fv_store_for_each_outgoing_func func,
                            void *user_data)
{
        struct for_each_outgoing_data data;
        FILE *file;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        /* This function runs synchronously but it should only be
         * called once at startup before connecting to any peers so it
         * shouldn't really matter */

        store->filename_buf.length = store->directory_len;
        fv_buffer_append_string(&store->filename_buf, "outgoing-messages.dat");

        file = fopen((char *) store->filename_buf.data, "r");

        if (file == NULL) {
                if (errno != ENOENT)
                        fv_log("Error opening %s: %s",
                                (char *) store->filename_buf.data,
                                strerror(errno));
                return;
        }

        data.func = func;
        data.user_data = user_data;
        fv_buffer_init(&data.used_content_ids);

        fv_load_outgoings(file, for_each_outgoing_cb, &data);

        delete_unused_outgoings(store,
                                (uint64_t *) data.used_content_ids.data,
                                data.used_content_ids.size / sizeof (uint64_t));

        fv_buffer_destroy(&data.used_content_ids);

        fclose(file);
}

struct fv_store_cookie *
fv_store_load_blob(struct fv_store *store,
                    const uint8_t *hash,
                    fv_store_load_callback func,
                    void *user_data)
{
        struct fv_store_task *task;
        struct fv_store_cookie *cookie;

        if (store == NULL)
                store = fv_store_get_default_or_abort();

        pthread_mutex_lock(&store->mutex);

        task = new_task(store, FV_STORE_TASK_TYPE_LOAD_BLOB);
        memcpy(task->load_blob.hash, hash, FV_PROTO_HASH_LENGTH);

        cookie = fv_slice_alloc(&fv_store_cookie_allocator);
        cookie->store = store;
        cookie->blob = NULL;
        cookie->task = task;
        cookie->idle_source = NULL;
        cookie->func = func;
        cookie->user_data = user_data;

        task->load_blob.cookie = cookie;

        pthread_mutex_unlock(&store->mutex);

        return cookie;
}

void
fv_store_cancel_task(struct fv_store_cookie *cookie)
{
        struct fv_store *store = cookie->store;

        pthread_mutex_lock(&store->mutex);

        if (cookie->task) {
                switch (cookie->task->type) {
                case FV_STORE_TASK_TYPE_LOAD_BLOB:
                        cookie->task->load_blob.cookie = NULL;
                        break;
                case FV_STORE_TASK_TYPE_LOAD_MESSAGE_CONTENT:
                        cookie->task->load_message_content.cookie = NULL;
                        break;
                case FV_STORE_TASK_TYPE_SAVE_BLOB:
                case FV_STORE_TASK_TYPE_SAVE_ADDR_LIST:
                case FV_STORE_TASK_TYPE_SAVE_KEYS:
                case FV_STORE_TASK_TYPE_SAVE_OUTGOINGS:
                case FV_STORE_TASK_TYPE_SAVE_MESSAGE:
                case FV_STORE_TASK_TYPE_SAVE_MESSAGE_CONTENT:
                case FV_STORE_TASK_TYPE_DELETE_MESSAGE_CONTENT:
                case FV_STORE_TASK_TYPE_DELETE_OBJECT:
                        assert(false);
                        break;
                }
        }
        if (cookie->idle_source)
                fv_main_context_remove_source(cookie->idle_source);
        if (cookie->blob)
                fv_blob_unref(cookie->blob);

        pthread_mutex_unlock(&store->mutex);

        fv_slice_free(&fv_store_cookie_allocator, cookie);
}

void
fv_store_free(struct fv_store *store)
{
        struct fv_store_task *task, *tmp;

        if (store->started) {
                pthread_mutex_lock(&store->mutex);
                store->quit = true;
                pthread_cond_signal(&store->cond);
                pthread_mutex_unlock(&store->mutex);
                pthread_join(store->thread, NULL);
        }

        fv_list_for_each_safe(task, tmp, &store->queue, link)
                free_task(store, task);

        fv_buffer_destroy(&store->maildir_buf);
        fv_buffer_destroy(&store->tmp_buf);
        fv_buffer_destroy(&store->filename_buf);

        fv_slice_allocator_destroy(&store->allocator);

        fv_free(store);

        if (fv_store_default == store)
                fv_store_default = NULL;
}
