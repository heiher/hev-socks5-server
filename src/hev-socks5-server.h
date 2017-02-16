/*
 ============================================================================
 Name        : hev-socks5-server.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 everyone.
 Description : Socks5 server
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SERVER_H__
#define __HEV_SOCKS5_SERVER_H__

int hev_socks5_server_init (void);
void hev_socks5_server_fini (void);

void hev_socks5_server_run (void);

#endif /* __HEV_SOCKS5_SERVER_H__ */

