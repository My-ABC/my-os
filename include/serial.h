#ifndef _SERIAL_H
#define _SERIAL_H

#include "stdint.h"

// 串口端口
#define COM1 0x3F8
#define COM2 0x2F8
#define COM3 0x3E8
#define COM4 0x2E8

// 初始化串口
void serial_init(int port);

// 发送字符
void serial_putchar(int port, char c);

// 发送字符串
void serial_write(int port, const char* str);

// 接收字符
char serial_getchar(int port);

// 检查是否有数据
int serial_has_data(int port);

#endif