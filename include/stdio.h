#ifndef _STDIO_H
#define _STDIO_H

#include "stdarg.h"
#include "stdint.h"

// Output functions
int putchar(char c);
int puts(const char* str);

// Formatted output functions
int printf(const char* format, ...);
int sprintf(char* str, const char* format, ...);

// Output to serial console
int serial_printf(const char* format, ...);

// Input functions
int getchar(void);          // 从键盘获取一个字符（阻塞）
char *gets(char *buf, int size);  // 读取一行字符串（带缓冲区大小）

void printd(const char* topic, const char* msg);

#endif
