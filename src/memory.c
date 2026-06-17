#include "memory.h"
#include "multiboot.h"
#include "stdio.h"
#include "string.h"

// 物理内存管理器
#define PAGE_SIZE 4096
#define PAGE_ALIGN_MASK (~(PAGE_SIZE - 1))

// 最大物理内存 4GB（32 位）
#define MAX_PHYS_MEM 0x100000000ULL
#define MAX_PAGES (MAX_PHYS_MEM / PAGE_SIZE)

// 位图（每 bit 代表一个 4KB 页，1=已使用，0=空闲）
static uint8_t page_bitmap[MAX_PAGES / 8];
static uint32_t total_pages = 0;
static uint32_t free_pages = 0;
static uint32_t used_pages = 0;

// 物理内存起始地址（内核结束后的第一个可用页）
static uint32_t phys_mem_start = 0;

// 获取 Multiboot 信息指针
struct multiboot_info* multiboot_info = 0;

// 设置位图中的位
static void bitmap_set(uint32_t page) {
    page_bitmap[page / 8] |= (1 << (page % 8));
}

// 清除位图中的位
static void bitmap_clear(uint32_t page) {
    page_bitmap[page / 8] &= ~(1 << (page % 8));
}

// 检查位图中的位
static int bitmap_test(uint32_t page) {
    return page_bitmap[page / 8] & (1 << (page % 8));
}

