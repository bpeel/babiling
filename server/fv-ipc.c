/*
 * Finvenkisto
 * Copyright (C) 2014  Neil Roberts
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

#include <stdlib.h>
#include <sys/socket.h>
#include <string.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/un.h>
#include <assert.h>
#include <fcntl.h>
#include <sys/file.h>

#include "fv-ipc.h"
#include "fv-ipc-proto.h"
#include "fv-ipc-sockaddr.h"
#include "fv-file-error.h"
#include "fv-util.h"
#include "fv-list.h"
#include "fv-main-context.h"
#include "fv-log.h"
#include "fv-buffer.h"
#include "fv-proto.h"
#include "fv-socket.h"
#include "fv-mail-parser.h"
#include "fv-blob.h"
#include "fv-mkdir.h"

struct fv_ipc {
        int sock;
        int sock_lock;
        uid_t uid;

        char *lock_path;
        struct sockaddr *sockaddr;

        struct fv_main_context_source *listen_source;

        struct fv_keyring *keyring;

        struct fv_list connections;
};

struct fv_ipc_connection {
        struct fv_ipc *ipc;

        int sock;
        struct fv_main_context_source *source;

        /* This becomes TRUE when the client has closed its end of the
           connection */
        bool read_finished;
        /* This becomes TRUE when we've stopped writing data. This will only
           happen after the client closes its connection */
        bool write_finished;

        struct fv_buffer outbuf;
        struct fv_buffer inbuf;
        struct fv_buffer fd_queue;

        struct fv_list emails;
        struct fv_list tasks;

        struct fv_list link;

        /* Used for building up a response command */
        size_t response_old_length;
};

/* This represents an email that is being read from a file descriptor
   passed in the email command */
struct fv_ipc_email {
        struct fv_ipc_connection *conn;

        struct fv_address from_address;
        struct fv_buffer to_addresses;
        struct fv_buffer content;
        int content_encoding;

        struct fv_main_context_source *source;
        int fd;
        int request_id;

        struct fv_mail_parser *parser;

        struct fv_list link;
};

struct fv_ipc_task {
        struct fv_ipc_connection *conn;

        struct fv_keyring_cookie *keyring_cookie;

        struct fv_list link;

        int request_id;
};

struct fv_ipc_command {
        const char *name;
        bool (* func)(struct fv_ipc_connection *conn,
                      uint32_t request_id,
                      const uint8_t *data,
                      uint32_t command_length);
};

static FV_PRINTF_FORMAT(4, 5) bool
send_response(struct fv_ipc_connection *conn,
              uint32_t request_id,
              enum fv_ipc_proto_status status,
              const char *format,
              ...);

static void
close_fd_queue(struct fv_ipc_connection *conn)
{
        const int *fds = (const int *) conn->fd_queue.data;
        int i;

        for (i = 0; i < conn->fd_queue.length / sizeof (int); i++)
                close(fds[i]);

        conn->fd_queue.length = 0;
}

static void
remove_email(struct fv_ipc_email *email)
{
        fv_buffer_destroy(&email->to_addresses);
        fv_buffer_destroy(&email->content);

        fv_mail_parser_free(email->parser);
        fv_main_context_remove_source(email->source);
        close(email->fd);
        fv_list_remove(&email->link);
        fv_free(email);
}

static void
remove_emails(struct fv_ipc_connection *conn)
{
        struct fv_ipc_email *email, *tmp;

        fv_list_for_each_safe(email, tmp, &conn->emails, link)
                remove_email(email);
}

static void
cancel_tasks(struct fv_ipc_connection *conn)
{
        struct fv_ipc_task *task, *tmp;

        fv_list_for_each_safe(task, tmp, &conn->tasks, link) {
                fv_keyring_cancel_task(task->keyring_cookie);
                fv_free(task);
        }
}

static void
remove_connection(struct fv_ipc_connection *conn)
{
        remove_emails(conn);
        cancel_tasks(conn);
        close_fd_queue(conn);
        fv_buffer_destroy(&conn->fd_queue);
        fv_buffer_destroy(&conn->inbuf);
        fv_buffer_destroy(&conn->outbuf);
        fv_main_context_remove_source(conn->source);
        close(conn->sock);
        fv_list_remove(&conn->link);
        fv_free(conn);
}

