/*
 ============================================================================
 Name        : hev-dns-query.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 everyone.
 Description : Simple DNS query generator
 ============================================================================
 */

#ifndef __HEV_DNS_QUERY_H__
#define __HEV_DNS_QUERY_H__

ssize_t hev_dns_query_generate (const char *domain, void *buf, size_t len);
unsigned int hev_dns_query_parse (const void *buf, size_t len);

#endif /* __HEV_DNS_QUERY_H__ */

