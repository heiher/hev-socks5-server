/*
 ============================================================================
 Name        : hev-socks5-session.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 everyone.
 Description : Socks5 session
 ============================================================================
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hev-socks5-session.h"
#include "hev-memory-allocator.h"
#include "hev-task.h"
#include "hev-task-io.h"
#include "hev-task-io-socket.h"
#include "hev-config.h"
#include "hev-dns-query.h"

#define SESSION_HP (10)
#define TASK_STACK_SIZE (3 * 4096)

static void hev_socks5_session_task_entry (void *data);

typedef struct _Socks5AuthHeader Socks5AuthHeader;
typedef struct _Socks5ReqResHeader Socks5ReqResHeader;

enum
{
    STEP_NULL,
    STEP_READ_AUTH_METHOD,
    STEP_WRITE_AUTH_METHOD,
    STEP_WRITE_AUTH_METHOD_ERROR,
    STEP_READ_REQUEST,
    STEP_READ_AUTH_REQUEST,
    STEP_WRITE_AUTH_RESPONSE,
    STEP_WRITE_AUTH_RESPONSE_ERROR,
    STEP_DO_CONNECT,
    STEP_DO_SPLICE,
    STEP_DO_DNS_FWD,
    STEP_DO_FWD_DNS,
    STEP_WRITE_RESPONSE,
    STEP_WRITE_RESPONSE_ERROR_CMD,
    STEP_WRITE_RESPONSE_ERROR_SOCK,
    STEP_WRITE_RESPONSE_ERROR_HOST,
    STEP_WRITE_RESPONSE_ERROR_ATYPE,
    STEP_CLOSE_SESSION,
};

struct _HevSocks5Session
{
    HevSocks5SessionBase base;

    int mode;

    int client_fd;
    int remote_fd;
    int ref_count;

    struct sockaddr_in address;

    HevSocks5SessionCloseNotify notify;
    void *notify_data;
};

struct _Socks5AuthHeader
{
    unsigned char ver;
    union
    {
        unsigned char method;
        unsigned char method_len;
    };
    unsigned char methods[256];
} __attribute__ ((packed));

struct _Socks5ReqResHeader
{
    unsigned char ver;
    union
    {
        unsigned char cmd;
        unsigned char rep;
    };
    unsigned char rsv;
    unsigned char atype;
    union
    {
        struct
        {
            unsigned int addr;
            unsigned short port;
        } ipv4;
        struct
        {
            unsigned char len;
            unsigned char addr[256 + 2];
        } domain;
    };
} __attribute__ ((packed));

HevSocks5Session *
hev_socks5_session_new (int client_fd, HevSocks5SessionCloseNotify notify,
                        void *notify_data)
{
    HevSocks5Session *self;
    HevTask *task;

    self = hev_malloc0 (sizeof (HevSocks5Session));
    if (!self)
        return NULL;

    self->base.hp = SESSION_HP;

    self->ref_count = 1;
    self->remote_fd = -1;
    self->client_fd = client_fd;
    self->notify = notify;
    self->notify_data = notify_data;

    task = hev_task_new (TASK_STACK_SIZE);
    if (!task) {
        hev_free (self);
        return NULL;
    }

    self->base.task = task;
    hev_task_set_priority (task, 9);

    return self;
}

HevSocks5Session *
hev_socks5_session_ref (HevSocks5Session *self)
{
    self->ref_count++;

    return self;
}

void
hev_socks5_session_unref (HevSocks5Session *self)
{
    self->ref_count--;
    if (self->ref_count)
        return;

    hev_free (self);
}

void
hev_socks5_session_run (HevSocks5Session *self)
{
    hev_task_run (self->base.task, hev_socks5_session_task_entry, self);
}

static int
socks5_session_task_io_yielder (HevTaskYieldType type, void *data)
{
    HevSocks5Session *self = data;

    self->base.hp = SESSION_HP;

    hev_task_yield (type);

    return (self->base.hp > 0) ? 0 : -1;
}

static int
socks5_read_auth_method (HevSocks5Session *self)
{
    Socks5AuthHeader socks5_auth;
    int i, auth_method = hev_config_get_auth_method ();
    ssize_t len;

    /* read socks5 auth method header */
    len = hev_task_io_socket_recv (self->client_fd, &socks5_auth, 2,
                                   MSG_WAITALL, socks5_session_task_io_yielder,
                                   self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;
    /* check socks5 version */
    if (socks5_auth.ver != 0x05)
        return STEP_CLOSE_SESSION;

    /* read socks5 auth methods */
    len = hev_task_io_socket_recv (self->client_fd, &socks5_auth.methods,
                                   socks5_auth.method_len, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;

    /* select socks5 auth method */
    for (i = 0; i < socks5_auth.method_len; i++) {
        if (socks5_auth.methods[i] == auth_method)
            return STEP_WRITE_AUTH_METHOD;
    }

    return STEP_WRITE_AUTH_METHOD_ERROR;
}

static int
socks5_write_auth_method (HevSocks5Session *self, int step)
{
    Socks5AuthHeader socks5_auth;
    int auth_method = hev_config_get_auth_method ();
    ssize_t len;

    /* write socks5 auth method */
    socks5_auth.ver = 0x05;
    switch (step) {
    case STEP_WRITE_AUTH_METHOD:
        socks5_auth.method = auth_method;
        break;
    case STEP_WRITE_AUTH_METHOD_ERROR:
        socks5_auth.method = 0xff;
        break;
    }
    len = hev_task_io_socket_send (self->client_fd, &socks5_auth, 2,
                                   MSG_WAITALL, socks5_session_task_io_yielder,
                                   self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;

    if (auth_method == HEV_CONFIG_AUTH_METHOD_USERPASS)
        return STEP_READ_AUTH_REQUEST;

    return STEP_READ_REQUEST;
}

static int
socks5_read_auth_request (HevSocks5Session *self)
{
    unsigned char ver;
    unsigned char ulen;
    unsigned char plen;
    char buf[256];
    const char *username;
    const char *password;
    ssize_t len;

    /* read socks5 auth request header */
    len = hev_task_io_socket_recv (self->client_fd, &ver, 1, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0 || ver != 0x01)
        return STEP_CLOSE_SESSION;

    /* read socks5 auth request ulen */
    len = hev_task_io_socket_recv (self->client_fd, &ulen, 1, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0 || ulen == 0)
        return STEP_CLOSE_SESSION;

    /* read socks5 auth request username */
    len = hev_task_io_socket_recv (self->client_fd, buf, ulen, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len != ulen)
        return STEP_CLOSE_SESSION;
    buf[ulen] = '\0';
    username = hev_config_get_auth_username ();
    if (strcmp (username, buf) != 0)
        return STEP_WRITE_AUTH_RESPONSE_ERROR;

    /* read socks5 auth request plen */
    len = hev_task_io_socket_recv (self->client_fd, &plen, 1, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0 || plen == 0)
        return STEP_CLOSE_SESSION;

    /* read socks5 auth request password */
    len = hev_task_io_socket_recv (self->client_fd, buf, plen, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len != plen)
        return STEP_CLOSE_SESSION;
    buf[plen] = '\0';
    password = hev_config_get_auth_password ();
    if (strcmp (password, buf) != 0)
        return STEP_WRITE_AUTH_RESPONSE_ERROR;

    return STEP_WRITE_AUTH_RESPONSE;
}

static int
socks5_write_auth_response (HevSocks5Session *self, int step)
{
    unsigned char res[2];
    ssize_t len;

    res[0] = 0x01;
    switch (step) {
    case STEP_WRITE_AUTH_RESPONSE:
        res[1] = 0x00;
        step = STEP_READ_REQUEST;
        break;
    case STEP_WRITE_AUTH_RESPONSE_ERROR:
        res[1] = 0xff;
        step = STEP_CLOSE_SESSION;
        break;
    }

    len = hev_task_io_socket_send (self->client_fd, res, 2, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;

    return step;
}

static int
socks5_read_request (HevSocks5Session *self)
{
    Socks5ReqResHeader socks5_r;
    ssize_t len;

    /* read socks5 request header */
    len = hev_task_io_socket_recv (self->client_fd, &socks5_r, 3, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;
    /* check socks5 version */
    if (socks5_r.ver != 0x05)
        return STEP_CLOSE_SESSION;

    switch (socks5_r.cmd) {
    case 0x01: /* connect */
        return STEP_DO_CONNECT;
    case 0x04: /* dns forward */
        return STEP_DO_DNS_FWD;
    }

    return STEP_WRITE_RESPONSE_ERROR_CMD;
}

static int
socks5_parse_addr_ipv4 (HevSocks5Session *self, Socks5ReqResHeader *socks5_r)
{
    ssize_t len;

    /* read socks5 request ipv4 addr and port */
    len = hev_task_io_socket_recv (self->client_fd, &socks5_r->ipv4, 6,
                                   MSG_WAITALL, socks5_session_task_io_yielder,
                                   self);
    if (len <= 0)
        return -1;

    self->address.sin_family = AF_INET;
    self->address.sin_addr.s_addr = socks5_r->ipv4.addr;
    self->address.sin_port = socks5_r->ipv4.port;

    return 0;
}

static int
socks5_parse_addr_domain (HevSocks5Session *self, Socks5ReqResHeader *socks5_r)
{
    HevTask *task;
    int ret = -1, dns_fd;
    unsigned char buf[2048];
    ssize_t len;
    struct sockaddr_in addr_dns;
    socklen_t addr_len_dns = sizeof (struct sockaddr_in);

    /* read socks5 request domain addr length */
    len = hev_task_io_socket_recv (self->client_fd, &socks5_r->domain.len, 1,
                                   MSG_WAITALL, socks5_session_task_io_yielder,
                                   self);
    if (len <= 0)
        return -1;

    /* read socks5 request domain addr and port*/
    len = hev_task_io_socket_recv (self->client_fd, &socks5_r->domain.addr,
                                   socks5_r->domain.len + 2, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0)
        return -1;

    /* copy port */
    self->address.sin_family = AF_INET;
    memcpy (&self->address.sin_port,
            socks5_r->domain.addr + socks5_r->domain.len, 2);

    /* check is ipv4 addr string */
    socks5_r->domain.addr[socks5_r->domain.len] = '\0';
    self->address.sin_addr.s_addr =
        inet_addr ((const char *)socks5_r->domain.addr);
    if (self->address.sin_addr.s_addr != INADDR_NONE)
        return 0;

    /* do dns resolve */
    len =
        hev_dns_query_generate ((const char *)socks5_r->domain.addr, buf, 2048);
    if (len <= 0)
        return -1;

    dns_fd = hev_task_io_socket_socket (AF_INET, SOCK_DGRAM, 0);
    if (dns_fd == -1)
        return -1;

    task = hev_task_self ();
    hev_task_add_fd (task, dns_fd, POLLIN | POLLOUT);

    addr_dns.sin_family = AF_INET;
    addr_dns.sin_addr.s_addr = inet_addr (hev_config_get_dns_address ());
    addr_dns.sin_port = htons (53);

    /* send dns query message */
    len = hev_task_io_socket_sendto (dns_fd, buf, len, 0,
                                     (struct sockaddr *)&addr_dns, addr_len_dns,
                                     socks5_session_task_io_yielder, self);
    if (len <= 0)
        goto quit;

    /* recv dns response message */
    len = hev_task_io_socket_recvfrom (dns_fd, buf, 2048, 0,
                                       (struct sockaddr *)&addr_dns,
                                       &addr_len_dns,
                                       socks5_session_task_io_yielder, self);
    if (len <= 0)
        goto quit;

    self->address.sin_addr.s_addr = hev_dns_query_parse (buf, len);
    if (self->address.sin_addr.s_addr == INADDR_NONE)
        goto quit;

    ret = 0;

quit:
    close (dns_fd);
    return ret;
}

static int
socks5_do_connect (HevSocks5Session *self)
{
    HevTask *task;
    Socks5ReqResHeader socks5_r;
    struct sockaddr *addr = (struct sockaddr *)&self->address;
    socklen_t addr_len = sizeof (struct sockaddr_in);
    ssize_t len;
    int ret;

    /* read socks5 request atype */
    len = hev_task_io_socket_recv (self->client_fd, &socks5_r.atype, 1,
                                   MSG_WAITALL, socks5_session_task_io_yielder,
                                   self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;

    switch (socks5_r.atype) {
    case 0x01: /* ipv4 */
        ret = socks5_parse_addr_ipv4 (self, &socks5_r);
        if (ret == -1)
            return STEP_WRITE_RESPONSE_ERROR_ATYPE;
        break;
    case 0x03: /* domain */
        ret = socks5_parse_addr_domain (self, &socks5_r);
        if (ret == -1)
            return STEP_WRITE_RESPONSE_ERROR_ATYPE;
        break;
    default: /* not supported */
        return STEP_WRITE_RESPONSE_ERROR_ATYPE;
    }

    self->remote_fd = hev_task_io_socket_socket (AF_INET, SOCK_STREAM, 0);
    if (self->remote_fd == -1)
        return STEP_WRITE_RESPONSE_ERROR_SOCK;

    task = hev_task_self ();
    hev_task_add_fd (task, self->remote_fd, POLLIN | POLLOUT);

    /* connect */
    ret = hev_task_io_socket_connect (self->remote_fd, addr, addr_len,
                                      socks5_session_task_io_yielder, self);
    if (ret < 0)
        return STEP_WRITE_RESPONSE_ERROR_HOST;

    return STEP_WRITE_RESPONSE;
}

static int
socks5_do_splice (HevSocks5Session *self)
{
    hev_task_io_splice (self->client_fd, self->client_fd, self->remote_fd,
                        self->remote_fd, 2048, socks5_session_task_io_yielder,
                        self);

    return STEP_CLOSE_SESSION;
}

static int
socks5_do_dns_fwd (HevSocks5Session *self)
{
    Socks5ReqResHeader socks5_r;
    ssize_t len;

    /* set to dns fwd mode */
    self->mode = 1;

    /* read socks5 request body */
    len = hev_task_io_socket_recv (self->client_fd, &socks5_r.atype, 7,
                                   MSG_WAITALL, socks5_session_task_io_yielder,
                                   self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;

    /* set default dns address */
    self->address.sin_family = AF_INET;
    self->address.sin_addr.s_addr = inet_addr (hev_config_get_dns_address ());
    self->address.sin_port = htons (53);

    return STEP_WRITE_RESPONSE;
}

static int
socks5_do_fwd_dns (HevSocks5Session *self)
{
    HevTask *task;
    int dns_fd;
    unsigned char buf[2048];
    ssize_t len;
    struct msghdr mh;
    struct iovec iov[2];
    socklen_t addr_len = sizeof (struct sockaddr_in);
    unsigned short dns_len;

    dns_fd = hev_task_io_socket_socket (AF_INET, SOCK_DGRAM, 0);
    if (dns_fd == -1)
        return STEP_CLOSE_SESSION;

    task = hev_task_self ();
    hev_task_add_fd (task, dns_fd, POLLIN | POLLOUT);

    /* read dns request length */
    len = hev_task_io_socket_recv (self->client_fd, &dns_len, 2, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;
    dns_len = ntohs (dns_len);

    /* check dns request length */
    if (dns_len >= 2048)
        return STEP_CLOSE_SESSION;

    /* read dns request */
    len = hev_task_io_socket_recv (self->client_fd, buf, dns_len, MSG_WAITALL,
                                   socks5_session_task_io_yielder, self);
    if (len <= 0)
        return STEP_CLOSE_SESSION;

    /* send dns request */
    len = hev_task_io_socket_sendto (dns_fd, buf, len, 0,
                                     (struct sockaddr *)&self->address,
                                     addr_len, socks5_session_task_io_yielder,
                                     self);
    if (len <= 0)
        goto quit;

    /* recv dns response */
    len = hev_task_io_socket_recvfrom (dns_fd, buf, 2048, 0,
                                       (struct sockaddr *)&self->address,
                                       &addr_len,
                                       socks5_session_task_io_yielder, self);
    if (len <= 0)
        goto quit;

    memset (&mh, 0, sizeof (mh));
    mh.msg_iov = iov;
    mh.msg_iovlen = 2;

    /* write dns response length */
    dns_len = htons (len);
    iov[0].iov_base = &dns_len;
    iov[0].iov_len = 2;

    /* send dns response */
    iov[1].iov_base = buf;
    iov[1].iov_len = len;

    hev_task_io_socket_sendmsg (self->client_fd, &mh, MSG_WAITALL,
                                socks5_session_task_io_yielder, self);

quit:
    close (dns_fd);
    return STEP_CLOSE_SESSION;
}

static int
socks5_write_response (HevSocks5Session *self, int step)
{
    Socks5ReqResHeader socks5_r;
    int next_step = STEP_CLOSE_SESSION;

    /* write socks5 reply */
    socks5_r.ver = 0x05;
    switch (step) {
    case STEP_WRITE_RESPONSE:
        socks5_r.rep = 0x00;
        next_step = self->mode ? STEP_DO_FWD_DNS : STEP_DO_SPLICE;
        break;
    case STEP_WRITE_RESPONSE_ERROR_CMD:
        socks5_r.rep = 0x07;
        break;
    case STEP_WRITE_RESPONSE_ERROR_SOCK:
        socks5_r.rep = 0x01;
        break;
    case STEP_WRITE_RESPONSE_ERROR_HOST:
        socks5_r.rep = 0x04;
        break;
    case STEP_WRITE_RESPONSE_ERROR_ATYPE:
        socks5_r.rep = 0x08;
        break;
    }
    socks5_r.rsv = 0x00;
    socks5_r.atype = 0x01;
    socks5_r.ipv4.addr = self->address.sin_addr.s_addr;
    socks5_r.ipv4.port = self->address.sin_port;
    hev_task_io_socket_send (self->client_fd, &socks5_r, 10, MSG_WAITALL,
                             socks5_session_task_io_yielder, self);

    return next_step;
}

static int
socks5_close_session (HevSocks5Session *self)
{
    if (self->remote_fd >= 0)
        close (self->remote_fd);
    close (self->client_fd);

    self->notify (self, self->notify_data);
    hev_socks5_session_unref (self);

    return STEP_NULL;
}

static void
hev_socks5_session_task_entry (void *data)
{
    HevTask *task = hev_task_self ();
    HevSocks5Session *self = data;
    int step = STEP_READ_AUTH_METHOD;

    hev_task_add_fd (task, self->client_fd, POLLIN | POLLOUT);

    while (step) {
        switch (step) {
        case STEP_READ_AUTH_METHOD:
            step = socks5_read_auth_method (self);
            break;
        case STEP_WRITE_AUTH_METHOD:
        case STEP_WRITE_AUTH_METHOD_ERROR:
            step = socks5_write_auth_method (self, step);
            break;
        case STEP_READ_AUTH_REQUEST:
            step = socks5_read_auth_request (self);
            break;
        case STEP_WRITE_AUTH_RESPONSE:
        case STEP_WRITE_AUTH_RESPONSE_ERROR:
            step = socks5_write_auth_response (self, step);
            break;
        case STEP_READ_REQUEST:
            step = socks5_read_request (self);
            break;
        case STEP_DO_CONNECT:
            step = socks5_do_connect (self);
            break;
        case STEP_DO_SPLICE:
            step = socks5_do_splice (self);
            break;
        case STEP_DO_DNS_FWD:
            step = socks5_do_dns_fwd (self);
            break;
        case STEP_DO_FWD_DNS:
            step = socks5_do_fwd_dns (self);
            break;
        case STEP_WRITE_RESPONSE:
        case STEP_WRITE_RESPONSE_ERROR_CMD:
        case STEP_WRITE_RESPONSE_ERROR_SOCK:
        case STEP_WRITE_RESPONSE_ERROR_HOST:
        case STEP_WRITE_RESPONSE_ERROR_ATYPE:
            step = socks5_write_response (self, step);
            break;
        case STEP_CLOSE_SESSION:
            step = socks5_close_session (self);
            break;
        default:
            step = STEP_NULL;
            break;
        }
    }
}