static bool
update_poll(struct fv_ipc_connection *conn)
{
        enum fv_main_context_poll_flags flags = 0;

        if (!conn->read_finished)
                flags |= FV_MAIN_CONTEXT_POLL_IN;

        /* Shutdown the socket if we've finished writing */
        if (!conn->write_finished &&
            conn->read_finished &&
            conn->outbuf.length == 0 &&
            fv_list_empty(&conn->emails) &&
            fv_list_empty(&conn->tasks)) {
                if (shutdown(conn->sock, SHUT_WR) == -1) {
                        fv_log("shutdown for IPC connection failed: %s",
                                strerror(errno));
                        remove_connection(conn);
                        return false;
                }

                conn->write_finished = true;
        }

        if (!conn->write_finished && conn->outbuf.length > 0)
                flags |= FV_MAIN_CONTEXT_POLL_OUT;

        /* If both ends of the conn are closed then we can abandon
           this connection */
        if (conn->read_finished && conn->write_finished) {
                remove_connection(conn);
                return false;
        }

        fv_main_context_modify_poll(conn->source, flags);

        return true;
}

static bool
process_control_data(struct fv_ipc_connection *conn,
                     struct msghdr *msg)
{
        struct cmsghdr *cmsg;

        for (cmsg = CMSG_FIRSTHDR(msg);
             cmsg != NULL;
             cmsg = CMSG_NXTHDR(msg, cmsg)) {
                if (cmsg->cmsg_level != SOL_SOCKET ||
                    cmsg->cmsg_type != SCM_RIGHTS)
                        continue;

                fv_buffer_append(&conn->fd_queue,
                                  CMSG_DATA(cmsg),
                                  cmsg->cmsg_len - CMSG_LEN(0));
        }

        return true;
}

static void
begin_send_response(struct fv_ipc_connection *conn,
                    uint32_t request_id,
                    enum fv_ipc_proto_status status)
{
        conn->response_old_length = conn->outbuf.length;

        fv_ipc_proto_begin_command(&conn->outbuf, "response", request_id);

        fv_proto_add_32(&conn->outbuf, status);
}

static bool
end_send_response(struct fv_ipc_connection *conn)
{
        fv_ipc_proto_end_command(&conn->outbuf, conn->response_old_length);

        return update_poll(conn);
}

static bool
send_response(struct fv_ipc_connection *conn,
              uint32_t request_id,
              enum fv_ipc_proto_status status,
              const char *format,
              ...)
{
        va_list ap;

        begin_send_response(conn, request_id, status);

        va_start(ap, format);
        fv_buffer_append_vprintf(&conn->outbuf, format, ap);
        va_end(ap);

        return end_send_response(conn);
}

static void
send_email(struct fv_ipc_email *email)
{
        struct fv_ipc_connection *conn = email->conn;
        struct fv_ipc *ipc = conn->ipc;
        struct fv_error *error = NULL;
        struct fv_blob *content;
        enum fv_ipc_proto_status status;
        bool res;

        content = fv_blob_dynamic_end(&email->content);
        fv_buffer_init(&email->content);

        res = fv_keyring_send_message(ipc->keyring,
                                       &email->from_address,
                                       (const struct fv_address *)
                                       email->to_addresses.data,
                                       email->to_addresses.length /
                                       sizeof (struct fv_address),
                                       email->content_encoding,
                                       content,
                                       &error);

        fv_blob_unref(content);

        if (res) {
                begin_send_response(conn,
                                    email->request_id,
                                    FV_IPC_PROTO_STATUS_SUCCESS);
                end_send_response(conn);
        } else {
                if (error->domain == &fv_keyring_error &&
                    error->code == FV_KEYRING_ERROR_UNKNOWN_FROM_ADDRESS)
                        status = FV_IPC_PROTO_STATUS_UNKNOWN_FROM_ADDRESS;
                else
                        status = FV_IPC_PROTO_STATUS_GENERIC_ERROR;

                send_response(conn,
                              email->request_id,
                              status,
                              "%s",
                              error->message);

                fv_error_free(error);
        }
}

