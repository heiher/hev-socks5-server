/*
 ============================================================================
 Name        : hev-socks5-proxy.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2024 hev
 Description : Socks5 Proxy
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdatomic.h>

#include <hev-task.h>
#include <hev-task-system.h>
#include <hev-memory-allocator.h>
#include <hev-socks5-authenticator.h>

#include "hev-config.h"
#include "hev-logger.h"
#include "hev-socks5-worker.h"
#include "hev-socket-factory.h"
#include "hev-socks5-user-mark.h"

#include "hev-socks5-proxy.h"

enum
{
    SYNC_CONT = 1 << 0,
    SYNC_ABRT = 1 << 1,
    SYNC_SEND = 1 << 2,
    SYNC_SENT = 1 << 3,
    SYNC_WAIT = 1 << 4,
    SYNC_STOP = 1 << 5,
};

typedef struct _HevSocks5WorkerData HevSocks5WorkerData;

struct _HevSocks5WorkerData
{
    HevSocks5Worker *worker;
    pthread_t thread;
    int ts;
};

static atomic_int tsync;
static HevSocks5WorkerData *worker_list;

static void
hev_socks5_proxy_load_file (HevSocks5Authenticator *auth, const char *file)
{
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    FILE *fp;

    fp = fopen (file, "r");
    if (!fp) {
        hev_object_unref (HEV_OBJECT (auth));
        return;
    }

    while ((nread = getline (&line, &len, fp)) != -1) {
        HevSocks5UserMark *user;
        unsigned int nlen;
        unsigned int plen;
        char name[256];
        char pass[256];
        long mark = 0;
        int res;

        res = sscanf (line, "%255s %255s %lx\n", name, pass, &mark);
        if (res < 2) {
            LOG_E ("socks5 proxy user/pass format");
            continue;
        }

        nlen = strlen (name);
        plen = strlen (pass);
        user = hev_socks5_user_mark_new (name, nlen, pass, plen, mark);
        if (!user) {
            LOG_E ("socks5 proxy user new");
            continue;
        }
        res = hev_socks5_authenticator_add (auth, HEV_SOCKS5_USER (user));
        if (res < 0) {
            LOG_E ("socks5 proxy user conflict");
            hev_object_unref (HEV_OBJECT (user));
        }
    }

    free (line);
    fclose (fp);
}

static void
hev_socks5_proxy_load (void)
{
    HevSocks5Authenticator *auth;
    const char *file, *name, *pass;
    int workers;
    int i;

    LOG_D ("socks5 proxy load");

    file = hev_config_get_auth_file ();
    name = hev_config_get_auth_username ();
    pass = hev_config_get_auth_password ();

    if (!file && !name && !pass)
        return;

    auth = hev_socks5_authenticator_new ();
    if (!auth)
        return;

    if (file) {
        hev_socks5_proxy_load_file (auth, file);
    } else {
        HevSocks5UserMark *user;

        user = hev_socks5_user_mark_new (name, strlen (name), pass,
                                         strlen (pass), 0);
        if (user)
            hev_socks5_authenticator_add (auth, HEV_SOCKS5_USER (user));
    }

    workers = hev_config_get_workers ();
    for (i = 0; i < workers; i++) {
        HevSocks5Worker *worker = worker_list[i].worker;
        hev_socks5_worker_set_auth (worker, auth);
        hev_socks5_worker_reload (worker);
    }

    hev_object_unref (HEV_OBJECT (auth));
}

static void
sigint_handler (int signum)
{
    hev_socks5_proxy_load ();
}

static void *
work_thread_handler (void *data)
{
    HevSocks5Worker *worker = data;
    int res;

retry:
    res = atomic_load (&tsync);
    if (res & SYNC_ABRT) {
        goto exit;
    } else if (!(res & SYNC_CONT)) {
        usleep (500);
        goto retry;
    }

    res = hev_task_system_init ();
    if (res < 0) {
        LOG_E ("socks5 proxy worker task system");
        goto exit;
    }

    hev_socks5_worker_start (worker);

    hev_task_system_run ();

    hev_task_system_fini ();
exit:
    return NULL;
}

int
hev_socks5_proxy_init (void)
{
    HevSocketFactory *factory;
    const char *listen_addr;
    const char *listen_port;
    int tcp_fastopen;
    int ipv6_only;
    int workers;
    int res;
    int i;

    LOG_D ("socks5 proxy init");

    listen_addr = hev_config_get_listen_address ();
    listen_port = hev_config_get_listen_port ();
    ipv6_only = hev_config_get_listen_ipv6_only ();
    tcp_fastopen = hev_config_get_tcp_fastopen ();

    res = hev_task_system_init ();
    if (res < 0) {
        LOG_E ("socks5 proxy task system");
        return -1;
    }

    factory = hev_socket_factory_new (listen_addr, listen_port, ipv6_only,
                                      tcp_fastopen);
    if (!factory) {
        LOG_E ("socks5 proxy socket factory");
        goto exit;
    }

    workers = hev_config_get_workers ();
    worker_list = hev_malloc0 (sizeof (HevSocks5WorkerData) * workers);
    if (!worker_list) {
        LOG_E ("socks5 proxy worker list");
        goto exit;
    }

    atomic_fetch_and (&tsync, ~(SYNC_CONT | SYNC_ABRT));

    for (i = 0; i < workers; i++) {
        HevSocks5Worker *worker;
        int fd;

        fd = hev_socket_factory_get (factory);
        if (fd < 0) {
            LOG_E ("socks5 proxy socket factory get");
            goto exit;
        }

        worker = hev_socks5_worker_new (fd);
        if (!worker) {
            LOG_E ("socks5 proxy worker %d", i);
            close (fd);
            goto exit;
        }
        worker_list[i].worker = worker;

        /* Skip worker 0 */
        if (i == 0)
            continue;

        res = pthread_create (&worker_list[i].thread, NULL, work_thread_handler,
                              worker);
        if (res != 0) {
            LOG_E ("socks5 proxy worker %d thread", i);
            goto exit;
        }
        worker_list[i].ts = 1;
    }

    hev_socket_factory_destroy (factory);
    factory = NULL;

    hev_socks5_proxy_load ();
    signal (SIGPIPE, SIG_IGN);
    signal (SIGUSR1, sigint_handler);
    atomic_fetch_or (&tsync, SYNC_SEND);

    return 0;

