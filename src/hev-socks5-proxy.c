/*
 ============================================================================
 Name        : hev-socks5-proxy.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2022 hev
 Description : Socks5 Proxy
 ============================================================================
 */

#include <signal.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>

#include <hev-task.h>
#include <hev-task-system.h>
#include <hev-memory-allocator.h>

#include "hev-config.h"
#include "hev-logger.h"
#include "hev-socks5-worker.h"
#include "hev-socket-factory.h"

#include "hev-socks5-proxy.h"

static int listen_fd = -1;
static unsigned int workers;

static HevTask *task;
static pthread_t *work_threads;
static HevSocketFactory *factory;
static HevSocks5Worker **worker_list;

static void
sigint_handler (int signum)
{
    int i;

    for (i = 0; i < workers; i++) {
        HevSocks5Worker *worker;

        worker = worker_list[i];
        if (!worker)
            continue;

        hev_socks5_worker_stop (worker);
    }
}

int
hev_socks5_proxy_init (void)
{
    LOG_D ("socks5 proxy init");

    if (hev_task_system_init () < 0) {
        LOG_E ("socks5 proxy task system");
        goto exit;
    }

    if (signal (SIGPIPE, SIG_IGN) == SIG_ERR) {
        LOG_E ("socks5 proxy sigpipe");
        goto free;
    }

    if (signal (SIGINT, sigint_handler) == SIG_ERR) {
        LOG_E ("socks5 proxy sigint");
        goto free;
    }

    task = hev_task_new (-1);
    if (!task) {
        LOG_E ("socks5 proxy task");
        goto free;
    }

    workers = hev_config_get_workers ();

    work_threads = hev_malloc0 (sizeof (pthread_t) * workers);
    if (!work_threads) {
        LOG_E ("socks5 proxy work threads");
        goto free;
    }

    worker_list = hev_malloc0 (sizeof (HevSocks5Worker *) * workers);
    if (!worker_list) {
        LOG_E ("socks5 proxy worker list");
        goto free;
    }

    factory = hev_socket_factory_new (hev_config_get_listen_address (),
                                      hev_config_get_listen_port (),
                                      hev_config_get_listen_ipv6_only ());
    if (!factory) {
        LOG_E ("socks5 proxy socket factory");
        goto free;
    }

    return 0;

free:
    hev_socks5_proxy_fini ();
exit:
    return -1;
}

void
hev_socks5_proxy_fini (void)
{
    LOG_D ("socks5 proxy fini");

    if (task)
        hev_task_unref (task);
    if (work_threads)
        hev_free (work_threads);
    if (worker_list)
        hev_free (worker_list);
    if (factory)
        hev_socket_factory_destroy (factory);
    hev_task_system_fini ();
}

static void *
work_thread_handler (void *data)
{
    HevSocks5Worker **worker = data;
    int fd;

    if (hev_task_system_init () < 0) {
        LOG_E ("socks5 proxy worker task system");
        goto exit;
    }

    fd = hev_socket_factory_get (factory);
    if (fd < 0) {
        LOG_E ("socks5 proxy worker socket");
        goto free;
    }

    *worker = hev_socks5_worker_new (fd);
    if (!*worker) {
        LOG_E ("socks5 proxy worker");
        goto free;
    }

    hev_socks5_worker_start (*worker);

    hev_task_system_run ();

    hev_socks5_worker_destroy (*worker);
    *worker = NULL;

free:
    if (fd >= 0)
        close (fd);
    hev_task_system_fini ();
exit:
    return NULL;
}

static void
hev_socks5_proxy_task_entry (void *data)
{
    int i;

    LOG_D ("socks5 proxy task run");

    listen_fd = hev_socket_factory_get (factory);
    if (listen_fd < 0)
        return;

    worker_list[0] = hev_socks5_worker_new (listen_fd);
    if (!worker_list[0]) {
        LOG_E ("socks5 proxy worker");
        return;
    }

    hev_socks5_worker_start (worker_list[0]);

    for (i = 1; i < workers; i++) {
        pthread_create (&work_threads[i], NULL, work_thread_handler,
                        &worker_list[i]);
    }

    task = NULL;
}

void
hev_socks5_proxy_run (void)
{
    LOG_D ("socks5 proxy run");

    hev_task_run (task, hev_socks5_proxy_task_entry, NULL);

    hev_task_system_run ();

    if (listen_fd >= 0)
        close (listen_fd);

    if (worker_list[0]) {
        int i;

        for (i = 1; i < workers; i++)
            pthread_join (work_threads[i], NULL);

        hev_socks5_worker_destroy (worker_list[0]);
        worker_list[0] = NULL;
    }
}