static void
email_poll_cb(struct fv_main_context_source *source,
              int fd,
              enum fv_main_context_poll_flags flags,
              void *user_data)
{
        struct fv_ipc_email *email = user_data;
        struct fv_ipc_connection *conn = email->conn;
        struct fv_error *error = NULL;
        uint8_t buf[512];
        ssize_t got;

        got = read(fd, buf, sizeof buf);

        if (got == -1) {
                send_response(conn,
                              email->request_id,
                              FV_IPC_PROTO_STATUS_FD_ERROR,
                              "Error reading from email file descriptor");
                remove_email(email);
        } else if (got == 0) {
                if (fv_mail_parser_end(email->parser, &error)) {
                        send_email(email);
                } else {
                        send_response(conn,
                                      email->request_id,
                                      FV_IPC_PROTO_STATUS_INVALID_EMAIL,
                                      "%s",
                                      error->message);
                        fv_error_free(error);
                }
                remove_email(email);
        } else if (!fv_mail_parser_parse(email->parser,
                                          buf,
                                          got,
                                          &error)) {
                send_response(conn,
                              email->request_id,
                              FV_IPC_PROTO_STATUS_INVALID_EMAIL,
                              "Error parsing email: %s",
                              error->message);
                fv_error_free(error);
                remove_email(email);
        }
}

static int
get_fd(struct fv_ipc_connection *conn)
{
        int fd;

        memcpy(&fd, conn->fd_queue.data, sizeof fd);
        memmove(conn->fd_queue.data,
                conn->fd_queue.data + sizeof fd,
                conn->fd_queue.length - sizeof fd);
        conn->fd_queue.length -= sizeof fd;

        return fd;
}

static bool
mail_parser_data_cb(enum fv_mail_parser_event event,
                    const uint8_t *data,
                    size_t length,
                    void *user_data,
                    struct fv_error **error)
{
        struct fv_ipc_email *email = user_data;

        switch (event) {
        case FV_MAIL_PARSER_EVENT_SOURCE:
        case FV_MAIL_PARSER_EVENT_DESTINATION:
                assert(false);

        case FV_MAIL_PARSER_EVENT_SUBJECT:
                fv_buffer_append_string(&email->content,
                                         "Subject:");
                fv_buffer_append(&email->content, data, length);
                fv_buffer_append_string(&email->content, "\nBody:");
                email->content_encoding = 2;
                break;

        case FV_MAIL_PARSER_EVENT_CONTENT:
                fv_buffer_append(&email->content, data, length);
                break;
        }

        return true;
}

static bool
mail_parser_address_cb(enum fv_mail_parser_event event,
                       const struct fv_address *address,
                       void *user_data,
                       struct fv_error **error)
{
        struct fv_ipc_email *email = user_data;

        switch (event) {
        case FV_MAIL_PARSER_EVENT_SOURCE:
                email->from_address = *address;
                break;

        case FV_MAIL_PARSER_EVENT_DESTINATION:
                fv_buffer_append(&email->to_addresses,
                                  address,
                                  sizeof *address);
                break;

        case FV_MAIL_PARSER_EVENT_SUBJECT:
        case FV_MAIL_PARSER_EVENT_CONTENT:
                assert(false);
        }

        return true;
}

static bool
handle_email_command(struct fv_ipc_connection *conn,
                     uint32_t request_id,
                     const uint8_t *data,
                     uint32_t command_length)
{
        struct fv_ipc_email *email;
        struct fv_error *error = NULL;
        bool res;
        int fd;

        if (conn->fd_queue.length < sizeof fd)
                return send_response(conn,
                                     request_id,
                                     FV_IPC_PROTO_STATUS_INVALID_COMMAND,
                                     "email command was sent without a "
                                     "file descriptor argument");

        fd = get_fd(conn);

        if (!fv_socket_set_nonblock(fd, &error)) {
                res = send_response(conn,
                                    request_id,
                                    FV_IPC_PROTO_STATUS_FD_ERROR,
                                    "%s",
                                    error->message);
                fv_error_free(error);
                close(fd);
                return res;
        }

        email = fv_alloc(sizeof *email);
        email->conn = conn;
        email->fd = fd;
        email->request_id = request_id;
        email->source = fv_main_context_add_poll(NULL,
                                                  email->fd,
                                                  FV_MAIN_CONTEXT_POLL_IN,
                                                  email_poll_cb,
                                                  email);

        email->parser = fv_mail_parser_new(mail_parser_address_cb,
                                            mail_parser_data_cb,
                                            email);

        fv_buffer_init(&email->to_addresses);

        fv_list_insert(&conn->emails, &email->link);

        fv_blob_dynamic_init(&email->content,
                              FV_PROTO_INV_TYPE_MSG);

        email->content_encoding = 1;

        return true;
}

