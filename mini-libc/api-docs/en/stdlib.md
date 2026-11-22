# `stdlib.h`

## Heap
`void *malloc(size_t)` — 8-byte aligned via a 16-byte header
layer; `malloc(0)` → `NULL`.
`void *calloc(n,size)` `void *realloc(p,size)` (alloc-copy-free;
preserves data on failure) `void free(p)`.
`void *aligned_alloc(size_t alignment, size_t size)` — C11 rules:
alignment must be a power of two and a multiple of
`sizeof(void*)`, size a nonzero multiple of alignment; violations
→ `NULL`/`errno=EINVAL`.
`int atexit(void (*fn)(void))` — 16 slots, LIFO, run by `exit()`.

## Exit
`_Noreturn void exit(int)` (atexit + fflush + PROC_EXIT),
`_Noreturn void _Exit(int)` (immediate),
`_Noreturn void abort(void)` (exit 134).

## Conversion
`int atoi(const char*)` `long atol(const char*)`
`long strtol(nptr,&end,base)` `unsigned long strtoul(...)`
base 0 auto-detects `0x`/leading-0; overflow → `ERANGE`/`LONG_MAX`.
No `atof`/`strtod` (no float formatting).

## Random
`int rand(void)` `void srand(unsigned)` — xorshift32,
`RAND_MAX = 0x7FFFFFFF`.

## Sort/search
`void qsort(base,n,size,cmp)` `void *bsearch(key,base,n,size,cmp)`.

## Integer/misc
`int abs(int)` `long labs(long)` `div_t div(int,int)`
`ldiv_t ldiv(long,long)` (`{quot,rem}`).
`char *getenv(const char*)` — always `NULL` (no environment).
