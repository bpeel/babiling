/*
 * Finvenkisto
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

#include "config.h"

#include <errno.h>
#include <string.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <limits.h>
#include <time.h>
#include <pthread.h>

#include "fv-main-context.h"
#include "fv-list.h"
#include "fv-util.h"
#include "fv-slice.h"

/* This is a simple replacement for the GMainLoop which uses
   epoll. The hope is that it will scale to more connections easily
   because it doesn't use poll which needs to upload the set of file
   descriptors every time it blocks and it doesn't have to walk the
   list of file descriptors to find out which object it belongs to */

struct fv_error_domain
fv_main_context_error;

struct fv_main_context_bucket;

struct fv_main_context {
        /* This mutex only guards access to n_sources, the
         * idle_sources list and the slice allocator so that idle
         * sources can be added from other threads. Everything else
         * should only be accessed from the main thread so it doesn't
         * need to guarded. Removing an idle source can only happen in
         * the main thread. That is necessary because it is difficult
         * to cope with random idle sources being removed while we are
         * iterating the list */
        pthread_mutex_t idle_mutex;

        int epoll_fd;
        /* Number of sources that are currently attached. This is used so we
           can size the array passed to epoll_wait to ensure it's possible
           to process an event for every single source */
        unsigned int n_sources;
        /* Array for receiving events */
        unsigned int events_size;
        struct epoll_event *events;

        /* List of quit sources. All of these get invoked when a quit signal
           is received */
        struct fv_list quit_sources;

        struct fv_list idle_sources;

        struct fv_main_context_source *async_pipe_source;
        int async_pipe[2];
        pthread_t main_thread;

        void (* old_int_handler)(int);
        void (* old_term_handler)(int);

        bool monotonic_time_valid;
        int64_t monotonic_time;

        bool wall_time_valid;
        int64_t wall_time;

        struct fv_list buckets;
        int64_t last_timer_time;

        /* This allocator is protected by the idle_mutex */
        struct fv_slice_allocator source_allocator;
};

struct fv_main_context_source {
        enum {
                FV_MAIN_CONTEXT_POLL_SOURCE,
                FV_MAIN_CONTEXT_TIMER_SOURCE,
                FV_MAIN_CONTEXT_IDLE_SOURCE,
                FV_MAIN_CONTEXT_QUIT_SOURCE
        } type;

        union {
                /* Poll sources */
                struct {
                        int fd;
                        enum fv_main_context_poll_flags current_flags;
                        struct fv_main_context_source *idle_source;
                };

                /* Quit sources */
                struct {
                        struct fv_list quit_link;
                };

                /* Idle sources */
                struct {
                        struct fv_list idle_link;
                };

                /* Timer sources */
                struct {
                        struct fv_main_context_bucket *bucket;
                        struct fv_list timer_link;
                };
        };

        void *user_data;
        void *callback;

        struct fv_main_context *mc;
};

struct fv_main_context_bucket {
        struct fv_list link;
        struct fv_list sources;
        int minutes;
        int minutes_passed;
};

FV_SLICE_ALLOCATOR(struct fv_main_context_bucket,
                    fv_main_context_bucket_allocator);

static struct fv_main_context *fv_main_context_default = NULL;

struct fv_main_context *
fv_main_context_get_default(struct fv_error **error)
{
        if (fv_main_context_default == NULL)
                fv_main_context_default = fv_main_context_new(error);

        return fv_main_context_default;
}

static struct fv_main_context *
fv_main_context_get_default_or_abort(void)
{
        struct fv_main_context *mc;
        struct fv_error *error = NULL;

        mc = fv_main_context_get_default(&error);

        if (mc == NULL)
                fv_fatal("failed to create default main context: %s\n",
                          error->message);

        return mc;
}

static void
async_pipe_cb(struct fv_main_context_source *source,
              int fd,
              enum fv_main_context_poll_flags flags,
              void *user_data)
{
        struct fv_main_context *mc = user_data;
        struct fv_main_context_source *quit_source;
        fv_main_context_quit_callback callback;
        uint8_t byte;

        if (read(mc->async_pipe[0], &byte, sizeof(byte)) == -1) {
                if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
                        fv_warning("Read from quit pipe failed: %s",
                                    strerror(errno));
        } else if (byte == 'Q') {
                fv_list_for_each(quit_source, &mc->quit_sources, quit_link) {
                        callback = quit_source->callback;
                        callback(quit_source, quit_source->user_data);
                }
        }
}

static void
send_async_byte(struct fv_main_context *mc,
                char byte)
{
        while (write(mc->async_pipe[1], &byte, 1) == -1 && errno == EINTR);
}

