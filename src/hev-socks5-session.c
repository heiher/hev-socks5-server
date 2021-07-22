/*
 ============================================================================
 Name        : hev-socks5-session.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Session
 ============================================================================
 */

#include <hev-memory-allocator.h>

#include "hev-logger.h"
#include "hev-config.h"

#include "hev-socks5-session.h"

static HevSocks5SessionClass _klass = {
    {
        {
            .name = "HevSocks5Session",
            .finalizer = hev_socks5_session_destruct,
        },
    },
};

int
hev_socks5_session_construct (HevSocks5Session *self)
{
    int read_write_timeout;
    int connect_timeout;
    const char *user;
    const char *pass;
    int res;

    res = hev_socks5_server_construct (&self->base);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session construct", self);

    HEV_SOCKS5 (self)->klass = HEV_SOCKS5_CLASS (&_klass);

    user = hev_config_get_auth_username ();
    pass = hev_config_get_auth_password ();
    if (user && pass)
        hev_socks5_set_auth_user_pass (HEV_SOCKS5 (&self->base), user, pass);

    connect_timeout = hev_config_get_misc_connect_timeout ();
    read_write_timeout = hev_config_get_misc_read_write_timeout ();

    hev_socks5_set_timeout (HEV_SOCKS5 (self), read_write_timeout);
    hev_socks5_server_set_connect_timeout (&self->base, connect_timeout);

    return 0;
}

void
hev_socks5_session_destruct (HevSocks5 *base)
{
    HevSocks5Session *self = HEV_SOCKS5_SESSION (base);

    LOG_D ("%p socks5 session destruct", self);

    hev_socks5_server_destruct (base);
}

HevSocks5Session *
hev_socks5_session_new (int fd)
{
    HevSocks5Session *self;
    int res;

    self = hev_malloc0 (sizeof (HevSocks5Session));
    if (!self)
        return NULL;

    LOG_D ("%p socks5 session new", self);

    res = hev_socks5_session_construct (self);
    if (res < 0) {
        hev_free (self);
        return NULL;
    }

    HEV_SOCKS5 (self)->fd = fd;

    return self;
}

void
hev_socks5_session_terminate (HevSocks5Session *self)
{
    LOG_D ("%p socks5 session terminate", self);

    hev_socks5_set_timeout (HEV_SOCKS5 (self), 0);
    hev_task_wakeup (self->task);
}
