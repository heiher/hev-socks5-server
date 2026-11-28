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

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-memory-allocator.h>

#include "hev-misc.h"
#include "hev-compat.h"
#include "hev-rbtree.h"
#include "hev-compiler.h"
#include "hev-logger.h"
#include "hev-config.h"
#include "hev-socks5-user-mark.h"

#include "hev-socks5-session.h"

typedef struct _UdpPortNode UdpPortNode;

struct _UdpPortNode
{
    HevRBTreeNode node;
    struct in6_addr addr;
    int port;
};

static HevRBTree udp_port_tree;
static HevMutex udp_port_mutex = HEV_MUTEX_INITIALIZER;

static int
udp_port_insert (UdpPortNode *self)
{
    HevRBTreeNode **new = &udp_port_tree.root, *parent = NULL;

    while (*new) {
        UdpPortNode *this = container_of (*new, UdpPortNode, node);
        int res;

        res = memcmp (&self->addr, &this->addr, sizeof (self->addr));
        if (!res)
            res = self->port - this->port;

        parent = *new;
        if (res < 0)
            new = &((*new)->left);
        else if (res > 0)
            new = &((*new)->right);
        else
            return -1;
    }

    hev_rbtree_node_link (&self->node, parent, new);
    hev_rbtree_insert_color (&udp_port_tree, &self->node);

    return 0;
}

static int
udp_port_alloc (HevSocks5Session *self, const struct sockaddr_in6 *dst)
{
    UdpPortNode *node;
    int beg, num, off;
    int res = -1;
    int i;

    off = hev_config_get_udp_listen_port_range (&beg, &num);
    if (!beg)
        return 0;

    if (dst->sin6_port)
        return beg + off;

    node = hev_malloc0 (sizeof (UdpPortNode));
    if (!node)
        return -1;

    node->addr = dst->sin6_addr;

    hev_mutex_lock (&udp_port_mutex);
    for (i = 0; (i < num) && (res < 0); i++) {
        node->port = beg + (off + i) % num;
        res = udp_port_insert (node);
    }
    hev_mutex_unlock (&udp_port_mutex);

    if (res < 0) {
        hev_free (node);
        return -1;
    }

    self->udp_port = node;

    return node->port;
}

static void
udp_port_free (UdpPortNode *node)
{
    if (!node)
        return;

    hev_mutex_lock (&udp_port_mutex);
    hev_rbtree_erase (&udp_port_tree, &node->node);
    hev_mutex_unlock (&udp_port_mutex);

    hev_free (node);
}

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

int
hev_socks5_session_run (HevSocks5Session *self)
{
    LOG_D ("%p socks5 session run", self);

    if (!hev_socks5_get_timeout (HEV_SOCKS5 (self)))
        return -1;

    return hev_socks5_server_run (HEV_SOCKS5_SERVER (self));
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

        memset (&addr, 0, sizeof (addr));
        res = hev_netaddr_resolve (&addr, saddr, NULL);
        if (res < 0)
            return -1;

        res = hev_task_io_socket_bind (fd, (struct sockaddr *)&addr,
                                       sizeof (addr));
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
                             const struct sockaddr_in6 *client,
                             struct sockaddr_in6 *local)
{
    HevSocks5ServerClass *sskptr;
    const char *saddr;
    int ipv6_only;
    int one = 1;
    int family;
    int sport;
    int res;

    LOG_D ("%p socks5 session udp bind", self);

    saddr = hev_config_get_udp_listen_address ();
    ipv6_only = hev_config_get_listen_ipv6_only ();

#ifdef SO_REUSEPORT
    res = hev_task_io_socket_setsockopt (sock, SOL_SOCKET, SO_REUSEPORT, &one,
                                         sizeof (one));
    if (res < 0)
        return -1;
#endif

    if (ipv6_only) {
        res = hev_task_io_socket_setsockopt (sock, IPPROTO_IPV6, IPV6_V6ONLY,
                                             &one, sizeof (one));
        if (res < 0)
            return -1;
    }

    sport = udp_port_alloc (HEV_SOCKS5_SESSION (self), client);
    if (sport < 0) {
        LOG_W ("%p socks5 session no free udp port for client", self);
        return -1;
    }

    if (saddr) {
        memset (local, 0, sizeof (struct sockaddr_in6));
        res = hev_netaddr_resolve (local, saddr, NULL);
        if (res < 0)
            return -1;
    }

    local->sin6_port = htons (sport);

    sskptr = HEV_SOCKS5_SERVER_CLASS (HEV_SOCKS5_SERVER_TYPE);
    res = sskptr->binder (self, sock, client, local);
    if (res < 0)
        return -1;

    if (IN6_IS_ADDR_V4MAPPED (&local->sin6_addr))
        family = AF_INET;
    else
        family = AF_INET6;

    saddr = hev_config_get_udp_public_address (family);
    if (saddr) {
        sport = local->sin6_port;
        res = hev_netaddr_resolve (local, saddr, NULL);
        local->sin6_port = sport;
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
    UdpPortNode *udp_port = self->udp_port;

    LOG_D ("%p socks5 session destruct", self);

    HEV_SOCKS5_SERVER_TYPE->destruct (base);
    udp_port_free (udp_port);
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
