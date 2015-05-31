/*
 * Finvenkisto
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
#include <string.h>
#include <errno.h>

#include "fv-key-value.h"
#include "fv-buffer.h"
#include "fv-log.h"
#include "fv-util.h"

enum fv_key_value_state {
        FV_KEY_VALUE_STATE_HEADER_START,
        FV_KEY_VALUE_STATE_BAD_HEADER_START,
        FV_KEY_VALUE_STATE_READING_HEADER,
        FV_KEY_VALUE_STATE_END_HEADER_LINE,
        FV_KEY_VALUE_STATE_END_HEADER_LINE2,
        FV_KEY_VALUE_STATE_FIELD_START,
        FV_KEY_VALUE_STATE_READING_FIELD_NAME,
        FV_KEY_VALUE_STATE_WAITING_EQUALS,
        FV_KEY_VALUE_STATE_BAD_FIELD,
        FV_KEY_VALUE_STATE_WAITING_VALUE_START,
        FV_KEY_VALUE_STATE_READING_VALUE,
};

struct fv_key_value_data {
        enum fv_key_value_state state;

        struct fv_ecc *ecc;

        fv_key_value_func func;
        void *user_data;

        struct fv_buffer key_buffer;
        struct fv_buffer value_buffer;

        int line_num;
};

static void
ensure_null_buffer(struct fv_buffer *buffer)
{
        fv_buffer_ensure_size(buffer, buffer->length + 1);
        buffer->data[buffer->length] = '\0';
}

static void
process_header(struct fv_key_value_data *data)
{
        ensure_null_buffer(&data->value_buffer);

        data->func(FV_KEY_VALUE_EVENT_HEADER,
                   data->line_num,
                   NULL, /* key */
                   (const char *) data->value_buffer.data,
                   data->user_data);
}

static void
process_value(struct fv_key_value_data *data)
{
        ensure_null_buffer(&data->key_buffer);

        while (data->value_buffer.length > 0 &&
               data->value_buffer.data[data->value_buffer.length - 1] == ' ')
                data->value_buffer.length--;

        ensure_null_buffer(&data->value_buffer);

        data->func(FV_KEY_VALUE_EVENT_PROPERTY,
                   data->line_num,
                   (const char *) data->key_buffer.data,
                   (const char *) data->value_buffer.data,
                   data->user_data);
}

