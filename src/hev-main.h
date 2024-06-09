/*
 ============================================================================
 Name        : hev-main.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2024 hev
 Description : Main
 ============================================================================
 */

#ifndef __HEV_MAIN_H__
#define __HEV_MAIN_H__

#ifdef __cplusplus
extern "C" {
#endif

/**
 * hev_socks5_server_main_from_file:
 * @config_path: config file path
 *
 * Start and run the socks5 server, this function will blocks until the
 * hev_socks5_server_quit is called or an error occurs.
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.6.7
 */
int hev_socks5_server_main_from_file (const char *config_path);

/**
 * hev_socks5_server_main_from_str:
 * @config_str: string config
 * @config_len: the byte length of string config
 *
 * Start and run the socks5 server, this function will blocks until the
 * hev_socks5_server_quit is called or an error occurs.
 *
 * Returns: returns zero on successful, otherwise returns -1.
 *
 * Since: 2.6.7
 */
int hev_socks5_server_main_from_str (const unsigned char *config_str,
                                     unsigned int config_len);

/**
 * hev_socks5_server_quit:
 *
 * Stop the socks5 server.
 *
 * Since: 2.6.7
 */
void hev_socks5_server_quit (void);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_MAIN_H__ */
