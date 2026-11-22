/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_FS_H
#define _MINI_LIBC_FS_H

#include <stddef.h>
#include <stdint.h>

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

#endif /* _MINI_LIBC_FS_H */
