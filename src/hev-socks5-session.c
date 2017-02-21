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
#include "hev-config.h"
#include "hev-dns-query.h"

#define SESSION_HP	(10)
#define TASK_STACK_SIZE	(8192)

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
	union {
		unsigned char method;
		unsigned char method_len;
	};
	unsigned char methods[256];
} __attribute__((packed));

struct _Socks5ReqResHeader
{
	unsigned char ver;
	union {
		unsigned char cmd;
		unsigned char rep;
	};
	unsigned char rsv;
	unsigned char atype;
	union {
		struct {
			unsigned int addr;
			unsigned short port;
		} ipv4;
		struct {
			unsigned char len;
			unsigned char addr[256 + 2];
		} domain;
	};
} __attribute__((packed));

HevSocks5Session *
hev_socks5_session_new (int client_fd,
			HevSocks5SessionCloseNotify notify, void *notify_data)
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
	hev_task_set_priority (task, 1);

	return self;
}

HevSocks5Session *
hev_socks5_session_ref (HevSocks5Session *self)
{
	self->ref_count ++;

	return self;
}

void
hev_socks5_session_unref (HevSocks5Session *self)
{
	self->ref_count --;
	if (self->ref_count)
		return;

	hev_free (self);
}

void
hev_socks5_session_run (HevSocks5Session *self)
{
	hev_task_run (self->base.task, hev_socks5_session_task_entry, self);
}

static ssize_t
task_socket_recv (int fd, void *buf, size_t len, int flags, HevSocks5Session *session)
{
	ssize_t s;
	size_t size = 0;

retry:
	s = recv (fd, buf + size, len - size, flags & ~MSG_WAITALL);
	if (s == -1 && errno == EAGAIN) {
		hev_task_yield (HEV_TASK_WAITIO);
		if (session->base.hp <= 0)
			return -2;
		goto retry;
	}

	if (!(flags & MSG_WAITALL))
		return s;

	if (s <= 0)
		return size;

	size += s;
	if (size < len)
		goto retry;

	return size;
}

static ssize_t
task_socket_send (int fd, const void *buf, size_t len, int flags, HevSocks5Session *session)
{
	ssize_t s;
	size_t size = 0;

retry:
	s = send (fd, buf + size, len - size, flags & ~MSG_WAITALL);
	if (s == -1 && errno == EAGAIN) {
		hev_task_yield (HEV_TASK_WAITIO);
		if (session->base.hp <= 0)
			return -2;
		goto retry;
	}

	if (!(flags & MSG_WAITALL))
		return s;

	if (s <= 0)
		return size;

	size += s;
	if (size < len)
		goto retry;

	return size;
}

static ssize_t
task_socket_sendmsg (int fd, const struct msghdr *msg, int flags, HevSocks5Session *session)
{
	ssize_t s;
	size_t i, size = 0, len = 0;
	struct msghdr mh;
	struct iovec iov[msg->msg_iovlen];

	mh.msg_name = msg->msg_name;
	mh.msg_namelen = msg->msg_namelen;
	mh.msg_control = msg->msg_control;
	mh.msg_controllen = msg->msg_controllen;
	mh.msg_flags = msg->msg_flags;
	mh.msg_iov = iov;
	mh.msg_iovlen = msg->msg_iovlen;

	for (i=0; i<msg->msg_iovlen; i++) {
		iov[i] = msg->msg_iov[i];
		len += iov[i].iov_len;
	}

retry:
	s = sendmsg (fd, &mh, flags & ~MSG_WAITALL);
	if (s == -1 && errno == EAGAIN) {
		hev_task_yield (HEV_TASK_WAITIO);
		if (session->base.hp <= 0)
			return -2;
		goto retry;
	}

	if (!(flags & MSG_WAITALL))
		return s;

	if (s <= 0)
		return size;

	size += s;
	if (size < len) {
		for (i=0; i<mh.msg_iovlen; i++) {
			if (s < iov[i].iov_len) {
				iov[i].iov_base += s;
				iov[i].iov_len -= s;
				break;
			}

			s -= iov[i].iov_len;
		}

		mh.msg_iov += i;
		mh.msg_iovlen -= i;

		goto retry;
	}

	return size;
}