static void
fv_main_context_quit_signal_cb(int signum)
{
        struct fv_main_context *mc = fv_main_context_get_default_or_abort();

        send_async_byte(mc, 'Q');
}

static void
init_main_context(struct fv_main_context *mc,
                  int fd)
{
        pthread_mutex_init(&mc->idle_mutex, NULL /* attrs */);
        fv_slice_allocator_init(&mc->source_allocator,
                                 sizeof(struct fv_main_context_source),
                                 FV_ALIGNOF(struct fv_main_context_source));
        mc->epoll_fd = fd;
        mc->n_sources = 0;
        mc->events = NULL;
        mc->events_size = 0;
        mc->monotonic_time_valid = false;
        mc->wall_time_valid = false;
        fv_list_init(&mc->quit_sources);
        fv_list_init(&mc->idle_sources);
        fv_list_init(&mc->buckets);
        mc->last_timer_time = fv_main_context_get_monotonic_clock(mc);

        mc->old_int_handler = signal(SIGINT, fv_main_context_quit_signal_cb);
        mc->old_term_handler = signal(SIGTERM, fv_main_context_quit_signal_cb);

        if (pipe(mc->async_pipe) == -1) {
                fv_warning("Failed to create pipe: %s",
                            strerror(errno));
        } else {
                mc->async_pipe_source
                        = fv_main_context_add_poll(mc, mc->async_pipe[0],
                                                    FV_MAIN_CONTEXT_POLL_IN,
                                                    async_pipe_cb,
                                                    mc);
        }

        mc->main_thread = pthread_self();
}

struct fv_main_context *
fv_main_context_new(struct fv_error **error)
{
        int fd;

        fd = epoll_create(16);

        if (fd == -1) {
                if (errno == EINVAL)
                        fv_set_error(error,
                                      &fv_main_context_error,
                                      FV_MAIN_CONTEXT_ERROR_UNSUPPORTED,
                                      "epoll is unsupported on this system");
                else
                        fv_set_error(error,
                                      &fv_main_context_error,
                                      FV_MAIN_CONTEXT_ERROR_UNKNOWN,
                                      "failed to create an "
                                      "epoll descriptor: %s",
                                      strerror(errno));

                return NULL;
        } else {
                struct fv_main_context *mc = fv_alloc(sizeof *mc);

                init_main_context(mc, fd);

                return mc;
        }
}

static uint32_t
get_epoll_events(enum fv_main_context_poll_flags flags)
{
        uint32_t events = 0;

        if (flags & FV_MAIN_CONTEXT_POLL_IN)
                events |= EPOLLIN | EPOLLRDHUP;
        if (flags & FV_MAIN_CONTEXT_POLL_OUT)
                events |= EPOLLOUT;

        return events;
}

static void
poll_idle_cb(struct fv_main_context_source *source,
             void *user_data)
{
        fv_main_context_poll_callback callback;

        /* This is used from an idle handler if a file descriptor was
         * added which doesn't support epoll. Instead it always
         * reports that the file descriptor is ready for reading and
         * writing in order to simulate the behaviour of poll */

        source = user_data;

        callback = source->callback;

        callback(source,
                 source->fd,
                 source->current_flags &
                 (FV_MAIN_CONTEXT_POLL_IN |
                  FV_MAIN_CONTEXT_POLL_OUT),
                 source->user_data);
}

struct fv_main_context_source *
fv_main_context_add_poll(struct fv_main_context *mc,
                          int fd,
                          enum fv_main_context_poll_flags flags,
                          fv_main_context_poll_callback callback,
                          void *user_data)
{
        struct fv_main_context_source *source;
        struct epoll_event event;

        if (mc == NULL)
                mc = fv_main_context_get_default_or_abort();

        pthread_mutex_lock(&mc->idle_mutex);
        source = fv_slice_alloc(&mc->source_allocator);
        mc->n_sources++;
        pthread_mutex_unlock(&mc->idle_mutex);

        source->mc = mc;
        source->fd = fd;
        source->callback = callback;
        source->type = FV_MAIN_CONTEXT_POLL_SOURCE;
        source->user_data = user_data;
        source->idle_source = NULL;

        event.events = get_epoll_events(flags);
        event.data.ptr = source;

        if (epoll_ctl(mc->epoll_fd, EPOLL_CTL_ADD, fd, &event) == -1) {
                /* EPERM will happen if the file descriptor doesn't
                 * support epoll. This will happen with regular files.
                 * Instead of poll on the file descriptor we will
                 * install an idle handler which just always reports
                 * that the descriptor is ready for reading and
                 * writing. This simulates what poll would do */
                if (errno == EPERM) {
                        source->idle_source =
                                fv_main_context_add_idle(mc,
                                                          poll_idle_cb,
                                                          source);
                } else {
                        fv_warning("EPOLL_CTL_ADD failed: %s",
                                    strerror(errno));
                }
        }

        source->current_flags = flags;

        return source;
}

