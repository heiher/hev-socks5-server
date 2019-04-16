/*
 ============================================================================
 Name        : hev-config.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2019 Heiher.
 Description : Config
 ============================================================================
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <iniparser.h>

#include "hev-config.h"
#include "hev-config-const.h"

static struct sockaddr_in6 listen_address;
static struct sockaddr_in6 dns_address;
static unsigned int workers;
static int ipv6_first;
static unsigned int auth_method;
static char username[256];
static char password[256];
static char pid_file[1024];
static int limit_nofile;

static int
address_to_sockaddr (const char *address, unsigned short port,
                     struct sockaddr_in6 *addr)
{
    __builtin_bzero (addr, sizeof (*addr));

    addr->sin6_family = AF_INET6;
    addr->sin6_port = htons (port);
    if (inet_pton (AF_INET, address, &addr->sin6_addr.s6_addr[12]) == 1) {
        ((uint16_t *)&addr->sin6_addr)[5] = 0xffff;
    } else {
        if (inet_pton (AF_INET6, address, &addr->sin6_addr) != 1) {
            return -1;
        }
    }

    return 0;
}

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

    /* Main:Port */
    int port = iniparser_getint (ini_dict, "Main:Port", -1);
    if (-1 == port) {
        fprintf (stderr, "Get Main:Port from file %s failed!\n", config_path);
        iniparser_freedict (ini_dict);
        return -3;
    }

    if (address_to_sockaddr (address, port, &listen_address) < 0) {
        fprintf (stderr, "Parse listen address failed!\n");
        iniparser_freedict (ini_dict);
        return -4;
    }

    /* Main:DNSAddress */
    address = iniparser_getstring (ini_dict, "Main:DNSAddress", NULL);
    if (!address) {
        fprintf (stderr, "Get Main:DNSAddress from file %s failed!\n",
                 config_path);
        iniparser_freedict (ini_dict);
        return -5;
    }

    if (address_to_sockaddr (address, 53, &dns_address) < 0) {
        fprintf (stderr, "Parse dns address failed!\n");
        iniparser_freedict (ini_dict);
        return -6;
    }

    /* Main:Workers */
    workers = iniparser_getint (ini_dict, "Main:Workers", 1);
    if (workers <= 0)
        workers = 1;

    /* Main:IPv6First */
    ipv6_first = iniparser_getboolean (ini_dict, "Main:IPv6First", 0);

    /* Auth:Username */
    char *user = iniparser_getstring (ini_dict, "Auth:Username", NULL);
    /* Auth:Password */
    char *pass = iniparser_getstring (ini_dict, "Auth:Password", NULL);
    if (user && pass) {
        strncpy (username, user, 255);
        strncpy (password, pass, 255);
        auth_method = HEV_CONFIG_AUTH_METHOD_USERPASS;
    }

    /* Misc:PidFile */
    char *path = iniparser_getstring (ini_dict, "Misc:PidFile", NULL);
    if (path)
        strncpy (pid_file, path, 1023);

    /* Misc:LimitNOFile */
    limit_nofile = iniparser_getint (ini_dict, "Misc:LimitNOFile", -2);

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

struct sockaddr *
hev_config_get_listen_address (socklen_t *addr_len)
{
    *addr_len = sizeof (listen_address);
    return (struct sockaddr *)&listen_address;
}

struct sockaddr *
hev_config_get_dns_address (socklen_t *addr_len)
{
    *addr_len = sizeof (dns_address);
    return (struct sockaddr *)&dns_address;
}

int
hev_config_get_ipv6_first (void)
{
    return ipv6_first;
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

const char *
hev_config_get_misc_pid_file (void)
{
    if ('\0' == pid_file[0])
        return NULL;

    return pid_file;
}

int
hev_config_get_misc_limit_nofile (void)
{
    return limit_nofile;
}