static void
handle_byte(struct fv_key_value_data *data,
            int ch)
{
        switch (data->state) {
        case FV_KEY_VALUE_STATE_HEADER_START:
                if (ch == '[') {
                        data->state = FV_KEY_VALUE_STATE_READING_HEADER;
                        data->value_buffer.length = 0;
                } else if (ch != ' ' && ch != '\n') {
                        fv_log("Invalid header on line %i",
                                data->line_num);
                        data->state = FV_KEY_VALUE_STATE_BAD_HEADER_START;
                }
                return;
        case FV_KEY_VALUE_STATE_BAD_HEADER_START:
                if (ch == '\n')
                        data->state = FV_KEY_VALUE_STATE_HEADER_START;
                return;
        case FV_KEY_VALUE_STATE_READING_HEADER:
                if (ch == '\n') {
                        fv_log("Invalid header on line %i",
                                data->line_num);
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                } else if (ch == ']') {
                        process_header(data);
                        data->state = FV_KEY_VALUE_STATE_END_HEADER_LINE;
                } else {
                        fv_buffer_append_c(&data->value_buffer, ch);
                }
                return;
        case FV_KEY_VALUE_STATE_END_HEADER_LINE:
                if (ch == '\n') {
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                } else if (ch != ' ') {
                        fv_log("Junk after header on line %i",
                                data->line_num);
                        data->state = FV_KEY_VALUE_STATE_END_HEADER_LINE2;
                }
                return;
        case FV_KEY_VALUE_STATE_END_HEADER_LINE2:
                if (ch == '\n')
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                return;
        case FV_KEY_VALUE_STATE_FIELD_START:
                if (ch == '[') {
                        data->state = FV_KEY_VALUE_STATE_READING_HEADER;
                        data->value_buffer.length = 0;
                } else if (ch != ' ' && ch != '\n') {
                        fv_buffer_set_length(&data->key_buffer, 1);
                        data->key_buffer.data[0] = ch;
                        data->state = FV_KEY_VALUE_STATE_READING_FIELD_NAME;
                }
                return;
        case FV_KEY_VALUE_STATE_READING_FIELD_NAME:
                if (ch == ' ') {
                        data->state = FV_KEY_VALUE_STATE_WAITING_EQUALS;
                } else if (ch == '=') {
                        data->state = FV_KEY_VALUE_STATE_WAITING_VALUE_START;
                } else if (ch == '\n') {
                        fv_log("Invalid line %i",
                                data->line_num);
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                } else {
                        fv_buffer_append_c(&data->key_buffer, ch);
                }
                return;
        case FV_KEY_VALUE_STATE_WAITING_EQUALS:
                if (ch == '=') {
                        data->state = FV_KEY_VALUE_STATE_WAITING_VALUE_START;
                } else if (ch == '\n') {
                        fv_log("Invalid line %i",
                                data->line_num);
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                } else if (ch != ' ') {
                        fv_log("Invalid line %i",
                                data->line_num);
                        data->state = FV_KEY_VALUE_STATE_BAD_FIELD;
                }
                return;
        case FV_KEY_VALUE_STATE_WAITING_VALUE_START:
                if (ch == '\n') {
                        fv_buffer_set_length(&data->value_buffer, 0);
                        process_value(data);
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                } else if (ch != ' ') {
                        fv_buffer_set_length(&data->value_buffer, 1);
                        data->value_buffer.data[0] = ch;
                        data->state = FV_KEY_VALUE_STATE_READING_VALUE;
                }
                return;
        case FV_KEY_VALUE_STATE_READING_VALUE:
                if (ch == '\n') {
                        process_value(data);
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                } else {
                        fv_buffer_append_c(&data->value_buffer, ch);
                }
                return;
        case FV_KEY_VALUE_STATE_BAD_FIELD:
                if (ch == '\n')
                        data->state = FV_KEY_VALUE_STATE_FIELD_START;
                return;
        }

        fv_fatal("Invalid state reached");
}

void
fv_key_value_load(FILE *file,
                   fv_key_value_func func,
                   void *user_data)
{
        struct fv_key_value_data data;
        int ch;

        data.line_num = 1;
        data.state = FV_KEY_VALUE_STATE_HEADER_START;

        fv_buffer_init(&data.key_buffer);
        fv_buffer_init(&data.value_buffer);

        data.func = func;
        data.user_data = user_data;

        while ((ch = fgetc(file)) != EOF) {
                handle_byte(&data, ch);

                if (ch == '\n')
                        data.line_num++;
        }

        handle_byte(&data, '\n');

        fv_buffer_destroy(&data.key_buffer);
        fv_buffer_destroy(&data.value_buffer);
}

bool
fv_key_value_parse_bool_value(int line_number,
                               const char *value,
                               bool *result)
{
        if (!strcmp(value, "true")) {
                *result = true;
                return true;
        }

        if (!strcmp(value, "false")) {
                *result = false;
                return true;
        }

        fv_log("Invalid boolean value on line %i", line_number);

        return false;
}

bool
fv_key_value_parse_int_value(int line_number,
                              const char *value,
                              int64_t max,
                              int64_t *result)
{
        long long int int_value;
        char *tail;

        errno = 0;

        int_value = strtoll(value, &tail, 10);

        if (errno || tail == value || *tail || value < 0) {
                fv_log("Invalid integer value on line %i", line_number);
                return false;
        }

        if (int_value > max) {
                fv_log("Value too big on line %i", line_number);
                return false;
        }

        *result = int_value;

        return true;
}
