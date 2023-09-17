/*
 ============================================================================
 Name        : hev-socks5-user-mark.c
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Socks5 User with mark
 ============================================================================
 */

#include <stdlib.h>
#include <string.h>

#include "hev-logger.h"

#include "hev-socks5-user-mark.h"

HevSocks5UserMark *
hev_socks5_user_mark_new (const char *name, unsigned int name_len,
                          const char *pass, unsigned int pass_len,
                          unsigned int mark)
{
    HevSocks5UserMark *self;
    int res;

    self = calloc (1, sizeof (HevSocks5UserMark));
    if (!self)
        return NULL;

    res = hev_socks5_user_mark_construct (self, name, name_len, pass, pass_len,
                                          mark);
    if (res < 0) {
        free (self);
        return NULL;
    }

    LOG_D ("%p socks5 user mark new", self);

    return self;
}

int
hev_socks5_user_mark_construct (HevSocks5UserMark *self, const char *name,
                                unsigned int name_len, const char *pass,
                                unsigned int pass_len, unsigned int mark)
{
    int res;

    res =
        hev_socks5_user_construct (&self->base, name, name_len, pass, pass_len);
    if (res < 0)
        return res;

    LOG_D ("%p socks5 user mark construct", self);

    HEV_OBJECT (self)->klass = HEV_SOCKS5_USER_MARK_TYPE;

    self->mark = mark;

    return 0;
}

static void
hev_socks5_user_mark_destruct (HevObject *base)
{
    HevSocks5UserMark *self = HEV_SOCKS5_USER_MARK (base);

    LOG_D ("%p socks5 user mark destruct", self);

    HEV_SOCKS5_USER_TYPE->finalizer (base);
}

HevObjectClass *
hev_socks5_user_mark_class (void)
{
    static HevSocks5UserMarkClass klass;
    HevSocks5UserMarkClass *kptr = &klass;
    HevObjectClass *okptr = HEV_OBJECT_CLASS (kptr);

    if (!okptr->name) {
        memcpy (kptr, HEV_SOCKS5_USER_TYPE, sizeof (HevSocks5UserClass));

        okptr->name = "HevSocks5UserMark";
        okptr->finalizer = hev_socks5_user_mark_destruct;
    }

    return okptr;
}
