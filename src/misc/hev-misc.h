/*
 ============================================================================
 Name        : hev-misc.h
 Authors     : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2022 - 2024 everyone.
 Description : Misc
 ============================================================================
 */

#ifndef __HEV_MISC_H__
#define __HEV_MISC_H__

#include <netinet/in.h>

int hev_netaddr_resolve (struct sockaddr_in6 *daddr, const char *addr,
                         const char *port);
int hev_netaddr_is_any (struct sockaddr_in6 *addr);

void run_as_daemon (const char *pid_file);
int set_limit_nofile (int limit_nofile);

int set_sock_bind (int fd, const char *iface);
int set_sock_mark (int fd, unsigned int mark);

#endif /* __HEV_MISC_H__ */
