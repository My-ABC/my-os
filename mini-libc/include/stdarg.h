/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDARG_H
#define _MINI_LIBC_STDARG_H

/* Minimal freestanding stdarg built on GCC builtins. */

typedef __builtin_va_list va_list;

#define va_start __builtin_va_start
#define va_arg   __builtin_va_arg
#define va_end   __builtin_va_end
#define va_copy  __builtin_va_copy

#endif /* _MINI_LIBC_STDARG_H */
