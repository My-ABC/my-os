#ifndef _KEYBOARD_H
#define _KEYBOARD_H

#include "stdint.h"

void keyboard_init(void);
char keyboard_getchar(void);
int keyboard_has_key(void);
void keyboard_handler(void);  // 中断处理函数

#endif