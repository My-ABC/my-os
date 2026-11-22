/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDDEF_H
#define _MINI_LIBC_STDDEF_H

#ifndef _MINI_LIBC_STDINT_H
#define _MINI_LIBC_STDINT_H
#include <stdint.h>
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

typedef unsigned int size_t;
typedef int ptrdiff_t;

#define offsetof(type, member) __builtin_offsetof(type, member)

#endif /* _MINI_LIBC_STDDEF_H */
