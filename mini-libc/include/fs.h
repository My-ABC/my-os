/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_FS_H
#define _MINI_LIBC_FS_H

#include <stddef.h>
#include <stdint.h>

#ifndef EOF
#define EOF (-1)
#endif

/* mini-libc filesystem API over SYS_FS.
 *
 * Thin wrappers around the kernel's FAT32 fd table. File descriptors
 * are small non-negative integers owned by the kernel; there is no
 * per-process fd table yet, so fds are global across Ring 3 tasks.
 *
 * fs_stat() encodes its result in the return value:
 *   >= 0  regular file size in bytes
 *   -2    path is a directory
 *   < 0   error / not found
 */

/* open modes for fs_open (kernel SYSFS_O_*) */
#define FS_O_RDONLY 0
#define FS_O_RDWR   1

int fs_open(const char *path, int mode);
int fs_close(int fd);
/* read/write up to `size` bytes; returns bytes transferred or <0 */
int fs_read(int fd, void *buf, unsigned size);
int fs_write(int fd, const void *buf, unsigned size);
int fs_unlink(const char *path);
int fs_mkdir(const char *path);
int fs_rmdir(const char *path);
int fs_stat(const char *path);
/* current working directory into buf (NUL-terminated); <0 on error */
int fs_getcwd(char *buf, unsigned size);
int fs_chdir(const char *path);
int fs_rename(const char *old_path, const char *new_path);
/* directory listing into buf: newline-separated records, first byte
 * of each record is 'f' or 'd'; returns byte count or <0 */
int fs_readdir(const char *path, void *buf, unsigned size);

/* ---- FILE: buffered-unaware stream over an fs fd ----
 *
 * Minimal FILE for stdio to build on: wraps one kernel fd plus eof/err
 * flags. No buffering yet — every call is a syscall. `stdin`/`stdout`/
 * `stderr` are not provided here (console streams live in stdio.h). */
typedef struct {
    int fd;         /* kernel fd, -1 when closed */
    int eof;        /* set after a short/EOF read */
    int err;        /* set on kernel errors */
} FILE;

FILE *fs_fopen(const char *path, const char *mode);
int   fs_fclose(FILE *f);
size_t fs_fread(void *buf, size_t size, size_t count, FILE *f);
size_t fs_fwrite(const void *buf, size_t size, size_t count, FILE *f);
int   fs_fgetc(FILE *f);
int   fs_fputc(int c, FILE *f);
int   fs_feof(FILE *f);
int   fs_ferror(FILE *f);
void  fs_clearerr(FILE *f);

#endif /* _MINI_LIBC_FS_H */
