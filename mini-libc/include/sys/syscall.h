/* SPDX-License-Identifier: MIT */
#ifndef _MINI_LIBC_SYSCALL_H
#define _MINI_LIBC_SYSCALL_H

/* MyOS syscall ABI: eax = class, ebx = sub-operation,
 * ecx/edx/esi = args, int 0x80, return value in eax. */

/* syscall classes */
#define SYS_IO       1
#define SYS_TIME     2
#define SYS_MEMORY   3
#define SYS_POWER    4
#define SYS_PROCESS  5
#define SYS_FS       6
#define SYS_SIG      7

/* SYS_IO sub-operations */
#define IO_WRITE       1
#define IO_GETCHAR     2
#define IO_READ        3
#define IO_CLEAR       4
#define IO_CURSOR_GET  5
#define IO_CURSOR_SET  6
#define IO_CONSOLE_INFO 7
#define IO_GETCHAR_NB  8

/* SYS_TIME sub-operations */
#define TIME_GET    1
#define TIME_TO     2
#define TIME_SLEEP  3

/* SYS_MEMORY sub-operations */
#define MEMORY_MALLOC   1
#define MEMORY_CALLOC   2
#define MEMORY_REALLOC  3
#define MEMORY_FREE     4

/* SYS_POWER sub-operations */
#define PWR_SHUTDOWN  1
#define PWR_REBOOT    2

/* SYS_PROCESS sub-operations */
#define PROC_GETPID  1
#define PROC_EXIT    2
#define PROC_YIELD   3
#define PROC_LIST    4
#define PROC_SET_FG  5
#define PROC_EXEC    6
#define PROC_RUN_DEMO     7
#define PROC_RUN_USERCODE 8

/* SYS_FS sub-operations */
#define FS_OPEN    1
#define FS_CLOSE   2
#define FS_READ    3
#define FS_WRITE   4
#define FS_UNLINK  5
#define FS_MKDIR   6
#define FS_RMDIR   7
#define FS_STAT    8
#define FS_GETCWD  9
#define FS_CHDIR   10
#define FS_RENAME  11
#define FS_READDIR 12

/* SYS_SIG sub-operations */
#define SIG_KILL    1
#define SIG_SIGNAL  2
#define SIG_RETURN  3

/* File open modes for FS_OPEN */
#define SYSFS_O_RDONLY 0
#define SYSFS_O_RDWR   1

/* PROC_LIST record: 12 bytes {pid, state, is_foreground} */
#define PROC_STATE_UNUSED  0
#define PROC_STATE_READY   1
#define PROC_STATE_RUNNING 2
#define PROC_STATE_DEAD    3

/* Raw wrappers implemented in syscall.S (cdecl). */
int syscall0(unsigned klass, unsigned op);
int syscall1(unsigned klass, unsigned op, unsigned a1);
int syscall2(unsigned klass, unsigned op, unsigned a1, unsigned a2);
int syscall3(unsigned klass, unsigned op,
             unsigned a1, unsigned a2, unsigned a3);

#endif /* _MINI_LIBC_SYSCALL_H */
