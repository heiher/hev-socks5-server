/*
 ============================================================================
 Name        : hev-dns-query.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2013 - 2019 everyone.
 Description : Simple DNS query generator
 ============================================================================
 */

#include <stddef.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hev-dns-query.h"

typedef struct _HevDNSHeader HevDNSHeader;

struct _HevDNSHeader
{
    uint16_t id;
    uint8_t rd : 1;
    uint8_t tc : 1;
    uint8_t aa : 1;
    uint8_t opcode : 4;
    uint8_t qr : 1;
    uint8_t rcode : 4;
    uint8_t z : 3;
    uint8_t ra : 1;
    uint16_t qdcount;
    uint16_t ancount;
    uint16_t nscount;
    uint16_t arcount;
} __attribute__ ((packed));

ssize_t
hev_dns_query_generate (const char *domain, void *buf, size_t len)
{
    HevDNSHeader *header = (HevDNSHeader *)buf;
    unsigned char c = 0, *buffer = buf;
    size_t size = __builtin_strlen (domain);
    const size_t hlen = sizeof (HevDNSHeader);
    ssize_t i;

    /* checking domain length */
    if ((len - hlen - 1 - 1 - 2 - 2) < size)
        return -1;

    /* copy domain to queries aera */
    for (i = size - 1; i >= 0; i--) {
        unsigned char b = 0;
        if ('.' == domain[i]) {
            b = c;
            c = 0;
        } else {
            b = domain[i];
            c++;
        }
        buffer[hlen + 1 + i] = b;
    }
    buffer[hlen] = c;
    buffer[hlen + 1 + size] = 0;
    /* type */
    buffer[hlen + 1 + size + 1] = 0;
    buffer[hlen + 1 + size + 2] = 1;
    /* class */
    buffer[hlen + 1 + size + 3] = 0;
    buffer[hlen + 1 + size + 4] = 1;
    /* dns resolve header */
    __builtin_bzero (header, hlen);
    header->id = htons (0x1234);
    header->rd = 1;
    header->qdcount = htons (1);
    /* size: header + 1st label len + domain len + NUL + type + class */
    size += hlen + 1 + 1 + 2 + 2;

    return size;
}

unsigned int
hev_dns_query_parse (const void *buf, size_t len)
{
    HevDNSHeader *header = (HevDNSHeader *)buf;
    const unsigned char *buffer = buf;
    size_t i = 0, offset = sizeof (HevDNSHeader);

    if ((sizeof (HevDNSHeader) > len) || (0 == header->ancount))
        return 0;

    header->qdcount = ntohs (header->qdcount);
    header->ancount = ntohs (header->ancount);

    /* skip queries */
    for (i = 0; i < header->qdcount; i++) {
        for (; offset < len;) {
            if (buffer[offset] == 0) {
                offset += 1;
                break;
            } else if (buffer[offset] & 0xc0) {
                offset += 2;
                break;
            } else {
                offset += (buffer[offset] + 1);
            }
        }
        offset += 4;
    }

    /* get address of first 'a' answer */
    for (i = 0; i < header->ancount; i++) {
        int is_a = 0;
        size_t rdlen;

        for (; offset < len;) {
            if (buffer[offset] == 0) {
                offset += 1;
                break;
            } else if (buffer[offset] & 0xc0) {
                offset += 2;
                break;
            } else {
                offset += (buffer[offset] + 1);
            }
        }
        if ((offset + 9) >= len)
            return 0;
        if ((0x00 == buffer[offset]) && (0x01 == buffer[offset + 1]))
            is_a = 1;
        rdlen = buffer[offset + 9] + (buffer[offset + 8] << 8);
        offset += 2 + 2 + 4 + 2 + rdlen;
        if (offset > len)
            return 0;

        if (is_a)
            return *(unsigned int *)(buffer + offset - rdlen);
    }

    return 0;
}