static ssize_t
task_socket_recvfrom (int fd, void *buf, size_t len, int flags,
			struct sockaddr *addr, socklen_t *addr_len,
			HevSocks5Session *session)
{
	ssize_t s;

retry:
	s = recvfrom (fd, buf, len, flags, addr, addr_len);
	if (s == -1 && errno == EAGAIN) {
		hev_task_yield (HEV_TASK_WAITIO);
		if (session->base.hp <= 0)
			return -2;
		goto retry;
	}

	return s;
}

static ssize_t
task_socket_sendto (int fd, const void *buf, size_t len, int flags,
			const struct sockaddr *addr, socklen_t addr_len,
			HevSocks5Session *session)
{
	ssize_t s;

retry:
	s = sendto (fd, buf, len, flags, addr, addr_len);
	if (s == -1 && errno == EAGAIN) {
		hev_task_yield (HEV_TASK_WAITIO);
		if (session->base.hp <= 0)
			return -2;
		goto retry;
	}

	return s;
}

static int
task_socket_splice (int fd_in, int fd_out, void *buf, size_t len,
			size_t *w_off, size_t *w_left)
{
	ssize_t s;

	if (*w_left == 0) {
		s = recv (fd_in, buf, len, 0);
		if (s == -1) {
			if (errno == EAGAIN)
				return 0;
			else
				return -1;
		} else if (s == 0) {
			return -1;
		} else {
			*w_off = 0;
			*w_left = s;
		}
	}

	s = send (fd_out, buf + *w_off, *w_left, 0);
	if (s == -1) {
		if (errno == EAGAIN)
			return 0;
		else
			return -1;
	} else if (s == 0) {
		return -1;
	} else {
		*w_off += s;
		*w_left -= s;
	}

	return *w_off;
}

static int
task_socket_connect (int fd, const struct sockaddr *addr, socklen_t addr_len,
			HevSocks5Session *session)
{
	int ret;
retry:
	ret = connect (fd, addr, addr_len);
	if (ret == -1 && errno == EINPROGRESS) {
		hev_task_yield (HEV_TASK_WAITIO);
		if (session->base.hp <= 0)
			return -2;
		goto retry;
	}

	return ret;
}

static int
socks5_read_auth_method (HevSocks5Session *self)
{
	Socks5AuthHeader socks5_auth;
	int i, auth_method = 0xff;
	ssize_t len;

	/* read socks5 auth method header */
	len = task_socket_recv (self->client_fd, &socks5_auth, 2, MSG_WAITALL, self);
	if (len <= 0)
		return STEP_CLOSE_SESSION;
	/* check socks5 version */
	if (socks5_auth.ver != 0x05)
		return STEP_CLOSE_SESSION;

	/* read socks5 auth methods */
	len = task_socket_recv (self->client_fd, &socks5_auth.methods,
				socks5_auth.method_len, MSG_WAITALL, self);
	if (len <= 0)
		return STEP_CLOSE_SESSION;

	/* select socks5 auth method */
	for (i=0; i<socks5_auth.method_len; i++) {
		if (socks5_auth.methods[i] == 0) {
			auth_method = 0x00;
			break;
		}
	}

	if (auth_method != 0x00)
		return STEP_WRITE_AUTH_METHOD_ERROR;

	return STEP_WRITE_AUTH_METHOD;
}

static int
socks5_write_auth_method (HevSocks5Session *self, int step)
{
	Socks5AuthHeader socks5_auth;
	ssize_t len;

	/* write socks5 auth method */
	socks5_auth.ver = 0x05;
	switch (step) {
	case STEP_WRITE_AUTH_METHOD:
		socks5_auth.method = 0x00;
		break;
	case STEP_WRITE_AUTH_METHOD_ERROR:
		socks5_auth.method = 0xff;
		break;
	}
	len = task_socket_send (self->client_fd, &socks5_auth, 2, MSG_WAITALL, self);
	if (len <= 0)
		return STEP_CLOSE_SESSION;

	return STEP_READ_REQUEST;
}

