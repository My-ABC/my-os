#ifndef _SYSCALL_H
#define _SYSCALL_H

// 系统调用号
#define SYS_WRITE    1   // 向 VGA 输出字符串
#define SYS_READ     2   // 从键盘读取字符串（带回显）
#define SYS_GETCHAR  3   // 从键盘读取单个字符（阻塞）
#define SYS_CLEAR    4   // 清屏
#define SYS_REBOOT   5   // 重启系统
#define SYS_TIME     6   // 获取 RTC 时间

#endif