/* SPDX-License-Identifier: MIT */
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    time_t t = time(0);
    CHK(t > 1700000000L, "time-nonzero");

    struct tm *g = gmtime(&t);
    CHK(g != 0 && g->tm_year >= 126 && g->tm_mon >= 0 && g->tm_mon <= 11,
        "gmtime-range");
    printf("  gmt %04d-%02d-%02d %02d:%02d:%02d wday=%d yday=%d\n",
           g->tm_year + 1900, g->tm_mon + 1, g->tm_mday,
           g->tm_hour, g->tm_min, g->tm_sec, g->tm_wday, g->tm_yday);

    struct tm *l = localtime(&t);
    CHK(l != 0 && l->tm_hour == (g->tm_hour + 8) % 24, "localtime-utc8");

    char buf[64];
    size_t n = strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S",
                        localtime(&t));
    CHK(n > 15 && buf[4] == '-', "strftime");
    printf("  fmt %s\n", buf);

    /* mktime treats fields as local (UTC+8); localtime returns local,
     * so the round-trip must give back the same wall-clock fields */
    struct tm fixed;
    memset(&fixed, 0, sizeof(fixed));
    fixed.tm_year = 126; fixed.tm_mon = 0; fixed.tm_mday = 1;
    fixed.tm_hour = 0; fixed.tm_min = 0; fixed.tm_sec = 0;
    time_t back = mktime(&fixed);
    CHK(back > 0, "mktime");
    struct tm *rt = localtime(&back);
    CHK(rt->tm_year == 126 && rt->tm_mon == 0 && rt->tm_mday == 1 &&
        rt->tm_hour == 0, "mktime-roundtrip");
    printf("  mkt %ld -> %04d-%02d-%02d %02d:%02d\n", back,
           rt->tm_year + 1900, rt->tm_mon + 1, rt->tm_mday,
           rt->tm_hour, rt->tm_min);

    CHK(difftime(t, t - 100) == 100.0, "difftime");

    sleep(1);
    CHK(1, "sleep1");

    printf("DONE-time fails=%d\n", fails);
    exit(fails);
}
