/*
 ============================================================================
 Name        : hev-config.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Config
 ============================================================================
 */

#ifndef __HEV_CONFIG_H__
#define __HEV_CONFIG_H__

#include <netinet/in.h>

int hev_config_init (const char *path);
void hev_config_fini (void);

unsigned int hev_config_get_workers (void);

const char *hev_config_get_listen_address (void);
const char *hev_config_get_listen_port (void);

const char *hev_config_get_auth_username (void);
const char *hev_config_get_auth_password (void);

int hev_config_get_misc_limit_nofile (void);
const char *hev_config_get_misc_pid_file (void);
const char *hev_config_get_misc_log_file (void);
int hev_config_get_misc_log_level (void);

#endif /* __HEV_CONFIG_H__ */
