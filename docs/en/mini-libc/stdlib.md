# `stdlib.h` — process exit, heap, conversion, PRNG, sorting

Covers process termination, heap allocation over `SYS_MEMORY`,
string↔number conversion, a PRNG, sorting/searching, and misc
helpers.

## Process termination

### `_Noreturn void exit(int status)`

- **Parameters**: `status` — exit status (conventionally `0` =
  success).
- **Returns**: never returns.
- **Details**: Runs `atexit()` handlers in LIFO order, flushes
  stdio, then issues `PROC_EXIT`. Call this from `main` instead of
  returning.

### `_Noreturn void _Exit(int status)`

- **Parameters**: `status` — exit status.
- **Returns**: never returns.
- **Details**: Immediate `PROC_EXIT`; skips `atexit` handlers and
  stdio flushing. `_exit()` in `unistd.h` is the same operation.

### `_Noreturn void abort(void)`

- **Parameters**: none.
- **Returns**: never returns.
- **Details**: Terminates via `PROC_EXIT` without any cleanup.

## Heap allocation

The heap is the kernel's per-process user heap behind a 16-byte
mini-libc header. `malloc` guarantees 8-byte alignment;
`aligned_alloc` honors larger power-of-two alignments. There is no
`sbrk` or heap introspection.

### `void *malloc(size_t size)`

- **Parameters**: `size` — bytes to allocate.
- **Returns**: block pointer (8-byte aligned), or `NULL` with
  `errno = ENOMEM`.
- **Details**: —

### `void *calloc(size_t nmemb, size_t size)`

- **Parameters**: `nmemb` — element count; `size` — element size.
- **Returns**: zeroed block pointer, or `NULL` on failure/overflow.
- **Details**: `nmemb * size` with overflow checking.

### `void *realloc(void *ptr, size_t size)`

- **Parameters**: `ptr` — existing block or `NULL`; `size` — new
  size.
- **Returns**: (possibly moved) block pointer, or `NULL` on failure
  with the original block untouched.
- **Details**: `size == 0` frees `ptr` and returns `NULL`; `ptr ==
  NULL` behaves like `malloc`.

### `void free(void *ptr)`

- **Parameters**: `ptr` — block from `malloc`/`calloc`/`realloc`,
  or `NULL`.
- **Returns**: nothing.
- **Details**: `NULL` is a no-op; freeing foreign pointers is
  undefined.

### `void *aligned_alloc(size_t alignment, size_t size)`

- **Parameters**: `alignment` — power of two and a multiple of
  `sizeof(void*)`; `size` — nonzero multiple of `alignment`.
- **Returns**: aligned pointer, or `NULL` (`errno = EINVAL` on
  contract violation, `ENOMEM` on exhaustion).
- **Details**: C11 semantics — stricter than POSIX `memalign`.

### `int atexit(void (*fn)(void))`

- **Parameters**: `fn` — function to run at `exit()`.
- **Returns**: `0` on success, `-1` when the table (max 16) is full.
- **Details**: Handlers run LIFO and only on `exit()` — not on
  `_Exit()`/`abort()` or abnormal termination.

## String → number conversion

### `int atoi(const char *s)` / `long atol(const char *s)`

- **Parameters**: `s` — decimal string (leading whitespace and sign
  allowed).
- **Returns**: converted value; `0` when no digits. Overflow is
  undefined (no `errno`).
- **Details**: Equivalent to `strtol(s, NULL, 10)`.

### `long strtol(const char *s, char **endptr, int base)`

- **Parameters**: `s` — input; `endptr` — out-param for the first
  unparsed char (may be `NULL`); `base` — 2–36, or `0` to
  auto-detect (`0x` hex, `0` octal, else decimal).
- **Returns**: converted `long`; `errno = ERANGE` on overflow.
- **Details**: Optional sign and base prefix handled.

### `unsigned long strtoul(const char *s, char **endptr, int base)`

- **Parameters/Returns/Details**: same as `strtol`, unsigned result;
  a leading `-` negates modulo `ULONG_MAX + 1`.

## PRNG

### `int rand(void)` / `void srand(unsigned seed)`

- **Parameters**: `seed` — PRNG seed.
- **Returns**: `rand` returns `[0, RAND_MAX]`; `RAND_MAX` is
  `0x7FFFFFFF`.
- **Details**: xorshift32 generator with a fixed default seed until
  `srand` is called.

## Sorting and searching

### `void qsort(void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))`

- **Parameters**: `base` — array base; `nmemb` — element count;
  `size` — element size in bytes; `compar` — comparator returning
  `<0`/`0`/`>0`.
- **Returns**: nothing; sorts in place.
- **Details**: Unstable; `nmemb == 0` or `size == 0` is a no-op.

### `void *bsearch(const void *key, const void *base, size_t nmemb, size_t size, int (*compar)(const void *, const void *))`

- **Parameters**: `key` — element to find; `base`/`nmemb`/`size` —
  sorted array; `compar` — same convention as `qsort`.
- **Returns**: pointer to the matching element, or `NULL`.
- **Details**: Array must be sorted with the same comparator.

## Integer helpers

### `int abs(int x)` / `long labs(long x)`

- **Parameters**: `x` — value.
- **Returns**: absolute value; `INT_MIN`/`LONG_MIN` result is
  undefined.

### `div_t div(int numer, int denom)` / `ldiv_t ldiv(long numer, long denom)`

- **Parameters**: `numer`, `denom` — dividend and divisor.
- **Returns**: `{quot, rem}` struct — quotient truncated toward
  zero, remainder takes the sign of `numer`. Division by zero is
  undefined.

### `char *getenv(const char *name)`

- **Parameters**: `name` — variable name.
- **Returns**: always `NULL`.
- **Details**: `crt0` passes `envp=NULL`; the environment is empty.
