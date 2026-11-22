/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_STDIO_H
#define _MINI_LIBC_STDIO_H

#include <stddef.h>
#include <stdarg.h>
#include <fs.h>

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
/* Read one console line into buf (size includes NUL); returns the
 * line length or -1. Uses SYS_IO/IO_READ directly, so it must not be
 * mixed with getchar/scanf on the same stream. */
int getline(char *buf, int size);

/* ---- FILE streams ----
 * stdin/stdout are console streams (stderr deferred). fopen() returns
 * unbuffered FILE* streams over the fs.h FAT32 fd layer. */
extern FILE *stdin;
extern FILE *stdout;

FILE   *fopen(const char *path, const char *mode);
int     fclose(FILE *f);
size_t  fread(void *buf, size_t size, size_t count, FILE *f);
size_t  fwrite(const void *buf, size_t size, size_t count, FILE *f);
int     fgetc(FILE *f);
int     fputc(int c, FILE *f);
int     fputs(const char *s, FILE *f);
char   *fgets(char *buf, int size, FILE *f);
int     fprintf(FILE *f, const char *fmt, ...);
int     fscanf(FILE *f, const char *fmt, ...);
int     feof(FILE *f);
int     ferror(FILE *f);
void    clearerr(FILE *f);
int     fflush(FILE *f);

#endif /* _MINI_LIBC_STDIO_H */
