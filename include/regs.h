#ifndef _REGS_H
#define _REGS_H

#include "stdint.h"

// 中断发生时保存的寄存器上下文, 顺序与 boot/interrupt.asm 的压栈顺序一致
struct registers {
    uint32_t ds;
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pusha
    uint32_t int_no, err_code;
    uint32_t eip, cs, eflags;
} __attribute__((packed));

#endif
