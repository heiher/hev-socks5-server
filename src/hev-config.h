/*
 ============================================================================
 Name        : hev-config.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2024 hev
 Description : Config
 ============================================================================
 */

#ifndef __HEV_CONFIG_H__
#define __HEV_CONFIG_H__

int hev_config_init_from_file (const char *config_path);
int hev_config_init_from_str (const unsigned char *config_str,
                              unsigned int config_len);
void hev_config_fini (void);

unsigned int hev_config_get_workers (void);

const char *hev_config_get_listen_address (void);
const char *hev_config_get_listen_port (void);
const char *hev_config_get_udp_listen_address (void);
int hev_config_get_udp_listen_port (void);
int hev_config_get_listen_ipv6_only (void);

const char *hev_config_get_bind_address (int family);
const char *hev_config_get_bind_interface (void);

int hev_config_get_address_family (void);
unsigned int hev_config_get_socket_mark (void);

const char *hev_config_get_auth_file (void);
const char *hev_config_get_auth_username (void);
const char *hev_config_get_auth_password (void);

int hev_config_get_misc_task_stack_size (void);
int hev_config_get_misc_udp_recv_buffer_size (void);
int hev_config_get_misc_connect_timeout (void);
int hev_config_get_misc_read_write_timeout (void);
int hev_config_get_misc_limit_nofile (void);
const char *hev_config_get_misc_pid_file (void);
const char *hev_config_get_misc_log_file (void);
int hev_config_get_misc_log_level (void);

#endif /* __HEV_CONFIG_H__ */
