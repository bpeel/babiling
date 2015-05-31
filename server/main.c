/*
 * Finvenkisto
 * Copyright (C) 2013, 2015  Neil Roberts
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

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <signal.h>

#include "fv-main-context.h"
#include "fv-log.h"
#include "fv-network.h"
#include "fv-file-error.h"
#include "fv-proto.h"

static struct fv_error_domain
arguments_error;

enum fv_arguments_error {
        FV_ARGUMENTS_ERROR_INVALID,
        FV_ARGUMENTS_ERROR_UNKNOWN
};

struct address {
        /* Only one of these will be set depending on whether the user
         * specified a full address or just a port */
        const char *address;
        const char *port;

        struct address *next;
};

static struct address *option_listen_addresses = NULL;
static char *option_log_file = NULL;
static bool option_daemonize = false;
static char *option_user = NULL;
static char *option_group = NULL;

static const char options[] = "-a:l:du:g:p:h";

static void
add_address(struct address **list,
            const char *address)
{
        struct address *listen_address;

        listen_address = fv_alloc(sizeof (struct address));
        listen_address->address = address;
        listen_address->port = NULL;
        listen_address->next = *list;
        *list = listen_address;
}

static void
add_port(struct address **list,
         const char *port_string)
{
        struct address *listen_address;

        listen_address = fv_alloc(sizeof (struct address));
        listen_address->address = NULL;
        listen_address->port = port_string;
        listen_address->next = *list;
        *list = listen_address;
}

static void
free_addresses(struct address *list)
{
        struct address *address, *next;

        for (address = list;
             address;
             address = next) {
                next = address->next;
                fv_free(address);
        }
}

static void
usage(void)
{
        printf("Finvenkisto Server. Version " PACKAGE_VERSION "\n"
               "usage: finvenkisto-server [options]...\n"
               " -h                    Show this help message\n"
               " -p <port>             Specifies a port to listen on.\n"
               "                       Equivalent to -a [::]:port.\n"
               " -a <address[:port]>   Add an address to listen on. Can be\n"
               "                       specified multiple times. Defaults to\n"
               "                       [::] to listen on port "
               FV_STRINGIFY(FV_PROTO_DEFAULT_PORT) "\n"
               " -l <file>             Specify the pathname for the log file\n"
               "                       Defaults to stdout.\n"
               " -d                    Fork and detach from terminal after\n"
               "                       creating listen socket. (Daemonize)\n"
               " -u <user>             Specify a user to run as. Used to drop\n"
               "                       privileges.\n"
               " -g <group>            Specify a group to run as.\n"
               "\n");
        exit(EXIT_FAILURE);
}

static bool
process_arguments(int argc, char **argv, struct fv_error **error)
{
        int opt;

        opterr = false;

        while ((opt = getopt(argc, argv, options)) != -1) {
                switch (opt) {
                case ':':
                case '?':
                        fv_set_error(error,
                                      &arguments_error,
                                      FV_ARGUMENTS_ERROR_INVALID,
                                      "invalid option '%c'",
                                      optopt);
                        goto error;

                case '\1':
                        fv_set_error(error,
                                      &arguments_error,
                                      FV_ARGUMENTS_ERROR_UNKNOWN,
                                      "unexpected argument \"%s\"",
                                      optarg);
                        goto error;

                case 'a':
                        add_address(&option_listen_addresses, optarg);
                        break;

                case 'p':
                        add_port(&option_listen_addresses, optarg);
                        break;

                case 'l':
                        option_log_file = optarg;
                        break;

                case 'd':
                        option_daemonize = true;
                        break;

                case 'u':
                        option_user = optarg;
                        break;

                case 'g':
                        option_group = optarg;
                        break;

                case 'h':
                        usage();
                        break;
                }
        }

        if (optind < argc) {
                fv_set_error(error,
                              &arguments_error,
                              FV_ARGUMENTS_ERROR_UNKNOWN,
                              "unexpected argument \"%s\"",
                              argv[optind]);
                goto error;
        }

        if (option_listen_addresses == NULL)
                add_port(&option_listen_addresses,
                         FV_STRINGIFY(FV_PROTO_DEFAULT_PORT));

        return true;

error:
        free_addresses(option_listen_addresses);
        option_listen_addresses = NULL;
        return false;
}

static void
daemonize(void)
{
        pid_t pid, sid;

        pid = fork();

        if (pid < 0) {
                fv_warning("fork failed: %s", strerror(errno));
                exit(EXIT_FAILURE);
        }
        if (pid > 0)
                /* Parent process, we can just quit */
                exit(EXIT_SUCCESS);

        /* Create a new SID for the child process */
        sid = setsid();
        if (sid < 0) {
                fv_warning("setsid failed: %s", strerror(errno));
                exit(EXIT_FAILURE);
        }

        /* Change the working directory so we're resilient against it being
           removed */
        if (chdir("/") < 0) {
                fv_warning("chdir failed: %s", strerror(errno));
                exit(EXIT_FAILURE);
        }

        /* Redirect standard files to /dev/null */
        stdin = freopen("/dev/null", "r", stdin);
        stdout = freopen("/dev/null", "w", stdout);
        stderr = freopen("/dev/null", "w", stderr);
}

