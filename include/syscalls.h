#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "stdint.h"

// 系统调用号
#define SYS_PRINT   1
#define SYS_READ    2
#define SYS_EXIT    3
#define SYS_GETPID  4
#define SYS_SLEEP   5
#define SYS_MALLOC  6
#define SYS_FREE    7

// 系统调用入口
void syscall_handler(void);

// 系统调用函数
int syscall(int num, ...);

#endif