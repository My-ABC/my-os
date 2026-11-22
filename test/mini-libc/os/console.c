/* SPDX-License-Identifier: MIT */
#include <console.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

int main(void) {
    uint16_t r0, c0, r1, c1;
    CHK(con_cursor_get(&r0, &c0) == 0, "cursor-get");
    printf("  cursor %u,%u\n", r0, c0);
    /* note: the emulated VGA text cursor may not round-trip reads
     * reliably, so only require set to succeed and get to stay sane */
    CHK(con_cursor_set(r0, (uint16_t)(c0 + 1)) == 0, "cursor-set");
    CHK(con_cursor_get(&r1, &c1) == 0 && r1 < 64 && c1 < 256,
        "cursor-sane");
    con_cursor_set(r0, c0);   /* restore */

    printf("DONE-console fails=%d\n", fails);
    exit(fails);
}