void
fv_main_context_modify_poll(struct fv_main_context_source *source,
                             enum fv_main_context_poll_flags flags)
{
        struct epoll_event event;

        fv_return_if_fail(source->type == FV_MAIN_CONTEXT_POLL_SOURCE);

        if (source->current_flags == flags)
                return;

        if (source->idle_source == NULL) {
                event.events = get_epoll_events(flags);
                event.data.ptr = source;

                if (epoll_ctl(source->mc->epoll_fd,
                              EPOLL_CTL_MOD,
                              source->fd,
                              &event) == -1)
                        fv_warning("EPOLL_CTL_MOD failed: %s",
                                    strerror(errno));
        }

        source->current_flags = flags;
}

struct fv_main_context_source *
fv_main_context_add_quit(struct fv_main_context *mc,
                          fv_main_context_quit_callback callback,
                          void *user_data)
{
        struct fv_main_context_source *source;

        if (mc == NULL)
                mc = fv_main_context_get_default_or_abort();

        pthread_mutex_lock(&mc->idle_mutex);
        source = fv_slice_alloc(&mc->source_allocator);
        mc->n_sources++;
        pthread_mutex_unlock(&mc->idle_mutex);

        source->mc = mc;
        source->callback = callback;
        source->type = FV_MAIN_CONTEXT_QUIT_SOURCE;
        source->user_data = user_data;

        fv_list_insert(&mc->quit_sources, &source->quit_link);

        return source;
}

static struct fv_main_context_bucket *
get_bucket(struct fv_main_context *mc, int minutes)
{
        struct fv_main_context_bucket *bucket;

        fv_list_for_each(bucket, &mc->buckets, link) {
                if (bucket->minutes == minutes)
                        return bucket;
        }

        bucket = fv_slice_alloc(&fv_main_context_bucket_allocator);
        fv_list_init(&bucket->sources);
        bucket->minutes = minutes;
        bucket->minutes_passed = 0;
        fv_list_insert(&mc->buckets, &bucket->link);

        return bucket;
}

struct fv_main_context_source *
fv_main_context_add_timer(struct fv_main_context *mc,
                           int minutes,
                           fv_main_context_timer_callback callback,
                           void *user_data)
{
        struct fv_main_context_source *source;

        if (mc == NULL)
                mc = fv_main_context_get_default_or_abort();

        pthread_mutex_lock(&mc->idle_mutex);
        source = fv_slice_alloc(&mc->source_allocator);
        mc->n_sources++;
        pthread_mutex_unlock(&mc->idle_mutex);

        source->mc = mc;
        source->bucket = get_bucket(mc, minutes);
        source->callback = callback;
        source->type = FV_MAIN_CONTEXT_TIMER_SOURCE;
        source->user_data = user_data;

        fv_list_insert(&source->bucket->sources, &source->timer_link);

        return source;
}

static void
wakeup_main_loop(struct fv_main_context *mc)
{
        if (!pthread_equal(pthread_self(), mc->main_thread))
                send_async_byte(mc, 'W');
}

struct fv_main_context_source *
fv_main_context_add_idle(struct fv_main_context *mc,
                          fv_main_context_idle_callback callback,
                          void *user_data)
{
        struct fv_main_context_source *source;

        if (mc == NULL)
                mc = fv_main_context_get_default_or_abort();

        /* This may be called from a thread other than the main one so
         * we need to guard access to the idle sources lists */
        pthread_mutex_lock(&mc->idle_mutex);
        source = fv_slice_alloc(&mc->source_allocator);
        fv_list_insert(&mc->idle_sources, &source->idle_link);
        mc->n_sources++;
        pthread_mutex_unlock(&mc->idle_mutex);

        source->mc = mc;
        source->callback = callback;
        source->type = FV_MAIN_CONTEXT_IDLE_SOURCE;
        source->user_data = user_data;

        wakeup_main_loop(mc);

        return source;
}

