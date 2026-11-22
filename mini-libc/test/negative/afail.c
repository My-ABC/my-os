/* SPDX-License-Identifier: MIT */
/* Negative test: this ELF MUST abort via assert failure. The runner
 * checks for "Assertion failed" on serial and that the shell survives. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

int main(void) {
    printf("afail-start\n");
    int bad = -3;
    assert(bad > 0);
    printf("afail-unreachable\n");
    return 0;
}
