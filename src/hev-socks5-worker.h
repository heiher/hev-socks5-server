/*
 ============================================================================
 Name        : hev-socks5-worker.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Worker
 ============================================================================
 */

#ifndef __HEV_SOCKS5_WORKER_H__
#define __HEV_SOCKS5_WORKER_H__

#include <hev-socks5-authenticator.h>

typedef struct _HevSocks5Worker HevSocks5Worker;

HevSocks5Worker *hev_socks5_worker_new (void);
void hev_socks5_worker_destroy (HevSocks5Worker *self);
int hev_socks5_worker_init (HevSocks5Worker *self, int fd);

void hev_socks5_worker_start (HevSocks5Worker *self);
void hev_socks5_worker_stop (HevSocks5Worker *self);
void hev_socks5_worker_reload (HevSocks5Worker *self);

void hev_socks5_worker_set_auth (HevSocks5Worker *self,
                                 HevSocks5Authenticator *auth);

#endif /* __HEV_SOCKS5_WORKER_H__ */