void
fv_main_context_remove_source(struct fv_main_context_source *source)
{
        struct fv_main_context *mc = source->mc;
        struct fv_main_context_bucket *bucket;
        struct epoll_event event;

        switch (source->type) {
        case FV_MAIN_CONTEXT_POLL_SOURCE:
                if (source->idle_source)
                        fv_main_context_remove_source(source->idle_source);
                else if (epoll_ctl(mc->epoll_fd,
                              EPOLL_CTL_DEL,
                              source->fd,
                              &event) == -1)
                        fv_warning("EPOLL_CTL_DEL failed: %s",
                                    strerror(errno));
                break;

        case FV_MAIN_CONTEXT_QUIT_SOURCE:
                fv_list_remove(&source->quit_link);
                break;

        case FV_MAIN_CONTEXT_IDLE_SOURCE:
                pthread_mutex_lock(&mc->idle_mutex);
                fv_list_remove(&source->idle_link);
                pthread_mutex_unlock(&mc->idle_mutex);
                break;

        case FV_MAIN_CONTEXT_TIMER_SOURCE:
                bucket = source->bucket;
                fv_list_remove(&source->timer_link);

                if (fv_list_empty(&bucket->sources)) {
                        fv_list_remove(&bucket->link);
                        fv_slice_free(&fv_main_context_bucket_allocator,
                                       bucket);
                }
                break;
        }

        pthread_mutex_lock(&mc->idle_mutex);
        fv_slice_free(&mc->source_allocator, source);
        mc->n_sources--;
        pthread_mutex_unlock(&mc->idle_mutex);
}

static int
get_timeout(struct fv_main_context *mc)
{
        struct fv_main_context_bucket *bucket;
        int min_minutes, minutes_to_wait;
        int64_t elapsed, elapsed_minutes;

        if (!fv_list_empty(&mc->idle_sources))
                return 0;

        if (fv_list_empty(&mc->buckets))
                return -1;

        min_minutes = INT_MAX;

        fv_list_for_each(bucket, &mc->buckets, link) {
                minutes_to_wait = bucket->minutes - bucket->minutes_passed;

                if (minutes_to_wait < min_minutes)
                        min_minutes = minutes_to_wait;
        }

        elapsed =
            fv_main_context_get_monotonic_clock(mc) - mc->last_timer_time;
        elapsed_minutes = elapsed / 60000000;

        /* If we've already waited enough time then don't wait any
         * further time */
        if (elapsed_minutes >= min_minutes)
                return 0;

        /* Subtract the number of minutes we've already waited */
        min_minutes -= (int) elapsed_minutes;

        return (60000 - (elapsed / 1000 % 60000) + (min_minutes - 1) * 60000);
}

static void
emit_bucket(struct fv_main_context_bucket *bucket)
{
        struct fv_main_context_source *source, *tmp_source;
        fv_main_context_timer_callback callback;

        fv_list_for_each_safe(source,
                               tmp_source,
                               &bucket->sources,
                               timer_link) {
                callback = source->callback;
                callback(source, source->user_data);
        }

        bucket->minutes_passed = 0;
}

static void
check_timer_sources(struct fv_main_context *mc)
{
        struct fv_main_context_bucket *bucket, *tmp_bucket;
        int64_t now;
        int64_t elapsed_minutes;

        if (fv_list_empty(&mc->buckets))
                return;

        now = fv_main_context_get_monotonic_clock(mc);
        elapsed_minutes = (now - mc->last_timer_time) / 60000000;
        mc->last_timer_time += elapsed_minutes * 60000000;

        if (elapsed_minutes < 1)
                return;

        fv_list_for_each_safe(bucket, tmp_bucket, &mc->buckets, link) {
                if (bucket->minutes_passed + elapsed_minutes >= bucket->minutes)
                        emit_bucket(bucket);
                else
                        bucket->minutes_passed += elapsed_minutes;
        }
}

static void
emit_idle_sources(struct fv_main_context *mc)
{
        struct fv_main_context_source *source, *tmp_source;
        fv_main_context_timer_callback callback;

        pthread_mutex_lock(&mc->idle_mutex);

        /* This loop needs to cope with sources being added from other
         * threads while iterating. It doesn't need to cope with
         * sources being removed, apart from the one currently being
         * executed. Any new sources would be added at the beginning
         * of the list so they shouldn't cause any problems and they
         * would just be missed by this loop */

        fv_list_for_each_safe(source, tmp_source,
                               &mc->idle_sources,
                               idle_link) {
                callback = source->callback;

                pthread_mutex_unlock(&mc->idle_mutex);
                callback(source, source->user_data);
                pthread_mutex_lock(&mc->idle_mutex);
        }

        pthread_mutex_unlock(&mc->idle_mutex);
}

static void
handle_epoll_event(struct fv_main_context *mc,
                   struct epoll_event *event)
{
        struct fv_main_context_source *source = source = event->data.ptr;
        fv_main_context_poll_callback callback;
        enum fv_main_context_poll_flags flags;

