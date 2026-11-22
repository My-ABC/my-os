/* SPDX-License-Identifier: MIT */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    char buf[128];
    int n, a, b;
    const char *rest;
    long lv;
    unsigned long uv;

    printf("fmt-check %d %u %x %X %o %c %s %p %%\n",
           -42, 42u, 0xABu, 0xCDu, 052u, 'Q', "str", (void *)buf);
    CHK(1, "printf-ran");

    n = sprintf(buf, "%5d|%-5d|%05d|%+d|% d",
                42, 42, 42, 42, 42);
    CHK(n == 25 && strcmp(buf, "   42|42   |00042|+42| 42") == 0,
        "sprintf-flags");
    printf("  -> [%s]\n", buf);

    n = snprintf(buf, 8, "abcdefghij");
    CHK(n == 10 && strcmp(buf, "abcdefg") == 0, "snprintf-trunc");

    n = sscanf("-12 0x1f 052 hello", "%d %i %i %4s", &a, &lv, &uv, buf);
    CHK(n == 4 && a == -12 && lv == 31 && uv == 42 &&
        strcmp(buf, "hell") == 0, "sscanf-mixed");

    n = sscanf("0xdeadBEEF", "%x", &a);
    CHK(n == 1 && a == (int)0xdeadBEEF, "sscanf-hex");

    /* %[...] scansets are not implemented yet — skip */

    n = sscanf("100%", "%%");   /* literal % matches nothing numeric */
    CHK(n == EOF || n == 0, "sscanf-pct");

    /* FILE* over a FAT32 file injected into the test disk image */
    FILE *f = fopen("tdata.txt", "r");
    CHK(f != 0, "fopen-r");
    if (f) {
        char line[64];
        /* fgets keeps the trailing newline */
        CHK(fgets(line, sizeof(line), f) != 0 &&
            strcmp(line, "mini-libc-test-data\n") == 0, "fgets");
        int c = fgetc(f);
        CHK(c == 's' && !feof(f), "fgetc-s");
        CHK(ferror(f) == 0, "ferror-clear");
        fclose(f);
    }
    CHK(fopen("nosuchfile.xyz", "r") == 0, "fopen-missing");

    /* fprintf to stdout stream */
    fprintf(stdout, "fprintf-%s-%d\n", "ok", 7);
    fputs("fputs-line\n", stdout);
    fputc('X', stdout);
    putchar('\n');
    puts("puts-line");

    printf("DONE-stdio fails=%d\n", fails);
    exit(fails);
}
