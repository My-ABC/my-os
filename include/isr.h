#ifndef _ISR_H
#define _ISR_H

#include "stdint.h"

// 声明为外部变量
extern volatile uint32_t system_ticks;
extern volatile uint32_t system_seconds;

// 系统时间
uint32_t get_ticks(void);
uint32_t get_seconds(void);
void sleep(uint32_t ms);

// PIC 初始化
void pic_init(void);

#endif