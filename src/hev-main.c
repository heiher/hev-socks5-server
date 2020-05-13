/*
 ============================================================================
 Name        : hev-main.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2020 Heiher.
 Description : Main
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/resource.h>

#include "hev-config.h"
#include "hev-config-const.h"
#include "hev-logger.h"
#include "hev-socks5-server.h"

#include "hev-main.h"

static void
show_help (const char *self_path)
{
    printf ("%s CONFIG_PATH\n", self_path);
    printf ("Version: %u.%u.%u\n", MAJOR_VERSION, MINOR_VERSION, MICRO_VERSION);
}

static void
run_as_daemon (const char *pid_file)
{
    FILE *fp;

    fp = fopen (pid_file, "w+");
    if (!fp) {
        LOG_E ("Open pid file %s failed!", pid_file);
        return;
    }

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (daemon (0, 0)) {
        /* ignore return value */
    }
#pragma GCC diagnostic pop

    fprintf (fp, "%u", getpid ());
    fclose (fp);
}

static int
set_limit_nofile (int limit_nofile)
{
    struct rlimit limit = {
        .rlim_cur = RLIM_INFINITY,
        .rlim_max = RLIM_INFINITY,
    };

    if (-1 > limit_nofile)
        return 0;

    if (0 <= limit_nofile) {
        limit.rlim_cur = limit_nofile;
        limit.rlim_max = limit_nofile;
    }

    return setrlimit (RLIMIT_NOFILE, &limit);
}

int
main (int argc, char *argv[])
{
    const char *pid_file;
    int limit_nofile;

    if (2 != argc) {
        show_help (argv[0]);
        return -1;
    }

    if (0 > hev_config_init (argv[1]))
        return -2;

    if (0 > hev_logger_init ())
        return -3;

    if (0 > hev_socks5_server_init ())
        return -4;

    limit_nofile = hev_config_get_misc_limit_nofile ();
    if (0 > set_limit_nofile (limit_nofile)) {
        LOG_E ("Set limit nofile failed!");
        return -5;
    }

    pid_file = hev_config_get_misc_pid_file ();
    if (pid_file)
        run_as_daemon (pid_file);

    hev_socks5_server_run ();

    hev_socks5_server_fini ();

    hev_logger_fini ();

    hev_config_fini ();

    return 0;
}
