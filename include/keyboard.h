#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "stdint.h"

#define KEYBOARD_DATA_PORT 0x60

void keyboard_init(void);

// IRQ1 处理: 读扫描码, 按下的可打印键写入缓冲区
void keyboard_irq(void);

// 取一个按键, 缓冲区为空返回 0
char keyboard_getchar(void);

// 阻塞等待一个按键
char keyboard_wait_key(void);

#endif
