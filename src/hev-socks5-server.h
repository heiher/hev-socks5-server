/*
 ============================================================================
 Name        : hev-socks5-server.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2020 Heiher.
 Description : Socks5 Server
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SERVER_H__
#define __HEV_SOCKS5_SERVER_H__

int hev_socks5_server_init (void);
void hev_socks5_server_fini (void);

int hev_socks5_server_run (void);

#endif /* __HEV_SOCKS5_SERVER_H__ */
