/*
 ============================================================================
 Name        : hev-misc.h
 Authors     : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2022 everyone.
 Description : Misc
 ============================================================================
 */

#ifndef __HEV_MISC_H__
#define __HEV_MISC_H__

int hev_netaddr_resolve (struct sockaddr_in6 *daddr, const char *addr,
                         const char *port);

#endif /* __HEV_MISC_H__ */
