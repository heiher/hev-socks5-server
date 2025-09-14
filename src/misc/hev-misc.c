/*
 ============================================================================
 Name        : hev-misc.c
 Authors     : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2022 - 2024 everyone.
 Description : Misc
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <net/if.h>
#include <sys/resource.h>

#if defined(__APPLE__)
#include <Availability.h>
#include <AvailabilityMacros.h>
#include <TargetConditionals.h>
#endif

#include <hev-task-dns.h>

#include "hev-logger.h"

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

int
hev_netaddr_is_any (struct sockaddr_in6 *addr)
{
    if (!IN6_IS_ADDR_V4MAPPED (&addr->sin6_addr))
        return !memcmp (&addr->sin6_addr, &in6addr_any, 16);

    return !memcmp (&addr->sin6_addr.s6_addr[12], &in6addr_any, 4);
}

void
run_as_daemon (const char *pid_file)
{
    FILE *fp;

    fp = fopen (pid_file, "w+");
    if (!fp) {
        LOG_E ("open pid file %s", pid_file);
        return;
    }

#ifndef TARGET_OS_TV
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    if (daemon (0, 0)) {
        /* ignore return value */
    }
#pragma GCC diagnostic pop
#endif

    fprintf (fp, "%u\n", getpid ());
    fclose (fp);
}

int
set_limit_nofile (int limit_nofile)
{
    struct rlimit limit = {
        .rlim_cur = limit_nofile,
        .rlim_max = limit_nofile,
    };

    return setrlimit (RLIMIT_NOFILE, &limit);
}

int
set_sock_bind (int fd, const char *iface)
{
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

    return res;
}

int
set_sock_mark (int fd, unsigned int mark)
{
#if defined(__linux__)
    return setsockopt (fd, SOL_SOCKET, SO_MARK, &mark, sizeof (mark));
#elif defined(__FreeBSD__)
    return setsockopt (fd, SOL_SOCKET, SO_USER_COOKIE, &mark, sizeof (mark));
#endif
    return 0;
}
