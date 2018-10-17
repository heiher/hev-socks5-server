/*
 ============================================================================
 Name        : hev-dns-query.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2013 everyone.
 Description : Simple DNS query generator
 ============================================================================
 */

#include <string.h>
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
    ssize_t i;
    unsigned char c = 0, *buffer = buf;
    ssize_t size = strlen (domain);

    /* checking domain length */
    if ((len - sizeof (HevDNSHeader) - 2 - 4) < size)
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
        buffer[sizeof (HevDNSHeader) + 1 + i] = b;
    }
    buffer[sizeof (HevDNSHeader)] = c;
    buffer[sizeof (HevDNSHeader) + 1 + size] = 0;
    /* type */
    buffer[sizeof (HevDNSHeader) + 1 + size + 1] = 0;
    buffer[sizeof (HevDNSHeader) + 1 + size + 2] = 1;
    /* class */
    buffer[sizeof (HevDNSHeader) + 1 + size + 3] = 0;
    buffer[sizeof (HevDNSHeader) + 1 + size + 4] = 1;
    /* dns resolve header */
    memset (header, 0, sizeof (HevDNSHeader));
    header->id = htons (0x1234);
    header->rd = 1;
    header->qdcount = htons (1);
    /* size */
    size += sizeof (HevDNSHeader) + 6;

    return size;
}

unsigned int
hev_dns_query_parse (const void *buf, size_t len)
{
    HevDNSHeader *header = (HevDNSHeader *)buf;
    size_t i = 0, offset = sizeof (HevDNSHeader);
    unsigned int *resp = NULL;
    const unsigned char *buffer = buf;

    if (sizeof (HevDNSHeader) > len)
        return 0;

    if (header->ancount == 0)
        return 0;

    header->qdcount = ntohs (header->qdcount);
    header->ancount = ntohs (header->ancount);
    /* skip queries */
    for (i = 0; i < header->qdcount; i++, offset += 4) {
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
    }
    /* goto first a type answer resource area */
    for (i = 0; i < header->ancount; i++) {
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
        offset += 8;
        /* checking the answer is valid */
        if ((offset - 7) >= len)
            return 0;
        /* is a type */
        if ((buffer[offset - 8] == 0x00) && (buffer[offset - 7] == 0x01))
            break;
        offset += 2 + (buffer[offset + 1] + (buffer[offset] << 8));
    }
    /* checking resource length */
    if (((offset + 5) >= len) || (buffer[offset] != 0x00) ||
        (buffer[offset + 1] != 0x04))
        return 0;
    resp = (unsigned int *)&buffer[offset + 2];

    return *resp;
}
