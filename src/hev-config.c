/*
 ============================================================================
 Name        : hev-config.c
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2024 hev
 Description : Config
 ============================================================================
 */

#include <stdio.h>
#include <arpa/inet.h>

#include <yaml.h>
#include <hev-socks5.h>
#include <hev-socks5-proto.h>

#include "hev-logger.h"
#include "hev-config.h"

static unsigned int workers;
static int listen_ipv6_only;
static char listen_address[256];
static char listen_port[8];
static char udp_listen_address[256];
static char udp_listen_port[8];
static char bind_address[2][256];
static char bind_interface[256];
static char auth_file[1024];
static char username[256];
static char password[256];
static char log_file[1024];
static char pid_file[1024];
static int task_stack_size = 8192;
static int udp_recv_buffer_size = 524288;
static int connect_timeout = 5000;
static int read_write_timeout = 60000;
static int limit_nofile = 65535;
static int log_level = HEV_LOGGER_WARN;
static int addr_family = HEV_SOCKS5_ADDR_FAMILY_UNSPEC;
static unsigned int socket_mark;

static int
hev_config_parse_main (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;
    const char *addr = NULL;
    const char *port = NULL;
    const char *mark = NULL;
    const char *udp_addr = NULL;
    const char *udp_port = NULL;
    const char *bind_saddr = NULL;
    const char *bind_saddr4 = NULL;
    const char *bind_saddr6 = NULL;
    const char *bind_iface = NULL;
    const char *addr_type = NULL;

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
            port = value;
        else if (0 == strcmp (key, "listen-address"))
            addr = value;
        else if (0 == strcmp (key, "udp-port"))
            udp_port = value;
        else if (0 == strcmp (key, "udp-listen-address"))
            udp_addr = value;
        else if (0 == strcmp (key, "listen-ipv6-only"))
            listen_ipv6_only = (0 == strcasecmp (value, "true")) ? 1 : 0;
        else if (0 == strcmp (key, "bind-address"))
            bind_saddr = value;
        else if (0 == strcmp (key, "bind-address-v4"))
            bind_saddr4 = value;
        else if (0 == strcmp (key, "bind-address-v6"))
            bind_saddr6 = value;
        else if (0 == strcmp (key, "bind-interface"))
            bind_iface = value;
        else if (0 == strcmp (key, "domain-address-type"))
            addr_type = value;
        else if (0 == strcmp (key, "mark"))
            mark = value;
    }

    if (!workers) {
        fprintf (stderr, "Can't found main.workers!\n");
        return -1;
    }

    if (!port) {
        fprintf (stderr, "Can't found main.port!\n");
        return -1;
    }

    if (!addr) {
        fprintf (stderr, "Can't found main.listen-address!\n");
        return -1;
    }

#ifdef __MSYS__
    if (workers > 1) {
        fprintf (stderr, "Only supports one worker on Windows.\n");
        workers = 1;
    }
#endif

    strncpy (listen_port, port, 8 - 1);
    strncpy (listen_address, addr, 256 - 1);

    if (udp_port)
        strncpy (udp_listen_port, udp_port, 8 - 1);
    if (udp_addr)
        strncpy (udp_listen_address, udp_addr, 256 - 1);

    if (bind_saddr4 && bind_saddr4[0] != '\0')
        strncpy (bind_address[0], bind_saddr4, 256 - 1);
    else if (bind_saddr && bind_saddr[0] != '\0')
        strncpy (bind_address[0], bind_saddr, 256 - 1);
    if (bind_saddr6 && bind_saddr6[0] != '\0')
        strncpy (bind_address[1], bind_saddr6, 256 - 1);
    else if (bind_saddr && bind_saddr[0] != '\0')
        strncpy (bind_address[1], bind_saddr, 256 - 1);

    if (bind_iface)
        strncpy (bind_interface, bind_iface, 256 - 1);

    if (addr_type) {
        if (0 == strcmp (addr_type, "ipv4"))
            addr_family = HEV_SOCKS5_ADDR_FAMILY_IPV4;
        else if (0 == strcmp (addr_type, "ipv6"))
            addr_family = HEV_SOCKS5_ADDR_FAMILY_IPV6;
    }

    if (mark)
        socket_mark = strtoul (mark, NULL, 0);

    return 0;
}

static int
hev_config_parse_auth (yaml_document_t *doc, yaml_node_t *base)
{
    yaml_node_pair_t *pair;
    const char *user = NULL, *pass = NULL, *file = NULL;

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
        else if (0 == strcmp (key, "file"))
            file = value;
    }

    if (file) {
        strncpy (auth_file, file, 1023);
    } else if (user && pass) {
        strncpy (username, user, 255);
        strncpy (password, pass, 255);
    }

    return 0;
}

