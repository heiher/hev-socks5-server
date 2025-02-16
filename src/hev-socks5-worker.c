/*
 ============================================================================
 Name        : hev-socks5-worker.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2024 hev
 Description : Socks5 Worker
 ============================================================================
 */

#include <stdlib.h>
#include <unistd.h>
#include <stdatomic.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-pipe.h>
#include <hev-task-io-socket.h>
#include <hev-memory-allocator.h>

#include "hev-config.h"
#include "hev-logger.h"
#include "hev-compiler.h"
#include "hev-socks5-session.h"

#include "hev-socks5-worker.h"

struct _HevSocks5Worker
{
    int fd;
    int quit;
    int event_fds[2];

    HevTask *task_event;
    HevTask *task_worker;
    HevList session_set;
    HevSocks5Authenticator *auth_curr;
    HevSocks5Authenticator *auth_next;
};

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    HevSocks5Worker *self = data;

    hev_task_yield (type);

    return self->quit ? -1 : 0;
}

static void
hev_socks5_worker_load (HevSocks5Worker *self)
{
    atomic_intptr_t *ptr;
    intptr_t prev;

    LOG_D ("%p works worker load", self);

    ptr = (atomic_intptr_t *)&self->auth_next;
    prev = atomic_exchange_explicit (ptr, 0, memory_order_relaxed);
    if (!prev)
        return;

    if (self->auth_curr)
        hev_object_unref (HEV_OBJECT (self->auth_curr));

    self->auth_curr = HEV_SOCKS5_AUTHENTICATOR (prev);
}

static void
hev_socks5_session_task_entry (void *data)
{
    HevSocks5Session *s = data;
    HevSocks5Worker *self = s->data;

    hev_socks5_server_run (HEV_SOCKS5_SERVER (s));

    hev_list_del (&self->session_set, &s->node);
    hev_object_unref (HEV_OBJECT (s));
}

static void
hev_socks5_worker_task_entry (void *data)
{
    HevTask *task = hev_task_self ();
    HevSocks5Worker *self = data;
    HevListNode *node;
    int stack_size;
    int fd;

    LOG_D ("socks5 worker task run");

    fd = self->fd;
    hev_task_add_fd (task, fd, POLLIN);
    stack_size = hev_config_get_misc_task_stack_size ();

    for (;;) {
        HevSocks5Session *s;
        HevTask *task;
        int nfd;

        nfd = hev_task_io_socket_accept (fd, NULL, NULL, task_io_yielder, self);
        if (nfd == -1) {
            LOG_E ("socks5 proxy accept");
            continue;
        } else if (nfd < 0) {
            break;
        }

        s = hev_socks5_session_new (nfd);
        if (!s) {
            close (nfd);
            continue;
        }

        task = hev_task_new (stack_size);
        if (!task) {
            hev_object_unref (HEV_OBJECT (s));
            continue;
        }

        if (self->auth_curr)
            hev_socks5_server_set_auth (HEV_SOCKS5_SERVER (s), self->auth_curr);

        s->task = task;
        s->data = self;
        hev_list_add_tail (&self->session_set, &s->node);
        hev_task_run (task, hev_socks5_session_task_entry, s);
    }

    node = hev_list_first (&self->session_set);
    for (; node; node = hev_list_node_next (node)) {
        HevSocks5Session *s;

        s = container_of (node, HevSocks5Session, node);
        hev_socks5_session_terminate (s);
    }

    hev_task_del_fd (task, fd);
}

static void
hev_socks5_event_task_entry (void *data)
{
    HevTask *task = hev_task_self ();
    HevSocks5Worker *self = data;
    int res;

    LOG_D ("socks5 event task run");

    res = hev_task_io_pipe_pipe (self->event_fds);
    if (res < 0) {
        LOG_E ("socks5 proxy pipe");
        return;
    }

    hev_task_add_fd (task, self->event_fds[0], POLLIN);

    for (;;) {
        char val;

        res = hev_task_io_read (self->event_fds[0], &val, sizeof (val), NULL,
                                NULL);
        if (res < sizeof (val))
            continue;

        if (val == 'r')
            hev_socks5_worker_load (self);
        else
            break;
    }

    self->quit = 1;
    hev_task_wakeup (self->task_worker);

    hev_task_del_fd (task, self->event_fds[0]);
    close (self->event_fds[0]);
    close (self->event_fds[1]);
}

HevSocks5Worker *
hev_socks5_worker_new (void)
{
    HevSocks5Worker *self;

    self = calloc (1, sizeof (HevSocks5Worker));
    if (!self)
        return NULL;

    LOG_D ("%p socks5 worker new", self);

    self->fd = -1;
    self->event_fds[0] = -1;
    self->event_fds[1] = -1;

    return self;
}

void
hev_socks5_worker_destroy (HevSocks5Worker *self)
{
    LOG_D ("%p works worker destroy", self);

    if (self->auth_curr)
        hev_object_unref (HEV_OBJECT (self->auth_curr));
    if (self->auth_next)
        hev_object_unref (HEV_OBJECT (self->auth_next));

    if (self->fd >= 0)
        close (self->fd);

    free (self);
}

int
hev_socks5_worker_init (HevSocks5Worker *self, int fd)
{
    LOG_D ("%p works worker init", self);

    self->task_worker = hev_task_new (-1);
    if (!self->task_worker) {
        LOG_E ("socks5 worker task worker");
        return -1;
    }

    self->task_event = hev_task_new (-1);
    if (!self->task_event) {
        LOG_E ("socks5 worker task event");
        hev_task_unref (self->task_worker);
        return -1;
    }

    self->fd = fd;

    return 0;
}

void
hev_socks5_worker_start (HevSocks5Worker *self)
{
    LOG_D ("%p works worker start", self);

    hev_task_run (self->task_event, hev_socks5_event_task_entry, self);
    hev_task_run (self->task_worker, hev_socks5_worker_task_entry, self);
}

void
hev_socks5_worker_stop (HevSocks5Worker *self)
{
    char val = 's';

    LOG_D ("%p works worker stop", self);

    if (self->event_fds[1] < 0)
        return;

    val = write (self->event_fds[1], &val, sizeof (val));
    if (val < 0)
        LOG_E ("socks5 proxy write event");
}

void
hev_socks5_worker_reload (HevSocks5Worker *self)
{
    char val = 'r';

    LOG_D ("%p works worker reload", self);

    if (self->event_fds[1] < 0)
        return;

    val = write (self->event_fds[1], &val, sizeof (val));
    if (val < 0)
        LOG_E ("socks5 proxy write event");
}

void
hev_socks5_worker_set_auth (HevSocks5Worker *self, HevSocks5Authenticator *auth)
{
    atomic_intptr_t *ptr;
    intptr_t prev;

    LOG_D ("%p works worker set auth", self);

    hev_object_ref (HEV_OBJECT (auth));

    if (self->auth_curr)
        ptr = (atomic_intptr_t *)&self->auth_next;
    else
        ptr = (atomic_intptr_t *)&self->auth_curr;

    prev = atomic_exchange (ptr, (intptr_t)auth);
    if (prev)
        hev_object_unref (HEV_OBJECT (prev));
}
