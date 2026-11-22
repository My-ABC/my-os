/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_UNISTD_H
#define _MINI_LIBC_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

/* mini-libc unistd: process control, fd-level I/O, and path
 * operations over the MyOS syscall ABI.
 *
 * Only what the kernel actually supports is implemented. Functions
 * with no backing syscall (fork, getppid, lseek, ...) return -1 with
 * errno = ENOSYS. sleep()/usleep() live in <time.h>. */

_Noreturn void _exit(int status);

pid_t getpid(void);
/* always fails: the kernel tracks no parent pid */
pid_t getppid(void);
/* yield the CPU to another ready Ring 3 process */
int sched_yield(void);
/* always fails: no fork in the ABI */
pid_t fork(void);
/* load `path` (ELF32 on FAT32) and run it; argv/envp are accepted for
 * signature compatibility but ignored — the kernel ABI takes only the
 * path. Returns only if the exec fails. */
int execve(const char *path, char *const argv[], char *const envp[]);

/* fd-level I/O; identical semantics to fs_read/fs_write/fs_close */
ssize_t read(int fd, void *buf, size_t count);
ssize_t write(int fd, const void *buf, size_t count);
int close(int fd);

int unlink(const char *path);
int rmdir(const char *path);
int chdir(const char *path);
/* copies the cwd into buf; returns buf, or NULL on error/overflow */
char *getcwd(char *buf, size_t size);
/* existence check only (F_OK); any mode value is accepted */
int access(const char *path, int mode);

/* fd seeks are not supported by the kernel ABI */
off_t lseek(int fd, off_t offset, int whence);

#endif /* _MINI_LIBC_UNISTD_H */
