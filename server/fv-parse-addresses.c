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
#include <openssl/ripemd.h>
#include <assert.h>

#include "fv-parse-addresses.h"
#include "fv-address.h"

struct fv_error_domain
fv_parse_addresses_error;

struct fv_parse_addresses_data {
        struct fv_buffer *buffer;
        bool had_address;
        fv_parse_addresses_cb cb;
        void *cb_user_data;
};

static void
reset_data(struct fv_parse_addresses_data *data)
{
        data->buffer->length = 0;
        data->had_address = false;
}

static bool
process_address(struct fv_parse_addresses_data *data,
                struct fv_error **error)
{
        uint8_t *end;
        struct fv_address address;

        end = memchr(data->buffer->data, '@', data->buffer->length);

        if (end == NULL) {
                fv_set_error(error,
                              &fv_parse_addresses_error,
                              FV_PARSE_ADDRESSES_ERROR_INVALID,
                              "Email address is missing the ‘@’ symbol");
                return false;
        }

        if (data->buffer->data + data->buffer->length - end - 1 != 10 ||
            memcmp(end + 1, "bitmessage", 10)) {
                fv_set_error(error,
                              &fv_parse_addresses_error,
                              FV_PARSE_ADDRESSES_ERROR_INVALID,
                              "The email addresses must be of the form "
                              "<address>@bitmessage");
                return false;
        }

        *end = '\0';

        if (!fv_address_decode(&address,
                                (const char *) data->buffer->data)) {
                fv_set_error(error,
                              &fv_parse_addresses_error,
                              FV_PARSE_ADDRESSES_ERROR_INVALID,
                              "The Bitmessage address in the email address "
                              "is invalid");
                return false;
        }

        return data->cb(&address, data->cb_user_data, error);
}

static bool
end_address(struct fv_parse_addresses_data *data,
            struct fv_error **error)
{
        if (!data->had_address &&
            !process_address(data, error))
                return false;

        reset_data(data);

        return true;
}

static ssize_t
parse_quotes(struct fv_parse_addresses_data *data,
             const uint8_t *quotes_start,
             size_t in_length,
             struct fv_error **error)
{
        const uint8_t *in = quotes_start;

        assert(*in == '"');

        in++;
        in_length--;

        while (in_length > 0) {
                switch (*in) {
                case '"':
                        return in + 1 - quotes_start;

                case '\\':
                        if (in_length < 2) {
                                fv_set_error(error,
                                              &fv_parse_addresses_error,
                                              FV_PARSE_ADDRESSES_ERROR_INVALID,
                                              "\\ character at end of address");
                                return -1;
                        }

                        fv_buffer_append_c(data->buffer, in[1]);

                        in += 2;
                        in_length -= 2;
                        break;

                default:
                        fv_buffer_append_c(data->buffer, *in);
                        in += 1;
                        in_length -= 1;
                }
        }

        fv_set_error(error,
                      &fv_parse_addresses_error,
                      FV_PARSE_ADDRESSES_ERROR_INVALID,
                      "Unterminated quotes in address");
        return -1;
}

static ssize_t
parse_brackets(struct fv_parse_addresses_data *data,
               const uint8_t *brackets_start,
               size_t in_length,
               struct fv_error **error)
{
        const uint8_t *in = brackets_start;

        assert(*in == '<');

        if (data->had_address) {
                fv_set_error(error,
                              &fv_parse_addresses_error,
                              FV_PARSE_ADDRESSES_ERROR_INVALID,
                              "Address contains multiple <>-brackets");
                return -1;
        }

        data->had_address = true;

        in++;
        in_length--;

        data->buffer->length = 0;

        while (in_length > 0) {
                switch (*in) {
                case '>':
                        if (!process_address(data, error))
                                return -1;
                        return in + 1 - brackets_start;

                default:
                        fv_buffer_append_c(data->buffer, *in);
                        in += 1;
                        in_length -= 1;
                }
        }

        fv_set_error(error,
                      &fv_parse_addresses_error,
                      FV_PARSE_ADDRESSES_ERROR_INVALID,
                      "Unterminated brackets in address");
        return -1;
}

bool
fv_parse_addresses(struct fv_buffer *buffer,
                    fv_parse_addresses_cb cb,
                    void *user_data,
                    struct fv_error **error)
{
        struct fv_parse_addresses_data data;
        const uint8_t *in;
        ssize_t processed;
        size_t in_length;

        /* The buffer is used as both the incoming address list and as
         * a temporary buffer to store the potential addresses. The
         * addresses should never end up being longer than the point
         * we've parsed to so this shouldn't cause any problems */

        in = buffer->data;
        in_length = buffer->length;

        data.buffer = buffer;
        data.cb = cb;
        data.cb_user_data = user_data;
        reset_data(&data);

        while (in_length > 0) {
                switch (*in) {
                case ',':
                        if (!end_address(&data, error))
                                return false;
                        reset_data(&data);
                        in++;
                        in_length--;
                        break;

                case '"':
                        processed = parse_quotes(&data, in, in_length, error);
                        if (processed < 0)
                                return false;
                        in += processed;
                        in_length -= processed;
                        break;

                case '<':
                        processed = parse_brackets(&data, in, in_length, error);
                        if (processed < 0)
                                return false;
                        in += processed;
                        in_length -= processed;
                        break;

                case ' ':
                        in++;
                        in_length--;
                        break;

                default:
                        fv_buffer_append_c(buffer, *in);
                        in++;
                        in_length--;
                        break;
                }
        }

        return end_address(&data, error);
}
