#ifndef _SYSCALL_H
#define _SYSCALL_H

#include "stdint.h"

// 系统调用 ABI：eax = 大类（SYS_*），ebx = 子操作号，
// 参数依次为 ecx、edx、esi（最多 3 个），返回值在 eax。

// 大类
#define SYS_IO       1   // 控制台 / 键盘输入输出
#define SYS_TIME     2   // 时间
#define SYS_MEMORY   3   // 用户堆内存
#define SYS_POWER    4   // 电源
#define SYS_PROCESS  5   // 进程
#define SYS_FS       6   // 文件系统
#define SYS_SIG      7   // 信号

// SYS_IO 子操作
#define IO_WRITE    1   // ecx: const char *str —— 向 VGA+串口输出字符串
#define IO_GETCHAR  2   // 阻塞读取单个字符，返回字符
#define IO_READ     3   // ecx: char *buf, edx: size —— 读取一行（带回显）
#define IO_CLEAR    4   // 清屏
#define IO_CURSOR_GET 5 // 返回 (row<<16)|col
#define IO_CURSOR_SET 6 // ecx: row, edx: col —— 设置光标位置

// SYS_TIME 子操作
#define TIME_GET    1   // 返回 Unix 时间戳（UTC）
#define TIME_TO     2   // ecx: int32 偏移秒数, edx: rtc_time_t *buf —— 换算到 UTC±n
#define TIME_SLEEP  3   // ecx: uint32 微秒数 —— 高精度定时等待（忙等 TSC）

// SYS_MEMORY 子操作
#define MEMORY_MALLOC  1   // ecx: size
#define MEMORY_CALLOC  2   // ecx: nmemb, edx: size
#define MEMORY_REALLOC 3   // ecx: ptr, edx: size
#define MEMORY_FREE    4   // ecx: ptr

// SYS_POWER 子操作
#define PWR_SHUTDOWN 1
#define PWR_REBOOT   2

// SYS_PROCESS 子操作
#define PROC_GETPID  1   // 返回当前 Ring 3 进程 ID
#define PROC_EXIT    2   // ecx: 退出码（暂不记录）—— 退出当前进程
#define PROC_YIELD   3   // 主动让出 CPU
#define PROC_LIST    4   // ecx: buf, edx: size —— 枚举进程（每项 12 字节: pid/state/fg）
#define PROC_SET_FG  5   // ecx: pid（0=取消前台）—— 设置前台进程
#define PROC_EXEC    6   // ecx: path —— 从 FAT32 加载 ELF32 并以前台运行

// SYS_FS 子操作
#define FS_OPEN    1   // ecx: path, edx: mode —— 返回 fd
#define FS_CLOSE   2   // ecx: fd
#define FS_READ    3   // ecx: fd, edx: buf, esi: size
#define FS_WRITE   4   // ecx: fd, edx: buf, esi: size
#define FS_UNLINK  5   // ecx: path —— 删除文件
#define FS_MKDIR   6   // ecx: path —— 创建目录
#define FS_RMDIR   7   // ecx: path —— 删除空目录
#define FS_STAT    8   // ecx: path —— 返回大小，目录为 -2
#define FS_GETCWD  9   // ecx: buf, edx: size —— 获取当前目录
#define FS_CHDIR   10  // ecx: path —— 切换当前目录
#define FS_RENAME  11  // ecx: old path, edx: new path —— 移动/重命名
#define FS_READDIR 12  // ecx: path, edx: buf, esi: size —— 列目录（每行 "d/- name"）

// SYS_SIG 子操作
#define SIG_KILL    1   // ecx: pid, edx: sig —— 发送信号
#define SIG_SIGNAL  2   // ecx: sig, edx: handler —— 注册处理器
#define SIG_RETURN  3   // 信号处理器返回，恢复被中断的上下文（由 restorer 调用）

#define SYSFS_O_RDONLY 0
#define SYSFS_O_RDWR   1

// 用户堆虚拟地址范围（供页错误按需映射做范围判定）
#define USER_HEAP_START 0x01000000U
#define USER_HEAP_END   0x40000000U
uint32_t sys_user_heap_contains(uint32_t addr);

uint32_t sys_call(uint32_t klass, uint32_t op,
                  uint32_t arg1, uint32_t arg2, uint32_t arg3);
void *sys_malloc(uint32_t size);
void *sys_calloc(uint32_t nmemb, uint32_t size);
void *sys_realloc(void *ptr, uint32_t size);
void sys_free(void *ptr);
void sys_shutdown(void);
void sys_reboot(void);
uint32_t sys_getpid(void);
#endif
