/* SPDX-License-Identifier: MIT */
#include <fs.h>
#include "sys/syscall.h"

int fs_open(const char *path, int mode) {
    return syscall2(SYS_FS, FS_OPEN, (unsigned)path, (unsigned)mode);
}

int fs_close(int fd) {
    return syscall1(SYS_FS, FS_CLOSE, (unsigned)fd);
}

int fs_read(int fd, void *buf, unsigned size) {
    return syscall3(SYS_FS, FS_READ, (unsigned)fd, (unsigned)buf, size);
}

int fs_write(int fd, const void *buf, unsigned size) {
    return syscall3(SYS_FS, FS_WRITE, (unsigned)fd,
                    (unsigned)buf, size);
}

int fs_unlink(const char *path) {
    return syscall1(SYS_FS, FS_UNLINK, (unsigned)path);
}

int fs_mkdir(const char *path) {
    return syscall1(SYS_FS, FS_MKDIR, (unsigned)path);
}

int fs_rmdir(const char *path) {
    return syscall1(SYS_FS, FS_RMDIR, (unsigned)path);
}

int fs_stat(const char *path) {
    return syscall1(SYS_FS, FS_STAT, (unsigned)path);
}

int fs_getcwd(char *buf, unsigned size) {
    return syscall2(SYS_FS, FS_GETCWD, (unsigned)buf, size);
}

int fs_chdir(const char *path) {
    return syscall1(SYS_FS, FS_CHDIR, (unsigned)path);
}

int fs_rename(const char *old_path, const char *new_path) {
    return syscall2(SYS_FS, FS_RENAME, (unsigned)old_path,
                    (unsigned)new_path);
}

int fs_readdir(const char *path, void *buf, unsigned size) {
    return syscall3(SYS_FS, FS_READDIR, (unsigned)path, (unsigned)buf, size);
}
