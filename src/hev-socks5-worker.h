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

typedef struct _HevSocks5Worker HevSocks5Worker;

HevSocks5Worker *hev_socks5_worker_new (int fd);
void hev_socks5_worker_destroy (HevSocks5Worker *self);

void hev_socks5_worker_start (HevSocks5Worker *self);
void hev_socks5_worker_stop (HevSocks5Worker *self);
void hev_socks5_worker_reload (HevSocks5Worker *self);

#endif /* __HEV_SOCKS5_WORKER_H__ */
