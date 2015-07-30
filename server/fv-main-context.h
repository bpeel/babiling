/*
 * Babiling
 * Copyright (C) 2011, 2013  Neil Roberts
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

#ifndef FV_MAIN_CONTEXT_H
#define FV_MAIN_CONTEXT_H

#include <stdint.h>

#include "fv-util.h"
#include "fv-error.h"

enum fv_main_context_error {
        FV_MAIN_CONTEXT_ERROR_UNSUPPORTED,
        FV_MAIN_CONTEXT_ERROR_UNKNOWN
};

enum fv_main_context_poll_flags {
        FV_MAIN_CONTEXT_POLL_IN = 1 << 0,
        FV_MAIN_CONTEXT_POLL_OUT = 1 << 1,
        FV_MAIN_CONTEXT_POLL_ERROR = 1 << 2,
};

extern struct fv_error_domain
fv_main_context_error;

struct fv_main_context;
struct fv_main_context_source;

typedef void
(* fv_main_context_poll_callback) (struct fv_main_context_source *source,
                                    int fd,
                                    enum fv_main_context_poll_flags flags,
                                    void *user_data);

typedef void
(* fv_main_context_timer_callback) (struct fv_main_context_source *source,
                                     void *user_data);

typedef void
(* fv_main_context_idle_callback) (struct fv_main_context_source *source,
                                    void *user_data);

typedef void
(* fv_main_context_quit_callback) (struct fv_main_context_source *source,
                                    void *user_data);

struct fv_main_context *
fv_main_context_new(struct fv_error **error);

struct fv_main_context *
fv_main_context_get_default(struct fv_error **error);

struct fv_main_context_source *
fv_main_context_add_poll(struct fv_main_context *mc,
                          int fd,
                          enum fv_main_context_poll_flags flags,
                          fv_main_context_poll_callback callback,
                          void *user_data);

void
fv_main_context_modify_poll(struct fv_main_context_source *source,
                             enum fv_main_context_poll_flags flags);

struct fv_main_context_source *
fv_main_context_add_quit(struct fv_main_context *mc,
                          fv_main_context_quit_callback callback,
                          void *user_data);

struct fv_main_context_source *
fv_main_context_add_timer(struct fv_main_context *mc,
                           int minutes,
                           fv_main_context_timer_callback callback,
                           void *user_data);

struct fv_main_context_source *
fv_main_context_add_idle(struct fv_main_context *mc,
                          fv_main_context_idle_callback callback,
                          void *user_data);

void
fv_main_context_remove_source(struct fv_main_context_source *source);

void
fv_main_context_poll(struct fv_main_context *mc);

/* Returns the number of microseconds since some epoch */
uint64_t
fv_main_context_get_monotonic_clock(struct fv_main_context *mc);

int64_t
fv_main_context_get_wall_clock(struct fv_main_context *mc);

void
fv_main_context_free(struct fv_main_context *mc);

#endif /* FV_MAIN_CONTEXT_H */
