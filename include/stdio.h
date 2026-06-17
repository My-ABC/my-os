#ifndef _STDIO_H
#define _STDIO_H

#include "vga.h"
#include "stdarg.h"

void print(const char* str);
void print_color(const char* str, uint8_t color);
void putchar(char c);

void print_info(const char* str);
void print_error(const char* str);
void print_warning(const char* str);

char getchar(void);
int has_key(void);
void scan(char* buffer, int max_len);

// ===== 新增 =====
void printf(const char* fmt, ...);
int scanf(const char* fmt, ...);
int vscanf(const char* fmt, va_list args);
int sscanf(const char* str, const char* fmt, ...);

#endif