        switch (source->type) {
        case FV_MAIN_CONTEXT_POLL_SOURCE:
                callback = source->callback;
                flags = 0;

                if (event->events & EPOLLOUT)
                        flags |= FV_MAIN_CONTEXT_POLL_OUT;
                if (event->events & (EPOLLIN | EPOLLRDHUP))
                        flags |= FV_MAIN_CONTEXT_POLL_IN;
                if (event->events & EPOLLHUP) {
                        /* If the source is polling for read then we'll
                         * just mark it as ready for reading so that any
                         * error or EOF will be handled by the read call
                         * instead of immediately aborting */
                        if ((source->current_flags & FV_MAIN_CONTEXT_POLL_IN))
                                flags |= FV_MAIN_CONTEXT_POLL_IN;
                        else
                                flags |= FV_MAIN_CONTEXT_POLL_ERROR;
                }
                if (event->events & EPOLLERR)
                        flags |= FV_MAIN_CONTEXT_POLL_ERROR;

                callback(source, source->fd, flags, source->user_data);
                break;

        case FV_MAIN_CONTEXT_QUIT_SOURCE:
        case FV_MAIN_CONTEXT_TIMER_SOURCE:
        case FV_MAIN_CONTEXT_IDLE_SOURCE:
                fv_warn_if_reached();
                break;
        }
}

void
fv_main_context_poll(struct fv_main_context *mc)
{
        int n_events;
        int n_sources;

        if (mc == NULL)
                mc = fv_main_context_get_default_or_abort();

        pthread_mutex_lock(&mc->idle_mutex);
        n_sources = mc->n_sources;
        pthread_mutex_unlock(&mc->idle_mutex);

        if (n_sources > mc->events_size) {
                fv_free(mc->events);
                mc->events = fv_alloc(sizeof (struct epoll_event) *
                                       n_sources);
                mc->events_size = n_sources;
        }

        n_events = epoll_wait(mc->epoll_fd,
                              mc->events,
                              mc->events_size,
                              get_timeout(mc));

        /* Once we've polled we can assume that some time has passed so our
           cached values of the clocks are no longer valid */
        mc->monotonic_time_valid = false;
        mc->wall_time_valid = false;

        if (n_events == -1) {
                if (errno != EINTR)
                        fv_warning("epoll_wait failed: %s", strerror(errno));
        } else {
                int i;

                for (i = 0; i < n_events; i++)
                        handle_epoll_event(mc, mc->events + i);

                check_timer_sources(mc);
                emit_idle_sources(mc);
        }
}

uint64_t
fv_main_context_get_monotonic_clock(struct fv_main_context *mc)
{
        struct timespec ts;

        if (mc == NULL)
                mc = fv_main_context_get_default_or_abort();

        /* Because in theory the program doesn't block between calls to
           poll, we can act as if no time passes between calls to
           epoll. That way we can cache the clock value instead of having to
           do a system call every time we need it */
        if (!mc->monotonic_time_valid) {
                clock_gettime(CLOCK_MONOTONIC, &ts);
                mc->monotonic_time = (ts.tv_sec * UINT64_C(1000000) +
                                      ts.tv_nsec / UINT64_C(1000));
                mc->monotonic_time_valid = true;
        }

        return mc->monotonic_time;
}

int64_t
fv_main_context_get_wall_clock(struct fv_main_context *mc)
{
        time_t now;

        if (mc == NULL)
                mc = fv_main_context_get_default_or_abort();

        /* Because in theory the program doesn't block between calls to
           poll, we can act as if no time passes between calls to
           epoll. That way we can cache the clock value instead of having to
           do a system call every time we need it */
        if (!mc->wall_time_valid) {
                time(&now);

                mc->wall_time = now;
                mc->wall_time_valid = true;
        }

        return mc->wall_time;
}

void
fv_main_context_free(struct fv_main_context *mc)
{
        fv_return_if_fail(mc != NULL);

        signal(SIGINT, mc->old_int_handler);
        signal(SIGTERM, mc->old_term_handler);
        fv_main_context_remove_source(mc->async_pipe_source);
        fv_close(mc->async_pipe[0]);
        fv_close(mc->async_pipe[1]);

        if (mc->n_sources > 0)
                fv_warning("Sources still remain on a main context "
                            "that is being freed");

        fv_free(mc->events);
        pthread_mutex_destroy(&mc->idle_mutex);
        fv_close(mc->epoll_fd);

        fv_slice_allocator_destroy(&mc->source_allocator);

        fv_free(mc);

        if (mc == fv_main_context_default)
                fv_main_context_default = NULL;
}
