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

int hev_config_init (const char *config_path);
void hev_config_fini (void);

unsigned int hev_config_get_workers (void);

const char *hev_config_get_listen_address (void);
unsigned short hev_config_get_port (void);
const char *hev_config_get_dns_address (void);

#endif /* __HEV_CONFIG_H__ */
