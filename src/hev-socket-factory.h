/*
 ============================================================================
 Name        : hev-socket-factory.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2022 hev
 Description : Socket Factory
 ============================================================================
 */

#ifndef __HEV_SOCKET_FACTORY_H__
#define __HEV_SOCKET_FACTORY_H__

typedef struct _HevSocketFactory HevSocketFactory;

HevSocketFactory *hev_socket_factory_new (const char *addr, const char *port,
                                          int ipv6_only);
void hev_socket_factory_destroy (HevSocketFactory *self);

int hev_socket_factory_get (HevSocketFactory *self);

#endif /* __HEV_SOCKET_FACTORY_H__ */
