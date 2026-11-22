/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_TIME_H
#define _MINI_LIBC_TIME_H

#include <stddef.h>

/* mini-libc time: calendar/clock functions over SYS_TIME.
 *
 * The kernel RTC keeps UTC; time() returns a Unix timestamp and
 * localtime() applies the fixed UTC+8 Beijing offset (no TZ/DST).
 * gmtime()/localtime() return pointers to a single static struct tm
 * (not reentrant). mktime() interprets the fields as local (UTC+8)
 * civil time. strftime() supports %Y %y %m %d %e %H %M %S %I %p %j
 * %w %a %A %b %B %%; everything else is copied verbatim.
 * sleep()/usleep() map to TIME_SLEEP (microsecond granularity). */

typedef long time_t;
typedef long clock_t;

struct tm {
    int tm_sec;    /* 0-60  (leap second allowed) */
    int tm_min;    /* 0-59 */
    int tm_hour;   /* 0-23 */
    int tm_mday;   /* 1-31 */
    int tm_mon;    /* 0-11  (January = 0) */
    int tm_year;   /* years since 1900 */
    int tm_wday;   /* 0-6   (Sunday = 0) */
    int tm_yday;   /* 0-365 */
    int tm_isdst;  /* always 0 */
};

#define CLOCKS_PER_SEC 1000000L

#ifndef NUL
#define NUL ((void *)0)
#endif

extern char *tzname[2];
extern long timezone;
extern int daylight;

/* seconds since 1970-01-01 00:00:00 UTC; *t gets the same value */
time_t time(time_t *t);
/* broken-down UTC / Beijing (UTC+8) time; static storage, not reentrant */
struct tm *gmtime(const time_t *t);
struct tm *localtime(const time_t *t);
/* inverse of localtime(); out-of-range fields are normalized */
time_t mktime(struct tm *tm);
double difftime(time_t a, time_t b);
/* formatted time; returns chars written (excl. NUL), 0 on overflow */
size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *tm);

/* CLOCKS_PER_SEC ticks of process CPU time — approximated by the
 * wall clock since process start is not available, so clock() returns
 * elapsed wall time in "clock ticks" (1 tick = 1 s). */
clock_t clock(void);

unsigned int sleep(unsigned int seconds);
int usleep(unsigned long usec);

#endif /* _MINI_LIBC_TIME_H */
