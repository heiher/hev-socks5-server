/*
 ============================================================================
 Name        : hev-socks5-server.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 everyone.
 Description : Socks5 server
 ============================================================================
 */

#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hev-socks5-server.h"
#include "hev-socks5-session.h"
#include "hev-config.h"
#include "hev-task.h"

#define TIMEOUT		(30 * 1000)

static void hev_socks5_server_task_entry (void *data);
static void hev_socks5_session_manager_task_entry (void *data);

static void session_manager_insert_session (HevSocks5Session *session);
static void session_manager_remove_session (HevSocks5Session *session);
static void session_close_handler (HevSocks5Session *session, void *data);

static HevTask *task;
static HevSocks5SessionBase *session_list;

int
hev_socks5_server_init (void)
{
	task = hev_task_new (4096);
	if (!task) {
		fprintf (stderr, "Create server's task failed!\n");
		return -1;
	}
	hev_task_run (task, hev_socks5_server_task_entry, NULL);

	task = hev_task_new (4096);
	if (!task) {
		fprintf (stderr, "Create session manager's task failed!\n");
		return -1;
	}

	return 0;
}

void
hev_socks5_server_fini (void)
{
}

void
hev_socks5_server_run (void)
{
	hev_task_run (task, hev_socks5_session_manager_task_entry, NULL);
}

static int
task_socket_accept (int fd, struct sockaddr *addr, socklen_t *addr_len)
{
	int new_fd;
retry:
	new_fd = accept (fd, addr, addr_len);
	if (new_fd == -1 && errno == EAGAIN) {
		hev_task_yield (HEV_TASK_WAITIO);
		goto retry;
	}

	return new_fd;
}

static void
hev_socks5_server_task_entry (void *data)
{
	HevTask *task = hev_task_self ();
	int fd, ret, nonblock = 1, reuseaddr = 1;
	struct sockaddr_in addr;

	fd = socket (AF_INET, SOCK_STREAM, 0);
	if (fd == -1) {
		fprintf (stderr, "Create socket failed!\n");
		return;
	}

	ret = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR,
				&reuseaddr, sizeof (reuseaddr));
	if (ret == -1) {
		fprintf (stderr, "Set reuse address failed!\n");
		close (fd);
		return;
	}
	ret = ioctl (fd, FIONBIO, (char *) &nonblock);
	if (ret == -1) {
		fprintf (stderr, "Set non-blocking failed!\n");
		close (fd);
		return;
	}

	memset (&addr, 0, sizeof (addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = inet_addr (hev_config_get_listen_address ());
	addr.sin_port = htons (hev_config_get_port ());
	ret = bind (fd, (struct sockaddr *) &addr, (socklen_t) sizeof (addr));
	if (ret == -1) {
		fprintf (stderr, "Bind address failed!\n");
		close (fd);
		return;
	}
	ret = listen (fd, 100);
	if (ret == -1) {
		fprintf (stderr, "Listen failed!\n");
		close (fd);
		return;
	}

	hev_task_add_fd (task, fd, EPOLLIN);

	for (;;) {
		int client_fd;
		struct sockaddr *in_addr = (struct sockaddr *) &addr;
		socklen_t addr_len = sizeof (addr);
		HevSocks5Session *session;

		client_fd = task_socket_accept (fd, in_addr, &addr_len);
		if (-1 == client_fd) {
			fprintf (stderr, "Accept failed!\n");
			continue;
		}

#ifdef _DEBUG
		printf ("New client %d enter from %s:%u\n", client_fd,
					inet_ntoa (addr.sin_addr), ntohs (addr.sin_port));
#endif

		ret = ioctl (client_fd, FIONBIO, (char *) &nonblock);
		if (ret == -1) {
			fprintf (stderr, "Set non-blocking failed!\n");
			close (client_fd);
		}

		session = hev_socks5_session_new (client_fd, session_close_handler, NULL);
		if (!session) {
			close (client_fd);
			continue;
		}

		session_manager_insert_session (session);
		hev_socks5_session_run (session);
	}

	close (fd);
}

static void
hev_socks5_session_manager_task_entry (void *data)
{
	for (;;) {
		HevSocks5SessionBase *session;

		hev_task_sleep (TIMEOUT);

#ifdef _DEBUG
		printf ("Enumerating session list ...\n");
#endif
		for (session=session_list; session; session=session->next) {
#ifdef _DEBUG
			printf ("Session %p's hp %d\n", session, session->hp);
#endif
			session->hp --;
			if (session->hp > 0)
				continue;

			/* wakeup session's task to do destroy */
			hev_task_wakeup (session->task);
#ifdef _DEBUG
			printf ("Wakeup session %p's task %p\n", session, session->task);
#endif
		}
	}
}

static void
session_manager_insert_session (HevSocks5Session *session)
{
	HevSocks5SessionBase *session_base = (HevSocks5SessionBase *) session;

#ifdef _DEBUG
	printf ("Insert session: %p\n", session);
#endif
	/* insert session to session_list */
	session_base->prev = NULL;
	session_base->next = session_list;
	if (session_list)
		session_list->prev = session_base;
	session_list = session_base;
}

static void
session_manager_remove_session (HevSocks5Session *session)
{
	HevSocks5SessionBase *session_base = (HevSocks5SessionBase *) session;

#ifdef _DEBUG
	printf ("Remove session: %p\n", session);
#endif
	/* remove session from session_list */
	if (session_base->prev) {
		session_base->prev->next = session_base->next;
	} else {
		session_list = session_base->next;
	}
	if (session_base->next) {
		session_base->next->prev = session_base->prev;
	}
}

static void
session_close_handler (HevSocks5Session *session, void *data)
{
	session_manager_remove_session (session);
}