static void
create_key_cb(struct fv_key *key,
              void *user_data)
{
        struct fv_ipc_task *task = user_data;
        struct fv_ipc_connection *conn = task->conn;
        uint32_t request_id = task->request_id;
        char address[FV_ADDRESS_MAX_LENGTH + 1];

        fv_list_remove(&task->link);
        fv_free(task);

        fv_address_encode(&key->address, address);

        begin_send_response(conn, request_id, FV_IPC_PROTO_STATUS_SUCCESS);
        fv_proto_add_var_int(&conn->outbuf, key->address.version);
        fv_proto_add_var_int(&conn->outbuf, key->address.stream);
        fv_buffer_append(&conn->outbuf,
                          key->address.ripe,
                          RIPEMD160_DIGEST_LENGTH);
        fv_proto_add_var_str(&conn->outbuf, address);
        end_send_response(conn);
}

static bool
label_is_valid(const struct fv_proto_var_str *label_str)
{
        size_t i;

        /* The label can't contain newline characters or it will break
         * the keys.dat file. We might as well disallow all control
         * characters */
        for (i = 0; i < label_str->length; i++) {
                if ((label_str->data[i] & 0xff) < ' ')
                        return false;
        }

        return true;
}

static bool
handle_keygen_command(struct fv_ipc_connection *conn,
                      uint32_t request_id,
                      const uint8_t *data,
                      uint32_t command_length)
{
        struct fv_ipc *ipc = conn->ipc;
        struct fv_proto_var_str label_str;
        struct fv_key_params params;
        struct fv_ipc_task *task;
        uint64_t version, stream;
        uint64_t pow_per_byte;
        uint64_t pow_extra_bytes;
        ssize_t header_size;
        uint8_t zeroes;
        char *label;

        header_size = fv_proto_get_command(data,
                                            command_length,

                                            FV_PROTO_ARGUMENT_VAR_INT,
                                            &version,

                                            FV_PROTO_ARGUMENT_VAR_INT,
                                            &stream,

                                            FV_PROTO_ARGUMENT_VAR_INT,
                                            &pow_per_byte,

                                            FV_PROTO_ARGUMENT_VAR_INT,
                                            &pow_extra_bytes,

                                            FV_PROTO_ARGUMENT_8,
                                            &zeroes,

                                            FV_PROTO_ARGUMENT_VAR_STR,
                                            &label_str,

                                            FV_PROTO_ARGUMENT_END);

        if (header_size == -1) {
                return send_response(conn,
                                     request_id,
                                     FV_IPC_PROTO_STATUS_INVALID_COMMAND,
                                     "The keygen command is invalid");
        }

        if (version == 0) {
                version = 4;
        } else if (version < 2 || version > 4) {
                return send_response(conn,
                                     request_id,
                                     FV_IPC_PROTO_STATUS_UNSUPPORTED,
                                     "The requested key version is not "
                                     "supported");
        }

        if (stream != 1) {
                return send_response(conn,
                                     request_id,
                                     FV_IPC_PROTO_STATUS_UNSUPPORTED,
                                     "The requested stream is not supported");
        }

        if (pow_per_byte == 0)
                pow_per_byte = FV_PROTO_MIN_POW_PER_BYTE * 2;

        if (pow_extra_bytes == 0)
                pow_extra_bytes = FV_PROTO_MIN_POW_EXTRA_BYTES;

        if (zeroes > 2) {
                return send_response(conn,
                                     request_id,
                                     FV_IPC_PROTO_STATUS_UNSUPPORTED,
                                     "The requested number of zeroes is not "
                                     "supported");
        }

        if (!label_is_valid(&label_str)) {
                return send_response(conn,
                                     request_id,
                                     FV_IPC_PROTO_STATUS_INVALID_COMMAND,
                                     "The new key label contains "
                                     "invalid characters");
        }

        task = fv_alloc(sizeof *task);
        task->request_id = request_id;
        task->conn = conn;

        params.flags = (FV_KEY_PARAM_LABEL |
                        FV_KEY_PARAM_VERSION |
                        FV_KEY_PARAM_STREAM |
                        FV_KEY_PARAM_POW_DIFFICULTY);

        label = fv_alloc(label_str.length + 1);
        memcpy(label, label_str.data, label_str.length);
        label[label_str.length] = '\0';

        params.label = label;
        params.version = version;
        params.stream = stream;
        params.pow_per_byte = pow_per_byte;
        params.pow_extra_bytes = pow_extra_bytes;

        task->keyring_cookie = fv_keyring_create_key(ipc->keyring,
                                                      &params,
                                                      zeroes,
                                                      create_key_cb,
                                                      task);

        fv_free(label);

        fv_list_insert(&conn->tasks, &task->link);

        return true;
}

