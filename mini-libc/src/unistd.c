/* SPDX-License-Identifier: MIT */
#include <unistd.h>
#include <fs.h>
#include <errno.h>
#include "sys/syscall.h"

void _exit(int status) {
    (void)status;   /* the ABI exit code is not recorded yet */
    syscall1(SYS_PROCESS, PROC_EXIT, 0);
    for (;;) { }    /* PROC_EXIT must not return; spin if it does */
}

pid_t getpid(void) {
    return (pid_t)syscall0(SYS_PROCESS, PROC_GETPID);
}

pid_t getppid(void) {
    errno = ENOSYS;
    return -1;
}

int sched_yield(void) {
    return syscall0(SYS_PROCESS, PROC_YIELD);
}

pid_t fork(void) {
    errno = ENOSYS;
    return -1;
}

int execve(const char *path, char *const argv[], char *const envp[]) {
    (void)argv;
    (void)envp;   /* kernel ABI takes only the path */
    if (path == 0) {
        errno = EINVAL;
        return -1;
    }
    return syscall1(SYS_PROCESS, PROC_EXEC, (unsigned)path);
}

ssize_t read(int fd, void *buf, size_t count) {
    return (ssize_t)fs_read(fd, buf, (unsigned)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    return (ssize_t)fs_write(fd, (const void *)buf, (unsigned)count);
}

int close(int fd) {
    return fs_close(fd);
}

int unlink(const char *path) {
    return fs_unlink(path);
}

int rmdir(const char *path) {
    return fs_rmdir(path);
}

int chdir(const char *path) {
    return fs_chdir(path);
}

char *getcwd(char *buf, size_t size) {
    if (buf == 0 || size == 0) {
        errno = EINVAL;
        return 0;
    }
    return fs_getcwd(buf, (unsigned)size) >= 0 ? buf : 0;
}

int access(const char *path, int mode) {
    (void)mode;   /* only existence (F_OK) is meaningful here */
    int st = fs_stat(path);
    if (st == -2 || st >= 0) return 0;   /* -2 = directory: exists */
    errno = ENOENT;
    return -1;
}

off_t lseek(int fd, off_t offset, int whence) {
    (void)fd;
    (void)offset;
    (void)whence;
    errno = ENOSYS;
    return -1;
}
