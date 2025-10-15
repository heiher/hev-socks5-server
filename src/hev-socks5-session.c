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
    int mark = 0;
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
        mark = user->mark;
    }

    if (!mark)
        mark = hev_config_get_socket_mark ();

    if (mark) {
        res = set_sock_mark (fd, mark);
        if (res < 0)
            return -1;
    }

    return 0;
}

static int
hev_socks5_session_udp_bind (HevSocks5Server *self, int sock,
                             struct sockaddr_in6 *src)
{
    struct sockaddr_in6 *dst = src;
    struct sockaddr_in6 addr;
    const char *saddr;
    socklen_t alen;
    int ipv6_only;
    int one = 1;
    int family;
    int sport;
    int res;
    int fd;

    LOG_D ("%p socks5 session udp bind", self);

    fd = HEV_SOCKS5 (self)->fd;
    saddr = hev_config_get_udp_listen_address ();
    sport = hev_config_get_udp_listen_port ();
    ipv6_only = hev_config_get_listen_ipv6_only ();

#ifdef SO_REUSEPORT
    res = setsockopt (sock, SOL_SOCKET, SO_REUSEPORT, &one, sizeof (one));
    if (res < 0)
        return -1;
#endif

    if (ipv6_only) {
        res = setsockopt (sock, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof (one));
        if (res < 0)
            return -1;
    }

    alen = sizeof (struct sockaddr_in6);
    if (saddr)
        res = hev_netaddr_resolve (&addr, saddr, NULL);
    else
        res = getsockname (fd, (struct sockaddr *)&addr, &alen);
    if (res < 0)
        return -1;

    addr.sin6_port = htons (sport);
    res = bind (sock, (struct sockaddr *)&addr, sizeof (struct sockaddr_in6));
    if (res < 0)
        return -1;

    if (hev_netaddr_is_any (dst)) {
        alen = sizeof (struct sockaddr_in6);
        res = getpeername (fd, (struct sockaddr *)&addr, &alen);
        if (res < 0)
            return -1;

        addr.sin6_port = dst->sin6_port;
        dst = &addr;
    }

    res = connect (sock, (struct sockaddr *)dst, sizeof (struct sockaddr_in6));
    if (res < 0)
        return -1;

    HEV_SOCKS5 (self)->udp_associated = !!dst->sin6_port;

    alen = sizeof (struct sockaddr_in6);
    res = getsockname (sock, (struct sockaddr *)src, &alen);
    if (res < 0)
        return -1;

    if (IN6_IS_ADDR_V4MAPPED (&src->sin6_addr))
        family = AF_INET;
    else
        family = AF_INET6;

    saddr = hev_config_get_udp_public_address (family);
    if (saddr) {
        sport = src->sin6_port;
        res = hev_netaddr_resolve (src, saddr, NULL);
        src->sin6_port = sport;
        if (res < 0)
            return -1;
    }

    return 0;
}

int
hev_socks5_session_construct (HevSocks5Session *self, int fd)
{
    int addr_family;
    int res;

    res = hev_socks5_server_construct (&self->base, fd);
    if (res < 0)
        return -1;

    LOG_D ("%p socks5 session construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_SESSION_TYPE;

    addr_family = hev_config_get_address_family ();
    hev_socks5_set_addr_family (HEV_SOCKS5 (self), addr_family);

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
