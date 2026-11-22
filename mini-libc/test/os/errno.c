/* SPDX-License-Identifier: MIT */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fs.h>
#include <unistd.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    errno = 0;
    /* fopen (not raw fs_open) reports failures through errno */
    FILE *nf = fopen("definitely-not-here.xyz", "r");
    CHK(nf == 0 && errno != 0, "errno-set-on-fail");
    printf("  errno=%d\n", errno);

    errno = EINVAL;
    CHK(errno == EINVAL && EPERM != EINVAL && ENOENT > 0, "errno-consts");

    errno = 0;
    void *p = malloc(64);
    CHK(p != 0 && errno == 0, "malloc-ok");
    free(p);

    errno = 0;
    access("definitely-not-here.xyz", 0);
    CHK(errno == ENOENT, "access-enoent");

    printf("DONE-errno fails=%d\n", fails);
    exit(fails);
}
