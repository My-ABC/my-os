/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDIO_H
#define _MINI_LIBC_STDIO_H

/* Minimal stdio: formatted output and input over the console syscalls.
 * printf supports %d %i %u %x %X %s %c %p %% ;
 * scanf supports %d %i %u %x %s %c and literal characters. */

int printf(const char *fmt, ...);
int scanf(const char *fmt, ...);

#endif /* _MINI_LIBC_STDIO_H */
