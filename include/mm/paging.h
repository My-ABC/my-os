#ifndef _PAGING_H
#define _PAGING_H

#include "stdint.h"

// 页大小（4KB）
#define PAGE_SIZE 4096

// 低 3GB 为用户地址空间，高 1GB 为内核地址空间
#define USER_SPACE_END 0xC0000000U
#define KERNEL_SPACE_BASE USER_SPACE_END

// 页目录和页表的索引位数
#define PD_INDEX_BITS 9
#define PT_INDEX_BITS 12

// 页目录项数量（1024）
#define PD_ENTRIES 512

// 页表项数量（1024）
#define PT_ENTRIES 512

/*
 * PAE 64-bit entries.  The kernel remains 32-bit, and PMM exposes only the
 * 36-bit physical addresses; the allocator enforces a 64 GiB ceiling.
 */
struct page_directory_entry {
    uint64_t value;
} __attribute__((aligned(8)));

struct page_table_entry {
    uint64_t value;
} __attribute__((aligned(8)));

struct page_directory_pointer {
    uint64_t entries[4];
} __attribute__((aligned(32)));

struct page_directory {
    struct page_directory_entry entries[PT_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

struct page_table {
    struct page_table_entry entries[PT_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

#define PAE_PRESENT       (1ULL << 0)
#define PAE_WRITABLE      (1ULL << 1)
#define PAE_USER          (1ULL << 2)
#define PAE_ACCESSED      (1ULL << 5)
#define PAE_DIRTY         (1ULL << 6)
#define PAE_PAGE_SIZE     (1ULL << 7)
#define PAE_NX            (1ULL << 63)

// 分页标志
#define PAGE_PRESENT    0x01    // 页存在
#define PAGE_WRITABLE   0x02    // 页可写
#define PAGE_USER       0x04    // 用户态可访问
#define PAGE_GLOBAL     0x100   // 全局页

// 对齐到页边界
#define PAGE_ALIGN(addr) (((addr) + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1))

// 初始化分页
void paging_init(void);

// 启用分页（汇编接口）
extern void paging_enable(uint32_t page_dir_addr);
/* One-page, single-slot kernel window. Do not nest; unmap before reuse.
   Interrupts are disabled while the slot is mapped to prevent preemption. */
void *paging_map_physical(uint64_t physical_addr);
void paging_unmap_physical(void);

// 获取页目录地址
uint32_t paging_get_page_directory(void);

// 映射虚拟地址到物理地址
void paging_map_page(uint32_t virtual_addr, uint64_t physical_addr, uint32_t flags);

// 解除页映射
void paging_unmap_page(uint32_t virtual_addr);

// 获取虚拟页对应的物理地址；未映射时返回 0
uint64_t paging_get_page_physical(uint32_t virtual_addr);

// 刷新TLB
void paging_flush_tlb(uint32_t virtual_addr);

// 判断虚拟地址是否属于用户空间
uint32_t paging_is_user_address(uint32_t virtual_addr);

#endif