exit:
    atomic_fetch_or (&tsync, SYNC_ABRT);
    if (factory)
        hev_socket_factory_destroy (factory);
    hev_socks5_proxy_fini ();
    return -1;
}

void
hev_socks5_proxy_fini (void)
{
    int res;

    LOG_D ("socks5 proxy fini");

retry:
    res = atomic_fetch_and (&tsync, ~(SYNC_SEND | SYNC_STOP | SYNC_SENT));
    if (res & SYNC_WAIT) {
        usleep (500);
        goto retry;
    }

    if (worker_list) {
        int workers = hev_config_get_workers ();
        int i;

        for (i = 0; i < workers; i++) {
            if (worker_list[i].ts)
                pthread_join (worker_list[i].thread, NULL);
            if (worker_list[i].worker)
                hev_socks5_worker_destroy (worker_list[i].worker);
        }

        hev_free (worker_list);
        worker_list = NULL;
    }

    hev_task_system_fini ();
}

void
hev_socks5_proxy_run (void)
{
    LOG_D ("socks5 proxy run");

    if (atomic_fetch_and (&tsync, ~SYNC_STOP) & SYNC_STOP)
        return;

    atomic_fetch_or (&tsync, SYNC_CONT);

    hev_socks5_worker_start (worker_list[0].worker);

    hev_task_system_run ();
}

void
hev_socks5_proxy_stop (void)
{
    int res;

    LOG_D ("socks5 proxy stop");

retry:
    res = atomic_fetch_or (&tsync, SYNC_WAIT);
    if (res & SYNC_WAIT) {
        usleep (500);
        goto retry;
    }

    if (res & SYNC_SEND) {
        res = atomic_fetch_or (&tsync, SYNC_SENT);
        if (!(res & SYNC_SENT)) {
            int workers;
            int i;
            workers = hev_config_get_workers ();
            for (i = 0; i < workers; i++)
                hev_socks5_worker_stop (worker_list[i].worker);
        }
    } else {
        atomic_fetch_or (&tsync, SYNC_STOP | SYNC_ABRT);
    }

    atomic_fetch_and (&tsync, ~SYNC_WAIT);
}
