/*
 * Copyright © 2008 Kristian Høgsberg
 * Copyright © 2013 Neil Roberts
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

/* This file was originally borrowed from the Wayland source code */

#ifndef FV_SIGNAL_H
#define FV_SIGNAL_H

#include <stdbool.h>

#include "fv-list.h"

struct fv_listener;

typedef bool
(* fv_notify_func)(struct fv_listener *listener, void *data);

struct fv_signal {
        struct fv_list listener_list;
};

struct fv_listener {
        struct fv_list link;
        fv_notify_func notify;
};

static inline void
fv_signal_init(struct fv_signal *signal)
{
        fv_list_init(&signal->listener_list);
}

static inline void
fv_signal_add(struct fv_signal *signal,
               struct fv_listener *listener)
{
        fv_list_insert(signal->listener_list.prev, &listener->link);
}

static inline bool
fv_signal_emit(struct fv_signal *signal, void *data)
{
        struct fv_listener *l, *next;

        fv_list_for_each_safe(l, next, &signal->listener_list, link)
                if (!l->notify(l, data))
                        return false;

        return true;
}

#endif /* FV_SIGNAL_H */
