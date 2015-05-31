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

#ifndef FV_PROXY_H
#define FV_PROXY_H

#include "fv-error.h"
#include "fv-netaddress.h"
#include "fv-buffer.h"

struct fv_proxy;

extern struct fv_error_domain
fv_proxy_error;

enum fv_proxy_error {
        FV_PROXY_ERROR_BAD_PROTOCOL,
        FV_PROXY_ERROR_NO_AUTHENTICATION_UNSUPPORTED,
        FV_PROXY_ERROR_GENERAL_SOCKS_SERVER_FAILURE,
        FV_PROXY_ERROR_CONNECTION_NOT_ALLOWED_BY_RULESET,
        FV_PROXY_ERROR_NETWORK_UNREACHABLE,
        FV_PROXY_ERROR_HOST_UNREACHABLE,
        FV_PROXY_ERROR_CONNECTION_REFUSED,
        FV_PROXY_ERROR_TTL_EXPIRED,
        FV_PROXY_ERROR_COMMAND_NOT_SUPPORTED,
        FV_PROXY_ERROR_ADDRESS_TYPE_NOT_SUPPORTED,
        FV_PROXY_ERROR_UNKNOWN
};

struct fv_proxy *
fv_proxy_new(const struct fv_netaddress *dst_addr,
              struct fv_buffer *in_buf,
              struct fv_buffer *out_buf);

bool
fv_proxy_process_commands(struct fv_proxy *proxy,
                           struct fv_error **error);

bool
fv_proxy_is_connected(struct fv_proxy *proxy);

void
fv_proxy_free(struct fv_proxy *proxy);

#endif /* FV_PROXY_H */
