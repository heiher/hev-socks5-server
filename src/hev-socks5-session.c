/*
 ============================================================================
 Name        : hev-socks5-session.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2024 hev
 Description : Socks5 Session
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>

#include <hev-memory-allocator.h>

#include "hev-misc.h"
#include "hev-logger.h"
#include "hev-config.h"
#include "hev-socks5-user-mark.h"

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
hev_socks5_session_bind (HevSocks5 *self, int fd, const struct sockaddr *dest)
{
    HevSocks5Server *srv = HEV_SOCKS5_SERVER (self);
    const char *saddr;
    const char *iface;
    int family;
    int res;

    LOG_D ("%p socks5 session bind", self);

    if (IN6_IS_ADDR_V4MAPPED (&((struct sockaddr_in6 *)dest)->sin6_addr))
        family = AF_INET;
    else
        family = AF_INET6;

    saddr = hev_config_get_bind_address (family);
    iface = hev_config_get_bind_interface ();

    if (saddr) {
        struct sockaddr_in6 addr;

        res = hev_netaddr_resolve (&addr, saddr, NULL);
        if (res < 0)
            return -1;

        res = bind (fd, (struct sockaddr *)&addr, sizeof (addr));
        if (res < 0)
            return -1;
    }

    if (iface) {
        res = set_sock_bind (fd, iface);
        if (res < 0)
            return -1;
    }

    if (srv->user) {
        HevSocks5UserMark *user = HEV_SOCKS5_USER_MARK (srv->user);

        if (user->mark) {
            res = set_sock_mark (fd, user->mark);
            if (res < 0)
                return -1;
        }
    }

    return 0;
}

static int
hev_socks5_session_udp_bind (HevSocks5Server *self, int sock)
{
    struct sockaddr_in6 addr;
    const char *saddr;
    const char *sport;
    socklen_t alen;
    int ipv6_only;
    int res;

    LOG_D ("%p socks5 session udp bind", self);

    alen = sizeof (struct sockaddr_in6);
    saddr = hev_config_get_udp_listen_address ();
    sport = hev_config_get_udp_listen_port ();
    ipv6_only = hev_config_get_listen_ipv6_only ();

    if (ipv6_only) {
        int one = 1;

        res = setsockopt (sock, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof (one));
        if (res < 0)
            return -1;
    }

    if (saddr) {
        res = hev_netaddr_resolve (&addr, saddr, NULL);
        if (res < 0)
            return -1;
    } else {
        int fd;

        fd = HEV_SOCKS5 (self)->fd;
        res = getsockname (fd, (struct sockaddr *)&addr, &alen);
        if (res < 0)
            return -1;
    }

    addr.sin6_port = sport ? htons (strtoul (sport, NULL, 10)) : 0;

    res = bind (sock, (struct sockaddr *)&addr, alen);
    if (res < 0)
        return -1;

    return 0;
}

int
hev_socks5_session_construct (HevSocks5Session *self, int fd)
{
    int read_write_timeout;
    int connect_timeout;
    int addr_type;
    int res;

    res = hev_socks5_server_construct (&self->base, fd);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_SESSION_TYPE;

    addr_type = hev_config_get_domain_address_type ();
    connect_timeout = hev_config_get_misc_connect_timeout ();
    read_write_timeout = hev_config_get_misc_read_write_timeout ();

    hev_socks5_set_domain_addr_type (HEV_SOCKS5 (self), addr_type);
    hev_socks5_set_timeout (HEV_SOCKS5 (self), read_write_timeout);
    hev_socks5_server_set_connect_timeout (&self->base, connect_timeout);

    return 0;
}

static void
hev_socks5_session_destruct (HevObject *base)
{
    HevSocks5Session *self = HEV_SOCKS5_SESSION (base);

    LOG_D ("%p socks5 session destruct", self);

    HEV_SOCKS5_SERVER_TYPE->destruct (base);
}

HevObjectClass *
hev_socks5_session_class (void)
{
    static HevSocks5SessionClass klass;
    HevSocks5SessionClass *kptr = &klass;
    HevObjectClass *okptr = HEV_OBJECT_CLASS (kptr);

    if (!okptr->name) {
        HevSocks5ServerClass *sskptr;
        HevSocks5Class *skptr;

        memcpy (kptr, HEV_SOCKS5_SERVER_TYPE, sizeof (HevSocks5ServerClass));

        okptr->name = "HevSocks5Session";
        okptr->destruct = hev_socks5_session_destruct;

        skptr = HEV_SOCKS5_CLASS (kptr);
        skptr->binder = hev_socks5_session_bind;

        sskptr = HEV_SOCKS5_SERVER_CLASS (kptr);
        sskptr->binder = hev_socks5_session_udp_bind;
    }

    return okptr;
}
