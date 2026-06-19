#ifndef _SYSCALLS_H
#define _SYSCALLS_H

// 系统调用号
#define SYS_PRINT   1
#define SYS_GETPID  4

// 用户调用的函数
int syscall_print(const char* str);
int syscall_getpid(void);

#endif