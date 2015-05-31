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

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h>

#include "fv-log.h"
#include "fv-buffer.h"
#include "fv-file-error.h"
#include "fv-thread.h"

static FILE *fv_log_file = NULL;
static struct fv_buffer fv_log_buffer = FV_BUFFER_STATIC_INIT;
static pthread_t fv_log_thread;
static bool fv_log_has_thread = false;
static pthread_mutex_t fv_log_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fv_log_cond = PTHREAD_COND_INITIALIZER;
static bool fv_log_finished = false;

struct fv_error_domain
fv_log_error;

bool
fv_log_available(void)
{
        return fv_log_file != NULL;
}

void
fv_log(const char *format, ...)
{
        va_list ap;
        time_t now;
        struct tm tm;

        if (!fv_log_available())
                return;

        pthread_mutex_lock(&fv_log_mutex);

        time(&now);
        gmtime_r(&now, &tm);

        fv_buffer_append_printf(&fv_log_buffer,
                                 "[%4d-%02d-%02dT%02d:%02d:%02dZ] ",
                                 tm.tm_year + 1900,
                                 tm.tm_mon + 1,
                                 tm.tm_mday,
                                 tm.tm_hour,
                                 tm.tm_min,
                                 tm.tm_sec);

        va_start(ap, format);
        fv_buffer_append_vprintf(&fv_log_buffer, format, ap);
        va_end(ap);

        fv_buffer_append_c(&fv_log_buffer, '\n');

        pthread_cond_signal(&fv_log_cond);

        pthread_mutex_unlock(&fv_log_mutex);
}

static void
block_sigint(void)
{
        sigset_t sigset;

        sigemptyset(&sigset);
        sigaddset(&sigset, SIGINT);
        sigaddset(&sigset, SIGTERM);

        if (pthread_sigmask(SIG_BLOCK, &sigset, NULL) == -1)
                fv_warning("pthread_sigmask failed: %s", strerror(errno));
}

static void *
fv_log_thread_func(void *data)
{
        struct fv_buffer alternate_buffer;
        struct fv_buffer tmp;
        bool had_error = false;

        block_sigint();

        fv_buffer_init(&alternate_buffer);

        pthread_mutex_lock(&fv_log_mutex);

        while (!fv_log_finished || fv_log_buffer.length > 0) {
                size_t wrote;

                /* Wait until there's something to do */
                while (!fv_log_finished && fv_log_buffer.length == 0)
                        pthread_cond_wait(&fv_log_cond, &fv_log_mutex);

                if (had_error) {
                        /* Just ignore the data */
                        fv_buffer_set_length(&fv_log_buffer, 0);
                } else {
                        /* Swap the log buffer for an empty alternate
                           buffer so we can write from the normal
                           one */
                        tmp = fv_log_buffer;
                        fv_log_buffer = alternate_buffer;
                        alternate_buffer = tmp;

                        /* Release the mutex while we do a blocking write */
                        pthread_mutex_unlock(&fv_log_mutex);

                        wrote = fwrite(alternate_buffer.data, 1 /* size */ ,
                                       alternate_buffer.length, fv_log_file);

                        /* If there was an error then we'll just start
                           ignoring data until we're told to quit */
                        if (wrote != alternate_buffer.length)
                                had_error = true;
                        else
                                fflush(fv_log_file);

                        fv_buffer_set_length(&alternate_buffer, 0);

                        pthread_mutex_lock(&fv_log_mutex);
                }
        }

        pthread_mutex_unlock(&fv_log_mutex);

        fv_buffer_destroy(&alternate_buffer);

        return NULL;
}

bool
fv_log_set_file(const char *filename, struct fv_error **error)
{
        FILE *file;

        file = fopen(filename, "a");

        if (file == NULL) {
                fv_file_error_set(error,
                                   errno,
                                   "%s: %s",
                                   filename,
                                   strerror(errno));
                return false;
        }

        fv_log_close();

        fv_log_file = file;
        fv_log_finished = false;

        return true;
}

void
fv_log_start(void)
{
        if (!fv_log_available() || fv_log_has_thread)
                return;

        fv_log_thread = fv_thread_create(fv_log_thread_func,
                                           NULL /* thread func arg */);
        fv_log_has_thread = true;
}

void
fv_log_close(void)
{
        if (fv_log_has_thread) {
                pthread_mutex_lock(&fv_log_mutex);
                fv_log_finished = true;
                pthread_cond_signal(&fv_log_cond);
                pthread_mutex_unlock(&fv_log_mutex);

                pthread_join(fv_log_thread, NULL);

                fv_log_has_thread = false;
        }

        fv_buffer_destroy(&fv_log_buffer);
        fv_buffer_init(&fv_log_buffer);

        if (fv_log_file) {
                fclose(fv_log_file);
                fv_log_file = NULL;
        }
}
