/*
 * Notbit - A Bitmessage client
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

#include <string.h>

#include "fv-proxy.h"
#include "fv-slice.h"

struct fv_error_domain
fv_proxy_error;

enum fv_proxy_state {
        FV_PROXY_STATE_AWAITING_METHOD,
        FV_PROXY_STATE_AWAITING_REPLY,
        FV_PROXY_STATE_CONNECTED
};

struct fv_proxy {
        enum fv_proxy_state state;
        struct fv_netaddress dst_addr;
        struct fv_buffer *in_buf;
        struct fv_buffer *out_buf;
};

FV_SLICE_ALLOCATOR(struct fv_proxy, fv_proxy_allocator);

struct fv_proxy *
fv_proxy_new(const struct fv_netaddress *dst_addr,
              struct fv_buffer *in_buf,
              struct fv_buffer *out_buf)
{
        struct fv_proxy *proxy = fv_slice_alloc(&fv_proxy_allocator);

        proxy->dst_addr = *dst_addr;
        proxy->in_buf = in_buf;
        proxy->out_buf = out_buf;
        proxy->state = FV_PROXY_STATE_AWAITING_METHOD;

        /* Add the version identifier */
        fv_buffer_append(out_buf,
                          "\x5" /* version 5 */
                          "\x1" /* one authentication method */
                          "\x0", /* no authentication required */
                          3);

        return proxy;
}

static ssize_t
handle_method(struct fv_proxy *proxy,
              const uint8_t *in_buf,
              size_t in_length,
              struct fv_error **error)
{
        uint16_t port;

        if (in_length < 2)
                return 0;

        /* The proxy server should probably reply with the same
         * version we requested */
        if (in_buf[0] != 5) {
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_BAD_PROTOCOL,
                              "Proxy server replied with an invalid version");
                return -1;
        }

        if (in_buf[1] == 0xff) {
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_NO_AUTHENTICATION_UNSUPPORTED,
                              "Proxy server doesn't support no "
                              "authentication");
                return -1;
        }

        if (in_buf[1] != 0) {
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_BAD_PROTOCOL,
                              "Invalid authentication method selected "
                              "by proxy server");
                return -1;
        }

        proxy->state = FV_PROXY_STATE_AWAITING_REPLY;

        fv_buffer_append(proxy->out_buf,
                          "\x5" /* version 5 */
                          "\x1" /* connect */
                          "\x0", /* reserved */
                          3);

        if (fv_netaddress_is_ipv6(&proxy->dst_addr)) {
                fv_buffer_append_c(proxy->out_buf, 4 /* ipv6 address */);
                fv_buffer_append(proxy->out_buf,
                                  proxy->dst_addr.host,
                                  16);
        } else {
                fv_buffer_append_c(proxy->out_buf, 1 /* ipv4 address */);
                fv_buffer_append(proxy->out_buf,
                                  proxy->dst_addr.host + 12,
                                  4);
        }

        port = FV_UINT16_TO_BE(proxy->dst_addr.port);
        fv_buffer_append(proxy->out_buf, &port, sizeof port);

        return 2;
}

static ssize_t
handle_reply(struct fv_proxy *proxy,
             const uint8_t *in_buf,
             size_t in_length,
             struct fv_error **error)
{
        int addr_len;

        if (in_length < 4)
                return 0;

        switch (in_buf[3]) {
        case 1:
                addr_len = 4;
                break;
        case 2:
                addr_len = 16;
                break;
        default:
                /* Reported as an error later */
                addr_len = 0;
                break;
        }

        if (in_length < 4 + addr_len + 2)
                return 0;

        if (in_buf[0] != 5) {
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_BAD_PROTOCOL,
                              "Proxy server replied with an invalid version");
                return -1;
        }

        switch (in_buf[1]) {
        case 0:
                break;
        case 1:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_GENERAL_SOCKS_SERVER_FAILURE,
                              "General SOCKS server failure");
                return -1;
        case 2:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_CONNECTION_NOT_ALLOWED_BY_RULESET,
                              "Connection not allowed by ruleset");
                return -1;
        case 3:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_NETWORK_UNREACHABLE,
                              "Network unreachable");
                return -1;
        case 4:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_HOST_UNREACHABLE,
                              "Host unreachable");
                return -1;
        case 5:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_CONNECTION_REFUSED,
                              "Connection refused");
                return -1;
        case 6:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_TTL_EXPIRED,
                              "TTL expired");
                return -1;
        case 7:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_COMMAND_NOT_SUPPORTED,
                              "Command not supported");
                return -1;
        case 8:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_ADDRESS_TYPE_NOT_SUPPORTED,
                              "Address type not supported");
                return -1;
        default:
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_UNKNOWN,
                              "Proxy reported an unknown error code");
                return -1;
        }

        if (addr_len == 0) {
                fv_set_error(error,
                              &fv_proxy_error,
                              FV_PROXY_ERROR_BAD_PROTOCOL,
                              "Proxy replied wit an unknown address type");
                return -1;
        }

        proxy->state = FV_PROXY_STATE_CONNECTED;

        return 4 + addr_len + 2;
}

bool
fv_proxy_process_commands(struct fv_proxy *proxy,
                           struct fv_error **error)
{
        const uint8_t *in_buf = proxy->in_buf->data;
        size_t in_length = proxy->in_buf->length;
        ssize_t command_size = -1;

        while (true) {
                switch (proxy->state) {
                case FV_PROXY_STATE_AWAITING_METHOD:
                        command_size = handle_method(proxy,
                                                     in_buf, in_length,
                                                     error);
                        break;

                case FV_PROXY_STATE_AWAITING_REPLY:
                        command_size = handle_reply(proxy,
                                                    in_buf, in_length,
                                                    error);
                        break;

                case FV_PROXY_STATE_CONNECTED:
                        goto done;
                }

                if (command_size == 0)
                        goto done;
                else if (command_size < 0)
                        return false;

                in_buf += command_size;
                in_length -= command_size;
        }

done:

        /* Move the data we've processed to the beginning of the buffer */
        memmove(proxy->in_buf->data, in_buf, in_length);
        proxy->in_buf->length = in_length;

        return true;
}

bool
fv_proxy_is_connected(struct fv_proxy *proxy)
{
        return proxy->state == FV_PROXY_STATE_CONNECTED;
}

void
fv_proxy_free(struct fv_proxy *proxy)
{
        fv_slice_free(&fv_proxy_allocator, proxy);
}
