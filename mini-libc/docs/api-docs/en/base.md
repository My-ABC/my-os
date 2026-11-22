# Base headers

Trivial freestanding base headers; no `.c` implementation.

| Header | Contents |
|---|---|
| `stdint.h` | `int8_t`–`int64_t`, `uint*_t`, `intptr_t`/`uintptr_t`, `intmax_t`/`uintmax_t`, least/fast variants, `INT*_MAX/MIN` |
| `stddef.h` | `size_t`, `ptrdiff_t`, `NULL`, `offsetof` |
| `stdbool.h` | `bool`, `true`, `false` |
| `limits.h` | `CHAR_BIT`, `SCHAR_MIN/MAX`, `UCHAR_MAX`, `SHRT_*`, `INT_*`, `UINT_*`, `LONG_*`, `ULONG_*`, `LLONG_*`, `ULLONG_*` |
| `float.h` | `FLT_*`, `DBL_*` (host values) |
| `stdarg.h` | `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy` |
| `stdnoreturn.h` | `noreturn` → `_Noreturn` |
