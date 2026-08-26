#ifndef _PAGING_H
#define _PAGING_H

#include "stdint.h"

// 页大小（4KB）
#define PAGE_SIZE 4096

// 低 3GB 为用户地址空间，高 1GB 为内核地址空间
#define USER_SPACE_END 0xC0000000U
#define KERNEL_SPACE_BASE USER_SPACE_END

// 页目录和页表的索引位数
#define PD_INDEX_BITS 10
#define PT_INDEX_BITS 12

// 页目录项数量（1024）
#define PD_ENTRIES 1024

// 页表项数量（1024）
#define PT_ENTRIES 1024

// 页目录项结构体（32位）
struct page_directory_entry {
    uint32_t present    : 1;    // P位：页是否存在
    uint32_t writable   : 1;    // R/W位：是否可写
    uint32_t user       : 1;    // U/S位：用户/特权级
    uint32_t pwt        : 1;    // PWT：写穿透
    uint32_t pcd        : 1;    // PCD：缓存禁用
    uint32_t accessed   : 1;    // A位：是否被访问
    uint32_t reserved   : 1;    // 保留位
    uint32_t page_size  : 1;    // PS位：页大小（0=4KB，1=4MB）
    uint32_t global     : 1;    // G位：全局页
    uint32_t available  : 3;    // 可用位
    uint32_t frame_addr : 20;   // 页表物理地址高20位
} __attribute__((packed));

// 页表项结构体（32位）
struct page_table_entry {
    uint32_t present    : 1;    // P位：页是否存在
    uint32_t writable   : 1;    // R/W位：是否可写
    uint32_t user       : 1;    // U/S位：用户/特权级
    uint32_t pwt        : 1;    // PWT：写穿透
    uint32_t pcd        : 1;    // PCD：缓存禁用
    uint32_t accessed   : 1;    // A位：是否被访问
    uint32_t dirty      : 1;    // D位：是否被修改
    uint32_t pat        : 1;    // PAT：页属性表
    uint32_t global     : 1;    // G位：全局页
    uint32_t available  : 3;    // 可用位
    uint32_t frame_addr : 20;   // 物理页帧号高20位
} __attribute__((packed));

// 页目录结构
struct page_directory {
    struct page_directory_entry entries[PD_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

// 页表结构
struct page_table {
    struct page_table_entry entries[PT_ENTRIES];
} __attribute__((aligned(PAGE_SIZE)));

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

// 获取页目录地址
uint32_t paging_get_page_directory(void);

// 映射虚拟地址到物理地址
void paging_map_page(uint32_t virtual_addr, uint32_t physical_addr, uint32_t flags);

// 解除页映射
void paging_unmap_page(uint32_t virtual_addr);

// 获取虚拟页对应的物理地址；未映射时返回 0
uint32_t paging_get_page_physical(uint32_t virtual_addr);

// 刷新TLB
void paging_flush_tlb(uint32_t virtual_addr);

// 判断虚拟地址是否属于用户空间
uint32_t paging_is_user_address(uint32_t virtual_addr);

#endif