/* SPDX-License-Identifier: MIT */
/* Interactive stdin test: the kernel IO_READ strips '\n', so mini-libc
 * synthesizes a virtual newline at each line end. fgets() must see it,
 * getchar() must not, and a second fgets() must start on a fresh line. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    char buf[64];

    /* Runner types "stdin-line-one" + Enter. */
    CHK(fgets(buf, sizeof(buf), stdin) != 0, "fgets-stdin-1");
    printf("  -> [%s]\n", buf);
    CHK(strcmp(buf, "stdin-line-one\n") == 0, "fgets-stdin-1-content");

    /* Runner types "stdin-line-two" + Enter. The virtual '\n' of the
     * first line must not leak into this read. */
    CHK(fgets(buf, sizeof(buf), stdin) != 0, "fgets-stdin-2");
    printf("  -> [%s]\n", buf);
    CHK(strcmp(buf, "stdin-line-two\n") == 0, "fgets-stdin-2-content");

    /* Small buffer: fgets must stop at size-1 and leave the rest of
     * the line buffered. Runner types "stdin-line-three-long". */
    {
        char small[8];
        CHK(fgets(small, sizeof(small), stdin) != 0, "fgets-stdin-3");
        printf("  -> [%s]\n", small);
        CHK(strcmp(small, "stdin-l") == 0, "fgets-stdin-3-trunc");
        /* remainder of the same line, still buffered */
        CHK(fgets(buf, sizeof(buf), stdin) != 0, "fgets-stdin-3-rest");
        printf("  -> [%s]\n", buf);
    }

    /* getchar() must never return '\n': drain the remainder of line 3
     * (if any) is already consumed above; type one more char + Enter
     * via the runner and confirm only the char comes back. */
    {
        int c = getchar();
        printf("  getchar -> %d '%c'\n", c, c >= 32 && c < 127 ? c : '?');
        CHK(c == 'z', "getchar-no-newline");
        /* the synthetic '\n' for this line is filtered; the next
         * getchar blocks for a new line — runner sends 'y'+Enter. */
        c = getchar();
        CHK(c == 'y', "getchar-next-line");
    }

    printf("DONE-stdin fails=%d\n", fails);
    exit(fails);
}
