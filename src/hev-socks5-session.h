/*
 ============================================================================
 Name        : hev-socks5-session.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 everyone.
 Description : Socks5 session
 ============================================================================
 */

#ifndef __HEV_SOCKS5_SESSION_H__
#define __HEV_SOCKS5_SESSION_H__

#include <netinet/in.h>

#include "hev-task.h"

typedef struct _HevSocks5SessionBase HevSocks5SessionBase;
typedef struct _HevSocks5Session HevSocks5Session;
typedef void (*HevSocks5SessionCloseNotify) (HevSocks5Session *self,
                                             void *data);

struct _HevSocks5SessionBase
{
    HevSocks5SessionBase *prev;
    HevSocks5SessionBase *next;
    HevTask *task;
    int hp;
};

HevSocks5Session *hev_socks5_session_new (int client_fd,
                                          struct sockaddr_in6 *saddr,
                                          HevSocks5SessionCloseNotify notify,
                                          void *notify_data);

HevSocks5Session *hev_socks5_session_ref (HevSocks5Session *self);
void hev_socks5_session_unref (HevSocks5Session *self);

void hev_socks5_session_run (HevSocks5Session *self);

#endif /* __HEV_SOCKS5_SESSION_H__ */