static struct fv_ipc_command
commands[] = {
        { "email", handle_email_command },
        { "keygen", handle_keygen_command }
};


static bool
process_command(struct fv_ipc_connection *conn,
                const uint8_t *data,
                uint32_t command_length)
{
        uint32_t request_id;
        int i;

        request_id = fv_proto_get_32(data + 12);

        for (i = 0; i < FV_N_ELEMENTS(commands); i++) {
                if (!strcmp((const char *) data, commands[i].name))
                        return commands[i].func(conn,
                                                request_id,
                                                data + 20,
                                                command_length);
        }

        return send_response(conn,
                             request_id,
                             FV_IPC_PROTO_STATUS_INVALID_COMMAND,
                             "Unknown command “%s”",
                             (const char *) data);
}

static void
process_commands(struct fv_ipc_connection *conn)
{
        const uint8_t *data = conn->inbuf.data;
        size_t data_length = conn->inbuf.length;
        uint32_t command_length;

        while (true) {
                if (data_length < 20)
                        break;

                command_length = fv_proto_get_32(data + 16);

                if (data_length < command_length + 20)
                        break;

                if (!fv_proto_check_command_string(data)) {
                       fv_log("Invalid data received from IPC connection\n");
                       remove_connection(conn);
                       return;
                }

                if (!process_command(conn, data, command_length))
                        return;

                data += command_length + 20;
                data_length -= command_length + 20;
         }

        /* If there are remaining file descriptors then there must
         * have been some for a command that we didn't understand.
         * We'll just close them in order to prevent a leak */
        if (conn->inbuf.length == 0)
                close_fd_queue(conn);
}

static void
handle_read(struct fv_ipc_connection *conn)
{
        ssize_t got;
        struct msghdr msg;
        struct iovec iov;
        uint8_t control_buf[512];

        fv_buffer_ensure_size(&conn->inbuf, conn->inbuf.length + 128);

        iov.iov_base = conn->inbuf.data + conn->inbuf.length;
        iov.iov_len = conn->inbuf.size - conn->inbuf.length;

        msg.msg_name = NULL;
        msg.msg_namelen = 0;
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control_buf;
        msg.msg_controllen = sizeof control_buf;
        msg.msg_flags = 0;

        got = recvmsg(conn->sock, &msg, MSG_DONTWAIT);

        if (got == -1) {
                if (errno != EINTR && errno != EWOULDBLOCK && errno != EAGAIN) {
                        fv_log("Error reading from IPC socket: %s",
                                strerror(errno));
                        remove_connection(conn);
                }
        } else {
                if (!process_control_data(conn, &msg))
                        return;

                if (got == 0) {
                        conn->read_finished = true;

                        update_poll(conn);
                } else {
                        conn->inbuf.length += got;
                        process_commands(conn);
                }
        }
}

static void
handle_write(struct fv_ipc_connection *conn)
{
        ssize_t wrote;

        wrote = send(conn->sock,
                     conn->outbuf.data,
                     conn->outbuf.length,
                     MSG_DONTWAIT);

        if (wrote == -1) {
                if (errno != EINTR && errno != EAGAIN && errno != EWOULDBLOCK) {
                        fv_log("Error writing to IPC socket: %s",
                                strerror(errno));
                        remove_connection(conn);
                }
        } else {
                /* Move any remaining data in the output buffer to the front */
                memmove(conn->outbuf.data,
                        conn->outbuf.data + wrote,
                        conn->outbuf.length - wrote);
                conn->outbuf.length -= wrote;

                update_poll(conn);
        }
}

