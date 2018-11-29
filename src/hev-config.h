/*
 ============================================================================
 Name        : hev-config.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 Heiher.
 Description : Config
 ============================================================================
 */

#ifndef __HEV_CONFIG_H__
#define __HEV_CONFIG_H__

typedef enum _HevConfigAuthMethod HevConfigAuthMethod;

enum _HevConfigAuthMethod
{
    HEV_CONFIG_AUTH_METHOD_NONE = 0,
    HEV_CONFIG_AUTH_METHOD_USERPASS = 2,
};

int hev_config_init (const char *config_path);
void hev_config_fini (void);

unsigned int hev_config_get_workers (void);

const char *hev_config_get_listen_address (void);
unsigned short hev_config_get_port (void);
const char *hev_config_get_dns_address (void);

unsigned int hev_config_get_auth_method (void);
const char *hev_config_get_auth_username (void);
const char *hev_config_get_auth_password (void);

#endif /* __HEV_CONFIG_H__ */
