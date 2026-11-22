/* SPDX-License-Identifier: MIT */
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

static jmp_buf env;
static volatile int stage;

int main(void) {
    stage = 0;
    int r = setjmp(env);
    if (r == 0) {
        stage = 1;
        longjmp(env, 42);
        printf("FAIL unreachable\n"); fails++;
    }
    CHK(r == 42 && stage == 1, "longjmp-42");

    /* val=0 coerces to 1 */
    r = setjmp(env);
    if (r == 0) longjmp(env, 0);
    CHK(r == 1, "longjmp-zero-to-one");

    /* nested-ish: reuse same env twice */
    r = setjmp(env);
    if (r == 0) longjmp(env, 7);
    CHK(r == 7, "longjmp-again");

    printf("DONE-setjmp fails=%d\n", fails);
    exit(fails);
}
