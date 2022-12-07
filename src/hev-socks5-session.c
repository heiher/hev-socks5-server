/*
 ============================================================================
 Name        : hev-socks5-session.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Session
 ============================================================================
 */

#include <string.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

#include <hev-memory-allocator.h>

#include "hev-misc.h"
#include "hev-logger.h"
#include "hev-config.h"

#include "hev-socks5-session.h"

HevSocks5Session *
hev_socks5_session_new (int fd)
{
    HevSocks5Session *self;
    int res;

    self = hev_malloc0 (sizeof (HevSocks5Session));
    if (!self)
        return NULL;

    res = hev_socks5_session_construct (self, fd);
    if (res < 0) {
        hev_free (self);
        return NULL;
    }

    LOG_D ("%p socks5 session new", self);

    return self;
}

void
hev_socks5_session_terminate (HevSocks5Session *self)
{
    LOG_D ("%p socks5 session terminate", self);

    hev_socks5_set_timeout (HEV_SOCKS5 (self), 0);
    hev_task_wakeup (self->task);
}

static int
hev_socks5_session_bind (HevSocks5 *self, int fd)
{
    const char *saddr;
    const char *iface;

    LOG_D ("%p socks5 session bind", self);

    saddr = hev_config_get_bind_address ();
    iface = hev_config_get_bind_interface ();

    if (saddr) {
        struct sockaddr_in6 addr;
        int res;

        res = hev_netaddr_resolve (&addr, saddr, NULL);
        if (res < 0)
            return -1;

        res = bind (fd, (struct sockaddr *)&addr, sizeof (addr));
        if (res < 0)
            return -1;
    }

    if (iface) {
        int res = 0;
#if defined(__linux__)
        struct ifreq ifr = { 0 };

        strncpy (ifr.ifr_name, iface, sizeof (ifr.ifr_name) - 1);
        res = setsockopt (fd, SOL_SOCKET, SO_BINDTODEVICE, &ifr, sizeof (ifr));
#elif defined(__APPLE__) || defined(__MACH__)
        int i;

        i = if_nametoindex (iface);
        if (i == 0) {
            return -1;
        }

        res = setsockopt (fd, IPPROTO_IPV6, IPV6_BOUND_IF, &i, sizeof (i));
#endif
        if (res < 0)
            return -1;
    }

    return 0;
}

int
hev_socks5_session_construct (HevSocks5Session *self, int fd)
{
    int read_write_timeout;
    int connect_timeout;
    const char *user;
    const char *pass;
    int res;

    res = hev_socks5_server_construct (&self->base, fd);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_SESSION_TYPE;

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

static void
hev_socks5_session_destruct (HevObject *base)
{
    HevSocks5Session *self = HEV_SOCKS5_SESSION (base);

    LOG_D ("%p socks5 session destruct", self);

    HEV_SOCKS5_SERVER_TYPE->finalizer (base);
}

HevObjectClass *
hev_socks5_session_class (void)
{
    static HevSocks5SessionClass klass;
    HevSocks5SessionClass *kptr = &klass;
    HevObjectClass *okptr = HEV_OBJECT_CLASS (kptr);

    if (!okptr->name) {
        HevSocks5Class *skptr;

        memcpy (kptr, HEV_SOCKS5_SERVER_TYPE, sizeof (HevSocks5ServerClass));

        okptr->name = "HevSocks5Session";
        okptr->finalizer = hev_socks5_session_destruct;

        skptr = HEV_SOCKS5_CLASS (kptr);
        skptr->binder = hev_socks5_session_bind;
    }

    return okptr;
}
