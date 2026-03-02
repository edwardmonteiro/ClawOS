/*
 * ClawOS - Logging
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>

#include "claw/kernel.h"

static const char *level_str[] = {
    "ERROR", "WARN", "INFO", "DEBUG"
};

static int global_log_level = CLAW_LOG_INFO;

void claw_log_set_level(int level)
{
    global_log_level = level;
}

void claw_log(int level, const char *fmt, ...)
{
    if (level > global_log_level)
        return;

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm;
    localtime_r(&ts.tv_sec, &tm);

    char timebuf[32];
    strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm);

    fprintf(stderr, "[%s.%03ld] [%s] ",
            timebuf, ts.tv_nsec / 1000000, level_str[level]);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);

    fprintf(stderr, "\n");
}
