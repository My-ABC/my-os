/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_SYS_TYPES_H
#define _MINI_LIBC_SYS_TYPES_H

#include <stddef.h>
#include <stdint.h>

/* Minimal POSIX-ish type aliases for unistd.h. */
typedef int pid_t;
typedef int ssize_t;          /* i386: ssize_t is 32-bit like size_t */
typedef int32_t off_t;        /* signed file offset */

#endif /* _MINI_LIBC_SYS_TYPES_H */
