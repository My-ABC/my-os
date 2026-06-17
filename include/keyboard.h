#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "stdint.h"

#define KEYBOARD_PORT 0x60
#define KEYBOARD_IRQ  1

void keyboard_init(void);
char keyboard_getchar(void);
int keyboard_has_key(void);
uint8_t keyboard_read_scan(void);
char scan_to_ascii(uint8_t scancode);

// 供 isr.c 调用的中断处理函数
void keyboard_handler(void);

#endif