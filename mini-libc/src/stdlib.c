/* SPDX-License-Identifier: MIT */
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include "sys/syscall.h"

static void run_atexit_handlers(void);

/* ---- process termination ---- */

void exit(int status) {
    run_atexit_handlers();  /* LIFO */
    fflush(0);              /* flush the shared console buffer */
    _Exit(status);
}

void _Exit(int status) {
    (void)status;           /* exit codes are not recorded yet */
    syscall1(SYS_PROCESS, PROC_EXIT, 0);
    for (;;) { }
}

void abort(void) {
    _Exit(134);             /* conventional SIGABRT exit status */
}

/* ---- heap ----
 *
 * All mini-libc allocations carry a 16-byte header just below the
 * user pointer:
 *   p-16  kernel base pointer (what SYS_MEMORY handed us)
 *   p-12  magic
 *   p-8   alignment (power of two, >= 4)
 *   p-4   requested size
 * The kernel allocator only guarantees ~4-byte alignment (its block
 * header is 20 bytes into a page), so we over-allocate and align the
 * user pointer ourselves. malloc() therefore returns 8-byte-aligned
 * memory; aligned_alloc() honors larger power-of-two alignments.
 * free()/realloc() verify the magic before touching the kernel. */

#define MH_MAGIC 0x4D484452u   /* "MHDR" */
#define MH_HDR   16u

static void *xalloc(size_t size, size_t align) {
    if (align < sizeof(void *)) align = sizeof(void *);
    /* round non-power-of-two alignments up to the next power of two */
    size_t a = 8;
    while (a < align) a <<= 1;
    align = a;

    size_t ksize = size + MH_HDR + align + 8;
    char *k = (char *)syscall1(SYS_MEMORY, MEMORY_MALLOC, (unsigned)ksize);
    if (k == 0) { errno = ENOMEM; return 0; }

    uintptr_t u = ((uintptr_t)k + MH_HDR + align - 1) & ~(uintptr_t)(align - 1);
    ((void **)(u - MH_HDR))[0] = k;                    /* base */
    ((uint32_t *)(u - MH_HDR))[1] = MH_MAGIC;          /* magic */
    ((uint32_t *)(u - MH_HDR))[2] = (uint32_t)align;   /* align */
    ((uint32_t *)(u - MH_HDR))[3] = (uint32_t)size;    /* size */
    return (void *)u;
}

static int hdr_of(void *p, void **base, uint32_t *align, uint32_t *size) {
    if (p == 0) return 0;
    uint32_t *h = (uint32_t *)((char *)p - MH_HDR);
    if (h[1] != MH_MAGIC) return 0;
    *base = (void *)h[0];
    *align = h[2];
    *size = h[3];
    return 1;
}

void *malloc(size_t size) {
    if (size == 0) return 0;
    return xalloc(size, 8);
}

void *calloc(size_t nmemb, size_t size) {
    if (nmemb != 0 && size > (size_t)-1 / nmemb) {
        errno = ENOMEM;
        return 0;
    }
    size_t total = nmemb * size;
    void *p = xalloc(total, 8);
    if (p) memset(p, 0, total);
    return p;
}

void *aligned_alloc(size_t alignment, size_t size) {
    if (size == 0 || alignment == 0) { errno = EINVAL; return 0; }
    return xalloc(size, alignment);
}

void *realloc(void *ptr, size_t size) {
    if (ptr == 0) return malloc(size);
    if (size == 0) { free(ptr); return 0; }

    void *base;
    uint32_t align, oldsize;
    if (!hdr_of(ptr, &base, &align, &oldsize)) {
        errno = EINVAL;         /* not a mini-libc allocation */
        return 0;
    }

    /* kernel realloc can't be used across the header indirection:
     * it frees the old block, and the payload offset changes with
     * the new alignment padding. Allocate, copy, free instead. */
    void *np = xalloc(size, align);
    if (np == 0) return 0;              /* old block left intact */
    memcpy(np, ptr, oldsize < size ? oldsize : size);
    free(ptr);
    return np;
}

void free(void *ptr) {
    if (ptr == 0) return;
    void *base;
    uint32_t align, size;
    if (!hdr_of(ptr, &base, &align, &size)) return;  /* not ours: ignore */
    syscall1(SYS_MEMORY, MEMORY_FREE, (unsigned)base);
}

/* ---- atexit handlers (LIFO, run by exit()) ---- */

#define MAX_ATEXIT 16
static void (*atexit_fns[MAX_ATEXIT])(void);
static int atexit_count;

int atexit(void (*fn)(void)) {
    if (fn == 0 || atexit_count >= MAX_ATEXIT) {
        errno = ENOMEM;
        return -1;
    }
    atexit_fns[atexit_count++] = fn;
    return 0;
}

static void run_atexit_handlers(void) {
    while (atexit_count > 0)
        atexit_fns[--atexit_count]();
}

/* ---- string -> number ---- */

static int scan_base_prefix(const char **sp, int base) {
    const char *s = *sp;
    if (base == 0 || base == 16) {
        if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X') &&
            isxdigit((unsigned char)s[2])) {
            *sp = s + 2;
            return 16;
        }
    }
    if (base == 0 && s[0] == '0' && isdigit((unsigned char)s[1]))
        return 8;
    return base == 0 ? 10 : base;
}