static int
hev_config_parse_log_level (const char *value)
{
    if (0 == strcmp (value, "debug"))
        return HEV_LOGGER_DEBUG;
    else if (0 == strcmp (value, "info"))
        return HEV_LOGGER_INFO;
    else if (0 == strcmp (value, "error"))
        return HEV_LOGGER_ERROR;

    return HEV_LOGGER_WARN;
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

        if (0 == strcmp (key, "task-stack-size"))
            task_stack_size = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "udp-recv-buffer-size"))
            udp_recv_buffer_size = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "connect-timeout"))
            connect_timeout = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "read-write-timeout"))
            read_write_timeout = strtoul (value, NULL, 10);
        else if (0 == strcmp (key, "pid-file"))
            strncpy (pid_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-file"))
            strncpy (log_file, value, 1024 - 1);
        else if (0 == strcmp (key, "log-level"))
            log_level = hev_config_parse_log_level (value);
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
hev_config_init_from_file (const char *path)
{
    yaml_parser_t parser;
    yaml_document_t doc;
    FILE *fp;
    int res = -1;

    if (!yaml_parser_initialize (&parser))
        goto exit;

    fp = fopen (path, "r");
    if (!fp) {
        fprintf (stderr, "Open %s failed!\n", path);
        goto exit_free_parser;
    }

    yaml_parser_set_input_file (&parser, fp);
    if (!yaml_parser_load (&parser, &doc)) {
        fprintf (stderr, "Parse %s failed!\n", path);
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

int
hev_config_init_from_str (const unsigned char *config_str,
                          unsigned int config_len)
{
    yaml_parser_t parser;
    yaml_document_t doc;
    int res = -1;

    if (!yaml_parser_initialize (&parser))
        goto exit;

    yaml_parser_set_input_string (&parser, config_str, config_len);
    if (!yaml_parser_load (&parser, &doc)) {
        fprintf (stderr, "Failed to parse config.");
        goto exit_free_parser;
    }

    res = hev_config_parse_doc (&doc);
    yaml_document_delete (&doc);

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

const char *
hev_config_get_listen_address (void)
{
    return listen_address;
}

const char *
hev_config_get_listen_port (void)
{
    return listen_port;
}

const char *
hev_config_get_udp_listen_address (void)
{
    if ('\0' == udp_listen_address[0])
        return NULL;

    return udp_listen_address;
}

const char *
hev_config_get_udp_listen_port (void)
{
    if ('\0' == udp_listen_port[0])
        return NULL;

    return udp_listen_port;
}

int
hev_config_get_listen_ipv6_only (void)
{
    return listen_ipv6_only;
}

const char *
hev_config_get_bind_address (int family)
{
    int idx = family == AF_INET6;

    if ('\0' == bind_address[idx][0])
        return NULL;

    return bind_address[idx];
}

const char *
hev_config_get_bind_interface (void)
{
    if ('\0' == bind_interface[0])
        return NULL;

    return bind_interface;
}

int
hev_config_get_address_family (void)
{
    return addr_family;
}

unsigned int
hev_config_get_socket_mark (void)
{
    return socket_mark;
}

const char *
hev_config_get_auth_file (void)
{
    if ('\0' == auth_file[0])
        return NULL;

    return auth_file;
}

const char *
hev_config_get_auth_username (void)
{
    if ('\0' == username[0])
        return NULL;

    return username;
}

const char *
hev_config_get_auth_password (void)
{
    if ('\0' == password[0])
        return NULL;

    return password;
}

int
hev_config_get_misc_task_stack_size (void)
{
    return task_stack_size;
}

int
hev_config_get_misc_udp_recv_buffer_size (void)
{
    return udp_recv_buffer_size;
}

int
hev_config_get_misc_connect_timeout (void)
{
    return connect_timeout;
}

int
hev_config_get_misc_read_write_timeout (void)
{
    return read_write_timeout;
}

int
hev_config_get_misc_limit_nofile (void)
{
    return limit_nofile;
}

const char *
hev_config_get_misc_pid_file (void)
{
    if ('\0' == pid_file[0])
        return NULL;

    return pid_file;
}

const char *
hev_config_get_misc_log_file (void)
{
    if ('\0' == log_file[0])
        return "stderr";

    return log_file;
}

int
hev_config_get_misc_log_level (void)
{
    return log_level;
}
