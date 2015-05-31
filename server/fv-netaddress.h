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

#ifndef FV_NETADDRESS_H
#define FV_NETADDRESS_H

#include <stdint.h>
#include <stdbool.h>
#include <netinet/in.h>

struct fv_netaddress {
        /* This is in network byte order. It is the same format as in
         * the Bitmessage protocol, ie, if it is an IPv4 address then
         * it will begin with the 12 bytes 00 00 00 00 00 00 00 00 00
         * 00 FF FF followed by the 4 byte address */
        uint8_t host[16];
        /* In native byte order */
        uint16_t port;
};

struct fv_netaddress_native {
        union {
                struct sockaddr sockaddr;
                struct sockaddr_in sockaddr_in;
                struct sockaddr_in6 sockaddr_in6;
        };
        socklen_t length;
};

void
fv_netaddress_to_native(const struct fv_netaddress *address,
                         struct fv_netaddress_native *native);

void
fv_netaddress_from_native(struct fv_netaddress *address,
                           const struct fv_netaddress_native *native);

char *
fv_netaddress_to_string(const struct fv_netaddress *address);

bool
fv_netaddress_from_string(struct fv_netaddress *address,
                           const char *str,
                           int default_port);

bool
fv_netaddress_is_allowed(const struct fv_netaddress *address,
                          bool allow_private_addresses);

bool
fv_netaddress_is_ipv6(const struct fv_netaddress *address);

#endif /* FV_NETADDRESS_H */