static void
connection_source_cb(struct fv_main_context_source *source,
                     int fd,
                     enum fv_main_context_poll_flags flags,
                     void *user_data)
{
        struct fv_ipc_connection *conn = user_data;

        if (flags & FV_MAIN_CONTEXT_POLL_ERROR) {
                int value;
                unsigned int value_len = sizeof(value);

                if (getsockopt(conn->sock,
                               SOL_SOCKET,
                               SO_ERROR,
                               &value,
                               &value_len) == -1 ||
                    value_len != sizeof(value) ||
                    value == 0) {
                        fv_log("Unknown error on IPC socket");
                } else {
                        fv_log("Error on IPC socket: %s",
                                strerror(value));
                }

                remove_connection(conn);
        } else if (flags & FV_MAIN_CONTEXT_POLL_IN) {
                handle_read(conn);
        } else if (flags & FV_MAIN_CONTEXT_POLL_OUT) {
                handle_write(conn);
        }
}

#if defined(HAVE_GETPEEREID)

static bool
get_peer_uid(int sock,
             uid_t *uid)
{
        uid_t uid_result;
        gid_t gid_result;

        if (getpeereid(sock, &uid_result, &gid_result) == -1)
                return false;

        *uid = uid_result;

        return true;
}

#elif defined(SO_PEERCRED)

static bool
get_peer_uid(int sock,
             uid_t *uid)
{
        struct ucred ucred;
        socklen_t socklen = sizeof ucred;
        int res;

        res = getsockopt(sock, SOL_SOCKET, SO_PEERCRED, &ucred, &socklen);
        if (res == -1)
                return false;

        *uid = ucred.uid;

        return true;
}

#else

#error No method for getting peer credentials is defined

#endif /* HAVE_GETPEEREID */

static bool
check_credentials(struct fv_ipc *ipc,
                  int sock)
{
        uid_t uid;

        if (!get_peer_uid(sock, &uid)) {
                fv_log("Error getting credentials for IPC connection: %s",
                        strerror(errno));
                return false;
        }


        if (uid != 0 && uid != ipc->uid) {
                fv_log("Rejecting IPC connection from unauthorized user %i",
                        uid);
                return false;
        }

        return true;
}

static void
listen_source_cb(struct fv_main_context_source *source,
                 int fd,
                 enum fv_main_context_poll_flags flags,
                 void *user_data)
{
        struct fv_ipc *ipc = user_data;
        struct fv_ipc_connection *conn;
        struct fv_error *error = NULL;
        int sock;

        sock = accept(fd, NULL, 0);

        if (sock == -1) {
                fv_log("Error accepting IPC connection: %s",
                        strerror(errno));
                fv_main_context_remove_source(source);
                ipc->listen_source = NULL;
                return;
        }

        if (!fv_socket_set_nonblock(sock, &error)) {
                fv_log("Error accepting IPC connection: %s",
                        error->message);
                close(sock);
                fv_error_free(error);
                return;
        }

        if (!check_credentials(ipc, sock)) {
                close(sock);
                return;
        }

        conn = fv_alloc(sizeof *conn);
        conn->sock = sock;
        conn->ipc = ipc;

        conn->read_finished = false;
        conn->write_finished = false;

        fv_buffer_init(&conn->inbuf);
        fv_buffer_init(&conn->outbuf);
        fv_buffer_init(&conn->fd_queue);

        fv_list_init(&conn->emails);
        fv_list_init(&conn->tasks);

        conn->source = fv_main_context_add_poll(NULL,
                                                 sock,
                                                 FV_MAIN_CONTEXT_POLL_IN,
                                                 connection_source_cb,
                                                 conn);

        fv_list_insert(&ipc->connections, &conn->link);
}

static int
create_socket_lock(const char *lock_path,
                   struct fv_error **error)
{
        int errnum;
        int fd, res;