// 初始化物理内存管理器
void pmm_init(struct multiboot_info* info) {
    multiboot_info = info;
    
    print_info("Initializing Physical Memory Manager...\n");
    
    // 清空位图
    memset(page_bitmap, 0, sizeof(page_bitmap));
    
    // 解析 Multiboot 内存映射
    if (!(info->flags & (1 << 6))) {
        print_error("No memory map provided by bootloader!\n");
        return;
    }
    
    uint32_t mmap_addr = info->mmap_addr;
    uint32_t mmap_length = info->mmap_length;
    uint32_t total_ram = 0;
    uint32_t max_addr = 0;
    
    print_info("Memory map entries:\n");
    
    struct multiboot_mmap_entry* entry = (struct multiboot_mmap_entry*)mmap_addr;
    while ((uint32_t)entry < mmap_addr + mmap_length) {
        uint64_t addr = entry->addr;
        uint64_t len = entry->len;
        uint32_t type = entry->type;
        
        // 打印内存区域信息
        print_info("  addr=0x");
        char buf[16];
        uint32_t a = (uint32_t)addr;
        const char* hex = "0123456789ABCDEF";
        for (int i = 7; i >= 0; i--) {
            buf[i] = hex[a & 0xF];
            a >>= 4;
        }
        buf[8] = '\0';
        print_info(buf);
        print_info(" len=0x");
        a = (uint32_t)len;
        for (int i = 7; i >= 0; i--) {
            buf[i] = hex[a & 0xF];
            a >>= 4;
        }
        buf[8] = '\0';
        print_info(buf);
        print_info(" type=");
        char type_buf[4];
        int n = type;
        if (n < 10) {
            type_buf[0] = '0' + n;
            type_buf[1] = '\0';
        } else if (n < 100) {
            type_buf[0] = '0' + (n / 10);
            type_buf[1] = '0' + (n % 10);
            type_buf[2] = '\0';
        } else {
            type_buf[0] = '0' + (n / 100);
            type_buf[1] = '0' + ((n / 10) % 10);
            type_buf[2] = '0' + (n % 10);
            type_buf[3] = '\0';
        }
        print_info(type_buf);
        print_info("\n");
        
        // 只使用可用内存 (type == 1)
        if (type == MULTIBOOT_MEMORY_AVAILABLE) {
            if (addr + len > max_addr) {
                max_addr = addr + len;
            }
            total_ram += len;
        }
        
        entry = (struct multiboot_mmap_entry*)((uint32_t)entry + entry->size + 4);
    }
    
    // 计算总页数
    total_pages = total_ram / PAGE_SIZE;
    free_pages = total_pages;
    used_pages = 0;
    
    // 标记已使用的内存（内核本身占用）
    // 内核从 1MB 开始，到 max_addr 结束
    // 我们简单地标记 0-4MB 为已使用
    uint32_t kernel_end = 4 * 1024 * 1024;  // 4MB
    uint32_t kernel_pages = kernel_end / PAGE_SIZE;
    for (uint32_t i = 0; i < kernel_pages && i < total_pages; i++) {
        bitmap_set(i);
        free_pages--;
        used_pages++;
    }
    
    // 找到第一个可用页（从 4MB 开始）
    phys_mem_start = 4 * 1024 * 1024;
    
    print_info("Total RAM: ");
    char buf[16];
    uint32_t n = total_ram / (1024 * 1024);
    if (n < 10) {
        buf[0] = '0' + n;
        buf[1] = '\0';
    } else if (n < 100) {
        buf[0] = '0' + (n / 10);
        buf[1] = '0' + (n % 10);
        buf[2] = '\0';
    } else {
        buf[0] = '0' + (n / 100);
        buf[1] = '0' + ((n / 10) % 10);
        buf[2] = '0' + (n % 10);
        buf[3] = '\0';
    }
    print_info(buf);
    print_info(" MB\n");
    
    print_info("Total pages: ");
    n = total_pages;
    if (n < 10) {
        buf[0] = '0' + n;
        buf[1] = '\0';
    } else if (n < 100) {
        buf[0] = '0' + (n / 10);
        buf[1] = '0' + (n % 10);
        buf[2] = '\0';
    } else if (n < 1000) {
        buf[0] = '0' + (n / 100);
        buf[1] = '0' + ((n / 10) % 10);
        buf[2] = '0' + (n % 10);
        buf[3] = '\0';
    } else {
        buf[0] = '0' + (n / 1000);
        buf[1] = '0' + ((n / 100) % 10);
        buf[2] = '0' + ((n / 10) % 10);
        buf[3] = '0' + (n % 10);
        buf[4] = '\0';
    }
    print_info(buf);
    print_info("\n");
    
    print_info("Free pages: ");
    n = free_pages;
    if (n < 10) {
        buf[0] = '0' + n;
        buf[1] = '\0';
    } else if (n < 100) {
        buf[0] = '0' + (n / 10);
        buf[1] = '0' + (n % 10);
        buf[2] = '\0';
    } else if (n < 1000) {
        buf[0] = '0' + (n / 100);
        buf[1] = '0' + ((n / 10) % 10);
        buf[2] = '0' + (n % 10);
        buf[3] = '\0';
    } else {
        buf[0] = '0' + (n / 1000);
        buf[1] = '0' + ((n / 100) % 10);
        buf[2] = '0' + ((n / 10) % 10);
        buf[3] = '0' + (n % 10);
        buf[4] = '\0';
    }
    print_info(buf);
    print_info("\n");
    
    print_info("PMM initialized\n");
}

// 分配一页物理内存
uint32_t pmm_alloc_page(void) {
    for (uint32_t i = 0; i < total_pages; i++) {
        if (!bitmap_test(i)) {
            bitmap_set(i);
            free_pages--;
            used_pages++;
            return i * PAGE_SIZE;
        }
    }
    return 0;  // 没有可用内存
}

// 释放一页物理内存
void pmm_free_page(uint32_t addr) {
    if (addr % PAGE_SIZE != 0) return;
    uint32_t page = addr / PAGE_SIZE;
    if (page >= total_pages) return;
    if (bitmap_test(page)) {
        bitmap_clear(page);
        free_pages++;
        used_pages--;
    }
}

// 获取内存统计信息
void pmm_get_stats(uint32_t* total, uint32_t* free, uint32_t* used) {
    if (total) *total = total_pages;
    if (free) *free = free_pages;
    if (used) *used = used_pages;
}