static int
socks5_read_request (HevSocks5Session *self)
{
	Socks5ReqResHeader socks5_r;
	ssize_t len;

	/* read socks5 request header */
	len = task_socket_recv (self->client_fd, &socks5_r, 3, MSG_WAITALL, self);
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
	len = task_socket_recv (self->client_fd, &socks5_r->ipv4, 6, MSG_WAITALL, self);
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
	int ret = -1, dns_fd, nonblock = 1;
	unsigned char buf[2048];
	ssize_t len;
	struct sockaddr_in addr_dns;
	socklen_t addr_len_dns = sizeof (struct sockaddr_in);

	/* read socks5 request domain addr length */
	len = task_socket_recv (self->client_fd, &socks5_r->domain.len,
				1, MSG_WAITALL, self);
	if (len <= 0)
		return -1;

	/* read socks5 request domain addr and port*/
	len = task_socket_recv (self->client_fd, &socks5_r->domain.addr,
				socks5_r->domain.len + 2, MSG_WAITALL, self);
	if (len <= 0)
		return -1;

	/* copy port */
	self->address.sin_family = AF_INET;
	memcpy (&self->address.sin_port, socks5_r->domain.addr + socks5_r->domain.len, 2);

	/* check is ipv4 addr string */
	socks5_r->domain.addr[socks5_r->domain.len] = '\0';
	self->address.sin_addr.s_addr = inet_addr ((const char *) socks5_r->domain.addr);
	if (self->address.sin_addr.s_addr != INADDR_NONE)
		return 0;

	/* do dns resolve */
	len = hev_dns_query_generate ((const char *) socks5_r->domain.addr, buf, 2048);
	if (len <= 0)
		return -1;

	dns_fd = socket (AF_INET, SOCK_DGRAM, 0);
	if (dns_fd == -1)
		return -1;

	if (ioctl (dns_fd, FIONBIO, (char *) &nonblock) == -1)
		goto quit;

	task = hev_task_self ();
	hev_task_add_fd (task, dns_fd, EPOLLIN | EPOLLOUT);

	addr_dns.sin_family = AF_INET;
	addr_dns.sin_addr.s_addr = inet_addr (hev_config_get_dns_address ());
	addr_dns.sin_port = htons (53);

	/* send dns query message */
	len = task_socket_sendto (dns_fd, buf, len, 0,
				(struct sockaddr *) &addr_dns, addr_len_dns, self);
	if (len <= 0)
		goto quit;

	/* recv dns response message */
	len = task_socket_recvfrom (dns_fd, buf, 2048, 0,
				(struct sockaddr *) &addr_dns, &addr_len_dns, self);
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
	ssize_t len;
	int ret, nonblock = 1;
	struct sockaddr *addr = (struct sockaddr *) &self->address;
	socklen_t addr_len = sizeof (struct sockaddr_in);

	/* read socks5 request atype */
	len = task_socket_recv (self->client_fd, &socks5_r.atype, 1, MSG_WAITALL, self);
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

	self->remote_fd = socket (AF_INET, SOCK_STREAM, 0);
	if (self->remote_fd == -1)
		return STEP_WRITE_RESPONSE_ERROR_SOCK;

	ret = ioctl (self->remote_fd, FIONBIO, (char *) &nonblock);
	if (ret == -1)
		return STEP_WRITE_RESPONSE_ERROR_SOCK;

	task = hev_task_self ();
	hev_task_add_fd (task, self->remote_fd, EPOLLIN | EPOLLOUT);

	/* connect */
	ret = task_socket_connect (self->remote_fd, addr, addr_len, self);
	if (ret < 0)
		return STEP_WRITE_RESPONSE_ERROR_HOST;

	return STEP_WRITE_RESPONSE;
}

