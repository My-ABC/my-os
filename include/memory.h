#ifndef _MEMORY_H
#define _MEMORY_H

#include "stdint.h"
#include "multiboot.h"

// 初始化物理内存管理器
void pmm_init(struct multiboot_info* info);

// 分配一页物理内存（返回物理地址）
uint32_t pmm_alloc_page(void);

// 释放一页物理内存
void pmm_free_page(uint32_t addr);

// 获取内存统计信息
void pmm_get_stats(uint32_t* total, uint32_t* free, uint32_t* used);

#endif