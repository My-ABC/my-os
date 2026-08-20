#ifndef _TSS_H
#define _TSS_H

#include "stdint.h"

struct tss_t {
    uint32_t link;        // 硬件任务
    uint32_t esp0;        // 内核栈
    uint16_t ss0;         // 内核数据段
    uint16_t reserved1;   // 必须为0
    uint32_t esp1;        // Ring1栈
    uint16_t ss1;         // Ring1数据段
    uint16_t reserved2;   // 必须为0
    uint32_t esp2;        // Ring2栈
    uint16_t ss2;         // Ring2数据段
    uint16_t reserved3;   // 必须为0
    uint32_t cr3;         // 硬件任务
    uint32_t eip;         // 寄存器
    uint32_t eflags;      // flags
    uint32_t eax;         // 寄存器
    uint32_t ecx;
    uint32_t edx;
    uint32_t ebx;
    uint32_t esp;
    uint32_t ebp;
    uint32_t esi;
    uint32_t edi;
    uint32_t es;           // 段
    uint32_t cs;
    uint32_t ss;
    uint32_t ds;
    uint32_t fs;
    uint32_t gs;
    uint32_t ldt;          // LDT
    uint16_t trap;         // trap
    uint16_t reserved4;    // 必须为0
    uint16_t io_map_base;  // 建议为sizeof(struct tss_t)
} __attribute__((packed));

void tss_init(void);

#endif