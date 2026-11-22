/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDIO_H
#define _MINI_LIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>

/* mini-libc stdio: formatted console/string I/O over the MyOS syscalls.
 * No FILE layer yet — all console output goes to SYS_IO/IO_WRITE
 * (VGA + serial) and console input comes from SYS_IO/IO_READ lines.
 *
 * printf family: %d %i %u %o %x %X %c %s %p %% with optional
 * flags (- 0 + space #), field width and precision (* allowed).
 * Length modifiers h/hh/l/ll are accepted and ignored (int assumed).
 *
 * scanf family: %d %i %u %o %x %X %c %s %p %% with optional
 * assignment suppression '*' and field width; length modifiers ignored. */

int printf(const char *fmt, ...);
int sprintf(char *buf, const char *fmt, ...);
int snprintf(char *buf, size_t n, const char *fmt, ...);
int puts(const char *s);
int putchar(int c);

int scanf(const char *fmt, ...);
int sscanf(const char *s, const char *fmt, ...);
int getchar(void);

#ifndef EOF
#define EOF (-1)
#endif

#endif /* _MINI_LIBC_STDIO_H */
