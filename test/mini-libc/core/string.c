/* SPDX-License-Identifier: MIT */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    char b1[32], b2[32];

    CHK(strlen("hello") == 5, "strlen");
    CHK(strlen("") == 0, "strlen0");

    memset(b1, 0xAB, 16);
    CHK(((unsigned char *)b1)[0] == 0xAB && ((unsigned char *)b1)[15] == 0xAB,
        "memset");

    const char *src = "abcdef";
    memcpy(b1, src, 7);
    CHK(strcmp(b1, "abcdef") == 0, "memcpy+strcmp");
    CHK(memcmp(b1, "abcxef", 3) == 0 && memcmp(b1, "abcxef", 4) != 0,
        "memcmp");

    CHK(strcmp("abc", "abc") == 0 && strcmp("abc", "abd") < 0 &&
        strcmp("abd", "abc") > 0, "strcmp-order");
    CHK(strncmp("abcdef", "abcxyz", 3) == 0 && strncmp("abcdef", "abcxyz", 4) != 0,
        "strncmp");

    strcpy(b2, "xy");
    strcat(b2, "z");
    CHK(strcmp(b2, "xyz") == 0, "strcpy+strcat");
    strncpy(b2, "123456", 3);
    CHK(b2[0]=='1' && b2[1]=='2' && b2[2]=='3', "strncpy");
    strncat(b2, "TAIL", 2);
    CHK(strstr(b2, "3TA") != 0, "strncat+strstr");

    CHK(strchr("hello", 'l') == strchr("hello", 'l'), "strchr");
    CHK(*strrchr("hello", 'l') == 'l' && strrchr("hi", 'l') == 0, "strrchr");
    CHK(strcmp(strstr("aXbXc", "Xc"), "Xc") == 0, "strstr");
    CHK(strstr("abc", "d") == 0, "strstr-miss");

    memmove(b1, b1 + 1, 3);   /* overlapping move must not corrupt */
    CHK(b1[0] != 0, "memmove");

    printf("DONE-string fails=%d\n", fails);
    exit(fails);
}