static int
socks5_do_splice (HevSocks5Session *self)
{
	int splice_f = 1, splice_b = 1;
	size_t w_off_f = 0, w_off_b = 0;
	size_t w_left_f = 0, w_left_b = 0;
	unsigned char buf_f[2048];
	unsigned char buf_b[2048];

	while (self->base.hp > 0) {
		int no_data = 0;

		self->base.hp = SESSION_HP;

		if (splice_f) {
			int ret;

			ret = task_socket_splice (self->client_fd, self->remote_fd,
						buf_f, 2048, &w_off_f, &w_left_f);
			if (ret == 0) { /* no data */
				/* forward no data and backward closed, quit */
				if (!splice_b)
					break;
				no_data ++;
			} else if (ret == -1) { /* error */
				/* forward error and backward closed, quit */
				if (!splice_b)
					break;
				/* forward error or closed, mark to skip */
				splice_f = 0;
			}
		}

		if (splice_b) {
			int ret;

			ret = task_socket_splice (self->remote_fd, self->client_fd,
						buf_b, 2048, &w_off_b, &w_left_b);
			if (ret == 0) { /* no data */
				/* backward no data and forward closed, quit */
				if (!splice_f)
					break;
				no_data ++;
			} else if (ret == -1) { /* error */
				/* backward error and forward closed, quit */
				if (!splice_f)
					break;
				/* backward error or closed, mark to skip */
				splice_b = 0;
			}
		}

		/* single direction no data, goto yield.
		 * double direction no data, goto waitio.
		 */
		hev_task_yield ((no_data < 2) ? HEV_TASK_YIELD : HEV_TASK_WAITIO);
	}

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
	len = task_socket_recv (self->client_fd, &socks5_r.atype, 7, MSG_WAITALL, self);
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
	int dns_fd, nonblock = 1;
	unsigned char buf[2048];
	ssize_t len;
	struct msghdr mh;
	struct iovec iov[2];
	socklen_t addr_len = sizeof (struct sockaddr_in);
	unsigned short dns_len;

	dns_fd = socket (AF_INET, SOCK_DGRAM, 0);
	if (dns_fd == -1)
		return STEP_CLOSE_SESSION;

	if (ioctl (dns_fd, FIONBIO, (char *) &nonblock) == -1)
		goto quit;

	task = hev_task_self ();
	hev_task_add_fd (task, dns_fd, EPOLLIN | EPOLLOUT);

	/* read dns request length */
	len = task_socket_recv (self->client_fd, &dns_len, 2, MSG_WAITALL, self);
	if (len <= 0)
		return STEP_CLOSE_SESSION;
	dns_len = ntohs (dns_len);

	/* read dns request */
	len = task_socket_recv (self->client_fd, buf, dns_len, MSG_WAITALL, self);
	if (len <= 0)
		return STEP_CLOSE_SESSION;

	/* send dns request */
	len = task_socket_sendto (dns_fd, buf, len, 0,
				(struct sockaddr *) &self->address, addr_len, self);
	if (len <= 0)
		goto quit;

	/* recv dns response */
	len = task_socket_recvfrom (dns_fd, buf, 2048, 0,
				(struct sockaddr *) &self->address, &addr_len, self);
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

	task_socket_sendmsg (self->client_fd, &mh, MSG_WAITALL, self);

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
	task_socket_send (self->client_fd, &socks5_r, 10, MSG_WAITALL, self);

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

	hev_task_add_fd (task, self->client_fd, EPOLLIN | EPOLLOUT);

	while (step) {
		self->base.hp = SESSION_HP;

		switch (step) {
		case STEP_READ_AUTH_METHOD:
			step = socks5_read_auth_method (self);
			break;
		case STEP_WRITE_AUTH_METHOD:
		case STEP_WRITE_AUTH_METHOD_ERROR:
			step = socks5_write_auth_method (self, step);
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

