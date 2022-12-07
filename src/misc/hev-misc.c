/*
 ============================================================================
 Name        : hev-misc.c
 Authors     : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2022 everyone.
 Description : Misc
 ============================================================================
 */

#include <string.h>
#include <netinet/in.h>

#include <hev-task-dns.h>

#include "hev-misc.h"

int
hev_netaddr_resolve (struct sockaddr_in6 *daddr, const char *addr,
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
        daddr->sin6_family = AF_INET6;
        daddr->sin6_port = adp->sin_port;
        memset (&daddr->sin6_addr, 0, 10);
        daddr->sin6_addr.s6_addr[10] = 0xff;
        daddr->sin6_addr.s6_addr[11] = 0xff;
        memcpy (&daddr->sin6_addr.s6_addr[12], &adp->sin_addr, 4);
    } else if (result->ai_family == AF_INET6) {
        memcpy (daddr, result->ai_addr, sizeof (struct sockaddr_in6));
    }

    freeaddrinfo (result);

    return 0;
}