        fd = open(lock_path, O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
        errnum = errno;

        if (fd == -1) {
                fv_file_error_set(error,
                                   errnum,
                                   "Error creating IPC lock file: %s",
                                   strerror(errno));
                return -1;
        }

        res = flock(fd, LOCK_EX | LOCK_NB);

        if (res == -1) {
                if (errno == EWOULDBLOCK) {
                        fv_file_error_set(error,
                                           EWOULDBLOCK,
                                           "Notbit is already running");
                } else {
                        fv_file_error_set(error,
                                           errno,
                                           "Error creating IPC lock: %s",
                                           strerror(errno));
                }

                fv_close(fd);

                return -1;
        }

        return fd;
}

static bool
create_ipc_directory(const char *sock_path,
                     struct fv_error **error)
{
        struct fv_buffer buffer;
        int sock_path_len = strlen(sock_path);
        bool res;

        fv_buffer_init(&buffer);

        while (sock_path_len > 0 && sock_path[sock_path_len - 1] != '/')
                sock_path_len--;

        fv_buffer_append(&buffer, sock_path, sock_path_len);

        res = fv_mkdir_hierarchy(&buffer, error);

        fv_buffer_destroy(&buffer);

        return res;
}

struct fv_ipc *
fv_ipc_new(struct fv_keyring *keyring,
            struct fv_error **error)
{
        struct fv_ipc *ipc;
        char *lock_path = NULL;
        struct sockaddr *sockaddr = NULL;
        socklen_t sockaddr_len;
        const char *sockaddr_path;
        int sock = -1;
        int sock_lock = -1;
        int res;

        fv_ipc_sockaddr_create(&sockaddr, &sockaddr_len);

        sockaddr_path = ((struct sockaddr_un *) sockaddr)->sun_path;

        if (!create_ipc_directory(sockaddr_path, error))
                goto error;

        lock_path = fv_strconcat(sockaddr_path, ".lock", NULL);

        sock_lock = create_socket_lock(lock_path, error);

        if (sock_lock == -1)
                goto error;

        sock = socket(PF_LOCAL, SOCK_STREAM, 0);

        if (sock == -1) {
                fv_file_error_set(error,
                                   errno,
                                   "Failed to create socket: %s",
                                   strerror(errno));
                goto error;
        }

        res = unlink(sockaddr_path);

        if (res == -1 && errno != ENOENT) {
                fv_file_error_set(error,
                                   errno,
                                   "Error deleting IPC socket %s: %s",
                                   sockaddr_path,
                                   strerror(errno));
                goto error;
        }

        res = bind(sock, (struct sockaddr *) sockaddr, sockaddr_len);

        if (res == -1) {
                fv_file_error_set(error,
                                   errno,
                                   "Failed to bind IPC socket %s: %s",
                                   sockaddr_path,
                                   strerror(errno));
                goto error;
        }

        res = listen(sock, 10);

        if (res == -1) {
                fv_file_error_set(error,
                                   errno,
                                   "Failed to make socket listen: %s",
                                   strerror(errno));
                goto error;
        }

        ipc = fv_alloc(sizeof *ipc);

        ipc->uid = getuid();

        ipc->sock = sock;
        ipc->lock_path = lock_path;
        ipc->sockaddr = sockaddr;
        ipc->sock_lock = sock_lock;

        ipc->listen_source =
                fv_main_context_add_poll(NULL,
                                          sock,
                                          FV_MAIN_CONTEXT_POLL_IN,
                                          listen_source_cb,
                                          ipc);

        ipc->keyring = keyring;

        fv_list_init(&ipc->connections);

        return ipc;

error:
        if (sock != -1)
                fv_close(sock);
        if (sock_lock != -1)
                fv_close(sock_lock);
        fv_free(sockaddr);
        fv_free(lock_path);

        return NULL;
}

void
fv_ipc_free(struct fv_ipc *ipc)
{
        struct fv_ipc_connection *conn, *tmp;

        fv_list_for_each_safe(conn, tmp, &ipc->connections, link)
                remove_connection(conn);

        if (ipc->listen_source)
                fv_main_context_remove_source(ipc->listen_source);

        fv_close(ipc->sock);
        unlink(((struct sockaddr_un *) ipc->sockaddr)->sun_path);
        fv_free(ipc->sockaddr);
        fv_close(ipc->sock_lock);
        unlink(ipc->lock_path);
        fv_free(ipc->lock_path);
        fv_free(ipc);
}
