/*
 ============================================================================
 Name        : hev-main.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 everyone.
 Description : Main
 ============================================================================
 */

#include <stdio.h>
#include <signal.h>

#include "hev-main.h"
#include "hev-task.h"
#include "hev-task-system.h"
#include "hev-config.h"
#include "hev-config-const.h"
#include "hev-socks5-server.h"

static void
show_help (const char *self_path)
{
	printf ("%s CONFIG_PATH\n", self_path);
	printf ("Version: %u.%u.%u\n", MAJOR_VERSION, MINOR_VERSION, MICRO_VERSION);
}

static void
sigint_handler (int signum)
{
	hev_socks5_server_stop ();
}

int
main (int argc, char *argv[])
{
	if (2 != argc) {
		show_help (argv[0]);
		return -1;
	}

	if (0 > hev_config_init (argv[1]))
		return -1;

	if (0 > hev_task_system_init ())
		return -2;

	if (signal (SIGPIPE, SIG_IGN) == SIG_ERR)
		return -3;

	if (0 > hev_socks5_server_init ())
		return -4;

	if (signal (SIGINT, sigint_handler) == SIG_ERR)
		return -5;

	hev_socks5_server_start ();

	hev_task_system_run ();

	hev_socks5_server_fini ();

	hev_task_system_fini ();

	hev_config_fini ();

	return 0;
}

