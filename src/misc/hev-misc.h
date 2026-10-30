/*
 ============================================================================
 Name        : hev-misc.h
 Authors     : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2022 - 2024 everyone.
 Description : Misc
 ============================================================================
 */

#ifndef __HEV_MISC_H__
#define __HEV_MISC_H__

#include <hev-sys.h>
#include <hev-task.h>

int hev_netaddr_resolve (struct sockaddr_in6 *daddr, const char *addr,
                         const char *port);
int hev_netaddr_is_any (struct sockaddr_in6 *addr);

void run_as_daemon (const char *pid_file);
int set_limit_nofile (int limit_nofile);

int set_sock_bind (int fd, const char *iface);
int set_sock_mark (int fd, unsigned int mark);

/*
 * A yielder that never waits, the I/O fails instead of yielding the current
 * task. For use outside of the task context, e.g. other threads and signal
 * handlers.
 */
static inline int
hev_task_io_nowait (HevTaskYieldType type, void *data)
{
    return -1;
}

#endif /* __HEV_MISC_H__ */
