/*
 ============================================================================
 Name        : hev-socks5-worker.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2021 hev
 Description : Socks5 Worker
 ============================================================================
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <hev-task.h>
#include <hev-task-io.h>
#include <hev-task-io-pipe.h>
#include <hev-task-io-socket.h>
#include <hev-task-dns.h>
#include <hev-memory-allocator.h>
#include <hev-socks5-authenticator.h>

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
    HevSocks5Authenticator *auth;
};

static void hev_socks5_event_task_entry (void *data);
static void hev_socks5_worker_task_entry (void *data);
static void hev_socks5_worker_load (HevSocks5Worker *self);

HevSocks5Worker *
hev_socks5_worker_new (int fd)
{
    HevSocks5Worker *self;

    self = hev_malloc0 (sizeof (HevSocks5Worker));
    if (!self)
        return NULL;

    LOG_D ("%p socks5 worker new", self);

    self->fd = fd;
    self->event_fds[0] = -1;
    self->event_fds[1] = -1;

    hev_socks5_worker_load (self);

    self->task_worker = hev_task_new (-1);
    if (!self->task_worker) {
        LOG_E ("socks5 worker task worker");
        hev_free (self);
        return NULL;
    }

    self->task_event = hev_task_new (-1);
    if (!self->task_event) {
        LOG_E ("socks5 worker task event");
        hev_task_unref (self->task_worker);
        hev_free (self);
        return NULL;
    }

    return self;
}

void
hev_socks5_worker_destroy (HevSocks5Worker *self)
{
    LOG_D ("%p works worker destroy", self);

    if (self->auth)
        hev_object_unref (HEV_OBJECT (self->auth));

    hev_free (self);
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

static int
task_io_yielder (HevTaskYieldType type, void *data)
{
    HevSocks5Worker *self = data;

    hev_task_yield (type);

    return self->quit ? -1 : 0;
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
    HevSocks5Worker *self = data;
    HevListNode *node;
    int stack_size;
    int fd;

    LOG_D ("socks5 worker task run");

    fd = self->fd;
    hev_task_add_fd (hev_task_self (), fd, POLLIN);
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

        if (self->auth)
            hev_socks5_server_set_auth (HEV_SOCKS5_SERVER (s), self->auth);

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
}

static void
hev_socks5_event_task_entry (void *data)
{
    HevSocks5Worker *self = data;
    int res;

    LOG_D ("socks5 event task run");

    res = hev_task_io_pipe_pipe (self->event_fds);
    if (res < 0) {
        LOG_E ("socks5 proxy pipe");
        return;
    }

    hev_task_add_fd (hev_task_self (), self->event_fds[0], POLLIN);

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

    close (self->event_fds[0]);
    close (self->event_fds[1]);
}

static void
hev_socks5_worker_load (HevSocks5Worker *self)
{
    const char *file, *name, *pass;
    char *line = NULL;
    size_t len = 0;
    ssize_t nread;
    FILE *fp;

    LOG_D ("%p works worker load", self);

    file = hev_config_get_auth_file ();
    name = hev_config_get_auth_username ();
    pass = hev_config_get_auth_password ();

    if (!file && !name && !pass)
        return;

    if (self->auth)
        hev_socks5_authenticator_clear (self->auth);

    self->auth = hev_socks5_authenticator_new ();
    if (!self->auth)
        return;

    if (!file) {
        HevSocks5User *user;

        user = hev_socks5_user_new (name, strlen (name), pass, strlen (pass));
        hev_socks5_authenticator_add (self->auth, user);
        return;
    }

    fp = fopen (file, "r");
    if (!fp)
        return;

    while ((nread = getline (&line, &len, fp)) != -1) {
        HevSocks5User *user;
        unsigned int nlen;
        unsigned int plen;
        char name[256];
        char pass[256];
        int res;

        res = sscanf (line, "%255s %255s\n", name, pass);
        if (res != 2) {
            LOG_E ("%p works worker user/pass format", self);
            continue;
        }

        nlen = strlen (name);
        plen = strlen (pass);
        user = hev_socks5_user_new (name, nlen, pass, plen);
        res = hev_socks5_authenticator_add (self->auth, user);
        if (res < 0)
            LOG_E ("%p works worker user conflict", self);
    }

    free (line);
    fclose (fp);
}
