/*
 ============================================================================
 Name        : hev-socket-factory.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2022 hev
 Description : Socket Factory
 ============================================================================
 */

#include <string.h>
#include <unistd.h>
#include <netinet/in.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-socket.h>
#include <hev-memory-allocator.h>
#include <hev-task-dns.h>

#include "hev-logger.h"

#include "hev-socket-factory.h"

struct _HevSocketFactory
{
    struct sockaddr_in6 addr;
    int ipv6_only;
    int fd;
};

static int
hev_socket_factory_resolve (HevSocketFactory *self, const char *addr,
                            const char *port)
{
    struct addrinfo hints = { 0 };
    struct addrinfo *result;
    int res;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    res = hev_task_dns_getaddrinfo (addr, port, &hints, &result);
    if (res < 0)
        return -1;

    if (result->ai_family == AF_INET) {
        struct sockaddr_in *adp;

        adp = (struct sockaddr_in *)result->ai_addr;
        self->addr.sin6_family = AF_INET6;
        self->addr.sin6_port = adp->sin_port;
        memset (&self->addr.sin6_addr, 0, 10);
        self->addr.sin6_addr.s6_addr[10] = 0xff;
        self->addr.sin6_addr.s6_addr[11] = 0xff;
        memcpy (&self->addr.sin6_addr.s6_addr[12], &adp->sin_addr, 4);
    } else if (result->ai_family == AF_INET6) {
        memcpy (&self->addr, result->ai_addr, sizeof (self->addr));
    }

    freeaddrinfo (result);

    return 0;
}

HevSocketFactory *
hev_socket_factory_new (const char *addr, const char *port, int ipv6_only)
{
    HevSocketFactory *self;
    int res;

    LOG_D ("socket factory new");

    self = hev_malloc0 (sizeof (HevSocketFactory));
    if (!self) {
        LOG_E ("socket factory alloc");
        return NULL;
    }

    res = hev_socket_factory_resolve (self, addr, port);
    if (res < 0) {
        LOG_E ("socket factory resolve");
        hev_free (self);
        return NULL;
    }

    self->ipv6_only = ipv6_only;
    self->fd = -1;

    return self;
}

void
hev_socket_factory_destroy (HevSocketFactory *self)
{
    LOG_D ("socket factory destroy");

    if (self->fd >= 0)
        close (self->fd);
    hev_free (self);
}

int
hev_socket_factory_get (HevSocketFactory *self)
{
    int one = 1;
    int res;
    int fd;

    LOG_D ("socket factory get");

    if (self->fd >= 0)
        return dup (self->fd);

    fd = hev_task_io_socket_socket (AF_INET6, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG_E ("socket factory socket");
        goto exit;
    }

    res = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof (one));
    if (fd < 0) {
        LOG_E ("socket factory reuse");
        goto exit_close;
    }

    res = -1;
#ifdef SO_REUSEPORT
    res = setsockopt (fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof (one));
#endif
    if (res < 0 && self->fd < 0)
        self->fd = dup (fd);

    if (self->ipv6_only) {
        res = setsockopt (fd, IPPROTO_IPV6, IPV6_V6ONLY, &one, sizeof (one));
        if (res < 0) {
            LOG_E ("socket factory ipv6 only");
            goto exit_close;
        }
    }

    res = bind (fd, (struct sockaddr *)&self->addr, sizeof (self->addr));
    if (res < 0) {
        LOG_E ("socket factory bind");
        goto exit_close;
    }

    res = listen (fd, 100);
    if (res < 0) {
        LOG_E ("socket factory listen");
        goto exit_close;
    }

    return fd;

exit_close:
    close (fd);
exit:
    return -1;
}
