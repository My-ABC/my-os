#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "stdint.h"

#define KEYBOARD_DATA_PORT 0x60
#define KEYBOARD_COMMAND_PORT 0x64

// 扩展方向键以控制字节形式进入按键缓冲区, 不会与可打印 ASCII 冲突
#define KEYBOARD_KEY_UP    0x01
#define KEYBOARD_KEY_DOWN  0x02
#define KEYBOARD_KEY_LEFT  0x03
#define KEYBOARD_KEY_RIGHT 0x04
#define KEYBOARD_KEY_SIGINT 0x05  /* Ctrl+C */

void keyboard_init(void);

// 设置扫描码集 (1 或 2)
void keyboard_set_scancode_set(int set);

// 获取当前扫描码集
int keyboard_get_scancode_set(void);

// IRQ1 处理: 读扫描码, 按下的可打印键写入缓冲区
void keyboard_irq(void);

// 取一个按键, 缓冲区为空返回 0
char keyboard_getchar(void);

// 阻塞等待一个按键
char keyboard_wait_key(void);

// 检查是否有按键等待读取 (非阻塞)
int keyboard_hit(void);

#endif
