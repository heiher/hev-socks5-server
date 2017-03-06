/*
 ============================================================================
 Name        : hev-config.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 Heiher.
 Description : Config
 ============================================================================
 */

#include <stdio.h>
#include <iniparser.h>

#include "hev-config.h"
#include "hev-config-const.h"

static char listen_address[16];
static char dns_address[16];
static unsigned short port;
static unsigned int workers;

int
hev_config_init (const char *config_path)
{
	dictionary *ini_dict;

	ini_dict = iniparser_load (config_path);
	if (!ini_dict) {
		fprintf (stderr, "Load config from file %s failed!\n",
					config_path);
		return -1;
	}

	/* Main:ListenAddress */
	char *address = iniparser_getstring (ini_dict, "Main:ListenAddress", NULL);
	if (!address) {
		fprintf (stderr, "Get Main:ListenAddress from file %s failed!\n", config_path);
		iniparser_freedict (ini_dict);
		return -2;
	}
	strncpy (listen_address, address, 16);

	/* Main:Port */
	port = iniparser_getint (ini_dict, "Main:Port", -1);
	if (-1 == port) {
		fprintf (stderr, "Get Main:Port from file %s failed!\n",
					config_path);
		iniparser_freedict (ini_dict);
		return -3;
	}

	/* Main:DNSAddress */
	address = iniparser_getstring (ini_dict, "Main:DNSAddress", NULL);
	if (!address) {
		fprintf (stderr, "Get Main:DNSAddress from file %s failed!\n", config_path);
		iniparser_freedict (ini_dict);
		return -4;
	}
	strncpy (dns_address, address, 16);

	/* Main:Workers */
	workers = iniparser_getint (ini_dict, "Main:Workers", 1);
	if (workers <= 0)
		workers = 1;

	iniparser_freedict (ini_dict);

	return 0;
}

void
hev_config_fini (void)
{
}

unsigned int
hev_config_get_workers (void)
{
	return workers;
}

const char *
hev_config_get_listen_address (void)
{
	return listen_address;
}

unsigned short
hev_config_get_port (void)
{
	return port;
}

const char *
hev_config_get_dns_address (void)
{
	return dns_address;
}

