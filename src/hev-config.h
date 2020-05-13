/*
 ============================================================================
 Name        : hev-config.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2020 Heiher.
 Description : Config
 ============================================================================
 */

#ifndef __HEV_CONFIG_H__
#define __HEV_CONFIG_H__

#include <netinet/in.h>

typedef enum _HevConfigAuthMethod HevConfigAuthMethod;

enum _HevConfigAuthMethod
{
    HEV_CONFIG_AUTH_METHOD_NONE = 0,
    HEV_CONFIG_AUTH_METHOD_USERPASS = 2,
};

int hev_config_init (const char *config_path);
void hev_config_fini (void);

unsigned int hev_config_get_workers (void);

struct sockaddr *hev_config_get_listen_address (socklen_t *addr_len);
struct sockaddr *hev_config_get_dns_address (socklen_t *addr_len);

int hev_config_get_ipv6_first (void);

unsigned int hev_config_get_auth_method (void);
const char *hev_config_get_auth_username (void);
const char *hev_config_get_auth_password (void);

const char *hev_config_get_misc_pid_file (void);

int hev_config_get_misc_limit_nofile (void);

const char *hev_config_get_misc_log_file (void);
const char *hev_config_get_misc_log_level (void);

#endif /* __HEV_CONFIG_H__ */
