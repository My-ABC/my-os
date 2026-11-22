/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDLIB_H
#define _MINI_LIBC_STDLIB_H

#include <stddef.h>

/* mini-libc stdlib: process exit, heap allocation over SYS_MEMORY,
 * string<->number conversion, PRNG, and sorting/searching.
 *
 * The heap is the kernel's per-process user heap (user_alloc in
 * src/os/syscall.c): allocations are page-granular, alignment is at
 * least 4 bytes, and there is no sbrk/heap introspection. */

/* ---- process termination ---- */
void exit(int status) __attribute__((noreturn));   /* flushes stdio */
void _Exit(int status) __attribute__((noreturn)); /* immediate */
void abort(void) __attribute__((noreturn));

/* ---- heap ---- */
void *malloc(size_t size);
void *calloc(size_t nmemb, size_t size);
void *realloc(void *ptr, size_t size);
void  free(void *ptr);
/* power-of-two alignment (rounded up if needed); size need not be a
 * multiple of alignment — we honor the request leniently */
void *aligned_alloc(size_t alignment, size_t size);

/* register fn to run at exit() (LIFO, max 16); -1 when the table is
 * full. Handlers do not run on _Exit()/abort() or abnormal termination */
int atexit(void (*fn)(void));

/* ---- string -> number ---- */
int           atoi(const char *s);
long          atol(const char *s);
long          strtol(const char *s, char **endptr, int base);
unsigned long strtoul(const char *s, char **endptr, int base);
/* double atof/strtod are not provided (no float formatting yet) */

/* ---- PRNG (xorshift32) ---- */
int  rand(void);            /* [0, RAND_MAX] */
void srand(unsigned seed);
#define RAND_MAX 2147483647

/* ---- sorting / searching ---- */
void qsort(void *base, size_t nmemb, size_t size,
           int (*compar)(const void *, const void *));
void *bsearch(const void *key, const void *base, size_t nmemb,
              size_t size, int (*compar)(const void *, const void *));

/* ---- integer helpers ---- */
int  abs(int x);
long labs(long x);

typedef struct { int quot; int rem; } div_t;
typedef struct { long quot; long rem; } ldiv_t;
div_t  div(int numer, int denom);
ldiv_t ldiv(long numer, long denom);

/* environ is empty (crt0 passes envp=NULL): getenv always fails */
char *getenv(const char *name);

#endif /* _MINI_LIBC_STDLIB_H */