static void
set_user(const char *user_name)
{
        struct passwd *user_info;

        user_info = getpwnam(user_name);

        if (user_info == NULL) {
                fprintf(stderr, "Unknown user \"%s\"\n", user_name);
                exit(EXIT_FAILURE);
        }

        if (setuid(user_info->pw_uid) == -1) {
                fprintf(stderr, "Error setting user privileges: %s\n",
                        strerror(errno));
                exit(EXIT_FAILURE);
        }
}

static void
set_group(const char *group_name)
{
        struct group *group_info;

        group_info = getgrnam(group_name);

        if (group_info == NULL) {
                fprintf(stderr, "Unknown group \"%s\"\n", group_name);
                exit(EXIT_FAILURE);
        }

        if (setgid(group_info->gr_gid) == -1) {
                fprintf(stderr, "Error setting group privileges: %s\n",
                        strerror(errno));
                exit(EXIT_FAILURE);
        }
}

static void
quit_cb(struct fv_main_context_source *source,
        void *user_data)
{
        bool *quit = user_data;
        *quit = true;
}

static bool
add_listen_address_to_network(struct fv_network *nw,
                              struct address *address,
                              struct fv_error **error)
{
        struct fv_error *local_error = NULL;
        char *full_address;
        bool res;

        if (address->address)
                return fv_network_add_listen_address(nw,
                                                     address->address,
                                                     error);

        /* If just the port is specified then we'll first try
         * listening on an IPv6 address. Listening on IPv6 should
         * accept IPv4 connections as well. However some servers have
         * IPv6 disabled so if it doesn't work we'll fall back to
         * IPv4 */
        full_address = fv_strconcat("[::]:", address->port, NULL);
        res = fv_network_add_listen_address(nw, full_address, &local_error);
        fv_free(full_address);

        if (res)
                return true;

        if (local_error->domain == &fv_file_error &&
            (local_error->code == FV_FILE_ERROR_PFNOSUPPORT ||
             local_error->code == FV_FILE_ERROR_AFNOSUPPORT)) {
                fv_error_free(local_error);
        } else {
                fv_error_propagate(error, local_error);
                return false;
        }

        full_address = fv_strconcat("0.0.0.0:", address->port, NULL);
        res = fv_network_add_listen_address(nw, full_address, error);
        fv_free(full_address);

        return res;
}

static bool
add_addresses(struct fv_network *nw,
              struct fv_error **error)
{
        struct address *address;

        for (address = option_listen_addresses;
             address;
             address = address->next) {
                if (!add_listen_address_to_network(nw,
                                                   address,
                                                   error))
                        return false;
        }

        return true;
}

static bool
set_log_file(struct fv_error **error)
{
        const char *log_filename;

        if (option_log_file)
                log_filename = option_log_file;
        else if (option_daemonize)
                log_filename = "/var/log/finvenkisto.log";
        else
                log_filename = "/dev/stdout";

        return fv_log_set_file(log_filename, error);
}

static void
run_main_loop(struct fv_network *nw)
{
        struct fv_main_context_source *quit_source;
        bool quit = false;

        if (option_group)
                set_group(option_group);
        if (option_user)
                set_user(option_user);

        if (option_daemonize)
                daemonize();

        signal(SIGPIPE, SIG_IGN);

        fv_log_start();

        quit_source = fv_main_context_add_quit(NULL, quit_cb, &quit);

        do
                fv_main_context_poll(NULL);
        while(!quit);

        fv_log("Exiting...");

        fv_main_context_remove_source(quit_source);
}

static int
run_network(void)
{
        struct fv_network *nw;
        int ret = EXIT_SUCCESS;
        struct fv_error *error = NULL;

        nw = fv_network_new();

        if (!add_addresses(nw, &error)) {
                fprintf(stderr, "%s\n", error->message);
                fv_error_clear(&error);
                ret = EXIT_FAILURE;
        } else if (!set_log_file(&error)) {
                fprintf(stderr, "%s\n", error->message);
                fv_error_clear(&error);
                ret = EXIT_FAILURE;
        } else {
                run_main_loop(nw);

                fv_log_close();
        }

        fv_network_free(nw);

        return ret;
}

int
main(int argc, char **argv)
{
        struct fv_main_context *mc;
        struct fv_error *error = NULL;
        int ret = EXIT_SUCCESS;

        if (!process_arguments(argc, argv, &error)) {
                fprintf(stderr, "%s\n", error->message);
                return EXIT_FAILURE;
        }

        mc = fv_main_context_get_default(&error);

        if (mc == NULL) {
                fprintf(stderr, "%s\n", error->message);
                return EXIT_FAILURE;
        }

        ret = run_network();

        fv_main_context_free(mc);

        free_addresses(option_listen_addresses);

        return ret;
}
