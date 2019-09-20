/*
 ============================================================================
 Name        : hev-socks5-worker.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2017 - 2019 everyone.
 Description : Socks5 worker
 ============================================================================
 */

#include <stdio.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "hev-socks5-worker.h"
#include "hev-socks5-session.h"
#include "hev-memory-allocator.h"
#include "hev-config.h"
#include "hev-logger.h"
#include "hev-task.h"
#include "hev-task-io.h"
#include "hev-task-io-pipe.h"
#include "hev-task-io-socket.h"

#define TIMEOUT (30 * 1000)

struct _HevSocks5Worker
{
    int fd;
    int event_fds[2];
    int quit;

    HevTask *task_worker;
    HevTask *task_event;
    HevTask *task_session_manager;
    HevSocks5SessionBase *session_list;
};

static void hev_socks5_worker_task_entry (void *data);
static void hev_socks5_event_task_entry (void *data);
static void hev_socks5_session_manager_task_entry (void *data);

static void session_manager_insert_session (HevSocks5Worker *self,
                                            HevSocks5Session *session);
static void session_manager_remove_session (HevSocks5Worker *self,
                                            HevSocks5Session *session);
static void session_close_handler (HevSocks5Session *session, void *data);

HevSocks5Worker *
hev_socks5_worker_new (int fd)
{
    HevSocks5Worker *self;

    self = hev_malloc0 (sizeof (HevSocks5Worker));
    if (!self) {
        LOG_E ("Allocate worker failed!");
        goto exit;
    }

    self->fd = fd;
    self->event_fds[0] = -1;
    self->event_fds[1] = -1;

    self->task_worker = hev_task_new (8192);
    if (!self->task_worker) {
        LOG_E ("Create worker's task failed!");
        goto exit_free;
    }

    self->task_event = hev_task_new (8192);
    if (!self->task_event) {
        LOG_E ("Create event's task failed!");
        goto exit_free_task_worker;
    }

    self->task_session_manager = hev_task_new (8192);
    if (!self->task_session_manager) {
        LOG_E ("Create session manager's task failed!");
        goto exit_free_task_event;
    }

    return self;

exit_free_task_event:
    hev_task_unref (self->task_event);
exit_free_task_worker:
    hev_task_unref (self->task_worker);
exit_free:
    hev_free (self);
exit:
    return NULL;
}

void
hev_socks5_worker_destroy (HevSocks5Worker *self)
{
    hev_free (self);
}

void
hev_socks5_worker_start (HevSocks5Worker *self)
{
    hev_task_run (self->task_worker, hev_socks5_worker_task_entry, self);
    hev_task_run (self->task_event, hev_socks5_event_task_entry, self);
    hev_task_run (self->task_session_manager,
                  hev_socks5_session_manager_task_entry, self);
}

void
hev_socks5_worker_stop (HevSocks5Worker *self)
{
    int val;

    if (self->event_fds[1] == -1)
        return;

    if (write (self->event_fds[1], &val, sizeof (val)) == -1)
        LOG_E ("Write stop event failed!");
}

static int
worker_task_io_yielder (HevTaskYieldType type, void *data)
{
    HevSocks5Worker *self = data;

    hev_task_yield (type);

    return (self->quit) ? -1 : 0;
}

static void
hev_socks5_worker_task_entry (void *data)
{
    HevSocks5Worker *self = data;
    HevTask *task = hev_task_self ();

    hev_task_add_fd (task, self->fd, POLLIN);

    for (;;) {
        int client_fd;
        struct sockaddr_in6 addr;
        socklen_t len = sizeof (addr);
        HevSocks5Session *s;

        client_fd = hev_task_io_socket_accept (self->fd,
                                               (struct sockaddr *)&addr, &len,
                                               worker_task_io_yielder, self);
        if (-1 == client_fd) {
            LOG_E ("Accept failed!");
            continue;
        } else if (-2 == client_fd) {
            break;
        }

        s = hev_socks5_session_new (client_fd, &addr, session_close_handler,
                                    self);
        if (!s) {
            close (client_fd);
            continue;
        }

        session_manager_insert_session (self, s);
        hev_socks5_session_run (s);
    }
}

static void
hev_socks5_event_task_entry (void *data)
{
    HevSocks5Worker *self = data;
    HevTask *task = hev_task_self ();
    HevSocks5SessionBase *s;
    int val;

    if (-1 == hev_task_io_pipe_pipe (self->event_fds)) {
        LOG_E ("Create eventfd failed!");
        return;
    }

    hev_task_add_fd (task, self->event_fds[0], POLLIN);
    hev_task_io_read (self->event_fds[0], &val, sizeof (val), NULL, NULL);

    /* set quit flag */
    self->quit = 1;
    /* wakeup worker's task */
    hev_task_wakeup (self->task_worker);
    /* wakeup session manager's task */
    hev_task_wakeup (self->task_session_manager);

    /* wakeup sessions's task */
    for (s = self->session_list; s; s = s->next) {
        s->hp = 0;

        /* wakeup s's task to do destroy */
        hev_task_wakeup (s->task);
    }

    close (self->event_fds[0]);
    close (self->event_fds[1]);
}

static void
hev_socks5_session_manager_task_entry (void *data)
{
    HevSocks5Worker *self = data;

    for (;;) {
        HevSocks5SessionBase *s;

        hev_task_sleep (TIMEOUT);
        if (self->quit)
            break;

        for (s = self->session_list; s; s = s->next) {
            s->hp--;
            if (s->hp > 0)
                continue;

            /* wakeup s's task to do destroy */
            hev_task_wakeup (s->task);
        }
    }
}

static void
session_manager_insert_session (HevSocks5Worker *self, HevSocks5Session *s)
{
    HevSocks5SessionBase *session_base = (HevSocks5SessionBase *)s;

    /* insert session to session_list */
    session_base->prev = NULL;
    session_base->next = self->session_list;
    if (self->session_list)
        self->session_list->prev = session_base;
    self->session_list = session_base;
}

static void
session_manager_remove_session (HevSocks5Worker *self, HevSocks5Session *s)
{
    HevSocks5SessionBase *session_base = (HevSocks5SessionBase *)s;

    /* remove session from session_list */
    if (session_base->prev) {
        session_base->prev->next = session_base->next;
    } else {
        self->session_list = session_base->next;
    }
    if (session_base->next) {
        session_base->next->prev = session_base->prev;
    }
}

static void
session_close_handler (HevSocks5Session *s, void *data)
{
    HevSocks5Worker *self = data;

    session_manager_remove_session (self, s);
}
