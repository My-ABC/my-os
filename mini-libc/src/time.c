/* SPDX-License-Identifier: MIT */
#include <time.h>
#include <string.h>
#include <stdio.h>
#include "sys/syscall.h"

/* UTC+8 Beijing offset; the kernel RTC stores UTC. */
#define BEIJING_OFFSET 28800L

char *tzname[2] = { "UTC", "CST" };
long timezone = -28800;   /* seconds west of UTC (POSIX sign) */
int daylight = 0;

/* ---- civil calendar math (Howard Hinnant's algorithms) ---- */

/* days since 1970-01-01 for y/m/d (m: 1-12, d: 1-31) */
static long days_from_civil(long y, unsigned m, unsigned d) {
    y -= m <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);                    /* [0, 399] */
    unsigned doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;         /* [0, 146096] */
    return era * 146097 + (long)doe - 719468;
}

/* y/m/d for a day count (may be negative) */
static void civil_from_days(long z, long *y, unsigned *m, unsigned *d) {
    z += 719468;
    long era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);                  /* [0, 146096] */
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long yy = (long)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);       /* [0, 365] */
    unsigned mp = (5 * doy + 2) / 153;                           /* [0, 11] */
    *d = doy - (153 * mp + 2) / 5 + 1;                            /* [1, 31] */
    *m = mp < 10 ? mp + 3 : mp - 9;                               /* [1, 12] */
    *y = yy + (*m <= 2);
}

static int leap(long y) {
    return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
}

static void tm_from_epoch(time_t t, long offset, struct tm *out) {
    long tt = (long)t + offset;
    long days = tt / 86400;
    long rem = tt % 86400;
    if (rem < 0) { rem += 86400; --days; }

    long y;
    unsigned m, d;
    civil_from_days(days, &y, &m, &d);

    out->tm_sec = (int)(rem % 60);
    rem /= 60;
    out->tm_min = (int)(rem % 60);
    out->tm_hour = (int)(rem / 60);
    out->tm_mday = (int)d;
    out->tm_mon = (int)m - 1;
    out->tm_year = (int)(y - 1900);
    out->tm_wday = (int)((days + 4) % 7);   /* 1970-01-01 was Thursday */
    if (out->tm_wday < 0) out->tm_wday += 7;
    /* day-of-year */
    static const unsigned short cum[] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    unsigned yday = cum[m - 1] + d - 1;
    if (m > 2 && leap(y)) ++yday;
    out->tm_yday = (int)yday;
    out->tm_isdst = 0;
}

static struct tm gmtime_buf;
static struct tm localtime_buf;
static clock_t clock_base;

/* ---- public API ---- */

time_t time(time_t *t) {
    time_t now = (time_t)(unsigned)syscall0(SYS_TIME, TIME_GET);
    if (t) *t = now;
    return now;
}

struct tm *gmtime(const time_t *t) {
    tm_from_epoch(*t, 0, &gmtime_buf);
    return &gmtime_buf;
}

struct tm *localtime(const time_t *t) {
    tm_from_epoch(*t, BEIJING_OFFSET, &localtime_buf);
    return &localtime_buf;
}

time_t mktime(struct tm *tm) {
    /* fold month overflow into the year, then normalize via
     * days_from_civil on the 1st of the month plus mday-1 days */
    long y = tm->tm_year + 1900;
    long m = tm->tm_mon + 1;
    y += (m - 1) / 12;
    m = (m - 1) % 12 + 1;
    if (m <= 0) { m += 12; --y; }
    long total_days = days_from_civil(y, (unsigned)m, 1)
                         + tm->tm_mday - 1;
    long sod = (long)tm->tm_hour * 3600
                  + (long)tm->tm_min * 60
                  + tm->tm_sec;
    return (time_t)(total_days * 86400 + sod - BEIJING_OFFSET);
}

double difftime(time_t a, time_t b) {
    return (double)(a - b);
}

clock_t clock(void) {
    /* No per-process CPU clock in the ABI; report wall seconds since
     * the first call as CLOCKS_PER_SEC ticks. */
    time_t now = time(0);
    if (clock_base == 0) clock_base = now;
    return (clock_t)((now - clock_base) * CLOCKS_PER_SEC);
}

