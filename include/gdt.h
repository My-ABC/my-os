#ifndef _GDT_H
#define _GDT_H

#include "stdint.h"

// GDT 段描述符结构体
struct gdt_entry {
    uint16_t limit_low;    // 段限制低 16 位
    uint16_t base_low;     // 基地址低 16 位
    uint8_t  base_middle;  // 基地址中间 8 位
    uint8_t  access;       // 访问权限
    uint8_t  granularity;  // 粒度和限制高 4 位
    uint8_t  base_high;    // 基地址高 8 位
} __attribute__((packed));

// GDT 指针结构体（用于 lgdt 指令）
struct gdt_ptr {
    uint16_t limit;        // GDT 限制
    uint32_t base;         // GDT 基地址
} __attribute__((packed));

// 段选择子宏
#define GDT_NULL_SELECTOR  0x00
#define GDT_CODE_SELECTOR  0x08
#define GDT_DATA_SELECTOR  0x10
#define GDT_TSS_SELECTOR   0x18

// 访问权限字节定义
#define GDT_ACCESS_PRESENT        0x80  // P 位：段存在
#define GDT_ACCESS_RING0          0x00  // DPL：特权级 0
#define GDT_ACCESS_RING3          0x60  // DPL：特权级 3
#define GDT_ACCESS_SYSTEM          0x00  // S 位：系统段
#define GDT_ACCESS_CODE_DATA      0x10  // S 位：代码/数据段
#define GDT_ACCESS_CODE           0x0A  // 类型：可执行代码段
#define GDT_ACCESS_DATA           0x02  // 类型：可读写数据段
#define GDT_ACCESS_CODE_READABLE  0x0A  // 类型：可读代码段
#define GDT_ACCESS_DATA_WRITABLE  0x02  // 类型：可写数据段

// 粒度字节定义
#define GDT_GRANULARITY_4KB       0x80  // G 位：4KB 粒度
#define GDT_GRANULARITY_BYTE      0x00  // G 位：字节粒度
#define GDT_GRANULARITY_32BIT     0x40  // D 位：32 位段
#define GDT_GRANULARITY_16BIT     0x00  // D 位：16 位段

// 初始化 GDT
void gdt_init(void);

// 设置 GDT 条目
void gdt_set_entry(int index, uint32_t base, uint32_t limit, uint8_t access, uint8_t granularity);

#endif