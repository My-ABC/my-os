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
#define SYS_GETPID   9   // 获取当前 Ring 3 进程 ID
#define SYS_EXIT     10  // 退出当前 Ring 3 进程
#define SYS_FOPEN    11  // 打开 FAT32 文件，返回 fd
#define SYS_FCLOSE   12  // 关闭文件描述符
#define SYS_FREAD    13  // 从文件描述符读取
#define SYS_FWRITE   14  // 向文件描述符写入
#define SYS_FUNLINK  15  // 删除 FAT32 文件
#define SYS_FMKDIR   16  // 创建 FAT32 目录
#define SYS_FRMDIR   17  // 删除空 FAT32 目录
#define SYS_FSTAT    18  // 获取路径的文件大小/类型
#define SYS_GETCWD   19  // 获取当前工作目录
#define SYS_CHDIR    20  // 切换当前工作目录
#define SYS_YIELD    21  // 主动让出 CPU
#define SYS_KILL     22  // 向进程发送信号 (ebx=pid, ecx=sig)
#define SYS_SIGNAL   23  // 注册信号处理器 (ebx=sig, ecx=handler)
#define SYS_SIGRETURN 24 // 信号处理器返回，恢复被中断的上下文（由 restorer 调用）

#define MEMORY_MALLOC  1
#define MEMORY_CALLOC  2
#define MEMORY_REALLOC 3
#define MEMORY_FREE    4

#define POWER_SHUTDOWN 1
#define POWER_REBOOT   2

#define SYSFS_O_RDONLY 0
#define SYSFS_O_RDWR   1

// 用户堆虚拟地址范围（供页错误按需映射做范围判定）
#define USER_HEAP_START 0x01000000U
#define USER_HEAP_END   0x40000000U
uint32_t sys_user_heap_contains(uint32_t addr);

uint32_t sys_call(int call_number, uint32_t arg1, uint32_t arg2, uint32_t arg3);
void *sys_malloc(uint32_t size);
void *sys_calloc(uint32_t nmemb, uint32_t size);
void *sys_realloc(void *ptr, uint32_t size);
void sys_free(void *ptr);
void sys_shutdown(void);
void sys_reboot(void);
uint32_t sys_getpid(void);
#endif