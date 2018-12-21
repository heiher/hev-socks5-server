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
static unsigned int auth_method;
static char username[256];
static char password[256];

int
hev_config_init (const char *config_path)
{
    dictionary *ini_dict;

    ini_dict = iniparser_load (config_path);
    if (!ini_dict) {
        fprintf (stderr, "Load config from file %s failed!\n", config_path);
        return -1;
    }

    /* Main:ListenAddress */
    char *address = iniparser_getstring (ini_dict, "Main:ListenAddress", NULL);
    if (!address) {
        fprintf (stderr, "Get Main:ListenAddress from file %s failed!\n",
                 config_path);
        iniparser_freedict (ini_dict);
        return -2;
    }
    strncpy (listen_address, address, 15);

    /* Main:Port */
    port = iniparser_getint (ini_dict, "Main:Port", -1);
    if (-1 == (int)port) {
        fprintf (stderr, "Get Main:Port from file %s failed!\n", config_path);
        iniparser_freedict (ini_dict);
        return -3;
    }

    /* Main:DNSAddress */
    address = iniparser_getstring (ini_dict, "Main:DNSAddress", NULL);
    if (!address) {
        fprintf (stderr, "Get Main:DNSAddress from file %s failed!\n",
                 config_path);
        iniparser_freedict (ini_dict);
        return -4;
    }
    strncpy (dns_address, address, 15);

    /* Main:Workers */
    workers = iniparser_getint (ini_dict, "Main:Workers", 1);
    if (workers <= 0)
        workers = 1;

    /* Auth:Username */
    char *user = iniparser_getstring (ini_dict, "Auth:Username", NULL);
    /* Auth:Password */
    char *pass = iniparser_getstring (ini_dict, "Auth:Password", NULL);
    if (user && pass) {
        strncpy (username, user, 255);
        strncpy (password, pass, 255);
        auth_method = HEV_CONFIG_AUTH_METHOD_USERPASS;
    }

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

unsigned int
hev_config_get_auth_method (void)
{
    return auth_method;
}

const char *
hev_config_get_auth_username (void)
{
    return username;
}

const char *
hev_config_get_auth_password (void)
{
    return password;
}
