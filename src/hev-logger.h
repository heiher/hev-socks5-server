/*
 ============================================================================
 Name        : hev-logger.h
 Author      : Heiher <r@hev.cc>
 Copyright   : Copyright (c) 2019 everyone.
 Description : Logger
 ============================================================================
 */

#ifndef __HEV_LOGGER_H__
#define __HEV_LOGGER_H__

#define LOG_D(fmt...) hev_logger_log (HEV_LOGGER_DEBUG, fmt)
#define LOG_I(fmt...) hev_logger_log (HEV_LOGGER_INFO, fmt)
#define LOG_W(fmt...) hev_logger_log (HEV_LOGGER_WARN, fmt)
#define LOG_E(fmt...) hev_logger_log (HEV_LOGGER_ERROR, fmt)

typedef enum
{
    HEV_LOGGER_DEBUG,
    HEV_LOGGER_INFO,
    HEV_LOGGER_WARN,
    HEV_LOGGER_ERROR,
} HevLoggerLevel;

int hev_logger_init (void);
void hev_logger_fini (void);

int hev_logger_enabled (void);

void hev_logger_log (HevLoggerLevel level, const char *fmt, ...);

#endif /* __HEV_LOGGER_H__ */
