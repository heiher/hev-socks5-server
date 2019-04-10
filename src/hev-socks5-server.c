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
#include <signal.h>
#include <pthread.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hev-socks5-server.h"
#include "hev-socks5-worker.h"
#include "hev-config.h"
#include "hev-task.h"
#include "hev-task-io.h"
#include "hev-task-io-socket.h"
#include "hev-task-system.h"
#include "hev-memory-allocator.h"

typedef struct _HevSocks5WorkerData HevSocks5WorkerData;

struct _HevSocks5WorkerData
{
    int fd;
    HevSocks5Worker *worker;
};

static unsigned int workers;
static HevSocks5WorkerData *worker_list;

static void sigint_handler (int signum);
static void *work_thread_handler (void *data);

static int
hev_socks5_server_socket (int reuseport)
{
    int fd, ret, reuse = 1;
    struct sockaddr_in6 addr6 = { 0 };
    struct sockaddr *addr = (struct sockaddr *)&addr6;
    const socklen_t addr_len = sizeof (addr6);
    const char *address = hev_config_get_listen_address ();

    fd = hev_task_io_socket_socket (AF_INET6, SOCK_STREAM, 0);
    if (fd == -1) {
        fprintf (stderr, "Create socket failed!\n");
        return -1;
    }

    ret = setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof (reuse));
    if (ret == -1) {
        fprintf (stderr, "Set reuse address failed!\n");
        close (fd);
        return -2;
    }

    if (reuseport) {
        ret = setsockopt (fd, SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof (reuse));
        if (ret == -1) {
            fprintf (stderr, "Set reuse port failed!\n");
            close (fd);
            return -3;
        }
    }

    addr6.sin6_family = AF_INET6;
    addr6.sin6_port = htons (hev_config_get_port ());
    if (inet_pton (AF_INET, address, &addr6.sin6_addr.s6_addr[12]) == 1) {
        ((unsigned short *)&addr6.sin6_addr)[5] = 0xffff;
    } else {
        if (inet_pton (AF_INET6, address, &addr6.sin6_addr) != 1) {
            fprintf (stderr, "Parse address failed!\n");
            close (fd);
            return -4;
        }
    }

    ret = bind (fd, addr, addr_len);
    if (ret == -1) {
        fprintf (stderr, "Bind address failed!\n");
        close (fd);
        return -5;
    }

    ret = listen (fd, 100);
    if (ret == -1) {
        fprintf (stderr, "Listen failed!\n");
        close (fd);
        return -6;
    }

    return fd;
}

int
hev_socks5_server_init (void)
{
    int i, reuseport = 1;

    if (hev_task_system_init () < 0) {
        fprintf (stderr, "Init task system failed!\n");
        return -1;
    }

    workers = hev_config_get_workers ();
    worker_list = hev_malloc0 (sizeof (HevSocks5WorkerData) * workers);
    if (!worker_list) {
        fprintf (stderr, "Allocate worker list failed!\n");
        return -2;
    }

    worker_list[0].fd = hev_socks5_server_socket (1);
    if (worker_list[0].fd == -3) {
        reuseport = 0;
        worker_list[0].fd = hev_socks5_server_socket (0);
    }
    if (worker_list[0].fd < 0)
        return -3;

    for (i = 1; i < workers; i++) {
        if (reuseport) {
            worker_list[i].fd = hev_socks5_server_socket (1);
            if (worker_list[i].fd < 0)
                return -4;
        } else {
            worker_list[i].fd = worker_list[0].fd;
        }
    }

    if (signal (SIGPIPE, SIG_IGN) == SIG_ERR) {
        fprintf (stderr, "Set signal pipe's handler failed!\n");
        return -5;
    }

    if (signal (SIGINT, sigint_handler) == SIG_ERR) {
        fprintf (stderr, "Set signal int's handler failed!\n");
        return -6;
    }

    return 0;
}

void
hev_socks5_server_fini (void)
{
    hev_free (worker_list);
    hev_task_system_fini ();
}

int
hev_socks5_server_run (void)
{
    int i;
    pthread_t work_threads[workers];

    worker_list[0].worker = hev_socks5_worker_new (worker_list[0].fd);
    if (!worker_list[0].worker) {
        fprintf (stderr, "Create socks5 worker 0 failed!\n");
        return -1;
    }

    hev_socks5_worker_start (worker_list[0].worker);

    for (i = 1; i < workers; i++) {
        pthread_create (&work_threads[i], NULL, work_thread_handler,
                        (void *)(intptr_t)i);
    }

    hev_task_system_run ();

    for (i = 1; i < workers; i++) {
        pthread_join (work_threads[i], NULL);
    }

    hev_socks5_worker_destroy (worker_list[0].worker);
    close (worker_list[0].fd);

    return 0;
}

static void
sigint_handler (int signum)
{
    int i;

    for (i = 0; i < workers; i++) {
        if (!worker_list[i].worker)
            continue;

        hev_socks5_worker_stop (worker_list[i].worker);
    }
}

static void *
work_thread_handler (void *data)
{
    int i = (intptr_t)data;

    if (hev_task_system_init () < 0) {
        fprintf (stderr, "Init task system failed!\n");
        return NULL;
    }

    worker_list[i].worker = hev_socks5_worker_new (worker_list[i].fd);
    if (!worker_list[i].worker) {
        fprintf (stderr, "Create socks5 worker %d failed!\n", i);
        hev_task_system_fini ();
        return NULL;
    }

    hev_socks5_worker_start (worker_list[i].worker);

    hev_task_system_run ();

    hev_socks5_worker_destroy (worker_list[i].worker);
    close (worker_list[i].fd);

    hev_task_system_fini ();

    return NULL;
}
