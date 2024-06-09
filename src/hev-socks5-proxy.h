/*
 ============================================================================
 Name        : hev-socks5-proxy.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Proxy
 ============================================================================
 */

#ifndef __HEV_SOCKS5_PROXY_H__
#define __HEV_SOCKS5_PROXY_H__

int hev_socks5_proxy_init (void);
void hev_socks5_proxy_fini (void);

void hev_socks5_proxy_run (void);
void hev_socks5_proxy_stop (void);

#endif /* __HEV_SOCKS5_PROXY_H__ */
