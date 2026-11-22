/* SPDX-License-Identifier: MIT */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    int32_t v = -12345;
    char buf[128];
    sprintf(buf, "d=%" PRId32 " i=%" PRIi32 " u=%" PRIu32
                 " x=%" PRIx32 " X=%" PRIX32 " o=%" PRIo32,
            v, v, (uint32_t)v, (uint32_t)v, (uint32_t)v, (uint32_t)v);
    CHK(strstr(buf, "d=-12345") != 0, "PRId32");
    CHK(strstr(buf, "u=4294954951") != 0, "PRIu32");
    CHK(strstr(buf, "x=ffffcfc7") != 0, "PRIx32");
    printf("  -> %s\n", buf);

    sprintf(buf, "p=%" PRIxPTR " m=%" PRIuMAX,
            (uintptr_t)&v, (uintmax_t)99);
    CHK(strstr(buf, "m=99") != 0, "PRIuMAX");

    char *e;
    intmax_t m = strtoimax("0x2A", &e, 0);
    CHK(m == 42 && *e == 0, "strtoimax");
    uintmax_t um = strtoumax("77", 0, 10);
    CHK(um == 77, "strtoumax");

    imaxdiv_t d = imaxdiv(-7, 2);
    CHK(d.quot == -3 && d.rem == -1, "imaxdiv");
    CHK(imaxabs(-9) == 9 && imaxabs(9) == 9, "imaxabs");

    /* 8/16-bit macros expand (formatter prints promoted int anyway) */
    int8_t i8 = -5;
    uint16_t u16 = 0xBEEF;
    sprintf(buf, "%" PRId8 " %" PRIu16, i8, u16);
    CHK(strstr(buf, "-5") != 0 && strstr(buf, "48879") != 0, "PR-8-16");

    printf("DONE-inttypes fails=%d\n", fails);
    exit(fails);
}