unsigned int sleep(unsigned int seconds) {
    /* TIME_SLEEP takes microseconds in a 32-bit arg: chunk at ~4000 s */
    while (seconds > 4000U) {
        syscall1(SYS_TIME, TIME_SLEEP, 4000U * 1000000U);
        seconds -= 4000U;
    }
    if (seconds != 0U)
        syscall1(SYS_TIME, TIME_SLEEP, seconds * 1000000U);
    return 0;
}

int usleep(unsigned long usec) {
    /* split into <= ~71 minute chunks to stay in u32 microseconds */
    while (usec > 4000000000UL) {
        syscall1(SYS_TIME, TIME_SLEEP, 4000000000U);
        usec -= 4000000000UL;
    }
    if (usec != 0UL)
        syscall1(SYS_TIME, TIME_SLEEP, (unsigned)usec);
    return 0;
}

/* ---- strftime ---- */

static size_t fmt_uint(char *buf, size_t max, size_t pos,
                       unsigned long v, int width, char pad) {
    char tmp[24];
    int n = 0;
    do { tmp[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    /* emit (width - n) pad chars then the digits */
    int padn = width - n;
    if (padn < 0) padn = 0;
    while (padn-- > 0 && pos + 1 < max) buf[pos++] = pad;
    while (n > 0 && pos + 1 < max) buf[pos++] = tmp[--n];
    return pos;
}

static size_t fmt_str(char *buf, size_t max, size_t pos,
                      const char *s) {
    while (*s && pos + 1 < max) buf[pos++] = *s++;
    return pos;
}

static const char *const wday_names[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char *const mon_names[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

size_t strftime(char *buf, size_t max, const char *fmt, const struct tm *tm) {
    static const char *const wday_full[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday",
        "Thursday", "Friday", "Saturday"
    };
    static const char *const mon_full[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    size_t pos = 0;
    if (max == 0) return 0;
    buf[0] = '\0';

    for (const char *p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            if (pos + 1 < max) buf[pos++] = *p;
            continue;
        }
        ++p;
        switch (*p) {
            case 'Y': pos = fmt_uint(buf, max, pos, (unsigned long)(tm->tm_year + 1900), 4, '0'); break;
            case 'y': pos = fmt_uint(buf, max, pos, (unsigned long)(tm->tm_year + 1900) % 100, 2, '0'); break;
            case 'm': pos = fmt_uint(buf, max, pos, (unsigned long)(tm->tm_mon + 1), 2, '0'); break;
            case 'd': pos = fmt_uint(buf, max, pos, (unsigned long)tm->tm_mday, 2, '0'); break;
            case 'e': pos = fmt_uint(buf, max, pos, (unsigned long)tm->tm_mday, 2, ' '); break;
            case 'H': pos = fmt_uint(buf, max, pos, (unsigned long)tm->tm_hour, 2, '0'); break;
            case 'M': pos = fmt_uint(buf, max, pos, (unsigned long)tm->tm_min, 2, '0'); break;
            case 'S': pos = fmt_uint(buf, max, pos, (unsigned long)tm->tm_sec, 2, '0'); break;
            case 'I': {
                unsigned h = (unsigned)tm->tm_hour % 12;
                if (h == 0) h = 12;
                pos = fmt_uint(buf, max, pos, h, 2, '0');
                break;
            }
            case 'p': pos = fmt_str(buf, max, pos, tm->tm_hour < 12 ? "AM" : "PM"); break;
            case 'j': pos = fmt_uint(buf, max, pos, (unsigned long)(tm->tm_yday + 1), 3, '0'); break;
            case 'w': pos = fmt_uint(buf, max, pos, (unsigned long)tm->tm_wday, 1, '0'); break;
            case 'a': pos = fmt_str(buf, max, pos, wday_names[tm->tm_wday]); break;
            case 'A': pos = fmt_str(buf, max, pos, wday_full[tm->tm_wday]); break;
            case 'b': case 'h': pos = fmt_str(buf, max, pos, mon_names[tm->tm_mon]); break;
            case 'B': pos = fmt_str(buf, max, pos, mon_full[tm->tm_mon]); break;
            case '%': if (pos + 1 < max) buf[pos++] = '%'; break;
            case '\0': --p; break;
            default:
                if (pos + 1 < max) { buf[pos++] = '%'; buf[pos++] = *p; }
                break;
        }
    }
    if (max > 0) buf[pos < max ? pos : max - 1] = '\0';
    return pos;
}
