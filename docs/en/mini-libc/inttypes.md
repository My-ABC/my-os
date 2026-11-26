# `inttypes.h` — printf/scanf length macros + imax family

LP32 i386: `int32_t` is `int`, `int64_t` is `long long`,
`intmax_t` is `int` (width of `int32_t`).

## PRI*/SCN* length macros

`PRI*` macros expand to the correct `printf` length modifier for a
given fixed-width type; `SCN*` do the same for `scanf`. Pattern:
`PRI{d,i,o,u,x,X}{8,16,32,64,LEAST*,FAST*,MAX,PTR}` and
`SCN{d,i,o,u,x}{...}`.

Examples: `PRId32` → `"d"`, `PRId64` → `"lld"`, `PRIx16` → `"hx"`,
`SCNu32` → `"u"`, `PRIuMAX` → `"u"`.

- **Parameters**: n/a (preprocessor macros).
- **Returns**: n/a — string literals for format strings.
- **Details**: Use as `printf("%" PRId64, val)`. **Caveat**: the
  stdio formatter currently handles `int`/`unsigned` width only —
  `ll` length macros expand correctly but `%lld` is not parsed yet.

## Types and functions

```c
typedef struct { intmax_t quot; intmax_t rem; } imaxdiv_t;
```

### `intmax_t imaxabs(intmax_t j)`

- **Parameters**: `j` — value.
- **Returns**: absolute value.
- **Details**: —

### `imaxdiv_t imaxdiv(intmax_t numer, intmax_t denom)`

- **Parameters**: `numer`, `denom` — dividend and divisor.
- **Returns**: `{quot, rem}` — quotient truncated toward zero,
  remainder takes the sign of `numer`. Division by zero is
  undefined.
- **Details**: —

### `intmax_t strtoimax(const char *nptr, char **endptr, int base)` / `uintmax_t strtoumax(const char *nptr, char **endptr, int base)`

- **Parameters**: `nptr` — input string; `endptr` — out-param for
  the first unparsed char (may be `NULL`); `base` — 2–36 or `0` to
  auto-detect.
- **Returns**: converted value; `errno = ERANGE` on overflow.
- **Details**: Same semantics as `strtol`/`strtoul`.
