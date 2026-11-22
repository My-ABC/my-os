# 基础头文件

trivial freestanding 基础头文件，无 `.c` 实现。

| 头文件 | 内容 |
|---|---|
| `stdint.h` | `int8_t`–`int64_t`、`uint*_t`、`intptr_t`/`uintptr_t`、`intmax_t`/`uintmax_t`、least/fast 变体、`INT*_MAX/MIN` |
| `stddef.h` | `size_t`、`ptrdiff_t`、`NULL`、`offsetof` |
| `stdbool.h` | `bool`、`true`、`false` |
| `limits.h` | `CHAR_BIT`、`SCHAR_MIN/MAX`、`UCHAR_MAX`、`SHRT_*`、`INT_*`、`UINT_*`、`LONG_*`、`ULONG_*`、`LLONG_*`、`ULLONG_*` |
| `float.h` | `FLT_*`、`DBL_*`（宿主值） |
| `stdarg.h` | `va_list`、`va_start`、`va_arg`、`va_end`、`va_copy` |
| `stdnoreturn.h` | `noreturn` → `_Noreturn` |
