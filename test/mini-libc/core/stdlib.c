/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static int fails;
#define CHK(c, n) do { if (c) printf("ok %s\n", n); \
    else { printf("FAIL %s\n", n); fails++; } } while (0)

static void hook1(void) { printf("ok atexit-1\n"); }
static void hook2(void) { printf("ok atexit-2\n"); }

static int cmp_int(const void *a, const void *b) {
    return *(const int *)a - *(const int *)b;
}

int main(void) {
    void *p = malloc(100);
    CHK(p != 0 && ((unsigned)p & 7) == 0, "malloc-8align");
    memset(p, 0x5A, 100);
    CHK(((unsigned char *)p)[0] == 0x5A, "malloc-write");

    int *c = calloc(4, sizeof(int));
    CHK(c != 0 && c[0] == 0 && c[3] == 0, "calloc-zero");

    char *r = malloc(8);
    strcpy(r, "hi");
    r = realloc(r, 64);
    CHK(r != 0 && strcmp(r, "hi") == 0, "realloc-keeps");
    free(r); free(c); free(p);

    void *a = aligned_alloc(64, 128);
    CHK(a != 0 && ((unsigned)a & 63) == 0, "aligned_alloc-64");
    errno = 0;
    CHK(aligned_alloc(48, 96) == 0 && errno == EINVAL, "aligned_alloc-nonpow2");
    errno = 0;
    CHK(aligned_alloc(64, 100) == 0 && errno == EINVAL, "aligned_alloc-nonsize");
    free(a);

    /* ksize / align overflow guards: huge sizes and alignments must
     * fail with ENOMEM instead of wrapping into a tiny allocation. */
    errno = 0;
    CHK(malloc(0xFFFFFF00u) == 0 && errno == ENOMEM, "malloc-huge-enomem");
    errno = 0;
    CHK(calloc(1, 0xFFFFFF00u) == 0 && errno == ENOMEM, "calloc-huge-enomem");
    errno = 0;
    CHK(aligned_alloc(0x200000u, 0x200000u) == 0 && errno == ENOMEM,
        "aligned_alloc-hugealign-enomem");

    CHK(atoi("  -42") == -42 && atol("010") == 10, "atoi-atol");
    char *e;
    CHK(strtol("0x1f", &e, 0) == 31 && *e == 0, "strtol-hex");
    CHK(strtol("010", 0, 0) == 8, "strtol-oct");
    CHK(strtoul("-1", 0, 10) == 0xFFFFFFFFUL, "strtoul-neg");
    errno = 0;
    strtol("99999999999999999999", 0, 10);
    CHK(errno == ERANGE, "strtol-range");

    srand(1234);
    int r1 = rand(), r2 = rand();
    CHK(r1 == r2 ? 0 : 1, "rand-varies"); /* same seed -> same seq; just check range */
    CHK(r1 >= 0 && r1 <= RAND_MAX, "rand-range");

    int arr[7] = {5, 1, 4, 1, 3, 9, 2};
    qsort(arr, 7, sizeof(int), cmp_int);
    CHK(arr[0]==1 && arr[1]==1 && arr[2]==2 && arr[3]==3 && arr[4]==4 &&
        arr[5]==5 && arr[6]==9, "qsort");
    int key = 4;
    CHK(bsearch(&key, arr, 7, sizeof(int), cmp_int) != 0, "bsearch-hit");
    key = 8;
    CHK(bsearch(&key, arr, 7, sizeof(int), cmp_int) == 0, "bsearch-miss");

    CHK(abs(-5) == 5 && labs(-9L) == 9L, "abs-labs");
    div_t dv = div(-7, 2);
    CHK(dv.quot == -3 && dv.rem == -1, "div");
    ldiv_t ld = ldiv(1000000L, 7L);
    CHK(ld.quot == 142857L && ld.rem == 1L, "ldiv");

    CHK(getenv("HOME") == 0, "getenv-empty");

    atexit(hook1);
    atexit(hook2);   /* LIFO: hook2 must print before hook1 */

    printf("DONE-stdlib fails=%d\n", fails);
    exit(fails);     /* hooks print here, after DONE line */
}
