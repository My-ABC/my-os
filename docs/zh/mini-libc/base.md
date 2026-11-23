# 基础头文件

无 `.c` 实现的 freestanding C 标准头文件——只有类型、常量上限
和编译器宏。

| 头文件 | 内容 |
|---|---|
| `stdint.h` | `int8_t`–`int64_t`、`uint*_t`、`intptr_t`/`uintptr_t`、`intmax_t`/`uintmax_t`、least/fast 变体、`INT*_MAX/MIN`、`UINT*_MAX` |
| `stddef.h` | `size_t`、`ptrdiff_t`、`NULL`、`offsetof` |
| `stdbool.h` | `bool`、`true`、`false`（C++ 下为空） |
| `limits.h` | `CHAR_BIT`、`CHAR/SCHAR/UCHAR/SHRT/INT/LONG/LLONG` 最值、`MB_LEN_MAX` |
| `float.h` | IEEE-754 `FLT_*`/`DBL_*` 常量 |
| `stdarg.h` | `va_list`、`va_start`、`va_arg`、`va_end`、`va_copy`（GCC 内建） |
| `stdnoreturn.h` | `noreturn` → `_Noreturn` |

说明：

- LP32 i386 布局：`int`/`long`/指针为 32 位；`long long` 为
  64 位。
- `stddef.h` 的 `offsetof` 由 `__builtin_offsetof` 实现。
- `sys/types.h`（类 POSIX 别名：`pid_t`、`ssize_t`、`off_t`）随
  `unistd.h`/`process.h` 文档说明。
