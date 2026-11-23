# Base headers

Freestanding C standard headers with no `.c` implementation —
types, limits, and compiler macros only.

| Header | Contents |
|---|---|
| `stdint.h` | `int8_t`–`int64_t`, `uint*_t`, `intptr_t`/`uintptr_t`, `intmax_t`/`uintmax_t`, least/fast variants, `INT*_MAX/MIN`, `UINT*_MAX` |
| `stddef.h` | `size_t`, `ptrdiff_t`, `NULL`, `offsetof` |
| `stdbool.h` | `bool`, `true`, `false` (empty under C++) |
| `limits.h` | `CHAR_BIT`, `CHAR/SCHAR/UCHAR/SHRT/INT/LONG/LLONG` min/max, `MB_LEN_MAX` |
| `float.h` | IEEE-754 `FLT_*`/`DBL_*` constants |
| `stdarg.h` | `va_list`, `va_start`, `va_arg`, `va_end`, `va_copy` (GCC builtins) |
| `stdnoreturn.h` | `noreturn` → `_Noreturn` |

Notes:

- LP32 i386 layout: `int`/`long`/pointers are 32-bit; `long long`
  is 64-bit.
- `stddef.h` also defines `offsetof` via `__builtin_offsetof`.
- `sys/types.h` (POSIX-ish aliases: `pid_t`, `ssize_t`, `off_t`) is
  documented with `unistd.h`/`process.h`.
