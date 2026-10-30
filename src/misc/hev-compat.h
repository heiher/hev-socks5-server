/*
 ============================================================================
 Name        : hev-compat.h
 Author      : hev <r@hev.cc>
 Copyright   : Copyright (c) 2026 hev
 Description : Compatibility
 ============================================================================
 */

#ifndef __HEV_COMPAT_H__
#define __HEV_COMPAT_H__

#ifndef _WIN32

#include <pthread.h>

#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hev-sys.h>
#include <windows.h>

typedef HANDLE pthread_t;

typedef struct _HevThreadArg
{
    void *(*entry) (void *);
    void *data;
} HevThreadArg;

static inline DWORD WINAPI
hev_thread_entry (LPVOID param)
{
    HevThreadArg arg = *(HevThreadArg *)param;

    free (param);
    arg.entry (arg.data);

    return 0;
}

static inline int
pthread_create (pthread_t *thread, const void *attr, void *(*entry) (void *),
                void *data)
{
    HevThreadArg *arg;

    (void)attr;

    arg = malloc (sizeof (HevThreadArg));
    if (!arg)
        return -1;
    arg->entry = entry;
    arg->data = data;

    *thread = CreateThread (NULL, 0, hev_thread_entry, arg, 0, NULL);
    if (!*thread) {
        free (arg);
        return -1;
    }

    return 0;
}

static inline int
pthread_join (pthread_t thread, void **ret)
{
    (void)ret;

    WaitForSingleObject (thread, INFINITE);
    CloseHandle (thread);

    return 0;
}

#define strcasecmp _stricmp
#define usleep(us) Sleep (((us) + 999) / 1000)

static inline ssize_t
getline (char **lineptr, size_t *n, FILE *stream)
{
    size_t len = 0;
    int c;

    if (!lineptr || !n || !stream)
        return -1;

    if (!*lineptr || *n < 2) {
        char *buf = realloc (*lineptr, 128);
        if (!buf)
            return -1;
        *lineptr = buf;
        *n = 128;
    }

    while ((c = fgetc (stream)) != EOF) {
        if (len + 2 > *n) {
            char *buf = realloc (*lineptr, *n * 2);
            if (!buf)
                return -1;
            *lineptr = buf;
            *n *= 2;
        }
        (*lineptr)[len++] = c;
        if (c == '\n')
            break;
    }

    if (len == 0)
        return -1;

    (*lineptr)[len] = '\0';
    return len;
}

#endif

#endif /* __HEV_COMPAT_H__ */
