#ifndef _TRAP_H
#define _TRAP_H

#include "stdint.h"

// 寄存器状态结构
struct registers {
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;
    uint32_t eip, cs, eflags, ss, ds, es, fs, gs;
};

// 注册异常处理
void trap_init(void);
void int3_breakpoint(void);
void kernel_panic(const char* message, struct registers* regs);

#endif