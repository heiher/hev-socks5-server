/*
 ============================================================================
 Name        : hev-config.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2020 Heiher.
 Description : Config
 ============================================================================
 */

#include <stdio.h>
#include <arpa/inet.h>
#include <yaml.h>

#include "hev-config.h"
#include "hev-config-const.h"

static struct sockaddr_in6 listen_address;
static struct sockaddr_in6 dns_address;
static unsigned int workers;
static int ipv6_first;
static unsigned int auth_method;
static char username[256];
static char password[256];
static char log_file[1024];
static char log_level[16];
static char pid_file[1024];
static int limit_nofile = -2;

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

static int
hev_config_parse_main (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;
    int port = 0;
    const char *listen_addr = NULL, *dns_addr = NULL;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "workers"))
            workers = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "port"))
            port = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "listen-address"))
            listen_addr = value;
        else if (0 == strcmp (key, "dns-address"))
            dns_addr = value;
        else if (0 == strcmp (key, "ipv6-first"))
            ipv6_first = (0 == strcasecmp (value, "true")) ? 1 : 0;
    }

    if (!workers) {
        fprintf (stderr, "Can't found main.workers!\n");
        return -1;
    }

    if (!port) {
        fprintf (stderr, "Can't found main.port!\n");
        return -1;
    }

    if (!listen_addr) {
        fprintf (stderr, "Can't found main.listen-address!\n");
        return -1;
    }

    if (address_to_sockaddr (listen_addr, port, &listen_address) < 0) {
        fprintf (stderr, "Parse main.listen-address!\n");
        return -1;
    }

    if (!dns_addr) {
        fprintf (stderr, "Can't found main.dns-address!\n");
        return -1;
    }

    if (address_to_sockaddr (dns_addr, 53, &dns_address) < 0) {
        fprintf (stderr, "Parse main.dns-address!\n");
        return -1;
    }

    return 0;
}

static int
hev_config_parse_auth (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;
    const char *user = NULL, *pass = NULL;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "username"))
            user = value;
        else if (0 == strcmp (key, "password"))
            pass = value;
    }

    if (user && pass) {
        strncpy (username, user, 255);
        strncpy (password, pass, 255);
        auth_method = HEV_CONFIG_AUTH_METHOD_USERPASS;
    }

    return 0;
}

static int
hev_config_parse_misc (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;

    if (!base || YAML_MAPPING_NODE != base->type)
        return -1;

    for (pair = base->data.mapping.pairs.start;
         pair < base->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key, *value;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        key = (const char *)node->data.scalar.value;

        node = yaml_document_get_node (doc, pair->value);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;
        value = (const char *)node->data.scalar.value;

        if (0 == strcmp (key, "pid-file"))
            strncpy (pid_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-file"))
            strncpy (log_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-level"))
            strncpy (log_level, value, 16 - 1);
        else if (0 == strcmp (key, "limit-nofile"))
            limit_nofile = strtol (value, NULL, 10);
    }

    return 0;
}

static int
hev_config_parse_doc (yaml_document_t *doc)
{
    yaml_node_t *root;
    yaml_node_pair_t *pair;

    root = yaml_document_get_root_node (doc);
    if (!root || YAML_MAPPING_NODE != root->type)
        return -1;

    for (pair = root->data.mapping.pairs.start;
         pair < root->data.mapping.pairs.top; pair++) {
        yaml_node_t *node;
        const char *key;
        int res = 0;

        if (!pair->key || !pair->value)
            break;

        node = yaml_document_get_node (doc, pair->key);
        if (!node || YAML_SCALAR_NODE != node->type)
            break;

        key = (const char *)node->data.scalar.value;
        node = yaml_document_get_node (doc, pair->value);

        if (0 == strcmp (key, "main"))
            res = hev_config_parse_main (doc, node);
        else if (0 == strcmp (key, "auth"))
            res = hev_config_parse_auth (doc, node);
        else if (0 == strcmp (key, "misc"))
            res = hev_config_parse_misc (doc, node);

        if (res < 0)
            return -1;
    }

    return 0;
}

int
hev_config_init (const char *config_path)
{
    yaml_parser_t parser;
    yaml_document_t doc;
    FILE *fp;
    int res = -1;

    if (!yaml_parser_initialize (&parser))
        goto exit;

    fp = fopen (config_path, "r");
    if (!fp) {
        fprintf (stderr, "Open %s failed!\n", config_path);
        goto exit_free_parser;
    }

    yaml_parser_set_input_file (&parser, fp);
    if (!yaml_parser_load (&parser, &doc)) {
        fprintf (stderr, "Parse %s failed!\n", config_path);
        goto exit_close_fp;
    }

    res = hev_config_parse_doc (&doc);
    yaml_document_delete (&doc);

exit_close_fp:
    fclose (fp);
exit_free_parser:
    yaml_parser_delete (&parser);
exit:
    return res;
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

const char *
hev_config_get_misc_log_file (void)
{
    if ('\0' == log_file[0])
        return NULL;
    if (0 == strcmp (log_file, "null"))
        return NULL;

    return log_file;
}

const char *
hev_config_get_misc_log_level (void)
{
    if ('\0' == log_level[0])
        return "warn";

    return log_level;
}
