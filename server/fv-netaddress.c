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
#include <inttypes.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdbool.h>
#include <errno.h>

#include "fv-netaddress.h"
#include "fv-util.h"
#include "fv-buffer.h"

static const uint8_t
ipv4_magic[12] = {
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff
};

static const uint8_t
ipv6_localhost[16] = {
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x01
};

static void
fv_netaddress_to_native_ipv4(const struct fv_netaddress *address,
                              struct sockaddr_in *native)
{
        native->sin_family = AF_INET;
        memcpy(&native->sin_addr.s_addr,
               address->host + sizeof ipv4_magic,
               sizeof (uint32_t));
        native->sin_port = htons(address->port);
}

static void
fv_netaddress_to_native_ipv6(const struct fv_netaddress *address,
                              struct sockaddr_in6 *native)
{
        native->sin6_family = AF_INET6;
        memcpy(&native->sin6_addr, address->host, sizeof address->host);
        native->sin6_flowinfo = 0;
        native->sin6_scope_id = 0;
        native->sin6_port = htons(address->port);
}

void
fv_netaddress_to_native(const struct fv_netaddress *address,
                         struct fv_netaddress_native *native)
{
        if (fv_netaddress_is_ipv6(address)) {
                fv_netaddress_to_native_ipv6(address, &native->sockaddr_in6);
                native->length = sizeof native->sockaddr_in6;
        } else {
                fv_netaddress_to_native_ipv4(address, &native->sockaddr_in);
                native->length = sizeof native->sockaddr_in;
        }
}

static void
fv_netaddress_from_native_ipv4(struct fv_netaddress *address,
                                const struct sockaddr_in *native)
{
        memcpy(address->host, ipv4_magic, sizeof ipv4_magic);
        memcpy(address->host + sizeof ipv4_magic,
               &native->sin_addr,
               sizeof native->sin_addr);
        address->port = ntohs(native->sin_port);
}

static void
fv_netaddress_from_native_ipv6(struct fv_netaddress *address,
                                const struct sockaddr_in6 *native)
{
        memcpy(address->host, &native->sin6_addr, sizeof native->sin6_addr);
        address->port = ntohs(native->sin6_port);
}

void
fv_netaddress_from_native(struct fv_netaddress *address,
                           const struct fv_netaddress_native *native)
{
        switch (native->sockaddr.sa_family) {
        case AF_INET:
                fv_netaddress_from_native_ipv4(address,
                                                &native->sockaddr_in);
                break;

        case AF_INET6:
                fv_netaddress_from_native_ipv6(address,
                                                &native->sockaddr_in6);
                break;

        default:
                memset(address, 0, sizeof *address);
                break;
        }
}

char *
fv_netaddress_to_string(const struct fv_netaddress *address)
{
        const int buffer_length = (8 * 5 + /* length of ipv6 address */
                                   2 + /* square brackets */
                                   1 + /* colon separator */
                                   5 + /* port number */
                                   1 + /* null terminator */
                                   16 /* ... and one for the pot */);
        char *buf = fv_alloc(buffer_length);
        int len;

        if (fv_netaddress_is_ipv6(address)) {
                buf[0] = '[';
                inet_ntop(AF_INET6,
                          address->host,
                          buf + 1,
                          buffer_length - 1);
                len = strlen(buf);
                buf[len++] = ']';
        } else {
                inet_ntop(AF_INET,
                          address->host + sizeof ipv4_magic,
                          buf,
                          buffer_length);
                len = strlen(buf);
        }

        snprintf(buf + len, buffer_length - len,
                 ":%" PRIu16,
                 address->port);

        return buf;
}

bool
fv_netaddress_from_string(struct fv_netaddress *address,
                           const char *str,
                           int default_port)
{
        struct fv_buffer buffer;
        const char *addr_end;
        char *port_end;
        unsigned long port;
        bool ret = true;

        fv_buffer_init(&buffer);

        if (*str == '[') {
                /* IPv6 address */
                addr_end = strchr(str + 1, ']');

                if (addr_end == NULL) {
                        ret = false;
                        goto out;
                }

                fv_buffer_append(&buffer, str + 1, addr_end - str - 1);
                fv_buffer_append_c(&buffer, '\0');

                if (inet_pton(AF_INET6,
                              (char *) buffer.data,
                              address->host) != 1) {
                        ret = false;
                        goto out;
                }

                addr_end++;
        } else {
                addr_end = strchr(str + 1, ':');
                if (addr_end == NULL)
                        addr_end = str + strlen(str);

                fv_buffer_append(&buffer, str, addr_end - str);
                fv_buffer_append_c(&buffer, '\0');

                if (inet_pton(AF_INET,
                              (char *) buffer.data,
                              address->host + sizeof ipv4_magic) != 1) {
                        ret = false;
                        goto out;
                }

                memcpy(address->host, ipv4_magic, sizeof ipv4_magic);
        }

        if (*addr_end == ':') {
                errno = 0;
                port = strtoul(addr_end + 1, &port_end, 10);
                if (errno ||
                    port > 0xffff ||
                    port_end == addr_end + 1 ||
                    *port_end) {
                        ret = false;
                        goto out;
                }
                address->port = port;
        } else if (*addr_end != '\0') {
                ret = false;
                goto out;
        } else {
                address->port = default_port;
        }

out:
        fv_buffer_destroy(&buffer);

        return ret;
}

bool
fv_netaddress_is_allowed(const struct fv_netaddress *address,
                          bool allow_private_addresses)
{
        const uint8_t *host;

        if (fv_netaddress_is_ipv6(address)) {
                /* IPv6 */
                /* Ignore localhost */
                if (!memcmp(address->host,
                            ipv6_localhost,
                            sizeof ipv6_localhost))
                        return false;
                /* Ignore local addresses */
                if (address->host[0] == 0xfe &&
                    (address->host[1] & 0xc0) == 0x80)
                        return false;
                if (!allow_private_addresses) {
                        /* Ignore unique local addresses */
                        if ((address->host[0] & 0xfe) == 0xfc)
                                return false;
                }
        } else {
                /* IPv4 */
                host = address->host + sizeof ipv4_magic;
                /* Ignore localhost */
                if (host[0] == 127)
                        return false;

                /* Ignore addresses in the private range */
                if (!allow_private_addresses) {
                        if (host[0] == 10)
                                return false;
                        if (host[0] == 172 && host[1] >= 16 && host[1] <= 31)
                                return false;
                        if (host[0] == 192 && host[1] == 168)
                                return false;
                }
        }

        return true;
}

bool
fv_netaddress_is_ipv6(const struct fv_netaddress *address)
{
        return memcmp(address->host, ipv4_magic, sizeof ipv4_magic);
}
