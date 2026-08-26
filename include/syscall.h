#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "stdint.h"

// 系统调用号
#define SYS_WRITE    1   // 向 VGA 输出字符串
#define SYS_READ     2   // 从键盘读取字符串（带回显）
#define SYS_GETCHAR  3   // 从键盘读取单个字符（阻塞）
#define SYS_CLEAR    4   // 清屏
#define SYS_REBOOT   5   // 重启系统
#define SYS_TIME     6   // 获取 RTC 时间
#define SYS_MEMORY   7   // 用户内存操作
#define SYS_POWER    8   // 电源操作

#define MEMORY_MALLOC  1
#define MEMORY_CALLOC  2
#define MEMORY_REALLOC 3
#define MEMORY_FREE    4

#define POWER_SHUTDOWN 1
#define POWER_REBOOT   2

uint32_t sys_call(int call_number, uint32_t arg1, uint32_t arg2, uint32_t arg3);
void *sys_malloc(uint32_t size);
void *sys_calloc(uint32_t nmemb, uint32_t size);
void *sys_realloc(void *ptr, uint32_t size);
void sys_free(void *ptr);
void sys_shutdown(void);
void sys_reboot(void);
#endif