long strtol(const char *s, char **endptr, int base) {
    const char *orig = s;
    if (s == 0) { if (endptr) *endptr = 0; return 0; }
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') neg = (*s++ == '-');
    base = scan_base_prefix(&s, base);
    if (base < 2 || base > 36) { if (endptr) *endptr = (char *)orig; return 0; }

    unsigned long acc = 0;
    int any = 0, of = 0;
    const unsigned long lim = (unsigned long)LONG_MAX + (neg ? 1UL : 0UL);
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        any = 1;
        if (acc > (lim - (unsigned long)d) / (unsigned)base)
            of = 1;
        else
            acc = acc * (unsigned)base + (unsigned long)d;
    }
    if (!any) { if (endptr) *endptr = (char *)orig; return 0; }
    if (of) {
        errno = ERANGE;
        return neg ? LONG_MIN : LONG_MAX;
    }
    if (endptr) *endptr = (char *)s;
    return neg ? -(long)acc : (long)acc;
}

unsigned long strtoul(const char *s, char **endptr, int base) {
    const char *orig = s;
    if (s == 0) { if (endptr) *endptr = 0; return 0; }
    while (isspace((unsigned char)*s)) s++;
    int neg = 0;
    if (*s == '+' || *s == '-') neg = (*s++ == '-');
    base = scan_base_prefix(&s, base);
    if (base < 2 || base > 36) { if (endptr) *endptr = (char *)orig; return 0; }

    unsigned long acc = 0;
    int any = 0, of = 0;
    for (;; s++) {
        int d;
        if (*s >= '0' && *s <= '9') d = *s - '0';
        else if (*s >= 'a' && *s <= 'z') d = *s - 'a' + 10;
        else if (*s >= 'A' && *s <= 'Z') d = *s - 'A' + 10;
        else break;
        if (d >= base) break;
        any = 1;
        if (acc > (0xFFFFFFFFUL - (unsigned long)d) / (unsigned)base)
            of = 1;
        else
            acc = acc * (unsigned)base + (unsigned long)d;
    }
    if (!any) { if (endptr) *endptr = (char *)orig; return 0; }
    if (of) { errno = ERANGE; return 0xFFFFFFFFUL; }
    if (endptr) *endptr = (char *)s;
    if (neg) acc = 0UL - acc;   /* wrap: strtoul accepts a leading '-' */
    return acc;
}

int atoi(const char *s) { return (int)strtol(s, 0, 10); }
long atol(const char *s) { return strtol(s, 0, 10); }

/* ---- PRNG: xorshift32 ---- */

static unsigned rng_state = 1;

void srand(unsigned seed) {
    rng_state = seed ? seed : 1;
}

int rand(void) {
    unsigned x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return (int)(x & 0x7FFFFFFFU);   /* [0, RAND_MAX] */
}

/* ---- qsort / bsearch ---- */

static void swap_bytes(char *a, char *b, size_t n) {
    while (n--) { char t = *a; *a++ = *b; *b++ = t; }
}

static void qs_rec(char *base, size_t n, size_t size,
                   int (*compar)(const void *, const void *)) {
    while (n > 1) {
        /* median-of-three pivot moved to base[0] */
        char *mid = base + (n / 2) * size;
        char *last = base + (n - 1) * size;
        if (compar(base, mid) > 0) swap_bytes(base, mid, size);
        if (compar(base, last) > 0) swap_bytes(base, last, size);
        if (compar(mid, last) > 0) swap_bytes(mid, last, size);
        swap_bytes(base, mid, size);           /* pivot = median */

        char *pivot = base;
        size_t store = 1;
        for (size_t i = 1; i < n; i++) {
            char *cur = base + i * size;
            if (compar(cur, pivot) < 0) {
                swap_bytes(cur, base + store * size, size);
                store++;
            }
        }
        swap_bytes(base, base + (store - 1) * size, size);

        /* recurse into the smaller side, loop on the larger */
        size_t left = store - 1;
        if (left < n - store) {
            qs_rec(base, left, size, compar);
            base += store * size;
            n -= store;
        } else {
            qs_rec(base + store * size, n - store, size, compar);
            n = left;
        }
    }
}

void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *)) {
    if (base == 0 || compar == 0 || size == 0 || nmemb < 2) return;
    qs_rec((char *)base, nmemb, size, compar);
}

void *bsearch(const void *key, const void *base, size_t nmemb,
              size_t size, int (*compar)(const void *, const void *)) {
    if (key == 0 || base == 0 || compar == 0 || size == 0) return 0;
    const char *lo = (const char *)base;
    size_t n = nmemb;
    while (n > 0) {
        size_t half = n / 2;
        const char *mid = lo + half * size;
        int c = compar(key, mid);
        if (c == 0) return (void *)mid;
        if (c < 0) { n = half; }
        else { lo = mid + size; n -= half + 1; }
    }
    return 0;
}

/* ---- integer helpers ---- */

int abs(int x) { return x < 0 ? -x : x; }
long labs(long x) { return x < 0 ? -x : x; }

div_t div(int numer, int denom) {
    div_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

ldiv_t ldiv(long numer, long denom) {
    ldiv_t r;
    r.quot = numer / denom;
    r.rem = numer % denom;
    return r;
}

char *getenv(const char *name) {
    (void)name;    /* crt0 passes envp=NULL: the environment is empty */
    return 0;
}
