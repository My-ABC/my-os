/* SPDX-License-Identifier: MIT */
#include <unistd.h>
#include <fs.h>
#include <errno.h>
#include <string.h>
#include "sys/syscall.h"

/* IO_WRITE takes a NUL-terminated string of at most this many bytes
 * (kernel IO_WRITE_MAX); larger console writes are chunked. */
#define CONSOLE_WRITE_MAX 4096U

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
    if (fd == 0 && !fs_owns_fd(0)) {
        /* Console stdin is line-buffered: IO_READ returns one whole
         * line (without '\n', NUL-terminated) of up to count-1 chars.
         * 0 means an empty line was submitted, NOT EOF. Do not mix
         * with fgets(stdin)/getchar — they share the same kernel
         * line buffer and would steal each other's input. */
        if (count == 0) return 0;
        if (buf == 0) {
            errno = EINVAL;
            return -1;
        }
        int rc = syscall2(SYS_IO, IO_READ, (unsigned)buf,
                          (unsigned)count);
        if (rc < 0) {
            errno = EIO;
            return -1;
        }
        return rc;
    }
    return (ssize_t)fs_read(fd, buf, (unsigned)count);
}

ssize_t write(int fd, const void *buf, size_t count) {
    if ((fd == 1 || fd == 2) && !fs_owns_fd(fd)) {
        /* Console output: IO_WRITE wants a NUL-terminated string, so
         * chunk into CONSOLE_WRITE_MAX pieces. Embedded NUL bytes
         * truncate a chunk early (the console is text-only); stderr
         * has no separate stream and shares the console. */
        if (count == 0) return 0;
        if (buf == 0) {
            errno = EINVAL;
            return -1;
        }
        const char *p = (const char *)buf;
        size_t done = 0;
        while (done < count) {
            char tmp[CONSOLE_WRITE_MAX + 1];
            size_t chunk = count - done;
            if (chunk > CONSOLE_WRITE_MAX) chunk = CONSOLE_WRITE_MAX;
            memcpy(tmp, p + done, chunk);
            tmp[chunk] = '\0';
            if (syscall1(SYS_IO, IO_WRITE, (unsigned)tmp) != 0) {
                errno = EIO;
                return done != 0 ? (ssize_t)done : -1;
            }
            done += chunk;
        }
        return (ssize_t)count;
    }
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
