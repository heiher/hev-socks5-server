/*
 ============================================================================
 Name        : hev-main.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2019 everyone.
 Description : Main
 ============================================================================
 */

#include <stdio.h>
#include <unistd.h>

#include "hev-main.h"
#include "hev-config.h"
#include "hev-config-const.h"
#include "hev-socks5-server.h"

static void
show_help (const char *self_path)
{
    printf ("%s CONFIG_PATH [PID_FILE]\n", self_path);
    printf ("Version: %u.%u.%u\n", MAJOR_VERSION, MINOR_VERSION, MICRO_VERSION);
}

static void
run_as_daemon (const char *pid_file)
{
    FILE *fp;

    fp = fopen (pid_file, "w+");
    if (!fp) {
        fprintf (stderr, "Open pid file %s failed!\n", pid_file);
        return;
    }

    daemon (0, 0);

    fprintf (fp, "%u", getpid ());
    fclose (fp);
}

int
main (int argc, char *argv[])
{
    const char *cfg_file = NULL;
    const char *pid_file = NULL;

    switch (argc) {
    case 3:
        pid_file = argv[2];
    case 2:
        cfg_file = argv[1];
        break;
    default:
        show_help (argv[0]);
        return -1;
    }

    if (0 > hev_config_init (cfg_file))
        return -2;

    if (0 > hev_socks5_server_init ())
        return -3;

    if (pid_file)
        run_as_daemon (pid_file);

    hev_socks5_server_run ();

    hev_socks5_server_fini ();

    hev_config_fini ();

    return 0;
}
