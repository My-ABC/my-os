#ifndef _PANIC_H
#define _PANIC_H

#include "regs.h"

#define PANIC_REBOOT_SECONDS 5

/* Set while panic_blue_screen is active: ring3_timer_switch must not
 * switch away from the panicking context. */
extern volatile int panic_in_progress;

// 蓝屏: 蓝底显示异常信息与寄存器, 倒计时后自动重启
void panic_blue_screen(const char* message, struct registers* regs);

#endif
