/*
 ============================================================================
 Name        : hev-socks5-user-mark.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2023 hev
 Description : Socks5 User with mark
 ============================================================================
 */

#ifndef __HEV_SOCKS5_USER_MARK_H__
#define __HEV_SOCKS5_USER_MARK_H__

#include "hev-socks5-user.h"

#ifdef __cplusplus
extern "C" {
#endif

#define HEV_SOCKS5_USER_MARK(p) ((HevSocks5UserMark *)p)
#define HEV_SOCKS5_USER_MARK_CLASS(p) ((HevSocks5UserMarkClass *)p)
#define HEV_SOCKS5_USER_MARK_TYPE (hev_socks5_user_mark_class ())

typedef struct _HevSocks5UserMark HevSocks5UserMark;
typedef struct _HevSocks5UserMarkClass HevSocks5UserMarkClass;

struct _HevSocks5UserMark
{
    HevSocks5User base;

    unsigned int mark;
};

struct _HevSocks5UserMarkClass
{
    HevSocks5UserClass base;
};

HevObjectClass *hev_socks5_user_mark_class (void);

int hev_socks5_user_mark_construct (HevSocks5UserMark *self, const char *name,
                                    unsigned int name_len, const char *pass,
                                    unsigned int pass_len, unsigned int mark);

HevSocks5UserMark *hev_socks5_user_mark_new (const char *name,
                                             unsigned int name_len,
                                             const char *pass,
                                             unsigned int pass_len,
                                             unsigned int mark);

#ifdef __cplusplus
}
#endif

#endif /* __HEV_SOCKS5_USER_MARK_H__ */
