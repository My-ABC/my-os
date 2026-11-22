/* SPDX-License-Identifier: MIT */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

static int side;
static void bump(void) { side++; }

int main(void) {
    side = 0;
    assert(1 == 1);
    assert(side == 0);
    bump();
    assert(side == 1);
    CHK(1, "assert-pass");

    /* assert with side-effect-free expr still evaluated once */
    int x = 5;
    assert(x++ == 5);
    CHK(x == 6, "assert-expr-eval");

    printf("DONE-assert fails=%d\n", fails);
    exit(fails);
}
